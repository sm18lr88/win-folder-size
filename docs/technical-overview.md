# Technical Overview

## How It Works

FolderSize is a COM shell extension DLL registered with `regsvr32`. It installs six hooks into Explorer via [Microsoft Detours](https://github.com/microsoft/detours):

| Hook | Target | Purpose |
|------|--------|---------|
| `CFSFolder::_GetSize` | `windows.storage.dll` | Intercepts folder size queries and supplies completed scanner results |
| `CRecursiveFolderOperation::Prepare/Do` | `windows.storage.dll` | RAII guard: suppresses size injection during copy/move/delete |
| `PSFormatForDisplayAlloc` | `propsys.dll` | Human-readable formatting (B/KB/MB/GB/TB) instead of "1,572,864 KB" |
| `PSFormatForDisplay` | `propsys.dll` | Same, for older Open/Save dialogs |
| `RegQueryValueExW` | `kernelbase.dll` | Injects `System.Size` into Explorer's property format strings so sizes appear in Tiles, Content, Details-pane, and status-bar views |

A background thread (`SHChangeNotifyRegister`) watches for filesystem changes and invalidates cache entries, including all ancestor directories. It also asks Explorer to re-query affected folder rows after invalidation so visible sizes update promptly after file operations.

Final folder sizes come from direct filesystem scanning. The scanner runs off-thread, and only completed scans are cached or displayed as finished sizes. UNC and other unsupported paths stay blank rather than showing guessed totals. Access-denied or cancelled scans also stay blank after the in-flight work ends.

The cache is fresh-only. `SizeCache::get()` returns a value only while it is inside the TTL; expired values are misses and must be recomputed. Change notifications are still useful because they invalidate affected folders and ancestors before the TTL expires, but correctness doesn't depend on receiving every notification.

Cache misses are queued onto a bounded background worker so Explorer can return immediately. While a lookup is queued or running, the Size column can show `Pending`. `Pending` means background work is in progress. It is not a completed size, and it isn't cached as one. Visible cached folders are also refreshed periodically in the background so missed shell notifications do not leave Explorer stuck on stale sizes until the full cache TTL expires.

[Everything](https://www.voidtools.com/) IPC code remains in the tree for diagnostics, legacy paths, or future acceleration work, but it is optional and not a source of final displayed sizes in the current correctness model.

For a full breakdown of each hook target, symbol resolution strategy, and reversibility guarantees, see [`hook-targets.md`](hook-targets.md).

---

## Architecture

```
src/
  com/          # COM class factory, DLL exports
  core/         # Init, logging, size formatting, LRU cache, change notifier
  hooks/        # Detours wrapper, hook manager, RegQueryValueExW hook
  providers/    # Direct folder scanner, optional Everything IPC client
include/        # Shared headers (logging, GUIDs, optional Everything IPC types)
tests/          # GTest unit tests (formatter, cache)
docs/           # Technical documentation
```

---

## Performance

Microsoft's official position is that folder sizes in Explorer would hurt performance, which was true for a naive recursive directory scan: blocking, O(files), disk I/O on the UI thread.

FolderSize avoids that UI-thread cost. The Explorer hook performs cache lookup and queues background work, while direct scanning runs off-thread. Large folders can still take time to scan, especially on slow disks or when Windows denies traversal, but Explorer shouldn't block on provider I/O.

The previous Everything-backed path was optimized for indexed IPC, but that path is optional and doesn't supply final displayed sizes in this correctness model. The current provider favors correctness over guessed speed: completed direct scans can be shown, incomplete scans cannot.

The LRU cache (50 MB, 5-min TTL) makes fresh repeat visits fast. Cache misses are deduplicated and capped so opening a folder with thousands of heavy subfolders doesn't fan out into unbounded work. Cache entries are invalidated when shell file-change notifications arrive, and expired entries are treated as misses if a notification is delayed or missed.

Microsoft's concern is legitimate for a naive implementation. FolderSize keeps recursive work off Explorer's hot path and treats blank or pending as safer than stale or partial completed sizes.

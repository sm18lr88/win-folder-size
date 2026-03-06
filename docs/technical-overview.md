# Technical Overview

## How It Works

FolderSize is a COM shell extension DLL registered with `regsvr32`. It installs six hooks into Explorer via [Microsoft Detours](https://github.com/microsoft/detours):

| Hook | Target | Purpose |
|------|--------|---------|
| `CFSFolder::_GetSize` | `windows.storage.dll` | Intercepts folder size queries; returns size from Everything |
| `CRecursiveFolderOperation::Prepare/Do` | `windows.storage.dll` | RAII guard: suppresses size injection during copy/move/delete |
| `PSFormatForDisplayAlloc` | `propsys.dll` | Human-readable formatting (B/KB/MB/GB/TB) instead of "1,572,864 KB" |
| `PSFormatForDisplay` | `propsys.dll` | Same, for older Open/Save dialogs |
| `RegQueryValueExW` | `kernelbase.dll` | Injects `System.Size` into Explorer's property format strings so sizes appear in Tiles, Content, Details-pane, and status-bar views |

A background thread (`SHChangeNotifyRegister`) watches for filesystem changes and invalidates stale cache entries — including all ancestor directories — so sizes stay current after file operations.

Folder sizes come from [Everything](https://www.voidtools.com/) via named pipe IPC (pre-indexed, sub-millisecond per query). For non-NTFS drives, a fallback recursive scanner runs off-thread with a short timeout. Cache misses are queued onto a bounded background worker so Explorer can return immediately; while a lookup is in flight, the Size column can show `Pending`. If Everything isn't running, folders eventually fall back to blank — same as stock Explorer.

For a full breakdown of each hook target, symbol resolution strategy, and reversibility guarantees, see [`hook-targets.md`](hook-targets.md).

---

## Architecture

```
src/
  com/          # COM class factory, DLL exports
  core/         # Init, logging, size formatting, LRU cache, change notifier
  hooks/        # Detours wrapper, hook manager, RegQueryValueExW hook
  providers/    # Everything IPC client, fallback folder scanner
include/        # Shared headers (logging, GUIDs, Everything IPC types)
tests/          # GTest unit tests (formatter, cache)
docs/           # Technical documentation
```

---

## Performance

Microsoft's official position is that folder sizes in Explorer would hurt performance — which was true when they last evaluated it, because the only viable approach at the time was a recursive directory scan: blocking, O(files), disk I/O on the UI thread.

Everything changes that. Sizes come from a pre-built in-memory index via named pipe IPC. Measured on a real session browsing `node_modules` directories with hundreds of packages:

| Metric | Value |
|--------|-------|
| Average IPC round-trip | 0.6 ms |
| Worst case | 4 ms |
| Sub-millisecond queries | 77% |
| Queries measured | 78 |
| Peak throughput | ~600 queries/sec |
| Explorer size hook blocked on provider I/O | Never |

The LRU cache (50 MB, 5-min TTL) means repeat visits to the same folder are instant — no IPC at all. Cache misses are deduplicated and capped so opening a folder with thousands of heavy subfolders does not fan out into unbounded work. Cache entries are invalidated immediately when files change, so sizes never go stale.

Microsoft's concern is legitimate for a naive implementation. This isn't one.

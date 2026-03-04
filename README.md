# FolderSize

Display folder sizes in Windows Explorer's Size column via a lightweight COM shell extension.

## How It Works

FolderSize is a COM shell extension DLL registered with `regsvr32`. It hooks Explorer's internal `CFSFolder::_GetSize` function (inside `windows.storage.dll`) using [Microsoft Detours](https://github.com/microsoft/detours). When Explorer requests a folder's size, the hook queries [Everything](https://www.voidtools.com/) via named pipe IPC and returns the result instantly.

Sizes are injected into Explorer's native property pipeline, so they sort, format, and display exactly like file sizes. Formatting is human-readable (B, KB, MB, GB, TB) rather than Explorer's default "1,572,864 KB" style. RAII guard hooks prevent size injection during file operations (copy, move, delete) to avoid interfering with Explorer's own size calculations. If Everything isn't running, the extension fails silently and Explorer shows blank sizes, the same as stock behavior.

## Prerequisites

**Runtime**

- Windows 10/11 x64
- [Everything](https://www.voidtools.com/) v1.4+ running with folder size indexing enabled

**Build**

- Visual Studio 2022+ (MSVC)
- CMake 3.20+
- vcpkg with `VCPKG_ROOT` set

## Quick Install

1. Download `foldersize-vX.X.X-win64.zip` from [Releases](../../releases)
2. Extract and run `install.bat` **as Administrator**
3. Folder sizes appear in Explorer's Size column immediately

## Building from Source

**Requirements**: Visual Studio 2022+, CMake 3.20+, vcpkg with `VCPKG_ROOT` set

```batch
git clone https://github.com/YOUR_USERNAME/win-folder-size.git
cd win-folder-size
scripts\build.bat          :: Release build
scripts\build.bat Debug    :: Debug build (verbose logging)
```

Output: `build\Release\foldersize.dll`

## Install / Uninstall / Status

All scripts require **Administrator** privileges.

```batch
scripts\install.bat        :: Registers DLL via regsvr32, restarts Explorer
scripts\uninstall.bat      :: Unregisters via regsvr32 /u, restarts Explorer
scripts\status.bat         :: Checks COM registration, DLL loaded in Explorer, Everything running
```

To uninstall manually without the script:

```batch
regsvr32 /u foldersize.dll
```

## Comparison with Windhawk

There's a [Windhawk mod](https://windhawk.net/) that does something similar. Here's how this project differs:

| Aspect | Windhawk mod | FolderSize (this project) |
|--------|--------------|---------------------------|
| Guard mechanism | `thread_local bool` (no RAII, can leak on exception) | RAII `RecursiveOpGuard` (exception-safe, depth-counter) |
| Cache | Ad-hoc `std::map` with tick-based expiry | Thread-safe LRU cache with TTL, bounded memory (50MB) |
| Error handling | `Wh_Log` only | Multi-output logging (DebugView + file) with source location |
| Hook framework | Windhawk API (proprietary DLL injection) | Microsoft Detours (officially supported) |
| Loading mechanism | Windhawk service (flagged as virus by some AV) | Standard COM registration (`regsvr32`) — no third-party service |
| Uninstallation | Requires Windhawk uninstall | `regsvr32 /u foldersize.dll` — one command |
| SEH protection | None — hooks can crash Explorer | Every hook wrapped in `__try/__except` |
| Global state | Many globals (`g_settings`, `g_cache*`) | Meyers singletons (thread-safe by C++11 standard) |
| Process exclusion | Manual exclude list | Only loads into Explorer (COM registration is Explorer-specific) |
| Compatibility | Requires Windhawk framework installed | Pure Windows API, no third-party dependencies at runtime |
| Footprint | Windhawk service + mod | Single DLL (~1.5 MB) |

See [`docs/hook-targets.md`](docs/hook-targets.md) for the full technical analysis.

## Architecture

```
src/
  com/          # COM class factory, DLL exports, shell overlay stub
  core/         # Logging, size formatting, LRU cache, initialization
  hooks/        # Detours wrapper, hook manager (installs/removes hooks)
  providers/    # Everything IPC client, fallback folder scanner
include/        # Public headers (logging, GUIDs, Everything IPC types)
def/            # DLL export definitions
scripts/        # Build, install, uninstall, status scripts
tests/          # GTest unit tests (size formatter, LRU cache)
docs/           # Technical documentation
```

## License

[MIT](LICENSE)

# FolderSize

<img width="500" alt="image" src="https://github.com/user-attachments/assets/5ec3452d-a6f3-438b-9677-b3fbc451919f" />

Display folder sizes in Windows Explorer — Details view, Tiles, Content view, Details pane, and status bar — via a COM shell extension.

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

Folder sizes come from [Everything](https://www.voidtools.com/) via named pipe IPC (pre-indexed, ~3 ms/query). For non-NTFS drives, a fallback recursive scanner runs with a 200 ms timeout. If Everything isn't running, folders show blank — same as stock Explorer.

**Installed = works. Uninstalled = `regsvr32 /u`. No toggles, no modes.**

## Prerequisites

**Runtime**
- Windows 10/11 x64
- [Everything](https://www.voidtools.com/) 1.5a with folder-size indexing enabled

**Build**
- Visual Studio 2022+ (MSVC)
- CMake 3.20+
- vcpkg with `VCPKG_ROOT` set

## Quick Install

1. Download `foldersize-vX.X.X-win64.zip` from [Releases](../../releases)
2. Extract and run `install.bat` **as Administrator**
3. Folder sizes appear immediately

## Building from Source

```batch
git clone https://github.com/sm18lr88/win-folder-size.git
cd win-folder-size
scripts\build.bat          :: Release build
scripts\build.bat Debug    :: Debug build (verbose logging)
```

Output: `build\foldersize.dll`

## Install / Uninstall

All scripts require **Administrator** privileges.

```batch
scripts\install.bat        :: Register DLL + restart Explorer
scripts\uninstall.bat      :: Unregister + restart Explorer
scripts\status.bat         :: Check COM registration and Everything status
```

Manual uninstall: `regsvr32 /u foldersize.dll`

## Acknowledgements

This project was inspired by m417z's excellent [Better file sizes in Explorer details](https://windhawk.net/mods/explorer-details-better-file-sizes) Windhawk mod, which pioneered the hook targets and approach used here. If you're already using Windhawk, that mod is a great option.

## Comparison with Windhawk "Better File Sizes"

| Feature | Windhawk | FolderSize |
|---------|----------|------------|
| Folder sizes in Details view | ✓ | ✓ |
| Folder sizes in Tiles / Content / Details-pane / Status bar | ✓ | ✓ |
| Reparse point / junction / symlink resolution | partial | ✓ (`GetFinalPathNameByHandle`) |
| `PSFormatForDisplay` (legacy dialogs) | ✓ | ✓ |
| Cache invalidation on filesystem changes | ✗ (stale forever) | ✓ (background thread, ancestor cascade) |
| SEH protection on all hooks | ✗ | ✓ |
| RAII recursive-op guard | ✗ | ✓ |
| Bounded LRU cache | ✗ | ✓ (50 MB, 5-min TTL) |
| Loading mechanism | Windhawk service | `regsvr32` — standard COM |
| Uninstallation | Requires Windhawk | `regsvr32 /u` |
| Footprint | Windhawk service + mod | Single DLL, 144 KB |

See [`docs/hook-targets.md`](docs/hook-targets.md) for the full technical analysis.

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

## License

[MIT](LICENSE)

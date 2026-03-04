# FolderSize

Display folder sizes in Windows Explorer.

<img width="500" alt="image" src="https://github.com/user-attachments/assets/5ec3452d-a6f3-438b-9677-b3fbc451919f" />


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

## Comparison with Windhawk "Better File Sizes"

| Feature | Windhawk | FolderSize |
|---------|----------|------------|
| Folder sizes in Details view | ✓ | ✓ |
| Folder sizes in Tiles / Content / Details-pane / Status bar | ✓ | ✓ |
| Reparse point / junction / symlink resolution | partial | ✓ |
| `PSFormatForDisplay` (legacy dialogs) | ✓ | ✓ |
| Cache invalidation on filesystem changes | ✗ | ✓ |
| SEH protection on all hooks | ✗ | ✓ |
| RAII recursive-op guard | ✗ | ✓ |
| Bounded LRU cache | ✗ | ✓ (50 MB, 5-min TTL) |
| Loading mechanism | Windhawk service | `regsvr32` — standard COM |
| Uninstallation | Requires Windhawk | `regsvr32 /u` |
| Extension footprint | Windhawk mod | Single DLL, 144 KB |
| Required runtime | Windhawk | [Everything](https://www.voidtools.com/) |

## Acknowledgements

This project was inspired by m417z's excellent [Better file sizes in Explorer details](https://windhawk.net/mods/explorer-details-better-file-sizes) Windhawk mod, which pioneered the hook targets and approach used here. If you're already using Windhawk, that mod is a great option.

## Technical Details

For how the hooks work, architecture, and performance benchmarks, see [`docs/technical-overview.md`](docs/technical-overview.md).

## License

[MIT](LICENSE)

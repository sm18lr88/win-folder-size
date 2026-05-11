# FolderSize


Display folder sizes in Windows Explorer.

<img width="500" alt="image" src="https://github.com/user-attachments/assets/5ec3452d-a6f3-438b-9677-b3fbc451919f" />


## Prerequisites

**Runtime**
- Windows 10/11 x64

**Optional**
- [Everything](https://www.voidtools.com/) 1.5a may be reported by diagnostics, but folder-size correctness doesn't depend on it.

**Build**
- Visual Studio 2022+ (MSVC)
- CMake 3.20+
- vcpkg with `VCPKG_ROOT` set

## Quick Install

1. Download `foldersize-vX.X.X-win64.zip` from [Releases](../../releases)
2. Extract and run `install.bat` **as Administrator**
3. Folder sizes appear after the background scanner completes each folder
4. Run `status.bat` **as Administrator** to confirm registration and Explorer loading

## Building from Source

```batch
git clone https://github.com/sm18lr88/win-folder-size.git
cd win-folder-size
scripts\build.bat          :: Release build
scripts\build.bat Debug    :: Debug build (verbose logging)
```

Output: `build\Release\foldersize.dll` or `build\Debug\foldersize.dll`.

## Install / Uninstall

All scripts require **Administrator** privileges.

```batch
scripts\install.bat        :: Register DLL + restart Explorer
scripts\uninstall.bat      :: Unregister + restart Explorer
scripts\status.bat         :: Check COM registration and diagnostics
```

Manual uninstall: `regsvr32 /u foldersize.dll`

Release ZIPs are self-contained: `install.bat`, `uninstall.bat`, and `status.bat`
work when run from an extracted release folder next to `foldersize.dll`.

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
| Fresh-only bounded LRU cache | ✗ | ✓ (50 MB, 5-min TTL) |
| Loading mechanism | Windhawk service | `regsvr32` — standard COM |
| Uninstallation | Requires Windhawk | `regsvr32 /u` |
| Extension footprint | Windhawk mod | Single DLL, 144 KB |
| External runtime dependency | Windhawk | None beyond Windows Explorer |

## Release Process

Releases are built by GitHub Actions for transparency. Push a `v*` tag, such as
`v0.2.4`, to trigger `.github/workflows/release.yml`. The workflow builds on
`windows-latest`, runs the test suite, packages the release ZIP, uploads the ZIP
and `.sha256` checksum as workflow artifacts, then attaches both files to the
GitHub Release.

Manual dry-runs can be started from the **Release** workflow in GitHub Actions;
manual runs upload workflow artifacts but do not create a GitHub Release unless
the run is for a pushed tag.

## Acknowledgements

This project was inspired by m417z's excellent [Better file sizes in Explorer details](https://windhawk.net/mods/explorer-details-better-file-sizes) Windhawk mod, which pioneered the hook targets and approach used here. If you're already using Windhawk, that mod is a great option.

## Technical Details

For how the hooks work, architecture, and performance benchmarks, see [`docs/technical-overview.md`](docs/technical-overview.md). For release steps, see [`docs/release.md`](docs/release.md).

## License

[MIT](LICENSE)

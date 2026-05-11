# Release Guide

FolderSize releases are built by GitHub Actions so the downloadable artifact is
traceable to a commit, workflow run, and tag.

## Version Checklist

Before tagging a release, make sure these versions match:

- `src/version.rc` `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and `ProductVersion`
- `vcpkg.json` `version`
- the Git tag, for example `v0.2.5`

## Build Artifact Contents

The release workflow packages:

- `foldersize.dll`
- `foldersize.pdb`
- `install.bat`
- `uninstall.bat`
- `status.bat`
- `README.md`
- `LICENSE`
- `BUILD-INFO.txt`

The workflow also publishes a `foldersize-<tag>-win64.zip.sha256` checksum.

## Release From GitHub Actions

1. Commit the release changes.
2. Push `master` and wait for the `CI` workflow to pass.
3. Create and push a tag:

   ```powershell
   git tag v0.2.5
   git push origin v0.2.5
   ```

4. The `Release` workflow builds and tests on `windows-latest`.
5. If tests pass, the workflow creates a GitHub Release with the ZIP and checksum attached.

## Manual Dry Run

Run the `Release` workflow manually from GitHub Actions to produce workflow
artifacts without creating a GitHub Release. Manual runs use an artifact name like
`foldersize-manual-<run_number>-win64.zip`.

## Local Verification

Local builds are useful before tagging, but they are not the release source of truth:

```batch
scripts\build.bat Release
build\Release\foldersize_tests.exe
ctest --test-dir build -C Release --output-on-failure
```

Install and status scripts require Administrator privileges:

```batch
scripts\install.bat
scripts\status.bat
```

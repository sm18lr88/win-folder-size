@echo off
setlocal
echo Building FolderSize...
echo.

set "BUILD_TYPE=%1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

echo Configuration: %BUILD_TYPE%
echo.

cmake -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" 2>&1
if errorlevel 1 (
    echo FAILED: CMake configuration
    exit /b 1
)

cmake --build build --config %BUILD_TYPE% 2>&1
if errorlevel 1 (
    echo FAILED: Build
    exit /b 1
)

echo.
echo Build complete: build\%BUILD_TYPE%\foldersize.dll
for %%F in ("build\%BUILD_TYPE%\foldersize.dll") do echo Size: %%~zF bytes
echo.
endlocal

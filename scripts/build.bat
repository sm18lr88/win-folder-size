@echo off
setlocal
echo Building FolderSize...
echo.

set "BUILD_TYPE=%1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

set "VS_ROOT="
if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" (
    set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
)

if "%VS_ROOT%"=="" (
    echo FAILED: Visual Studio 18 Enterprise or BuildTools developer prompt not found
    exit /b 1
)

call "%VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
    echo FAILED: Visual Studio developer environment setup
    exit /b 1
)

set "VCPKG_ROOT=%VS_ROOT%\VC\vcpkg"
set "PATH=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VCPKG_ROOT%;%PATH%"

echo Configuration: %BUILD_TYPE%
echo Visual Studio: %VS_ROOT%
echo VCPKG_ROOT: %VCPKG_ROOT%
echo.

if exist build\CMakeCache.txt (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026" build\CMakeCache.txt >nul 2>&1
    if errorlevel 1 (
        echo Existing build directory uses a different CMake generator; refreshing CMake configure metadata...
        del /q build\CMakeCache.txt >nul 2>&1
        if exist build\CMakeFiles rmdir /s /q build\CMakeFiles
    )
)

cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" 2>&1
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

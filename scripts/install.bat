@echo off
setlocal
net session >nul 2>&1 || (echo ERROR: Run as Administrator & exit /b 1)
echo Installing FolderSize...

set "DLL_PATH=%~dp0..\build\Release\foldersize.dll"
if not exist "%DLL_PATH%" (
    set "DLL_PATH=%~dp0..\build\Debug\foldersize.dll"
)
if not exist "%DLL_PATH%" (
    set "DLL_PATH=%~dp0..\build\foldersize.dll"
)
if not exist "%DLL_PATH%" (
    echo ERROR: foldersize.dll not found. Build the project first.
    exit /b 1
)

regsvr32 /s "%DLL_PATH%"
if errorlevel 1 (
    echo FAILED: regsvr32 returned error
    exit /b 1
)

echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
start "" explorer.exe

echo.
echo Done. Folder sizes will appear in Explorer's Size column.
echo Requires Everything (voidtools.com) running with folder size indexing enabled.
endlocal

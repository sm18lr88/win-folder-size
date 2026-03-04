@echo off
setlocal
net session >nul 2>&1 || (echo ERROR: Run as Administrator & exit /b 1)
echo Uninstalling FolderSize...

set "DLL_PATH=%~dp0..\build\Release\foldersize.dll"
if not exist "%DLL_PATH%" (
    set "DLL_PATH=%~dp0..\build\Debug\foldersize.dll"
)
if not exist "%DLL_PATH%" (
    set "DLL_PATH=%~dp0..\build\foldersize.dll"
)
if not exist "%DLL_PATH%" (
    echo ERROR: foldersize.dll not found.
    exit /b 1
)

regsvr32 /u /s "%DLL_PATH%"
if errorlevel 1 (
    echo FAILED: regsvr32 returned error
    exit /b 1
)

echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
start "" explorer.exe

echo.
echo Done. Explorer restored to default behavior.
endlocal

@echo off
setlocal
echo FolderSize Status Check
echo ========================
echo.

:: Check registry
reg query "HKCR\CLSID\{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}\InProcServer32" >nul 2>&1
if %errorlevel% equ 0 (
    echo [REGISTERED] COM class is registered in registry
    reg query "HKCR\CLSID\{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}\InProcServer32" /ve 2>nul
) else (
    echo [NOT REGISTERED] COM class not found in registry
)
echo.

:: Check if DLL is loaded in Explorer
tasklist /m foldersize.dll 2>nul | findstr /i "explorer.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo [LOADED] foldersize.dll is loaded in explorer.exe
) else (
    echo [NOT LOADED] foldersize.dll is not loaded in explorer.exe
)
echo.

:: Check Everything service
tasklist /fi "imagename eq Everything.exe" 2>nul | findstr /i "Everything.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo [RUNNING] Everything.exe is running
) else (
    echo [NOT RUNNING] Everything.exe is not running
    echo WARNING: Folder sizes require Everything to be running
)

echo.
endlocal

@echo off
REM MicMap OpenVR Driver Installation Script
REM This script installs the MicMap driver to SteamVR's drivers directory

setlocal enabledelayedexpansion

echo ============================================
echo MicMap OpenVR Driver Installer
echo ============================================
echo.

REM Check for admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Warning: Not running as administrator.
    echo Some operations may fail without admin privileges.
    echo.
)

REM Find SteamVR installation
set "STEAMVR_PATH="

REM Try common Steam installation paths (check each individually to handle spaces)
if exist "C:\Program Files (x86)\Steam\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
    goto :found_steamvr
)

if exist "C:\Program Files\Steam\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=C:\Program Files\Steam\steamapps\common\SteamVR"
    goto :found_steamvr
)

if exist "D:\Steam\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=D:\Steam\steamapps\common\SteamVR"
    goto :found_steamvr
)

if exist "D:\SteamLibrary\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=D:\SteamLibrary\steamapps\common\SteamVR"
    goto :found_steamvr
)

if exist "E:\Steam\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=E:\Steam\steamapps\common\SteamVR"
    goto :found_steamvr
)

if exist "E:\SteamLibrary\steamapps\common\SteamVR" (
    set "STEAMVR_PATH=E:\SteamLibrary\steamapps\common\SteamVR"
    goto :found_steamvr
)

REM Try to find Steam from registry
for /f "tokens=2*" %%a in ('reg query "HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do (
    set "STEAM_PATH=%%b"
    if exist "!STEAM_PATH!\steamapps\common\SteamVR" (
        set "STEAMVR_PATH=!STEAM_PATH!\steamapps\common\SteamVR"
        goto :found_steamvr
    )
)

for /f "tokens=2*" %%a in ('reg query "HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul') do (
    set "STEAM_PATH=%%b"
    if exist "!STEAM_PATH!\steamapps\common\SteamVR" (
        set "STEAMVR_PATH=!STEAM_PATH!\steamapps\common\SteamVR"
        goto :found_steamvr
    )
)

echo ERROR: Could not find SteamVR installation.
echo Please ensure SteamVR is installed.
echo.
echo You can manually specify the path by editing this script
echo or by setting the STEAMVR_PATH environment variable.
goto :error

:found_steamvr
echo Found SteamVR at: %STEAMVR_PATH%
echo.

REM Set driver paths
REM Scripts can be run from either:
REM   1. scripts/ folder (source): driver at ..\build\driver\micmap
REM   2. build\bin\ folder: driver at .\driver\micmap (same directory as script)
set "DRIVER_SOURCE=%~dp0..\build\driver\micmap"
if not exist "%DRIVER_SOURCE%\driver.vrdrivermanifest" (
    REM Try the build/bin location (driver folder next to script)
    set "DRIVER_SOURCE=%~dp0driver\micmap"
)
set "DRIVER_DEST=%STEAMVR_PATH%\drivers\micmap"

REM Check if source driver exists
if not exist "%DRIVER_SOURCE%" (
    echo ERROR: Driver not found at: %DRIVER_SOURCE%
    echo.
    echo Please build the project first:
    echo   1. mkdir build
    echo   2. cd build
    echo   3. cmake ..
    echo   4. cmake --build . --config Release
    goto :error
)

REM Check if driver.vrdrivermanifest exists
if not exist "%DRIVER_SOURCE%\driver.vrdrivermanifest" (
    echo ERROR: driver.vrdrivermanifest not found in: %DRIVER_SOURCE%
    echo The driver may not have been built correctly.
    goto :error
)

REM Remove existing driver if present
if exist "%DRIVER_DEST%" (
    echo Removing existing MicMap driver...
    rmdir /s /q "%DRIVER_DEST%" 2>nul
    
    REM Wait a moment for filesystem to catch up
    timeout /t 1 /nobreak >nul 2>&1
    
    REM Check if directory still exists
    if exist "%DRIVER_DEST%" (
        echo Warning: Directory still exists, attempting to remove again...
        rmdir /s /q "%DRIVER_DEST%" 2>nul
        timeout /t 1 /nobreak >nul 2>&1
    )
    
    REM Final check
    if exist "%DRIVER_DEST%" (
        echo ERROR: Failed to remove existing driver.
        echo Please close SteamVR and any applications using the driver, then try again.
        goto :error
    )
    echo Existing driver removed successfully.
)

REM Create destination directory
echo Creating driver directory...
mkdir "%DRIVER_DEST%" 2>nul

REM Copy driver files
echo Installing MicMap driver to: %DRIVER_DEST%
xcopy /e /i /y "%DRIVER_SOURCE%" "%DRIVER_DEST%" >nul
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy driver files.
    echo.
    echo Retrying copy operation...
    timeout /t 2 /nobreak >nul 2>&1
    xcopy /e /i /y "%DRIVER_SOURCE%" "%DRIVER_DEST%" >nul
    if !errorLevel! neq 0 (
        echo ERROR: Copy failed after retry.
        goto :error
    )
)

echo Driver files copied successfully.
echo.

REM Now copy the MicMap application
REM The driver expects the app at: <driver>/apps/micmap.exe
REM Scripts can be run from either:
REM   1. scripts/ folder (source): app at ..\build\bin\micmap.exe
REM   2. build\bin\ folder: app at .\micmap.exe (same directory as script)
set "APP_SOURCE=%~dp0..\build\bin"
if not exist "%APP_SOURCE%\micmap.exe" (
    REM Try same directory as script (when running from build\bin)
    set "APP_SOURCE=%~dp0."
)
set "APP_DEST=%DRIVER_DEST%\apps"
set "APP_INSTALLED=0"

REM Check if the application exists
if not exist "%APP_SOURCE%\micmap.exe" (
    echo WARNING: MicMap application not found.
    echo Searched in:
    echo   - %~dp0..\build\bin\micmap.exe
    echo   - %~dp0micmap.exe
    echo.
    echo The driver will be installed, but the application will not auto-launch.
    echo You can manually run the application from the build directory.
    echo.
    goto :skip_app_install
)

echo Installing MicMap application...

REM Create apps directory
mkdir "%APP_DEST%" 2>nul

REM Copy the application executable
copy /y "%APP_SOURCE%\micmap.exe" "%APP_DEST%\" >nul
if %errorLevel% neq 0 (
    echo WARNING: Failed to copy micmap.exe
    goto :skip_app_install
)

REM Copy config directory if it exists
if exist "%APP_SOURCE%\config" (
    echo Copying configuration files...
    xcopy /e /i /y "%APP_SOURCE%\config" "%APP_DEST%\config" >nul
    if !errorLevel! neq 0 (
        echo WARNING: Failed to copy config files
    )
)

set "APP_INSTALLED=1"
echo MicMap application installed successfully.
echo.

:skip_app_install

echo.
echo ============================================
echo Installation Complete!
echo ============================================
echo.
echo The MicMap driver has been installed to:
echo   %DRIVER_DEST%
echo.
if "%APP_INSTALLED%"=="1" (
    echo The MicMap application has been installed to:
    echo   %APP_DEST%
    echo.
    echo The application will auto-launch when SteamVR starts.
    echo.
)
if "%APP_INSTALLED%"=="0" (
    echo NOTE: The MicMap application was not installed.
    echo You will need to run it manually from the build directory.
    echo.
)
echo To verify the driver is loaded:
echo   - Open SteamVR Settings
echo   - Go to Developer ^> Enable Developer Settings
echo   - Check the Startup/Shutdown section for "micmap"
echo.
echo To uninstall, run: uninstall_driver.bat
echo.
goto :end

:error
echo.
echo Installation failed.
exit /b 1

:end
endlocal
exit /b 0
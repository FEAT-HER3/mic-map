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

REM Try common Steam installation paths
set "STEAM_PATHS=C:\Program Files (x86)\Steam;C:\Program Files\Steam;D:\Steam;D:\SteamLibrary"

for %%p in (%STEAM_PATHS%) do (
    if exist "%%p\steamapps\common\SteamVR" (
        set "STEAMVR_PATH=%%p\steamapps\common\SteamVR"
        goto :found_steamvr
    )
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
set "DRIVER_SOURCE=%~dp0..\build\driver\micmap"
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
    rmdir /s /q "%DRIVER_DEST%"
    if %errorLevel% neq 0 (
        echo ERROR: Failed to remove existing driver.
        echo Please close SteamVR and try again.
        goto :error
    )
)

REM Copy driver files
echo Installing MicMap driver to: %DRIVER_DEST%
xcopy /e /i /y "%DRIVER_SOURCE%" "%DRIVER_DEST%"
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy driver files.
    goto :error
)

echo.
echo ============================================
echo Installation Complete!
echo ============================================
echo.
echo The MicMap driver has been installed to:
echo   %DRIVER_DEST%
echo.
echo Next steps:
echo   1. Start SteamVR
echo   2. The driver will be loaded automatically
echo   3. Run the MicMap application
echo.
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
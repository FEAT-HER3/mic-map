@echo off
REM MicMap OpenVR Driver Uninstallation Script
REM This script removes the MicMap driver from SteamVR's drivers directory

setlocal enabledelayedexpansion

echo ============================================
echo MicMap OpenVR Driver Uninstaller
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
goto :error

:found_steamvr
echo Found SteamVR at: %STEAMVR_PATH%
echo.

set "DRIVER_DEST=%STEAMVR_PATH%\drivers\micmap"

REM Check if driver is installed
if not exist "%DRIVER_DEST%" (
    echo MicMap driver is not installed.
    goto :end
)

REM Remove driver
echo Removing MicMap driver from: %DRIVER_DEST%
rmdir /s /q "%DRIVER_DEST%"
if %errorLevel% neq 0 (
    echo ERROR: Failed to remove driver.
    echo Please close SteamVR and try again.
    goto :error
)

echo.
echo ============================================
echo Uninstallation Complete!
echo ============================================
echo.
echo The MicMap driver has been removed.
echo.
goto :end

:error
echo.
echo Uninstallation failed.
exit /b 1

:end
endlocal
exit /b 0
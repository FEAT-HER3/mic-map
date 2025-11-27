@echo off
REM MicMap OpenVR Driver Test Installation Script
REM This script installs the MicMap driver with auto-launch DISABLED for testing

setlocal enabledelayedexpansion

echo ============================================
echo MicMap OpenVR Driver Test Installer
echo ============================================
echo.
echo This installer configures the driver for TESTING:
echo   - Auto-launch is DISABLED
echo   - Only driver files are installed (not the main app)
echo   - Use hmd_button_test.exe to test the driver
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
REM Check each path individually to handle spaces properly

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

REM Ensure auto-launch is disabled in the installed settings
set "SETTINGS_FILE=%DRIVER_DEST%\resources\settings\default.vrsettings"
if exist "%SETTINGS_FILE%" (
    echo.
    echo Verifying auto-launch is disabled...
    
    REM Check current setting
    findstr /C:"autoLaunchApp" "%SETTINGS_FILE%" | findstr /C:"false" >NUL
    if %errorLevel% equ 0 (
        echo Auto-launch is already disabled.
    ) else (
        echo Updating settings to disable auto-launch...
        REM Create a new settings file with auto-launch disabled
        echo { > "%SETTINGS_FILE%.tmp"
        echo     "driver_micmap": { >> "%SETTINGS_FILE%.tmp"
        echo         "enable": true, >> "%SETTINGS_FILE%.tmp"
        echo         "http_port": 27015, >> "%SETTINGS_FILE%.tmp"
        echo         "http_host": "127.0.0.1", >> "%SETTINGS_FILE%.tmp"
        echo         "autoLaunchApp": false, >> "%SETTINGS_FILE%.tmp"
        echo         "appPath": "", >> "%SETTINGS_FILE%.tmp"
        echo         "appArgs": "" >> "%SETTINGS_FILE%.tmp"
        echo     } >> "%SETTINGS_FILE%.tmp"
        echo } >> "%SETTINGS_FILE%.tmp"
        move /y "%SETTINGS_FILE%.tmp" "%SETTINGS_FILE%" >NUL
        echo Auto-launch disabled.
    )
)

echo.
echo ============================================
echo Test Installation Complete!
echo ============================================
echo.
echo The MicMap driver has been installed to:
echo   %DRIVER_DEST%
echo.
echo Configuration:
echo   - Auto-launch: DISABLED
echo   - HTTP port: 27015
echo   - HTTP host: 127.0.0.1
echo.
echo Testing Instructions:
echo   1. Start SteamVR
echo   2. Run: scripts\test_driver.bat
echo      Or manually run: build\apps\hmd_button_test\Release\hmd_button_test.exe
echo   3. Click "Test Driver" to verify connection
echo   4. Click "Open Dashboard" to open SteamVR dashboard
echo   5. Click "Send Click" to test button injection
echo.
echo To enable auto-launch for production:
echo   Edit: %SETTINGS_FILE%
echo   Set "autoLaunchApp" to true and configure "appPath"
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
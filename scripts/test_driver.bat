@echo off
REM MicMap Driver Test Script
REM This script helps test the MicMap driver with hmd_button_test.exe

setlocal enabledelayedexpansion

echo ============================================
echo MicMap Driver Test
echo ============================================
echo.

REM Find the build directory
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%..\build"
set "TEST_EXE=%BUILD_DIR%\apps\hmd_button_test\Release\hmd_button_test.exe"

REM Also check Debug build
if not exist "%TEST_EXE%" (
    set "TEST_EXE=%BUILD_DIR%\apps\hmd_button_test\Debug\hmd_button_test.exe"
)

REM Check if test executable exists
if not exist "%TEST_EXE%" (
    echo ERROR: hmd_button_test.exe not found.
    echo.
    echo Expected locations:
    echo   %BUILD_DIR%\apps\hmd_button_test\Release\hmd_button_test.exe
    echo   %BUILD_DIR%\apps\hmd_button_test\Debug\hmd_button_test.exe
    echo.
    echo Please build the project first:
    echo   1. mkdir build
    echo   2. cd build
    echo   3. cmake ..
    echo   4. cmake --build . --config Release
    echo.
    goto :error
)

echo Found test executable: %TEST_EXE%
echo.

REM Check if SteamVR is running
echo Checking SteamVR status...
tasklist /FI "IMAGENAME eq vrserver.exe" 2>NUL | find /I /N "vrserver.exe">NUL
if %errorLevel% neq 0 (
    echo.
    echo WARNING: SteamVR does not appear to be running.
    echo.
    echo The driver test requires SteamVR to be running with the
    echo MicMap driver installed.
    echo.
    echo To start SteamVR:
    echo   1. Open Steam
    echo   2. Click VR icon or go to Library ^> Tools ^> SteamVR
    echo   3. Wait for SteamVR to fully start
    echo.
    set /p "CONTINUE=Continue anyway? (y/n): "
    if /i "!CONTINUE!" neq "y" goto :end
    echo.
)

REM Check if driver is installed
echo Checking driver installation...
set "STEAMVR_PATH="

REM Try common Steam installation paths (check each individually to handle spaces)
if exist "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
    goto :found_driver
)

if exist "C:\Program Files\Steam\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=C:\Program Files\Steam\steamapps\common\SteamVR"
    goto :found_driver
)

if exist "D:\Steam\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=D:\Steam\steamapps\common\SteamVR"
    goto :found_driver
)

if exist "D:\SteamLibrary\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=D:\SteamLibrary\steamapps\common\SteamVR"
    goto :found_driver
)

if exist "E:\Steam\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=E:\Steam\steamapps\common\SteamVR"
    goto :found_driver
)

if exist "E:\SteamLibrary\steamapps\common\SteamVR\drivers\micmap" (
    set "STEAMVR_PATH=E:\SteamLibrary\steamapps\common\SteamVR"
    goto :found_driver
)

REM Try to find Steam from registry
for /f "tokens=2*" %%a in ('reg query "HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do (
    set "STEAM_PATH=%%b"
    if exist "!STEAM_PATH!\steamapps\common\SteamVR\drivers\micmap" (
        set "STEAMVR_PATH=!STEAM_PATH!\steamapps\common\SteamVR"
        goto :found_driver
    )
)

echo.
echo WARNING: MicMap driver not found in SteamVR drivers folder.
echo.
echo Please install the driver first:
echo   scripts\install_driver.bat
echo.
echo Or for testing without auto-launch:
echo   scripts\install_driver_test.bat
echo.
set /p "CONTINUE=Continue anyway? (y/n): "
if /i "!CONTINUE!" neq "y" goto :end
echo.
goto :run_test

:found_driver
echo Driver found at: %STEAMVR_PATH%\drivers\micmap
echo.

REM Check driver settings
set "SETTINGS_FILE=%STEAMVR_PATH%\drivers\micmap\resources\settings\default.vrsettings"
if exist "%SETTINGS_FILE%" (
    echo Driver settings file found.
    findstr /C:"autoLaunchApp" "%SETTINGS_FILE%" | findstr /C:"false" >NUL
    if %errorLevel% equ 0 (
        echo Auto-launch is DISABLED (good for testing)
    ) else (
        echo NOTE: Auto-launch may be enabled. For testing, consider
        echo       running install_driver_test.bat to disable it.
    )
    echo.
)

:run_test
echo ============================================
echo Starting HMD Button Test
echo ============================================
echo.
echo Test Instructions:
echo   1. Click "Test Driver" to verify driver connection
echo   2. Click "Open Dashboard" to open SteamVR dashboard
echo   3. Click "Send Click" to send a button press via driver
echo   4. Check the event log for HTTP responses
echo.
echo Press Ctrl+C to cancel, or...
pause

echo.
echo Launching %TEST_EXE%...
start "" "%TEST_EXE%"

echo.
echo Test application launched.
echo.
goto :end

:error
echo.
echo Test failed.
exit /b 1

:end
endlocal
exit /b 0
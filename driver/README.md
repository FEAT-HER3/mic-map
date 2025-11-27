# MicMap OpenVR Driver

This directory contains the OpenVR driver for MicMap that enables button injection into SteamVR.

## Overview

The MicMap driver registers a virtual controller with SteamVR that can inject button events. This allows the MicMap application to trigger dashboard selection (clicking) by sending HTTP requests to the driver.

## Architecture

```
┌─────────────────────┐     HTTP      ┌─────────────────────┐
│  MicMap Application │ ──────────── │   MicMap Driver     │
│  (vr_input.cpp)     │  localhost   │  (driver_micmap.dll)│
└─────────────────────┘   :27015     └─────────────────────┘
                                              │
                                              │ IVRDriverInput
                                              ▼
                                     ┌─────────────────────┐
                                     │      SteamVR        │
                                     │  (Button Events)    │
                                     └─────────────────────┘
```

## Quick Start Testing

The fastest way to test the driver:

```bash
# 1. Build the project
mkdir build && cd build
cmake ..
cmake --build . --config Release

# 2. Install driver (with auto-launch disabled for testing)
cd ..
scripts\install_driver_test.bat

# 3. Start SteamVR

# 4. Run the test application
scripts\test_driver.bat
```

## Components

### Driver Entry Point (`driver_main.cpp`)
- Exports `HmdDriverFactory()` function called by SteamVR
- Returns the device provider instance

### Device Provider (`device_provider.hpp/cpp`)
- Implements `IServerTrackedDeviceProvider`
- Manages the virtual controller lifecycle
- Starts/stops the HTTP server

### Virtual Controller (`virtual_controller.hpp/cpp`)
- Implements `ITrackedDeviceServerDriver`
- Registers as a controller device (with `TrackedControllerRole_OptOut`)
- Creates boolean input components for system and A buttons
- Provides methods to press/release/click buttons

### HTTP Server (`http_server.hpp/cpp`)
- Listens on localhost (default port 27015)
- Provides REST-style endpoints for button control
- Falls back to alternative ports (27015-27025) if default is in use

## HTTP API

### Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/status` | Get driver status |
| GET | `/health` | Health check |
| GET | `/port` | Get actual port number |
| POST | `/click` | Press and release button |
| POST | `/press` | Press button down |
| POST | `/release` | Release button |

### Parameters

- `button` - Button name: `system` (default) or `a`
- `duration` - Click duration in milliseconds (default: 100)

### Examples

```bash
# Check driver status
curl http://localhost:27015/status

# Click system button
curl -X POST "http://localhost:27015/click?button=system&duration=100"

# Press and hold A button
curl -X POST "http://localhost:27015/press?button=a"

# Release A button
curl -X POST "http://localhost:27015/release?button=a"
```

## Building

The driver is built as part of the main MicMap project:

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The driver will be output to `build/driver/micmap/`.

### Requirements

- OpenVR SDK (set `OPENVR_SDK_PATH` environment variable)
- cpp-httplib (automatically fetched by CMake)

## Installation

### For Testing (Recommended First)

Use the test installation script which disables auto-launch:

```bash
scripts\install_driver_test.bat
```

This installs the driver with `autoLaunchApp` set to `false`, allowing you to test the driver manually with `hmd_button_test.exe`.

### For Production

Run the standard installation script:

```bash
scripts\install_driver.bat
```

Then configure auto-launch by editing the settings file (see Settings section below).

### Manual Installation

1. Copy the `build/driver/micmap` directory to SteamVR's drivers folder:
   - Typically: `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\`

2. The driver will be loaded automatically when SteamVR starts.

## Uninstallation

Run the uninstallation script:

```bash
scripts\uninstall_driver.bat
```

Or manually delete the `micmap` folder from SteamVR's drivers directory.

## Settings

The driver settings are stored in `resources/settings/default.vrsettings`:

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `enable` | bool | `true` | Enable/disable the driver |
| `http_port` | int | `27015` | HTTP server port |
| `http_host` | string | `"127.0.0.1"` | HTTP server host (localhost only for security) |
| `autoLaunchApp` | bool | `false` | Auto-launch MicMap app when SteamVR starts |
| `appPath` | string | `""` | Path to application to launch |
| `appArgs` | string | `""` | Command line arguments for the application |

### Configuring Auto-Launch

To enable auto-launch for production use:

1. Edit the settings file in the installed driver location:
   ```
   <SteamVR>/drivers/micmap/resources/settings/default.vrsettings
   ```

2. Set the following values:
   ```json
   {
       "driver_micmap": {
           "enable": true,
           "http_port": 27015,
           "http_host": "127.0.0.1",
           "autoLaunchApp": true,
           "appPath": "C:/Path/To/micmap.exe",
           "appArgs": ""
       }
   }
   ```

3. Restart SteamVR for changes to take effect.

## Testing with hmd_button_test.exe

The `hmd_button_test.exe` application provides a GUI for testing the driver:

### Features

- **SteamVR Status**: Shows connection to SteamVR runtime
- **Driver Status**: Shows HTTP connection to the MicMap driver
- **Dashboard State**: Shows if SteamVR dashboard is open/closed
- **Test Driver**: Tests the HTTP connection to the driver
- **Open Dashboard**: Opens the SteamVR dashboard
- **Send Click**: Sends a button click via the driver HTTP API
- **Auto**: Performs the appropriate action based on dashboard state

### Testing Workflow

1. **Build the project**:
   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build . --config Release
   ```

2. **Install driver for testing**:
   ```bash
   scripts\install_driver_test.bat
   ```

3. **Start SteamVR**

4. **Run the test application**:
   ```bash
   scripts\test_driver.bat
   ```
   Or directly:
   ```bash
   build\apps\hmd_button_test\Release\hmd_button_test.exe
   ```

5. **Verify driver connection**:
   - Click "Test Driver" button
   - Should show "Connected (port 27015)" in Driver Status

6. **Test dashboard interaction**:
   - Click "Open Dashboard" to open SteamVR dashboard
   - Click "Send Click" to send a button press via the driver
   - Check the event log for HTTP responses

### Expected Results

- **Test Driver**: Should show "Driver connected on port 27015"
- **Open Dashboard**: Should open the SteamVR dashboard overlay
- **Send Click**: Should trigger a click in the dashboard (activates whatever is under the head-locked pointer)

## Troubleshooting

### Driver not loading

1. Check SteamVR logs: `%LOCALAPPDATA%\openvr\logs\`
2. Verify the driver is in the correct location
3. Ensure `driver.vrdrivermanifest` exists in the driver folder
4. Look for "micmap" in the SteamVR startup log

### HTTP connection failed

1. Check if SteamVR is running
2. Check if the driver is loaded (SteamVR Settings > Startup/Shutdown)
3. Try the test application: `scripts\test_driver.bat`
4. Check if port 27015 is available (driver will try 27015-27025)
5. Check firewall settings for localhost connections

### Button events not working

1. Verify the virtual controller is registered in SteamVR
2. Check SteamVR's device list for "MicMap Virtual Controller"
3. Review driver logs for error messages
4. Make sure the dashboard is open when sending clicks

### Driver Status shows "Not Connected"

1. Ensure SteamVR is running
2. Check that the driver is installed: `<SteamVR>/drivers/micmap/`
3. Restart SteamVR after installing the driver
4. Check SteamVR logs for driver loading errors

### Auto-launch not working

1. Verify `autoLaunchApp` is set to `true` in settings
2. Verify `appPath` points to a valid executable
3. Check that the path uses forward slashes or escaped backslashes
4. Check SteamVR logs for launch errors

## Files

```
driver/
├── CMakeLists.txt                    # Build configuration
├── driver.vrdrivermanifest           # Driver manifest
├── README.md                         # This file
├── resources/
│   ├── driver.vrresources            # Resource definitions
│   ├── settings/
│   │   └── default.vrsettings        # Default settings
│   └── input/
│       └── micmap_controller_profile.json  # Input profile
└── src/
    ├── driver_main.cpp               # Entry point
    ├── device_provider.hpp/cpp       # Device provider
    ├── virtual_controller.hpp/cpp    # Virtual controller
    └── http_server.hpp/cpp           # HTTP server
```

## License

Part of the MicMap project. See the main project LICENSE file.
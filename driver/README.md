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

### Automatic Installation

Run the installation script:

```bash
scripts\install_driver.bat
```

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

## Troubleshooting

### Driver not loading

1. Check SteamVR logs: `%LOCALAPPDATA%\openvr\logs\`
2. Verify the driver is in the correct location
3. Ensure `driver.vrdrivermanifest` exists in the driver folder

### HTTP connection failed

1. Check if the driver is running (look for `driver_micmap.dll` in SteamVR logs)
2. Try different ports (27015-27025)
3. Check firewall settings for localhost connections

### Button events not working

1. Verify the virtual controller is registered in SteamVR
2. Check SteamVR's device list for "MicMap Virtual Controller"
3. Review driver logs for error messages

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
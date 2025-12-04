# MicMap

A SteamVR addon that listens to microphone input and detects a unique noise pattern (like covering your microphone) to trigger SteamVR dashboard interactions.

## Overview

MicMap provides a hands-free way to interact with the SteamVR dashboard by detecting when you cover your microphone. When the characteristic white noise pattern is detected for a configurable duration, MicMap simulates a controller input to open or interact with the SteamVR dashboard.

## a note from FEAT~~HER3~~

Thanks for checking out MicMap. It's an idea I've had for a couple years now, and so finally made time for it. Well... Kind of.

While I generally take pride in my programming, C++ is not an environment I am terribly familiar with. Because my time is so limited these days, I vibecoded (used AI) to create most of this project. I can at least say that I followed each step closely and with scrutiny to make sure nothing too silly made its way in.

The state of mic blockage detection is a bit rough today. It's my hope that this baseline will serve the community, and others with a little more experience can jump in to make MicMap a great tool. May it serve you well!

-Reavo

### How It Works

1. **Audio Capture**: MicMap continuously monitors your selected microphone input
2. **Spectral Analysis**: The audio is analyzed using FFT to detect white noise characteristics
3. **Pattern Detection**: A trained pattern recognizes when the microphone is covered vs normal ambient sound
4. **SteamVR Integration**: When detection triggers, MicMap sends input through a virtual SteamVR controller driver

## Requirements

- **Operating System**: Windows 10 or Windows 11
- **VR Runtime**: SteamVR installed and configured
- **Build Tools** (for building from source):
  - Visual Studio 2022 with C++ desktop development workload
  - CMake 3.20 or higher

## Installation

### Installing the SteamVR Driver

Run the installation script as Administrator:

```bash
scripts\install_driver.bat
```

This script:
1. Copies the driver files to the SteamVR drivers directory
2. Registers the driver with SteamVR
3. Sets up the MicMap application to launch automatically with SteamVR

**Driver Installation Location**: `<SteamVR>/drivers/micmap/`

### Uninstalling

To remove MicMap:

```bash
scripts\uninstall_driver.bat
```

## Usage

### Starting MicMap

MicMap automatically launches when SteamVR starts (after driver installation). You can also run it manually:

```bash
build\apps\micmap\Release\micmap.exe
```

### Training the Detection Pattern

Before first use, you should train MicMap to recognize your microphone's characteristics:

1. Launch MicMap
2. Cover one of the HMD microphones with a finger
3. From the settings window, click "Train Pattern"
4. Training completes automatically and saves the profile

### Using Dashboard Interaction

Once trained:

1. Tap your microphone with your finger and hold
2. Keep holding for a bit longer than the configured detection time (default: 0.3 seconds).
  - You may need to try 2-3 times per attempt (it's the current state of the detection algorithm)
  - Strugglin'? Try retraining with a gentler mic hold and/or lowering the detection time
3. When it fires, it functions like the Valve Index HMD button. So either the SteamVR dashboard will open, or items in the dashboard will be clicked.

## Configuration

### Detection Time

Adjust how long the microphone must be covered before triggering:
- Access via system tray → Show
- Default: 300ms
- Range: IDK mess around kekw

### Device Selection

Select which audio input device to monitor:
- Dropdown menu in settings
- Lists all available microphone inputs
- Selection persists across sessions

### Configuration File

Settings are stored in `%APPDATA%/MicMap/config.json`:

```json
{
  "detection_time_ms": 300,
  "selected_device": "Microphone (Beyond)",
  "active_profile": "default",
  "auto_start": true
}
```

## Troubleshooting

### Verify Driver Installation

Check if the driver is properly installed:

1. Open SteamVR Settings
2. Go to Developer → Startup/Shutdown
3. Look for "micmap" in the driver list

Or run:
```bash
scripts\test_driver.bat
```

### Check if MicMap is Running

1. Look for the MicMap icon in the system tray
2. Or check Task Manager for `micmap.exe`

### Common Issues

#### MicMap doesn't detect microphone covering
- Ensure the correct microphone is selected
- Re-train the pattern in a quiet environment
- Increase detection sensitivity in settings
- Check that the microphone isn't muted at the system level

#### SteamVR doesn't respond to detection
- Verify the driver is installed correctly
- Restart SteamVR after driver installation
- Check SteamVR logs for driver errors

### Log Files

MicMap logs are stored in:
- Application logs: `%APPDATA%/MicMap/logs/`
- Driver logs: Check SteamVR's vrserver.txt log

## Known Issues

1. **Visual beam from head to laser pointer**: When the virtual controller activates, SteamVR may display a visible beam/ray from the headset to the laser pointer position. This is a cosmetic issue and does not affect functionality.

2. **Subpar microphone covered detection**: The current detection algorithm may have difficulty distinguishing between microphone covering and other loud sounds in some environments. Training in a quiet environment can help mitigate this.

3. **MicMap app is not auto-starting alongside SteamVR**

## Building from Source

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/mic-map.git
cd mic-map
```

### 2. Configure with CMake

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

### 3. Build

```bash
cmake --build build --config Release
```

### Output Locations

After building, you'll find:
- **MicMap Application**: `build/apps/micmap/Release/micmap.exe`
- **SteamVR Driver**: `build/driver/Release/driver_micmap.dll`

## Project Structure

```
mic-map/
├── apps/                    # Application executables
│   └── micmap/             # Main MicMap application
├── driver/                  # SteamVR driver
│   ├── src/                # Driver source code
│   └── resources/          # Driver configuration files
├── src/                     # Core libraries
│   ├── audio/              # Audio capture and processing
│   ├── common/             # Shared utilities
│   ├── core/               # Configuration and state management
│   ├── detection/          # Noise detection algorithms
│   └── steamvr/            # SteamVR integration
├── config/                  # Default configuration files
├── scripts/                 # Installation and utility scripts
└── docs/                    # Additional documentation
```

## Contributing

Feel free to fork, clone, and PR. Useful additions and improvements welcome!

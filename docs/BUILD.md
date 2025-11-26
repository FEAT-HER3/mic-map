# MicMap Build Instructions

## Prerequisites

### Required
- **CMake 3.20+**: Download from https://cmake.org/download/
- **Visual Studio 2022** (or 2019): With "Desktop development with C++" workload
- **Windows 10 SDK**: Included with Visual Studio

### Optional (for full VR functionality)
- **OpenXR SDK**: For VR runtime integration
- **OpenVR SDK**: For SteamVR-specific features

## Building the Project

### Using Visual Studio

1. **Configure with CMake**:
   ```cmd
   cmake -B build -S . -G "Visual Studio 17 2022" -A x64
   ```
   
   For Visual Studio 2019:
   ```cmd
   cmake -B build -S . -G "Visual Studio 16 2019" -A x64
   ```

2. **Open the solution**:
   ```cmd
   start build\MicMap.sln
   ```

3. **Build in Visual Studio**:
   - Select `Release` or `Debug` configuration
   - Build → Build Solution (Ctrl+Shift+B)

### Using Command Line

1. **Configure**:
   ```cmd
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   ```

2. **Build**:
   ```cmd
   cmake --build build --config Release
   ```

3. **Run tests** (optional):
   ```cmd
   ctest --test-dir build --output-on-failure
   ```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `MICMAP_BUILD_TESTS` | ON | Build unit tests |
| `MICMAP_BUILD_TEST_APPS` | ON | Build test applications |

Example with options:
```cmd
cmake -B build -S . -DMICMAP_BUILD_TESTS=OFF
```

## Output

After building, executables are located in:
- `build/bin/Release/` (Release build)
- `build/bin/Debug/` (Debug build)

### Executables

| Name | Description |
|------|-------------|
| `micmap.exe` | Main MicMap application |
| `mic_test.exe` | Audio capture and detection test |
| `hmd_button_test.exe` | SteamVR button event test |

## Installing OpenXR SDK (Optional)

1. Download from https://github.com/KhronosGroup/OpenXR-SDK/releases
2. Extract to a known location
3. Set environment variable:
   ```cmd
   set OPENXR_SDK_PATH=C:\path\to\openxr-sdk
   ```
4. Re-run CMake configuration

## Installing OpenVR SDK (Optional)

1. Download from https://github.com/ValveSoftware/openvr/releases
2. Extract to a known location
3. Set environment variable:
   ```cmd
   set OPENVR_SDK_PATH=C:\path\to\openvr
   ```
4. Re-run CMake configuration

## Troubleshooting

### CMake not found
Ensure CMake is installed and added to your PATH. You can verify with:
```cmd
cmake --version
```

### Visual Studio generator not found
Make sure Visual Studio is installed with the "Desktop development with C++" workload.

### WASAPI errors
The audio capture requires Windows 10 or later. Ensure you have the Windows 10 SDK installed.

### VR not detected
If OpenXR/OpenVR SDKs are not found, the application will use stub implementations. VR features will be simulated but won't interact with actual VR hardware.

## Development

### Adding new source files
1. Add the file to the appropriate `src/*/src/` directory
2. Update the corresponding `CMakeLists.txt` to include the new file
3. Re-run CMake configuration

### Code style
- C++17 standard
- Use namespaces: `micmap::audio`, `micmap::detection`, etc.
- Header files in `include/micmap/*/`
- Implementation files in `src/`
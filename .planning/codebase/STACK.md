# Technology Stack

**Analysis Date:** 2026-04-23

## Languages

**Primary:**
- C++ 17 - Core application logic, audio processing, VR integration, driver development

**Secondary:**
- JSON - Configuration and action binding files
- Batch - Installation and testing scripts for Windows deployment

## Runtime

**Environment:**
- Windows 10 or Windows 11
- Requires SteamVR installed and running for VR functionality
- Requires OpenVR SDK (optional but needed for driver compilation)

**Build System:**
- CMake 3.20+ - Cross-platform build configuration
- Visual Studio 2022 - Recommended C++ compiler with /MP multi-processor compilation
- MSVC with /W4 warning level

## Frameworks

**Core:**
- Dear ImGui v1.90.1 - Immediate mode GUI library for settings UI (Windows + DirectX11 backend)
- OpenVR SDK (optional) - SteamVR-specific virtual controller driver and input handling
- OpenXR SDK (optional) - VR integration abstraction layer

**Audio Processing:**
- WASAPI (Windows Audio Session API) - Native Windows audio capture via `<mmdeviceapi.h>`, `<audioclient.h>`
- KissFFT 131.1.0 - Lightweight Fast Fourier Transform library for spectral analysis (single-precision float)

**Utilities:**
- nlohmann/json v3.11.2 - Header-only JSON parsing and serialization library (configured but not actively used in config manager)
- cpp-httplib v0.14.3 - Header-only HTTP server library for localhost communication between driver and application

## Key Dependencies

**Critical:**
- OpenVR SDK - Required for building the SteamVR driver (`driver_micmap`). Provides virtual controller registration and input injection
  - Search paths: `${OPENVR_SDK_PATH}`, `./external/openvr`, `C:/OpenVR`, system Program Files
- ImGui - Desktop UI for settings, device selection, detection threshold configuration
  - Compiled with Win32 + DirectX11 backends
  - Requires: `d3d11`, `d3dcompiler`, `dxgi` system libraries

**Infrastructure:**
- KissFFT - Spectral frequency analysis for microphone pattern detection
  - Compiled as static C library with `kiss_fft_scalar=float` configuration
- cpp-httplib - HTTP server for RPC communication between driver and main application
  - Port: 27015 (localhost only)
  - Auto-detects OpenSSL but not required for HTTP-only communication

**Windows System Libraries:**
- `mmdeviceapi.h` - Device enumeration and audio session management
- `audioclient.h` - Raw audio frame access and format management
- `ws2_32.lib` - Winsock2 for HTTP server socket operations
- `d3d11.lib`, `d3dcompiler.lib`, `dxgi.lib` - DirectX 11 for ImGui rendering
- `shell32.lib`, `dwmapi.lib` - Windows shell and DWM for application window management
- `ShlObj.h` - AppData folder detection for configuration persistence

## Configuration

**Environment:**
- `OPENVR_SDK_PATH` - Environment variable to locate OpenVR SDK (optional, searches multiple default paths)
- Configuration file: `%APPDATA%/MicMap/config.json` (created at runtime)

**Build:**
- CMakeLists.txt - Root build configuration in `/d/Documents/Projects/mic-map/CMakeLists.txt`
- Build options (enabled by default):
  - `MICMAP_BUILD_TESTS` - Build unit tests (currently placeholder with Google Test scaffolding)
  - `MICMAP_BUILD_TEST_APPS` - Build test applications (`mic_test`, `hmd_button_test`)
  - `MICMAP_BUILD_DRIVER` - Build OpenVR driver (skipped if OpenVR SDK not found)
- Optional CMake flags:
  - `MICMAP_USE_GTEST` - Enable Google Test framework (currently OFF)

**Compiler Settings:**
- Windows (MSVC): `/W4` warnings, `/MP` multi-processor compilation, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`
- Other platforms (GCC/Clang): `-Wall -Wextra -Wpedantic`

## Platform Requirements

**Development:**
- Visual Studio 2022 with C++ desktop development workload
- CMake 3.20 or higher
- OpenVR SDK (for driver development)
- Microphone input device

**Production:**
- Windows 10 or Windows 11
- SteamVR installed and running
- Microphone input device for detection
- Virtual controller driver installed via `scripts/install_driver.bat`

---

*Stack analysis: 2026-04-23*

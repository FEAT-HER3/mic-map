# External Integrations

**Analysis Date:** 2026-04-23

## APIs & External Services

**SteamVR Integration:**
- OpenVR API - Core VR runtime communication for controller input injection
  - SDK: OpenVR SDK (external dependency, installed separately)
  - Configuration: `cmake/FindOpenVR.cmake` locates SDK via `OPENVR_SDK_PATH` env var or hardcoded paths
  - Driver manifest: `driver/driver.vrdrivermanifest`
  - Action bindings: `config/actions/micmap_actions.json` with platform-specific bindings for Knuckles, Vive, Oculus, WMR, Gamepad, HMD

**Virtual Controller Registration:**
- Virtual OpenVR Controller Driver
  - Implementation: `driver/src/device_provider.cpp`, `driver/src/virtual_controller.cpp`
  - Provides simulated controller input for dashboard interaction
  - Bindings registered for multiple controller types via `config/actions/bindings_*.json`

**Microphone Pattern Detection:**
- Windows WASAPI (Windows Audio Session API)
  - SDK: Native Windows headers (`mmdeviceapi.h`, `audioclient.h`)
  - Device enumeration: `src/audio/src/device_enumerator.cpp`
  - Audio capture: `src/audio/src/audio_capture.cpp` (loopback recording + format conversion)
  - Multi-channel format handling (stereo to mono downmix)

## Data Storage

**Databases:**
- None - Application is client-only

**File Storage:**
- Local filesystem only
  - Configuration: `%APPDATA%/MicMap/config.json` (runtime-persisted)
  - Training data: `training_data.bin` (binary pattern storage location defined in `config/default_config.json`)
  - Default config shipped: `config/default_config.json`

**Caching:**
- In-memory audio ring buffer
  - Implementation: `src/audio/include/micmap/audio/audio_buffer.hpp`
  - Ring buffer size: Configurable via `bufferSizeMs` in config (default: 10ms)
  - Audio samples cached for FFT spectral analysis

## Authentication & Identity

**Auth Provider:**
- None - No network authentication required
- Local application with driver-to-application HTTP communication only

**Local Communication:**
- HTTP RPC between driver and application
  - Implementation: `driver/src/http_server.cpp`
  - Protocol: HTTP/1.1 (no HTTPS)
  - Host: 127.0.0.1 (localhost only)
  - Port: 27015 (defined in `driver/resources/settings/default.vrsettings`)
  - Header-only library: cpp-httplib v0.14.3
  - Exception handling: `CPPHTTPLIB_NO_EXCEPTIONS` in driver builds

## Monitoring & Observability

**Error Tracking:**
- None - No external error tracking service

**Logs:**
- Console/debug output via `src/common/include/micmap/common/logger.hpp`
- No persistent logging to external services
- Application logging for debugging purposes

**VR Event Monitoring:**
- SteamVR event polling in `src/steamvr/src/dashboard_manager.cpp`
- Connection state monitoring: `ConnectionState` enum (Disconnected, Connecting, Connected, Reconnecting)
- Dashboard state tracking: `DashboardState` from `src/steamvr/include/micmap/steamvr/vr_input.hpp`

## CI/CD & Deployment

**Hosting:**
- Windows desktop application (no server hosting)
- SteamVR driver installation via batch script: `scripts/install_driver.bat`
- Driver installation location: `<SteamVR_Directory>/drivers/micmap/`

**CI Pipeline:**
- None configured - Build scripts only

**Installation & Deployment:**
- Batch scripts for Windows:
  - `scripts/install_driver.bat` - Registers driver with SteamVR, copies driver files
  - `scripts/uninstall_driver.bat` - Removes driver from SteamVR
  - `scripts/test_driver.bat` - Testing helper script

**Build Artifacts:**
- Main application: `build/bin/micmap.exe`
- Driver DLL: `build/driver/micmap/bin/win64/driver_micmap.dll`
- Test applications: `build/bin/mic_test.exe`, `build/bin/hmd_button_test.exe`

## Environment Configuration

**Required env vars:**
- `OPENVR_SDK_PATH` (optional but needed for driver compilation)
  - If not set, searches: `./external/openvr`, `C:/OpenVR`, `C:/Program Files/OpenVR`

**Application env vars:**
- None required - Configuration stored in `%APPDATA%/MicMap/config.json`

**Driver env vars:**
- Driver settings in `driver/resources/settings/default.vrsettings`:
  - `enable` - Enable/disable driver
  - `http_port` - HTTP server port (default: 27015)
  - `http_host` - HTTP server host (default: 127.0.0.1)
  - `autoLaunchApp` - Auto-launch MicMap app with SteamVR
  - `appPath` - Custom app launch path
  - `appArgs` - Custom app launch arguments

**Secrets location:**
- No secrets stored - Local-only communication
- Windows AppData permissions protect `%APPDATA%/MicMap/` directory

## Webhooks & Callbacks

**Incoming:**
- HTTP server listening for driver commands via cpp-httplib
- Endpoint: `http://127.0.0.1:27015` (localhost)
- Used for: Button press injection, driver-to-app communication

**Outgoing:**
- None - Application is client-only

**VR System Callbacks:**
- OpenVR event callbacks registered in `src/steamvr/src/dashboard_manager.cpp`
- Callback types:
  - `DashboardCallback` - Dashboard state changes
  - `ConnectionCallback` - SteamVR connection state changes (Connected, Disconnected, Reconnecting)
  - `QuitCallback` - SteamVR shutdown notifications
- Reconnection mechanism with configurable interval (default: 5000ms)

**Audio Callbacks:**
- WASAPI event-driven capture via Windows Events
- Audio buffer ring management in `src/audio/src/audio_buffer.cpp`

## VR Runtime Communication

**SteamVR Driver Protocol:**
- Virtual controller events injected via OpenVR API
- Action manifests: `config/actions/micmap_actions.json`
- Actions defined:
  - `/actions/micmap/in/dashboard_toggle` - Toggle dashboard visibility
  - `/actions/micmap/in/dashboard_select` - Select item in dashboard (HMD button behavior)
- Platform support: Knuckles, Vive, Oculus Touch, Windows Mixed Reality, Gamepad, HMD

**Microphone Input Flow:**
1. WASAPI device enumeration → `src/audio/src/device_enumerator.cpp`
2. WASAPI loopback capture → `src/audio/src/audio_capture.cpp`
3. Ring buffer storage → `src/audio/include/micmap/audio/audio_buffer.hpp`
4. FFT spectral analysis → `src/detection/src/spectral_analyzer.cpp` (KissFFT backend)
5. Pattern detection → `src/detection/src/noise_detector.cpp`
6. HTTP RPC to driver → `driver/src/http_server.cpp`
7. Virtual controller injection → `driver/src/virtual_controller.cpp`

---

*Integration audit: 2026-04-23*

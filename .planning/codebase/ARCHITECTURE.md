# Architecture

**Analysis Date:** 2026-04-23

## Pattern Overview

**Overall:** Layered architecture with plugin-driver model

**Key Characteristics:**
- Modular library-based design with core service libraries
- Separation of detection logic (audio processing) from VR integration
- Windows platform-specific with D3D11 GUI rendering
- Two-process model: application process + SteamVR driver plugin
- Interface-based abstractions for all major components (factory pattern)

## Layers

**Audio Layer:**
- Purpose: Capture raw microphone input and provide callbacks
- Location: `src/audio/`
- Contains: Audio device enumeration, WASAPI-based capture, circular buffer management
- Depends on: Windows WASAPI API
- Used by: Detection layer, main application

**Detection Layer:**
- Purpose: Analyze audio spectral characteristics and detect white noise patterns
- Location: `src/detection/`
- Contains: FFT-based spectral analyzer, noise detector with training capability, pattern trainer
- Depends on: Audio layer, external FFT library (KissFFT)
- Used by: State machine, main application

**Core Layer:**
- Purpose: Configuration management and state machine orchestration
- Location: `src/core/`
- Contains: Config manager (JSON serialization), state machine (idle/training/detecting/triggered/cooldown states)
- Depends on: Common utilities, filesystem
- Used by: Main application, all coordination logic

**SteamVR Integration Layer:**
- Purpose: Bridge microphone detection events to VR input actions
- Location: `src/steamvr/`
- Contains: VR input handler (OpenVR SDK), dashboard state management, driver client (HTTP communication)
- Depends on: OpenVR SDK, external HTTP library (cpp-httplib)
- Used by: Main application

**Common Layer:**
- Purpose: Shared utilities and type definitions
- Location: `src/common/`
- Contains: Logger, Result enumeration, common types (AudioSample, Timestamp, Duration)
- Depends on: C++ standard library
- Used by: All other layers

**Driver Layer (SteamVR Plugin):**
- Purpose: Virtual controller provider for SteamVR
- Location: `driver/`
- Contains: Device provider, virtual controller (button injection), HTTP server, driver logging
- Depends on: OpenVR SDK, cpp-httplib
- Used by: SteamVR runtime, main application (via HTTP)

**Application Layer:**
- Purpose: User interface and orchestration of all components
- Location: `apps/micmap/`
- Contains: ImGui-based UI, message handling, component lifecycle management
- Depends on: All libraries, ImGui, Direct3D 11
- Uses: All lower layers

## Data Flow

**Audio Capture to Detection:**

1. Audio callback from WASAPI → `audio::IAudioCapture::setAudioCallback()`
2. Callback receives raw float samples (-1.0 to 1.0)
3. Main application calculates RMS level and dB values
4. If training: samples fed to `detector->addTrainingSample()`
5. If detecting: samples analyzed via `detector->analyze()` → `DetectionResult`
6. Result includes: confidence, energy, spectral flatness, correlation, isWhiteNoise flag

**Detection to State Machine to Action:**

1. Detection result confidence → `stateMachine->update(confidence, deltaTime)`
2. State machine tracks: Idle, Training, Detecting, Triggered, Cooldown
3. When minimum duration met and threshold exceeded → fires trigger callback
4. Trigger callback → `onTrigger()` in main app
5. Checks dashboard state via `dashboardManager->getDashboardState()`
6. If closed: opens dashboard via `driverClient->click("system", 100)`
7. If open: sends selection click via `driverClient->click("trigger", 100)`

**Training Flow:**

1. User initiates training via UI button
2. State machine enters Training state
3. Audio samples collected via `detector->addTrainingSample()`
4. Auto-stop when `trainingSampleCount >= MIN_TRAINING_SAMPLES * 3` (150 samples)
5. `detector->finishTraining()` computes spectral profile and thresholds
6. Result saved to `%APPDATA%/MicMap/training_data.bin` via `detector->saveTrainingData()`
7. State machine resets to Idle, ready for detection

**Configuration Persistence:**

1. On startup: `configManager->loadDefault()` from `%APPDATA%/MicMap/config.json`
2. Loads: device selection, detection time (ms), sensitivity, SteamVR settings
3. On shutdown: `configManager->saveDefault()` writes current config
4. Training data persisted separately in binary format

**State Management:**

```
Idle --[training initiated]--> Training
Idle --[detection confidence > threshold + duration met]--> Triggered
Training --[finishTraining()]--> Idle
Detecting --[duration expired]--> Idle
Triggered --[cooldown elapsed]--> Idle
Cooldown --[timeout]--> Idle
```

## Key Abstractions

**IAudioCapture:**
- Purpose: Abstract over WASAPI implementation details
- Examples: `audio::createWASAPICapture()` in `src/audio/src/audio_capture.cpp`
- Pattern: Factory function returns unique_ptr to interface, implementation hidden

**INoiseDetector:**
- Purpose: Define white noise detection contract
- Examples: `detection::createFFTDetector()` in `src/detection/src/noise_detector.cpp`
- Pattern: Factory creates detector with configurable FFT size (default 2048)
- Supports training mode and detection mode with confidence scoring

**IStateMachine:**
- Purpose: Manage detection state transitions and timing constraints
- Examples: `core::createStateMachine()` in `src/core/src/state_machine.cpp`
- Pattern: Accepts detection confidence, tracks time in state, fires callbacks
- Configuration: minimum detection duration, cooldown, threshold

**IConfigManager:**
- Purpose: Serialize/deserialize application state
- Examples: `core::createConfigManager()` in `src/core/src/config_manager.cpp`
- Pattern: Loads from `%APPDATA%/MicMap/config.json`, supports defaults
- Hierarchical config: AudioConfig, DetectionConfig, SteamVRConfig, TrainingConfig

**IVRInput:**
- Purpose: OpenVR SDK wrapper for HMD button events
- Examples: `steamvr::createOpenVRInput()` in `src/steamvr/src/vr_input.cpp`
- Pattern: Async initialization, event polling, dashboard state queries

**IDriverClient:**
- Purpose: HTTP communication with virtual controller driver
- Examples: `steamvr::createDriverClient()` in `src/steamvr/src/vr_input.cpp`
- Pattern: Connects to driver's HTTP server, sends button click commands

**IDashboardManager:**
- Purpose: Unified dashboard action handling
- Examples: `steamvr::createDashboardManager()` in `src/steamvr/src/dashboard_manager.cpp`
- Pattern: Wraps VR input and driver client, provides `performDashboardAction()`

## Entry Points

**Main Application (micmap.exe):**
- Location: `apps/micmap/main.cpp`
- Triggers: User runs executable or system tray context menu
- Responsibilities:
  - Creates window and ImGui context
  - Initializes all subsystems (audio, detection, VR, config)
  - Runs message loop and renders UI
  - Handles tray icon and window messages
  - Manages async VR/driver initialization threads
  - Coordinates audio callbacks with state machine and trigger logic

**SteamVR Driver (driver_micmap.dll):**
- Location: `driver/src/driver_main.cpp`
- Triggers: SteamVR runtime on startup (reads from `driver/driver.vrdrivermanifest`)
- Responsibilities:
  - Implements `HmdDriverFactory()` exported function
  - Provides DeviceProvider to SteamVR
  - Runs HTTP server for receiving click commands from application
  - Creates virtual controller device
  - Injects button events to SteamVR

**Test Applications:**
- **mic_test.exe** (`apps/mic_test/main.cpp`): Audio and detection testing without VR
- **hmd_button_test.exe** (`apps/hmd_button_test/main.cpp`): VR input testing without audio

## Error Handling

**Strategy:** Graceful degradation with logging

**Patterns:**
- Audio capture failure → detection disabled, logs warning
- VR connection failure → retries async, displays "Not Connected" in UI
- Device switch failure → logs error, keeps previous device
- Invalid config → loads defaults, retries
- Missing training data → detection disabled until training completes
- State machine timing errors → cooldown prevents trigger spam

## Cross-Cutting Concerns

**Logging:** 
- Implementation: `common::Logger` in `src/common/src/logger.cpp`
- Approach: Centralized logger with level filtering, writes to `%APPDATA%/MicMap/logs/`

**Validation:**
- Config validation on load (version check, range clamping)
- Audio device validation before selection
- Sample rate validation before detector creation
- Training sample count validation (minimum 150 for auto-stop)

**Authentication:**
- HTTP driver communication: port-based security (localhost only)
- Config file: standard APPDATA permissions (user-specific)
- No external authentication required

---

*Architecture analysis: 2026-04-23*

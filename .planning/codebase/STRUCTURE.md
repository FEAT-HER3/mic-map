# Codebase Structure

**Analysis Date:** 2026-04-23

## Directory Layout

```
mic-map/
├── .planning/              # GSD planning documents
├── apps/                   # Application executables
│   ├── micmap/            # Main MicMap application (GUI + orchestration)
│   ├── mic_test/          # Audio capture and detection testing
│   └── hmd_button_test/   # VR input testing
├── driver/                # SteamVR virtual controller driver
│   ├── src/              # Driver implementation (C++)
│   ├── resources/        # VR configuration files (JSON, vrsettings)
│   └── driver.vrdrivermanifest  # SteamVR driver registration
├── src/                   # Core libraries
│   ├── audio/            # WASAPI audio capture and buffering
│   ├── common/           # Shared utilities and logging
│   ├── core/             # Configuration and state management
│   ├── detection/        # Spectral analysis and noise detection
│   └── steamvr/          # SteamVR integration and VR input
├── config/               # Default configuration files
├── docs/                 # Project documentation
├── scripts/              # Installation and utility scripts
├── external/             # External dependencies (ImGui, OpenVR SDK)
├── cmake/                # CMake helper modules
├── tests/                # Test suites
├── build/                # Build artifacts (CMake output)
├── CMakeLists.txt        # Root CMake configuration
└── README.md             # Project overview
```

## Directory Purposes

**apps/**
- Purpose: Executable applications - main app and test utilities
- Contains: Win32/ImGui UI code, entry points, message loops
- Key files: `main.cpp` for each app, resource definitions

**apps/micmap/**
- Purpose: Main user-facing application
- Contains: ImGui GUI rendering, component orchestration, message handling
- Key files: `main.cpp` (1100 lines)

**apps/mic_test/**
- Purpose: Standalone audio capture and detection testing without VR
- Contains: Simple Win32 GUI for device selection and training
- Key files: `main.cpp` (500+ lines), testing patterns

**apps/hmd_button_test/**
- Purpose: SteamVR virtual controller and dashboard interaction testing
- Contains: Basic button click test without audio processing
- Key files: `main.cpp`

**driver/**
- Purpose: SteamVR plugin DLL that provides virtual controller
- Contains: Device provider, virtual controller, HTTP server for IPC
- Key files: `driver_main.cpp` (entry point), `device_provider.cpp`, `virtual_controller.cpp`, `http_server.cpp`

**driver/resources/**
- Purpose: VR runtime configuration
- Contains: Controller profile JSON, input bindings, driver settings
- Key files: `micmap_controller_profile.json`, `default.vrsettings`

**src/**
- Purpose: Core libraries - all business logic
- Contains: Modular library code with clear interfaces
- Key files: CMakeLists.txt for multi-library build orchestration

**src/audio/**
- Purpose: Audio input abstraction over Windows WASAPI
- Contains: Device enumeration, capture loop, circular buffer
- Key files: 
  - `include/micmap/audio/audio_capture.hpp` - interface definition
  - `include/micmap/audio/device_enumerator.hpp` - device info structs
  - `src/audio_capture.cpp` - WASAPI implementation
  - `src/device_enumerator.cpp` - Windows audio device enumeration

**src/common/**
- Purpose: Shared utilities across all modules
- Contains: Logger, Result type, common type definitions
- Key files:
  - `include/micmap/common/logger.hpp` - logging interface
  - `include/micmap/common/types.hpp` - Result enum, Timestamp, Duration

**src/core/**
- Purpose: Application state and configuration management
- Contains: JSON config loading/saving, state machine
- Key files:
  - `include/micmap/core/config_manager.hpp` - AppConfig struct (audio, detection, steamvr, training)
  - `include/micmap/core/state_machine.hpp` - State enum (Idle/Training/Detecting/Triggered/Cooldown)
  - `src/config_manager.cpp` - APPDATA-based persistence
  - `src/state_machine.cpp` - timing and state transition logic

**src/detection/**
- Purpose: Audio analysis and white noise detection
- Contains: FFT-based spectral analysis, training, pattern recognition
- Key files:
  - `include/micmap/detection/noise_detector.hpp` - training/detection interface
  - `include/micmap/detection/spectral_analyzer.hpp` - FFT wrapper
  - `src/noise_detector.cpp` - confidence scoring, duration tracking
  - `src/spectral_analyzer.cpp` - FFT computation, spectral metrics
  - `src/pattern_trainer.cpp` - training data aggregation

**src/steamvr/**
- Purpose: SteamVR runtime integration
- Contains: OpenVR SDK wrapper, driver IPC, dashboard state
- Key files:
  - `include/micmap/steamvr/vr_input.hpp` - dashboard state, button events
  - `include/micmap/steamvr/dashboard_manager.hpp` - unified action handler
  - `src/vr_input.cpp` - OpenVR initialization, event polling
  - `src/dashboard_manager.cpp` - intelligently opens/clicks dashboard

**config/**
- Purpose: Default configuration templates
- Contains: JSON schema for user settings
- Key files: `config.json` (default detection time, device, sensitivity)

**docs/**
- Purpose: Developer and user documentation
- Contains: Architecture notes, API documentation
- Key files: Referenced in README.md

**scripts/**
- Purpose: Installation and maintenance scripts
- Contains: Batch files for driver registration, uninstallation
- Key files: 
  - `install_driver.bat` - copies driver to SteamVR directory
  - `uninstall_driver.bat` - removes driver files

**external/**
- Purpose: Third-party source code and headers
- Contains: ImGui, OpenVR SDK, nlohmann_json, cpp-httplib, KissFFT
- Key files: Not analyzed (dependency sources)

**cmake/**
- Purpose: CMake helper modules
- Contains: Find scripts for dependencies
- Key files: Custom find modules for ImGui, OpenVR

**tests/**
- Purpose: Unit and integration tests
- Contains: Test fixtures and test cases
- Key files: Depends on build configuration (MICMAP_BUILD_TESTS)

## Key File Locations

**Entry Points:**
- `apps/micmap/main.cpp` - Main application (WinMain, ImGui render loop)
- `driver/src/driver_main.cpp` - SteamVR driver (HmdDriverFactory export)
- `apps/mic_test/main.cpp` - Audio test app (simple detection testing)

**Configuration:**
- `config/config.json` - Default user settings template
- `driver/driver.vrdrivermanifest` - Driver registration metadata
- `driver/resources/default.vrsettings` - VR runtime settings
- `CMakeLists.txt` - Build configuration (root and per-directory)

**Core Logic:**
- `src/audio/src/audio_capture.cpp` - WASAPI capture implementation
- `src/detection/src/noise_detector.cpp` - Confidence scoring and duration tracking
- `src/detection/src/spectral_analyzer.cpp` - FFT-based analysis
- `src/core/src/state_machine.cpp` - State transitions and timing
- `src/core/src/config_manager.cpp` - JSON serialization

**SteamVR Integration:**
- `src/steamvr/src/vr_input.cpp` - OpenVR SDK wrapper
- `src/steamvr/src/dashboard_manager.cpp` - Dashboard action coordination
- `driver/src/device_provider.cpp` - Virtual controller provider
- `driver/src/virtual_controller.cpp` - Button injection logic
- `driver/src/http_server.cpp` - IPC server for button commands

**Testing:**
- `apps/mic_test/main.cpp` - Audio and detection testing patterns
- `apps/hmd_button_test/main.cpp` - VR button testing patterns

## Naming Conventions

**Files:**
- Header files: `*.hpp` (modern C++ style)
- Implementation files: `*.cpp`
- Resource files: `*.vrdrivermanifest`, `*.vrsettings`, `*.json`, `*.rc`
- Example: `audio_capture.hpp` paired with `audio_capture.cpp` in sibling `src/` directory

**Directories:**
- Library folders: lowercase descriptive name (`audio`, `detection`, `steamvr`)
- Nested structure: `<lib>/include/micmap/<lib>/` and `<lib>/src/`
- Application folders: descriptive name (`micmap`, `mic_test`, `hmd_button_test`)

**Classes and Types:**
- Interfaces: `I` prefix + descriptive name (`IAudioCapture`, `INoiseDetector`, `IStateMachine`)
- Implementations: Factory functions + descriptive names (e.g., `createWASAPICapture()` returns `IAudioCapture*`)
- Enums: PascalCase (`State`, `DashboardState`, `VREventType`)
- Structs: PascalCase descriptive name (`AudioDevice`, `DetectionResult`, `AppConfig`)

**Functions and Methods:**
- camelCase with descriptive verbs (`startCapture()`, `addTrainingSample()`, `performDashboardAction()`)
- Getters: `get*` or `is*` prefix (`getCurrentDevice()`, `isCapturing()`, `hasTrainingData()`)
- Setters: `set*` prefix (`setAudioCallback()`, `setSensitivity()`, `setTriggerCallback()`)

**Variables:**
- Local/member: camelCase (`audioCapture`, `detectionResult`, `currentLevel`)
- Constants: UPPER_SNAKE_CASE (`MIN_TRAINING_SAMPLES`, `WINDOW_WIDTH`, `BUTTON_FIRE_DURATION_MS`)
- Atomic types: descriptive camelCase (`detectionActive`, `inCooldown`, `trainingSampleCount`)

## Where to Add New Code

**New Feature (audio processing enhancement):**
- Primary code: `src/detection/src/` (analysis logic) and `src/detection/include/micmap/detection/` (interface)
- Tests: `tests/` (if test coverage is added)
- Example: New spectral feature computation goes in `spectral_analyzer.cpp`

**New Component/Module (e.g., recording, playback):**
- Implementation: Create `src/<module_name>/` with `include/micmap/<module_name>/` and `src/` subdirectories
- Header pattern: `<module_name>_interface.hpp` with `I<ModuleName>` interface
- Factory: `create<Module>()` function in namespace `micmap::<module_name>`
- Build: Add `CMakeLists.txt` in new directory, link in root `src/CMakeLists.txt`

**Utilities (helper functions):**
- Shared helpers: `src/common/include/micmap/common/` (types) or `src/common/src/` (implementation)
- Library-specific helpers: Keep in that library's `include/micmap/<lib>/` directory
- Example: FFT utilities stay in `src/detection/` even if potentially reusable

**Test Application:**
- Implementation: Create `apps/<test_name>/main.cpp` with Win32 message loop
- Build: Add `CMakeLists.txt`, link required libraries
- Pattern: Follow `mic_test/main.cpp` structure (simple GUI, minimal dependencies)

**SteamVR Integration:**
- VR interaction: Extend `src/steamvr/src/vr_input.cpp` and dashboard_manager
- Driver functionality: Modify `driver/src/` files
- Controller commands: Add methods to `virtual_controller.cpp`

**UI Components:**
- ImGui code: Keep in `apps/micmap/main.cpp` within `MicMapApp::renderUI()`
- Callbacks: Add to audio callback lambda in `MicMapApp::initialize()` or new `onTrigger()` methods
- Settings panels: Add ImGui sections in `renderUI()`

## Special Directories

**build/**
- Purpose: CMake output and build artifacts
- Generated: Yes (from `cmake -B build`)
- Committed: No (in .gitignore)
- Contains: Compiled libraries, executables, intermediate object files

**external/**
- Purpose: Third-party source code
- Generated: No (part of repository)
- Committed: Yes (some libraries via git submodules or FetchContent)
- Contains: ImGui, OpenVR SDK headers, nlohmann_json, cpp-httplib

**.planning/codebase/**
- Purpose: GSD codebase analysis documents
- Generated: No (written by gsd-map-codebase)
- Committed: Yes (referenced by gsd-plan-phase and gsd-execute-phase)
- Contains: ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, CONCERNS.md, STACK.md, INTEGRATIONS.md

---

*Structure analysis: 2026-04-23*

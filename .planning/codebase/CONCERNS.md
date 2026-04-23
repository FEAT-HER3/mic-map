# Codebase Concerns

**Analysis Date:** 2026-04-23

## Tech Debt

**JSON Configuration Parsing Not Implemented:**
- Issue: Configuration file parsing is stubbed - only writes JSON but doesn't read/parse it back
- Files: `src/core/src/config_manager.cpp` (line 142)
- Impact: User settings are not persisted across sessions; application always runs with default configuration
- Fix approach: Implement proper JSON parsing using nlohmann/json library (already available as dependency)

**SteamVR Overlay Management Incomplete:**
- Issue: Four critical overlay functions are stubbed with TODO comments but return success:
  - `createSettingsOverlay()` - Creates overlay object but doesn't invoke OpenVR API
  - `showOverlay()` - Sets state flag but doesn't show to user
  - `hideOverlay()` - Sets state flag but doesn't hide from user
  - Overlay destruction similarly incomplete
- Files: `src/steamvr/src/dashboard_manager.cpp` (lines 218, 250, 265, 361)
- Impact: Settings overlay UI cannot be displayed to users; Stage 2 functionality is non-functional
- Fix approach: Implement actual OpenVR IVROverlay API calls (vr::VROverlay()->CreateDashboardOverlay, ShowOverlay, HideOverlay)

**Partial Implementation on Non-Windows Platforms:**
- Issue: Audio capture and device enumeration return empty stubs on non-Windows platforms
- Files: `src/audio/src/audio_capture.cpp` (lines 763, 769-770) and `src/audio/src/device_enumerator.cpp` (lines 227-239)
- Impact: Application is Windows-only; no platform-specific implementations exist for Linux/macOS
- Fix approach: Either add platform implementations or document Windows-only requirement clearly

## Known Bugs

**Subpar Microphone Detection Accuracy:**
- Symptoms: Detection algorithm struggles to distinguish microphone covering from loud ambient sounds (music, loud voices)
- Files: `src/detection/src/noise_detector.cpp`, `src/detection/src/pattern_trainer.cpp`
- Trigger: Using detection in non-quiet environments; training in quiet environment then using in loud environment
- Workaround: Retrain pattern in the specific environment where it will be used; lower detection sensitivity threshold; increase minimum detection duration

**Visual Beam from HMD During Dashboard Interaction:**
- Symptoms: When virtual controller activates, SteamVR displays visible beam/ray from headset to laser pointer
- Files: Likely in driver input handling or OpenVR integration
- Trigger: Any successful detection event that triggers dashboard action
- Workaround: Purely cosmetic issue - functionality works despite visual artifact

**Missing Auto-start Feature:**
- Symptoms: MicMap application does not automatically launch when SteamVR starts
- Files: Installation script and application lifecycle management
- Trigger: Starting SteamVR without manually launching MicMap
- Workaround: Manually launch `build/apps/micmap/Release/micmap.exe` or add to Windows startup folder

## Security Considerations

**HTTP Client Communication with Driver:**
- Risk: Driver client uses unencrypted HTTP on localhost for button injection commands
- Files: `src/steamvr/src/vr_input.cpp` (DriverClient implementation, lines 158-300+)
- Current mitigation: Communication is localhost-only (127.0.0.1), not exposed to network
- Recommendations: 
  - Verify that HTTP client doesn't accept external network connections
  - Consider adding local-only binding check in driver server
  - Document that this requires driver and application on same machine

**No Input Validation on Configuration:**
- Risk: Configuration values are read from JSON but not validated before use
- Files: `src/core/src/config_manager.cpp` - JSON parsing stub
- Current mitigation: Default values are used initially; no user-provided values are loaded
- Recommendations: When JSON parsing is implemented, add bounds checking on sensitivity, thresholds, and duration values

**COM Object Lifecycle in Audio Capture:**
- Risk: DeviceNotificationClient manually manages reference counting with COM objects
- Files: `src/audio/src/audio_capture.cpp` (lines 116-157)
- Current mitigation: Uses Microsoft WRL::ComPtr for most COM objects (automatic reference counting)
- Recommendations: Verify DeviceNotificationClient reference counting is correct; consider wrapping in ComPtr for consistency

## Performance Bottlenecks

**Audio Buffer Accumulation:**
- Problem: Audio capture buffer grows indefinitely until maximum size (1 second of samples at sample rate)
- Files: `src/audio/src/audio_capture.cpp` (lines 571-578)
- Cause: If detection pipeline cannot consume audio fast enough, buffer will grow and be trimmed by erasing oldest samples
- Improvement path: Implement circular buffer or ring buffer instead of vector erase operations; implement backpressure

**FFT Analysis on Every Audio Frame:**
- Problem: Spectral analysis (FFT) is computed for every captured audio chunk
- Files: `src/detection/src/noise_detector.cpp`
- Cause: Provides low-latency detection but may be computationally expensive on slower CPUs
- Improvement path: Implement frame aggregation or decimation to reduce FFT frequency; profile CPU usage on target systems

**Device Enumeration on Startup:**
- Problem: All WASAPI devices are enumerated and properties fetched during initialization
- Files: `src/audio/src/device_enumerator.cpp`
- Cause: Could cause UI blocking on systems with many audio devices or slow device enumeration
- Improvement path: Implement async device enumeration; cache device list with refresh option

## Fragile Areas

**State Machine Thread Safety:**
- Files: `src/core/src/state_machine.cpp`, `src/core/include/micmap/core/state_machine.hpp`
- Why fragile: State transitions can occur from audio capture thread, detection thread, and VR event callbacks; minimal synchronization primitives
- Safe modification: All state transitions must use the same mutex; add thread-safety documentation
- Test coverage: No unit tests for concurrent state transitions; high risk of race conditions

**Audio Device Disconnect Handling:**
- Files: `src/audio/src/audio_capture.cpp` (DeviceNotificationClient, onDeviceRemoved)
- Why fragile: Device disconnection sets `deviceLost_` flag but capture thread may still be waiting on audio events
- Safe modification: Verify that capture event is signaled immediately when device is lost; add integration test for device removal during capture
- Test coverage: No test coverage for device disconnection scenario

**Configuration File Path Resolution:**
- Files: `src/core/src/config_manager.cpp` (lines 25-34, getAppDataPath)
- Why fragile: Falls back to current directory if SHGetFolderPathW fails; may not be writable
- Safe modification: Add fallback priority (temp dir, executable dir) and explicit error logging; don't silently fall through
- Test coverage: No test for fallback paths

**Detection Temporal Consistency:**
- Files: `src/detection/src/noise_detector.cpp` (temporal consistency window)
- Why fragile: Default minimum detection duration is hardcoded to 300ms; may not work for all microphone types
- Safe modification: Make minimum duration configurable; add logging when threshold is adjusted
- Test coverage: Pattern trainer has acceptance criteria (>5 samples) but detector has no direct coverage

## Scaling Limits

**Single Microphone Device:**
- Current capacity: One audio input device at a time
- Limit: Cannot simultaneously monitor multiple microphones
- Scaling path: Implement multi-device support; aggregate multiple device streams; add device selection per detector instance

**Training Data Storage:**
- Current capacity: Single training profile stored per application
- Limit: Cannot maintain separate profiles for different environments/microphones
- Scaling path: Implement profile management system; store multiple named profiles; allow per-profile configuration

**Real-time Audio Processing:**
- Current capacity: Single detection pipeline processing at system sample rate
- Limit: If sample rate is very high (192kHz+) or CPU is slow, may drop samples or lag
- Scaling path: Implement adaptive downsampling; allow configurable analysis frame size

## Dependencies at Risk

**cpp-httplib (Embedded HTTP Client):**
- Risk: Embedded single-header library; may not receive security updates quickly
- Impact: Used for driver communication; vulnerability in HTTP parsing could affect driver commands
- Migration plan: Monitor cpp-httplib releases; consider migrating to libcurl if security issues arise

**KissFFT (Embedded FFT Library):**
- Risk: Legacy FFT implementation; may have performance issues on modern hardware
- Impact: Core to noise detection algorithm; suboptimal FFT could impact detection accuracy
- Migration plan: Evaluate modern FFT libraries (FFTW, Intel MKL) if performance becomes issue

## Missing Critical Features

**Configuration UI/Dashboard Overlay:**
- Problem: No user-facing settings overlay in VR; users must manually edit JSON
- Blocks: Convenient runtime adjustment of sensitivity, detection duration, device selection
- Priority: High (listed as Stage 2 work)

**Auto-start Integration:**
- Problem: Application doesn't launch with SteamVR despite installation infrastructure
- Blocks: Hands-free experience; users must remember to launch manually
- Priority: High (README acknowledges this as known issue)

**Multi-Profile Support:**
- Problem: Single training profile for all scenarios
- Blocks: Adapting to different microphones, environments, or use patterns
- Priority: Medium

**Undo/Revert Training:**
- Problem: No way to revert to previous training if new training is poor
- Blocks: Easy recovery from bad training sessions
- Priority: Low

## Test Coverage Gaps

**No Unit Tests for Core Detection:**
- What's not tested: FFT analysis accuracy, temporal consistency logic, pattern matching thresholds
- Files: `src/detection/src/noise_detector.cpp`, `src/detection/src/pattern_trainer.cpp`
- Risk: Changes to detection algorithm could silently break accuracy without test failure
- Priority: High

**No Tests for State Transitions:**
- What's not tested: Concurrent state changes from multiple threads; lifecycle (init → training → detecting → shutdown)
- Files: `src/core/src/state_machine.cpp`
- Risk: Race conditions in state machine may only manifest under specific timing conditions
- Priority: High

**No Integration Tests for Audio Capture:**
- What's not tested: Device enumeration with many devices, device disconnection during capture, format conversion edge cases
- Files: `src/audio/src/audio_capture.cpp`, `src/audio/src/device_enumerator.cpp`
- Risk: Audio subsystem failures may not be discovered until deployed on diverse hardware
- Priority: High

**No Tests for VR Input/Dashboard Manager:**
- What's not tested: Reconnection logic, port scanning fallback, HTTP client retry behavior
- Files: `src/steamvr/src/vr_input.cpp`, `src/steamvr/src/dashboard_manager.cpp`
- Risk: Connection failures or driver disconnection may leave system in bad state
- Priority: Medium

**No Tests for Configuration Persistence:**
- What's not tested: Config file reading/writing, fallback paths, corruption recovery
- Files: `src/core/src/config_manager.cpp`
- Risk: Will become critical once JSON parsing is implemented
- Priority: Medium (currently lower since parsing is not implemented)

**Empty Test Placeholder:**
- What's not tested: Everything
- Files: `tests/test_placeholder.cpp`
- Current state: Single test that always passes; does not exercise any application code
- Priority: Critical - test infrastructure exists but is not being used

---

*Concerns audit: 2026-04-23*

# Research Summary -- v1.6 Feature Migration

**Project:** MicMap
**Domain:** SteamVR driver-resident audio detection pipeline (brownfield migration)
**Researched:** 2026-04-30
**Confidence:** HIGH

## Executive Summary

MicMap v1.6 is a brownfield migration milestone. v1.5 shipped a working two-process architecture: micmap.exe owns audio capture, FFT detection, state machine, and the trigger path (HTTP POST to driver); driver_micmap.dll is a thin HTTP receiver that drains a CommandQueue from RunFrame. v1.6 inverts this: the driver becomes the primary end-to-end runtime hosting WASAPI capture, FFT detection, state machine, config persistence, and the trigger pipeline. The client demotes to a settings editor and health observer. A new micmap_core_runtime INTERFACE CMake target aggregates the existing static libs (micmap_audio, micmap_detection, micmap_core, micmap_common) into a shared boundary consumed by both the driver DLL and the client EXE with no OpenVR symbols crossing the shared layer.

The recommended approach is a 7-phase incremental migration with a hard phase-1-first invariant. Phase 1 extracts the shared library boundary (structural refactor, no behavior change). Phase 2 spikes WASAPI capture inside the driver (feasibility validation, feature-flagged OFF). Phase 3 adds the driver-side detection thread (flag-gated, parallel with client detection). Phase 4 reshapes IPC (settings push, health pull, dead trigger route deleted). Phase 5 migrates training flow to driver-resident. Phase 6 flips the flag and deletes dead code. Phase 7 ships the deferred documentation. Each phase ends in a self-consistent, UAT-able state. The v1.5 CommandQueue boundary (only RunFrame touches OpenVR API) is the load-bearing invariant that must survive every phase.

Key risks: (1) COM apartment incompatibility when WASAPI initializes inside vrserver -- mitigated by spinning a dedicated audio worker thread that owns its own MTA init; (2) WASAPI lifecycle leaks across Cleanup/Init cycles -- mitigated by strict reverse-order teardown and a 50-cycle stress test; (3) detection thread bypassing CommandQueue and calling UpdateBooleanComponent directly -- mitigated by a grep-based CI lint asserting VRDriverInput only appears in device_provider.cpp; (4) two-process config race -- eliminated by making the driver the sole config.json writer; (5) double-trigger during phased migration -- mitigated by a single MICMAP_DRIVER_OWNS_DETECTION feature flag.

## Key Findings

### Recommended Stack

No new third-party dependencies are required for v1.6. The locked v1.5 stack (C++17, CMake, ImGui+D3D11, WASAPI, KissFFT, nlohmann/json, cpp-httplib v0.14.3, OpenVR SDK, Inno Setup) is unchanged. The migration is structural: existing static libs get a new INTERFACE facade target, the driver consumes them, the IPC surface shrinks on the trigger path and grows on the settings/health path, and a ~80 LoC FileLogSink fills the driver-has-no-stdout gap. One dep version bump is recommended: cpp-httplib v0.14.3 to v0.20.1 to cover CVE-2025-46728 -- defer to a dedicated plan inside Phase 1.

**Core technologies:**
- WASAPI (src/audio/): unchanged; reused inside driver DLL -- vrserver is a user process, no session-0 restriction, CoInitializeEx on capture thread already correct
- micmap_core_runtime INTERFACE target: new CMake aggregate over existing STATIC libs; no source movement required
- cmake/AssertNoOpenVRInCore.cmake: new symbol-leak guard asserting audio/detection/core/common do not link OpenVR
- FileLogSink (~80 LoC): new in src/common/; bridges to existing Logger interface and to OpenVR DriverLog simultaneously
- cpp-httplib (keep, no transport swap): same port range 27015-27025; POST /button deleted; GET /state, PUT /settings, GET /devices, POST+GET /training/* added

### Expected Features

Brownfield migration catalog across 7 clusters (MIG-, LIB-, IPC-S, TRAIN-, HEALTH-, TEST-, FAIL-). The feature surface covers the migration delta only -- shipped v1.5 features are not re-listed.

**Must have (table stakes):**
- MIG-01: Driver hosts WASAPI capture on its own thread; lifecycle aligned with Init/Cleanup
- MIG-02: Detection runs continuously while SteamVR is running regardless of HMD activation state
- MIG-03: Detection pauses cleanly on EnterStandby, resumes on LeaveStandby
- MIG-04: Trigger is a driver-internal CommandQueue push; POST /button route deleted
- LIB-01: micmap_core_runtime shared static lib -- no OpenVR dep, no ImGui dep; compiles into driver, client, and mic_test.exe
- LIB-02: Logger abstraction with DriverLogSink (driver) and StdoutLogSink (client/mic_test)
- LIB-03: Config ownership -- driver reads at boot, client pushes via PUT /settings, never writes config.json directly
- IPC-S-01..06: Settings push (full AppConfig, atomic whole-object), hot-apply scalars, re-init on device change, rejection with structured error, persistence ordering, device enumeration via GET /devices
- TRAIN-01..05: Driver-resident training (client observer only), sample-count streaming at 5-10 Hz, threshold preview/confirm/discard, driver writes training_data.bin, training mutex with detection
- HEALTH-01..07: Driver-loaded indicator, SteamVR-running indicator, detection-state pill, last-trigger timestamp + last-error, live RMS/dB level meter (poll-based), adaptive poll cadence, device-disappearance recovery
- TEST-01..03: mic_test.exe continues working against shared lib; --debug-trigger CLI flag; stdout log capture
- FAIL-01..05: Mic permission denied, driver not loaded, SteamVR not running, double-instance guard, driver not installed
- DOC-01, DOC-02: README sync and architecture.md (v1.5 Phase 5 carryover)

**Should have (differentiators):**
- HEALTH-D1: Per-component health badges with hover tooltips
- HEALTH-D4: Tray-icon overlay reflecting armed/triggered/error state
- TEST-D1: --replay mode for mic_test.exe (WAV file as live mic input) -- modest cost, large QA value; recommend pulling into v1.6

**Defer (v2+):**
- HEALTH-D2: Trigger-history sparkline
- TRAIN-D1: Recompute thresholds without re-collecting
- TRAIN-D2: A/B threshold preview
- HEALTH-D3: In-VR overlay status pill (UX-02, explicitly out of scope)
- TEST-D2: /debug/snapshot driver endpoint

**Anti-features (explicitly avoid):**
- TRAIN-AF-01: Client takes mic back during training -- two processes contend for one WASAPI handle
- IPC-AF-01: Driver watches config.json with ReadDirectoryChangesW -- torn reads, race conditions
- MIG-AF-02: Client-side audio as runtime fallback -- two code paths, two test matrices
- IPC-AF-02: Replace HTTP IPC with named pipes/shared memory -- no latency benefit (trigger goes in-process)

### Architecture Approach

The driver becomes the primary runtime hosting 4 threads: WASAPI capture thread (existing WASAPIAudioCapture), detection thread (new DetectionRunner class), HTTP server thread (existing HttpServer), and vrserver RunFrame (not owned by driver). A new SPSC lock-free ring bridges WASAPI callback to DetectionRunner, decoupling the hot audio path from FFT latency. DetectionRunner pushes TapCommand into the existing CommandQueue; only RunFrame drains the queue and calls UpdateBooleanComponent -- the v1.5 SVR-05 invariant preserved unchanged. A new SettingsSnapshot (std::atomic<std::shared_ptr<const AppConfig>>) enables lock-free settings reads on the detection hot path. The trigger path collapses from 7 hops cross-process to 4 hops in-process.

**Major components (new or heavily modified):**
1. micmap_core_runtime (CMake INTERFACE target) -- aggregates existing STATIC libs; consumed by driver, client, mic_test.exe; no OpenVR symbols
2. DetectionRunner (new: driver/src/detection_runner.{hpp,cpp}) -- owns sample ring, detection thread loop, state-machine TriggerCallback
3. SampleRing (new: driver/src/sample_ring.hpp) -- SPSC lock-free ring, WASAPI producer to DetectionRunner consumer
4. SettingsSnapshot (new: driver/src/settings_snapshot.hpp) -- atomic<shared_ptr<const AppConfig>> wrapper
5. TrainingSession (new: driver/src/training_session.{hpp,cpp}) -- single-session training lifecycle, HTTP handlers delegate to it
6. IDriverApi (renamed from IDriverClient: src/steamvr/src/vr_input.{hpp,cpp}) -- broadened HTTP client surface; tap() deleted
7. DeviceProvider (heavily modified: driver/src/device_provider.{hpp,cpp}) -- Init constructs audio+detection; Cleanup tears down in reverse order
8. HttpServer (modified: driver/src/http_server.{hpp,cpp}) -- POST /button deleted at Phase 6; GET /state, PUT /settings, GET /devices, POST+GET /training/* added
9. apps/micmap/main.cpp (slimmed ~1061 to ~500 lines) -- WASAPI callback FFT/state-machine body deleted; settings form PUTs to driver; training UI polls driver

**IPC delta:**
- DELETED: POST /button (trigger no longer crosses wire)
- ADDED: GET /state, GET /telemetry/level, GET /devices, GET /settings, PUT /settings, POST /training/start, GET /training/progress, POST /training/finalize, POST /training/cancel
- UNCHANGED: GET /health, GET /port (liveness and port discovery)

**Config ownership:**
- Driver sole writer of config.json (after PUT /settings validates and persists)
- Driver sole writer of training_data.bin (after POST /training/finalize)
- Client reads config.json at startup as fallback only; driver snapshot wins once IPC is available

### Critical Pitfalls

1. **COM apartment incompatibility (Pitfall 1)** -- CoInitializeEx must only be called on the dedicated audio worker thread, not on DeviceProvider::Init or RunFrame thread. vrserver threads may be STA; RPC_E_CHANGED_MODE (0x80010106) is not safe to ignore. Existing audio_capture.cpp already correct -- preserve it.
2. **Detection thread calls OpenVR directly (Pitfall 3)** -- in-process detection tempts devs to skip CommandQueue and call UpdateBooleanComponent from detection thread. OpenVR is not thread-safe off RunFrame. Add CMake grep check asserting VRDriverInput only appears in device_provider.cpp.
3. **WASAPI lifecycle leak on Cleanup/Init cycles (Pitfall 4)** -- Cleanup must stop in reverse construction order: detection thread, audio capture (join + unregister IMMNotificationClient + Release ComPtrs + CoUninitialize), HTTP server. Add 50-cycle stress test.
4. **Config file race between two processes (Pitfall 5)** -- eliminated by making driver the sole config.json writer via PUT /settings IPC. Driver reads file once at Init with 3-attempt retry on ERROR_SHARING_VIOLATION. No file-watcher in driver.
5. **Double-trigger during phased migration (Pitfall 10)** -- both client-side and driver-side detection active mid-milestone. Single MICMAP_DRIVER_OWNS_DETECTION runtime flag gates which side acts. Deleted at Phase 6.
6. **IMMNotificationClient fires after Cleanup (Pitfall 13)** -- wrap callback in shared_ptr<State> with atomic<bool> alive. Set alive=false before Unregister. Callback checks alive before dereferencing.
7. **Static lib ODR violations (Pitfall 6)** -- no __declspec(dllexport) in shared lib; no #ifdef MICMAP_DRIVER_BUILD in shared lib; Logger sinks injected at construction; dumpbin /exports check on every CI build.

## Implications for Roadmap

Based on combined research, 7 phases in load-bearing order:

### Phase 1: Shared Library Extraction
**Rationale:** Keystone refactor; every subsequent phase requires the shared lib boundary to exist cleanly. Pure CMake INTERFACE target addition -- no source movement, no behavior change. LIB-01 is the dependency root of the entire feature graph.
**Delivers:** micmap_core_runtime INTERFACE target; cmake/AssertNoOpenVRInCore.cmake guard; mic_test.exe switches to core_runtime; driver links core_runtime (without using it yet); headless build verified.
**Features:** LIB-01, LIB-02, partial LIB-03
**Avoids:** Pitfall 6 (ODR/symbol bloat), Pitfall 9 (mic_test drift), Pitfall 10 (duplicate code in driver later)
**Research flag:** STANDARD -- well-documented CMake INTERFACE pattern; micmap_bindings precedent already in-tree.

### Phase 2: Driver-Side Audio Capture Spike
**Rationale:** Highest-risk unknown -- does WASAPI work inside vrserver DLL host? Must surface before detection or IPC work begins. Behind enableDriverAudio flag (default OFF). If WASAPI fails in DLL context, escalate before Phase 3.
**Delivers:** WASAPI capture inside driver_micmap.dll; first 1 second of RMS in vrserver.txt; COM apartment handling verified on real hardware.
**Features:** MIG-01 (partial -- capture only)
**Avoids:** Pitfall 1 (COM apartment), Pitfall 2 (mic privacy gate), Pitfall 4 (WASAPI lifecycle)
**Research flag:** NEEDS VALIDATION on real Bigscreen Beyond + Win11. Exit criterion requires successful audio capture logged in vrserver.txt.

### Phase 3: Driver-Side Detection Thread
**Rationale:** Depends on Phase 2 (WASAPI validated). Introduces DetectionRunner, SampleRing, SettingsSnapshot. Behind enableDriverDetection flag (default OFF). Client detection still active. Two trigger paths log for correlation.
**Delivers:** Full in-process trigger pipeline; driver covers mic->dashboard toggle without HTTP hop; end-to-end test with flag ON.
**Features:** MIG-02, MIG-03, MIG-04 (partially -- trigger goes in-process, POST /button not yet deleted)
**Avoids:** Pitfall 3 (detection thread calls OpenVR), Pitfall 12 (RunFrame budget), Pitfall 13 (IMMNotificationClient)
**Research flag:** STANDARD for threading pattern; NEEDS VALIDATION for real-hardware trigger latency and HMD sleep/wake cycle.

### Phase 4: IPC Contract Reshape
**Rationale:** Depends on Phase 1. Can run parallel with Phase 3 client-side work. New IPC surface is prerequisite for Phase 5 training migration. Trigger HTTP path persists until Phase 6.
**Delivers:** GET /state, GET /telemetry/level, GET /devices, GET /settings, PUT /settings; driver as sole config.json writer; client settings form uses PUT; IDriverClient renamed to IDriverApi.
**Features:** IPC-S-01..06, LIB-03, HEALTH-01..06
**Avoids:** Pitfall 5 (config race), Pitfall 7 (localhost binding regression, dead trigger residue)
**Research flag:** STANDARD -- extends existing cpp-httplib pattern; same CommandQueue discipline.

### Phase 5: Training Migration
**Rationale:** Depends on Phase 4 (IPC surface with /state and /settings exists) and Phase 3 (driver owns audio). Driver becomes sole training coordinator; client becomes observer. training_data.bin ownership transfers to driver.
**Delivers:** POST /training/start, GET /training/progress, POST /training/finalize, POST /training/cancel; driver writes training_data.bin; client training UI polls driver; threshold preview/confirm/discard.
**Features:** TRAIN-01..05, optional TRAIN-D1, TEST-D1 (replay mode)
**Avoids:** TRAIN-AF-01 (client takes mic back), Pitfall 5 (training_data.bin write race)
**Research flag:** MEDIUM -- commit/discard flow designed from first principles, no v1.5 prior art. Validate UX with real training session on hardware.

### Phase 6: Cutover and Cleanup
**Rationale:** All paths proven in prior phases. Flip enableDriverDetection default ON, delete dead code. Risk is low if prior phases maintained clean feature flags. Largest code deletion in the milestone.
**Delivers:** POST /button deleted; IDriverClient::tap() deleted; client WASAPI callback FFT/state-machine body deleted; ~500 LoC reduction in main.cpp; driver DLL is sole end-to-end runtime; HEALTH-07; FAIL cluster.
**Features:** MIG-04 complete, HEALTH-07, FAIL-01..05, TEST-02 (--debug-trigger CLI), HEALTH-D4 (tray icon)
**Avoids:** Pitfall 10 (double-trigger eliminated), Pitfall 7 (dead code swept), Pitfall 11 (v1.5 priors re-verified)
**Research flag:** STANDARD -- deletion phase; v1.5 SVR-04 rip-out discipline is the template.

### Phase 7: Documentation
**Rationale:** v1.5 Phase 5 carryover. Docs describe shipped reality; must follow Phase 6 cutover.
**Delivers:** DOC-01 (README sync), DOC-02 (docs/architecture.md reflecting driver-resident pipeline, shared lib, settings IPC, training flow).
**Features:** DOC-01, DOC-02
**Research flag:** STANDARD -- writing docs, no technical unknowns.

### Phase Ordering Rationale

- **Phase 1 must be first.** Without shared lib, every other phase duplicates code or breaks the mic_test.exe headless invariant.
- **Phase 2 before Phase 3.** Audio feasibility validation must precede detection work -- if WASAPI fails inside vrserver, Phase 3 is blocked.
- **Phase 4 client work can run parallel with Phase 3.** Driver detection thread and IPC client plumbing are mostly independent. Phase 4 driver-side needs Phase 3 capture; Phase 4 client-side only needs Phase 1 shared lib.
- **Phase 4 before Phase 5.** Training endpoints depend on /state and /settings IPC surface.
- **Phase 5 before Phase 6.** Cutover deletes client training path; without Phase 5, users lose the ability to retrain.
- **Phase 7 last.** Docs describe post-cutover reality.

### Research Flags

Phases needing deeper research or validation during planning:
- **Phase 2:** WASAPI inside vrserver DLL host -- validated once in sister project bey-closer-t1 but not yet in this driver. Real-hardware spike is mandatory before Phase 3 begins.
- **Phase 5:** Training UX commit/discard pattern -- no v1.5 prior art; validate with real training session on hardware.

Phases with standard patterns (minimal research needed):
- **Phase 1:** CMake INTERFACE target -- documented, proven in-repo (micmap_bindings precedent).
- **Phase 4:** cpp-httplib endpoint extension -- same pattern as v1.5 Phase 1 HTTP server setup.
- **Phase 6:** Deletion/cutover -- v1.5 SVR-04 rip-out is the template.
- **Phase 7:** Documentation only.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | No new deps. Every pick is in-tree or a structural addition. Only MEDIUM item is cpp-httplib version bump -- deferred. |
| Features | HIGH (MIG/LIB/IPC/FAIL/TEST), MEDIUM (TRAIN/HEALTH) | Lifecycle and IPC contract grounded in v1.5 prior art and official OpenVR docs. Training commit/discard UX and health poll cadence are opinionated with no direct measurement. |
| Architecture | HIGH | Every named symbol/path verified against hmd-button branch on 2026-04-30. Thread model grounded in v1.5 CommandQueue discipline. |
| Pitfalls | HIGH | Anchored in v1.5 shipped pitfalls (paid in commits/UAT), bey-closer-t1 experiments, and actual driver source. |

**Overall confidence:** HIGH

### Gaps to Address

1. **WASAPI in DLL host (Phase 2 exit criterion)** -- not validated in this driver yet. Real-hardware spike is mandatory. If it fails, migration architecture needs reassessment.
2. **Training UX** -- commit/discard pattern designed from first principles. Validate in Phase 5 UAT; adjust if users resist explicit Confirm step.
3. **Health poll cadence** -- 5 Hz visible, 0.5 Hz minimized is a guess, not measured. Bump to 10 Hz if users report laggy level meter.
4. **hmd_button_test.exe retirement** -- with TEST-02 (--debug-trigger) in main client, may be redundant. Explicit decision needed before Phase 6.
5. **Backwards compatibility on mismatched install** -- v1.6 client does not speak POST /button; v1.5 driver does not have new endpoints. Installer must enforce co-versioning OR client gracefully degrades. Needs explicit requirement.
6. **cpp-httplib v0.20.1 bump** -- covers CVE-2025-46728. Deferred to Phase 1 plan; if wire-format regressions emerge, defer to a standalone deps milestone.

## Sources

### Primary (HIGH confidence -- in-tree, verified 2026-04-30)
- .planning/codebase/ARCHITECTURE.md -- current two-process layout, library boundaries
- .planning/codebase/CONCERNS.md -- v1.5 tech debt (audio callback FFT inline, state-machine thread safety, IMMNotificationClient ref-counting)
- .planning/codebase/INTEGRATIONS.md -- IPC contract today, port range, cpp-httplib version
- .planning/PROJECT.md -- v1.6 milestone scope, in/out-of-scope decisions
- .planning/MILESTONES.md -- v1.5 shipping record, Phase 5 docs deferred
- driver/src/device_provider.{hpp,cpp} -- lifecycle hooks, CommandQueue ownership
- driver/src/http_server.{hpp,cpp} -- current IPC surface
- src/audio/src/audio_capture.cpp:196,521 -- CoInitializeEx on capture thread (already correct)
- apps/micmap/main.cpp:353-444 -- audio callback FFT body (to be deleted at Phase 6)
- apps/mic_test/CMakeLists.txt -- headless invariant

### Primary (HIGH confidence -- official docs)
- https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md -- RunFrame threading contract, Init/Cleanup lifecycle
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi -- WASAPI capture, COM apartment rules
- https://cmake.org/cmake/help/latest/command/add_library.html -- INTERFACE/STATIC target semantics

### Secondary (MEDIUM confidence)
- D:/Documents/Projects/bey-closer-t1/HMD Button Stub.md -- WASAPI + audio thread coexistence validated once in sister project under SteamVR
- https://github.com/yhirose/cpp-httplib/releases -- v0.20.1 CVE fix confirmation
- https://github.com/gabime/spdlog/wiki/How-to-use-spdlog-in-DLLs -- spdlog DLL registration dance (justifies rejection)

---
*Research completed: 2026-04-30*
*Ready for roadmap: yes*

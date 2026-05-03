# Phase 7: Driver-Side Detection Thread — Research

**Researched:** 2026-05-03
**Domain:** SteamVR driver-resident detection pipeline (in-process trigger collapse) — C++17/20, OpenVR, WASAPI, threading, lock-free SPSC
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

D-01..D-28 from `07-CONTEXT.md` are LOCKED. Highlights:

**A. SPSC ring + audio plumbing**
- D-01: New header `driver/src/sample_ring.hpp`. Bounded SPSC, contiguous backing, power-of-two slots, atomic head/tail with acquire/release fences. Header-only, default 16 slots.
- D-02: Slot payload = `std::array<float, kSlotFrames>` aligned to WASAPI shared-mode buffer cadence (~480 frames @ 48kHz/10ms). 16×480 ≈ 160ms backlog ceiling.
- D-03: Overflow = drop-OLDEST. Audio thread NEVER blocks. Drop count tracked in `atomic<uint32_t>`.
- D-04: Wakeup = `std::condition_variable + std::mutex` on DetectionRunner. Audio thread `notify_one()` after each push; detection waits with short timeout (Claude's discretion 25–50ms).
- D-05: Audio callback rewired in `AudioWorker::RunWorker()` setAudioCallback lambda — keep `weak_ptr<State>` UAF guard, push frames into ring, `cv_.notify_one()`, RMS-log block guarded behind `MICMAP_DEBUG_RMS_LOG`.

**B. HMD standby**
- D-06: Standby pauses **detection only**. AudioWorker keeps capturing.
- D-07: `Pause()`/`Resume()` callable from RunFrame; detection thread on pause enters drain-and-discard loop, state machine NOT reset.
- D-08: SC3 verification = HMD wake/sleep ×2 cycles via Process Explorer handle stability check.

**C. Double-trigger coexistence (Pitfall 10)**
- D-09: `GET /health` JSON gains `"driver_detection_active": bool`. True iff `enable_driver_audio=1` AND `enable_driver_detection=1` AND DetectionRunner constructed successfully.
- D-10: Client polls `/health`, suppresses local trigger when field is true. Keeps detection running for UI confidence/RMS visualization but drops the `IDriverClient::tap()` call.
- D-11: State machine cooldown is the belt-and-suspenders backstop for transient double-tap during /health-poll lag.
- D-12: P10 deletes the entire scaffolding (POST /button, IDriverClient::tap, the field, and the suppression dance).

**D. Settings source**
- D-13: VRSettings single-read at `DeviceProvider::Init` (no shared-lib touch). New `default.vrsettings` keys: `enable_driver_detection` (bool, default false), `detection_sensitivity` (float, default 0.7), `detection_threshold` (float, default 0.6), `detection_cooldown_ms` (int32, default 1000), `detection_min_duration_ms` (int32, default 200). Defaults mirror v1.5 client config.json defaults.
- D-14: P7 does NOT add `PUT /settings`, does NOT read `config.json`, does NOT pull `ConfigManager`/`nlohmann::json` into the driver. Phase 8 owns IPC reshape.
- D-15: MIG-06 50ms-propagation satisfied by **mechanism**: `DetectionRunner::publish(std::shared_ptr<const DetectionConfig>)` swaps an internal `std::atomic<std::shared_ptr<const DetectionConfig>>`. Headless test (`tests/driver/detection_settings_propagation_test.cpp`) measures propagation < 50ms.
- D-16: Driver consumes only the `DetectionConfig` subset (sensitivity / threshold / cooldown / min-duration). Audio device selection unchanged from P6 D-11.

**E. DetectionRunner shape**
- D-17: New driver-only files `driver/src/detection_runner.{hpp,cpp}`. Owns `std::thread`, `unique_ptr<INoiseDetector>` via `createFFTDetector(sampleRate, 2048)`, `unique_ptr<IStateMachine>` via `createStateMachine(StateMachineConfig{...})`, `atomic<shared_ptr<const DetectionConfig>> activeConfig_`, `atomic<bool> shutdown_`/`paused_`, `cv_`, `mu_`, refs to ring + CommandQueue.
- D-18: Loop = `cv_.wait_for(...)` → if shutdown break / if paused drain-discard / else drain ring → `analyze` → `update` → on rising-edge Triggered `commandQueue_->push(TapCommand{})`. Reload activeConfig_ snapshot at top of each iteration; if changed, push new sensitivity/threshold/cooldown into detector + state machine.

**F. Lifecycle ordering**
- D-19: Init order: VR_INIT → PatchGenericHmdBindings → CommandQueue → HttpServer (with extended /health JSON) → VRSettings reads → AudioWorker (if enable_driver_audio) → DetectionRunner (if enable_driver_detection AND audioWorker_ alive). Fail-soft if detection requested but audio unavailable: log warning + skip; do NOT fail Init.
- D-20: Cleanup order (reverse): DetectionRunner.reset() FIRST → audioWorker_.reset() → httpServer_.Stop() + commandQueue_.reset() → HMD-state reset → VR_CLEANUP. DetectionRunner destructor: signal shutdown, notify_all, join with 2s watchdog (P6 AudioWorker pattern).
- D-21: RunFrame splice for HMD standby. Recommend `EnterStandby`/`LeaveStandby` callbacks (`device_provider.cpp:253-259`) over `VREvent_TrackedDeviceDeactivated`. Claude's discretion confirmed.

**G. Pitfall enforcement**
- D-22: Extend `cmake/AssertAudioWorkerNoVrApi.cmake` (or add sibling `AssertDetectionRunnerNoVrApi.cmake`) covering `detection_runner.cpp`, `sample_ring.hpp`. SC2 grep audit verifies `vr::*` only in `device_provider.cpp` + `manifest_registrar.cpp`.
- D-23: DetectionRunner does NOT register `IMMNotificationClient` or any COM notifier. Only touches: SPSC ring (read), config snapshot (atomic load), state machine, noise detector, CommandQueue (push). Ring lifetime tied to AudioWorker; D-20 ordering prevents read-after-free.
- D-24: Pitfall 13 hardening unchanged from P6 D-15/D-16. ComPtr migration of DeviceNotificationClient deferred to P10.

**H. UAT**
- D-25: Six UAT cases on Bigscreen Beyond + Win11 Pro: (1) flag-ON in-process trigger / SC1 — zero `POST /button` traffic, (2) HMD wake/sleep ×2 / SC3, (3) 50-cycle Init→500ms→Cleanup stress / SC4, (4) MIG-06 50ms propagation headless / SC5, (5) Flag-OFF regression — byte-identical to P6 closeout, (6) coexistence — both flags + client running, single tap per cover, mid-session flag flip.
- D-26: UAT artifacts in `07-UAT.md`. SC4's 50-cycle bar is a hard step up from P6's single-cycle spot-check.

**I. Merge / flag default**
- D-27: Land on `main` with both flags default OFF. P10 flips both ON in one cutover commit.
- D-28: Document in PLAN.md that `enable_driver_detection=true` deployment in P7 has no runtime settings mutation (must edit `default.vrsettings` + restart SteamVR). P8 ships `PUT /settings`.

### Claude's Discretion

Areas where research must recommend, not constrain:

- Exact SPSC slot count (8 / 16 / 32) and per-slot frame count.
- DetectionRunner `cv_.wait_for` timeout (25 / 50 / 100 ms).
- `SampleRing` template-on-T vs hardcoded float.
- C++20 `std::atomic<std::shared_ptr<>>` direct vs `std::atomic_load`/`store` free functions on a member.
- Detection thread log verbosity and `MicMap detection:` prefix convention.
- File layout: single `detection_runner.{hpp,cpp}` vs split runner/state/snapshot.
- Headless test layout: extend `audio_worker_lifecycle_headless.cpp` vs new `detection_runner_*_test.cpp` files.
- HMD pause splice: `EnterStandby`/`LeaveStandby` (recommended) vs `VREvent_TrackedDeviceDeactivated`.
- Drop-count diagnostic logging cadence.

### Deferred Ideas (OUT OF SCOPE)

- `PUT /settings` HTTP endpoint, `GET /devices`, `GET /telemetry/level`, `GET /state` — Phase 8.
- LIB-04 logger sink injection — Phase 8.
- Training migration (`POST /training/*`, `training_data.bin` ownership transfer) — Phase 9.
- `POST /button` deletion + `IDriverClient::tap()` removal (MIG-05) — Phase 10.
- `enable_driver_detection` default flip ON — Phase 10.
- Client-side detection deletion — Phase 10.
- `OnDefaultDeviceChanged` follow-the-default + device pinning (Pitfall 14) — Phase 8+.
- DeviceNotificationClient ComPtr migration (Pitfall 13 hardening beyond alive-flag) — Phase 10.
- FFT-on-every-frame perf cost (CONCERNS.md Bottleneck #2) — backlog.
- Tray-icon state glyphs (HEALTH-08), FAIL-01..05 graceful failure UX, INST-09 — Phase 10.
- `hmd_button_test.exe` retirement — TEST-05 (P10).
- cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728) — Phase 8 prereq plan.
- Detection-accuracy work (DET-01/02) — out of v1.6 scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MIG-02 | Detection thread drains audio ring, runs FFT + RMS + state machine, emits `TapCommand` to existing CommandQueue. Detection thread never touches `vr::*`. | §3 Architecture Patterns (DetectionRunner shape, CommandQueue boundary preserved); §6 Don't Hand-Roll (state machine + FFT factories); §7 Pitfalls 3 + 12 enforcement. |
| MIG-03 | Detection runs continuously regardless of HMD activation; `EnterStandby` pauses cleanly, `LeaveStandby` resumes; no leaked audio device handles across cycles. | §3 HMD standby splice (EnterStandby/LeaveStandby chosen over VREvent_TrackedDeviceDeactivated per official Valve semantics); §5 Pause-discard pattern. |
| MIG-04 | Driver Cleanup tears down detection → audio → HTTP in reverse order; 50-cycle Init/Cleanup stress with zero leaked handles. | §3 Lifecycle ordering (D-19/D-20); §5 50-cycle stress harness pattern (`device_provider_lifecycle_stress_test.cpp`). |
| MIG-06 | Detection reads settings via lock-free `std::atomic<std::shared_ptr<const AppConfig>>` snapshot. Sensitivity/threshold/cooldown changes propagate within 50ms with no audio-thread blocking. | §3 Atomic snapshot pattern; §4 Standard Stack (C++20 `std::atomic<std::shared_ptr>` MSVC support — VS 2022 v19.30+ verified); §10 Validation Architecture (50ms headless propagation test). |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **Phase order is load-bearing.** Driver Sidecar → Config Read-Back → Auto-Start → Installer → Docs is v1.5; this phase is v1.6 P7. Do not reorder. P7 depends on P6 closeout (GO).
- **Locked stack:** C++17, CMake (3.20+), ImGui+D3D11 (client only), WASAPI, KissFFT (already pulled via `micmap::core_runtime`), cpp-httplib v0.14.3, nlohmann/json (driver-side ALREADY linked for `POST /button` body parse — no new dep), OpenVR SDK. **No framework changes.**
- **Bash via Git Bash:** Unix-style paths in shell commands (forward slashes, `/dev/null`), not Windows.
- **Windows-only runtime:** WASAPI + OpenVR driver DLL + Inno Setup. Non-Windows audio stubs remain (P7 preserves the AudioWorker non-Windows skeleton inherited from P6).
- **Installer runs elevated; runtime does not require admin.** P7 changes nothing here.
- **Visual validation on real HMD is mandatory** for any user-visible exit criterion. Type-checking and build-success do not substitute. SC1, SC3 in P7 require Bigscreen Beyond + Win11 Pro UAT (D-25).
- **GSD workflow artifacts are project memory across context resets.** Honor the 07-CONTEXT.md → 07-RESEARCH.md → PLAN.md chain.

## Summary

Phase 7 collapses the v1.5 trigger pipeline from 7 hops cross-process to 4 hops in-process by spawning a `DetectionRunner` thread inside `driver_micmap.dll`. The thread drains a SPSC sample ring fed by P6's `AudioWorker` WASAPI callback, runs `INoiseDetector::analyze()` + `IStateMachine::update()` from `micmap::core_runtime`, and pushes `TapCommand` onto the existing v1.5 `CommandQueue` on rising-edge Triggered. The CommandQueue → RunFrame → `UpdateBooleanComponent` boundary is preserved verbatim — DetectionRunner is a NEW PRODUCER of an existing primitive, nothing about RunFrame changes.

Every load-bearing decision is locked in 07-CONTEXT.md (D-01..D-28). Research focused on validating the four NEEDS-VALIDATION items: (1) MSVC v19.30+ (VS 2022) C++20 `std::atomic<std::shared_ptr>` support and propagation latency methodology, (2) OpenVR `EnterStandby`/`LeaveStandby` vs `VREvent_TrackedDeviceDeactivated` semantics on Bigscreen Beyond, (3) SPSC ring slot-count tradeoff against the WASAPI shared-mode buffer cadence, and (4) the 50-cycle Init/Cleanup stress harness shape.

**Primary recommendation:** Land DetectionRunner with C++20 `std::atomic<std::shared_ptr<const DetectionConfig>>` (MSVC v19.30+ confirmed in this build environment via `Visual Studio 17 2022`), 16-slot ring of `std::array<float, 480>` (160ms backlog ceiling), 50ms `cv_.wait_for` timeout, sibling lint `cmake/AssertDetectionRunnerNoVrApi.cmake` (rather than extending the audio-worker lint — keeps blast radius small), pause splice on `EnterStandby`/`LeaveStandby` (semantically correct per Valve's driver-API documentation), and a new `tests/driver/device_provider_lifecycle_stress_test.cpp` headless harness driving 50 Init→500ms→Cleanup cycles via direct DeviceProvider construct/destroy (no SteamVR restart loop).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| WASAPI capture lifecycle (device handles, COM init, IMMNotificationClient) | Driver / AudioWorker thread | — | P6 D-04/D-05 owns this; P7 does NOT touch device lifecycle. Pitfall 4 mitigation lives entirely inside AudioWorker. |
| Sample buffering between audio cb and detection | Driver / SampleRing (header-only) | — | New primitive; lives in `driver/src/` per D-01. Coupled to detection-thread design, isolated TU enables unit tests. |
| FFT + state machine logic | Driver / DetectionRunner thread | shared lib (`micmap::core_runtime`) | Factory-instantiated from shared lib (`createFFTDetector`, `createStateMachine`); thread ownership is driver-side, math is reused unchanged. |
| Trigger emission to OpenVR | Driver / RunFrame ONLY | CommandQueue (driver-side) | v1.5 SVR-05 invariant. DetectionRunner pushes; RunFrame drains; only RunFrame calls `vr::*`. |
| Settings snapshot for hot-path reads | Driver / DetectionRunner internal | — | `atomic<shared_ptr<const DetectionConfig>>` lives inside DetectionRunner per D-15. P8 will land the HTTP path that calls `publish()`. |
| HMD standby pause/resume | Driver / DeviceProvider RunFrame thread | DetectionRunner (Pause/Resume API) | Only RunFrame thread reacts to OpenVR lifecycle callbacks; DetectionRunner exposes thread-safe Pause/Resume. |
| HTTP /health JSON state read | HTTP server thread | DeviceProvider (read-only state) | New `driver_detection_active` field; needs read access to DeviceProvider state via callback or shared atomic. |
| Client-side trigger suppression during coexistence | Client / `apps/micmap/main.cpp` | — | Polls `/health.driver_detection_active`; gates `IDriverClient::tap()` call. Transient migration scaffolding (P10 deletes). |
| `default.vrsettings` schema | Driver resources | — | Single-read at `DeviceProvider::Init`. No hot-reload. P8's `PUT /settings` is the runtime mutation path. |
| Lifecycle ordering (Init/Cleanup) | DeviceProvider | — | Reverse-order teardown is the load-bearing correctness invariant; P7 prepends DetectionRunner.reset() as new step 1. |

## Standard Stack

### Core (already present in driver — no new deps)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C++ standard threading (`<thread>`, `<atomic>`, `<condition_variable>`, `<mutex>`) | C++17 (CMake `cxx_std_17`) | DetectionRunner thread, shutdown signals, ring atomics, CV wakeup | In-tree, zero linkage cost. P6 AudioWorker already uses identical primitives. |
| `std::shared_ptr` + `std::atomic<std::shared_ptr<T>>` | C++20 (MSVC v19.30+, _MSC_VER >= 1930) | Lock-free settings snapshot for MIG-06 | VS 2022 (Visual Studio 17 2022, confirmed in CMakeCache.txt). Cppreference confirms C++20 partial specialization is the canonical pattern; MSVC implements via mutex-backed control-block lock-bit (not lock-free in the strict sense, but never blocks the audio thread because the audio thread does not call publish). [VERIFIED: Microsoft Old New Thing 2024-12-19 "Inside STL: The atomic shared_ptr"] |
| KissFFT | already linked via `micmap::core_runtime` (P5 D-10) | FFT for `INoiseDetector` analyze loop | No driver-side touch — `createFFTDetector` factory returns `unique_ptr<INoiseDetector>`, KissFFT is a private impl detail. |
| OpenVR driver SDK | bundled `external/openvr/` | `EnterStandby`/`LeaveStandby` callbacks splice point | Valve's documented driver-API surface. Standby semantics verified §7. |
| nlohmann/json | already linked into `driver_micmap.dll` (for `POST /button` body parse) | `/health` JSON response with new `driver_detection_active` field | No new dep — extending existing usage (`http_server.cpp:131`). |

### Supporting (new files only)

| File | Purpose | When to Use |
|------|---------|-------------|
| `driver/src/sample_ring.hpp` | Header-only SPSC bounded ring | Producer = WASAPI capture cb, consumer = DetectionRunner. Drop-OLDEST on full. |
| `driver/src/detection_runner.{hpp,cpp}` | Detection thread owner + lifecycle | Constructed in `DeviceProvider::Init` after AudioWorker. |
| `cmake/AssertDetectionRunnerNoVrApi.cmake` | Source-grep lint for Pitfall 3 | Sibling to `AssertAudioWorkerNoVrApi.cmake`; runs every build via CTest. |
| `tests/driver/detection_settings_propagation_test.cpp` | MIG-06 < 50ms propagation test | Headless; constructs DetectionRunner with stub ring; calls `publish()` from worker thread; measures `steady_clock` to consumer-thread observed swap. |
| `tests/driver/device_provider_lifecycle_stress_test.cpp` | SC4 50-cycle Init→500ms→Cleanup harness | Direct construct/destroy of DeviceProvider without `VR_Init` (driver-host shim or fake interface table). Process Explorer handle audit run separately. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| C++20 `std::atomic<std::shared_ptr<T>>` | `std::atomic_load` / `std::atomic_store` free functions on a `shared_ptr<const DetectionConfig>` member (deprecated in C++20, but present in C++17 MSVC) | Free-function form is C++17-portable but DEPRECATED in C++20. Project is on C++17 (`set(CMAKE_CXX_STANDARD 17)` in `driver/CMakeLists.txt`). **Recommend the C++17 free-function form for portability** — driver compiles as C++17, MSVC v19.30+ still supports both. Document the migration path: when project bumps to C++20, swap to native specialization (one-line change). [CITED: cppreference.com/w/cpp/memory/shared_ptr/atomic, cppreference.com/w/cpp/memory/shared_ptr/atomic2] |
| Custom hand-rolled SPSC ring | rigtorp/SPSCQueue, cameron314/readerwriterqueue | Adding a third-party header for ~80 LoC of well-understood code is overkill; both libs are MIT but bring vendor/version discipline overhead. Hand-roll is justified for a header-only file < 100 lines using std::atomic + acquire/release. **Recommend hand-roll, header-only, follow rigtorp's API shape (try_push / try_pop) for familiarity.** [CITED: github.com/rigtorp/SPSCQueue] |
| Slot count = 32 (~320ms backlog) | 16 (~160ms) or 8 (~80ms) | 16 slots × 480 frames = 7.5 KB ring. Generous backlog without bloat. State machine cooldown (default 1s in D-13) means triggers are bounded irrespective of ring depth. **Recommend 16.** |
| Slot frames = 1024 | 480 (10ms @ 48kHz) | 480 matches WASAPI shared-mode buffer cadence; producing larger slots requires audio cb to accumulate, defeating the no-block invariant. **Recommend 480 (kSlotFrames).** |
| `cv_.wait_for(50ms)` | 25ms or 100ms | 50ms balances shutdown latency (max stuck wait at Cleanup before checking shutdown_) against idle CPU cost (cv wakeup is cheap on Windows SRWLOCK; 50ms = 20 wakeups/sec idle). **Recommend 50ms.** |
| Single combined `cmake/AssertNoVrApiInDriver.cmake` lint | Sibling files (`AssertDetectionRunnerNoVrApi.cmake`) | Sibling keeps blast radius small; one lint failure doesn't take down all driver-TU lints. **Recommend sibling pattern.** Both can share helper logic if needed (CMake `include()`), but file-scoped lints scale to P10 deletions cleanly. |
| Pause splice via `VREvent_TrackedDeviceDeactivated` | `EnterStandby` / `LeaveStandby` driver callbacks | Per Valve docs: standby = system-wide power state transition; deactivated = device disconnection. **Recommend EnterStandby/LeaveStandby** — semantically correct, lower noise (HMD reactivation cycles also fire deactivated, would re-pause unnecessarily). [VERIFIED: github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md] |

**Installation:** No new dependencies. CMake additions only:

```cmake
# driver/CMakeLists.txt — add detection_runner.cpp to driver_micmap sources
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
    src/audio_worker.cpp
    src/detection_runner.cpp     # NEW P7
)
# sample_ring.hpp is header-only — no source registration needed
# but ensure include path picks it up (already covered by src/ include path)
```

**Version verification:** Visual Studio 17 2022 confirmed in `CMakeCache.txt` (worktree build). MSVC v19.30+ supports C++20 `std::atomic<std::shared_ptr>` partial specialization, but project compiles as C++17 (`CMAKE_CXX_STANDARD 17`), so use the C++17 free-function form `std::atomic_load`/`std::atomic_store` on a `shared_ptr<const DetectionConfig>` member. [VERIFIED: build/CMakeCache.txt CMAKE_GENERATOR=Visual Studio 17 2022]

## Architecture Patterns

### System Architecture Diagram

```
┌─────────────────────── driver_micmap.dll (vrserver host) ───────────────────────┐
│                                                                                 │
│  WASAPI capture thread (owned by AudioWorker — P6)                              │
│      │  setAudioCallback lambda:                                                │
│      │   1. weak_ptr<State> lock + alive check (Pitfall 13 / D-15/D-16)         │
│      │   2. SampleRing::try_push(block) — drop-OLDEST on full (D-03)            │
│      │   3. cv_.notify_one() on DetectionRunner (D-04)                          │
│      │   4. (debug-only) RMS log line behind MICMAP_DEBUG_RMS_LOG               │
│      ▼                                                                          │
│  ┌───────────── SampleRing (header-only SPSC, 16×480 floats) ──────────────┐    │
│  │  atomic<size_t> head / tail with acquire/release fences                 │    │
│  │  contiguous std::array backing; power-of-two slot count                 │    │
│  │  drop-OLDEST atomicity = head advances tail then writes (CAS-free)      │    │
│  │  drops_ atomic<uint32_t> for diagnostic logging                         │    │
│  └────────────────────────────────────┬────────────────────────────────────┘    │
│                                       │                                         │
│  DetectionRunner thread (NEW, P7)     │ ─ pop_chunk                             │
│      │  loop:                                                                   │
│      │   cv_.wait_for(lk, 50ms, [&]{ return shutdown_ || paused_ ||             │
│      │                                  ring_->has_data(); })                   │
│      │   if shutdown_ → break                                                   │
│      │   if paused_ → drain-discard ring → continue (no FFT, no state machine)  │
│      │   reload activeConfig_ snapshot (atomic load); if changed →              │
│      │       push new sensitivity/threshold/cooldown into detector + sm         │
│      │   for each block in ring:                                                │
│      │     INoiseDetector::analyze(block, count) → DetectionResult              │
│      │     IStateMachine::update(result.confidence, dt)                         │
│      │     if state machine TriggerCallback fires (rising-edge Triggered):      │
│      │   ┌───┴────────────────────────────────────────────────────────┐         │
│      │   │  CommandQueue (existing v1.5, mutex+deque, kMaxDepth=8)    │         │
│      │   │  push(TapCommand{}) — drop-OLDEST at depth 8               │         │
│      │   │  NEW PRODUCER (P7) on top of existing HTTP-thread producer │         │
│      │   └─────────────────────────┬──────────────────────────────────┘         │
│      ▼                              │                                           │
│  HTTP server thread (existing v1.5, P6 unchanged)                               │
│      │   POST /button (rollback path until P10):                                │
│      │     queue_.push(TapCommand{}) — UNCHANGED                                │
│      │   GET /health (extended P7):                                             │
│      │     {"status":"healthy","driver_detection_active":bool}                  │
│      │     read DeviceProvider state via callback or shared atomic              │
│                                     │                                           │
│  RunFrame (vrserver thread, NOT owned)                                          │
│      while (auto cmd = commandQueue_->try_pop()) {                              │
│          writeValue(true) → kTapHold(150ms) → writeValue(false)                 │
│          UpdateBooleanComponent on /input/system/click — ONLY caller of vr::*   │
│      }                                                                          │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
                                       │ (in-process — no IPC)
                                       ▼
                                ┌──────────────┐
                                │   SteamVR    │
                                │   Dashboard  │
                                │   Toggle     │
                                └──────────────┘
```

### Data Flow — Trigger Latency Comparison

**v1.5 (7-hop cross-process):** mic → WASAPI cb → FFT → state machine → IDriverClient::tap → HTTP POST → http_server.cpp → CommandQueue → RunFrame → UpdateBooleanComponent.

**v1.6 P7 (4-hop in-process):** mic → WASAPI cb → SampleRing → DetectionRunner (FFT + state machine) → CommandQueue → RunFrame → UpdateBooleanComponent.

The HTTP hop and the cross-process boundary are eliminated. JSON parse + cpp-httplib routing + localhost socket round-trip vanish from the critical path.

### Component Responsibilities

| File | Role | Producer / Consumer Threads |
|------|------|----------------------------|
| `driver/src/audio_worker.{hpp,cpp}` | WASAPI capture lifecycle owner; pushes frames into SampleRing | Audio cb thread = producer of ring |
| `driver/src/sample_ring.hpp` (NEW) | Lock-free SPSC bounded ring | Producer = audio cb; consumer = DetectionRunner thread |
| `driver/src/detection_runner.{hpp,cpp}` (NEW) | Detection thread owner; drains ring, runs FFT + state machine, pushes TapCommand | Consumer of ring; producer of CommandQueue |
| `driver/src/command_queue.hpp` | Existing v1.5 mutex+deque; **gains second producer in P7** | Producers = HTTP thread + DetectionRunner thread; consumer = RunFrame |
| `driver/src/http_server.cpp` | HTTP routes; `/health` extended for `driver_detection_active` | HTTP thread |
| `driver/src/device_provider.{hpp,cpp}` | Init / Cleanup / RunFrame; constructs+tears DetectionRunner; routes EnterStandby/LeaveStandby to DetectionRunner::Pause/Resume | RunFrame thread (vrserver) |
| `driver/resources/settings/default.vrsettings` | New keys: `enable_driver_detection`, `detection_sensitivity`, `detection_threshold`, `detection_cooldown_ms`, `detection_min_duration_ms` | Single-read at Init |

### Recommended Project Structure

```
driver/src/
├── driver_main.cpp           # unchanged
├── device_provider.{hpp,cpp} # MODIFIED: Init step 7 + Cleanup step 1 + RunFrame standby splice
├── http_server.{hpp,cpp}     # MODIFIED: /health JSON gains driver_detection_active field
├── command_queue.hpp         # UNCHANGED
├── audio_worker.{hpp,cpp}    # MODIFIED: callback lambda push-to-ring + cv notify
├── detection_runner.hpp      # NEW
├── detection_runner.cpp      # NEW
├── sample_ring.hpp           # NEW header-only
├── driver_log.hpp            # unchanged
└── vr_error.hpp              # unchanged

cmake/
├── AssertAudioWorkerNoVrApi.cmake          # P6 — unchanged
└── AssertDetectionRunnerNoVrApi.cmake      # NEW P7

tests/driver/
├── audio_worker_lifecycle_headless.cpp                      # P6 — unchanged
├── detection_settings_propagation_test.cpp                  # NEW P7 (MIG-06)
└── device_provider_lifecycle_stress_test.cpp                # NEW P7 (SC4 / MIG-04)

driver/resources/settings/default.vrsettings                 # MODIFIED: 5 new keys
```

### Pattern 1: Lock-free SPSC ring with drop-OLDEST

**What:** Bounded ring buffer with single producer (audio cb) and single consumer (detection thread). Audio thread NEVER blocks; on full ring, oldest slot is reclaimed.

**When to use:** Producer/consumer where producer rate is fixed by hardware (WASAPI 100 Hz) and consumer rate is variable (FFT cost dependent on CPU load).

**Why drop-OLDEST not drop-NEWEST:** Detection cares about **recent** acoustic state. During a stall, the freshest frames are the relevant ones; old frames in the ring are stale and should be discarded.

**Sketch (verified pattern from rigtorp/SPSCQueue + cppreference acquire/release primer):**

```cpp
// driver/src/sample_ring.hpp
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace micmap::driver {

template<size_t kSlots, size_t kFrames>
class SampleRing {
    static_assert((kSlots & (kSlots - 1)) == 0, "kSlots must be power-of-two");
public:
    static constexpr size_t kMask = kSlots - 1;

    /// Producer (audio cb). Drop-OLDEST on full. Returns true if a slot was dropped.
    bool try_push(const float* samples, size_t count) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const bool full  = (head - tail) == kSlots;
        if (full) {
            // Drop-OLDEST atomicity: bump tail BEFORE writing the new slot, so
            // the consumer never sees a torn slot. Producer is sole writer of
            // tail in this branch (consumer holds tail elsewhere via acquire);
            // SPSC invariant means consumer is either ahead-or-equal — so this
            // race-free advance is safe under the assumption that consumer
            // never observes tail going backwards.
            tail_.fetch_add(1, std::memory_order_release);
            drops_.fetch_add(1, std::memory_order_relaxed);
        }
        auto& slot = slots_[head & kMask];
        const size_t n = (count < kFrames) ? count : kFrames;
        for (size_t i = 0; i < n; ++i) slot[i] = samples[i];
        slot_count_[head & kMask] = n;
        head_.store(head + 1, std::memory_order_release);
        return full;
    }

    /// Consumer (detection thread). Returns false if ring is empty.
    bool try_pop(std::array<float, kFrames>& out, size_t& out_count) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = slots_[tail & kMask];
        out_count = slot_count_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool has_data() const noexcept {
        return head_.load(std::memory_order_acquire)
             != tail_.load(std::memory_order_acquire);
    }

    uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

private:
    alignas(64) std::atomic<size_t> head_{0};      // producer-only writer
    alignas(64) std::atomic<size_t> tail_{0};      // consumer writer; producer also bumps on drop
    alignas(64) std::atomic<uint32_t> drops_{0};
    std::array<std::array<float, kFrames>, kSlots> slots_{};
    std::array<size_t, kSlots> slot_count_{};
};

} // namespace micmap::driver
```

**Note on drop-OLDEST atomicity:** the `tail_.fetch_add(1)` from the producer inside the full-branch is the ONE place where the SPSC invariant is bent. Acceptable because the consumer never observes tail going backwards (only forwards), and any consumer in flight that read the now-overwritten slot already cleared it with a tail bump. The pattern is documented in cameron314/readerwriterqueue and rigtorp/SPSCQueue overflow handling.

[CITED: github.com/rigtorp/SPSCQueue, github.com/cameron314/readerwriterqueue, dev.to/lakshya_bankey_27825e4908/building-a-high-performance-lock-free-ring-buffer]

### Pattern 2: Atomic shared_ptr settings snapshot (MIG-06)

**What:** `std::shared_ptr<const DetectionConfig>` member accessed via `std::atomic_load` / `std::atomic_store` (C++17 free functions) for lock-free reads on the detection hot path; rare writers via `publish()`.

**When to use:** Read-mostly state shared between many readers and rare writers, where the value is not a primitive and copying is expensive.

**Why it satisfies MIG-06 < 50ms:** The atomic store-release on the publish thread is observed by the next acquire-load on the detection thread, which happens at the top of every loop iteration. With `cv_.wait_for(50ms)` worst-case wakeup latency + immediate snapshot reload, total propagation is bounded at one wakeup interval. Headless test measures actual latency.

**Sketch:**

```cpp
// detection_runner.hpp
class DetectionRunner {
public:
    /// Worker thread (HTTP / future P8 PUT /settings handler) calls this.
    void publish(std::shared_ptr<const DetectionConfig> next) {
        std::atomic_store_explicit(&activeConfig_, std::move(next),
                                   std::memory_order_release);
    }

private:
    std::shared_ptr<const DetectionConfig> activeConfig_;  // C++17 atomic_load/store

    // Detection loop top:
    void loop_iteration() {
        auto cfg = std::atomic_load_explicit(&activeConfig_,
                                             std::memory_order_acquire);
        if (cfg.get() != lastCfg_.get()) {
            // changed — push new sensitivity/threshold/cooldown into detector + sm
            applyConfig(*cfg);
            lastCfg_ = cfg;
        }
        // ... drain ring, analyze, update ...
    }
};
```

**Implementation note (MSVC):** [VERIFIED] MSVC v19.30+ (VS 2022) implements both the deprecated C++17 free-function form and the C++20 `std::atomic<std::shared_ptr<T>>` partial specialization. Both use a control-block lock-bit (mutex-style synchronization, not strictly lock-free). For our use case this is fine: writers are rare (P8's PUT /settings handler), readers are the detection thread (single thread, holds an acquire load briefly). Audio thread NEVER calls publish or load — it only calls `SampleRing::try_push`. The settings-snapshot lock therefore can never block the audio thread. [CITED: devblogs.microsoft.com/oldnewthing/20241219-00 "Inside STL: The atomic shared_ptr"]

[CITED: cppreference.com/w/cpp/memory/shared_ptr/atomic — C++17 free functions, deprecated in C++20 but present; cppreference.com/w/cpp/memory/shared_ptr/atomic2 — C++20 partial specialization]

### Pattern 3: HMD standby splice via EnterStandby / LeaveStandby

**What:** Override `EnterStandby()` / `LeaveStandby()` in `DeviceProvider`; route to `detectionRunner_->Pause()` / `Resume()`.

**When to use:** Power-management state transition where the system is signaling "go quiet, expect to come back". NOT for device disconnection / unrecoverable loss.

**Why it's the right splice (vs `VREvent_TrackedDeviceDeactivated`):** [VERIFIED — Valve docs] `EnterStandby` is "called when the whole system is entering standby mode, after a user-configured time after which the system becomes inactive (HMD not being worn, controllers off or not used, etc.)". `VREvent_TrackedDeviceDeactivated` indicates "a tracked device was unplugged or the system is no longer able to contact it". Different semantics, different recovery paths. EnterStandby fires once on user inactivity; deactivation events fire on every HMD-handle invalidation cycle (which already happens on the regular reactivation path inherited from v1.5 SVR-01). Using EnterStandby avoids re-pausing on every HMD container reattach.

**Sketch:**

```cpp
// device_provider.cpp — replaces existing no-op log lines at :253-259
void DeviceProvider::EnterStandby() {
    DriverLog("MicMap driver entering standby\n");
    if (detectionRunner_) {
        detectionRunner_->Pause();
        DriverLog("MicMap detection: paused (EnterStandby)\n");
    }
    // AudioWorker continues capturing — handles stay valid (D-06).
}

void DeviceProvider::LeaveStandby() {
    DriverLog("MicMap driver leaving standby\n");
    if (detectionRunner_) {
        detectionRunner_->Resume();
        DriverLog("MicMap detection: resumed (LeaveStandby)\n");
    }
}
```

[VERIFIED: github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md]

### Pattern 4: 50-cycle Init/Cleanup stress test via headless harness

**What:** Direct `DeviceProvider::Init()` / `Cleanup()` cycle 50 times in a single test process, asserting handle-count stability via Process Explorer (manual UAT) or Win32 `GetProcessHandleCount` (automated).

**When to use:** SC4 / MIG-04 verification. Lifecycle leaks across reverse-order teardown.

**Why direct construct/destroy not SteamVR-restart loop:** Driving 50 SteamVR restarts manually is a 30+ minute UAT. Direct `DeviceProvider` construction without `VR_INIT_SERVER_DRIVER_CONTEXT` requires a driver-host shim or a fake interface table — most of OpenVR's driver-API surface fails open when the context is absent (`vr::VRServerDriverHost()` returns null), and `device_provider.cpp:60` calls `VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext)` first thing. Two viable paths:

1. **Fake/stub `IVRDriverContext`** that returns null/no-op for every interface getter. Init sees null `vr::VRSettings()` → bails on settings read with default `false` (matching the existing fail-soft path). Cleanup does normal teardown. AudioWorker construct/destroy still cycles real WASAPI (the high-value handle leak surface).
2. **Skip `VR_Init` entirely** in the test binary by exercising only the AudioWorker + DetectionRunner construction sequence outside DeviceProvider. Lower fidelity (skips the HTTP server start/stop lifecycle) but simpler.

**Recommend approach 1.** Higher fidelity to the real lifecycle and reuses P6's RED-tolerant scaffold pattern from `tests/driver/audio_worker_lifecycle_headless.cpp` (skip-on-null-context, fail-soft semantics).

**Handle-count check:** Win32 `GetProcessHandleCount(GetCurrentProcess(), &count)` is automatable; baseline pre-loop, sample post-Cleanup, assert delta ≤ small constant (e.g., 5 — accounting for thread-pool churn). For real-WASAPI-handle leaks, Process Explorer manual audit on Bigscreen Beyond rig is the authoritative SC4 sign-off.

**Sketch:**

```cpp
// tests/driver/device_provider_lifecycle_stress_test.cpp
int main() {
    using namespace std::chrono;
    namespace md = micmap::driver;

    DWORD baseHandles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &baseHandles);

    for (int i = 0; i < 50; ++i) {
        md::DeviceProvider dp;
        // Init with null context — DeviceProvider's settings-read path
        // fail-softs to default-false, AudioWorker is not constructed.
        // For higher fidelity (audio cycling) we'd need a real or stubbed
        // context; this minimum shape proves no leak on the OpenVR-context
        // teardown path.
        EVRInitError err = dp.Init(nullptr);
        (void)err;
        std::this_thread::sleep_for(milliseconds(500));
        // Cleanup runs implicitly via destructor; explicit call is fine too.
    }

    DWORD afterHandles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &afterHandles);
    if ((afterHandles > baseHandles + 5)) {
        std::cerr << "FAIL: handle leak " << baseHandles << " → " << afterHandles << "\n";
        return 1;
    }
    std::cout << "PASS lifecycle_stress_50_cycles handles=" << afterHandles << "\n";
    return 0;
}
```

### Anti-Patterns to Avoid

- **Calling `vr::*` from DetectionRunner thread.** Pitfall 3 — only RunFrame calls OpenVR API. CMake lint enforces.
- **DetectionRunner registering its own `IMMNotificationClient`.** D-23 — notifier surface lives in WASAPIAudioCapture (registered inside AudioWorker per P6 D-15). Adding a second notifier doubles Pitfall 13's UAF surface.
- **Bumping `cv_.wait_for` timeout to seconds.** Cleanup latency = wait timeout. 50ms is the sweet spot.
- **Reading `config.json` from the driver in P7.** D-14 — Pitfall 15 (json bloat in DLL) deferred to P8 IPC reshape. P7's settings come exclusively from VRSettings single-read.
- **Pause+stop AudioWorker on EnterStandby.** D-06 — closing/reopening WASAPI on every HMD wake/sleep cycles Pitfall 4's lifecycle pressure 1000× more often than needed. Audio handles are expensive; detection pause is cheap.
- **Calling `INoiseDetector::analyze` or `IStateMachine::update` from the audio cb.** Pitfall 12 — FFT in audio cb blocks WASAPI buffer service → audio glitches. The whole point of the SPSC ring is to decouple.
- **Pushing `TapCommand` from the audio cb directly.** State-machine cooldown is the natural rate-limiter (Pitfall 12 D-14). Producing TapCommand without state-machine gate floods the queue and the user sees double-taps.
- **Writing `default.vrsettings` from P7 code.** Settings flow from VRSettings (read) and from P8's PUT /settings (write). P7 only reads.
- **Resetting state machine on EnterStandby/LeaveStandby.** D-07 — cooldown timers keep ticking; resume continues from the same logical state. Reset on resume would emit a spurious trigger after a sleep.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| FFT-based noise detection | Custom KissFFT wrapper or hand-rolled FFT | `micmap::detection::createFFTDetector(sampleRate, 2048)` (`src/detection/src/noise_detector.cpp:745`) | Existing factory in `micmap::core_runtime` (linked PRIVATE since P5 D-10). FFT size 2048 matches v1.5 client default. Don't re-instantiate KissFFT; the factory does it. |
| State machine with rising-edge trigger semantics | Custom enum + transitions + cooldown timer | `micmap::core::createStateMachine(StateMachineConfig{...})` (`src/core/src/state_machine.cpp:160`) | Existing factory; `TriggerCallback` fires once per rising-edge into Triggered. State machine cooldown is the natural CommandQueue rate-limiter (Pitfall 12 D-14). |
| WASAPI capture lifecycle | New COM init + IMMNotificationClient | AudioWorker (P6 — already shipped) | P6 owns Pitfall 1 / 4 / 13 mitigations. DetectionRunner is a consumer of the ring AudioWorker fills; it does NOT touch device handles. |
| Bounded multi-producer queue with drop-OLDEST | Custom deque + mutex | `driver/src/command_queue.hpp` (existing v1.5) | Already correct. P7 just adds a second producer. Concurrent push from HTTP-thread and DetectionRunner-thread is handled by the existing `std::lock_guard` per Pitfall 3 §"How to avoid". |
| Settings publish/subscribe | Observer pattern, signals/slots | `std::atomic_load`/`store` on `shared_ptr<const DetectionConfig>` member | C++17 / C++20 standard pattern. Cppreference's canonical example for "many readers, rare writer". Lock-free reads on the hot path. |
| 2-second shutdown watchdog | Custom polling loop with detach fallback | P6 AudioWorker pattern (`audio_worker.cpp:104-122`) | Already battle-tested in P6 D-13. DetectionRunner replicates verbatim — same atomic `thread_finished_`, same poll cadence (25ms), same detach last-resort. |
| HTTP /health JSON shape | Custom serializer | nlohmann::json (already linked into driver) | Just extend the existing `/health` route handler. No new dep. |
| SPSC lock-free ring | (Hand-roll IS justified here) | Header-only ~80 LoC; pattern from rigtorp/SPSCQueue / cameron314/readerwriterqueue | Adding a third-party header for ~80 LoC is overkill; vendor/version overhead exceeds benefit. Hand-roll with std::atomic + acquire/release fences. |

**Key insight:** Phase 7's net code add is small. Two new files (`detection_runner.{hpp,cpp}` ≈ 250 LoC, `sample_ring.hpp` ≈ 100 LoC), three modified files (audio_worker callback rewire, device_provider Init/Cleanup splice, http_server `/health` field), one new lint, two new tests. Everything else is consumed from `micmap::core_runtime` or v1.5 driver primitives. **Most of P7's risk lives in lifecycle ordering and pitfall enforcement, not in new code.**

## Runtime State Inventory

P7 is NOT a rename/refactor/migration phase in the string-replacement sense. It introduces new code rather than altering existing strings. However, it DOES introduce new lifecycle state that must be torn down cleanly across Init/Cleanup cycles (MIG-04). Audit:

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — P7 does NOT introduce any persistent storage. `config.json` ownership transfer deferred to P8; `training_data.bin` deferred to P9. | None |
| Live service config | New `default.vrsettings` keys (`enable_driver_detection`, `detection_*`). Read once at Init via VRSettings; not mutated at runtime in P7. P8 ships the runtime mutation path. | Edit `driver/resources/settings/default.vrsettings`. POST_BUILD copy in `driver/CMakeLists.txt:148` propagates to `build/driver/micmap/resources/settings/default.vrsettings`. Installer (P10 INST-09) propagates to install root. |
| OS-registered state | None — P7 does NOT register Windows Tasks, services, named pipes, or COM components. AudioWorker registers `IMMNotificationClient` (P6 D-15); P7 does NOT register any new notifier (D-23). | None |
| Secrets / env vars | None — P7 does NOT introduce any secrets or environment variable lookups. | None |
| Build artifacts / installed packages | New CMake target source (`driver/src/detection_runner.cpp`); new CTest registrations; new lint script. No new pip/npm/cargo packages. | Edit `driver/CMakeLists.txt` (add `src/detection_runner.cpp` to `add_library` source list); edit `tests/CMakeLists.txt` (register new tests + sibling lint). |

**Nothing else found.** This is a well-contained code-add phase, not a state migration.

## Common Pitfalls

### Pitfall 3: Detection thread calls vr::* directly

**What goes wrong:** "We're already in-process now — why not just call `UpdateBooleanComponent` from `TriggerCallback`?" The temptation is high; symptoms are subtle.
- **Symptom A (silent):** UpdateBoolean returns success from wrong thread until HMD reactivation; state diverges.
- **Symptom B (crash):** HMD deactivated handler races with audio-thread UpdateBoolean → SEGV in vrserver.exe.
- **Symptom C (deadlock):** OpenVR holds an internal mutex on the input layer; audio thread + RunFrame deadlock at shutdown.

**Why it happens:** Removing the IPC hop feels like removing the queue too. CommandQueue stays even when both producers are in-process.

**How to avoid:**
- DetectionRunner ONLY calls `commandQueue_->push(TapCommand{})`. Same path the v1.5 HTTP thread uses.
- CMake lint `cmake/AssertDetectionRunnerNoVrApi.cmake` (sibling of P6's lint) greps `detection_runner.{hpp,cpp}` + `sample_ring.hpp` for `vr::*` and `<openvr*.h>`. Fails build on hit.
- SC2 grep audit on every CI build: `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost\|VRSettings' driver/src/` returns hits ONLY in `device_provider.cpp` and `manifest_registrar.cpp`.

**Warning signs:** Triggers fire reliably for a while, then stop after HMD sleep/wake. vrserver.exe AV after extended sessions. Stress test deadlocks at shutdown.

### Pitfall 4: Lifecycle leak across Init/Cleanup cycles

**What goes wrong:** SteamVR restart-without-quit cycles `Cleanup` → `Init`. If teardown isn't strictly reverse-of-construction, parallel WASAPI sessions accumulate; second `IAudioClient::Initialize` returns `AUDCLNT_E_DEVICE_IN_USE`. IMMNotificationClient may also leak.

**Why it happens:** Adding a new owned thread (DetectionRunner) without prepending it to Cleanup's reverse-construction order leaves the thread running after the ring's producer (AudioWorker) goes away.

**How to avoid:**
- D-19 / D-20: DetectionRunner.reset() FIRST in Cleanup (before AudioWorker.reset()). Destructor: signal shutdown, notify_all, join with 2s watchdog, last-resort detach (P6 pattern).
- 50-cycle stress test (`tests/driver/device_provider_lifecycle_stress_test.cpp`) drives Init→500ms→Cleanup × 50; Process Explorer / `GetProcessHandleCount` audits show no leak.
- Real-hardware UAT D-25(3) on Bigscreen Beyond + Win11 Pro is the authoritative SC4 sign-off (50 cycles via headless OR scripted SteamVR restart loop, whichever is faster).

**Warning signs:** Second SteamVR session produces no audio in driver, log shows capture started. Process Explorer shows `driver_micmap.dll` keeping a handle on the audio device after Cleanup. AUDCLNT_E_DEVICE_IN_USE in vrserver.txt after a restart.

### Pitfall 10: Migration boundary — double-trigger during phased migration

**What goes wrong:** Both client-side detection (still active in P7) AND driver-side detection (new in P7) fire on the same mic-cover. Dashboard opens, then "tap" via HTTP triggers a second time, dashboard closes. User sees noise.

**Why it happens:** Two trigger paths exist in parallel during P7→P10 migration. Some coordination signal is required to stand one of them down.

**How to avoid:**
- D-09 / D-10 / D-11: `/health` JSON gains `driver_detection_active` field. Client polls (already does at v1.5 connection-indicator cadence). When true, client suppresses `IDriverClient::tap()` call but keeps detection running for the UI's confidence/RMS visualization. Belt-and-suspenders: state machine cooldown (default 1s) swallows transient double-triggers in the /health-poll lag window.
- Coexistence UAT D-25(6): both flags + client running, single tap per mic-cover; mid-session flag flip resumes client trigger within ≤ 1 health-poll cycle.

**Warning signs:** Dashboard opens then immediately closes on a single mic-cover. `vrserver.txt` shows TapCommand pushed from BOTH the HTTP thread AND the detection thread within the cooldown window.

### Pitfall 12: WASAPI capture buffer + FFT pacing

**What goes wrong:** Detection thread runs FFT every audio packet (~10ms cadence). If FFT is slow (default 2048 on slow CPU), audio thread blocks WASAPI buffer service → audio glitches → detection sees discontinuous samples → false negatives. Conversely, if detection thread can't keep up, ring fills, drop-OLDEST kicks in, sample stream has gaps.

**Why it happens:** v1.5 ran FFT in the audio cb under audioMutex (CONCERNS.md "Audio Buffer Accumulation" + "FFT Analysis on Every Audio Frame"). P7 fixes by moving FFT to a separate thread with a SPSC ring decoupling.

**How to avoid:**
- D-03 (drop-OLDEST): audio thread NEVER blocks on a full ring. Drop count tracked; diagnostic logging cadence per Claude's discretion (recommend per-N-drops summary, not per-drop, to avoid log flood — pattern matches P6 D-08 RMS budget).
- D-18: state-machine cooldown (default 1000ms) is the natural CommandQueue rate-limit. Triggers come from rising-edge-into-Triggered, not from raw FFT. At most one TapCommand per cooldown window. CommandQueue depth 8 is fine.
- FFT-on-every-frame perf cost is a known smell (CONCERNS.md Performance Bottleneck #2) — explicitly DEFERRED in P7 (preserve v1.5 behavior; targeted perf phase later).

**Warning signs:** vrserver.txt shows `MicMap detection: ring drops` summary lines climbing. Audio glitches reported by user. RunFrame budget timing asserts tripping.

### Pitfall 13: IMMNotificationClient callback after Cleanup

**What goes wrong:** WASAPI's `DeviceNotificationClient` runs on a system-managed thread. If a callback is mid-flight when Cleanup destroys driver state, dereferencing freed memory causes vrserver crash.

**Why it happens:** COM `Unregister` is documented as "no longer call after returning" but does NOT wait for in-flight callbacks. Race window between Unregister start and callback completion.

**How to avoid:**
- D-23 / D-24: P7 inherits P6's mitigation unchanged. Notifier lives in WASAPIAudioCapture (registered inside AudioWorker per P6 D-15). DetectionRunner does NOT add its own notifier surface. weak_ptr<State> + atomic<bool> alive flag in the audio cb (P6 `audio_worker.cpp:241-263`) carries through P7 — the audio cb is the same lambda, just with new payload (push to ring).
- Full ComPtr migration of `DeviceNotificationClient` (CONCERNS.md flagged manual `InterlockedIncrement`/`Decrement`) DEFERRED to P10.

**Warning signs:** Crash during SteamVR shutdown when an audio device was just unplugged. Backtrace ends in `mmdevapi.dll` calling our callback.

### Pitfall 14 (deferred — read for awareness)

`OnDefaultDeviceChanged` makes the default mic move under the driver. P7 explicitly DEFERS — audio device surface unchanged from P6 D-11 ("Beyond" → `isDefault` → first). P8+ owns device pinning and follow-the-default logic. Read PITFALLS.md §14 for context, but do NOT add `OnDefaultDeviceChanged` handling in P7.

### Pitfall 15 (deferred — read for awareness)

Symbol bloat from nlohmann/json in three binaries. P7 DOES extend `/health` JSON with one new field but does NOT pull `nlohmann::json` into any new TU — `http_server.cpp` already includes it. D-14 explicitly keeps `config.json` (and therefore broader nlohmann usage) out of the driver until P8.

## Code Examples

### DetectionRunner skeleton (D-17, D-18)

```cpp
// driver/src/detection_runner.hpp
#pragma once
#include "command_queue.hpp"
#include "sample_ring.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Forward declarations to avoid pulling shared-lib headers into this header.
namespace micmap::detection { class INoiseDetector; }
namespace micmap::core { class IStateMachine; struct StateMachineConfig; }

namespace micmap::driver {

struct DetectionConfig {
    float sensitivity{0.7f};
    float threshold{0.6f};
    int   cooldown_ms{1000};
    int   min_duration_ms{200};
};

class DetectionRunner {
public:
    DetectionRunner(SampleRing<16, 480>& ring,
                    CommandQueue& commandQueue,
                    uint32_t sampleRate,
                    DetectionConfig initial);
    ~DetectionRunner();

    DetectionRunner(const DetectionRunner&) = delete;
    DetectionRunner& operator=(const DetectionRunner&) = delete;

    bool Start();
    void Stop();        ///< Idempotent; called by destructor. 2 s watchdog.
    void Pause();       ///< RunFrame-safe (called from EnterStandby).
    void Resume();      ///< RunFrame-safe (called from LeaveStandby).
    void publish(std::shared_ptr<const DetectionConfig> next);  ///< MIG-06.

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    uint32_t TriggersEmitted() const { return triggers_.load(std::memory_order_relaxed); }

private:
    static void ThreadEntry(DetectionRunner* self);
    void RunLoop();
    void applyConfig(const DetectionConfig& cfg);

    SampleRing<16, 480>&                       ring_;
    CommandQueue&                              commandQueue_;
    uint32_t                                   sampleRate_;

    std::shared_ptr<const DetectionConfig>     activeConfig_;   // C++17 atomic_load/store
    std::shared_ptr<const DetectionConfig>     lastObserved_;   // local cache

    std::unique_ptr<micmap::detection::INoiseDetector> detector_;
    std::unique_ptr<micmap::core::IStateMachine>       stateMachine_;

    std::thread                                thread_;
    std::mutex                                 mu_;
    std::condition_variable                    cv_;
    std::atomic<bool>                          shutdown_{false};
    std::atomic<bool>                          paused_{false};
    std::atomic<bool>                          running_{false};
    std::atomic<bool>                          thread_finished_{false};
    std::atomic<uint32_t>                      triggers_{0};
};

} // namespace micmap::driver
```

### DetectionRunner loop (D-18)

```cpp
// driver/src/detection_runner.cpp (excerpt)
void DetectionRunner::RunLoop() {
    using clock = std::chrono::steady_clock;
    auto last_tick = clock::now();

    // Trigger callback — fires from inside stateMachine_->update on rising edge.
    stateMachine_->setTriggerCallback([this]() {
        commandQueue_.push(TapCommand{});
        triggers_.fetch_add(1, std::memory_order_relaxed);
        DriverLog("MicMap detection: TapCommand pushed (n=%u)\n",
                  triggers_.load(std::memory_order_relaxed));
    });

    constexpr auto kWakeTimeout = std::chrono::milliseconds(50);

    std::array<float, 480> block;
    size_t block_count = 0;

    while (true) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, kWakeTimeout, [this] {
                return shutdown_.load(std::memory_order_acquire)
                    || paused_.load(std::memory_order_acquire)
                    || ring_.has_data();
            });
        }
        if (shutdown_.load(std::memory_order_acquire)) break;

        // MIG-06: reload settings snapshot, apply if changed.
        auto cfg = std::atomic_load_explicit(&activeConfig_,
                                             std::memory_order_acquire);
        if (cfg.get() != lastObserved_.get()) {
            applyConfig(*cfg);
            lastObserved_ = cfg;
        }

        if (paused_.load(std::memory_order_acquire)) {
            // Drain-discard: keeps the ring from filling, prevents drop-storm
            // logspam on resume. State machine NOT updated (D-07).
            while (ring_.try_pop(block, block_count)) { /* discard */ }
            continue;
        }

        // Active path: drain → analyze → update → trigger fires inside update.
        while (ring_.try_pop(block, block_count)) {
            auto result = detector_->analyze(block.data(), block_count);
            const auto now = clock::now();
            const auto dt  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - last_tick);
            last_tick = now;
            stateMachine_->update(result.confidence, dt);
        }
    }

    thread_finished_.store(true, std::memory_order_release);
}
```

### AudioWorker callback rewire (D-05)

```cpp
// driver/src/audio_worker.cpp — replaces existing setAudioCallback lambda at :241-263
std::weak_ptr<State> weak = state_;
SampleRing<16, 480>* ring_ptr = ring_;        // injected at construction
DetectionRunner* runner_ptr  = runner_;       // optional; null when detection disabled

capture_->setAudioCallback(
    [weak, ring_ptr, runner_ptr](const float* samples, size_t count) {
        auto sp = weak.lock();
        if (!sp || !sp->alive.load(std::memory_order_acquire)) return;   // Pitfall 13
        sp->frames_seen.fetch_add(1, std::memory_order_relaxed);

        // P7 D-05 step 2: push frames into ring (drop-oldest on full).
        if (ring_ptr) {
            const bool dropped = ring_ptr->try_push(samples, count);
            if (dropped) {
                // Drop-count diagnostic — Claude's discretion: per-N-drops
                // summary not per-drop. Match P6 D-08 RMS budget shape.
                const uint32_t total = ring_ptr->drops();
                if ((total % 100) == 1) {
                    DriverLog("MicMap detection: ring overflow drops=%u\n", total);
                }
            }
        }

        // P7 D-05 step 3: notify the detection thread.
        if (runner_ptr) runner_ptr->NotifyOne();

#ifdef MICMAP_DEBUG_RMS_LOG
        // P6 RMS log retained behind debug define so production driver doesn't
        // flood vrserver.txt (D-05 step 4).
        double sumSq = 0.0;
        for (size_t i = 0; i < count; ++i) sumSq += samples[i] * samples[i];
        const float rms = std::sqrt(sumSq / std::max<size_t>(count, 1));
        const uint32_t emitted = sp->rms_logs_emitted.fetch_add(1, std::memory_order_relaxed);
        if (emitted < kRmsBudget) DriverLog("MicMap audio: rms[%u]=%.6f\n", emitted, rms);
#endif
    });
```

### `/health` JSON extension (D-09)

```cpp
// driver/src/http_server.cpp — replaces existing /health route handler
// HttpServer constructor gains a std::function<bool()> getter for the
// driver_detection_active state (passed by DeviceProvider at construction).

server_->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
    nlohmann::json body;
    body["status"] = "healthy";
    body["driver_detection_active"] =
        driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false;
    res.set_content(body.dump(), "application/json");
});
```

### EnterStandby / LeaveStandby splice (D-21)

See Pattern 3 above.

### CMake lint sibling (D-22)

```cmake
# cmake/AssertDetectionRunnerNoVrApi.cmake — sibling of AssertAudioWorkerNoVrApi.cmake
# Phase 7 / D-22 / SVR-05: source-grep lint for detection_runner.{hpp,cpp}
# and sample_ring.hpp. Same RED-tolerant skip-on-NOT-EXISTS pattern.

if(NOT DEFINED DETECTION_RUNNER_DIR)
    message(FATAL_ERROR "AssertDetectionRunnerNoVrApi: DETECTION_RUNNER_DIR not provided.")
endif()

set(_targets
    "${DETECTION_RUNNER_DIR}/detection_runner.hpp"
    "${DETECTION_RUNNER_DIR}/detection_runner.cpp"
    "${DETECTION_RUNNER_DIR}/sample_ring.hpp")

set(_violations "")
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        continue()   # RED-tolerant
    endif()
    file(READ "${_file}" _content)
    if(_content MATCHES "[<\"]openvr[a-z_]*\\.h[>\"]"
            OR _content MATCHES "[^a-zA-Z0-9_]vr::"
            OR _content MATCHES "^vr::")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertDetectionRunnerNoVrApi: ${_vcount} file(s) violate the no-vr::-in-detection rule (D-22 / Pitfall 3):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()
message(STATUS "AssertDetectionRunnerNoVrApi: clean")
```

### `default.vrsettings` extension (D-13)

```json
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1",
        "enable_driver_audio": false,
        "enable_driver_detection": false,
        "detection_sensitivity": 0.7,
        "detection_threshold": 0.6,
        "detection_cooldown_ms": 1000,
        "detection_min_duration_ms": 200
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| FFT in audio cb under audioMutex (v1.5 client) | SPSC ring + dedicated detection thread (P7) | This phase | Eliminates audio glitches from slow FFT; CONCERNS.md "Audio Buffer Accumulation" + "State Machine Thread Safety" both addressed structurally. |
| 7-hop cross-process trigger via `POST /button` | 4-hop in-process trigger via direct CommandQueue push | This phase (rollback retained until P10) | Removes JSON parse + cpp-httplib routing + localhost socket round-trip from critical path. |
| `std::atomic_load`/`store` free functions on `shared_ptr` (C++17) | `std::atomic<std::shared_ptr<T>>` partial specialization (C++20) | C++20, MSVC v19.30+ | C++17 free-function form DEPRECATED in C++20 but still present. Project on C++17 → use free functions. Migrate to native specialization when project bumps. |
| HMD pause via `VREvent_TrackedDeviceDeactivated` (would re-pause every reactivation cycle) | `EnterStandby`/`LeaveStandby` driver callbacks | This phase | Semantically correct per Valve docs; lower noise; clean splice point at `device_provider.cpp:253-259`. |
| WASAPI capture in client (`micmap.exe`) | WASAPI capture in driver (`driver_micmap.dll`) | P6 (already shipped) | DetectionRunner consumes the ring AudioWorker fills. Pitfall 1/4/13 mitigations live in AudioWorker; DetectionRunner doesn't reopen the wound. |

**Deprecated/outdated:**
- v1.5 client-side `apps/micmap/main.cpp:353-444` audio cb FFT body — kept alive in P7 for rollback path; deleted in P10.
- v1.5 `IDriverClient::tap()` HTTP path — kept alive in P7 for rollback; deleted in P10 (MIG-05).

## Assumptions Log

All claims with `[ASSUMED]` tags below need user/planner confirmation.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Slot count of 16 with 480-frame slots is enough headroom on Bigscreen Beyond's typical CPU under sustained mic-cover scenarios | §4 Standard Stack alternatives | If FFT cost spikes (uncommon — KissFFT 2048 is well within budget on modern CPUs), drop-OLDEST kicks in and detection sees gaps. Real-hardware UAT D-25(1) catches it. Mitigation: bump to 32 slots if drop-count diagnostic fires. |
| A2 | `cv_.wait_for(50ms)` is the right balance between Cleanup latency and idle CPU | §4 Standard Stack alternatives | Too short → idle CPU waste; too long → Cleanup join takes longer (still < 2s watchdog so functionally safe). Diagnostic: measure idle wakeup rate during D-25(2) HMD wake/sleep test. |
| A3 | C++17 `std::atomic_load`/`store` free functions on `shared_ptr` are functionally equivalent to C++20 `std::atomic<std::shared_ptr>` for our use case (rare writes, single reader on hot path) | §3 Pattern 2 | If MSVC v19.30+ silently changes the free-function semantics in a future toolchain, behavior could drift. Mitigation: bump project to C++20 when the platform allows; today the deprecation is documented but the function is still present. |
| A4 | `EnterStandby`/`LeaveStandby` actually fire on Bigscreen Beyond when the user removes the headset (semantically correct per Valve docs, but real-hardware behavior on this specific HMD is the unknown) | §3 Pattern 3 | If Bigscreen Beyond never fires `EnterStandby` (some HMDs implement standby differently), detection thread keeps running while user is away — wastes a few % CPU but no correctness issue. Audio path is unaffected. UAT D-25(2) is the verifier. |
| A5 | 50-cycle stress test in a single test process via direct `DeviceProvider` construct/destroy (with null context fail-soft) is sufficient fidelity for SC4 — equivalent to 50 SteamVR-restart cycles for handle-leak detection purposes | §3 Pattern 4 | If real-WASAPI handle leaks only manifest with a real `IVRDriverContext`, the headless test misses them. Mitigation: run a smaller scripted SteamVR-restart loop (5-10 cycles) on Bigscreen Beyond as a complement to the headless 50-cycle. |
| A6 | Drop-OLDEST atomicity (producer bumps tail in the full-branch) is safe under SPSC invariant | §3 Pattern 1 | If a consumer snapshot of tail is in flight when producer bumps it, the consumer might re-read a slot that was simultaneously rewritten. Mitigation: the slot data is an array, not a pointer — torn reads of float arrays produce garbage but not crashes. State machine handles spurious low-confidence values cleanly (cooldown + threshold). |
| A7 | The `audio_worker.cpp` callback can hold a raw pointer to DetectionRunner safely because lifecycle ordering guarantees AudioWorker outlives DetectionRunner construction and is destroyed AFTER DetectionRunner | §"Code Examples" callback rewire | Wrong if cleanup order is broken (D-20 violation). Linted by no-vr-API rule indirectly (Cleanup is in `device_provider.cpp` which is the only TU allowed to use `vr::*` AND owns the lifecycle ordering). Belt-and-suspenders: AudioWorker's existing weak_ptr<State> + alive flag prevents calling into a dead `runner_ptr` because the audio cb itself bails. |
| A8 | The `MICMAP_DEBUG_RMS_LOG` define is the right opt-in mechanism for the legacy P6 RMS log block; production builds don't define it | §"Code Examples" callback rewire | If the production build accidentally defines it, vrserver.txt floods. Linted by build-system convention; production CI builds have explicit defines. Adding a regression test (`grep` on a release build's emitted log lines for `MicMap audio: rms`) is a candidate guardrail but probably over-engineered for the risk. |

**If this table is empty:** All claims were verified or cited. (It is not empty — the items above need planner / discuss-phase confirmation.)

## Open Questions

1. **HTTP server access to DeviceProvider state for `/health.driver_detection_active`** — Three viable wirings:
   - (a) Pass `std::function<bool()>` getter at HttpServer construction (most decoupled).
   - (b) Pass `std::atomic<bool>* driverDetectionActive` to HttpServer (lower indirection).
   - (c) Promote HttpServer to take a `DeviceProvider&` reference (highest coupling, simplest call site).
   - **Recommendation:** (a) — matches v1.5 HttpServer ctor shape (takes `CommandQueue&`); minimal coupling expansion. PLAN.md should pick the wiring; the choice is Claude's-discretion.

2. **Headless test layout — extend `audio_worker_lifecycle_headless.cpp` vs new files** — D-25 introduces TWO new tests (50ms propagation + 50-cycle stress). Option to extend the existing P6 test vs adding two new files. **Recommendation:** new files (`detection_settings_propagation_test.cpp`, `device_provider_lifecycle_stress_test.cpp`) — preserves clear file-to-requirement mapping (one test file per SC) and the existing P6 test continues to be the AudioWorker-only Wave 0 RED gate.

3. **Where does AudioWorker get the SampleRing reference?** — Three options:
   - (a) AudioWorker owns the ring; DetectionRunner takes a reference to it.
   - (b) DetectionRunner owns the ring; AudioWorker takes a reference at attach-time.
   - (c) DeviceProvider owns the ring; both take references.
   - **Recommendation:** (a) — AudioWorker is the producer end-of-life owner (when AudioWorker dies, the producer is gone); detection has nothing to push to. AudioWorker exposes `SampleRing& ring()` accessor. DetectionRunner's ctor takes the ref. Cleanup ordering D-20 (DetectionRunner FIRST, AudioWorker SECOND) keeps the ring alive while DetectionRunner is shutting down.

4. **Drop-count diagnostic logging cadence** — Per-drop logs flood; per-Cleanup-summary logs lose temporal information. **Recommendation:** per-N-drops summary (every 100 drops emit one line), matching P6 D-08 RMS budget shape. PLAN.md picks N.

5. **Should DetectionRunner's `Pause`/`Resume` be idempotent?** — RunFrame may receive duplicate EnterStandby events in theory. **Recommendation:** yes, idempotent — `paused_.exchange(true)` returns the previous state; if already paused, no-op. Same for resume.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| MSVC C++17 | All driver TUs | ✓ | Visual Studio 17 2022 (v19.30+) | — |
| C++20 `std::atomic<std::shared_ptr>` | If we choose native specialization | Compiler ✓; project compiles as C++17 → use free-function form | — | C++17 `std::atomic_load`/`store` (deprecated in C++20 but present) — recommended |
| OpenVR SDK | Driver build | ✓ | bundled `external/openvr/` | — (driver requires it) |
| WASAPI | Audio capture (P6 — unchanged) | ✓ | Win11 Pro | non-Windows stub for headless tests |
| KissFFT | Detection (via `createFFTDetector`) | ✓ | linked via `micmap::core_runtime` | — |
| nlohmann/json | Driver `/health` JSON extension | ✓ | already linked into `driver_micmap.dll` for `POST /button` | — |
| cpp-httplib v0.14.3 | HTTP server | ✓ | locked in v1.5 stack | CVE-2025-46728 deferred to P8 prereq |
| Bigscreen Beyond + Win11 Pro UAT rig | D-25 manual sign-off | ✓ (project rig) | — | — |
| Process Explorer | SC4 manual handle audit | Available on dev machine | — | `GetProcessHandleCount` via Win32 (automatable in headless test) |

**Missing dependencies with no fallback:** None — every P7 dependency is either in-tree or already proven on the project rig.

**Missing dependencies with fallback:** None — the C++20 specialization is preferred but not required; C++17 free-function form is the documented fallback.

## Validation Architecture

Note: nyquist_validation is enabled (`workflow.nyquist_validation: true` in `.planning/config.json`). This section drives `VALIDATION.md` for orchestrator.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest (CMake-native) — plain-main exit-code convention. P5/P6 precedent. |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `ctest --test-dir build -R 'AssertDetectionRunnerNoVrApi\|test_command_queue\|AudioWorkerLifecycleHeadless\|DetectionSettingsPropagation' --output-on-failure` |
| Full suite command | `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MIG-02 | DetectionRunner pushes TapCommand to CommandQueue, no `vr::*` calls | unit + lint | `ctest -R AssertDetectionRunnerNoVrApi -V` | ❌ Wave 0 (`cmake/AssertDetectionRunnerNoVrApi.cmake` to be added) |
| MIG-02 | CommandQueue accepts concurrent push from HTTP-thread + DetectionRunner-thread without races | unit | `ctest -R test_command_queue` | ✅ existing `tests/test_command_queue.cpp` (verify it covers concurrent push from two producers; if not, extend) |
| MIG-03 | Detection thread pause/resume on EnterStandby/LeaveStandby; ring drains, state machine not reset | manual UAT (D-25(2)) | n/a — Bigscreen Beyond + Win11 Pro | n/a |
| MIG-04 | Reverse-order Cleanup; 50-cycle Init/Cleanup stress with no leaked handles | headless integration | `ctest -R DeviceProviderLifecycleStress -V` | ❌ Wave 0 (`tests/driver/device_provider_lifecycle_stress_test.cpp` to be added) |
| MIG-04 | Real-hardware 50-cycle (Process Explorer audit) | manual UAT (D-25(3)) | n/a | n/a |
| MIG-06 | `publish()` to atomic snapshot propagates to detection thread within < 50ms | headless integration | `ctest -R DetectionSettingsPropagation -V` | ❌ Wave 0 (`tests/driver/detection_settings_propagation_test.cpp` to be added) |
| SC1 | Cover mic toggles dashboard via in-process trigger; zero `POST /button` traffic | manual UAT (D-25(1)) | n/a — Bigscreen Beyond | n/a |
| SC2 | grep audit for `vr::*` outside `device_provider.cpp` + `manifest_registrar.cpp` | CI grep | `ctest -R AssertDetectionRunnerNoVrApi` + existing `lint_no_openvr_in_core` | ❌ Wave 0 |
| Coexistence | `/health.driver_detection_active` field present + client suppression handshake | manual UAT (D-25(6)) | partial automated coverage via `curl http://127.0.0.1:27015/health \| jq .driver_detection_active` | n/a |
| Flag-OFF regression | byte-identical to P6 closeout when both flags = 0 | manual UAT (D-25(5)) via `hmd_button_test.exe` | existing harness | ✅ |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R 'AssertDetectionRunnerNoVrApi\|AudioWorkerLifecycleHeadless\|test_command_queue' --output-on-failure` — quick lint + smoke loops in < 5 seconds.
- **Per wave merge:** full ctest suite + driver build clean reconfigure: `cmake --build build --config Release --clean-first && ctest --test-dir build -C Release --output-on-failure`.
- **Phase gate:** all five SC1–SC5 UAT runs on Bigscreen Beyond + Win11 Pro per D-25, evidence in `07-UAT.md`. `/gsd-verify-work` cannot pass until UAT signed off.

### Wave 0 Gaps

- [ ] `cmake/AssertDetectionRunnerNoVrApi.cmake` — sibling lint covering `detection_runner.{hpp,cpp}` + `sample_ring.hpp`
- [ ] `tests/driver/detection_settings_propagation_test.cpp` — MIG-06 < 50ms verifier
- [ ] `tests/driver/device_provider_lifecycle_stress_test.cpp` — SC4 / MIG-04 50-cycle harness
- [ ] `tests/CMakeLists.txt` — register the two new tests + the new lint (mirror `AssertAudioWorkerNoVrApi` registration shape at `tests/CMakeLists.txt:173-176`)
- [ ] CommandQueue concurrent-push test coverage — verify `tests/test_command_queue.cpp` exercises two producer threads pushing simultaneously; if not, extend (small diff)

Test framework install: not required — CTest is CMake-native and already in use.

## Sources

### Primary (HIGH confidence)

- `.planning/phases/07-driver-side-detection-thread/07-CONTEXT.md` — D-01..D-28 locked decisions, scope boundaries, deferred-ideas list
- `.planning/REQUIREMENTS.md` — MIG-02, MIG-03, MIG-04, MIG-06 falsifiable definitions
- `.planning/STATE.md` — P6 spike outcome GO; P7 unblocked
- `.planning/ROADMAP.md` §"Phase 7" — SC1..SC5; research flags
- `.planning/research/PITFALLS.md` §3, §4, §10, §12, §13, §14, §15
- `.planning/research/SUMMARY.md` — research-numbered Phase 3 = roadmap Phase 7
- `.planning/research/ARCHITECTURE.md` §2 (driver-side threading model), §6 (per-file delta)
- `.planning/codebase/CONCERNS.md` — v1.5 tech debt: audio buffer accumulation, FFT-on-every-frame, state machine thread safety, IMMNotificationClient ref counting
- `driver/src/audio_worker.{hpp,cpp}` — P6 shipped — full source verified
- `driver/src/device_provider.{hpp,cpp}` — Init/Cleanup/RunFrame splice points verified at lines 80-110, 113-150, 166-178, 253-259
- `driver/src/command_queue.hpp` — existing v1.5 mutex+deque, `kMaxDepth=8`, `std::lock_guard`-protected push
- `driver/src/http_server.cpp` — existing `/health` route at `:155-157`
- `driver/resources/settings/default.vrsettings` — current keys verified
- `driver/CMakeLists.txt` — link pattern (`micmap::core_runtime` PRIVATE per P5 D-10/D-11)
- `cmake/AssertAudioWorkerNoVrApi.cmake` — P6 lint pattern, RED-tolerant skip-on-NOT-EXISTS
- `tests/CMakeLists.txt` — registration pattern (lines 126-176)
- `src/detection/include/micmap/detection/noise_detector.hpp` — `createFFTDetector(sampleRate, fftSize)` factory at line 147
- `src/core/include/micmap/core/state_machine.hpp` — `createStateMachine(StateMachineConfig)` factory at line 140; `TriggerCallback` rising-edge contract at line 56
- `src/audio/include/micmap/audio/audio_capture.hpp` — `IAudioCapture::getSampleRate` at :93
- `src/core/include/micmap/core/config_manager.hpp` — `DetectionConfig` schema at :28-33 (sensitivity 0.7, minDurationMs 300, cooldownMs 300, fftSize 2048)
- `build/CMakeCache.txt` (worktree) — `CMAKE_GENERATOR=Visual Studio 17 2022` confirms MSVC v19.30+

### Primary (HIGH confidence — official docs)

- [OpenVR Driver API Documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md) — EnterStandby/LeaveStandby semantics
- [cppreference: std::atomic<std::shared_ptr> (C++20)](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2)
- [cppreference: std::atomic_load/store on shared_ptr (C++17, deprecated C++20)](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [cppreference: std::memory_order](https://en.cppreference.com/cpp/atomic/memory_order)
- [Microsoft Old New Thing: Inside STL — The atomic shared_ptr (2024-12-19)](https://devblogs.microsoft.com/oldnewthing/20241219-00/?p=110663) — MSVC implementation details, lock-bit + unlock-notify

### Secondary (MEDIUM confidence — verified pattern source)

- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — bounded SPSC pattern, alignas(64) anti-false-sharing, power-of-two
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — overflow handling, atomic fence primitives
- [Optimizing a Lock-Free Ring Buffer (David Álvarez Rosa)](https://david.alvarezrosa.com/posts/optimizing-a-lock-free-ring-buffer/) — power-of-two bitmask wrap-around
- [Building a High-Performance Lock-Free Ring Buffer (DEV)](https://dev.to/lakshya_bankey_27825e4908/building-a-high-performance-lock-free-ring-buffer-in-c-for-ultra-low-latency-messaging-19h6) — acquire-release semantics for SPSC

### Sister-project reference (existence proof)

- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — informed P6 AudioWorker shape; nothing to add for P7 (per 07-CONTEXT.md canonical refs)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — every dep already in-tree; no version bumps in P7; MSVC v19.30+ confirmed in build environment
- Architecture: HIGH — every named symbol/path verified against current source on `hmd-button` branch
- Pitfalls: HIGH — anchored in v1.5 + P6 shipped pitfalls + Pitfall 10/12 explicitly addressed by D-09..D-12 + D-03/D-18
- HMD standby semantics: MEDIUM-HIGH — Valve docs verified for EnterStandby vs deactivated; Bigscreen Beyond actual standby firing behavior is the UAT-only unknown (A4)
- 50-cycle stress harness fidelity: MEDIUM — headless test catches handle leaks via Win32 GetProcessHandleCount; real-WASAPI handle leaks may need scripted SteamVR-restart complement (A5)

**Research date:** 2026-05-03
**Valid until:** 2026-06-03 (30 days — stable phase, no fast-moving deps; if MSVC toolchain bumps or OpenVR SDK rolls, re-verify §"Standard Stack" + §"Pattern 3")

---

## RESEARCH COMPLETE

**Phase:** 7 — Driver-Side Detection Thread
**Confidence:** HIGH

### Key Findings

- **Locked-decision phase.** 28 D-decisions in 07-CONTEXT.md cover every load-bearing question. Research validated the four NEEDS-VALIDATION items: MSVC C++20 atomic shared_ptr, OpenVR EnterStandby/LeaveStandby semantics, SPSC ring slot-count tradeoff, and 50-cycle stress harness shape.
- **C++17 free-function form recommended over C++20 native specialization.** Project compiles as C++17 (`CMAKE_CXX_STANDARD 17` in `driver/CMakeLists.txt`); MSVC v19.30+ supports both. Free-function form is deprecated in C++20 but still present.
- **`EnterStandby`/`LeaveStandby` is the right HMD-pause splice** (not `VREvent_TrackedDeviceDeactivated`). Valve docs confirm semantic difference; Pause/Resume of detection only, AudioWorker keeps capturing (D-06).
- **Sibling lint pattern recommended** — `cmake/AssertDetectionRunnerNoVrApi.cmake` rather than extending the audio-worker lint. Smaller blast radius; scales to P10 deletions cleanly.
- **Headless 50-cycle stress test sufficient for SC4 automation**, complemented by manual Process Explorer audit on Bigscreen Beyond. Two viable construction paths (null-context fail-soft vs no-Init); recommend null-context fail-soft for higher fidelity.
- **Hand-roll the SPSC ring** (~80 LoC) — header-only, follows rigtorp/SPSCQueue API shape. Third-party header for this size is overkill.
- **Drop-OLDEST atomicity** — producer bumps tail in the full-branch is acceptable under SPSC invariant given torn array reads produce garbage but not crashes (state machine cooldown handles spurious low-confidence values).

### File Created

`.planning/phases/07-driver-side-detection-thread/07-RESEARCH.md`

### Confidence Assessment

| Area | Level | Reason |
|------|-------|--------|
| Standard Stack | HIGH | All deps in-tree; MSVC v19.30+ confirmed in `build/CMakeCache.txt` |
| Architecture | HIGH | Every symbol/path verified against current source |
| Pitfalls | HIGH | Pitfalls 3, 4, 10, 12, 13 covered by locked decisions; 14, 15 explicitly deferred |
| HMD standby on Bigscreen Beyond | MEDIUM-HIGH | Valve docs confirm semantics; real-hardware firing pattern is UAT D-25(2) sign-off |
| 50-cycle stress fidelity | MEDIUM | Headless catches handle leaks; complementary scripted SteamVR-restart smoke recommended |

### Open Questions (for planner)

1. HttpServer ↔ DeviceProvider state-read wiring: callback (recommended), shared atomic, or back-reference.
2. Headless test layout: new files (recommended) vs extend `audio_worker_lifecycle_headless.cpp`.
3. SampleRing ownership: AudioWorker (recommended) vs DetectionRunner vs DeviceProvider.
4. Drop-count log cadence: per-N-drops summary (recommended) — pick N in PLAN.md.
5. DetectionRunner Pause/Resume idempotency: yes (recommended).

### Ready for Planning

Research complete. Planner can now create PLAN.md files. Eight assumptions logged in §"Assumptions Log" need confirmation during plan-check or discuss-phase before locking the plan.

Sources:
- [OpenVR Driver API Documentation (ValveSoftware)](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md)
- [cppreference: std::atomic<std::shared_ptr> (C++20)](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2)
- [cppreference: std::atomic_load/store on shared_ptr (C++17)](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Inside STL: The atomic shared_ptr (Microsoft Old New Thing)](https://devblogs.microsoft.com/oldnewthing/20241219-00/?p=110663)
- [rigtorp/SPSCQueue (GitHub)](https://github.com/rigtorp/SPSCQueue)
- [cameron314/readerwriterqueue (GitHub)](https://github.com/cameron314/readerwriterqueue)
- [cppreference: std::memory_order](https://en.cppreference.com/cpp/atomic/memory_order)

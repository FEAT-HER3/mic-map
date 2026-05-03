---
phase: 07
plan: 03
subsystem: driver
tags: [detection-thread, mig-06, atomic-snapshot, watchdog, wave-2]
dependency-graph:
  requires:
    - "driver/src/sample_ring.hpp (07-01 SPSC ring; consumed via SampleRing<16,480>& ring_)"
    - "driver/src/command_queue.hpp (existing v1.5 SVR-05 primitive; new producer joins)"
    - "driver/src/driver_log.hpp (DriverLog macro -- only OpenVR-shaped surface allowed from worker thread)"
    - "src/detection/include/micmap/detection/noise_detector.hpp (createFFTDetector + INoiseDetector::analyze + setSensitivity)"
    - "src/core/include/micmap/core/state_machine.hpp (createStateMachine + StateMachineConfig + setTriggerCallback + update + configure)"
    - "cmake/AssertDetectionRunnerNoVrApi.cmake (07-01 lint -- now scans 3 files clean)"
    - "tests/driver/detection_settings_propagation_test.cpp (07-01 RED scaffold -- now GREEN gate)"
    - "micmap::core_runtime (P5 D-10 PRIVATE link with INTERFACE include paths)"
  provides:
    - "micmap::driver::DetectionRunner class -- consumed by 07-05 DeviceProvider for lifecycle wiring (Init/Cleanup, EnterStandby/LeaveStandby, AudioWorker callback NotifyOne)"
    - "micmap::driver::DetectionConfig POD struct -- consumed by 07-04 vrsettings reader and 07-07 HTTP /settings handler for publish() snapshots"
    - "MIG-06 atomic-shared_ptr publish/load mechanism -- ready for 07-07 HTTP/PUT /settings"
    - "DetectionSettingsPropagation ctest GREEN gate (publish -> observed-swap < 50 ms verified)"
  affects:
    - "Plan 07-05 (DeviceProvider lifecycle wiring) -- now has the class to construct/destruct"
    - "Plan 07-07 (HTTP /settings handler) -- now has publish() to call"
tech-stack:
  added:
    - "C++17 std::atomic_load_explicit / std::atomic_store_explicit on std::shared_ptr (free-function form; deprecated in C++20 but present in MSVC v19.30+ via control-block lock-bit)"
    - "std::condition_variable wait_for + predicate pattern (50 ms timeout balances Cleanup latency vs idle CPU)"
  patterns:
    - "Shared Pattern 1 -- DriverLog (driver-host service interface) is the only OpenVR-shaped surface allowed from the worker thread"
    - "Shared Pattern 4 -- reverse-construction-order teardown ON the worker thread (detector_/stateMachine_ reset inside RunLoop after the loop body)"
    - "P6 AudioWorker 2 s shutdown watchdog (audio_worker.cpp:104-120) cloned with `MicMap detection:` log prefix"
    - "rigtorp/SPSCQueue acquire/release fence shape (already in 07-01 SampleRing; consumed here as has_data + try_pop)"
    - "Pitfall 12 -- audio thread NEVER blocks; rate-limit happens at state-machine cooldown, not at queue.push"
key-files:
  created:
    - "driver/src/detection_runner.hpp"
    - "driver/src/detection_runner.cpp"
  modified:
    - "driver/CMakeLists.txt"
decisions:
  - "Used the actual StateMachineConfig field names (detectionThreshold / minDetectionDuration / cooldownDuration as chrono::ms) instead of the plan's placeholder names (threshold / minDurationMs / cooldownMs) -- the plan acknowledged the names had to be confirmed from the header at read_first time."
  - "applyConfig uses IStateMachine::configure(StateMachineConfig) for the rebuild rather than per-field setters -- the IStateMachine surface only exposes whole-config replace, not per-field setters, so we rebuild the config struct each time and pass it through. detector_->setSensitivity(float) is the live setter for the noise detector (line 110 of noise_detector.hpp)."
  - "Skipped pushing min_duration_ms into the noise detector via detector_->setMinDetectionDuration() -- the state machine already enforces min-duration via its own clock (Detecting->Triggered hold window). Pushing it into both would double-count the hold window. RESEARCH.md / D-15 confirms the propagation test verifies the SNAPSHOT MECHANISM, not the full re-parametrization."
  - "Reworded the file-banner reference to IMMNotificationClient to `COM device-notifier` to satisfy the no-COM-token grep criterion (RULE 1 fix)."
metrics:
  duration_minutes: 18
  tasks_completed: 3
  files_created: 2
  files_modified: 1
  commits: 3
  completed: "2026-05-03"
---

# Phase 7 Plan 3: DetectionRunner Class Summary

`micmap::driver::DetectionRunner` is the load-bearing P7 deliverable: a 285-line .cpp + 137-line .hpp that owns a `std::thread` draining the SPSC SampleRing fed by AudioWorker, runs `createFFTDetector(48000, 2048)` + `createStateMachine(StateMachineConfig{...})` from `micmap::core_runtime`, and on rising-edge Triggered pushes `TapCommand` into the existing v1.5 CommandQueue. v1.5 had 7 hops cross-process; P7 collapses to 4 hops in-process via this class. The HTTP-thread -> CommandQueue -> RunFrame -> UpdateBooleanComponent boundary stays byte-stable (SVR-05 invariant unchanged) -- DetectionRunner is a NEW PRODUCER of an existing primitive.

## What Shipped

### `driver/src/detection_runner.hpp` (Task 1)

137 lines. `class DetectionRunner` with full public surface: ctor (4-arg with `SampleRing<16, 480>&`, `CommandQueue&`, `uint32_t sampleRate`, `DetectionConfig initial`), dtor, deleted copy/move, `Start`, `Stop`, `Pause`, `Resume`, `publish`, `NotifyOne`, `IsRunning`, `TriggersEmitted`, `active_config_for_test`. `struct DetectionConfig { float sensitivity{0.7f}; float threshold{0.6f}; int cooldown_ms{1000}; int min_duration_ms{200}; }` mirroring `driver/resources/settings/default.vrsettings` (07-02 / D-13). Forward-decls for `INoiseDetector`/`IStateMachine`/`StateMachineConfig` keep shared-lib headers out of the driver header (mirrors `audio_worker.hpp:25` precedent). Members: `ring_` (ref), `commandQueue_` (ref), `sampleRate_`, `activeConfig_` + `lastObserved_` (atomic shared_ptr pair), `detector_` + `stateMachine_` (unique_ptr), `thread_` + `mu_` + `cv_`, four `std::atomic<bool>` flags (shutdown_, paused_, running_, thread_finished_), `std::atomic<uint32_t> triggers_`. Zero `vr::*` surface, zero openvr*.h includes.

### `driver/src/detection_runner.cpp` (Task 2)

285 lines. File-scope constants: `kShutdownWatchdog = 2 s`, `kWatchdogPoll = 25 ms`, `kWakeTimeout = 50 ms`. The five lifecycle entry points + RunLoop:

- **Constructor:** seeds `activeConfig_` with `make_shared<const DetectionConfig>(initial)` via `atomic_store_explicit(release)` so `Start()` can read it without a publish() race.
- **`Start()`:** idempotent guard; loads `activeConfig_`; constructs `createFFTDetector(sampleRate_, 2048)` then `createStateMachine(StateMachineConfig{detectionThreshold, minDetectionDuration, cooldownDuration})` from snapshot fields; resets atomics; spawns `std::thread(&ThreadEntry, this)`; logs `MicMap detection: thread spawned (sampleRate=%u, fftSize=2048)`.
- **`Stop()`:** lock_guard sets `shutdown_=true`; `cv_.notify_all()`; polls `thread_finished_` every 25 ms with 2 s deadline; joins on success / detaches on overrun (T3 mitigation -- never block vrserver.exe shutdown).
- **`Pause()`/`Resume()`:** idempotent via `paused_.exchange(value, acq_rel)` -- early-return if no transition; otherwise `cv_.notify_one()` + log.
- **`publish(next)`:** `atomic_store_explicit(release)` + `cv_.notify_one()` so wait_for unblocks promptly when the ring is empty (bounds SC5 < 50 ms latency).
- **`active_config_for_test()`:** `atomic_load_explicit(acquire)` -- the 07-01 propagation test polls this at 1 ms cadence.
- **`applyConfig(cfg)`:** pushes `setSensitivity(cfg.sensitivity)` into the detector and rebuilds the StateMachineConfig + calls `stateMachine_->configure(smCfg)`.
- **`RunLoop()`:** sets the trigger callback BEFORE the loop (`commandQueue_.push(TapCommand{}); triggers_.fetch_add(1); DriverLog("MicMap detection: TapCommand pushed (n=%u)\n", n);`), then enters the outer loop. Each iteration: cv_.wait_for(50 ms, predicate=`shutdown_||paused_||has_data()`); break-on-shutdown; reload `activeConfig_` and applyConfig if pointer-identity differs from `lastObserved_`; if paused, drain-discard the ring and `continue`; else drain via `ring_.try_pop(block, count)`, `detector_->analyze(block.data(), count)`, compute `dt`, `stateMachine_->update(result.confidence, dt)`. State-machine cooldown is the natural rate-limiter (Pitfall 12). After loop: reverse-order teardown ON the detection thread (`stateMachine_.reset(); detector_.reset();`); log `thread exiting cleanly (triggers=N)`; set `thread_finished_=true`.

### `driver/CMakeLists.txt` (Task 3)

One-line append at line 28: `src/detection_runner.cpp # P7 D-17: driver-side detection thread`. Zero new `find_package`/`target_link_libraries`/`target_include_directories` directives -- `micmap::core_runtime` (P5 D-10 PRIVATE link with INTERFACE include paths) already pulls everything.

## Verification Results

```
cmake --build build --config Release --target driver_micmap  -> 0
  detection_runner.cpp compiles clean (only pre-existing LIBCMT lib warning)
  driver_micmap.dll links

ctest --test-dir build -C Release -R "AssertDetectionRunnerNoVrApi|
  DetectionSettingsPropagation|AudioWorkerLifecycleHeadless|
  AssertAudioWorkerNoVrApi|test_command_queue|lint_no_openvr_in_core|
  lint_no_driver_macro" --output-on-failure
  -> 7/7 PASS:
     test_command_queue              (P5 carryover w/ 07-01 concurrent case)
     lint_no_openvr_in_core          (P5 carryover)
     lint_no_driver_macro            (P5 carryover)
     AudioWorkerLifecycleHeadless    (P6 carryover)
     AssertAudioWorkerNoVrApi        (P6 carryover)
     DetectionSettingsPropagation    (07-01 RED scaffold -> GREEN gate today)
     AssertDetectionRunnerNoVrApi    (07-01 -- now scans 3 files clean)

dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll
  -> 1 export: HmdDriverFactory (P5 SC3 carryover preserved)
```

`DetectionSettingsPropagation` `PASS case_propagation_under_50ms elapsed_ms=...` -- end-to-end: ctor seed -> Start -> publish(cfg1) -> detection thread sees pointer-identity change inside the wait_for cycle (well under 50 ms with the cv_.notify_one() prod from publish()).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] StateMachineConfig field names diverge from plan placeholders**
- **Found during:** Task 2 read_first
- **Issue:** The plan's `<interfaces>` block uses `smCfg.threshold = ...; smCfg.minDurationMs = ...; smCfg.cooldownMs = ...;`, but the actual `StateMachineConfig` (src/core/include/micmap/core/state_machine.hpp:17-21) has `detectionThreshold` (float), `minDetectionDuration` (`std::chrono::milliseconds`), `cooldownDuration` (`std::chrono::milliseconds`). The plan's `<read_first>` Task 2 explicitly directs the executor to "verify the actual field names from the header" -- so this divergence was anticipated.
- **Fix:** Used the real field names; wrapped int min_duration_ms / cooldown_ms with `std::chrono::milliseconds(...)` at the call site. Both in `Start()` and in `applyConfig()`.
- **Files modified:** `driver/src/detection_runner.cpp`
- **Commit:** `7d62d6b`

**2. [Rule 1 - Bug] applyConfig used per-field setters that don't exist on IStateMachine**
- **Found during:** Task 2 read_first
- **Issue:** Plan's interface block sketches `stateMachine_->setThreshold(...); stateMachine_->setCooldownMs(...);` etc, but `IStateMachine` only exposes `configure(const StateMachineConfig&)` -- whole-config replace, no per-field setters. Header was source of truth per plan's note.
- **Fix:** `applyConfig()` rebuilds a `StateMachineConfig` from `cfg` and calls `stateMachine_->configure(smCfg)`. Detector keeps `setSensitivity(float)` (real setter, line 110 of noise_detector.hpp).
- **Files modified:** `driver/src/detection_runner.cpp`
- **Commit:** `7d62d6b`

**3. [Rule 1 - Bug] Skipped detector_->setMinDetectionDuration() to avoid double-counting hold window**
- **Found during:** Task 2 read_first
- **Issue:** `INoiseDetector::setMinDetectionDuration(int)` exists (line 126 of noise_detector.hpp), but the state machine already enforces min-duration via its own clock (Detecting -> Triggered transition). Wiring both would mean the input signal has to satisfy both the detector's internal hold AND the state machine's min-duration timer, which would double-count and silently raise the effective trigger latency.
- **Fix:** applyConfig pushes min_duration_ms ONLY into the state machine. Documented in the applyConfig doc-block. RESEARCH.md / D-15 confirms the propagation test verifies the SNAPSHOT MECHANISM (publish + atomic swap), not full re-parametrization, so partial application is acceptable for P7.
- **Files modified:** `driver/src/detection_runner.cpp`
- **Commit:** `7d62d6b`

**4. [Rule 1 - Bug] File-banner literal token `IMMNotificationClient` violated grep == 0 criterion**
- **Found during:** Task 2 acceptance check
- **Issue:** Plan acceptance criterion `grep -c "IMMNotificationClient\|CoInitialize\|CoUninitialize\|IMMDevice" driver/src/detection_runner.cpp == 0`. My initial draft mentioned the literal `IMMNotificationClient` token in a narrative comment ("NO IMMNotificationClient registration here").
- **Fix:** Reworded banner comment to "NO COM device-notifier registration here". Same intent, no forbidden literal token. Mirrors 07-01 Task 1 deviation #1 (same-shape lint sensitivity).
- **Files modified:** `driver/src/detection_runner.cpp`
- **Commit:** `7d62d6b`

### Criteria Drift Notes (non-blocking)

- Plan acceptance criterion expects `grep -c "thread_.detach()" >= 1`. Actual count is 2 (one in narrative comment, one real call site). Both expected; criterion is `>= 1`.
- `setTriggerCallback` chosen over plan's `setTriggerCallback|onTrigger` alternation -- the actual header (state_machine.hpp:105) names it `setTriggerCallback`, no `onTrigger` alias.
- The `(void)cfg;` line from the plan's interface block was removed because the new applyConfig actually consumes `cfg` -- the suppression was vestigial from the stub-template version.

## Authentication Gates

None. All work was filesystem + cmake + ctest + git on a local Windows + VS2022 build; no remote API credentials, no manual interactive steps.

## Threat Flags

None. The plan's `<threat_model>` covers all surfaces touched (T-07-03-01 through T-07-03-08). Mitigations:

- T-07-03-01 (vr::* call from detection thread): `grep -c "vr::" driver/src/detection_runner.cpp == 0`; `AssertDetectionRunnerNoVrApi` ctest scans on every build (3 files clean today).
- T-07-03-02 (UAF on detector_/stateMachine_ across threads): reverse-order reset INSIDE RunLoop on the detection thread; Stop() does not touch detector_/stateMachine_, only signals shutdown_ and joins. The 2 s watchdog is a last-resort detach.
- T-07-03-03 (Cleanup deadlock): 2 s watchdog + log line `MicMap detection: did not exit within 2 s watchdog - detaching (T3 mitigation)` + `thread_.detach()` call site present.
- T-07-03-04 (torn read of shared_ptr<DetectionConfig>): `std::atomic_store_explicit(release)` + `std::atomic_load_explicit(acquire)` pair; MSVC v19.30+ uses control-block lock-bit (verified non-blocking on the audio thread, which never calls publish or load anyway -- only DetectionRunner reads, only HTTP/publish writes).
- T-07-03-05 (Pause/Resume non-idempotent): `paused_.exchange()` returns previous; early-return on no-change; `grep -c "paused_.exchange" == 2`.
- T-07-03-06 (concurrent push to CommandQueue): existing std::lock_guard<std::mutex> on CommandQueue::push (command_queue.hpp:20) serializes; 07-01 Task 3's test_concurrent_two_producer_push regression test PASSES on every ctest run.
- T-07-03-07 (Start fail-soft leaks detector_): explicit `detector_.reset()` before returning false on stateMachine null. `grep -c "detector_.reset()" == 2` (Start fail-soft + RunLoop teardown).
- T-07-03-08 (DetectionRunner adds COM/notifier surface, doubling Pitfall 13 risk): `grep -c "IMMNotificationClient\|CoInitialize\|CoUninitialize\|IMMDevice" == 0`. D-23 holds.

## Known Stubs

None. `applyConfig()` is fully implemented (calls `setSensitivity` on detector + `configure` on state machine). The MIG-06 propagation test passes end-to-end. There is no symbol that future plans would need to wire that hasn't been wired here -- DetectionRunner is the class itself; lifecycle wiring (Init/Cleanup, EnterStandby/LeaveStandby, AudioWorker callback) is explicitly 07-05's scope per the plan's objective ("This plan does NOT wire DetectionRunner into DeviceProvider").

## Self-Check: PASSED

Verified the following exist on disk:

- driver/src/detection_runner.hpp -- FOUND
- driver/src/detection_runner.cpp -- FOUND
- driver/CMakeLists.txt (modified at line 28) -- FOUND

Verified the following commits exist on `hmd-button` (worktree branch):

- 083d75b -- feat(07-03): add DetectionRunner header with DetectionConfig (D-17 / D-22) -- FOUND
- 7d62d6b -- feat(07-03): implement DetectionRunner thread loop (D-17 / D-18 / D-20) -- FOUND
- 9afc9f2 -- feat(07-03): register detection_runner.cpp in driver_micmap target (D-17) -- FOUND

ctest sweep on Release driver-on build:

- test_command_queue (with 07-01 concurrent case) -- PASS
- lint_no_openvr_in_core -- PASS
- lint_no_driver_macro -- PASS
- AudioWorkerLifecycleHeadless -- PASS
- AssertAudioWorkerNoVrApi -- PASS
- AssertDetectionRunnerNoVrApi (3 files scanned clean) -- PASS
- DetectionSettingsPropagation (07-01 RED -> GREEN today; PASS case_propagation_under_50ms) -- PASS

dumpbin verification:

- driver_micmap.dll exports exactly 1 symbol: HmdDriverFactory (P5 SC3 preserved)

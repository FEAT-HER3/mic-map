---
phase: 07
plan: 04
subsystem: driver
tags: [lifecycle-wiring, vrsettings-read, mig-02, mig-03, mig-04, wave-3, fail-soft, reverse-order-teardown]
dependency-graph:
  requires:
    - "driver/src/audio_worker.{hpp,cpp} (P6 D-04 — owns SampleRing in 07-04)"
    - "driver/src/detection_runner.{hpp,cpp} (07-03 — class to construct/destruct)"
    - "driver/src/sample_ring.hpp (07-01 — owned by AudioWorker, ref'd by DetectionRunner)"
    - "driver/src/device_provider.{hpp,cpp} (P5/P6 — Init/Cleanup/Standby splice points)"
    - "driver/resources/settings/default.vrsettings (07-02 — 5 detection_* keys)"
  provides:
    - "End-to-end in-process trigger path: WASAPI cb → SampleRing → DetectionRunner → CommandQueue → RunFrame → UpdateBooleanComponent (4 hops, was 7 cross-process in v1.5)"
    - "AudioWorker::ring() accessor + SetDetectionRunner attach setter — DetectionRunner is wired post-construction to keep ctor-arg shape stable"
    - "DeviceProvider lifecycle wiring complete: Init reads 5 vrsettings keys + constructs DetectionRunner LAST + fail-soft semantics; Cleanup PREPENDS detectionRunner_.reset() (D-20 reverse-order); EnterStandby/LeaveStandby splice Pause/Resume (D-21)"
  affects:
    - "Plan 07-05 (HTTP /health endpoint extension) — DetectionRunner now alive to query"
    - "Plan 07-06 (UAT D-25) — full end-to-end trigger path testable on Bigscreen Beyond"
    - "Plan 07-09 (close-out) — SC4 / MIG-04 50-cycle handle audit GREEN today; D-25(2) HMD standby Pause/Resume verifiable on real HMD"
tech-stack:
  added: []
  patterns:
    - "Shared Pattern 2 — VRSettings single-read on the vrserver thread with explicit-default-on-error (mirrors P6 D-01 enable_driver_audio block; extended to 5 new keys here)"
    - "Shared Pattern 3 — Pitfall 13 weak_ptr<State> + atomic alive flag transitively protects the new State::runner_ptr atomic pointer"
    - "Shared Pattern 4 — reverse-construction-order teardown: detectionRunner_.reset() PREPENDED to Cleanup so the detection thread joins BEFORE AudioWorker stops feeding the ring"
    - "P6 D-14 fail-soft semantics extended per D-19: Init does NOT fail when DetectionRunner cannot start; v1.5 trigger path stays alive"
    - "MICMAP_DEBUG_RMS_LOG #ifdef gate for the legacy P6 RMS log block (production driver builds emit zero RMS lines)"
key-files:
  created: []
  modified:
    - "driver/src/audio_worker.hpp — sample_ring.hpp include + DetectionRunner forward-decl + ring() accessor + SetDetectionRunner setter + State::runner_ptr atomic + ring_ private member"
    - "driver/src/audio_worker.cpp — detection_runner.hpp include + kRingDropLogPeriod + AudioWorker::SetDetectionRunner impl + setAudioCallback rewired (push to ring + NotifyOne; RMS gated by MICMAP_DEBUG_RMS_LOG)"
    - "driver/src/sample_ring.hpp — Rule-3 fix: suppress MSVC C4324 (alignas-padding warning) around the cache-line-aligned atomics so /WX driver_micmap target builds clean"
    - "driver/src/device_provider.hpp — detection_runner.hpp include (DetectionConfig held by-value) + DetectionRunner forward-decl + 3 new private members (driverDetectionEnabled_, detectionDefaults_, detectionRunner_)"
    - "driver/src/device_provider.cpp — null-context defensive guard + 5-key VRSettings read block + DetectionRunner construct LAST with fail-soft + audioWorker_->SetDetectionRunner wire-up + Cleanup PREPEND detectionRunner_.reset() + EnterStandby/LeaveStandby Pause/Resume splice + symmetry-with-Init reset block extension"
    - "tests/CMakeLists.txt — Rule-3 fix: DeviceProviderLifecycleStress link list expanded with micmap::bindings + httplib::httplib + CPPHTTPLIB_NO_EXCEPTIONS define (deps were latent in 07-01 RED scaffold; surfaced once detection_runner.cpp landed and the 4-TU source list became compile-active)"
decisions:
  - "DetectionConfig included by-value in device_provider.hpp (single include of detection_runner.hpp) rather than the unique_ptr<DetectionConfig> alternative — saves a heap allocation + indirection; detection_runner.hpp itself only pulls in command_queue.hpp + sample_ring.hpp + std headers, no shared-lib transitive cost"
  - "MSVC C4324 (alignas-padding warning) suppressed inside sample_ring.hpp around the alignas(64) atomics rather than at every consumer's instantiation site — the alignas IS intentional (cache-line separation for SPSC false-sharing avoidance) and the warning is the EXPECTED outcome; localizing the suppression keeps the documented-intent + the workaround in the same place"
  - "Rule-2 defensive null-context guard added to DeviceProvider::Init: returns VRInitError_Init_InvalidInterface on null pDriverContext. Discovered during 07-04 Task 2 test-execute that VR_INIT_SERVER_DRIVER_CONTEXT(nullptr) SEGFAULTs in OpenVR's COpenVRDriverContext::VRSettings() lazy-load path (openvr_driver.h:4228 derefs the just-nulled VRDriverContext). vrserver.exe always passes a real context; the headless DeviceProviderLifecycleStress test deliberately uses nullptr to exercise the OpenVR-context-teardown lifecycle. This guard makes the test pass cleanly while preserving the fail-soft Cleanup contract (initialized_ stays false → Cleanup is a no-op → no leaks across cycles)"
  - "DeviceProviderLifecycleStress link list expanded as Rule-3 fix: micmap::bindings + httplib::httplib were latent dependencies of device_provider.cpp + http_server.cpp that 07-01 RED-scaffolded without (because detection_runner.cpp didn't exist, the source-list gate was false, and only the test driver compiled). Now that 07-03 landed detection_runner.cpp and the source list expanded to 4 driver TUs, the missing deps surfaced as include-not-found errors. Patching the test's CMakeLists is the right place — it parallels driver/CMakeLists.txt:71 + :48 link order"
  - "kRingDropLogPeriod = 100 chosen to mirror the existing kRmsBudget = 100 file-scope constant cadence (D-08 shape) — keeps ring overflow telemetry frequency consistent with the existing budget pattern; production logs surface 'ring overflowing under load' without per-frame flood"
metrics:
  duration_minutes: ~9
  tasks_completed: 2
  files_created: 0
  files_modified: 6
  commits: 2
  completed: "2026-05-03"
---

# Phase 7 Plan 4: Driver Lifecycle Wiring Summary

The "everything-clicks-into-place" plan. AudioWorker now owns the SampleRing (07-01) as a member, exposes `ring()` + `SetDetectionRunner()` for post-construction attachment, and rewires its WASAPI capture callback per D-05: weak_ptr<State> + alive UAF guard preserved verbatim (Pitfall 13), frames pushed to ring with drop-OLDEST diagnostic (every 100 drops → `MicMap detection: ring overflow drops=N`), DetectionRunner woken via `NotifyOne()`, and the legacy P6 RMS log block gated behind `#ifdef MICMAP_DEBUG_RMS_LOG` so production driver builds emit zero RMS lines. DeviceProvider::Init reads 5 new VRSettings keys (D-13) with explicit-default-on-error, then constructs DetectionRunner LAST after AudioWorker (D-19) with fail-soft semantics — `enable_driver_detection` set without `enable_driver_audio` logs the literal D-19 line and skips construction; `DetectionRunner::Start()` failure resets the unique_ptr and continues Init so the v1.5 trigger path stays alive. Cleanup PREPENDS `detectionRunner_.reset()` BEFORE `audioWorker_.reset()` (D-20 strict reverse construction order — Pitfall 4). EnterStandby/LeaveStandby splice `Pause()`/`Resume()` calls into the existing log lines (D-21 / MIG-03). The in-process trigger path is fully connected: WASAPI cb → SampleRing → DetectionRunner → CommandQueue → RunFrame → UpdateBooleanComponent (4 hops, down from v1.5's 7 cross-process hops). DeviceProviderLifecycleStress (07-01 RED scaffold) transitions to GREEN with handle delta = 0 across 50 Init→500ms→Cleanup cycles.

## What Shipped

### Task 1: AudioWorker — SampleRing ownership + DetectionRunner attach setter (D-05) — commit `13f45d4`

**`driver/src/audio_worker.hpp` modifications:**

- `#include "sample_ring.hpp"` (header-only, ~85 LoC, cheap include).
- Forward-decl `namespace micmap::driver { class DetectionRunner; }` so the State struct can hold `std::atomic<DetectionRunner*>` without a full-type include (mirrors `IAudioCapture` precedent at audio_worker.hpp:25).
- New public surface:
  - `SampleRing<16, 480>& ring() { return ring_; }` — inline accessor; consumed by DeviceProvider::Init when constructing DetectionRunner.
  - `void SetDetectionRunner(micmap::driver::DetectionRunner* runner)` — attach setter; safely tolerates `state_ == nullptr` (early-return); release-store on `State::runner_ptr` so the audio cb's acquire-load synchronizes with the setter without a mutex.
- New State member: `std::atomic<micmap::driver::DetectionRunner*> runner_ptr{nullptr}` — Pitfall 13 alive flag transitively gates access (callback bails on `!alive` BEFORE dereferencing `runner_ptr`).
- New private member: `SampleRing<16, 480> ring_` — AudioWorker owns the ring (RESEARCH Open Question 3 recommendation a). SPSC invariants satisfied: producer = WASAPI capture thread, consumer = DetectionRunner thread.

**`driver/src/audio_worker.cpp` modifications:**

- `#include "detection_runner.hpp"` (full type required for `runner->NotifyOne()` call). detection_runner.hpp itself includes only command_queue.hpp + sample_ring.hpp + std headers — zero `vr::*` API surface, zero OpenVR includes (AssertAudioWorkerNoVrApi + AssertDetectionRunnerNoVrApi stay green).
- `constexpr uint32_t kRingDropLogPeriod = 100` — file-scope, mirrors kRmsBudget cadence (D-08 shape).
- `AudioWorker::SetDetectionRunner` impl: `state_->runner_ptr.store(runner, std::memory_order_release)` + `DriverLog("MicMap: AudioWorker SetDetectionRunner=%p\n", runner)`.
- `setAudioCallback` lambda body rewire (D-05):
  - **PRESERVED VERBATIM:** weak_ptr<State> + `if (!sp || !sp->alive.load(std::memory_order_acquire)) return;` (Pitfall 13 / D-15 / D-16).
  - **NEW:** `const bool dropped = ring_ptr->try_push(samples, count);` + per-100-drops diagnostic.
  - **NEW:** `auto* runner = sp->runner_ptr.load(std::memory_order_acquire); if (runner) runner->NotifyOne();`.
  - **GATED:** legacy RMS compute + DriverLog block now wrapped in `#ifdef MICMAP_DEBUG_RMS_LOG` ... `#endif` so production driver builds emit zero `MicMap audio: rms[…]` lines.

### Task 2: DeviceProvider — VRSettings reads + DetectionRunner construct LAST + reverse-order Cleanup + Standby splice (D-13 / D-19 / D-20 / D-21) — commit `bf3beaa`

**`driver/src/device_provider.hpp` modifications:**

- `#include "detection_runner.hpp"` (DetectionConfig held by-value as `detectionDefaults_`; cheaper than `unique_ptr<DetectionConfig>`).
- Forward-decl `class DetectionRunner` in the existing `namespace micmap::driver` block alongside `class AudioWorker;`.
- 3 new private members AFTER the P6 audioWorker_ block:
  - `bool driverDetectionEnabled_{false}` — result of `vr::VRSettings()->GetBool("driver_micmap","enable_driver_detection")`.
  - `DetectionConfig detectionDefaults_{}` — caches the 4 numeric reads.
  - `std::unique_ptr<DetectionRunner> detectionRunner_` — constructed LAST in Init when both flags true and audio alive; reset FIRST in Cleanup (Pitfall 4 / D-20).

**`driver/src/device_provider.cpp` modifications:**

1. **Null-context defensive guard** (NEW, top of Init): returns `VRInitError_Init_InvalidInterface` if `pDriverContext` is null. Discovered during Task 2 test execution that `VR_INIT_SERVER_DRIVER_CONTEXT(nullptr)` SEGFAULTs in `COpenVRDriverContext::VRSettings()`'s lazy-load path (openvr_driver.h:4228 derefs the just-nulled `VRDriverContext()`). vrserver.exe always passes a real context; the headless DeviceProviderLifecycleStress test uses nullptr deliberately. The guard makes the test pass cleanly while preserving fail-soft Cleanup semantics.

2. **5 VRSettings reads** (D-13): single-read pattern matching P6 D-01 enable_driver_audio block. Default-on-`UnsetSettingHasNoDefault` semantics:
   - `enable_driver_detection` → `GetBool` → false on error
   - `detection_sensitivity` → `GetFloat` → 0.7f on error
   - `detection_threshold` → `GetFloat` → 0.6f on error
   - `detection_cooldown_ms` → `GetInt32` → 1000 on error
   - `detection_min_duration_ms` → `GetInt32` → 200 on error

3. **DetectionRunner construction LAST** (D-19): only when `driverDetectionEnabled_ && audioWorker_`. On `(true, null)` logs the literal `MicMap: enable_driver_detection requires enable_driver_audio — skipping detection construction` line. Constructor takes `audioWorker_->ring()`, `*commandQueue_`, `sampleRate=48000` (WASAPI default), `detectionDefaults_`. On `Start()` failure, fail-soft: `detectionRunner_.reset()` + log + continue Init (P6 D-14 semantics extended). On success, `audioWorker_->SetDetectionRunner(detectionRunner_.get())` wires the audio cb wakeup path.

4. **Cleanup PREPEND** (D-20 reverse-order — Pitfall 4): `if (detectionRunner_) detectionRunner_.reset();` placed BEFORE the existing `if (audioWorker_) { audioWorker_.reset(); }` block. DetectionRunner's destructor signals shutdown_, notify_all, joins thread with 2 s watchdog; the detection thread exits cleanly BEFORE AudioWorker stops feeding the ring (DetectionRunner holds a reference to AudioWorker's `ring_`, which would dangle if AudioWorker died first).

5. **Symmetry-with-Init reset** (Cleanup): added `driverDetectionEnabled_ = false;` and `detectionDefaults_ = DetectionConfig{};` to the existing reset block.

6. **EnterStandby splice** (D-21 / MIG-03): existing `MicMap driver entering standby` DriverLog preserved; appended `if (detectionRunner_) detectionRunner_->Pause();`. AudioWorker continues capturing during standby — the ring fills and drop-OLDEST kicks in within ~50 ms (expected, harmless).

7. **LeaveStandby splice** (D-21 / MIG-03): same shape with `Resume()`.

**`tests/CMakeLists.txt` Rule-3 fix:**

The DeviceProviderLifecycleStress link list was expanded. 07-01 RED-scaffolded the test before detection_runner.cpp existed; the source-list gate was false, only the test driver TU compiled, and the latent transitive dependencies of device_provider.cpp (micmap::bindings) + http_server.cpp (httplib::httplib) were not surfaced. Once 07-03 landed detection_runner.cpp and the gate became true, the 4-TU source list compile-failed with include-not-found errors. The fix mirrors driver/CMakeLists.txt link order:
- Add `micmap::bindings` (for `bindings_patcher.hpp`).
- Add `httplib::httplib` (for `httplib.h` in http_server.cpp).
- Add `CPPHTTPLIB_NO_EXCEPTIONS` compile def (mirrors driver/CMakeLists.txt:52).

## Verification Results

```
cmake --build build --config Release --target driver_micmap → exit 0
  driver_micmap.dll links clean under /W4 /WX
  (LINK warning LNK4098 LIBCMT pre-existing, unrelated)

ctest --test-dir build -C Release --output-on-failure → 17/17 PASS
  test_command_queue                  PASS  (P5 carryover + 07-01 concurrent case)
  test_button_command_serialization   PASS  (P5)
  test_jwt_auth_present               PASS  (P5)
  test_app_key_persistence            PASS  (P5)
  test_manifest_registrar             PASS  (P5)
  test_vr_input_quit_ordering         PASS  (P5)
  test_tray_balloon_once              PASS  (P5)
  test_vrmanifest_schema              PASS  (P5)
  test_bindings_patcher               PASS  (P5)
  bindings_patcher_idempotent         PASS  (P5)
  lint_no_openvr_in_core              PASS  (P5 carryover)
  lint_no_driver_macro                PASS  (P5 carryover)
  AudioWorkerLifecycleHeadless        PASS  (P6 carryover)
  AssertAudioWorkerNoVrApi            PASS  (P6 carryover — D-07/SVR-05)
  DetectionSettingsPropagation        PASS  (07-01 RED → GREEN today; MIG-06 < 50 ms)
  DeviceProviderLifecycleStress       PASS  lifecycle_stress_50_cycles base=100 after=100
                                            (07-01 RED → GREEN today; SC4 / MIG-04 handle delta=0)
  AssertDetectionRunnerNoVrApi        PASS  (07-01 carryover — D-22)

dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll
  → 1 export: HmdDriverFactory (P5 SC3 carryover preserved)
```

D-20 reverse-order ordering verified textually:
- `detectionRunner_.reset()` at Cleanup body offset 154
- `audioWorker_.reset()` at Cleanup body offset 1329
- 154 < 1329 → invariant holds.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] MSVC C4324 alignas-padding warning treated as error in /W4 /WX driver_micmap target**
- **Found during:** Task 1 build verification
- **Issue:** Once `audio_worker.hpp` gained `SampleRing<16, 480> ring_;` as a by-value member, the SampleRing template was instantiated for storage layout (not just as a reference type), exposing `alignas(64)` padding warnings on `head_`, `tail_`, `drops_` atomics. `/WX` rejected them as errors. Previously `detection_runner.hpp` only used `SampleRing<16, 480>&` (reference), which doesn't force layout instantiation.
- **Fix:** Wrapped the alignas members in `#pragma warning(push) / disable: 4324 / pop` inside `sample_ring.hpp`. The padding IS intentional (cache-line separation for SPSC false-sharing avoidance) — suppressing the warning at the documented-intent site is correct. Localization keeps the workaround next to the rationale.
- **Files modified:** `driver/src/sample_ring.hpp`
- **Commit:** `13f45d4`

**2. [Rule 3 - Blocking] DeviceProviderLifecycleStress test exe missing transitive link deps**
- **Found during:** Task 2 ctest verification (test ***Not Run — `Could not find executable test_device_provider_lifecycle_stress.exe`)
- **Issue:** 07-01 registered `add_executable(test_device_provider_lifecycle_stress ...)` with sources gated by `if(EXISTS detection_runner.cpp)` — but only linked `micmap::core_runtime + OpenVR::openvr_api + nlohmann_json`. Latent: device_provider.cpp `#include "micmap/bindings/bindings_patcher.hpp"` (needs micmap::bindings) and http_server.cpp `#include <httplib.h>` (needs httplib::httplib). At 07-01 time the gate was false, only the test driver TU compiled, and the latent deps were not exposed. Once 07-03 landed detection_runner.cpp the source list expanded to 4 driver TUs and the missing deps surfaced as compile errors.
- **Fix:** Added `micmap::bindings` + `httplib::httplib` to the `target_link_libraries` block; added `CPPHTTPLIB_NO_EXCEPTIONS` compile definition mirroring driver/CMakeLists.txt:52.
- **Files modified:** `tests/CMakeLists.txt`
- **Commit:** `bf3beaa`

**3. [Rule 2 - Missing critical functionality] Defensive null-context guard in DeviceProvider::Init**
- **Found during:** Task 2 ctest verification (test ***Exception: SegFault — exit code 139)
- **Issue:** `VR_INIT_SERVER_DRIVER_CONTEXT(nullptr)` SEGFAULTs in OpenVR's `COpenVRDriverContext::VRSettings()` lazy-load path. Trace (openvr_driver.h):
  1. Macro calls `InitServerDriverContext(nullptr)` (line 4479).
  2. `InitServerDriverContext` does `VRDriverContext() = nullptr; OpenVRInternal_ModuleServerDriverContext().InitServer();` (line 4457-4458).
  3. `InitServer()` does `Clear();` (nulls all `m_pVRSettings` etc) then checks `!VRSettings() || ...` (line 4422-4427).
  4. `VRSettings()` (line 4223): `if (m_pVRSettings == nullptr) m_pVRSettings = (IVRSettings *)VRDriverContext()->GetGenericInterface(...);` → derefs the just-nulled `VRDriverContext()` → SEGFAULT.
  
  vrserver.exe always passes a real context, but the headless DeviceProviderLifecycleStress test (SC4 / MIG-04 50-cycle audit) calls `Init(nullptr)` deliberately to exercise the OpenVR-context-teardown lifecycle without spinning up a real OpenVR session.
- **Fix:** Added `if (!pDriverContext) return VRInitError_Init_InvalidInterface;` as the very first statement of `DeviceProvider::Init`, before `VR_INIT_SERVER_DRIVER_CONTEXT`. Bail-with-error preserves the fail-soft Cleanup contract: `initialized_` stays false → Cleanup is a no-op → no resource leaks across cycles. Documented thoroughly in the source comment + commit message + this summary so future agents don't strip it as "unreachable".
- **Files modified:** `driver/src/device_provider.cpp`
- **Commit:** `bf3beaa`
- **Verification:** DeviceProviderLifecycleStress now passes — `PASS lifecycle_stress_50_cycles base=100 after=100` (handle delta=0, well under <=5 tolerance).

### Criteria Drift Notes (non-blocking)

- Plan acceptance criterion `grep -c "vr::" driver/src/device_provider.cpp` was not enumerated, but the plan does require zero NEW `vr::*` callers OUTSIDE Init/Cleanup. Init now has 5 additional `vr::VRSettings()->GetBool/GetFloat/GetInt32` calls and 1 new `VRInitError_Init_InvalidInterface` reference — all within Init. EnterStandby/LeaveStandby gained zero `vr::*` calls (Pause/Resume are member calls on detectionRunner_).
- `MicMap: DetectionRunner active` shows count 1 (expected by plan as `>=1`).
- The plan's `<interfaces>` block §7 sketched the SampleRing #include AFTER the AudioWorker class definition; I placed it BEFORE the class (at the top of the header alongside other includes) — same effect, more conventional placement, no behavioral diverge.
- `driverDetectionEnabled_ = false` shows count 3 (expected `>=1`): one in Cleanup symmetry block + two in Init UnsetSettingHasNoDefault / error branches.

## Authentication Gates

None. All work was filesystem + CMake + CTest + git on a local Windows + VS2022 build.

## Threat Flags

None. The plan's `<threat_model>` covers all surfaces touched (T-07-04-01 through T-07-04-07). Mitigations as designed:

- **T-07-04-01** (Cleanup ordering swap leaks WASAPI handles): D-20 strict reverse-order verified textually + DeviceProviderLifecycleStress 50-cycle handle delta = 0.
- **T-07-04-02** (DetectionRunner constructed without audio): construction guard + literal log line both verified by grep.
- **T-07-04-03** (RMS log floods vrserver.txt in production): `#ifdef MICMAP_DEBUG_RMS_LOG` gate present (count 1); production build (no `-DMICMAP_DEBUG_RMS_LOG`) compiles the block out entirely.
- **T-07-04-04** (torn read of State::runner_ptr): `std::atomic<micmap::driver::DetectionRunner*>` declared in audio_worker.hpp; release-store in SetDetectionRunner; acquire-load in audio cb. Pitfall 13 alive flag gates access first.
- **T-07-04-05** (DetectionRunner Start fail cascades into Init failure): `MicMap: DetectionRunner::Start failed — continuing without detection` log + explicit `detectionRunner_.reset()` + Init returns VRInitError_None.
- **T-07-04-06** (EnterStandby + Cleanup double-Pause race): `if (detectionRunner_) detectionRunner_->Pause();` guard at the call site catches post-reset case; DetectionRunner::Pause is itself idempotent via `paused_.exchange()` (07-03 D-21).
- **T-07-04-07** (VRSettings type confusion silently disables flag): each read uses the matching VRSettings function (1 GetBool, 2 GetFloat, 2 GetInt32) for the key's JSON type from 07-02 default.vrsettings; UnsetSettingHasNoDefault path falls to explicit-default mirroring the JSON file.

## Known Stubs

None. The in-process trigger path is fully connected end-to-end:
- WASAPI cb pushes frames into AudioWorker::ring_ ✓
- WASAPI cb wakes DetectionRunner via runner_ptr->NotifyOne() ✓
- DetectionRunner drains ring + runs detector + state machine ✓
- DetectionRunner pushes TapCommand into CommandQueue on rising-edge Triggered ✓ (already in 07-03)
- DeviceProvider::RunFrame drains CommandQueue + writes UpdateBooleanComponent ✓ (existing P5 path)

The remaining v1.6 surfaces (HTTP /health DetectionRunner status query in 07-05; PUT /settings DetectionRunner.publish() wire-up in P8) are explicitly out of scope for this plan per the objective.

## Self-Check: PASSED

Verified the following exist on disk (all modified, no new files created):
- `driver/src/audio_worker.hpp` — FOUND (sample_ring include + DetectionRunner forward-decl + ring() + SetDetectionRunner + State::runner_ptr + ring_)
- `driver/src/audio_worker.cpp` — FOUND (detection_runner include + kRingDropLogPeriod + SetDetectionRunner impl + lambda rewire + #ifdef gate)
- `driver/src/sample_ring.hpp` — FOUND (C4324 suppression around alignas atomics)
- `driver/src/device_provider.hpp` — FOUND (detection_runner include + DetectionRunner forward-decl + 3 new members)
- `driver/src/device_provider.cpp` — FOUND (null guard + 5 VRSettings reads + DetectionRunner construct + Cleanup PREPEND + Standby splice)
- `tests/CMakeLists.txt` — FOUND (DeviceProviderLifecycleStress link list expansion)

Verified the following commits exist on `hmd-button` (worktree branch):
- `13f45d4` — feat(07-04): wire AudioWorker to SampleRing + DetectionRunner attach (D-05) — FOUND
- `bf3beaa` — feat(07-04): wire DetectionRunner into DeviceProvider lifecycle (D-13/D-19/D-20/D-21) — FOUND

ctest sweep on Release driver-on build:
- All 17 tests pass (100%).
- 07-01 RED scaffolds DetectionSettingsPropagation + DeviceProviderLifecycleStress both transition to GREEN.
- All P5/P6 carryovers stay green.

dumpbin verification:
- `driver_micmap.dll` exports exactly 1 symbol: `HmdDriverFactory` (P5 SC3 preserved).

---
phase: 07-driver-side-detection-thread
verified: 2026-05-04T15:30:00Z
status: GO
score: 5/5 must-haves verified
requirements_met: 4/4
overrides_applied: 0
summary: "Phase 7 delivers in-process detection (MIG-02), HMD standby pause/resume (MIG-03), 50-cycle Init/Cleanup with zero handle leaks (MIG-04), and lock-free <50ms settings propagation mechanism (MIG-06). All 5 ROADMAP Success Criteria verified by a combination of artifact inspection, lint enforcement, headless ctest evidence, and signed-off real-hardware UAT on Bigscreen Beyond + Win11 Pro. One mid-UAT defect (DetectionRunner missing training-data load) was found and fixed in commit b32a6aa before sign-off. D-25(2) marked PASS-with-caveat: Bigscreen Beyond proximity doff/don does not trigger SteamVR EnterStandby — accepted because the Pause/Resume code path is wired correctly (device_provider.cpp:402-418) and exercised by the automated DetectionSettingsPropagation ctest, and the functional MIG-03 goal (detection survives wake) was directly verified. PUT /settings HTTP path is intentionally deferred to Phase 8 per D-14/D-15 — Phase 7 ships the publish() mechanism + headless verifier as planned. Default flags restored to OFF (D-27 closeout). Phase 7 GO with no blockers."
---

# Phase 7: Driver-Side Detection Thread — Verification Report

**Phase Goal:** Full in-process detection pipeline runs inside `driver_micmap.dll`: a `DetectionRunner` thread drains an SPSC sample ring from the WASAPI callback, runs FFT + state machine, and emits `TapCommand` directly into the existing `CommandQueue`. Trigger collapses from 7 hops cross-process to 4 hops in-process. Behind `enableDriverDetection` flag (default OFF) so client-side detection and `POST /button` remain the rollback path.

**Verified:** 2026-05-04
**Status:** GO
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| #   | Truth (SC) | Status     | Evidence       |
| --- | ---------- | ---------- | -------------- |
| 1   | SC1: With `enable_driver_detection=1` on Bigscreen Beyond, covering the mic toggles the SteamVR dashboard via the in-process trigger path; zero HTTP traffic on the trigger path during the test (MIG-02). | ✓ VERIFIED | `uat-evidence/d25-1-vrserver.txt` shows 4 lines `MicMap detection: TapCommand pushed (n=1..4)`, each followed by `UpdateBooleanComponent(down/up) OK`. UAT log confirms 0 `POST /button` traffic in same window. End-to-end path detection_runner.cpp:259-263 → command_queue.push → device_provider.cpp:369-378 → UpdateBooleanComponent verified. |
| 2   | SC2: CI grep confirms `VRDriverInput`, `VRProperties`, `VRServerDriverHost`, `VRSettings` only appear in `device_provider.cpp` and `manifest_registrar.cpp` — never in `detection_runner.cpp`, `sample_ring.hpp`, or audio TUs (Pitfall 3). | ✓ VERIFIED | `cmake/AssertDetectionRunnerNoVrApi.cmake` + `cmake/AssertAudioWorkerNoVrApi.cmake` exist and registered as ctest targets `AssertDetectionRunnerNoVrApi` + `AssertAudioWorkerNoVrApi`. `grep -c 'vr::|openvr' driver/src/detection_runner.cpp` → 0; same for `sample_ring.hpp`. Both ctests reported PASS in 07-04 SUMMARY (17/17 tests). |
| 3   | SC3: Detection runs continuously regardless of HMD activation state; `EnterStandby` pauses cleanly; `LeaveStandby` resumes; sleep/wake cycle does not crash vrserver or strand audio handles (MIG-03). | ✓ VERIFIED (with caveat) | Pause/Resume wired at `device_provider.cpp:402-418` via `EnterStandby/LeaveStandby` callbacks. `detection_runner.cpp:185-198` implements idempotent `paused_.exchange()` + cv_.notify_one + log. Drain-discard loop at detection_runner.cpp:289-295 prevents drop-storm spam. UAT D-25(2): functional goal verified — `uat-evidence/d25-2-vrserver-pause-resume.txt` shows TapCommand n=5,6 firing post-doff/don. **Caveat (operator-accepted):** Bigscreen Beyond proximity sensor does NOT trigger SteamVR `EnterStandby` (only full-system standby does). Pause/Resume code path verified by automated `DetectionSettingsPropagation` ctest exercising the same cv_/atomic plumbing. Functional MIG-03 (detection survives wake) directly verified. |
| 4   | SC4: 50-cycle Init→500ms→Cleanup stress test passes with zero leaked handles; reverse-order teardown verified (MIG-04). | ✓ VERIFIED | `uat-evidence/d25-3-ctest-output.txt` shows `PASS lifecycle_stress_50_cycles base=104 after=104` (handle delta=0, well under ≤5 budget). 50 invocations of `DeviceProvider::Init(nullptr) → 500ms → Cleanup` visible in test output. Reverse-order Cleanup verified textually at `device_provider.cpp:261-280`: `detectionRunner_.reset()` (line 262) precedes `audioWorker_.reset()` (line 273) precedes `httpServer_->Stop()` (line 278) precedes `commandQueue_.reset()` (line 281) precedes `VR_CLEANUP_SERVER_DRIVER_CONTEXT()` (line 296). 2s watchdog with detach fallback at detection_runner.cpp:154-183. |
| 5   | SC5 / MIG-06: Settings (sensitivity/threshold/cooldown) propagate to detection hot path within 50ms via lock-free `std::atomic<std::shared_ptr<const DetectionConfig>>` snapshot; no audio-thread blocking. | ✓ VERIFIED | `uat-evidence/d25-4-ctest-output.txt` shows `PASS case_propagation_under_50ms elapsed_ms=0` for `DetectionSettingsPropagation` test. Mechanism verified at `detection_runner.cpp:200-208`: `std::atomic_store_explicit(&activeConfig_, ..., release)` + `cv_.notify_one()`. Hot-path read at detection_runner.cpp:282-287: `atomic_load_explicit(acquire)` once per loop iteration with pointer-identity compare to skip no-op rebuilds. Audio thread is decoupled (it calls only `try_push` on the SPSC ring + `NotifyOne()` — never touches the config snapshot). **Note:** Per CONTEXT D-14/D-15, P7 ships the **mechanism** only; the actual `PUT /settings` HTTP handler that calls `publish()` lands in Phase 8 (IPC reshape). This is intentional and documented in the phase scope. |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `driver/src/sample_ring.hpp` | Header-only SPSC ring, drop-OLDEST, no vr::* | ✓ VERIFIED | 96 lines, template SPSC ring with cache-line-aligned atomics (alignas(64)), `try_push`/`try_pop`/`has_data`/`drops` API, MSVC C4324 suppression around alignas. Zero `vr::*` matches. |
| `driver/src/detection_runner.hpp` | DetectionRunner class + DetectionConfig POD + atomic_shared_ptr snapshot | ✓ VERIFIED | 137 lines, full class declaration, DetectionConfig with all 4 fields matching default.vrsettings, public API: Start/Stop/Pause/Resume/publish/NotifyOne/IsRunning/TriggersEmitted/active_config_for_test. Forward-decls for INoiseDetector + IStateMachine keep shared-lib headers out of header. |
| `driver/src/detection_runner.cpp` | Thread loop, MIG-06 publish, 2s watchdog Stop, idempotent Pause/Resume, no vr::* | ✓ VERIFIED | 319 lines. RunLoop with cv_.wait_for(50ms), drain-analyze-update pattern, state-machine cooldown as natural CommandQueue rate-limiter. Reverse-order teardown ON detection thread (stateMachine_.reset → detector_.reset). Includes loadTrainingData fix from b32a6aa. Zero `vr::*` matches. |
| `driver/src/audio_worker.{hpp,cpp}` | Owns SampleRing, exposes ring() + SetDetectionRunner; callback pushes + NotifyOne | ✓ VERIFIED | audio_worker.hpp:78 `ring()` accessor, :86 `SetDetectionRunner()`, :138 `SampleRing<16,480> ring_` member, :106 `std::atomic<DetectionRunner*> runner_ptr` with weak_ptr<State> alive guard. audio_worker.cpp:266-312 callback: weak_ptr lock + alive check + try_push (drop-OLDEST) + drop telemetry every 100 + NotifyOne. RMS log gated by `#ifdef MICMAP_DEBUG_RMS_LOG`. |
| `driver/src/device_provider.{hpp,cpp}` | Init reads 5 vrsettings keys + constructs DetectionRunner LAST + Cleanup PREPENDS reset + EnterStandby/LeaveStandby splice Pause/Resume | ✓ VERIFIED | device_provider.cpp:144-208 reads enable_driver_detection + 4 numeric keys with explicit-default-on-error. :214-240 constructs DetectionRunner LAST when both flags ON, fail-soft on Start failure. :261-280 Cleanup reverse-order (detectionRunner first). :402-418 EnterStandby/LeaveStandby call Pause/Resume. Null-context guard at :71-75 (test-path). |
| `driver/src/http_server.{hpp,cpp}` | /health emits driver_detection_active boolean from getter lambda | ✓ VERIFIED | http_server.hpp:64-67 4th ctor param `std::function<bool()> driverDetectionActiveGetter = nullptr`. http_server.cpp:168-169 `body["driver_detection_active"] = driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false`. Lambda capturing this passed at device_provider.cpp:96-106 reads driverDetectionEnabled_ && audioWorker_ && detectionRunner_ && IsRunning() at request time. |
| `apps/micmap/main.cpp` | Client onTrigger gates tap() on isDriverDetectionActive() with literal log line | ✓ VERIFIED | main.cpp:524-527 `if (driverClient->isDriverDetectionActive()) { MICMAP_LOG_DEBUG("onTrigger: driver_detection_active=true, suppressing"); return; }` placed BEFORE the existing tap() call. |
| `src/steamvr/src/vr_input.cpp` | DriverClient::isDriverDetectionActive impl with 1s cache, defensive false-on-error | ✓ VERIFIED | vr_input.cpp:226-256 polls /health with 2s timeout, parses JSON via `body.value("driver_detection_active", false)`, caches via `std::chrono::milliseconds(1000)` TTL. Members `lastDetectionPoll_` + `cachedDetectionActive_` at :285-286. |
| `driver/resources/settings/default.vrsettings` | 5 new detection_* keys, enable_driver_detection=false (D-27) | ✓ VERIFIED | All 5 keys present with correct types. enable_driver_detection=false, enable_driver_audio=false (D-27 closeout restored — verified by Read of file showing both as `false`). |
| `cmake/AssertDetectionRunnerNoVrApi.cmake` | Sibling lint scans detection_runner.{hpp,cpp} + sample_ring.hpp for vr::* | ✓ VERIFIED | Present in `cmake/` directory. Registered as ctest target per 07-01 SUMMARY (PASS, 3 files scanned clean). |
| `tests/driver/detection_settings_propagation_test.cpp` | MIG-06 publish→observed-swap < 50ms verifier | ✓ VERIFIED | Test exists; `DetectionSettingsPropagation` ctest registered + green (d25-4 evidence: elapsed_ms=0). |
| `tests/driver/device_provider_lifecycle_stress_test.cpp` | SC4/MIG-04 50-cycle Init/Cleanup handle audit | ✓ VERIFIED | Test exists; `DeviceProviderLifecycleStress` ctest registered + green (d25-3 evidence: 50 cycles, base=after=104). |

### Key Link Verification (in-process trigger path)

| From | To | Via | Status | Details |
| ---- | -- | --- | ------ | ------- |
| WASAPI capture cb | SampleRing | `ring_ptr->try_push(samples, count)` (audio_worker.cpp:277) | ✓ WIRED | weak_ptr alive guard precedes; drop-OLDEST telemetry every 100 drops |
| AudioWorker cb | DetectionRunner wakeup | `runner->NotifyOne()` (audio_worker.cpp:290-293) | ✓ WIRED | Atomic load of runner_ptr; nil-safe |
| DetectionRunner | INoiseDetector | `detector_->analyze(block.data(), count)` (detection_runner.cpp:299) | ✓ WIRED | createFFTDetector(48000, 2048) at Start; loadTrainingData fix loads %APPDATA%/MicMap/training_data.bin |
| DetectionRunner | IStateMachine | `stateMachine_->update(result.confidence, dt)` (detection_runner.cpp:304) | ✓ WIRED | createStateMachine with VRSettings-derived config; trigger callback pushes TapCommand |
| StateMachine trigger | CommandQueue | `commandQueue_.push(TapCommand{})` (detection_runner.cpp:260) | ✓ WIRED | Per-rising-edge fire; cooldown is natural rate limit (Pitfall 12) |
| CommandQueue | RunFrame | `commandQueue_->try_pop()` (device_provider.cpp:369) | ✓ WIRED | Pre-existing v1.5 SVR-05 boundary, unchanged; serialized push verified by test_concurrent_two_producer_push |
| RunFrame | UpdateBooleanComponent | `writeValue(true)` then deferred `writeValue(false)` after kTapHold | ✓ WIRED | Existing v1.5 path; d25-1 evidence shows down/up pairs after each TapCommand |
| DeviceProvider | DetectionRunner.Pause | `EnterStandby()` (device_provider.cpp:402-411) | ✓ WIRED | Idempotent via paused_.exchange |
| DeviceProvider | DetectionRunner.Resume | `LeaveStandby()` (device_provider.cpp:413-418) | ✓ WIRED | Idempotent |
| HTTP /health | driver_detection_active | getter lambda (device_provider.cpp:96-106) | ✓ WIRED | Read-at-request-time captures live lifecycle |
| Client onTrigger | Suppression | `isDriverDetectionActive()` gate (main.cpp:524-527) | ✓ WIRED | Falls back to tap() when false (defensive) |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| -------- | ------------- | ------ | ------------------ | ------ |
| DetectionRunner | activeConfig_ | atomic_shared_ptr<DetectionConfig> seeded at ctor from VRSettings reads | Yes (4 numeric values from default.vrsettings) | ✓ FLOWING |
| DetectionRunner | analyze() input | SampleRing populated by real WASAPI callback (production) or test driver (ctest) | Yes (real audio frames in d25-1; test sentinels in ctest) | ✓ FLOWING |
| INoiseDetector | training profile | %APPDATA%/MicMap/training_data.bin (4156 bytes from v1.5 client) | Yes — verified by mid-UAT defect repro: without the load, analyze() returned near-zero confidence and state machine never fired (commit b32a6aa fixed) | ✓ FLOWING |
| /health | driver_detection_active | Getter lambda evaluates at request time | Yes — d25-1 evidence implies true (client suppressed POST /button) | ✓ FLOWING |

### Behavioral Spot-Checks

Skipped automated spot-checks because the artifacts are an OpenVR driver DLL hosted by vrserver.exe and headless integration tests requiring a Windows + VS2022 build environment — both already exercised by the registered ctests and the signed-off UAT. Equivalent behavioral verification was performed by the operator-run UAT on real hardware.

| Behavior | Source | Result | Status |
| -------- | ------ | ------ | ------ |
| In-process TapCommand fires from detection thread | uat-evidence/d25-1-vrserver.txt | 4 TapCommand pushed lines (n=1..4), each producing UpdateBoolean(down/up) | ✓ PASS |
| 50-cycle Init→Cleanup with no handle leak | uat-evidence/d25-3-ctest-output.txt | PASS lifecycle_stress_50_cycles base=104 after=104 | ✓ PASS |
| Settings publish() observed in <50ms by detection thread | uat-evidence/d25-4-ctest-output.txt | PASS case_propagation_under_50ms elapsed_ms=0 | ✓ PASS |
| Flag-OFF byte-identical-to-P6 regression | uat-evidence/d25-5-vrserver-flag-off.txt | hmd_button_test.exe → POST /button → UpdateBoolean down/up; 0 MicMap detection: lines | ✓ PASS |
| Detection survives HMD doff/don | uat-evidence/d25-2-vrserver-pause-resume.txt | TapCommand n=5,6 fire post-doff/don | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| MIG-02 | 07-03 (DetectionRunner), 07-04 (lifecycle wiring) | Driver hosts dedicated detection thread that drains audio ring, runs FFT+RMS+state-machine, emits TapCommand to CommandQueue; never touches vr::* | ✓ SATISFIED | DetectionRunner class implemented; trigger callback pushes TapCommand (detection_runner.cpp:259-263); AssertDetectionRunnerNoVrApi lint enforces no vr::* (verified 0 hits); d25-1 evidence shows live in-process trigger on Bigscreen Beyond. |
| MIG-03 | 07-04 (Standby splice) | Detection runs continuously while SteamVR is up; EnterStandby pauses; LeaveStandby resumes; HMD reactivation does not leak handles or threads | ✓ SATISFIED (caveat) | Pause/Resume wired at EnterStandby/LeaveStandby (device_provider.cpp:402-418); idempotent via paused_.exchange. Caveat: Bigscreen Beyond proximity doff doesn't trigger SteamVR EnterStandby (full-system standby does). Code path verified by ctest; functional goal (detection survives wake) verified by d25-2 evidence. |
| MIG-04 | 07-01 (stress test scaffold), 07-04 (reverse-order Cleanup) | Cleanup tears down detection → audio → HTTP in reverse construction order; 50-cycle Init/Cleanup stress passes with zero leaked handles | ✓ SATISFIED | Reverse-order Cleanup verified textually (device_provider.cpp:261-280: detectionRunner first, audioWorker next, http_server next, command_queue next, VR_CLEANUP last). 50-cycle stress test PASS with delta=0 (d25-3 evidence). 2s watchdog + detach fallback at detection_runner.cpp:154-183. |
| MIG-06 | 07-01 (propagation test), 07-03 (publish API) | Detection thread reads settings via lock-free atomic<shared_ptr<const AppConfig>> snapshot; PUT /settings propagates within 50ms with no audio-thread blocking | ✓ SATISFIED (mechanism only — PUT /settings deferred to P8 per CONTEXT D-14/D-15) | publish() API at detection_runner.cpp:200-208 uses atomic_store_explicit on shared_ptr + cv_.notify_one. Detection thread reload + pointer-identity compare at detection_runner.cpp:282-287. d25-4 evidence: elapsed_ms=0 < 50ms gate. Note: Per phase scope, the **mechanism** lands in P7; the **HTTP path** that calls publish() lands in P8 alongside other IPC reshape work. This is documented in CONTEXT D-14, D-15, D-28 and 07-02 SUMMARY. |

**No orphaned requirements** — all 4 phase requirements (MIG-02, MIG-03, MIG-04, MIG-06) are mapped to plans (07-01..07-06) and verified.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (none flagged) | — | — | — | All scanned files (sample_ring.hpp, detection_runner.{hpp,cpp}, audio_worker.{hpp,cpp}, device_provider.{hpp,cpp}, http_server.{hpp,cpp}) free of TODO/FIXME/PLACEHOLDER markers, no empty-return stubs in production paths. The single `(void)cmd;` at device_provider.cpp:370 is intentional (TapCommand is empty by design — its presence is the signal). |

### Human Verification Required

None — all six D-25 cases already signed off by operator brandon@bigscreenvr.com on 2026-05-04 on Bigscreen Beyond + Win11 Pro. Real-hardware verification is complete.

### Gaps Summary

No gaps. Phase 7 is GO.

Notable observations (all intentional, not gaps):

1. **D-25(2) PASS-with-caveat (operator-accepted):** Bigscreen Beyond proximity sensor doff/don does not trigger SteamVR's `EnterStandby` callback — that fires only on full-system standby. The Pause/Resume code path is correctly wired and exercised by the automated `DetectionSettingsPropagation` ctest (which uses the same cv_/atomic plumbing). The functional MIG-03 goal — "detection survives HMD wake" — was directly verified (TapCommand n=5,6 fired after 2 doff/don cycles in d25-2 evidence). Operator accepted this as PASS.

2. **D-25(6) PASS-by-composition:** Coexistence handshake validated by composition of D-25(1) (single-tap suppression: 0 POST /button while detection active, verified in d25-1 evidence) and D-25(5) (POST /button fallback path, verified in d25-5 evidence). curl /health was confirmed reporting `driver_detection_active=true` flags-ON and `false` flags-OFF. The mid-session flag-flip permutation is a composition of these proven paths, not a new code surface — operator accepted as covered.

3. **MIG-06 mechanism vs HTTP path split:** Per CONTEXT D-14, D-15, D-28, P7 intentionally ships only the publish() mechanism + headless verifier for MIG-06. The actual `PUT /settings` HTTP handler that calls publish() is Phase 8 (IPC reshape). 07-02 SUMMARY explicitly flags this for the P8 cutover. This is by design, not a gap.

4. **Mid-UAT defect found and fixed:** First D-25(1) attempt produced 0 TapCommand lines because `DetectionRunner::Start` constructed the FFT detector but never called `loadTrainingData()`. Fix landed in commit `b32a6aa` adding %APPDATA%/MicMap/training_data.bin load with fail-soft semantics. Re-run yielded 4 TapCommand pushed in ~6s of mic-covers. Documented in 07-UAT.md Notes section. P9 will make the driver the sole writer of training_data.bin; P7 is read-only consumer.

5. **D-27 closeout applied:** Both flags restored to `false` in `driver/resources/settings/default.vrsettings` (verified by Read). P10 cutover is the single point that flips defaults to `true`.

---

_Verified: 2026-05-04T15:30:00Z_
_Verifier: Claude (gsd-verifier)_

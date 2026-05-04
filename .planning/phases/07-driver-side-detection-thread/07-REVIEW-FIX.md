---
phase: 07-driver-side-detection-thread
fixed_at: 2026-05-04T00:00:00Z
review_path: .planning/phases/07-driver-side-detection-thread/07-REVIEW.md
iteration: 1
findings_in_scope: 10
fixed: 8
skipped: 2
status: partial
---

# Phase 7: Code Review Fix Report

**Fixed at:** 2026-05-04
**Source review:** `.planning/phases/07-driver-side-detection-thread/07-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 10 (4 warnings + 6 info)
- Fixed: 8
- Skipped: 2 (both deferred with justification — no SKIP_CANNOT)

**Build verification:** `cmake --build build-driver --config Debug` succeeds with no errors. All 17 tests pass via `ctest -C Debug --output-on-failure`, including the three Phase 7 driver tests:
- `DetectionSettingsPropagation` — passed
- `DeviceProviderLifecycleStress` — passed (25.47 s, 50-cycle handle-leak audit)
- `AssertDetectionRunnerNoVrApi` — passed

## Fixed Issues

### WR-01: Detached detection thread holds dangling reference to AudioWorker's ring

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 0257a08
**Applied fix:** Replaced `thread_.detach()` on watchdog overrun with `thread_.join()`. The detection thread's only blocking surface is `cv_.wait_for(50 ms)` after `shutdown_` is set + notified, so the worst additional wait is ~one timeout period. A 2 s overrun signals something is genuinely wrong; preferring a diagnosable hang over silent UAF on AudioWorker's ring + on `this` (commandQueue_, triggers_, cv_, mu_) once `~DetectionRunner` returns. Added a "WARNING - did not exit within 2 s watchdog; joining anyway to avoid UAF" log line. Implementation matches REVIEW.md option (a).

### WR-02: setTriggerCallback re-applied implicitly on configure() — verify retention

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 457cb59
**Applied fix:** Defensively re-attach the trigger callback at the bottom of `applyConfig()` (inside the `if (stateMachine_)` block, after `stateMachine_->configure(smCfg)`). If a future change to `IStateMachine::configure()` ever clears internal callbacks, the trigger path stays wired silently. Cost is one `std::function` move per `applyConfig` call (rare event — only on `publish()` of a new DetectionConfig). RunLoop's existing one-shot bind on entry stays for clarity.

### WR-03: Ring drop log throttle skips most windows

**Files modified:** `driver/src/audio_worker.cpp`
**Commit:** 71368cd
**Applied fix:** Changed `(total % kRingDropLogPeriod) == 1` to `total > 0 && (total % kRingDropLogPeriod) == 0`. The `total > 0` guard preserves "no log when there are no drops"; the `== 0` form ("every 100th drop") is robust under any future change to bump-by-N drop semantics. Behavior under today's bump-by-1 semantics: log fires at 100, 200, 300, ... instead of 1, 101, 201, ... — net difference is ~100 drops earlier latency on the *first* spam window before the burst threshold is reached, which is well within the diagnostic budget.

### IN-01: `runner_ptr` setter does not clear on Stop

**Files modified:** `driver/src/device_provider.cpp`
**Commit:** ba6fbdd
**Applied fix:** Added `if (audioWorker_) audioWorker_->SetDetectionRunner(nullptr);` at the top of `DeviceProvider::Cleanup`, before the `detectionRunner_.reset()` call. The audio callback's existing `weak_ptr<State> + state->alive` guards already make this safe today; the explicit clear pairs with the `SetDetectionRunner(runner.get())` call in `Init` and survives any future reordering of the Cleanup steps.

### IN-02: `loadTrainingData` non-Windows silent skip

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 841940d
**Applied fix:** Added an `#else` arm to the `#ifdef _WIN32` block that emits a single `"non-Windows build, skipping profile load (detection inert)"` log line. Functionally inert on Windows builds (driver is Windows-only per CLAUDE.md); makes test-stub builds explicit instead of silently leaving `profile_path` empty.

### IN-03: detectionDefaults reads collapse Unset vs error log shapes

**Files modified:** `driver/src/device_provider.cpp`
**Commit:** 8af2da6
**Applied fix:** Split each of the four `detection_*` reads (sensitivity / threshold / cooldown_ms / min_duration_ms) into three branches matching the precedent of `enable_driver_audio` + `enable_driver_detection` blocks above: `VRSettingsError_UnsetSettingHasNoDefault`, "any other error" (with error code in log), and success. Fail-soft defaults unchanged (0.7 / 0.6 / 1000 / 200).

### IN-05: Hardcoded FFT size 2048

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 0ebb017
**Applied fix:** Added `constexpr int kDetectionFftSize = 2048;` to the anonymous namespace at top of file with a comment linking it to the v1.5 GUI default and noting Phase 8 (config read-back) will thread this through `DetectionConfig`. Replaced the inline `2048` in `createFFTDetector()` and the `"thread spawned"` log format string. Behavior unchanged.

### IN-06: Redundant first-iteration applyConfig

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 7e74256
**Applied fix:** Added `lastObserved_ = cfg;` immediately after the `applyConfig(*cfg)` call inside `Start()`. The first RunLoop iteration's pointer-identity compare `cfg.get() != lastObserved_.get()` is now false, skipping the redundant re-apply. `publish()` correctness is unaffected because publish always installs a fresh `shared_ptr` whose `.get()` differs from this seeded value.

## Skipped Issues

### WR-04: HttpServer::Start uses sleep-poll to detect bind success

**File:** `driver/src/http_server.cpp:64-92`
**Reason:** SKIP_DEFERRED — REVIEW.md explicitly notes "this is pre-existing code (not Phase 7), but Phase 7 still touches the HttpServer surface (driverDetectionActiveGetter ctor param)." The proper fix (`std::promise<bool>` for bind-result synchronization) requires a non-trivial refactor of the bind-loop in `Start()` and a new private member on `HttpServer` to thread the promise/future across the `ServerThread()` -> caller boundary. This is a startup-path correctness change that pre-dates Phase 7, would touch all phases that exercise `HttpServer::Start` (effectively all of v1.5 plus the new `driverDetectionActiveGetter` integration), and has no Phase 7 regression risk under the current 200 ms sleep heuristic in CI / dev hardware. Recommended landing point: a dedicated maintenance phase or alongside any future change that already touches `HttpServer::Start`.

**Original issue:** 200 ms `sleep_for` after spawning `ServerThread` is racy with the bind/listen path's deterministic `running_ = true` assignment — on slow / loaded machines the bind may not have happened yet, leaving the caller to advance to the next port even though the current bind would have succeeded.

### IN-04: Hardcoded sample rate 48000 in DeviceProvider::Init

**File:** `driver/src/device_provider.cpp:222`
**Reason:** SKIP_DEFERRED — REVIEW.md explicitly classifies this as a "tracking item" and recommends "file an issue or P8 task to add `AudioWorker::sample_rate()`". The fix requires a new public accessor on `AudioWorker` (`uint32_t sample_rate() const noexcept`) that reads the rate observed after `capture_->startCapture()` returns, plus thread-safety considerations for read-during-construction. The comment in the source already acknowledges this: "if a future plan adds an accessor, swap to `audioWorker_->sample_rate()`". Recommended landing point: Phase 8 (config read-back) which will already be touching `DetectionConfig` plumbing and the AudioWorker / DetectionRunner interface.

**Original issue:** WASAPI shared-mode default is typically 48 kHz but can be 44.1 kHz on legacy hardware or 96 kHz on pro audio interfaces. The detection FFT bins, threshold curves, and state-machine timing assumptions all depend on the actual sample rate; mismatch produces confidence drift.

---

_Fixed: 2026-05-04_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

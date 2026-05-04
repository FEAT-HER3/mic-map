---
phase: 07-driver-side-detection-thread
reviewed: 2026-05-03T00:00:00Z
depth: standard
files_reviewed: 20
files_reviewed_list:
  - driver/src/sample_ring.hpp
  - driver/src/detection_runner.hpp
  - driver/src/detection_runner.cpp
  - driver/src/device_provider.hpp
  - driver/src/device_provider.cpp
  - driver/src/audio_worker.hpp
  - driver/src/audio_worker.cpp
  - driver/src/http_server.hpp
  - driver/src/http_server.cpp
  - src/steamvr/include/micmap/steamvr/vr_input.hpp
  - src/steamvr/src/vr_input.cpp
  - apps/micmap/main.cpp
  - driver/resources/settings/default.vrsettings
  - cmake/AssertDetectionRunnerNoVrApi.cmake
  - driver/CMakeLists.txt
  - src/steamvr/CMakeLists.txt
  - tests/CMakeLists.txt
  - tests/test_command_queue.cpp
  - tests/driver/detection_settings_propagation_test.cpp
  - tests/driver/device_provider_lifecycle_stress_test.cpp
findings:
  critical: 0
  warning: 4
  info: 6
  total: 10
status: issues_found
---

# Phase 7: Code Review Report

**Reviewed:** 2026-05-03
**Depth:** standard
**Files Reviewed:** 20
**Status:** issues_found

## Summary

Phase 7 (driver-side detection thread, MIG-02/03/04/06) is well-architected: the SPSC ring is textbook rigtorp-style with proper drop-OLDEST atomicity and cache-line padding, lifecycle teardown follows strict reverse-construction order with documented rationale, pause/resume use atomic exchange for idempotency, MIG-06 settings propagation uses C++17 `std::atomic_*<std::shared_ptr>` with pointer-identity caching, and `weak_ptr<State>` + `alive` flag guards the audio callback from UAF. Threading discipline is consistent with P6 patterns.

That said, there are real correctness concerns worth fixing before P8:

1. **WR-01 (highest priority)** — `DetectionRunner::Stop()` watchdog-detach path leaves `detector_` and `stateMachine_` mid-flight on a detached thread that holds raw references into `ring_` (which AudioWorker is about to destroy). Cleanup ordering in `DeviceProvider::Cleanup()` does not protect against this because the watchdog has already given up.
2. **WR-02** — The `stateMachine_->setTriggerCallback` lambda captures `this`, and the callback fires from inside `update()` which runs on the detection thread. After `RunLoop` exits, `stateMachine_` is reset on the same thread, so this is safe; however, the callback closure still holds `this` while `applyConfig()` calls `stateMachine_->configure(smCfg)` mid-loop — `configure()` may or may not preserve callbacks. Worth verifying.
3. **WR-03** — `kRingDropLogPeriod` log throttle uses `total % kRingDropLogPeriod == 1` which only fires on drops 1, 101, 201… A wraparound-immediately-after-2nd drop window can mute the diagnostic; minor but worth flagging.
4. **WR-04** — `HttpServer::Start()` uses a 200 ms `sleep_for` to detect successful bind, which is racy with the bind/listen path that already sets `running_` deterministically.

Plus six info-level cleanup items.

## Warnings

### WR-01: Detached detection thread holds dangling reference to AudioWorker's ring

**File:** `driver/src/detection_runner.cpp:177-181` and `driver/src/device_provider.cpp:261-274`
**Issue:** When the 2 s shutdown watchdog in `DetectionRunner::Stop()` expires, the code calls `thread_.detach()` and returns. Two correctness problems compound:

1. The detached detection thread continues calling `ring_.has_data()` and `ring_.try_pop(...)` against `SampleRing<16,480>& ring_` — a reference to a member of `AudioWorker`. Immediately after `Stop()` returns, `DeviceProvider::Cleanup()` proceeds to `audioWorker_.reset()` (device_provider.cpp:272), which destroys the `SampleRing` that the detached thread is still reading. This is a use-after-free under the watchdog-overrun branch.
2. The detached thread also still owns `unique_ptr<INoiseDetector> detector_` and `unique_ptr<IStateMachine> stateMachine_`, both of which are members of `DetectionRunner`. When `~DetectionRunner` returns (it doesn't wait — Stop already returned), the `DetectionRunner` itself is destroyed, but the detached thread keeps dereferencing `this` (e.g., `triggers_.fetch_add`, `commandQueue_.push`, `cv_.wait_for(lk, ..., [this]{...})`). UAF on `this`.

The 50-cycle stress test (`device_provider_lifecycle_stress_test.cpp`) does NOT exercise this path because `driverDetectionEnabled_` defaults to false with null context, so `DetectionRunner` is never constructed. The watchdog-overrun branch is therefore untested.

**Fix:** Two reasonable options — pick one:

a. **Block-until-joined fallback (preferred for correctness):** After the watchdog overrun, do not detach. Instead, log loudly and `thread_.join()` anyway. The T3 mitigation rationale ("never block vrserver.exe shutdown") is valid for AudioWorker (whose teardown crosses COM/WASAPI), but DetectionRunner's only blocking surface is `cv_.wait_for(50ms)` — the thread will exit within at most one timeout period after `shutdown_` is observed. The 2 s watchdog overrunning means something is genuinely wrong; better to surface it than UAF.

b. **Heap-allocate the runner state (shared_ptr) and have the thread own a strong reference:** If detach is truly required, change `detector_`, `stateMachine_`, `commandQueue_` reference, and `ring_` reference into a single `shared_ptr<RunnerState>` that the thread captures by value. Then the detached thread's reads stay valid until the thread exits. AudioWorker's `Stop()` would need a similar treatment for the ring (or the ring needs to be heap-allocated and shared via `shared_ptr`).

```cpp
// detection_runner.cpp:174-181 — option (a):
if (thread_finished_.load(std::memory_order_acquire)) {
    thread_.join();
    DriverLog("MicMap detection: thread joined cleanly\n");
} else {
    DriverLog("MicMap detection: WARNING - did not exit within 2 s watchdog; "
              "joining anyway to avoid UAF on AudioWorker ring teardown\n");
    thread_.join();   // accept the block; UAF is worse than a hang
}
```

### WR-02: setTriggerCallback re-applied implicitly on configure() — verify retention

**File:** `driver/src/detection_runner.cpp:259-263` and `:237-243`
**Issue:** `RunLoop()` sets `stateMachine_->setTriggerCallback([this]{...})` once before the main loop. On every `applyConfig()` call (line 242), `stateMachine_->configure(smCfg)` is invoked. If `IStateMachine::configure()` resets the trigger callback (or any internal state that disconnects it), the callback chain silently breaks and `commandQueue_.push(TapCommand{})` stops firing — no log, no metric, just silence.

The contract of `IStateMachine::configure()` w.r.t. callback retention is not visible from this review's scope (state_machine.hpp wasn't part of the file list), but the plan comment at line 233-234 says configure "rebuilds the entire config in one shot" — that wording is ambiguous about callbacks.

**Fix:** Either (a) re-set the trigger callback at the bottom of `applyConfig()` defensively, or (b) add an explicit comment + test asserting the callback survives `configure()`. Defensive option:

```cpp
void DetectionRunner::applyConfig(const DetectionConfig& cfg) {
    if (detector_) {
        detector_->setSensitivity(cfg.sensitivity);
    }
    if (stateMachine_) {
        micmap::core::StateMachineConfig smCfg;
        smCfg.detectionThreshold   = cfg.threshold;
        smCfg.minDetectionDuration = std::chrono::milliseconds(cfg.min_duration_ms);
        smCfg.cooldownDuration     = std::chrono::milliseconds(cfg.cooldown_ms);
        stateMachine_->configure(smCfg);
        // Defensive: re-attach the trigger callback in case configure() reset it.
        // RunLoop sets it once on entry, but if configure() ever clears callbacks,
        // the very first MIG-06 publish would silently disable triggering.
        stateMachine_->setTriggerCallback([this]() {
            commandQueue_.push(TapCommand{});
            const uint32_t n = triggers_.fetch_add(1, std::memory_order_relaxed) + 1;
            DriverLog("MicMap detection: TapCommand pushed (n=%u)\n", n);
        });
    }
}
```

### WR-03: Ring drop log throttle skips most windows

**File:** `driver/src/audio_worker.cpp:282`
**Issue:** `if ((total % kRingDropLogPeriod) == 1)` fires only when total drops are exactly 1, 101, 201, ... The intent is "every 100th drop"; the comparison `== 1` (rather than `== 0`) means the first log fires on drop #1, but if drops happen in clusters and the count crosses from say 99 to 105 in a single producer burst (multiple slots dropped via repeated `try_push` calls), no log fires for that burst — only the next time `total` lands exactly on n*100+1.

In practice `try_push` increments `drops_` by 1 per call (drop-OLDEST is per-overflow-slot), so monotonic +1 increments are normal — but the symmetric variant (`== 0`) is more robust under any future change to bump-by-N semantics.

**Fix:** Change to `== 0` and skip log on `total == 0`:

```cpp
if (total > 0 && (total % kRingDropLogPeriod) == 0) {
    DriverLog("MicMap detection: ring overflow drops=%u\n", total);
}
```

Or use a separate `last_logged_drop_threshold_` counter to make the intent explicit.

### WR-04: HttpServer::Start uses sleep-poll to detect bind success

**File:** `driver/src/http_server.cpp:64-92`
**Issue:** `Start()` spawns a server thread that calls `bind_to_port` then sets `running_ = true`, then calls the blocking `listen_after_bind()`. The caller waits a fixed 200 ms `sleep_for`, then checks `running_` to decide whether the bind succeeded. Two problems:

1. On a slow/loaded machine 200 ms may not be enough — the bind hasn't happened yet, `running_` is still false, and `Start()` proceeds to the next port even though the current bind would have succeeded shortly. Result: bind racing across multiple ports, possibly leaving multiple httplib::Server instances mid-init.
2. On a fast machine where bind fails immediately (port already in use), `running_` stays false and the join + recreate dance proceeds — but the listen thread might exit between the 200 ms wait and the `joinable()` check, which is benign here but indicates the synchronization is implicit.

This is pre-existing code (not Phase 7), but Phase 7 still touches the HttpServer surface (driverDetectionActiveGetter ctor param). Worth noting.

**Fix:** Use a bind-result `std::promise<bool>` that the server thread sets after `bind_to_port` returns. The caller `wait_for`s on the future with a longer deadline (e.g., 2 s). Returns immediately on bind success or known failure; no fixed-time guesswork.

## Info

### IN-01: `runner_ptr` setter does not clear on Stop, leaving a stale pointer

**File:** `driver/src/audio_worker.cpp:131-142` and `driver/src/audio_worker.hpp:106`
**Issue:** `SetDetectionRunner` is called from `DeviceProvider::Init` after `DetectionRunner::Start()` succeeds. There is no symmetric clear in `Cleanup` — `audioWorker_->SetDetectionRunner(nullptr)` is never called before `detectionRunner_.reset()`. This is currently safe because:
- `state_->alive` is checked first in the callback, so by the time the callback would deref `runner`, it has already bailed.
- The audio worker is reset *after* the detection runner, so the callback won't fire after `runner` becomes invalid.

But the code reads as if it relies on those orderings without enforcing them locally. A defensive `audioWorker_->SetDetectionRunner(nullptr)` at the top of `DeviceProvider::Cleanup` (before `detectionRunner_.reset()`) would make the intent self-evident.

**Fix:**
```cpp
// device_provider.cpp Cleanup, before detectionRunner_.reset():
if (audioWorker_) audioWorker_->SetDetectionRunner(nullptr);
if (detectionRunner_) detectionRunner_.reset();
```

### IN-02: `loadTrainingData` block uses Win32-only path; Linux/macOS leaves `profile_path` empty silently

**File:** `driver/src/detection_runner.cpp:104-124`
**Issue:** The `#ifdef _WIN32` guard around `SHGetFolderPathW` means on non-Windows builds, `profile_path` stays `std::filesystem::path{}` (empty). The check `if (!profile_path.empty() && std::filesystem::exists(profile_path))` skips silently — no log line at all on non-Windows. The `else` branch (line 120-122) only fires when `profile_path` is non-empty but doesn't exist. The "no training profile" log is therefore suppressed on the platform stubs.

This is functionally fine (driver is Windows-only per CLAUDE.md) but the code asymmetry is a small smell. Either log a "platform has no profile-path resolution; detection inert" line for non-Windows, or document why silent skip is acceptable.

**Fix:** Add an `#else` arm that logs the platform-skip:
```cpp
#ifdef _WIN32
    // existing block
#else
    DriverLog("MicMap detection: non-Windows build, skipping profile load (detection inert)\n");
#endif
```

### IN-03: `detectionDefaults_.threshold` 0.0 is indistinguishable from "user explicitly set 0.0"

**File:** `driver/src/device_provider.cpp:166-207`
**Issue:** Each VRSettings GetFloat/GetInt32 call uses `err != VRSettingsError_None` as the trigger to fall back to the hardcoded default. But `GetFloat` returns 0.0 on `VRSettingsError_UnsetSettingHasNoDefault` *before* the err check (per the v1.5 atomic-config-read pattern). The block as written handles all errors uniformly (including UnsetSettingHasNoDefault), which is correct behavior, but the log message ("unset/error, defaulting to ...") collapses two distinct cases.

This is consistent with the existing `enable_driver_audio` block (lines 117-131) which DOES distinguish UnsetSettingHasNoDefault from other errors, so the new detection_* block is slightly less informative than the precedent. Consider matching the precedent for consistency:

```cpp
if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
    detectionDefaults_.threshold = 0.6f;
    DriverLog("MicMap: detection_threshold unset, defaulting to 0.6\n");
} else if (err != vr::VRSettingsError_None) {
    detectionDefaults_.threshold = 0.6f;
    DriverLog("MicMap: VRSettings GetFloat(detection_threshold) error=%d, defaulting to 0.6\n",
              static_cast<int>(err));
} else {
    DriverLog("MicMap: detection_threshold = %.3f\n", detectionDefaults_.threshold);
}
```

### IN-04: Hardcoded sample rate 48000 in DeviceProvider::Init may diverge from actual capture rate

**File:** `driver/src/device_provider.cpp:222`
**Issue:** Comment acknowledges this: "if a future plan adds an accessor, swap to audioWorker_->sample_rate()". WASAPI shared-mode default is typically 48 kHz but can be 44.1 kHz on legacy hardware, or 96 kHz on pro audio interfaces. The detection FFT bins, threshold curves, and state-machine timing assumptions all depend on the actual sample rate.

Tracking item: file an issue or P8 task to add `AudioWorker::sample_rate()` (read after `capture_->startCapture()` returns) and pass it through to `DetectionRunner`. Until then, mismatched rates produce confidence drift.

### IN-05: Fixed FFT size 2048 is also hardcoded and undocumented

**File:** `driver/src/detection_runner.cpp:92`
**Issue:** `createFFTDetector(sampleRate_, /*fftSize=*/2048)` — the magic number 2048 is in-line with no rationale. The v1.5 GUI app at `apps/micmap/main.cpp:262` uses `config.detection.fftSize` from ConfigManager. The driver-side detector uses a hardcoded constant. Phase 8 (config read-back) is the obvious place to thread this through DetectionConfig, but worth noting now so it lands.

**Fix:** Add `int fft_size{2048};` to `DetectionConfig` (with the comment "wired through in P8"), and reference it here. Or at minimum:

```cpp
constexpr int kDetectionFftSize = 2048;  // matches v1.5 default; threaded into DetectionConfig in P8
```

### IN-06: `lastObserved_` initial pointer-identity compare relies on null vs. seeded shared_ptr

**File:** `driver/src/detection_runner.cpp:284`
**Issue:** First iteration: `lastObserved_` is default-constructed (nullptr), `cfg` is the seeded value from the constructor. `cfg.get() != lastObserved_.get()` is true, so `applyConfig(*cfg)` runs. This is desired but redundant — `applyConfig(*cfg)` is also called in `Start()` at line 140, so the first iteration re-applies the same config. Cosmetic only; harmless because `applyConfig` is idempotent.

**Fix:** None required. If desired, seed `lastObserved_` in `Start()` after `applyConfig(*cfg)`:
```cpp
applyConfig(*cfg);
lastObserved_ = cfg;   // skip the redundant re-apply on first loop iter
```

---

_Reviewed: 2026-05-03_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

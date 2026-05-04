---
phase: 07-driver-side-detection-thread
fixed_at: 2026-05-04T00:00:00Z
review_path: .planning/phases/07-driver-side-detection-thread/07-REVIEW.md
iteration: 2
findings_in_scope: 11
fixed: 9
skipped: 2
status: partial
---

# Phase 7: Code Review Fix Report (Adversarial Re-Review, Iteration 2)

**Fixed at:** 2026-05-04T00:00:00Z
**Source review:** .planning/phases/07-driver-side-detection-thread/07-REVIEW.md
**Iteration:** 2 (adversarial re-review of post-iteration-1 state)

**Summary:**
- Findings in scope: 11 (1 blocker, 6 warnings, 4 info)
- Fixed: 9
- Skipped: 2 (both info-level, deferred per review's own recommendation)
- Build verification: cmake --build Debug succeeded, ctest 17/17 passed
  (including DetectionSettingsPropagation, DeviceProviderLifecycleStress,
  AssertDetectionRunnerNoVrApi)

## Fixed Issues

### BL-01: SampleRing drop-NEWEST eliminates SPSC tail race

**Files modified:** `driver/src/sample_ring.hpp`, `driver/src/audio_worker.cpp`, `driver/src/audio_worker.hpp`
**Commit:** 2e98161
**Applied fix:** Switched try_push from drop-OLDEST to drop-NEWEST per
review's recommended Option A. The producer now never writes tail_; the
consumer is the sole tail_ writer. On full ring, the new sample is
discarded and drops_ counter is incremented. Eliminates the two-writer
race on tail_ AND the data race on slot contents (producer writing
slots_[head & kMask] while consumer reads slots_[tail & kMask] when
full=true). Also: try_pop's tail_ load is now acquire (consumer-as-sole-
writer discipline; fence-free on x86/x64) and the inaccurate
"rigtorp/SPSCQueue" attribution comment was corrected. Updated callsite
comments in audio_worker.cpp/.hpp to reflect drop-NEWEST.

### WR-01: Live WASAPI sample rate plumbed through AudioWorker

**Files modified:** `driver/src/audio_worker.hpp`, `driver/src/audio_worker.cpp`, `driver/src/device_provider.cpp`
**Commit:** 903fe1f
**Applied fix:** Added State::sample_rate atomic + AudioWorker::sample_rate()
accessor. The worker thread publishes the rate (release-store) immediately
after capture_->startCapture() succeeds, reading capture_->getSampleRate()
which already exists in IAudioCapture. DeviceProvider::Init polls
audioWorker_->sample_rate() for up to 500 ms before falling back to
48000 with a clear WARNING log so the assumption is auditable in
vrserver.txt. The poll is necessary because AudioWorker::Start spawns the
worker thread asynchronously; the rate is not known synchronously when
Init proceeds to construct DetectionRunner. Includes <chrono> + <thread>
added to device_provider.cpp.

### WR-02: Pause/Resume guard on running_

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 48eaa55 (combined with WR-03)
**Applied fix:** Added `if (!running_.load()) return;` at the head of both
Pause() and Resume(). When the runner has not been Start()ed (or has been
Stop()ped), Pause/Resume now no-op cleanly instead of mutating paused_
and emitting misleading "paused"/"resumed" log lines. Closes the
"Pause-before-Start silently wiped by Start()'s unconditional
paused_.store(false)" hole.

### WR-03: Resume always notifies cv_

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 48eaa55 (combined with WR-02)
**Applied fix:** Resume() now unconditionally calls cv_.notify_one()
(under the running_ guard). The previous early-return-when-already-
resumed shape made `Pause(); Resume();` toggles inside one frame yield
the same wakeup latency as no-op (the thread could still sleep the full
50 ms wait_for window). The cv predicate handles spurious wakeups safely
and a redundant notify is cheap. The "resumed" log is suppressed only
when the state did not actually change (was_paused == false).

### WR-04: Trigger-callback install contract documented

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** afecbb4 (combined with WR-05)
**Applied fix:** Per the review's "if the contract truly is ambiguous,
the current 'install everywhere' defense is acceptable, but document it
explicitly" -- added a LOAD-BEARING comment in applyConfig listing all
three install sites (Start, RunLoop entry, applyConfig) with rationale
and an audit checklist for future maintainers who change
IStateMachine's callback retention contract. Behavior unchanged; the
maintenance hazard is now visible.

### WR-05: dt computed from sample count, not wall clock

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** afecbb4 (combined with WR-04)
**Applied fix:** Per the review's recommended Option 3 -- dt is now
`block_count * 1000 / sampleRate_` (milliseconds), matching what an
offline analyzer would compute. Removes scheduler jitter from state
machine timing entirely. The previous wall-clock dt fed the entire idle
duration (cv_.wait_for timeout, or full Pause->Resume gap) into the
state machine on the first post-wait update, prematurely satisfying
cooldown timers. Removed the now-unused `using clock = ...` alias and
the `last_tick` variable. Includes a fallback `rate_for_dt = sampleRate_
? sampleRate_ : 1` guard against division-by-zero in the (impossible)
sampleRate_=0 path.

**Note:** The dt change is a logic change in state-machine timing
semantics. Build + DetectionSettingsPropagation tests pass, but the
end-to-end "real trigger fires within expected wall-clock window"
behavior is best-validated on hardware. **Requires human verification
on the Phase 7 UAT harness (real Beyond mic + trained profile).**

### WR-06: SHGetKnownFolderPath for AppData resolution

**Files modified:** `driver/src/detection_runner.cpp`
**Commit:** 6c99986
**Applied fix:** Replaced SHGetFolderPathW (deprecated, MAX_PATH-bounded,
silently truncates on long-path symlinks) with SHGetKnownFolderPath
(long-path-aware, not deprecated). The returned PWSTR is freed via
CoTaskMemFree on every code path (success, failure-but-pointer-set).
Differentiated three log shapes:
  1. "SHGetKnownFolderPath(FOLDERID_RoamingAppData) failed (hr=0x%08X) -
     no profile load attempted (detection inert)"
  2. "profile file does not exist at <path> - detection inert until valid profile"
  3. "loadTrainingData failed for <path> - detection inert until valid profile"
The previous "no training profile at <empty path> - detection inert"
shape, which fired with an empty path on AppData failure, is gone.

### IN-01: Stale "future-accessor" comment removed

**Files modified:** `driver/src/device_provider.cpp` (covered by WR-01 commit)
**Commit:** 903fe1f (rolled into WR-01)
**Applied fix:** The comment `"AudioWorker does not currently expose
getSampleRate() out to here; if a future plan adds an accessor, swap to
audioWorker_->sample_rate()"` was deleted as part of the WR-01 rewrite
that landed exactly that accessor. Replaced with a comment explaining
the new poll-with-fallback logic.

### IN-03: driverDetectionActiveGetter Cleanup-window semantics documented

**Files modified:** `driver/src/device_provider.cpp`
**Commit:** b9f5829
**Applied fix:** Added a one-line comment above the lambda noting that it
reads driver state at request time and may report transitionally false
during Cleanup (detectionRunner_ non-null but IsRunning() already false
because Stop() flips running_ before unique_ptr.reset() destroys the
object). Per review: "Add a one-line comment ... No code change needed."

## Skipped Issues

### IN-02: Lint regex limitation in AssertDetectionRunnerNoVrApi.cmake

**File:** `cmake/AssertDetectionRunnerNoVrApi.cmake:60-61`
**Reason:** Skipped per review's own recommendation: "**Fix:** None
required. Document the limitation in the file-header comment if the
regex set diverges from the core lint in the future." The current regex
mirrors lint_no_openvr_in_core.cmake byte-for-byte (intentional; one
source of truth for the no-OpenVR rule), and the narrow three-file lint
scope is sufficient for the current use case. No documentation change
needed at this iteration; revisit if the core lint regex diverges.

### IN-04: tests/CMakeLists.txt EXISTS-guard refactor

**File:** `tests/CMakeLists.txt:190-255`
**Reason:** Skipped per review's own recommendation: "Pure refactor, no
behavior change. Defer until a Phase 8 / 9 test wants the same shape."
The duplication is a known scaling concern but does not affect Phase 7
correctness, build, or tests. The `gsd_define_phase_test()` extraction
is best done in concert with the first Phase 8/9 test that exercises
the same shape so the abstraction matches actual usage rather than
being designed in isolation.

## Build & Test Verification

```
cmake -S . -B build-driver-fix -DOPENVR_SDK_PATH=...
cmake --build build-driver-fix --config Debug
# All targets compiled cleanly; no errors.
ctest --test-dir build-driver-fix -C Debug --output-on-failure
# 100% tests passed, 0 tests failed out of 17
# - DetectionSettingsPropagation: PASS (0.08 s)
# - DeviceProviderLifecycleStress: PASS (25.48 s)
# - AssertDetectionRunnerNoVrApi: PASS (no vr:: leakage)
# - AssertAudioWorkerNoVrApi: PASS
```

## Followups for Verifier / UAT

- **WR-05 (sample-count dt) requires hardware UAT validation.** The unit
  tests verify the state machine still updates correctly under the new
  dt computation, but real-world cooldown / hold-window behavior with a
  trained profile is best confirmed on the Phase 7 UAT harness
  (Beyond mic + trained profile). The change is fail-safe in the
  "fewer triggers" direction (sample-count dt is always less-than-or-
  equal-to wall-clock dt for a given block).

- **WR-01 fallback path is a quiet danger.** If AudioWorker fails to
  publish sample_rate within 500 ms (capture init failed silently or
  hung), the FFT detector is built against assumed 48000 Hz and the log
  line says so. Watch for that WARNING in vrserver.txt during UAT --
  it is the canonical "trained profile won't match" signal.

- **BL-01 changes overflow semantics from drop-OLDEST to drop-NEWEST.**
  Under sustained back-pressure (slow detection thread, scheduler stall,
  Pause-then-quick-Resume), the most recent audio frame is now the one
  discarded rather than the oldest queued frame. For a periodic noise-
  cover detector this is indistinguishable in practice (the audio
  fingerprint repeats across many 10 ms frames). Worth noting in case
  the verifier sees a change in detection latency under stress.

---

_Fixed: 2026-05-04T00:00:00Z_
_Fixer: Claude (gsd-code-fixer, iteration 2)_
_Iteration: 2 (adversarial re-review of post-iteration-1 state)_

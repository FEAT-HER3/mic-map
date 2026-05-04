---
phase: 07-driver-side-detection-thread
reviewed: 2026-05-04T00:00:00Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - cmake/AssertDetectionRunnerNoVrApi.cmake
  - driver/CMakeLists.txt
  - driver/resources/settings/default.vrsettings
  - driver/src/audio_worker.cpp
  - driver/src/audio_worker.hpp
  - driver/src/detection_runner.cpp
  - driver/src/detection_runner.hpp
  - driver/src/device_provider.cpp
  - driver/src/device_provider.hpp
  - driver/src/sample_ring.hpp
  - tests/CMakeLists.txt
  - tests/driver/detection_settings_propagation_test.cpp
  - tests/driver/device_provider_lifecycle_stress_test.cpp
  - tests/test_command_queue.cpp
findings:
  blocker: 1
  warning: 6
  info: 4
  total: 11
status: issues_found
---

# Phase 7: Code Review Report (Adversarial Re-Review)

**Reviewed:** 2026-05-04T00:00:00Z
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

Adversarial re-review of Phase 7 driver-side detection thread after prior fix
round (commits IN-02..IN-06 plus earlier WR-01..WR-03). The prior review missed
a fundamental concurrency defect in `sample_ring.hpp`: the drop-OLDEST path has
the producer mutate `tail_` concurrently with the consumer's read-then-store,
which violates SPSC discipline and produces both data races on the slot
contents (UB) and lost-update races on `tail_` itself. The header's claim that
this pattern is "from rigtorp/SPSCQueue" is incorrect — that queue does not
support drop-oldest from the producer side at all.

Lifecycle ordering (Init/Cleanup, Pause/Resume, AudioWorker -> DetectionRunner
attach order, reverse-order teardown), VRSettings read paths, and the
CommandQueue producer-fan-in are otherwise sound. Several quality concerns
remain around hardcoded sample rate, redundant trigger-callback re-bind paths,
and a Pause-before-Start race that silently wipes intent on Start.

## Blocker Issues

### BL-01: `SampleRing::try_push` drop-OLDEST path races with consumer on both `tail_` and slot contents

**File:** `driver/src/sample_ring.hpp:31-50`
**Issue:**

The drop-OLDEST path lets the **producer** issue `tail_.fetch_add(1, release)`
while the **consumer** independently does `tail_.load(relaxed)` followed by
`tail_.store(tail + 1, release)` in `try_pop` (lines 56, 61). That is two
writers to `tail_`, with the consumer using a non-atomic read-modify-write
(`load` + `store`, not `fetch_add` or `compare_exchange`). The consumer can
therefore lose the producer's tail bump:

```
Initial: head=8, tail=0, kSlots=8 (full)
Consumer try_pop:                Producer try_push (full path):
  tail = tail_.load() => 0        head = head_.load() => 8
  head = head_.load() => 8        tail = tail_.load() => 0  (full)
  head != tail, proceed           tail_.fetch_add(1) => 1
  out = slots_[0]                 slots_[0] = new samples   <-- DATA RACE
                                  slot_count_[0] = n
                                  head_.store(9)
  tail_.store(0+1) => 1           (lost: producer's bump)
```

After this interleaving the ring believes head=9, tail=1 (one drained), but
two pushes happened (the original index 0 push that the consumer was reading,
and the producer's overwrite). One slot's worth of audio data is silently
dropped *in addition* to whatever the drops_ counter says.

Even when no tail-update is lost, the producer writing `slots_[head & kMask]`
while the consumer reads `slots_[tail & kMask]` (same index when full=true)
is a plain data race on a `std::array<float, 480>`, which is undefined
behavior in C++17. The consumer can read torn / mixed-vintage samples,
feeding garbage into `INoiseDetector::analyze` and producing spurious or
missed detections.

The header comment claims this pattern is "from rigtorp/SPSCQueue", but
rigtorp's SPSC queue does not support drop-oldest from the producer side —
its `try_push` returns false when full and the caller must handle it. There
is no two-writer-on-tail SPSC pattern that is correct without CAS.

This is masked today because:
- The ring is sized for ~16 * 10 ms = 160 ms of WASAPI buffering vs. the
  detection thread's 50 ms wake timeout, so full-state is rare in practice.
- The detection profile is missing on most dev machines, so `analyze` returns
  near-zero confidence and torn slot data does not propagate to a spurious
  TapCommand.

Neither of those is a guarantee. Under load (paused detection thread that
just resumed, scheduler stall, slow detector) the ring will hit full and the
race will fire.

**Fix:** Pick one of the standard SPSC-with-drop patterns:

Option A — drop-NEWEST (trivial, producer never touches tail):

```cpp
bool try_push(const float* samples, size_t count) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    if ((head - tail) == kSlots) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return true;   // dropped THIS sample
    }
    auto& slot = slots_[head & kMask];
    const size_t n = (count < kFrames) ? count : kFrames;
    for (size_t i = 0; i < n; ++i) slot[i] = samples[i];
    slot_count_[head & kMask] = n;
    head_.store(head + 1, std::memory_order_release);
    return false;
}
```

Option B — keep drop-OLDEST semantics but make the consumer the sole
tail-writer; producer signals "tail-ahead" by bumping a separate
producer-owned counter the consumer reads:

```cpp
// Producer never writes tail_. On full, advance an owned read_offset_
// counter; consumer subtracts it to compute the effective tail. Exact
// shape requires more care than fits in this comment.
```

Option A is recommended for v1 — drop-NEWEST under WASAPI back-pressure is
indistinguishable from drop-OLDEST for a real-time noise-cover detector
(the audio fingerprint repeats across many 10 ms frames), and it removes a
class of data races permanently.

Either way: also fix `try_pop` to use `memory_order_acquire` on the
`tail_` load (line 56) for symmetry with the consumer-as-sole-writer
discipline, and update the header comment to remove the inaccurate
"Pattern from rigtorp/SPSCQueue" attribution.

## Warnings

### WR-01: Hardcoded WASAPI sample rate `48000` mismatches actual device rate

**File:** `driver/src/device_provider.cpp:242`
**Issue:**

`DeviceProvider::Init` constructs `DetectionRunner` with `sampleRate = 48000`
under a comment that says "WASAPI shared-mode default sample rate.
AudioWorker does not currently expose getSampleRate() out to here". WASAPI
shared-mode rate is whatever the user picked in Windows Sound settings
(commonly 44100 or 96000 on consumer mics; the Bigscreen Beyond's mic
endpoint can present at 24000). When the actual rate differs from 48000,
the FFT detector's `createFFTDetector(sampleRate_, kDetectionFftSize)` is
constructed against the wrong rate, so the noise fingerprint frequency
bins shift by the rate ratio and the trained profile fails to match. The
production effect: detection runs but never (or rarely) triggers on the
trained sound.

This is also load-bearing for IN-05's claim that `kDetectionFftSize=2048`
"matches the v1.5 GUI default" — that match only holds if the rate matches
v1.5's (which the GUI app reads from the live capture device).

**Fix:** Plumb the actual sample rate from the live `IAudioCapture`. The
existing `IAudioCapture` API does not currently expose a `getSampleRate()`
accessor, so either:
1. Add `IAudioCapture::sampleRate()` and read it after `startCapture()`
   succeeds inside `AudioWorker::RunWorker`, expose via
   `AudioWorker::sample_rate()`, and pass into `DetectionRunner` via a
   delayed setter (call after `audioWorker_->Start()` returns true).
2. Or have `DetectionRunner` itself accept a rate-getter callback rather
   than a snapshot, so the value is read after WASAPI negotiation completes.

Until fixed, log a clear WARNING when detection is enabled noting the rate
is assumed-not-measured.

### WR-02: `Pause()` called before `Start()` is silently wiped by `Start()`

**File:** `driver/src/detection_runner.cpp:165` (Start), `:220-227` (Pause)
**Issue:**

`Pause()` does `paused_.exchange(true, acq_rel)` on the bare atomic
unconditionally — there is no "are we running" guard. `Start()`
unconditionally does `paused_.store(false, release)` at line 165 before
spawning the thread. So the sequence:

```
DeviceProvider::Init -> EnterStandby (early?) -> ... -> detectionRunner_->Start()
```

silently wipes the EnterStandby intent. While the current `DeviceProvider`
flow does not trigger this (Pause is only called from EnterStandby, and
DetectionRunner is constructed and Started inside Init before any RunFrame
event delivery), the contract published by the public Pause/Resume API
suggests they are general-purpose lifecycle hooks. A future caller (P8/P9
test harness, future external command) that pauses-then-starts will see
the pause silently dropped.

The `Pause()` log line ("MicMap detection: paused") will fire even when no
thread exists — also misleading.

**Fix:** Guard Pause/Resume to return early when `running_` is false, or
make `Start()` honor a pre-Start `paused_` value (don't unconditionally
clear it). Either fix paired with a brief comment in the header documenting
the chosen contract.

### WR-03: `Pause()` early-return suppresses the wake of an already-paused-but-now-shutting-down thread

**File:** `driver/src/detection_runner.cpp:223-227`
**Issue:**

```cpp
if (paused_.exchange(true, std::memory_order_acq_rel)) return;
cv_.notify_one();
```

If `Pause()` is called when already paused, the function returns
immediately and skips `cv_.notify_one()`. That is the documented "no
wakeup storm" intent. But the same mutation pattern interacts badly with
any caller that races Pause against shutdown: if Stop is called, sets
`shutdown_=true`, calls `notify_all`, and then on a different thread Pause
is called (with paused_ already true) — the Pause early-return is fine
because shutdown's notify_all already got the thread. So no bug there.

However, `Resume()` has the same shape (line 230) and the asymmetric case
matters more: if the detection thread blocks in `cv_.wait_for` because
neither shutdown nor data nor pause is set, an idempotent Resume that
observes paused already false will skip notify and the thread will sleep
the full 50 ms. This is the documented behavior, but it means
`Resume()` after a quick `Pause(); Resume();` toggle inside one frame
yields exactly the same wakeup latency as no Pause/Resume at all — which
defeats the stated purpose of Resume notifying the thread.

**Fix:** `Resume()` should always notify (the cv predicate handles spurious
wakeup safely; a redundant notify is cheap). Reserve the early-return-on-
no-state-change pattern for Pause only, where the goal is avoiding logspam
and unnecessary wakeups while no work is being done. Adjust the log line
shape similarly so a no-op Resume is not logged as if it changed state.

### WR-04: `applyConfig` re-installs trigger callback on every settings publish, duplicating the one set in `RunLoop`

**File:** `driver/src/detection_runner.cpp:285-289` (applyConfig),
`:306-310` (RunLoop entry)
**Issue:**

`RunLoop` sets the trigger callback once at entry (line 306). `applyConfig`
also sets the same trigger callback (line 285), and `applyConfig` is called:

1. Once from `Start()` (before the thread spawns) — installs callback on
   the calling thread.
2. Once at the top of every RunLoop iteration where `cfg.get() !=
   lastObserved_.get()` — re-installs on the detection thread.

The Start-time install is then immediately overwritten by RunLoop's entry
install. After that, every MIG-06 publish re-overwrites with an identical
callback. The behavior is correct (callbacks are functionally identical),
but:

- The reasoning in WR-02's comment (the IN-fix that motivated re-installing
  in applyConfig) implicitly assumes `IStateMachine::configure()` *might*
  clear callbacks. If that assumption is true, the install in `RunLoop`
  entry is the one that matters at startup, and the install in Start() is
  dead code (the very next iteration's `applyConfig` would re-install
  anyway... unless lastObserved_ matches initial cfg, which it does after
  IN-06, so applyConfig is *not* called the first iteration, and the
  Start-time install is also moot because RunLoop's entry overwrote it).
- If the assumption is false (configure() preserves callbacks), the
  applyConfig install is dead.

Either way, the trigger-callback wiring has three independent install sites
and tangled invariants. A future maintainer changing `IStateMachine`'s
callback retention contract has three places to update.

**Fix:** Centralize callback install. Pick one site (RunLoop entry is the
natural spot since it owns the long-lived state machine). Remove the
applyConfig install behind a documented comment that says "configure()
preserves callbacks per <interface contract reference>" — and verify the
contract by either:
- Reading the IStateMachine impl and adding a static_assert / unit test, or
- Adding a dedicated test that publishes a new config mid-loop and asserts
  the next trigger still fires.

If the contract truly is ambiguous, the current "install everywhere"
defense is acceptable, but document it explicitly with a `// LOAD-BEARING:
configure() may clear callbacks; re-install required` comment in
applyConfig.

### WR-05: `RunLoop`'s `last_tick` initialized at function entry produces a large `dt` for the first state-machine update after a long idle wait

**File:** `driver/src/detection_runner.cpp:299, 347-351`
**Issue:**

`last_tick = clock::now()` is set at the top of `RunLoop`, before the cv_
wait. The first time `try_pop` returns data (could be 50 ms into the loop
if the audio thread had not yet pushed, or 5+ seconds after a Pause/Resume
cycle that drain-discarded the ring), the `dt` computed against `last_tick`
will be the entire idle duration. That `dt` is fed to
`stateMachine_->update(result.confidence, dt)`, which uses it to advance
internal cooldown / hold / detection timers.

Effect on the state machine semantics:
- In the standby/Pause case, the state machine's cooldown timer "catches
  up" by the standby duration — could prematurely satisfy a cooldown that
  was supposed to elapse after Resume.
- In the cold-start case, the first `dt` may be 50 ms (worst-case wait
  timeout) instead of a true sample-period delta.

The state machine probably handles this gracefully (the cooldown being
larger-than-expected is fail-safe in the "fewer triggers" direction), but
the semantics deserve explicit reasoning.

**Fix:**
1. Reset `last_tick` to `clock::now()` immediately *before* the inner
   `while (ring_.try_pop(...))` loop (so dt measures only between drained
   blocks), or
2. Reset `last_tick` after any Pause->Resume transition (track an explicit
   `pause_state_changed_` flag), or
3. Compute `dt` from sample count instead of wall clock:
   `dt = std::chrono::milliseconds(block_count * 1000 / sampleRate_)` —
   this matches what an offline analyzer would compute and removes scheduler
   jitter from the timing.

Option 3 is the most correct for a deterministic detector.

### WR-06: `DetectionRunner::Start` allocates `std::filesystem::path` from `SHGetFolderPathW` without checking buffer-too-small

**File:** `driver/src/detection_runner.cpp:113-117`
**Issue:**

```cpp
wchar_t path_buf[MAX_PATH];
if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path_buf))) {
    profile_path = std::filesystem::path(path_buf) / L"MicMap" / L"training_data.bin";
}
```

`SHGetFolderPathW` is documented to write at most `MAX_PATH` characters
(including the null terminator), so the fixed-size buffer is safe at the
Win32-API contract level. However:

1. `SHGetFolderPathW` is deprecated in favor of `SHGetKnownFolderPath`,
   which uses long-path-aware APIs. On a system where AppData is mapped
   via a symlink that exceeds MAX_PATH, the deprecated API may silently
   truncate. This codepath is the only thing standing between the driver
   and "detection silently inert because we loaded a corrupt half-path".
2. There is no bounds check / sanity check after the call — an unexpected
   return value (FAILED) is handled, but a "succeeded with empty buffer"
   edge case (which can happen on locked-down systems) would produce
   `profile_path = "/MicMap/training_data.bin"` which then fails the
   `std::filesystem::exists` check silently with no diagnostic.
3. The branch logs differently for `path empty` vs `path non-empty but
   not exists` — but the "path empty" case (when SHGetFolderPathW fails)
   logs `"no training profile at - detection inert"` with an empty path
   rendered, which is misleading.

**Fix:** Switch to `SHGetKnownFolderPath` (preferred) and check the
returned path is non-empty before constructing the profile path.
Differentiate the log shapes for "appdata path resolution failed" vs
"profile file does not exist at <path>". Example:

```cpp
PWSTR appdata = nullptr;
if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))
    && appdata != nullptr) {
    profile_path = std::filesystem::path(appdata) / L"MicMap" / L"training_data.bin";
    CoTaskMemFree(appdata);
} else {
    DriverLog("MicMap detection: SHGetKnownFolderPath(RoamingAppData) failed - "
              "no profile load attempted (detection inert)\n");
}
```

Note `SHGetKnownFolderPath` requires COM to be initialized; this runs on
the vrserver Init thread, which has its own apartment — verify before
shipping.

## Info

### IN-01: Sample-rate-mismatch comment drift in `device_provider.cpp:241-242`

**File:** `driver/src/device_provider.cpp:241`
**Issue:** The comment says "if a future plan adds an accessor, swap to
audioWorker_->sample_rate()" — this is precisely the pivot WR-01 demands
and the comment treats it as nice-to-have rather than required. Once WR-01
is fixed, delete this comment to match reality.
**Fix:** Track WR-01 fix; remove the stale comment when the accessor lands.

### IN-02: `AssertDetectionRunnerNoVrApi.cmake` regex matches `vr::` only at `^` or after non-identifier — misses `// vr::`-in-comment but also misses real uses inside lambdas-on-line-1

**File:** `cmake/AssertDetectionRunnerNoVrApi.cmake:60-61`
**Issue:** The `^vr::` branch handles the start-of-file edge case but a
real production C++ file rarely starts with `vr::`. The
`[^a-zA-Z0-9_]vr::` branch covers the typical case (`vr::Foo`, ` vr::`,
`(vr::`, etc.) but explicitly does not match `vr::` immediately after a
backtick or backslash (uncommon in production code, but the comment claim
"mirror lint_no_openvr_in_core.cmake regex set byte-for-byte" is the only
thing keeping this from being flagged stricter). Acceptable for the
current narrow three-file lint scope.
**Fix:** None required. Document the limitation in the file-header comment
if the regex set diverges from the core lint in the future.

### IN-03: `DetectionRunner::IsRunning()` semantics drift from `AudioWorker::IsRunning()`

**File:** `driver/src/detection_runner.hpp:96`,
`driver/src/audio_worker.hpp:72`
**Issue:** Both classes expose `IsRunning() const` reading their respective
`running_` atomic. AudioWorker sets `running_=true` in `Start()` after
spawning the thread; DetectionRunner does the same. AudioWorker sets
`running_=false` in `Stop()` *after* the thread joins; DetectionRunner
does the same. So far parallel.

But `DeviceProvider::Init`'s `driverDetectionActiveGetter` lambda chains
`audioWorker_ && detectionRunner_ && detectionRunner_->IsRunning()` —
which is half of the truth. A consumer querying `/health` between
`detectionRunner_.reset()` (which joins the thread, sets running_=false,
then destroys the object — the unique_ptr null assignment is the last
step) and the very next instruction has a tiny window where
`detectionRunner_` is still non-null but `IsRunning()` returns false.
Already correct, just worth documenting.
**Fix:** Add a one-line comment at line 99 of device_provider.cpp noting
that the chain reads driver state at request time and may report
transitionally false during Cleanup. No code change needed.

### IN-04: `tests/CMakeLists.txt` test executable list grows without grouping comment scaffolding

**File:** `tests/CMakeLists.txt:190-255`
**Issue:** The Phase 7 tests block (`---- Phase 7 Wave 0 ----`) is well
commented, but each `if(EXISTS ${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp)`
guard duplicates the same conditional logic. As more phases land,
this scales to N copy-pasted EXISTS branches.
**Fix:** Extract a CMake function `gsd_define_phase_test(NAME EXE SOURCES
[GUARDED_SOURCE...])` that handles the EXISTS-guard pattern uniformly.
Pure refactor, no behavior change. Defer until a Phase 8 / 9 test wants
the same shape.

---

_Reviewed: 2026-05-04T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer, adversarial re-review)_
_Depth: standard_

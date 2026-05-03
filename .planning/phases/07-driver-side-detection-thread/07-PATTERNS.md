# Phase 7: Driver-Side Detection Thread — Pattern Map

**Mapped:** 2026-05-02
**Branch verified against:** `hmd-button`
**Files analyzed:** 11 (5 NEW, 6 MODIFIED)
**Analogs found:** 11 / 11 (every new/modified file has a strong in-tree analog or is itself an extension of an existing pattern)

> All line numbers below were re-verified by direct `Read` of the actual files on the `hmd-button` branch as of the mapping date. Where the RESEARCH.md cited a slightly drifted range, this PATTERNS.md is authoritative for the planner.

---

## File Classification

| New/Modified File                                                | Status   | Role                       | Data Flow                       | Closest Analog                                                        | Match Quality |
|------------------------------------------------------------------|----------|----------------------------|---------------------------------|-----------------------------------------------------------------------|---------------|
| `driver/src/sample_ring.hpp`                                     | NEW      | utility (header-only SPSC) | streaming (audio cb → detection)| `driver/src/command_queue.hpp` (bounded queue + drop-OLDEST)          | role-match (different concurrency model — SPSC lock-free vs mutex) |
| `driver/src/detection_runner.hpp`                                | NEW      | service / thread owner     | event-driven (cv-wakeup loop)   | `driver/src/audio_worker.hpp` (worker-thread lifecycle owner)         | exact         |
| `driver/src/detection_runner.cpp`                                | NEW      | service / thread impl      | event-driven (cv-wakeup loop)   | `driver/src/audio_worker.cpp` (worker-thread RunWorker)               | exact         |
| `cmake/AssertDetectionRunnerNoVrApi.cmake`                       | NEW      | build/lint                 | source-grep (script-mode)       | `cmake/AssertAudioWorkerNoVrApi.cmake`                                | exact (sibling clone) |
| `tests/driver/detection_settings_propagation_test.cpp`           | NEW      | test (headless integration)| request-response (publish→read) | `tests/driver/audio_worker_lifecycle_headless.cpp`                    | exact         |
| `tests/driver/device_provider_lifecycle_stress_test.cpp`         | NEW      | test (headless integration)| batch (50× loop)                | `tests/driver/audio_worker_lifecycle_headless.cpp`                    | role-match    |
| `driver/src/audio_worker.cpp` *(MOD)*                            | MODIFIED | service                    | streaming (callback rewire)     | self — extending lines 241-263 (existing setAudioCallback lambda)     | exact         |
| `driver/src/audio_worker.hpp` *(MOD)*                            | MODIFIED | service header             | streaming (ring/runner attach)  | self — adds attach API                                                | exact         |
| `driver/src/device_provider.{hpp,cpp}` *(MOD)*                   | MODIFIED | controller / lifecycle     | request-response (RunFrame)     | self — extending Init/Cleanup/Standby blocks                          | exact         |
| `driver/src/http_server.cpp` *(MOD)*                             | MODIFIED | controller (HTTP routes)   | request-response (`/health`)    | self — extending lines 154-157 `/health` handler                      | exact         |
| `driver/resources/settings/default.vrsettings` *(MOD)*           | MODIFIED | config (JSON)              | static config                   | self — appending to existing `driver_micmap` keys                     | exact         |
| `driver/CMakeLists.txt` *(MOD)*                                  | MODIFIED | build                      | n/a                             | self — appending to existing `add_library(driver_micmap …)` source list | exact     |
| `tests/CMakeLists.txt` *(MOD)*                                   | MODIFIED | build                      | n/a                             | self — mirror lines 150-176 (P6 test + lint registration)             | exact         |
| `tests/test_command_queue.cpp` *(MOD)*                           | MODIFIED | test                       | concurrent push                 | self — extend with two-producer thread test                           | exact         |
| `apps/micmap/main.cpp` *(MOD)*                                   | MODIFIED | client (trigger site)      | request-response (`/health` poll)| self — line 520 `driverClient->tap()` site                            | exact         |

---

## Pattern Assignments

### `driver/src/sample_ring.hpp` (utility, streaming SPSC)

**Analog:** `driver/src/command_queue.hpp` (full file is 42 lines — small enough to mirror end-to-end)

**Why this analog:** Same project header conventions, same `namespace micmap::driver`, same "header-only bounded queue with drop-OLDEST overflow" semantics. The new SPSC ring is a different concurrency model (lock-free atomics vs mutex) but the file shape, naming, and overflow-policy contract carry over verbatim.

**Pattern to copy — file shape and namespace** (`command_queue.hpp:1-12`):

```cpp
#pragma once
#include <deque>
#include <mutex>
#include <optional>

namespace micmap::driver {

// A single tap command. The app posts one of these per detection rising
// edge; the driver expands it into UpdateBooleanComponent(true) followed
// by UpdateBooleanComponent(false) after a short hold, so SteamVR's
// complex_button binding sees a clean single-click.
struct TapCommand {};
```

**Pattern to copy — drop-OLDEST contract documented at the API** (`command_queue.hpp:18-25`):

```cpp
// Producer (HTTP thread). Returns true if queue was full and oldest dropped.
bool push(TapCommand cmd) {
    std::lock_guard<std::mutex> lk(m_);
    bool dropped = false;
    if (q_.size() >= kMaxDepth) { q_.pop_front(); dropped = true; }
    q_.push_back(cmd);
    return dropped;
}
```

**Diverge from analog:** Replace mutex+deque with `alignas(64) std::atomic<size_t>` head/tail, fixed `std::array<std::array<float, kFrames>, kSlots>` backing, acquire/release fences. SPSC invariant: producer is sole writer of head; consumer is sole writer of tail EXCEPT in the full-branch where producer bumps tail forward (drop-OLDEST atomicity per RESEARCH.md §"Pattern 1"). Track an `atomic<uint32_t> drops_` for diagnostics.

**Sketch source:** `07-RESEARCH.md` lines 308-373 (full reference impl, MIT-pattern from rigtorp/SPSCQueue).

---

### `driver/src/detection_runner.hpp` + `driver/src/detection_runner.cpp` (service, event-driven)

**Analog:** `driver/src/audio_worker.{hpp,cpp}` — exact role match (driver-only worker thread owner).

**Why this analog:** P6 D-13/D-14 shipped `AudioWorker` as the canonical "driver-only thread owner" template. Same lifecycle (`Start()`/`Stop()`, 2-second watchdog, idempotent shutdown via destructor), same forward-decl-only header strategy, same `DriverLog` logging convention, same `MicMap:` log-prefix shape, same `std::atomic<bool>` shutdown signal + `cv_.wait` pattern. DetectionRunner replicates the entire skeleton; only the loop body differs.

#### Imports / includes pattern (`audio_worker.hpp:15-26`)

```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

// Forward-declare to avoid pulling micmap/audio/* into the header.
namespace micmap::audio { class IAudioCapture; }

namespace micmap::driver {
```

**Apply to detection_runner.hpp:** identical includes; forward-declare `micmap::detection::INoiseDetector` and `micmap::core::IStateMachine` + `StateMachineConfig` (don't pull shared-lib headers into a driver-side header). Forward-declare `CommandQueue` and the `SampleRing<Slots,Frames>` template instantiation parameters.

#### Class shape — public lifecycle API (`audio_worker.hpp:51-92`)

```cpp
class AudioWorker {
public:
    AudioWorker();
    ~AudioWorker();

    AudioWorker(const AudioWorker&) = delete;
    AudioWorker& operator=(const AudioWorker&) = delete;

    /// Spawns the worker thread. Returns true if joinable.
    bool Start();

    /// Idempotent shutdown; called by destructor. 2 s watchdog (D-13).
    void Stop();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    // ...
```

**Apply to DetectionRunner:** mirror exactly — ctor, dtor, deleted copy/move, `Start()`, `Stop()`, `IsRunning()`. Add `Pause()`, `Resume()`, `publish(std::shared_ptr<const DetectionConfig>)`, and `NotifyOne()` (called by AudioWorker's audio callback after each `try_push`).

#### Member layout (`audio_worker.hpp:94-106`)

```cpp
private:
    static void ThreadEntry(AudioWorker* self);
    void RunWorker();

    std::shared_ptr<State>                        state_;
    std::unique_ptr<micmap::audio::IAudioCapture> capture_;  // owned by worker thread
    std::thread                                   thread_;
    std::mutex                                    mu_;
    std::condition_variable                       cv_;
    std::atomic<bool>                             shutdown_{false};
    std::atomic<bool>                             running_{false};
    std::atomic<bool>                             thread_finished_{false};
};
```

**Apply to DetectionRunner:** same `ThreadEntry`/`RunLoop` split; same `mu_`/`cv_`/`shutdown_`/`running_`/`thread_finished_` quartet. ADD: `std::atomic<bool> paused_{false}`, `std::atomic<uint32_t> triggers_{0}`, `std::shared_ptr<const DetectionConfig> activeConfig_` (accessed via `std::atomic_load_explicit` — C++17 free-function form per RESEARCH.md §"Standard Stack" — project compiles as C++17 per `driver/CMakeLists.txt:18`), refs to `SampleRing<…>&` and `CommandQueue&`, and `unique_ptr<INoiseDetector>` + `unique_ptr<IStateMachine>`.

#### Start() pattern (`audio_worker.cpp:72-88`)

```cpp
bool AudioWorker::Start() {
    if (running_.load(std::memory_order_acquire)) {
        DriverLog("MicMap: AudioWorker::Start called while already running\n");
        return thread_.joinable();
    }
    shutdown_.store(false, std::memory_order_release);
    thread_finished_.store(false, std::memory_order_release);
    if (state_) {
        state_->alive.store(true, std::memory_order_release);
        state_->rms_logs_emitted.store(0, std::memory_order_release);
        state_->frames_seen.store(0, std::memory_order_release);
    }
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&AudioWorker::ThreadEntry, this);
    DriverLog("MicMap: AudioWorker thread spawned\n");
    return thread_.joinable();
}
```

**Apply to DetectionRunner::Start:** identical structure. Substitute `MicMap detection:` log prefix per Claude's discretion in CONTEXT.md. Reset `paused_` and `triggers_` counters; do NOT reset `activeConfig_` (publishers may have populated it before Start).

#### Stop() — 2-second watchdog (`audio_worker.cpp:90-122`) — LOAD-BEARING

```cpp
void AudioWorker::Stop() {
    if (!thread_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }
    if (state_) {
        state_->alive.store(false, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_.store(true, std::memory_order_release);
    }
    cv_.notify_all();

    // 2 s watchdog (D-13) matching v1.5 VREvent_Quit precedent. Poll
    // thread_finished_ every 25 ms; on overrun log + detach so we never
    // block vrserver.exe shutdown (T3 mitigation).
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + kShutdownWatchdog;
    while (!thread_finished_.load(std::memory_order_acquire) &&
           clock::now() < deadline) {
        std::this_thread::sleep_for(kWatchdogPoll);
    }
    if (thread_finished_.load(std::memory_order_acquire)) {
        thread_.join();
        DriverLog("MicMap: AudioWorker thread joined cleanly\n");
    } else {
        DriverLog("MicMap: audio worker did not exit within 2 s watchdog "
                  "- detaching (T3 mitigation)\n");
        thread_.detach();
    }
    running_.store(false, std::memory_order_release);
}
```

**File-scope constants** (`audio_worker.cpp:55-59`):

```cpp
constexpr uint32_t kRmsBudget = 100;
constexpr auto kShutdownWatchdog = std::chrono::seconds(2);
constexpr auto kWatchdogPoll     = std::chrono::milliseconds(25);
```

**Apply to DetectionRunner::Stop:** copy verbatim; replace log prefix with `MicMap detection:`. Reuse the same `kShutdownWatchdog` (2s) and `kWatchdogPoll` (25ms) constants. ADD a separate `kWakeTimeout = std::chrono::milliseconds(50)` for the per-iteration `cv_.wait_for` (Claude's discretion confirmed at 50ms).

#### Run loop — wait-on-CV pattern (`audio_worker.cpp:281-287`)

```cpp
{
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] {
        return shutdown_.load(std::memory_order_acquire);
    });
}
```

**Apply to DetectionRunner::RunLoop (per D-18):** use `cv_.wait_for(lk, kWakeTimeout, predicate)`; predicate is `shutdown_ || paused_ || ring_->has_data()`. After predicate returns: check `shutdown_` (break), reload `activeConfig_` snapshot via `std::atomic_load_explicit`, check `paused_` (drain-discard, continue), else drain ring → `analyze` → `update` (rising-edge `TriggerCallback` pushes `TapCommand`).

**Sketch source:** `07-RESEARCH.md` lines 700-755 (full DetectionRunner::RunLoop reference impl).

#### Reverse-order teardown comment style (`audio_worker.cpp:289-303`)

```cpp
// Reverse-order teardown — ALL on this worker thread (Pitfall 4 / D-13).
// ~WASAPIAudioCapture (audio_capture.cpp:222-235) unregisters the
// IMMNotificationClient, releases ComPtrs, and calls CoUninitialize on
// the capture's own balanced count - all on the same thread that did
// the register, automatically.
if (capture_) {
    capture_->stopCapture();
    capture_.reset();
}
#ifdef _WIN32
::CoUninitialize();   // matches our own CoInitializeEx above
#endif
DriverLog("MicMap: audio worker thread exiting cleanly\n");
if (state_) state_->alive.store(false, std::memory_order_release);
thread_finished_.store(true, std::memory_order_release);
```

**Apply to DetectionRunner end-of-loop:** at thread exit, `detector_.reset()` then `stateMachine_.reset()` (reverse construction order); set `thread_finished_.store(true)`. Detection thread does NOT touch COM (no `CoInitializeEx` needed — it never calls a COM API).

---

### `cmake/AssertDetectionRunnerNoVrApi.cmake` (lint, source-grep)

**Analog:** `cmake/AssertAudioWorkerNoVrApi.cmake` — sibling clone (per D-22 + RESEARCH.md §"Standard Stack" alternatives recommendation).

**Why this analog:** Same script-mode CTest invocation pattern, same regex set, same RED-tolerant skip-on-NOT-EXISTS branch, same `_violations` aggregation. Per RESEARCH.md the recommendation is a sibling (smaller blast radius — one lint failure doesn't take down all driver-TU lints; scales to P10 deletions cleanly) rather than extending the AudioWorker lint.

**Whole-file pattern to copy** (`AssertAudioWorkerNoVrApi.cmake:22-69`):

```cmake
if(NOT DEFINED AUDIO_WORKER_DIR)
    message(FATAL_ERROR "AssertAudioWorkerNoVrApi: AUDIO_WORKER_DIR not provided. "
        "Pass -DAUDIO_WORKER_DIR=<dir-containing-audio_worker.{hpp,cpp}>")
endif()

set(_targets
    "${AUDIO_WORKER_DIR}/audio_worker.hpp"
    "${AUDIO_WORKER_DIR}/audio_worker.cpp")

set(_violations "")
set(_files_scanned 0)

foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip
        continue()
    endif()
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    if(_content MATCHES "[<\"]openvr[a-z_]*\\.h[>\"]"
            OR _content MATCHES "[^a-zA-Z0-9_]vr::"
            OR _content MATCHES "^vr::")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertAudioWorkerNoVrApi: ${_vcount} file(s) violate the no-vr::-in-audio-worker rule (D-07 / Pitfall 3):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertAudioWorkerNoVrApi: clean (${_files_scanned} files scanned)")
```

**Diverge from analog:**
- Variable name: `DETECTION_RUNNER_DIR` (replaces `AUDIO_WORKER_DIR`).
- `_targets` list: three files — `detection_runner.hpp`, `detection_runner.cpp`, `sample_ring.hpp`.
- All log/`message()` strings: `AssertDetectionRunnerNoVrApi` and `D-22` (replaces `AssertAudioWorkerNoVrApi` and `D-07`).
- Regex set: byte-identical (don't drift the regex; it mirrors `cmake/lint_no_openvr_in_core.cmake`'s P5 D-02 contract).

---

### `tests/driver/detection_settings_propagation_test.cpp` (test, headless integration)

**Analog:** `tests/driver/audio_worker_lifecycle_headless.cpp` — exact role match (headless driver-side test, plain-main exit-code convention, `MM_CHECK` macro shape).

#### Header / convention block (`audio_worker_lifecycle_headless.cpp:1-35`)

```cpp
/**
 * @file audio_worker_lifecycle_headless.cpp
 * @brief Phase 6 Wave 0 RED test for AudioWorker lifecycle invariants.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Matches
 * tests/test_command_queue.cpp / tests/test_vr_input_quit_ordering.cpp.
 */

#include "audio_worker.hpp"   // resolved via target_include_directories tests/CMakeLists.txt

#include <chrono>
#include <iostream>
#include <thread>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)
```

**Apply to detection_settings_propagation_test.cpp:** same file-prefix doc comment shape (P7 / MIG-06 / D-25(4)); same `MM_CHECK` macro; include `detection_runner.hpp`; same `namespace md = micmap::driver` alias.

#### Per-case test scaffold pattern (`audio_worker_lifecycle_headless.cpp:46-55`, `64-75`)

```cpp
{
    auto t0 = steady_clock::now();
    {
        md::AudioWorker w;
        (void)w;
    }
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);
    MM_CHECK(elapsed.count() < 100);
    std::cout << "PASS case_1_no_start_destructor_fast\n";
}
```

**Apply to MIG-06 propagation test:**
1. Construct DetectionRunner with a stub SampleRing + stub CommandQueue.
2. Start() the thread.
3. From a separate worker thread, call `runner.publish(newConfig)` and capture `t0 = steady_clock::now()`.
4. Spin-wait (or poll `runner.lastObservedConfigPtr()` test accessor — analogous to `state_for_test()` at `audio_worker.hpp:92`) until the detection thread has swapped to the new config.
5. `MM_CHECK((t1 - t0).count() < 50)` — the SC5 / MIG-06 < 50ms gate.
6. `std::cout << "PASS case_propagation_under_50ms\n"`.

#### Test-only accessor pattern (`audio_worker.hpp:84-92`)

```cpp
/**
 * @brief Test-only accessor for State, used by
 *        tests/driver/audio_worker_lifecycle_headless.cpp case 3
 *        (Pitfall 13 alive-before-shutdown ordering check).
 *
 * Test-only — may be deleted in P7 if unused. Does NOT widen the
 * production API surface (Start/Stop/IsRunning are the production
 * surface; State exposure is a zero-cost convenience).
 */
std::shared_ptr<State> state_for_test() const { return state_; }
```

**Apply to DetectionRunner:** add `std::shared_ptr<const DetectionConfig> active_config_for_test() const` (returns `std::atomic_load_explicit(&activeConfig_, …)`) — keeps test-only surface scoped, mirrors P6's precedent. The propagation test reads this on a tight poll loop (sleep 1ms between reads) until pointer-identity changes from the pre-publish snapshot.

---

### `tests/driver/device_provider_lifecycle_stress_test.cpp` (test, batch loop)

**Analog:** `tests/driver/audio_worker_lifecycle_headless.cpp` — same scaffold pattern but loop-driven, not single-cycle.

**Pattern to copy — outer harness (RED-tolerant compile-against-impl shape from `tests/CMakeLists.txt:150-167`):** test compiles `driver/src/device_provider.cpp` directly into the test exe (Pitfall 6 — never link `driver_micmap.dll` into a test). Gate on `OpenVR_FOUND` per the existing P6 precedent. Wave 0 RED-tolerance via `if(EXISTS …)` source-list conditional.

**Pattern to copy — handle-count audit shape:** the RESEARCH.md §"Pattern 4" sketch (`07-RESEARCH.md:469-499`) is the authoritative reference impl. Match the existing test convention: plain-main, exit 0/1, `std::cout << "PASS lifecycle_stress_50_cycles handles=…\n"`.

**Diverge from analog:**
- 50-iteration loop wrapping `DeviceProvider::Init(nullptr)` + `sleep_for(500ms)` + implicit `~DeviceProvider()` (per CONTEXT.md D-25(3)).
- Pre-loop and post-loop `GetProcessHandleCount(GetCurrentProcess(), &count)` calls; assert delta ≤ 5 (per RESEARCH.md §"Pattern 4" — accounts for thread-pool churn).
- Null-context fail-soft path: `Init(nullptr)` triggers DeviceProvider's settings-read path to fall through to default-false (matching the existing fail-soft semantics at `device_provider.cpp:85-95`); AudioWorker is not constructed, but the lifecycle teardown of the HTTP server, CommandQueue, and OpenVR-context cleanup is exercised across all 50 cycles.

---

### `driver/src/audio_worker.cpp` MODIFIED — callback rewire (D-05)

**Self-analog:** the existing `setAudioCallback` lambda at `audio_worker.cpp:241-263` is the file-scope precedent. P7 keeps the weak_ptr<State> + `alive` UAF guard verbatim; replaces the body.

**Existing pattern to preserve verbatim** (`audio_worker.cpp:237-263`):

```cpp
// RMS callback wired with weak_ptr alive-flag (Pitfall 13 / D-15 / D-16).
// The audio callback fires on the WASAPI internal capture thread (per
// audio_capture.cpp captureLoop). The weak_ptr lock + alive check at
// the head guarantees no UAF after Stop() flips alive=false.
std::weak_ptr<State> weak = state_;
capture_->setAudioCallback(
    [weak](const float* samples, size_t count) {
        auto sp = weak.lock();
        if (!sp || !sp->alive.load(std::memory_order_acquire)) {
            return;
        }
        sp->frames_seen.fetch_add(1, std::memory_order_relaxed);
        // ... (RMS-log body to be guarded behind MICMAP_DEBUG_RMS_LOG) ...
    });
```

**Diverge per D-05 (rewire shape):** see `07-RESEARCH.md:761-797` for the full reference rewrite. Capture additional pointers (`SampleRing<16,480>* ring_ptr`, `DetectionRunner* runner_ptr`); after the alive check, push to the ring (drop-OLDEST diagnostic per Pitfall 12 D-08 — emit one log per 100 drops, matching `kRmsBudget = 100` cadence at `audio_worker.cpp:55`); call `runner_ptr->NotifyOne()` if non-null; gate the RMS body behind `#ifdef MICMAP_DEBUG_RMS_LOG`.

**Pattern to preserve — drop-budget cadence convention** (`audio_worker.cpp:55-59` + `:256-262`):

```cpp
constexpr uint32_t kRmsBudget = 100;   // file-scope
// ...
const uint32_t emitted = sp->rms_logs_emitted.fetch_add(1, std::memory_order_relaxed);
if (emitted < kRmsBudget) {
    DriverLog("MicMap audio: rms[%u]=%.6f\n", emitted, rms);
}
```

**Apply to ring-drop diagnostic:** mirror the budgeted-log shape — one log per 100 drops with `MicMap detection: ring overflow drops=%u` line, matching the P6 D-08 RMS budget convention referenced in CONTEXT.md "Claude's Discretion".

---

### `driver/src/audio_worker.hpp` MODIFIED — ring + runner attach API

**Self-analog:** existing public surface at `audio_worker.hpp:51-92` (`Start`/`Stop`/`IsRunning`/`state_for_test`).

**Open Question 3 in RESEARCH.md (line 919-923):** ring ownership recommendation is "AudioWorker owns the ring; DetectionRunner takes a reference". DeviceProvider attaches DetectionRunner to AudioWorker after both are constructed.

**Pattern recommendation:** add ONE accessor `SampleRing<16, 480>& ring()` (mirrors the test-only `state_for_test()` shape — minimal API surface widening) and one attach setter `void SetDetectionRunner(DetectionRunner*)` so the audio callback's captured `runner_ptr` can be flipped non-null at construction-AFTER-construction time. SetDetectionRunner is a pointer-store under `mu_`; the audio callback re-reads via the captured pointer (which is a copy of the variable at lambda-capture time — so the setter has to either replace the lambda or store the pointer in a member that the lambda captures by-reference). PLAN.md picks the wiring; recommend "DetectionRunner pointer lives in `State` struct as `std::atomic<DetectionRunner*>`" so the existing weak_ptr<State> capture still applies.

---

### `driver/src/device_provider.{hpp,cpp}` MODIFIED — Init/Cleanup/Standby splice

**Self-analog:** the P6 D-13/D-14 implementation already lives in `device_provider.cpp`.

#### Init pattern — single-read VRSettings flag with default-on-error (`device_provider.cpp:78-96`) — LOAD-BEARING

```cpp
// D-01: read the flag once, here, on the vrserver thread. Single-read
// pattern matches v1.5 atomic-config-read shape (Pitfall 11). Default
// false on UnsetSettingHasNoDefault — SC4 safety net.
{
    vr::EVRSettingsError err = vr::VRSettingsError_None;
    driverAudioEnabled_ = vr::VRSettings()->GetBool(
        "driver_micmap", "enable_driver_audio", &err);
    if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
        driverAudioEnabled_ = false;   // explicit default per D-01 + SC4
        DriverLog("MicMap: enable_driver_audio unset, defaulting to false\n");
    } else if (err != vr::VRSettingsError_None) {
        DriverLog("MicMap: VRSettings GetBool(enable_driver_audio) error=%d\n",
                  static_cast<int>(err));
        driverAudioEnabled_ = false;
    } else {
        DriverLog("MicMap: enable_driver_audio = %s\n",
                  driverAudioEnabled_ ? "true" : "false");
    }
}
```

**Apply to P7 (Init step 5 per D-13/D-19):** add a parallel block that reads `enable_driver_detection` (bool, default false), `detection_sensitivity` (float via `GetFloat`, default 0.7), `detection_threshold` (float, default 0.6), `detection_cooldown_ms` (int32 via `GetInt32`, default 1000), `detection_min_duration_ms` (int32, default 200). Same `EVRSettingsError` checking, same `UnsetSettingHasNoDefault` → explicit-default pattern, same `MicMap:` log prefix.

#### Init pattern — fail-soft conditional construction (`device_provider.cpp:98-107`) — LOAD-BEARING

```cpp
// D-14: construct AudioWorker LAST so an audio failure does not corrupt
// the v1.5 trigger path. D-03: when flag is OFF, never construct the
// worker — no thread, no COM, no WASAPI. Byte-identical to Phase 5.
if (driverAudioEnabled_) {
    audioWorker_ = std::make_unique<AudioWorker>();
    if (!audioWorker_->Start()) {
        DriverLog("MicMap: AudioWorker::Start failed — continuing without audio\n");
        audioWorker_.reset();   // do NOT fail Init — v1.5 trigger path stays alive
    }
}
```

**Apply to P7 (Init step 7 per D-19):** mirror exactly. Construct DetectionRunner only if `driverDetectionEnabled_ && audioWorker_` (per D-19 fail-soft when audio is unavailable). Same fail-soft `reset()` on Start failure, same `do NOT fail Init` invariant. Log line: `"MicMap: enable_driver_detection requires enable_driver_audio — skipping detection construction"` per CONTEXT.md D-19.

#### Cleanup pattern — reverse-order with `reset()` first (`device_provider.cpp:120-145`) — LOAD-BEARING

```cpp
// D-13 step 1: AudioWorker FIRST (reverse construction order). Destructor
// sets state->alive=false, signals shutdown CV, joins thread with 2 s
// watchdog. Worker thread itself runs the WASAPI/COM teardown on its own
// apartment (Pitfall 4 — IMMNotificationClient unregister BEFORE COM
// Release, both on the same thread that did the register).
if (audioWorker_) {
    audioWorker_.reset();
}

// D-13 step 2-onwards: existing v1.5 sequence unchanged.
if (httpServer_) {
    httpServer_->Stop();
    httpServer_.reset();
}
commandQueue_.reset();

hSystemClick_ = k_ulInvalidInputComponentHandle;
state_ = HmdComponentState::NotReady;
// ...
driverAudioEnabled_ = false;   // P6 — symmetry with the Init-time read
initialized_ = false;

VR_CLEANUP_SERVER_DRIVER_CONTEXT();
```

**Apply to P7 (Cleanup step 1 per D-20) — PREPEND, do not replace:** add `if (detectionRunner_) detectionRunner_.reset();` BEFORE the `audioWorker_.reset()` call. Add `driverDetectionEnabled_ = false;` to the symmetry-with-Init reset block (mirror `driverAudioEnabled_ = false;`).

#### Standby splice (`device_provider.cpp:253-259`) — LOAD-BEARING

```cpp
void DeviceProvider::EnterStandby() {
    DriverLog("MicMap driver entering standby\n");
}

void DeviceProvider::LeaveStandby() {
    DriverLog("MicMap driver leaving standby\n");
}
```

**Apply to P7 (D-21 — confirmed `EnterStandby`/`LeaveStandby` over `VREvent_TrackedDeviceDeactivated`):** extend the bodies; preserve the existing `DriverLog` lines. Add `if (detectionRunner_) detectionRunner_->Pause();` (resp. `Resume()`) and a follow-up DriverLog. Reference impl in `07-RESEARCH.md:430-448`.

#### RunFrame — does NOT need modification

The existing RunFrame (`device_provider.cpp:156-247`) already drains the CommandQueue (line 220) — DetectionRunner's `commandQueue_->push(TapCommand{})` is consumed through the same `try_pop` loop. No splice needed inside RunFrame proper; only the standby callbacks change.

#### Header member additions (`device_provider.hpp:75-86`) — extension pattern

Existing P6 pattern (`device_provider.hpp:79-86`):

```cpp
// P6 D-01/D-03/D-14: driver-side audio capture spike.
// driverAudioEnabled_ holds the result of the single Init-time
// vr::VRSettings()->GetBool("driver_micmap","enable_driver_audio") read.
// audioWorker_ is constructed LAST in Init when the flag is true and
// reset FIRST in Cleanup (reverse construction order, Pitfall 4).
bool                          driverAudioEnabled_{false};
std::unique_ptr<AudioWorker>  audioWorker_;
```

**Apply to P7:** append parallel members with parallel doc-comment block — `bool driverDetectionEnabled_{false}`, `DetectionConfig detectionDefaults_{}` (cached snapshot of VRSettings reads), `std::unique_ptr<DetectionRunner> detectionRunner_`. Forward-declare `class DetectionRunner;` near the existing `class AudioWorker;` forward decl at `device_provider.hpp:27`.

---

### `driver/src/http_server.cpp` MODIFIED — `/health` JSON extension (D-09)

**Self-analog:** existing `/health` route handler at `http_server.cpp:154-157`.

**Pattern to extend** (`http_server.cpp:154-157`):

```cpp
// GET /health — liveness + port-probe endpoint (used by DriverClient).
server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"healthy"})", "application/json");
});
```

**Existing nlohmann::json usage in the same file** (`http_server.cpp:130-138, :148-150`):

```cpp
auto body = nlohmann::json::parse(req.body);
if (!body.contains("kind")) {
    res.status = 400;
    res.set_content(R"({"error":"missing \"kind\" field"})",
                    "application/json");
    return;
}
const auto kind = body.at("kind").get<std::string>();
// ...
} catch (const nlohmann::json::exception&) {
    res.status = 400;
    res.set_content(R"({"error":"malformed JSON body"})",
                    "application/json");
}
```

**Apply to P7 (D-09):** rewrite the `/health` lambda to construct a `nlohmann::json` body, set `body["status"] = "healthy"`, set `body["driver_detection_active"] = driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false`, then `res.set_content(body.dump(), "application/json")`. Reference impl in `07-RESEARCH.md:807-813`. Existing `nlohmann/json.hpp` include at `http_server.cpp:18` is already in place — no new dep.

#### HttpServer ctor wiring — Open Question 1

RESEARCH.md §"Open Questions" #1 recommends passing `std::function<bool()> driverDetectionActiveGetter` at construction (matches the existing ctor shape `HttpServer(CommandQueue& queue, …)` at `http_server.hpp:44-46`). DeviceProvider passes a lambda capturing `[this]{ return driverDetectionEnabled_ && audioWorker_ && detectionRunner_ && detectionRunner_->IsRunning(); }` per D-09.

**Existing ctor pattern** (`http_server.hpp:44-46` + `http_server.cpp:26-32`):

```cpp
explicit HttpServer(CommandQueue& queue,
                    int port = 27015,
                    const std::string& host = "127.0.0.1");

// http_server.cpp:
HttpServer::HttpServer(CommandQueue& queue, int port, const std::string& host)
    : queue_(queue)
    , port_(port)
    , host_(host)
{ ... }
```

**Apply to P7:** add a fourth param `std::function<bool()> driverDetectionActiveGetter = nullptr` (nullable so test code can construct without DeviceProvider). Initialize a `std::function<bool()> driverDetectionActiveGetter_;` member; the `/health` handler defensively null-checks before calling.

---

### `driver/resources/settings/default.vrsettings` MODIFIED — append 5 keys (D-13)

**Self-analog:** existing 4 keys at `default.vrsettings:2-7`.

**Existing content (whole file is 8 lines):**

```json
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1",
        "enable_driver_audio": false
    }
}
```

**Apply to P7 (D-13):** append five keys per the schema. Final shape (per `07-RESEARCH.md:863-876`):

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

**Defaults mirror v1.5 client-side `DetectionConfig` defaults** (`src/core/include/micmap/core/config_manager.hpp:28-33`):

```cpp
struct DetectionConfig {
    float sensitivity = 0.7f;           ///< Detection sensitivity (0.0 to 1.0)
    int minDurationMs = 300;            ///< Minimum detection duration in ms
    int cooldownMs = 300;               ///< Cooldown after trigger in ms
    int fftSize = 2048;                 ///< FFT window size
};
```

**Note:** P7's `default.vrsettings` defaults differ from the v1.5 `DetectionConfig` struct on `minDurationMs` (200 vs 300) and `cooldownMs` (1000 vs 300). These differences are LOCKED per CONTEXT.md D-13. Document the value-mirror in PLAN.md per the "Specifics" section warning.

---

### `driver/CMakeLists.txt` MODIFIED — register `detection_runner.cpp`

**Self-analog:** existing source list at `driver/CMakeLists.txt:23-28`.

**Existing pattern:**

```cmake
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
    src/audio_worker.cpp     # P6 D-05: driver-side audio capture worker
)
```

**Apply to P7:** append one line `src/detection_runner.cpp     # P7 D-17: driver-side detection thread`. `sample_ring.hpp` is header-only (no source registration) — the existing `target_include_directories` at line 31-34 already covers `src/`. No new link deps (per CONTEXT.md D-17 + `driver/CMakeLists.txt:81` — `micmap::core_runtime` already linked PRIVATE since P5).

---

### `tests/CMakeLists.txt` MODIFIED — register tests + new lint

**Self-analog:** the P6 registration block at `tests/CMakeLists.txt:126-176`.

#### P6 audio worker test gated on `OpenVR_FOUND` pattern (`tests/CMakeLists.txt:150-167`)

```cmake
set(_p6_audio_worker_sources driver/audio_worker_lifecycle_headless.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
    list(APPEND _p6_audio_worker_sources
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_audio_worker_lifecycle_headless ${_p6_audio_worker_sources})
    target_compile_features(test_audio_worker_lifecycle_headless PRIVATE cxx_std_17)
    target_include_directories(test_audio_worker_lifecycle_headless PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_audio_worker_lifecycle_headless PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api)
    add_test(NAME AudioWorkerLifecycleHeadless
        COMMAND test_audio_worker_lifecycle_headless)
else()
    message(STATUS "AudioWorkerLifecycleHeadless: skipped (OpenVR SDK not found)")
endif()
```

**Apply to P7 — `DetectionSettingsPropagation`:** mirror exactly. Source list = `tests/driver/detection_settings_propagation_test.cpp` plus conditional `${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp` (RED-tolerant — Wave 0 has no impl yet); link `micmap::core_runtime` + `OpenVR::openvr_api` (DetectionRunner transitively pulls `driver_log.hpp` → `<openvr_driver.h>` per the same Rule 3 fix called out in the P6 comment at lines 142-149).

**Apply to P7 — `DeviceProviderLifecycleStress`:** same shape; source list = `tests/driver/device_provider_lifecycle_stress_test.cpp` plus conditional `device_provider.cpp` + `audio_worker.cpp` + `detection_runner.cpp` + `http_server.cpp` (full DeviceProvider lifecycle compile-in). All gated on `OpenVR_FOUND`.

#### P6 lint registration pattern (`tests/CMakeLists.txt:169-176`) — MIRROR THIS

```cmake
# P6 D-07 / SVR-05: assert no vr::* in driver/src/audio_worker.{hpp,cpp}.
# Pure CMake script-mode (-P) lint, no compiled exe. Wave 0 RED-tolerant —
# the lint script's skip-on-NOT-EXISTS branch keeps this GREEN before the
# audio_worker source files land in Plan 06-02.
add_test(NAME AssertAudioWorkerNoVrApi
    COMMAND ${CMAKE_COMMAND}
        -DAUDIO_WORKER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertAudioWorkerNoVrApi.cmake)
```

**Apply to P7:** add a parallel `add_test(NAME AssertDetectionRunnerNoVrApi …)` block — same `-D` shape, replace `AUDIO_WORKER_DIR` with `DETECTION_RUNNER_DIR`, point `-P` at the new sibling lint script.

---

### `tests/test_command_queue.cpp` MODIFIED — two-producer concurrent push

**Self-analog:** existing single-thread test cases at `test_command_queue.cpp:10-36`.

**Existing pattern** (`test_command_queue.cpp:24-36`):

```cpp
static void test_drop_oldest_on_overflow() {
    CommandQueue q;
    for (int i = 0; i < 8; ++i) {
        bool dropped = q.push(TapCommand{});
        assert(!dropped);
    }
    bool dropped = q.push(TapCommand{});   // 9th -- forces drop-oldest
    assert(dropped);
    int count = 0;
    while (q.try_pop()) ++count;
    assert(count == 8);
}
```

**Apply to P7 (per RESEARCH.md §"Wave 0 Gaps" item 5):** add a new `test_concurrent_two_producer_push()` case spawning two `std::thread`s each calling `q.push(TapCommand{})` 1000× concurrently; assert no crash and total queue depth never exceeds `kMaxDepth` (use `try_pop` drain after both threads join). The existing `std::lock_guard<std::mutex>` at `command_queue.hpp:20, :29` already protects concurrent push — this test verifies the existing impl supports the new P7 use case (DetectionRunner thread + HTTP thread as concurrent producers).

---

### `apps/micmap/main.cpp` MODIFIED — `/health` poll suppression (D-10)

**Self-analog:** existing trigger site at `main.cpp:515-523` (`onTrigger`).

**Existing pattern:**

```cpp
void MicMapApp::onTrigger() {
    if (!driverClient || !driverClient->isConnected()) {
        MICMAP_LOG_DEBUG("onTrigger: driver not connected, skipping");
        return;
    }
    if (!driverClient->tap()) {
        MICMAP_LOG_WARNING("onTrigger failed: ", driverClient->getLastError());
    }
}
```

**Apply to P7 (D-10):** add a poll-`/health.driver_detection_active` check before the `tap()` call. Recommended wiring per RESEARCH.md §"Open Questions" #1 / Open-impl in `07-RESEARCH.md`: extend `IDriverClient` (`src/steamvr/include/micmap/steamvr/vr_input.hpp:170-186`) with a new method `bool isDriverDetectionActive()` whose impl polls `GET /health` and parses `driver_detection_active`. Cache the result for ≤ 1s to keep `onTrigger`'s latency budget bounded.

**Existing `/health` poll precedent** (`src/steamvr/src/vr_input.cpp:142-148` — DriverClient::connect):

```cpp
auto res = client.Get("/health");
if (res && res->status == 200) {
    port_ = port;
    connected_ = true;
    MICMAP_LOG_INFO("Connected to MicMap driver on port ", port_);
    return true;
}
```

**Apply to P7 — new `isDriverDetectionActive()` method:** add to `vr_input.hpp` interface near line 186 (next to `getStatus()`); impl in `vr_input.cpp` near line 200 mirrors the existing `httplib::Client client(host_, port_); client.set_connection_timeout(2); auto res = client.Get("/health");` shape; parse `nlohmann::json` body and return `body.value("driver_detection_active", false)`. Cache via member with `std::chrono::steady_clock` last-poll timestamp. P10 deletes this whole method per CONTEXT.md D-12.

**Trigger-site diff** (per D-10 — minimal):

```cpp
void MicMapApp::onTrigger() {
    if (!driverClient || !driverClient->isConnected()) { /* unchanged */ return; }
    // P7 D-10: suppress local trigger when driver owns the detection path.
    // Cooldown (state machine) is the belt-and-suspenders backstop per D-11.
    if (driverClient->isDriverDetectionActive()) {
        MICMAP_LOG_DEBUG("onTrigger: driver_detection_active=true, suppressing");
        return;
    }
    if (!driverClient->tap()) {
        MICMAP_LOG_WARNING("onTrigger failed: ", driverClient->getLastError());
    }
}
```

---

## Shared Patterns

These cross-cutting patterns apply to multiple new/modified files. Plans should reference these once and then point per-task at the per-file pattern assignment above.

### Shared Pattern 1: `MicMap:` log prefix convention

**Source:** `audio_worker.cpp:65-67, :86, :104-115, :138-139, :151-152` (and pervasively throughout `device_provider.cpp`).

**Apply to:** `detection_runner.cpp` (use `MicMap detection:` per CONTEXT.md "Claude's Discretion"), modified portions of `device_provider.cpp` (continue using `MicMap:`), modified portions of `http_server.cpp` (continue using `MicMap:` / no module-tag — matches existing `/health` handler).

```cpp
DriverLog("MicMap: AudioWorker thread spawned\n");
DriverLog("MicMap: AudioWorker thread joined cleanly\n");
DriverLog("MicMap audio: rms[%u]=%.6f\n", emitted, rms);
```

The `MicMap audio:` / `MicMap detection:` two-tier prefix lets `vrserver.txt` greps cleanly partition by subsystem. Already-shipped P6 lines used `MicMap audio:`; P7 introduces `MicMap detection:` (matches CONTEXT.md "use the same MicMap detection: prefix convention as P6's MicMap audio:").

### Shared Pattern 2: VRSettings single-read with explicit-default fallback

**Source:** `device_provider.cpp:78-96` (P6 D-01).

**Apply to:** all 5 new VRSettings reads in P7 Init step 5 (D-13).

```cpp
vr::EVRSettingsError err = vr::VRSettingsError_None;
<member> = vr::VRSettings()->GetBool/GetFloat/GetInt32(
    "driver_micmap", "<key>", &err);
if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
    <member> = <explicit-default>;
    DriverLog("MicMap: <key> unset, defaulting to ...\n");
} else if (err != vr::VRSettingsError_None) {
    DriverLog("MicMap: VRSettings ... error=%d\n", static_cast<int>(err));
    <member> = <safe-default>;
} else {
    DriverLog("MicMap: <key> = %s/%.2f/%d\n", ...);
}
```

Pitfall 11 anchor: single read at Init, no hot reload. Runtime mutation flows through `DetectionRunner::publish()` (D-15) — never re-reads VRSettings.

### Shared Pattern 3: `weak_ptr<State>` + `atomic<bool> alive` UAF guard

**Source:** `audio_worker.hpp:67-92` + `audio_worker.cpp:241-263` (P6 D-15/D-16, Pitfall 13 mitigation).

**Apply to:** the rewired audio callback in `audio_worker.cpp` (preserve verbatim — only the body inside the alive-check changes per D-05). DetectionRunner does NOT add its own notifier surface (D-23) so this pattern is NOT replicated; only the audio cb retains it.

```cpp
struct State {
    std::atomic<bool>     alive{true};
    std::atomic<uint32_t> rms_logs_emitted{0};
    std::atomic<uint32_t> frames_seen{0};
};

std::weak_ptr<State> weak = state_;
capture_->setAudioCallback(
    [weak](const float* samples, size_t count) {
        auto sp = weak.lock();
        if (!sp || !sp->alive.load(std::memory_order_acquire)) return;
        // ... body ...
    });
```

If the State struct grows a DetectionRunner pointer per Open Question 3 recommendation, add it here as `std::atomic<DetectionRunner*> runner_ptr{nullptr}` — preserves the alive-flag UAF discipline transitively.

### Shared Pattern 4: Reverse-order teardown with strict comments

**Source:** `device_provider.cpp:113-150` (Cleanup) + `audio_worker.cpp:289-303` (worker-thread teardown).

**Apply to:** `DeviceProvider::Cleanup` extension (D-20 — DetectionRunner FIRST, AudioWorker SECOND), `DetectionRunner::~/Stop` thread teardown order (`detector_.reset()` before `stateMachine_.reset()`), and the `DeviceProvider::Init` failure paths (any branch that returns early after partial construction must reset members in reverse — the existing P5/P6 code at `device_provider.cpp:71-75` is the precedent).

The "comment-as-load-bearing" convention: every reset block should carry a doc comment identifying the D-decision and the Pitfall it mitigates, so future readers see the ordering invariant immediately. P6 examples include the comments at `device_provider.cpp:120-124` (8 lines of context for a 3-line block) and `audio_worker.cpp:160-165` (5-line "load-bearing comment" preceding `createWASAPICapture()`).

### Shared Pattern 5: Wave 0 RED-tolerance — `if(EXISTS …)` + skip-on-NOT-EXISTS

**Source:** `cmake/AssertAudioWorkerNoVrApi.cmake:43-50` (lint script) + `tests/CMakeLists.txt:151-154` (test source list).

**Apply to:** the new `cmake/AssertDetectionRunnerNoVrApi.cmake` lint (skip-on-NOT-EXISTS for all three target files); the new test executables in `tests/CMakeLists.txt` (conditional source-list append per file). This keeps Wave 0 / RED state green at configure-time so CTest can run while the impl files are still missing — the build-time compile failure of the test exe IS the Nyquist gate, not the lint or the configure step.

### Shared Pattern 6: Test exe compiles impl TU directly, never links DLL (Pitfall 6)

**Source:** `tests/CMakeLists.txt:74-84` (test_tray_balloon_once) + `tests/CMakeLists.txt:150-167` (test_audio_worker_lifecycle_headless).

**Apply to:** both new tests (`detection_settings_propagation`, `device_provider_lifecycle_stress`). Compile `driver/src/detection_runner.cpp` (and others as needed) directly into the test exe; link `micmap::core_runtime` + `OpenVR::openvr_api`; never `target_link_libraries(... PRIVATE driver_micmap)`. The P6 comment at `tests/CMakeLists.txt:127-131` is explicit about this:

```cmake
# AudioWorker lifecycle invariants. Compiles audio_worker.cpp directly into
# the test exe — never link driver_micmap.dll into a test binary (Pitfall 6).
```

---

## No Analog Found

**None.** Every P7 file has a strong in-tree analog (P6's AudioWorker for the thread-owner shape; v1.5's CommandQueue for the queue-with-drop-OLDEST contract; P5's `lint_no_openvr_in_core.cmake` and P6's `AssertAudioWorkerNoVrApi.cmake` for the lint script shape; P5's `test_command_queue.cpp` and P6's `audio_worker_lifecycle_headless.cpp` for the test scaffold). DetectionRunner is exceptionally well-precedented — its skeleton is `AudioWorker` with the loop body swapped.

The closest thing to "no analog" is the SPSC ring concurrency model itself (lock-free with acquire/release fences), which has no in-tree precedent. CONTEXT.md and RESEARCH.md acknowledge this and provide a full reference impl in `07-RESEARCH.md:308-373` derived from rigtorp/SPSCQueue and cppreference's acquire/release primer. The planner should treat this section of RESEARCH.md as the analog (since no in-tree code exists) and copy from the sketch directly.

---

## Metadata

**Analog search scope:**
- `driver/src/` — all `.{hpp,cpp}` files (8 files total — every one read or grep-scanned)
- `cmake/` — all `.cmake` lint scripts
- `tests/` — `tests/CMakeLists.txt`, `tests/test_command_queue.cpp`, `tests/driver/audio_worker_lifecycle_headless.cpp`
- `src/{audio,detection,core,steamvr}/include/` — interface headers for factories called by DetectionRunner
- `apps/micmap/main.cpp` — client trigger site (lines 515-523)

**Files read in full or in targeted ranges:**
- `driver/src/audio_worker.{hpp,cpp}` (read in full)
- `driver/src/device_provider.{hpp,cpp}` (read in full)
- `driver/src/command_queue.hpp` (read in full)
- `driver/src/http_server.{hpp,cpp}` (read in full)
- `driver/resources/settings/default.vrsettings` (read in full — 8 lines)
- `driver/CMakeLists.txt` (read in full)
- `tests/CMakeLists.txt` (read in full)
- `tests/test_command_queue.cpp` (read in full)
- `tests/driver/audio_worker_lifecycle_headless.cpp` (read in full)
- `cmake/AssertAudioWorkerNoVrApi.cmake` (read in full)
- `src/detection/include/micmap/detection/noise_detector.hpp` (read in full — 149 lines)
- `src/core/include/micmap/core/state_machine.hpp` (read in full — 142 lines)
- `src/core/include/micmap/core/config_manager.hpp` (read targeted — DetectionConfig at lines 28-33)
- `src/audio/include/micmap/audio/audio_capture.hpp` (read targeted — getSampleRate at line 93)
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` (read targeted — IDriverClient at lines 160-216)
- `src/steamvr/src/vr_input.cpp` (read targeted — DriverClient impl at lines 120-210)
- `apps/micmap/main.cpp` (grep + targeted read — trigger site at lines 515-523)

**Pattern extraction date:** 2026-05-02

**Branch:** `hmd-button` (verified — all line numbers re-checked against the current worktree, which matches the recent commit `ad3da4d docs(state): record phase 7 context session`).

---

## PATTERN MAPPING COMPLETE

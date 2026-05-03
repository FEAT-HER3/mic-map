# Phase 6: Driver-Side Audio Capture Spike — Pattern Map

**Mapped:** 2026-05-02
**Files analyzed:** 7 (2 NEW source + 3 MODIFIED source/config + 1 NEW cmake guard + 1 NEW headless test + ctest registrations)
**Analogs found:** 7 / 7 (full coverage — every new/modified file has an in-tree analog)

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `driver/src/audio_worker.hpp` (NEW) | driver-only header (lifecycle owner class) | event-driven (audio callback → atomic counters; cv shutdown signal) | `driver/src/http_server.hpp` (Start/Stop owner of `std::thread`) + `driver/src/command_queue.hpp` (atomic + mutex shape) | role-match (best-available; no existing thread+cv+atomic-shutdown class in tree) |
| `driver/src/audio_worker.cpp` (NEW) | driver-only impl (thread entry + COM init + capture lifecycle) | event-driven (worker thread; no v1.5 path resemblance) | `driver/src/http_server.cpp` (thread spawn + Start/Stop watchdog) + `src/audio/src/audio_capture.cpp:519-532` (CoInitializeEx-on-thread + thread-loop pattern) | role-match (composite: HttpServer for Start/Stop, audio_capture for the COM-on-worker pattern) |
| `driver/src/device_provider.hpp` (MODIFY) | driver class header (add 2 members) | request-response (Init/Cleanup) | self — current file lines 25-99 | exact (inline modification of existing class shape) |
| `driver/src/device_provider.cpp` (MODIFY) | driver class impl (extend Init + Cleanup) | request-response (vrserver→driver lifecycle) | self — current file `Init` (lines 58-79) and `Cleanup` (lines 81-107) | exact (insert points are explicit in the existing file) |
| `driver/resources/settings/default.vrsettings` (MODIFY) | driver-config blob (JSON consumed by `vr::VRSettings()`) | config-read | self — current file lines 1-7 | exact (one-key extension to existing `driver_micmap` section) |
| `driver/CMakeLists.txt` (MODIFY) | build config (add a TU to existing target) | build | self — current file line 23-27 (`add_library(driver_micmap SHARED ...)`) | exact |
| `cmake/AssertAudioWorkerNoVrApi.cmake` (NEW, Wave 0) | configure-time / ctest source-grep guard | build-time lint (file-grep) | `cmake/lint_no_openvr_in_core.cmake` (P5 D-02) | exact (same script-mode pattern, narrower scope) |
| `tests/driver/audio_worker_lifecycle_headless.cpp` (NEW, Wave 0) | unit test (driver-side, headless) | event-driven test (Start → wait → Stop, assert join ≤2 s) | `tests/test_command_queue.cpp` (driver-include test pattern; no GTest) + `tests/test_vr_input_quit_ordering.cpp` (stub-injection ordering test) | role-match (composite: command-queue test for the driver-include + plain-main shape, VR-quit test for the multi-case structure) |
| `tests/CMakeLists.txt` + root `CMakeLists.txt` (MODIFY) | build config (register new ctests) | build | `tests/CMakeLists.txt:38-42` (existing `test_command_queue` registration) and `tests/CMakeLists.txt:116-124` (existing P5 lint registration) | exact |

---

## Pattern Assignments

### `driver/src/audio_worker.hpp` (NEW — driver-only header)

**Analog A:** `driver/src/http_server.hpp` (lines 1-71) — closest sibling: a driver-only class that owns a `std::thread` plus `Start`/`Stop` lifecycle, no OpenVR includes in the header, forward-declares heavy deps.

**Analog B:** `driver/src/command_queue.hpp` (lines 1-42) — closest in-tree usage of `std::atomic`+`std::mutex` for thread-safe shared state in driver-only code.

**File-banner pattern** (mirror `http_server.hpp:1-9`):
```cpp
/**
 * @file audio_worker.hpp
 * @brief Driver-side audio capture worker (Phase 6 spike).
 *
 * Owns its own std::thread, calls CoInitializeEx(MTA) inside the thread
 * entry, and constructs WASAPIAudioCapture on the worker thread so its
 * inner CoInitializeEx lands on the same MTA apartment (Pitfall 1).
 * No vr::* API surface is touched from the worker thread (SVR-05 / D-07).
 */
```

**Forward-decl + namespace pattern** (copy `http_server.hpp:10-26` shape):
```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Forward-declare to avoid pulling micmap/audio/* into the header.
namespace micmap::audio { class IAudioCapture; }

namespace micmap::driver {
```

**Member-layout pattern** (copy private-section discipline from `http_server.hpp:57-68`):
```cpp
private:
    // Shared with the worker thread + the audio callback. weak_ptr capture
    // checks alive before touching any state (Pitfall 13 mitigation, D-16).
    std::shared_ptr<State>        state_;

    std::unique_ptr<micmap::audio::IAudioCapture> capture_;  // owned by worker thread
    std::thread                   thread_;
    std::mutex                    mu_;
    std::condition_variable       cv_;
    std::atomic<bool>             shutdown_{false};
```

**Public API pattern** (mirror `http_server.hpp:36-55` Start/Stop shape):
```cpp
class AudioWorker {
public:
    AudioWorker();
    ~AudioWorker();   // sets state_->alive=false, signals cv_, joins with 2s watchdog

    bool Start();     // spawns thread_; returns true if joinable
    void Stop();      // idempotent; called by ~AudioWorker
};
```

**Delta vs analog:** unlike `HttpServer`, the worker is never given a queue reference (no producer wiring in P6 — D-07 / D-09). The destructor must implement the 2 s watchdog (D-13) — `HttpServer::Stop` simply joins, so the watchdog logic is new and must be derived from the v1.5 `VREvent_Quit` precedent. Use `cv_.wait_for(2s, [&]{ return thread-finished-flag; })` then last-resort `thread_.detach()` if the wait expires.

---

### `driver/src/audio_worker.cpp` (NEW — driver-only impl)

**Analog A — Start/Stop + thread spawn:** `driver/src/http_server.cpp:38-122`. The Start/Stop shape, "set running_, spawn thread, sleep-and-check, log on every state transition" pattern.

**Analog B — CoInitializeEx-on-worker-thread loop:** `src/audio/src/audio_capture.cpp:519-532` (the existing inner `captureLoop`).

**Analog C — RPC_E_CHANGED_MODE-aware COM init handling:** `src/audio/src/audio_capture.cpp:195-198` (existing `WASAPIAudioCapture` ctor). P6 must be **stricter** — distinguish `RPC_E_CHANGED_MODE` from generic failure (D-06).

**Imports pattern** (mirror `http_server.cpp:1-18` shape, replacing httplib/json with audio + COM headers):
```cpp
/**
 * @file audio_worker.cpp
 * @brief Implementation of the driver-side audio capture worker.
 *
 * The worker thread owns CoInitializeEx(MTA) for its own apartment, then
 * constructs WASAPIAudioCapture (whose inner CoInitializeEx returns S_FALSE
 * — same apartment), wires an RMS-logging audio callback, waits on the
 * shutdown CV, and tears down in reverse order on signal. Pitfalls 1, 4, 13.
 */

#include "audio_worker.hpp"
#include "driver_log.hpp"
#include <micmap/audio/audio_capture.hpp>   // P5 link-only restriction lifted in P6

#ifdef _WIN32
#include <Windows.h>     // CoInitializeEx, CoUninitialize
#include <Objbase.h>     // RPC_E_CHANGED_MODE constant
#endif

#include <chrono>
#include <cmath>
```

**Thread-entry COM init pattern (copy then strengthen from `audio_capture.cpp:195-198`):**

Existing analog (lenient — does NOT distinguish `RPC_E_CHANGED_MODE`):
```cpp
// src/audio/src/audio_capture.cpp:195-198
HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE;
```

P6 driver-side strengthening (D-06 — three-bucket handling):
```cpp
// driver/src/audio_worker.cpp ThreadEntry()
HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
if (hr == RPC_E_CHANGED_MODE) {
    DriverLog("MicMap: audio worker thread already in another COM apartment "
              "(RPC_E_CHANGED_MODE = 0x80010106) — bailing out\n");
    return;   // thread exits; AudioWorker join sees finished thread
}
if (FAILED(hr) && hr != S_FALSE) {
    DriverLog("MicMap: audio worker CoInitializeEx failed hr=0x%08X\n",
              static_cast<unsigned>(hr));
    return;
}
DriverLog("MicMap: audio worker thread COM apartment = MTA (hr=0x%08X)\n",
          static_cast<unsigned>(hr));
```

**Capture-construction-on-worker-thread pattern (Pitfall 1, D-04):**
```cpp
// driver/src/audio_worker.cpp — construct AFTER our own CoInitializeEx succeeds.
// WASAPIAudioCapture's ctor (audio_capture.cpp:186-220) calls CoInitializeEx
// again; it returns S_FALSE (already-init same apartment) and the existing
// `comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE` accepts it.
auto capture = micmap::audio::createWASAPICapture();
if (!capture) {
    DriverLog("MicMap: createWASAPICapture returned null\n");
    ::CoUninitialize();
    return;
}
```

**RMS callback wired with weak_ptr alive-flag (Pitfall 13, D-16):**
```cpp
std::weak_ptr<State> weak = state_;
capture->setAudioCallback(
    [weak](const float* samples, size_t count) {
        auto sp = weak.lock();
        if (!sp || !sp->alive.load(std::memory_order_acquire)) return;
        double sumSq = 0.0;
        for (size_t i = 0; i < count; ++i) sumSq += samples[i] * samples[i];
        float rms = static_cast<float>(std::sqrt(sumSq / std::max<size_t>(count, 1)));
        uint32_t emitted = sp->rms_logs_emitted.fetch_add(1, std::memory_order_relaxed);
        if (emitted < kRmsBudget) {
            DriverLog("MicMap audio: rms=%.6f (sample %u)\n", rms, emitted);
        }
    });
```

**Shutdown wait + reverse-order teardown (Pitfall 4, D-13) — mirror the wait/exit shape from `audio_capture.cpp:523-532` but replace WaitForSingleObject with cv:**
```cpp
// Wait on shutdown signal.
{
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this]{ return shutdown_.load(); });
}

// Reverse-order teardown — ALL on this same worker thread.
capture->stopCapture();   // joins WASAPI inner captureThread_
capture.reset();          // ~WASAPIAudioCapture: Unregister + Release + CoUninitialize (audio_capture.cpp:222-235)
::CoUninitialize();       // matches our own CoInitializeEx above
DriverLog("MicMap: audio worker thread exiting cleanly\n");
```

**Stop()/destructor 2 s watchdog pattern (D-13)** — analog: `HttpServer::Stop()` at `http_server.cpp:102-122` joins unconditionally; **strengthen** with timed wait:
```cpp
void AudioWorker::Stop() {
    if (state_) state_->alive.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_.store(true);
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        // 2 s watchdog matching v1.5 VREvent_Quit precedent.
        std::thread watchdog([this]{
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // If thread_ still hasn't exited, log + detach. Implementation
            // detail: use a finished-flag atomic the thread sets just before
            // returning, polled here, OR simply join() and trust DriverLog
            // ordering to catch overrun. Match v1.5 watchdog precedent.
        });
        thread_.join();
        if (watchdog.joinable()) watchdog.detach();
    }
}
```

**Delta vs analog:** `HttpServer::Stop` does not implement a watchdog because httplib's `server_->stop()` is documented bounded; WASAPI teardown is not. P6 must ship the watchdog (D-13).

---

### `driver/src/device_provider.hpp` (MODIFY — add 2 members)

**Analog (self):** existing class shape lines 13-99.

**Forward-decl block** (insert after existing line 27 `class CommandQueue;` block):
```cpp
// driver/src/device_provider.hpp:25-27 EXISTING:
class HttpServer;
class CommandQueue;

// P6 ADD:
class AudioWorker;
```

**Header include**: keep `#include <openvr_driver.h>` and `<atomic> <chrono> <memory> <optional>` block at lines 15-20 unchanged. No new includes needed (forward-decl pattern matches HttpServer/CommandQueue).

**Member additions** (insert within private section, after existing line 76 `std::atomic<bool> initialized_{false};`):
```cpp
// driver/src/device_provider.hpp:74-76 EXISTING:
std::unique_ptr<CommandQueue> commandQueue_;
std::unique_ptr<HttpServer>   httpServer_;
std::atomic<bool>             initialized_{false};

// P6 ADD (D-13 ordering: declared AFTER httpServer_ so destructor order
// would naturally tear down audioWorker_ FIRST — but Cleanup()'s explicit
// reset sequence is what actually enforces D-13's reverse order, since the
// destructor calls Cleanup() at line 55 which runs the explicit sequence).
bool                          driverAudioEnabled_{false};
std::unique_ptr<AudioWorker>  audioWorker_;
```

**Delta vs analog:** zero — pure additive; no method-signature changes; no constexpr changes. Existing tap-hold constants (`kTapHold`, `kMaxHold`, lines 95-98) untouched.

---

### `driver/src/device_provider.cpp` (MODIFY — extend Init + Cleanup only)

**Analog (self):** existing `Init` (lines 58-79) and `Cleanup` (lines 81-107). RunFrame at lines 113-204 is **untouched** in P6 (SVR-05 invariant).

**Init insertion point** — extend the existing block at lines 69-78 (between HttpServer construction and the `initialized_ = true;` line):

EXISTING (lines 69-78):
```cpp
commandQueue_ = std::make_unique<CommandQueue>();
httpServer_ = std::make_unique<HttpServer>(*commandQueue_);
if (!httpServer_->Start()) {
    DriverLog("MicMap: failed to start HTTP server\n");
    return VRInitError_Driver_Failed;
}
DriverLog("MicMap: HTTP server listening on port %d\n", httpServer_->GetPort());

initialized_ = true;
return VRInitError_None;
```

P6 EXTENSION (D-01, D-14 — read flag, conditionally construct worker LAST so any audio failure does not corrupt the v1.5 trigger path):
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

initialized_ = true;
return VRInitError_None;
```

**Cleanup insertion point** — prepend to the existing block at lines 88-92:

EXISTING (lines 88-92):
```cpp
if (httpServer_) {
    httpServer_->Stop();
    httpServer_.reset();
}
commandQueue_.reset();
```

P6 EXTENSION (D-13 — reverse-order teardown; AudioWorker first):
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
```

**Reset of `driverAudioEnabled_`** — add at the existing reset block (lines 94-102) for symmetry with `initLogged_` etc.:
```cpp
// EXISTING line 102:
initialized_ = false;

// ADD just before:
driverAudioEnabled_ = false;
```

**Delta vs analog:** zero new vr::* surfaces beyond `vr::VRSettings()->GetBool` (already a documented driver-side service interface); RunFrame untouched; existing log lines preserved; new log lines all prefixed `MicMap:` per existing convention (`device_provider.cpp:61, 72, 75, 86, 106`).

---

### `driver/resources/settings/default.vrsettings` (MODIFY — add one bool key)

**Analog (self):** current file lines 1-7 (the existing `driver_micmap` section).

EXISTING:
```json
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1"
    }
}
```

P6 RESULT (D-01 — add `enable_driver_audio: false`):
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

**Delta vs analog:** one key added; trailing-comma rule respected; existing keys untouched. Note: existing keys `enable`, `http_port`, `http_host` are not currently code-consumed (per CONTEXT D-01 / RESEARCH §"VRSettings reentrancy" pitfall); P6 introduces the first read.

---

### `driver/CMakeLists.txt` (MODIFY — add audio_worker.cpp to driver target)

**Analog (self):** current file lines 22-27.

EXISTING:
```cmake
# Driver must be a shared library
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
)
```

P6 RESULT:
```cmake
# Driver must be a shared library
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
    src/audio_worker.cpp     # P6 D-05: driver-side audio capture worker
)
```

**Delta vs analog:** zero new link deps. `IAudioCapture` is reachable via the existing `target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)` at line 80 (P5 D-10). No new `find_package`, no new include directory. The P5 link-only restriction is explicitly lifted in P6 per `driver/CMakeLists.txt:79` comment ("Phases 6/7 lift the link-only restriction.").

---

### `cmake/AssertAudioWorkerNoVrApi.cmake` (NEW, Wave 0 — invariant guard)

**Analog:** `cmake/lint_no_openvr_in_core.cmake` (P5 D-02 source-grep lint, lines 1-61). Same script-mode CMake module shape, narrower scope (two specific files instead of four directory roots).

**Banner pattern** (mirror lint_no_openvr_in_core.cmake:1-12):
```cmake
# cmake/AssertAudioWorkerNoVrApi.cmake
#
# Phase 6 / D-07 / SVR-05: source-grep lint that fails the build (via CTest)
# if any vr::* symbol use or OpenVR header include appears under
# driver/src/audio_worker.{hpp,cpp}. The audio worker thread MUST NOT call
# any OpenVR API (Pitfall 3); only DriverLog (which is null-safe and lives
# in driver_log.hpp, not under audio_worker.*) is permitted.
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME assert_audio_worker_no_vr_api
#       COMMAND ${CMAKE_COMMAND}
#           -DAUDIO_WORKER_DIR=${CMAKE_SOURCE_DIR}/driver/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertAudioWorkerNoVrApi.cmake)
```

**Body pattern** (copy from lint_no_openvr_in_core.cmake:14-60, swap from directory-glob to a two-file explicit list):
```cmake
if(NOT DEFINED AUDIO_WORKER_DIR)
    message(FATAL_ERROR "AssertAudioWorkerNoVrApi: AUDIO_WORKER_DIR not provided.")
endif()

set(_targets
    "${AUDIO_WORKER_DIR}/audio_worker.hpp"
    "${AUDIO_WORKER_DIR}/audio_worker.cpp")

set(_violations "")
set(_files_scanned 0)

foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # File not yet authored (Wave 0 RED phase) — skip; the lifecycle
        # test will fail to build instead. Once the file lands, this lint
        # turns GREEN and stays GREEN.
        continue()
    endif()
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    # Mirror lint_no_openvr_in_core.cmake regex set:
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

**Delta vs analog:**
- File list is two-file-explicit (driver/src/audio_worker.{hpp,cpp}) instead of `GLOB_RECURSE` over `SRC_ROOTS`.
- DriverLog macro lives in `driver_log.hpp` (not under audio_worker.*), so the existing lint regex `[<\"]openvr[a-z_]*\\.h[>\"]` would fire on a stray `<openvr_driver.h>` include — exactly what we want.
- The Wave 0 "file may not exist yet" branch is **new** (lint_no_openvr_in_core.cmake assumes the directories always exist); P6 needs the guard to be GREEN when invoked before the source files land. Alternative: register the test with `add_dependencies` on the driver target so it only runs after compile.

---

### `tests/driver/audio_worker_lifecycle_headless.cpp` (NEW, Wave 0)

**Analog A (driver-include + plain-main):** `tests/test_command_queue.cpp` (lines 1-44). Same shape: include the driver header directly via include-dir injection in tests/CMakeLists.txt, no link to driver target itself, plain `int main()` returning 0/1.

**Analog B (multi-case structure with assertions):** `tests/test_vr_input_quit_ordering.cpp` (lines 76-115). Per-case scoped block, `MM_CHECK(expr)` macro wrapping condition asserts.

**Banner pattern** (mirror test_vr_input_quit_ordering.cpp:1-32):
```cpp
/**
 * @file audio_worker_lifecycle_headless.cpp
 * @brief Phase 6 Wave 0 RED test for AudioWorker lifecycle invariants.
 *
 * Verifies — without any real WASAPI device — that:
 *   1. AudioWorker constructed → Start() returns true (or fails-soft);
 *   2. ~AudioWorker() / Stop() sets state->alive = false BEFORE signaling
 *      the shutdown CV (Pitfall 13 ordering — alive-flag must propagate
 *      to in-flight callbacks before they can race with destruction);
 *   3. The worker thread joins within the 2 s watchdog budget (D-13).
 *
 * Invocation: plain-main, exit 0 = pass, 1 = fail. Matches
 * tests/test_command_queue.cpp convention.
 *
 * RED state: until audio_worker.cpp lands, this TU fails to build with
 * a missing "audio_worker.hpp" diagnostic — the expected RED state.
 */

#include "audio_worker.hpp"   // resolved via target_include_directories tests/CMakeLists.txt

#include <chrono>
#include <iostream>
#include <thread>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)
```

**Test-case shape** (mirror test_vr_input_quit_ordering.cpp:76-91):
```cpp
int main() {
    using namespace std::chrono;
    namespace md = micmap::driver;

    // ---- Case 1: construct → destruct returns within 2 s watchdog ----
    {
        auto t0 = steady_clock::now();
        {
            md::AudioWorker w;
            // Don't even Start — just verify destructor on never-started
            // worker is fast. SC: destructor on no-op worker ≤100 ms.
        }
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);
        MM_CHECK(elapsed.count() < 100);
        std::cout << "PASS case_1_no_start_destructor_fast\n";
    }

    // ---- Case 2: Start → immediate Stop → join within 2 s watchdog ----
    {
        md::AudioWorker w;
        bool started = w.Start();
        // On a headless box with no WASAPI default capture, Start may
        // fail-soft (worker thread bails on createWASAPICapture==null).
        // EITHER outcome is acceptable for SC2 — what matters is that
        // ~AudioWorker joins ≤ 2 s.
        (void)started;

        auto t0 = steady_clock::now();
        // destructor runs at end of scope below
    }
    // (auto t1 measured outside the scope — alternative: explicit Stop().)
    std::cout << "PASS case_2_start_then_destruct_within_watchdog\n";

    // ---- Case 3: alive-flag-before-shutdown ordering (Pitfall 13) ----
    // Spike-grade: requires a hook on State that this test can observe.
    // If AudioWorker exposes a test-only getStateForTest() method, assert
    // state->alive == false BEFORE the cv signal would have been observed.
    // Otherwise: skip / mark pending, escalate to planner for an ABI hook.

    std::cout << "all tests passed\n";
    return 0;
}
```

**Delta vs analog:**
- `test_command_queue.cpp` includes `command_queue.hpp` directly via `target_include_directories(test_command_queue PRIVATE ${CMAKE_SOURCE_DIR}/driver/src)` (tests/CMakeLists.txt:40-41). The new test follows the same pattern.
- Unlike the queue test, this test exercises a thread; the watchdog assertion is the load-bearing part. If the headless path requires WASAPI to be present, the planner may need to gate Case 2 with a runtime guard or build a test-only mock injection seam — flag for planner attention.
- Pitfall 13 alive-flag ordering (Case 3) requires an inspection hook on `AudioWorker::State` that the production code does not need; planner should decide whether to add `getStateForTest()` (acceptable per spike scope) or defer Case 3 to a P7 follow-up.

---

### `tests/CMakeLists.txt` + root `CMakeLists.txt` (MODIFY — register new tests)

**Analog A (test_command_queue registration with driver-include path):** `tests/CMakeLists.txt:38-42`:
```cmake
# SVR-05: CommandQueue thread-safety / drop-oldest / depth-8 coverage
add_executable(test_command_queue test_command_queue.cpp)
target_compile_features(test_command_queue PRIVATE cxx_std_17)
target_include_directories(test_command_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
add_test(NAME test_command_queue COMMAND test_command_queue)
```

**Analog B (lint registration with `-P` script-mode invocation):** `tests/CMakeLists.txt:116-124`:
```cmake
add_test(NAME lint_no_openvr_in_core
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)
```

**P6 ADDITION — new exe + ctest** (mirror Analog A, but the source TU lives under `tests/driver/`; the test must compile `audio_worker.cpp` directly to satisfy the link, similar to how `test_tray_balloon_once` links the impl TU at `tests/CMakeLists.txt:74-84`):
```cmake
# P6 D-05: AudioWorker lifecycle invariants (Wave 0 RED scaffold).
# Compiles audio_worker.cpp directly into the test exe so we don't link
# driver_micmap.dll itself (Pitfall 6 — never link the driver into a test
# binary). Mirror tests/CMakeLists.txt:74-84 (test_tray_balloon_once)
# pattern: include the impl TU as a source.
add_executable(test_audio_worker_lifecycle_headless
    driver/audio_worker_lifecycle_headless.cpp
    ${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp)
target_compile_features(test_audio_worker_lifecycle_headless PRIVATE cxx_std_17)
target_include_directories(test_audio_worker_lifecycle_headless PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
target_link_libraries(test_audio_worker_lifecycle_headless PRIVATE micmap::core_runtime)
add_test(NAME test_audio_worker_lifecycle_headless
    COMMAND test_audio_worker_lifecycle_headless)
```

**P6 ADDITION — new ctest invariant** (mirror Analog B, narrower scope):
```cmake
# P6 D-07 / SVR-05: assert no vr::* surface in driver/src/audio_worker.{hpp,cpp}.
add_test(NAME assert_audio_worker_no_vr_api
    COMMAND ${CMAKE_COMMAND}
        -DAUDIO_WORKER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertAudioWorkerNoVrApi.cmake)
```

**Root CMakeLists.txt** — no changes required (the existing `if(MICMAP_BUILD_TESTS) add_subdirectory(tests)` block at lines 102-105 already runs the tests subdir). UNLESS the planner chooses to elevate `AssertAudioWorkerNoVrApi` to a configure-time `include()` (mirroring AssertNoOpenVRInCore.cmake at root line 90). Spike-grade choice: ctest-only is sufficient; configure-time elevation is over-engineering for a single-phase invariant.

**Delta vs analog:**
- Test source path is `tests/driver/audio_worker_lifecycle_headless.cpp` — the `tests/driver/` subdirectory **does not yet exist**; planner must include `mkdir tests/driver` in the file-creation task.
- Linking `micmap::core_runtime` PRIVATE into the test mirrors `driver/CMakeLists.txt:80` and is the minimum surface needed to compile `audio_worker.cpp` (which `#include`s `<micmap/audio/audio_capture.hpp>`).
- The lint-style `add_test(NAME assert_audio_worker_no_vr_api ...)` pattern is byte-identical to `lint_no_openvr_in_core` registration except for the `-D` argument shape (single dir, no `$<SEMICOLON>` join).

---

## Shared Patterns

### Driver-only file convention

**Source:** `driver/src/http_server.{hpp,cpp}`, `driver/src/command_queue.hpp`, `driver/src/driver_log.hpp` — all driver-only TUs.

**Apply to:** `driver/src/audio_worker.{hpp,cpp}` (P6 D-05).

**Rules:**
1. Files live under `driver/src/` and are added to `add_library(driver_micmap SHARED ...)` in `driver/CMakeLists.txt`. Never under `src/` (would violate P5 D-12 / SC4).
2. Headers may `#include <openvr_driver.h>` only when actually needed. `audio_worker.hpp` does NOT need it (D-07 — no vr::*) so it stays out, narrowing the surface the new ctest invariant guards.
3. Forward-declare cross-driver types (e.g., `class AudioWorker;` in `device_provider.hpp:25-27` style) instead of including their headers.

### DriverLog usage from any thread

**Source:** `driver/src/driver_log.hpp:24-39` — `SafeDriverLog` is null-safe pre-context and falls through to `vr::VRDriverLog()->Log` once the driver context is up.

**Apply to:** every log statement from the audio worker thread (D-07 — DriverLog is the **only** OpenVR-shaped surface allowed from the worker; SafeDriverLog calls `vr::VRDriverLog()` but that's the documented thread-safe driver-host service interface, not a `vr::*` API call in the SVR-05 sense).

**Pattern:**
```cpp
DriverLog("MicMap: <event> %s\n", details);   // macro from driver_log.hpp
```

The `MicMap:` prefix and trailing `\n` are project convention (see every existing call site in `device_provider.cpp` and `http_server.cpp`).

### Init/Cleanup symmetry (Pitfall 4 reverse-order teardown)

**Source:** `driver/src/device_provider.cpp:58-107`. Construction order: bindings patch → CommandQueue → HttpServer (start). Teardown order (current): HttpServer (stop+reset) → CommandQueue.

**Apply to:** P6 D-13/D-14 — extend symmetrically. New construction order: bindings patch → CommandQueue → HttpServer → **AudioWorker (last)**. New teardown order: **AudioWorker.reset() (first)** → HttpServer → CommandQueue → state-reset block → `VR_CLEANUP_SERVER_DRIVER_CONTEXT`.

### Plain-main test convention (no GTest)

**Source:** `tests/test_command_queue.cpp`, `tests/test_vr_input_quit_ordering.cpp`, `tests/test_config_manager.cpp`, `tests/test_placeholder.cpp`. Project-wide convention: `int main()` returning `0` on pass, `1` on fail; `assert()` or `MM_CHECK` macro, never GTest.

**Apply to:** `tests/driver/audio_worker_lifecycle_headless.cpp` (Wave 0).

**Rationale:** `tests/CMakeLists.txt:8-24` defines an opt-in `MICMAP_USE_GTEST` flag but the codebase has zero GTest-using test files; defaulting to plain-main matches every existing test.

### Source-grep ctest invariant pattern

**Source:** `cmake/lint_no_openvr_in_core.cmake` (P5 D-02), `cmake/lint_no_driver_macro.cmake` (P5 D-12).

**Apply to:** `cmake/AssertAudioWorkerNoVrApi.cmake` (P6 D-07).

**Rules:**
1. Pure CMake-script-mode (`-P`), no compiled executable, runs as a ctest test entry.
2. Argument-passing via `-D<VAR>=<value>` on the ctest invocation; semicolons in lists escaped via `$<SEMICOLON>`.
3. Body: `file(GLOB_RECURSE ...)` or explicit-list, `file(READ)` each, regex match, accumulate violations, `message(FATAL_ERROR ...)` on non-empty.
4. Print `STATUS` line on success with files-scanned count for CI visibility.

---

## No Analog Found

None. Every file in the P6 scope has either a self-analog (modify-in-place) or a sibling driver-side or shared-lib analog. The composite analogs (HttpServer + audio_capture for `audio_worker.cpp`) are the closest available; a single canonical "thread + cv-signal + atomic-shutdown" class does not exist in the tree, but the two halves do.

---

## Metadata

**Analog search scope:**
- `driver/src/` — full read of every `.cpp/.hpp` (5 files; all relevant to driver-only conventions)
- `cmake/` — full read of P5 lint/guard modules
- `tests/` — read of CMakeLists.txt, test_command_queue.cpp, test_vr_input_quit_ordering.cpp
- `src/audio/` — header `audio_capture.hpp` + targeted reads of `audio_capture.cpp` (lines 1-240, 380-530) for the COM/thread/IMMNotificationClient pattern
- Root `CMakeLists.txt` and `driver/CMakeLists.txt`

**Files scanned:** ~12 files across driver/, cmake/, tests/, src/audio/.

**Pattern extraction date:** 2026-05-02.

**Phase:** 06-driver-side-audio-capture-spike.

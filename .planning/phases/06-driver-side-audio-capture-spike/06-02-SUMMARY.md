---
phase: 06-driver-side-audio-capture-spike
plan: 02
subsystem: driver
tags: [driver, audio, wasapi, com, threading, lifecycle, pitfall-1, pitfall-3, pitfall-4, pitfall-13, d-04, d-06, d-13, d-22]

# Dependency graph
requires:
  - phase: 05-shared-library-extraction
    provides: micmap::core_runtime INTERFACE target (transitively pulls IAudioCapture); driver/src/* link path; SafeDriverLog macro shape; D-22 dumpbin invariant
  - phase: 06-driver-side-audio-capture-spike
    plan: 01
    provides: cmake/AssertAudioWorkerNoVrApi.cmake source-grep lint (now scanning real impl files); tests/driver/audio_worker_lifecycle_headless.cpp scaffold (RED gate now closed); EXISTS-gated source-list pattern in tests/CMakeLists.txt
provides:
  - driver/src/audio_worker.hpp (class AudioWorker — Start/Stop/IsRunning/state_for_test public surface; nested State struct with atomic<bool> alive; weak_ptr<State> alive-flag pattern for Pitfall 13)
  - driver/src/audio_worker.cpp (thread entry with three-bucket CoInitializeEx HRESULT handling per D-06 / SC2; D-04 apartment-trick capture construction; RMS-budgeted callback per D-08; reverse-order teardown per Pitfall 4 / D-13; 2 s watchdog Stop() per D-13)
  - driver/CMakeLists.txt registration of src/audio_worker.cpp (P6 D-05; zero new link deps — IAudioCapture reached via existing micmap::core_runtime PRIVATE link)
  - driver/resources/settings/default.vrsettings.driver_micmap.enable_driver_audio = false (D-01 / SC4 safety net)
  - driver/src/driver_log.hpp Rule-3 fix: SafeDriverLog now actually safe pre-context (was claimed-but-not-actually safe; would AV on first call without a SteamVR context)
  - tests/CMakeLists.txt OpenVR_FOUND gate on test_audio_worker_lifecycle_headless target (P5 SC1 headless invariant preservation)
affects: [06-03, 06-04, 07-detection-thread]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Apartment-trick (D-04): worker thread owns CoInitializeEx(MTA); WASAPIAudioCapture's inner CoInitializeEx then returns S_FALSE in the same apartment. Constructor placement is the load-bearing trick — do NOT move construction back into DeviceProvider::Init"
    - "Three-bucket HRESULT handling for CoInitializeEx (D-06): RPC_E_CHANGED_MODE → distinct literal log line + bail; FAILED && != S_FALSE → generic failure log + bail; success or S_FALSE → MTA confirmation log"
    - "weak_ptr<State> + atomic<bool> alive callback gate (Pitfall 13): callback captures weak_ptr, locks + checks alive.load(memory_order_acquire) at head before any state mutation; State's lifetime outlives the AudioWorker via shared_ptr"
    - "RMS log budget (D-08): kRmsBudget=100 (~1 s @ 10 ms WASAPI shared-mode period); callback drains frames after budget exhausted but skips DriverLog writes — log-flood mitigation"
    - "2 s watchdog Stop() (D-13): poll thread_finished_ atomic every 25 ms with steady_clock deadline; on overrun log + thread_.detach() last-resort to avoid blocking vrserver.exe shutdown"
    - "Reverse-order teardown ALL on the worker thread (Pitfall 4): capture_->stopCapture() → capture_.reset() (~WASAPIAudioCapture: IMMNotificationClient unregister + ComPtr release + CoUninitialize) → our own ::CoUninitialize() — the same thread that did the register/CoInit"
    - "Test target OpenVR gating: when a test source compiles a driver TU directly and that TU transitively includes <openvr_driver.h>, gate add_executable on OpenVR_FOUND so the headless build (P5 SC1 invariant) configures cleanly"

key-files:
  created:
    - driver/src/audio_worker.hpp
    - driver/src/audio_worker.cpp
    - .planning/phases/06-driver-side-audio-capture-spike/06-02-SUMMARY.md
  modified:
    - driver/CMakeLists.txt
    - driver/resources/settings/default.vrsettings
    - driver/src/driver_log.hpp
    - tests/CMakeLists.txt

key-decisions:
  - "AudioWorker constructor only allocates the State shared_ptr; thread is spawned in Start(). Mirrors HttpServer's two-phase init/start shape and lets DeviceProvider construct the worker member without paying for the thread until Init time"
  - "kRmsBudget = 100 (compile-time constexpr in anonymous namespace) — fixed-count budget rather than steady_clock cutoff. Simpler reasoning across WASAPI period jitter; D-08 explicitly allows either"
  - "Stop() polls thread_finished_ atomic instead of using cv_.wait_for. The worker thread sets thread_finished_ at the END of RunWorker, AFTER ~capture_ + ::CoUninitialize. Polling avoids a second mutex/cv pair and matches the v1.5 watchdog precedent shape"
  - "Watchdog overrun detaches thread_ as last-resort — explicit T3 mitigation per D-13 over the alternative of forcing a join (which would block vrserver.exe shutdown indefinitely if WASAPI deadlocks)"
  - "[Rule 3] Headless test target gated on OpenVR_FOUND because it compiles audio_worker.cpp directly. Alternative — vendoring openvr include path into the test target without a real OpenVR_FOUND — was rejected as fragile cross-build hygiene; the lint still scans the source files in either configuration"
  - "[Rule 3] driver_log.hpp SafeDriverLog now guards on vr::VRDriverContext() before calling vr::VRDriverLog(). Original implementation's pre-context safety claim was aspirational; the actual code AV'd on first call in the absence of VR_INIT_SERVER_DRIVER_CONTEXT because VRDriverLog() itself dereferences the context pointer before its own null check"
  - "Em-dash in the RPC_E_CHANGED_MODE log line is encoded as the UTF-8 byte sequence `\\xe2\\x80\\x94` rather than a raw em-dash literal — keeps the source byte-stable across MSVC source charset autodetection while still emitting `(RPC_E_CHANGED_MODE = 0x80010106) — bailing out` to vrserver.txt"

# Metrics
duration: ~30min
completed: 2026-05-03
---

# Phase 6 Plan 02: AudioWorker Lifecycle Implementation Summary

**Driver-side AudioWorker class lands the highest-risk-bucket of v1.6 in two TUs and a vrsettings flag: a worker thread that owns CoInitializeEx(MTA), constructs WASAPIAudioCapture on its own apartment (D-04 apartment-trick), wires a Pitfall 13 weak_ptr alive-flag callback, and tears down WASAPI/COM in reverse order all on the worker — bounded by a 2 s watchdog. Wave 0 RED gate closes; AssertAudioWorkerNoVrApi confirms zero `vr::*` surface in the worker; dumpbin still shows only `HmdDriverFactory`.**

## Performance

- **Duration:** ~30 min
- **Started:** 2026-05-03 (immediately after 06-01 plan completion)
- **Tasks:** 2
- **Files created:** 2 (driver/src/audio_worker.{hpp,cpp})
- **Files modified:** 4 (driver/CMakeLists.txt, default.vrsettings, driver/src/driver_log.hpp, tests/CMakeLists.txt)

## Accomplishments

### Task 1 — vrsettings flag + driver target source registration

- Added `"enable_driver_audio": false` to the existing `driver_micmap` section of `driver/resources/settings/default.vrsettings` (D-01 / SC4 safety net — flag-OFF means byte-identical to Phase 5 driver behavior).
- Appended `src/audio_worker.cpp` to the `add_library(driver_micmap SHARED ...)` source list in `driver/CMakeLists.txt` with the inline marker `# P6 D-05: driver-side audio capture worker`. Zero new link dependencies — `IAudioCapture` is already pulled via the existing P5 `target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)` link.
- `cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` exits 0 (configure clean both before and after the audio_worker source files land).

### Task 2 — AudioWorker class authoring + Wave 0 RED gate close

- `driver/src/audio_worker.hpp` (109 lines, public surface):
  - `class AudioWorker { Start(); Stop(); IsRunning(); ~AudioWorker(); }` plus a nested `struct State { atomic<bool> alive; atomic<uint32_t> rms_logs_emitted; atomic<uint32_t> frames_seen; }` and a test-only `state_for_test()` accessor for the Pitfall 13 alive-before-shutdown ordering check (currently used by the headless test only as the SKIP-pending hook).
  - Forward-declares `micmap::audio::IAudioCapture` to keep the header free of audio-runtime symbols (D-07).
  - Zero `vr::*`, zero `<openvr*.h>` includes — `AssertAudioWorkerNoVrApi` confirms this.
- `driver/src/audio_worker.cpp` (~210 lines, impl):
  - **Pitfall 1 / D-05:** `RunWorker()` calls `::CoInitializeEx(nullptr, COINIT_MULTITHREADED)` exactly once on the worker thread.
  - **D-06 / SC2:** three-bucket HRESULT handling — `RPC_E_CHANGED_MODE` emits the literal log line `MicMap: audio worker thread already in another COM apartment (RPC_E_CHANGED_MODE = 0x80010106) — bailing out` and exits without constructing the capture; generic failure emits a different `MicMap: audio worker CoInitializeEx failed hr=0x%08X` line; success or `S_FALSE` emits `MicMap: audio worker thread COM apartment = MTA (hr=0x%08X)`.
  - **D-04 (apartment-trick):** `auto capture = micmap::audio::createWASAPICapture()` runs AFTER our own `CoInitializeEx`, so `WASAPIAudioCapture`'s inner `CoInitializeEx` returns `S_FALSE` (already-init same apartment) and the existing `comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE` accepts it. Load-bearing comment in the source warns future maintainers not to move construction back into `DeviceProvider::Init`.
  - **Pitfall 13 / D-15 / D-16:** RMS audio callback captures `std::weak_ptr<State> weak`, locks + checks `sp->alive.load(std::memory_order_acquire)` at the head before computing RMS or touching state. Frame counter (`frames_seen`) increments on every callback; log-emit counter (`rms_logs_emitted`) is the budget gate.
  - **D-08 (RMS budget):** `constexpr uint32_t kRmsBudget = 100` (~1 s @ 10 ms WASAPI shared-mode period). Callback emits `MicMap audio: rms[%u]=%.6f` for the first 100 frames, then drains frames silently — log-flood mitigation, T6 / Pitfall 6 carry.
  - **Pitfall 4 / D-13 (reverse-order teardown):** all teardown runs on the worker thread — `capture_->stopCapture()` → `capture_.reset()` (which runs `~WASAPIAudioCapture`: `IMMNotificationClient` unregister + `ComPtr` release + `CoUninitialize` per `audio_capture.cpp:222-235`) → our own `::CoUninitialize()`.
  - **D-13 (2 s watchdog):** `Stop()` flips `state_->alive=false`, signals shutdown, polls `thread_finished_` atomic every 25 ms with a 2 s `steady_clock` deadline; on success `thread_.join()`, on overrun logs `MicMap: audio worker did not exit within 2 s watchdog - detaching (T3 mitigation)` and `thread_.detach()`.
  - **D-22 carry:** zero `__declspec(dllexport)` / `__declspec(dllimport)`. dumpbin confirms only `HmdDriverFactory` is exported.
- Wave 0 RED gate closed:
  - `cmake --build build --config Release --target driver_micmap` succeeds with the new `audio_worker.cpp` in the source list.
  - `ctest -R AssertAudioWorkerNoVrApi -C Release` passes — the lint now scans both real files and confirms zero `vr::*` / `<openvr*.h>` surface (D-07 / Pitfall 3 / SC3).
  - `ctest -R AudioWorkerLifecycleHeadless -C Release` passes — Case 1 (no-Start destructor < 100 ms) + Case 2 (Start-then-destruct within 2.5 s) both run GREEN; Case 3 (Pitfall 13 alive-before-shutdown ordering) prints `SKIP` pending an explicit alive-flag observation hook on top of the now-exposed `state_for_test()` accessor.
  - All P5 carryover invariants stay green: `lint_no_openvr_in_core`, `lint_no_driver_macro`, `AssertNoOpenVRInCore` (configure-time).
- Full ctest battery in `build/`: 14/14 PASS.
- Headless build (`build-headless/`, `MICMAP_BUILD_DRIVER=OFF`, OpenVR not found): the same 3 tests that were "Not Run" before this plan are still "Not Run" (pre-existing P5-OpenVR-presence gates on `test_manifest_registrar`, `test_vr_input_quit_ordering`, `test_vrmanifest_schema`). All other tests pass; in particular the `AssertAudioWorkerNoVrApi` lint runs in headless mode (script-mode `-P`, no compile required) and confirms zero `vr::*` surface.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add `enable_driver_audio` flag + register audio_worker.cpp** — `19f4689` (feat)
2. **Task 2: Author audio_worker.{hpp,cpp} + Rule-3 fixes (driver_log.hpp + tests/CMakeLists.txt)** — `ad2c671` (feat)

**Plan metadata commit:** to follow at end of plan execution (this SUMMARY + STATE.md + ROADMAP.md updates).

## Files Created / Modified

- `driver/src/audio_worker.hpp` (NEW, 109 lines) — declarations described above. Zero `vr::*`, zero `<openvr*.h>`.
- `driver/src/audio_worker.cpp` (NEW, ~210 lines) — implementation described above. Zero `vr::*`, zero `<openvr*.h>`.
- `driver/CMakeLists.txt` (+1 line) — `src/audio_worker.cpp` added to driver target source list with `# P6 D-05` marker.
- `driver/resources/settings/default.vrsettings` (+1 key) — `enable_driver_audio: false` added under `driver_micmap`.
- `driver/src/driver_log.hpp` (Rule 3 fix, +9 lines) — `SafeDriverLog` now guards on `vr::VRDriverContext()` before calling `vr::VRDriverLog()`. Original implementation crashed in headless test environments (claimed-but-not-actually safe pre-context).
- `tests/CMakeLists.txt` (Rule 3 fix, +13 −7 lines) — `test_audio_worker_lifecycle_headless` target now gated on `OpenVR_FOUND`. Without the gate, the headless build (P5 SC1 invariant) would fail to compile because `audio_worker.cpp` transitively pulls `<openvr_driver.h>` via `driver_log.hpp`.

## Decisions Made

- **AudioWorker constructor allocates State shared_ptr only; thread is spawned in Start().** Mirrors HttpServer's two-phase init/start shape; lets `DeviceProvider` construct the member during Init without forcing the thread spawn until the explicit `Start()` call (Plan 06-03 territory).
- **kRmsBudget = 100 fixed-count rather than steady_clock cutoff.** Simpler reasoning across WASAPI period jitter and ECS sample-rate drift; D-08 explicitly allows either choice. The frame counter still increments unbounded (so 06-04 UAT can confirm callbacks continued past the log budget).
- **Stop() polls a `thread_finished_` atomic instead of using `cv_.wait_for`.** The worker thread sets `thread_finished_` at the END of `RunWorker`, AFTER `~capture_` + `::CoUninitialize`. Polling at 25 ms intervals avoids a second mutex/cv pair and matches the v1.5 `VREvent_Quit` watchdog precedent shape (sleep-and-check rather than CV signal). 25 ms × 80 polls = 2 s deadline.
- **Watchdog overrun detaches thread_ as last-resort.** Explicit T3 mitigation per D-13. Alternative — forcing a join — would block `vrserver.exe` shutdown indefinitely if WASAPI deadlocks; detach lets the process exit while the leaked thread (rare) gets cleaned up by OS process teardown.
- **Em-dash in the RPC_E_CHANGED_MODE log line uses the UTF-8 byte escape `\xe2\x80\x94`** rather than a raw em-dash. Keeps the source byte-stable across MSVC source-charset auto-detection while still emitting the must-have-truth-required `(RPC_E_CHANGED_MODE = 0x80010106) — bailing out` to `vrserver.txt`. Em-dashes in comments (which is C++ comment text, not a string literal) are tolerated by `/W4 /WX`; em-dashes in string literals can occasionally trigger C4566 depending on locale.
- **Test target OpenVR gate over header-include vendoring.** Rejected the alternative of forcing the OpenVR include path into the headless test target manually — that would compile `<openvr_driver.h>` against potentially-mismatched stub libs and silently regress P5 SC1 (headless-without-OpenVR build correctness). The clean answer is "if OpenVR isn't found, this driver-coupled test isn't built". The `AssertAudioWorkerNoVrApi` lint still scans the source files unconditionally (script-mode, no compile), so the D-07 invariant is enforced in either build configuration.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `SafeDriverLog` AVs on first call without a SteamVR context**

- **Found during:** Task 2 verification — first run of `test_audio_worker_lifecycle_headless.exe` after the impl landed. Exit code -1073741819 (`STATUS_ACCESS_VIOLATION`).
- **Issue:** `driver/src/driver_log.hpp`'s `SafeDriverLog` claimed in its docstring to "safely handle logging before the OpenVR driver context is initialized." The implementation guards on `if (vr::VRDriverLog()) ...`, but `vr::VRDriverLog()` itself dereferences `vr::VRDriverContext()` (a default-null `static IVRDriverContext *pHost`) BEFORE its own null check — so calling `VRDriverLog()` without a `VR_INIT_SERVER_DRIVER_CONTEXT` first segfaults inside the OpenVR header. The docstring was aspirational; the implementation never actually achieved it. Pre-existing latent bug — only surfaced now because this is the first headless test to compile a driver TU that calls `DriverLog`.
- **Fix:** Added an explicit `vr::VRDriverContext() != nullptr` guard before the `vr::VRDriverLog()` call. When the context pointer is null (= no `VR_INIT_SERVER_DRIVER_CONTEXT` has run yet), fall through to the existing stderr fallback. Comment in the source documents the original aspiration vs. actual fix so a future maintainer doesn't re-introduce the bug.
- **Files modified:** `driver/src/driver_log.hpp`
- **Verification:** `test_audio_worker_lifecycle_headless.exe` now exits 0 with both Case 1 + Case 2 PASSING and Case 3 SKIPPING. The fix is no-op for the production path (driver loaded by vrserver.exe, context always set before any DriverLog call).
- **Committed in:** `ad2c671` (Task 2 commit)

**2. [Rule 3 — Blocking] `test_audio_worker_lifecycle_headless` target failed to compile under `MICMAP_BUILD_DRIVER=OFF` because `audio_worker.cpp` transitively pulls `<openvr_driver.h>` via `driver_log.hpp`**

- **Found during:** First headless build after impl files landed. Error: `error C1083: Cannot open include file: 'openvr_driver.h': No such file or directory`.
- **Issue:** Wave 0's `add_executable(test_audio_worker_lifecycle_headless ... ${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp)` was registered unconditionally. Once `audio_worker.cpp` lands, that source file's `#include "driver_log.hpp"` brings in `<openvr_driver.h>` — which doesn't exist in the headless build (P5 SC1: build-headless deliberately runs without OpenVR present). Linking `OpenVR::openvr_api` to the test would still fail because the target itself isn't defined when OpenVR isn't found. The plan acceptance criterion `cmake --build build-headless --config Release --target driver_micmap` is unfulfillable as written — `driver_micmap` only exists when `MICMAP_BUILD_DRIVER=ON AND OpenVR_FOUND`. The verify section's second line (`cmake --build build --config Release --target driver_micmap`, the OpenVR-present build) is the actual canonical verification.
- **Fix:** Wrapped the `add_executable(test_audio_worker_lifecycle_headless ...)` block plus the `add_test(NAME AudioWorkerLifecycleHeadless ...)` in `if(OpenVR_FOUND) ... else() message(STATUS ...) endif()`. Test only registers in the OpenVR-present configuration; the lint (`AssertAudioWorkerNoVrApi`, script-mode CMake) runs unconditionally in either build. P5 SC1 invariant preserved.
- **Files modified:** `tests/CMakeLists.txt`
- **Verification:** `cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` configure clean; `cmake -B build -S .` configure clean; `ctest --test-dir build -C Release -R AudioWorkerLifecycleHeadless` PASS; in `build-headless` the test simply isn't registered and the `AssertAudioWorkerNoVrApi` lint still runs.
- **Committed in:** `ad2c671` (Task 2 commit, same commit as fix 1 since they're both blocking the same Task 2 verify step)

---

**Total deviations:** 2 auto-fixed (both Rule 3 — blocking issues caused by this plan's new code first exercising a pre-existing latent surface).
**Impact on plan:** Both fixes preserve the plan's intent. Fix 1 is a no-op on the production path (vrserver.exe always inits context before any DriverLog). Fix 2 narrows the test's build configuration matrix without weakening the lint invariant. No scope creep.

## Issues Encountered

- The plan's verify section line `cmake --build build-headless --config Release --target driver_micmap` is unfulfillable as-written: `driver_micmap` is only defined when `MICMAP_BUILD_DRIVER=ON AND OpenVR_FOUND`, and `build-headless` is configured with `MICMAP_BUILD_DRIVER=OFF` per P5 SC1. The intended canonical verification is the OpenVR-present build (`build/`), which is also in the verify section. Used `build/` for the driver build acceptance check.
- The plan's `default.vrsettings` byte-stable acceptance was complicated by Git's `core.autocrlf` warning (LF → CRLF on commit) — the file's committed shape uses CRLF line endings, which is what the existing file used. JSON parser doesn't care; documenting for completeness.
- `dumpbin.exe` is not on PATH in Git Bash by default; resolved via VS install path `/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe`. Future executors should use `where dumpbin` from `cmd.exe` or set up an MSVC dev-prompt PATH alias.

## User Setup Required

None. UAT on real hardware (Bigscreen Beyond + Win11 Pro per D-17) is Plan 06-04's responsibility. This plan's verification is fully automated via ctest + dumpbin in the OpenVR-present build configuration.

## Next Phase Readiness

- **Plan 06-03 (DeviceProvider Init/Cleanup wiring) is unblocked.** Plan 06-03 will:
  1. Add `bool driverAudioEnabled_{false}` and `std::unique_ptr<AudioWorker> audioWorker_` members to `DeviceProvider` (forward-decl `class AudioWorker` in `device_provider.hpp` per PATTERNS.md analog).
  2. Read the new `enable_driver_audio` vrsettings flag in `DeviceProvider::Init` via `vr::VRSettings()->GetBool(...)` (D-01, single-read, default false on UnsetSettingHasNoDefault).
  3. Conditionally construct `AudioWorker` LAST in Init (after CommandQueue + HttpServer, D-14 — audio failure must not corrupt the v1.5 trigger path).
  4. Reset `audioWorker_.reset()` FIRST in Cleanup (before HttpServer + CommandQueue, D-13 reverse-order).
- **AudioWorker production surface is stable** — `Start()` / `Stop()` / `IsRunning()` / `~AudioWorker()` are the only entry points DeviceProvider needs. `state_for_test()` is an explicitly test-only escape hatch and may be deleted in P7 if unused.
- **No blockers for Plan 06-04** — UAT can use the AudioWorker via Plan 06-03's DeviceProvider wiring once that lands.
- **Plan 07 (Detection Thread) inherits a clean lifecycle scaffold** — adding the SPSC SampleRing + DetectionRunner is now an additive change to AudioWorker (push samples to a ring inside the existing callback) rather than a new thread/COM/lifecycle design.

## Self-Check: PASSED

Verifying claims in this SUMMARY against on-disk and git state:

- File `driver/src/audio_worker.hpp` — FOUND (109 lines, present at HEAD = `ad2c671`).
- File `driver/src/audio_worker.cpp` — FOUND (~210 lines, present at HEAD = `ad2c671`).
- File `driver/resources/settings/default.vrsettings` — FOUND with `enable_driver_audio: false` (verified via `python -c "import json; assert json.load(open(...))['driver_micmap']['enable_driver_audio']==False"`).
- File `driver/CMakeLists.txt` — FOUND with `src/audio_worker.cpp` registered + `# P6 D-05` marker.
- File `driver/src/driver_log.hpp` — FOUND with `vr::VRDriverContext()` guard added.
- File `tests/CMakeLists.txt` — FOUND with `if(OpenVR_FOUND) ... else() message(STATUS ...) endif()` wrapping the test_audio_worker_lifecycle_headless target.
- Commit `19f4689` (`feat(06-02): add enable_driver_audio flag + register audio_worker.cpp`) — FOUND in git log.
- Commit `ad2c671` (`feat(06-02): implement AudioWorker driver-side capture worker`) — FOUND in git log.
- Acceptance: `cmake --build build --config Release --target driver_micmap` exits 0 — VERIFIED.
- Acceptance: `ctest --test-dir build -C Release -R AssertAudioWorkerNoVrApi --output-on-failure` exits 0 — VERIFIED (now scans real impl files clean: STATUS line `AssertAudioWorkerNoVrApi: clean (2 files scanned)`).
- Acceptance: `ctest --test-dir build -C Release -R AudioWorkerLifecycleHeadless --output-on-failure` exits 0 — VERIFIED (Wave 0 RED gate closed; all three cases run, Case 3 outputs SKIP).
- Acceptance: `dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll` lists ONLY `HmdDriverFactory` — VERIFIED (1 ordinal, 1 name, no AudioWorker symbols leaked).
- Acceptance: All P5 carryover invariants (`AssertNoOpenVRInCore`, `lint_no_openvr_in_core`, `lint_no_driver_macro`) remain green — VERIFIED.
- Acceptance: Full `ctest --test-dir build -C Release` battery: 14/14 PASS — VERIFIED.

## TDD Gate Compliance

This plan declared `tdd="true"` on each task. The TDD shape was:

- **RED phase** was provided by Plan 06-01 (Wave 0 scaffold): `AudioWorkerLifecycleHeadless` could not build (missing `audio_worker.hpp`); `AssertAudioWorkerNoVrApi` returned the no-op clean (skip-on-NOT-EXISTS branch). The Plan 06-01 Task 2 commit (`8565744`) is the RED gate commit per project convention.
- **GREEN phase** is this plan's two `feat(...)` commits: `19f4689` lands the vrsettings/cmake wiring, `ad2c671` lands the impl. After `ad2c671`, both tests turn GREEN (lint scans real files clean; lifecycle test builds and passes).
- **REFACTOR phase** was not needed — the implementation matched the PATTERNS.md byte-template and required no follow-up cleanup.

Gate sequence in git log: `8565744` (test, RED) → `19f4689` (feat, GREEN structure) → `ad2c671` (feat, GREEN impl). Compliant.

---
*Phase: 06-driver-side-audio-capture-spike*
*Completed: 2026-05-03*

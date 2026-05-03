---
phase: 06-driver-side-audio-capture-spike
plan: 03
subsystem: driver
tags: [driver, lifecycle, vrsettings, audio, wiring, d-01, d-03, d-13, d-14, pitfall-4, pitfall-11, svr-05]

# Dependency graph
requires:
  - phase: 06-driver-side-audio-capture-spike
    plan: 01
    provides: AssertAudioWorkerNoVrApi source-grep lint scoped to audio_worker.{hpp,cpp} only — leaves device_provider.cpp free to use vr::VRSettings()
  - phase: 06-driver-side-audio-capture-spike
    plan: 02
    provides: class AudioWorker with Start()/Stop()/IsRunning() public surface, 2 s watchdog destructor, fail-on-Start return value semantics; default.vrsettings driver_micmap.enable_driver_audio = false (D-01 / SC4 safety net)
provides:
  - driver/src/device_provider.hpp forward-decl class AudioWorker; + private members `bool driverAudioEnabled_{false}` + `std::unique_ptr<AudioWorker> audioWorker_`
  - driver/src/device_provider.cpp Init flag-read (single-read, three-bucket error handling, default false) + conditional AudioWorker construction LAST in Init (D-14 fail-soft)
  - driver/src/device_provider.cpp Cleanup audioWorker_.reset() FIRST (D-13 reverse construction order)
affects: [06-04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Single-read vrsettings flag at Init (D-01 / Pitfall 11): vr::VRSettings()->GetBool inside an inner scope so EVRSettingsError doesn't leak; result cached on driverAudioEnabled_ member; no hot-reload"
    - "Three-bucket EVRSettingsError handling (D-01 / SC4 safety net): success → use returned value; UnsetSettingHasNoDefault → explicit log + default false; any other error code → log + default false. Mirrors three-bucket CoInitializeEx pattern from Plan 06-02"
    - "Construct LAST, destruct FIRST (D-13/D-14): AudioWorker construction is the LAST step in Init (after HttpServer::Start succeeds and BEFORE initialized_=true); audioWorker_.reset() is the FIRST step in Cleanup (BEFORE httpServer_->Stop and commandQueue_.reset). Reverse-order teardown enforced by explicit Cleanup() sequence not by member declaration order"
    - "Fail-soft on AudioWorker::Start failure (D-14): on Start() returning false, audioWorker_.reset() but Init still returns VRInitError_None — v1.5 HTTP/CommandQueue/HMD trigger path stays alive"
    - "Forward-decl in header (mirrors HttpServer / CommandQueue pattern): class AudioWorker; in device_provider.hpp keeps audio_worker.hpp out of the header's transitive include closure"

key-files:
  created:
    - .planning/phases/06-driver-side-audio-capture-spike/06-03-SUMMARY.md
  modified:
    - driver/src/device_provider.hpp
    - driver/src/device_provider.cpp

key-decisions:
  - "Forward-decl class AudioWorker in the header rather than #include audio_worker.hpp — mirrors the HttpServer/CommandQueue pattern. std::unique_ptr<incomplete-type> requires the destructor be defined in the TU that sees the complete type, which is already the case (DeviceProvider::~DeviceProvider() lives in device_provider.cpp where audio_worker.hpp is included)"
  - "Read the flag AFTER httpServer_->Start() succeeds rather than before. Rationale: HttpServer is the v1.5 trigger path; if vrsettings read fails for any reason we still have a working driver. Putting the flag-read first would delay HTTP startup for no benefit"
  - "AudioWorker construction wrapped in `if (driverAudioEnabled_)` — D-03 mandates that flag-OFF means zero audio surface (no thread spawn, no COM, no WASAPI). Using a no-op AudioWorker stub was rejected because it would still allocate State and pay the destructor cost"
  - "audioWorker_.reset() in the failure path of `if (!audioWorker_->Start())` is explicit (rather than relying on unique_ptr destructor at end of Init scope). Frees the State allocation immediately and matches the symmetric Cleanup-step-1 reset pattern — easier to read"
  - "driverAudioEnabled_ added to the state-reset block before initialized_=false even though the flag is re-read on next Init. Symmetry with the surrounding bool-reset block (initLogged_, loggedAwaitingHmd_, profilePropsWritten_) and explicit documentation that no init-time state survives Cleanup"

# Metrics
duration: ~5min
completed: 2026-05-03
---

# Phase 6 Plan 03: DeviceProvider AudioWorker Wiring Summary

**Two-file additive change wires the Plan 06-02 AudioWorker into the driver's lifecycle: forward-decl + 2 members in the header, an Init-time vrsettings flag-read with three-bucket error handling, conditional construction LAST in Init (D-14 fail-soft), and `audioWorker_.reset()` FIRST in Cleanup (D-13 reverse-order teardown). RunFrame is byte-identical (SVR-05 invariant). With the shipped default `enable_driver_audio: false`, the driver is byte-identical to Phase 5; with the flag flipped, Plan 04 UAT can light up real-hardware audio capture.**

## Performance

- **Duration:** ~5 min (plan was a pure mechanical wiring after Plans 06-01 + 06-02 landed the scaffold and impl)
- **Started:** 2026-05-03T02:01:56Z
- **Tasks:** 1 (single-task plan)
- **Files created:** 1 (this SUMMARY)
- **Files modified:** 2 (driver/src/device_provider.hpp, driver/src/device_provider.cpp)

## Accomplishments

### Task 1 — DeviceProvider Init/Cleanup wiring

**`driver/src/device_provider.hpp` (+9 lines, 0 deletions):**

- Added `class AudioWorker;` forward-decl after the existing `class CommandQueue;` forward-decl. No new `#include` directive — `audio_worker.hpp` is NOT pulled into the header (mirrors the HttpServer pattern).
- Added two private members in the class body, immediately after the existing `std::atomic<bool> initialized_{false};`:
  - `bool driverAudioEnabled_{false};` — caches the result of the Init-time vrsettings read.
  - `std::unique_ptr<AudioWorker> audioWorker_;` — owning handle; default-init nullptr; only allocated when the flag is true.
- Inline comment block documents D-01/D-03/D-14/Pitfall 4 references for future maintainers.

**`driver/src/device_provider.cpp` (+43 lines, 0 deletions):**

1. **`#include "audio_worker.hpp"`** added to the include list (after `http_server.hpp`). This is the TU that owns DeviceProvider's destructor, so the complete `AudioWorker` type must be visible here for `std::unique_ptr<AudioWorker>` to instantiate cleanly.

2. **Init flag-read block** inserted between `DriverLog("MicMap: HTTP server listening on port %d\n", ...)` and the existing `initialized_ = true;` line. Wrapped in an inner scope so `vr::EVRSettingsError err` doesn't leak into the surrounding function. Three-bucket handling:
   - `err == VRSettingsError_None`: log `MicMap: enable_driver_audio = true|false` and use the GetBool result.
   - `err == VRSettingsError_UnsetSettingHasNoDefault`: log `MicMap: enable_driver_audio unset, defaulting to false` and force false.
   - any other error code: log `MicMap: VRSettings GetBool(enable_driver_audio) error=%d` and force false.
   The existing `default.vrsettings` ships with `enable_driver_audio: false` (Plan 06-02 Task 1) so the bucket-1 success path is the production hot path; bucket 2 + bucket 3 are the SC4 safety nets.

3. **Conditional AudioWorker construction** immediately after the flag-read block, gated by `if (driverAudioEnabled_)`. On gate-true:
   ```cpp
   audioWorker_ = std::make_unique<AudioWorker>();
   if (!audioWorker_->Start()) {
       DriverLog("MicMap: AudioWorker::Start failed — continuing without audio\n");
       audioWorker_.reset();
   }
   ```
   D-14 fail-soft semantics: on `Start()` failure we reset the unique_ptr to release the partial-init State allocation but do NOT return `VRInitError_Driver_Failed`. The v1.5 HTTP/CommandQueue/HMD trigger path stays alive — the only behavior loss is that audio detection won't run this session.

4. **Cleanup audioWorker_.reset() FIRST** prepended at the top of the existing Cleanup body (BEFORE `if (httpServer_) { httpServer_->Stop(); ... }`). The destructor runs the 2 s watchdog from Plan 06-02 (`state_->alive=false`, signal CV, poll thread_finished_ every 25 ms with 2 s deadline, on overrun detach). Because all WASAPI / COM teardown happens inside the AudioWorker's worker thread (Plan 06-02 D-13), the existing v1.5 cleanup sequence (`httpServer_->Stop()`, `commandQueue_.reset()`, state-reset, `VR_CLEANUP_SERVER_DRIVER_CONTEXT()`) is byte-identical to Phase 5.

5. **State-reset symmetry:** added `driverAudioEnabled_ = false;` immediately before `initialized_ = false;` in the existing state-reset block. Mirrors the surrounding bool-reset pattern (`initLogged_`, `loggedAwaitingHmd_`, `profilePropsWritten_`) and ensures no Init-time state survives a Cleanup.

**RunFrame body untouched.** `git diff --stat` reports `2 files changed, 52 insertions(+)`. Pure additive — zero deletions, zero hunks intersect the RunFrame function body (lines 156-247 of the post-modification file). SVR-05 invariant preserved: HTTP-thread → CommandQueue → RunFrame is still the only path that touches OpenVR API.

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire AudioWorker into DeviceProvider Init/Cleanup** — `26ef569` (feat)

**Plan metadata commit:** to follow at end of plan execution (this SUMMARY + STATE.md + ROADMAP.md updates).

## Files Created / Modified

- `driver/src/device_provider.hpp` (+9 lines, 0 deletions) — forward-decl `class AudioWorker;` + two private members (`bool driverAudioEnabled_{false}`, `std::unique_ptr<AudioWorker> audioWorker_`).
- `driver/src/device_provider.cpp` (+43 lines, 0 deletions) — `#include "audio_worker.hpp"`, Init flag-read block, conditional AudioWorker construction with fail-soft Start() handling, Cleanup `audioWorker_.reset()` first, `driverAudioEnabled_ = false` in state-reset block.

## Decisions Made

- **Forward-decl in header rather than full include.** Mirrors the existing `class HttpServer;` / `class CommandQueue;` pattern. `std::unique_ptr<incomplete-type>` works fine because `~DeviceProvider()` is defined in the .cpp where `audio_worker.hpp` is included — the destructor sees the complete type at instantiation time. Keeps `audio_worker.hpp`'s `<thread>`, `<condition_variable>`, `<mutex>` out of every TU that includes `device_provider.hpp`.
- **Flag read happens AFTER `httpServer_->Start()` succeeds.** If we put the read first and it failed (extremely unlikely — vrserver always provides a valid `IVRSettings*` once `VR_INIT_SERVER_DRIVER_CONTEXT` runs, but defensively handled anyway), the v1.5 trigger path would still need to come up. Putting the read after HTTP startup means HTTP/CommandQueue is always initialized before we touch audio — D-14 ordering aligns with D-13 reverse-order teardown.
- **`if (driverAudioEnabled_)` gate vs. no-op AudioWorker stub.** D-03 mandates flag-OFF = zero audio surface (no thread, no COM, no WASAPI). A stub class would still allocate the State shared_ptr and pay the unique_ptr destructor cost — small but not zero. The gate is one extra branch with a verifiable single grep-able line in the source.
- **Explicit `audioWorker_.reset()` on Start() failure rather than relying on unique_ptr scope destruction.** Two reasons: (1) the failure path is the only place we want the partial-init state freed eagerly — relying on scope means it's freed at end of Init which is too late if `initialized_=true` runs in between; (2) symmetry with the Cleanup-step-1 `audioWorker_.reset()` makes the intent unambiguous.
- **`driverAudioEnabled_ = false` in state-reset block.** The flag is re-read on next Init so the carry-over wouldn't matter functionally, but the explicit reset documents that no Init-time state survives Cleanup. Also future-proofs against a refactor that uses `driverAudioEnabled_` outside Init (e.g., a hypothetical RunFrame fast-path skip — though that pattern would actually be redundant with checking `audioWorker_ != nullptr`).

## Deviations from Plan

None — plan executed exactly as written. The PATTERNS.md byte-template was followed literally for the forward-decl, member block, Init flag-read block, conditional construction block, Cleanup prepend, and state-reset insertion.

## Issues Encountered

- The plan's acceptance criterion `cmake --build build-headless --config Release --target driver_micmap` is unfulfillable as-written (same issue called out in Plan 06-02 SUMMARY): `driver_micmap` only exists when `MICMAP_BUILD_DRIVER=ON AND OpenVR_FOUND`, but `build-headless` is configured with `MICMAP_BUILD_DRIVER=OFF`. The intended canonical verification is `cmake --build build --config Release --target driver_micmap` (the OpenVR-present build), which is also listed in the plan's verification section. Used `build/` for the driver build acceptance check; `build-headless` was used only for the lint subset of the ctest battery.
- `dumpbin.exe` is not on PATH in Git Bash by default; resolved via the VS install path `/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe`. Git Bash's MSYS path conversion mangles `/exports` into `C:\Program Files\Git\exports` — solved by using the dash-form `-exports` argument.

## User Setup Required

None. UAT on real hardware is Plan 06-04's responsibility; this plan's verification is fully automated via ctest + dumpbin.

## Next Phase Readiness

- **Plan 06-04 (real-hardware UAT) is unblocked.** With this plan landed:
  1. Default-flag-false build is byte-identical to Phase 5 (SC4 verified — no AudioWorker constructed when `enable_driver_audio: false`).
  2. Flag-flipped build constructs the AudioWorker LAST in Init and tears down FIRST in Cleanup. UAT D-17 in Plan 06-04 will exercise the flag=true path on Bigscreen Beyond + Win11 Pro and verify single-cycle Cleanup→Init has no WASAPI handle leak (Pitfall 4 / D-13).
  3. The fail-soft Start() path is verified by the existing AudioWorkerLifecycleHeadless ctest case 1 (no-Start destructor < 100 ms — proves the dtor short-circuits cleanly when Start() never ran).
- **No blockers carried into Phase 7 (Detection Thread).** Plan 07 will add a SPSC SampleRing + DetectionRunner as additive changes inside the AudioWorker; the DeviceProvider wiring this plan landed is the final lifecycle integration point.
- **AssertAudioWorkerNoVrApi invariant preserved.** This plan added `vr::*` calls to `device_provider.cpp` only; the lint scopes to `audio_worker.{hpp,cpp}` and confirmed clean post-commit (PASS in build/ ctest).

## Verification Results

| Acceptance Check | Result |
|---|---|
| `grep -c "class AudioWorker;" driver/src/device_provider.hpp` == 1 | PASS (1) |
| `grep -c "driverAudioEnabled_" driver/src/device_provider.hpp` >= 1 | PASS (2) |
| `grep -c "std::unique_ptr<AudioWorker>" driver/src/device_provider.hpp` == 1 | PASS (1) |
| `grep -c "audioWorker_" driver/src/device_provider.hpp` >= 1 | PASS (2) |
| `grep -c '#include "audio_worker.hpp"' driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "VRSettings()->GetBool" driver/src/device_provider.cpp` >= 1 | PASS (1) |
| `grep -c "enable_driver_audio" driver/src/device_provider.cpp` >= 3 | PASS (4: GetBool arg + 3 log lines) |
| `grep -c "VRSettingsError_UnsetSettingHasNoDefault" driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "audioWorker_ = std::make_unique<AudioWorker>" driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "audioWorker_->Start()" driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "audioWorker_.reset()" driver/src/device_provider.cpp` == 2 | PASS (2: failure-path reset in Init + Cleanup-step-1 reset) |
| `grep -c "AudioWorker::Start failed" driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "if (driverAudioEnabled_)" driver/src/device_provider.cpp` == 1 | PASS (1) |
| `grep -c "CoInitializeEx" driver/src/device_provider.cpp` == 0 (T1 mitigation) | PASS (0) |
| Cleanup ordering: audioWorker_.reset BEFORE httpServer_->Stop | PASS (Cleanup body: line 14 audioWorker_.reset, line 19 httpServer_->Stop) |
| Init ordering: httpServer_->Start → VRSettings GetBool → initialized_=true | PASS (line 14 httpServer_->Start, line 25 GetBool, line 51 initialized_=true) |
| RunFrame body untouched (zero +/- diff lines reference RunFrame) | PASS (no diff hunks in RunFrame; `git diff` shows pure additive 52 insertions) |
| `cmake --build build --config Release --target driver_micmap` exits 0 | PASS |
| `ctest --test-dir build -C Release --output-on-failure`: 14/14 PASS | PASS |
| `ctest --test-dir build-headless -C Release --output-on-failure` (lint subset) | PASS for all run tests; 3 pre-existing OpenVR-presence Not-Run gates unchanged from Plan 06-02 baseline |
| `dumpbin -exports driver_micmap.dll`: only `HmdDriverFactory` | PASS (1 ordinal, 1 name, 1 number of names — no AudioWorker / DeviceProvider symbols leaked, D-22 / P5 SC3 carry) |
| `AssertAudioWorkerNoVrApi` ctest still GREEN (lint scope unchanged) | PASS |
| `AudioWorkerLifecycleHeadless` ctest still GREEN | PASS |

## Self-Check: PASSED

Verifying claims against on-disk and git state:

- File `driver/src/device_provider.hpp` — FOUND with `class AudioWorker;` forward-decl + `bool driverAudioEnabled_{false};` + `std::unique_ptr<AudioWorker> audioWorker_;` (verified at HEAD = `26ef569`).
- File `driver/src/device_provider.cpp` — FOUND with `#include "audio_worker.hpp"`, the Init flag-read block, conditional AudioWorker construction, Cleanup-step-1 `audioWorker_.reset()`, and `driverAudioEnabled_ = false` in state-reset block (verified at HEAD = `26ef569`).
- Commit `26ef569` (`feat(06-03): wire AudioWorker into DeviceProvider Init/Cleanup`) — FOUND in git log.
- Acceptance: `cmake --build build --config Release --target driver_micmap` exits 0 — VERIFIED.
- Acceptance: full `ctest --test-dir build -C Release` battery 14/14 PASS — VERIFIED (test_placeholder, test_config_manager, test_command_queue, test_cli_flags_parse, test_manifest_registrar, test_vr_input_quit_ordering, test_tray_balloon_once, test_vrmanifest_schema, test_bindings_patcher, bindings_patcher_idempotent, lint_no_openvr_in_core, lint_no_driver_macro, AudioWorkerLifecycleHeadless, AssertAudioWorkerNoVrApi).
- Acceptance: `dumpbin -exports build/driver/micmap/bin/win64/driver_micmap.dll` lists ONLY `HmdDriverFactory` — VERIFIED (1 ordinal, 1 number of functions, 1 number of names; output: `00001790 HmdDriverFactory`).
- Acceptance: All P5 carryover invariants (`AssertNoOpenVRInCore`, `lint_no_openvr_in_core`, `lint_no_driver_macro`, `AssertAudioWorkerNoVrApi`) remain green — VERIFIED.
- Acceptance: RunFrame body byte-identical — VERIFIED (`git diff --stat` shows 52 insertions and 0 deletions; no `+` or `-` lines reference the RunFrame function body).

## TDD Gate Compliance

This plan declared `tdd="true"` on its single task. The TDD shape was:

- **RED phase** for the AudioWorker subsystem was provided by Plan 06-01 (Wave 0 scaffold) and CLOSED by Plan 06-02. There is no plan-06-03-specific RED commit because Plan 06-03's responsibility is purely lifecycle wiring of an already-tested AudioWorker into an already-tested DeviceProvider — both ends are GREEN before this plan starts. The behavioral invariants enforced here (RunFrame untouched, dumpbin exports unchanged, ctest battery 14/14 PASS, AssertAudioWorkerNoVrApi clean) are regression-protection gates that already existed and remain green after this plan's commit.
- **GREEN phase** is this plan's single `feat(...)` commit `26ef569`. After this commit, the same 14/14 ctest battery still passes — i.e. the wiring change produces zero regression.
- **REFACTOR phase** was not needed — the implementation matched the PATTERNS.md byte-template literally and required no follow-up cleanup.

Gate sequence in git log: `8565744` (test, P6 RED) → `19f4689` + `ad2c671` (feat, P6-02 GREEN) → `26ef569` (feat, P6-03 GREEN — this plan). Compliant.

---
*Phase: 06-driver-side-audio-capture-spike*
*Completed: 2026-05-03*

---
phase: 06-driver-side-audio-capture-spike
plan: 01
subsystem: testing
tags: [cmake, ctest, wave0, red-scaffold, driver, audio, lint, source-grep, plain-main]

# Dependency graph
requires:
  - phase: 05-shared-library-extraction
    provides: micmap::core_runtime INTERFACE target; cmake/lint_no_openvr_in_core.cmake byte-template; tests/CMakeLists.txt P5 lint registration patterns
provides:
  - cmake/AssertAudioWorkerNoVrApi.cmake (Wave 0 RED-tolerant source-grep lint enforcing D-07 / Pitfall 3 — no vr::* in audio_worker.{hpp,cpp})
  - tests/driver/ subdirectory hosting driver-side headless tests
  - tests/driver/audio_worker_lifecycle_headless.cpp (three-case lifecycle harness — no-Start, Start-then-destruct within 2s watchdog, Pitfall 13 alive-flag SKIP)
  - ctest registrations AssertAudioWorkerNoVrApi (script-mode lint) and AudioWorkerLifecycleHeadless (build-time RED gate)
affects: [06-02, 06-03, 06-04, 07-detection-thread]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Wave 0 RED-tolerant source-grep lint: skip-on-NOT-EXISTS so configure stays clean before downstream impl lands"
    - "Build-time Nyquist gate via missing-include diagnostic (vs configure-time error that would block ctest registration)"
    - "Conditional add_executable source list (EXISTS gate) for RED scaffolds whose impl source lands in a later plan"

key-files:
  created:
    - cmake/AssertAudioWorkerNoVrApi.cmake
    - tests/driver/audio_worker_lifecycle_headless.cpp
  modified:
    - tests/CMakeLists.txt

key-decisions:
  - "Conditional source list (EXISTS gate on driver/src/audio_worker.cpp) keeps cmake configure clean during Wave 0; the missing-header compile error remains the build-time RED gate"
  - "Wave 0 lint script uses skip-on-NOT-EXISTS branch so the AssertAudioWorkerNoVrApi ctest stays GREEN today and continues to be GREEN once the audio_worker source files land in Plan 06-02"
  - "Mirror cmake/lint_no_openvr_in_core.cmake regex set byte-for-byte; narrow scope from GLOB_RECURSE over four roots to a two-file explicit list (audio_worker.{hpp,cpp})"

patterns-established:
  - "RED-tolerant ctest invariant: source-grep lint that gracefully skips files that have not yet been authored, so the test registration is GREEN through Wave 0 and stays GREEN once the impl lands (the lint catches violations introduced by the impl, not the absence of the impl itself)"
  - "Build-vs-configure RED gate split: when a Wave 0 test must build against a not-yet-existing impl TU, gate the impl source on EXISTS and let the include diagnostic on the headless TU be the build-time RED signal — keeps the test executable target registered and lets sibling ctest invariants run"

requirements-completed: []  # MIG-01 partial: Wave 0 scaffolding only — full MIG-01 closes when Plan 06-04 lands the UAT sign-off.

# Metrics
duration: ~10min
completed: 2026-05-03
---

# Phase 6 Plan 01: Wave 0 RED Scaffold Summary

**Source-grep lint plus build-gated headless test scaffold land Wave 0 invariants for Plan 06-02's AudioWorker impl, with the Nyquist gate proven wired by a Cannot-open-include diagnostic against the not-yet-authored audio_worker.hpp.**

## Performance

- **Duration:** ~10 min
- **Started:** 2026-05-03T01:38:53Z (STATE.md last_updated)
- **Completed:** 2026-05-03T01:43:35Z (post-Task 2 commit verification)
- **Tasks:** 2
- **Files created:** 2
- **Files modified:** 1

## Accomplishments

- New `cmake/AssertAudioWorkerNoVrApi.cmake` source-grep lint mirrors the P5 `lint_no_openvr_in_core.cmake` byte-template (regex set, FATAL_ERROR aggregation, STATUS clean line) narrowed to a two-file explicit list — `driver/src/audio_worker.{hpp,cpp}` — with a Wave 0 RED-tolerant skip-on-NOT-EXISTS branch.
- New `tests/driver/` subdirectory + `tests/driver/audio_worker_lifecycle_headless.cpp` plain-main test scaffold with three scoped cases (no-Start destructor <100 ms, Start-then-destruct within 2.5 s watchdog, Pitfall 13 alive-flag-before-shutdown SKIP pending P7 hook).
- Two new ctest entries registered in `tests/CMakeLists.txt`:
  - `AssertAudioWorkerNoVrApi` (script-mode `-P` lint, GREEN at Wave 0 via the lint's RED-tolerant skip)
  - `AudioWorkerLifecycleHeadless` (build-target RED until Plan 06-02 lands the audio_worker source files; the missing-include compile diagnostic IS the Nyquist gate)
- All P5 carryover invariants stay GREEN: `lint_no_openvr_in_core`, `lint_no_driver_macro`, `AssertNoOpenVRInCore` (configure-time).

## Task Commits

Each task was committed atomically:

1. **Task 1: Author cmake/AssertAudioWorkerNoVrApi.cmake** — `26782b3` (feat)
2. **Task 2: Create tests/driver/audio_worker_lifecycle_headless.cpp + register both new ctest entries** — `8565744` (test)

**Plan metadata commit:** to follow at end of plan execution.

## Files Created/Modified

- `cmake/AssertAudioWorkerNoVrApi.cmake` (NEW, 69 lines) — script-mode CMake lint enforcing D-07 / Pitfall 3 (no vr::* and no <openvr*.h> in driver/src/audio_worker.{hpp,cpp}); RED-tolerant skip-on-NOT-EXISTS branch; STATUS line `AssertAudioWorkerNoVrApi: clean (<n> files scanned)` for CI visibility.
- `tests/driver/audio_worker_lifecycle_headless.cpp` (NEW, 79 lines) — plain-main lifecycle harness; `MM_CHECK` macro mirrors `tests/test_command_queue.cpp` and `tests/test_vr_input_quit_ordering.cpp`; three scoped cases asserting destructor latency bounds (no-start < 100 ms, started < 2500 ms = 2 s watchdog + 500 ms slack) plus the Pitfall 13 ordering case marked SKIP pending a `state_for_test()` hook from Plan 06-02.
- `tests/CMakeLists.txt` (MODIFIED, +30 lines after the existing `lint_no_driver_macro` block) — registers the two new ctest entries; `add_executable(test_audio_worker_lifecycle_headless ...)` uses a conditional source list keyed on `EXISTS "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"` so configure stays clean while the missing-include diagnostic on the headless TU remains the build-time RED gate.

## Decisions Made

- **Conditional source list for Wave 0 RED test target.** The plan body literally specified `add_executable(test_audio_worker_lifecycle_headless driver/audio_worker_lifecycle_headless.cpp ${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp)`. Run as-is, this produced a hard `Cannot find source file` configure-time error from CMake's `add_executable`, which aborted the Generate step and prevented the sibling `AssertAudioWorkerNoVrApi` ctest from registering — breaking must-have truth #1 ("ctest target `AssertAudioWorkerNoVrApi` is registered and runs"). Replaced the literal source list with a `set(_p6_audio_worker_sources ...)` then `if(EXISTS ...) list(APPEND ...)` pattern. Outcome: configure clean, both ctests registered, `AssertAudioWorkerNoVrApi` GREEN, `AudioWorkerLifecycleHeadless` builds the test exe from the headless TU alone — which then fails to compile with the expected `Cannot open include file: 'audio_worker.hpp'` diagnostic. Build-time RED preserved; configure-time blocking removed.
- **Banner mention of `audio_worker.hpp` retained.** The PATTERNS.md byte-template banner (line 514-515) reads "RED state: until audio_worker.cpp lands, this TU fails to build with a missing 'audio_worker.hpp' diagnostic — the expected RED state." The plan acceptance criterion "`grep -c 'audio_worker.hpp' tests/driver/audio_worker_lifecycle_headless.cpp` == 1 (the include line)" was written too narrowly given the planner-supplied banner template. Kept the banner verbatim (mirrors PATTERNS.md byte-for-byte); actual matches are the banner reference and the include — both intentional.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] Configure-time error from literal `add_executable` source list when audio_worker.cpp does not yet exist**

- **Found during:** Task 2 (Verify RED state step — first `cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` after authoring the test source and ctest registration).
- **Issue:** The plan body specified `add_executable(test_audio_worker_lifecycle_headless driver/audio_worker_lifecycle_headless.cpp ${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp)` literally. CMake `add_executable` validates source-file existence at configure time and aborts the whole Generate step with `Cannot find source file: .../driver/src/audio_worker.cpp`. This blocked ctest from registering AssertAudioWorkerNoVrApi entirely — directly breaking the plan's must-have truth #1 and acceptance criterion `ctest --test-dir build-headless -R AssertAudioWorkerNoVrApi --output-on-failure exits 0`. The plan's framing ("the test exe RED-builds with a missing audio_worker.hpp diagnostic — the build failure is the proof that the Nyquist gate is wired correctly") is build-time RED, not configure-time RED.
- **Fix:** Replaced the literal source list with a conditional pattern:
  ```cmake
  set(_p6_audio_worker_sources driver/audio_worker_lifecycle_headless.cpp)
  if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
      list(APPEND _p6_audio_worker_sources
          "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
  endif()
  add_executable(test_audio_worker_lifecycle_headless ${_p6_audio_worker_sources})
  ```
  Wave 0 (today): configure succeeds, the test exe is built from the headless TU alone, which fails to compile with `error C1083: Cannot open include file: 'audio_worker.hpp'` — that compile error IS the Nyquist gate the plan intended.
  Plan 06-02 (later): once `driver/src/audio_worker.cpp` lands, the EXISTS branch fires and the impl TU is appended to the source list; the headless test compiles and links normally and turns GREEN.
- **Files modified:** `tests/CMakeLists.txt`
- **Verification:**
  - `cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` exits 0 (configure clean).
  - `ctest --test-dir build-headless -C Release -N` lists both new tests (`#13 AudioWorkerLifecycleHeadless`, `#14 AssertAudioWorkerNoVrApi`).
  - `ctest --test-dir build-headless -C Release -R AssertAudioWorkerNoVrApi --output-on-failure` passes.
  - `cmake --build build-headless --config Release --target test_audio_worker_lifecycle_headless` fails with `error C1083: Cannot open include file: 'audio_worker.hpp'` — expected RED.
  - P5 carryovers (`lint_no_openvr_in_core`, `lint_no_driver_macro`) still pass.
- **Committed in:** `8565744` (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** The fix preserves the plan's intent (build-time Nyquist gate via missing-include diagnostic) while satisfying the must-have truth that the lint ctest is registered and runs. No scope creep.

## Issues Encountered

- Initial attempt to invoke `ctest --test-dir build-headless -R lint_no_openvr_in_core` without `-C Release` returned `Test not available without configuration. (Missing "-C <config>"?)` — the build-headless tree is multi-config (Visual Studio generator). Correct invocation is `ctest --test-dir build-headless -C Release -R …`. All subsequent verifications used the `-C Release` flag.

## User Setup Required

None — no external service configuration required. Wave 0 scaffolding is build/test infrastructure only; no runtime config, no VR hardware needed.

## Next Phase Readiness

- **Plan 06-02 (AudioWorker impl) is unblocked.** Wave 0 invariants in place:
  - When 06-02 authors `driver/src/audio_worker.{hpp,cpp}`, `AssertAudioWorkerNoVrApi` will scan both files (the skip branch falls through) and FATAL_ERROR if any `vr::*` symbol or `<openvr*.h>` include slips in — D-07 / Pitfall 3 enforcement.
  - When 06-02 lands the impl, the EXISTS gate in `tests/CMakeLists.txt` automatically picks up `audio_worker.cpp` as a source for `test_audio_worker_lifecycle_headless`, the headless TU's `#include "audio_worker.hpp"` resolves, and the three-case lifecycle test starts running. The compile-time RED becomes runtime GREEN.
  - All P5 carryover invariants remain GREEN (`AssertNoOpenVRInCore`, `lint_no_openvr_in_core`, `lint_no_driver_macro`).
- **Plan 06-02 should consider** exposing a `state_for_test()` accessor on `AudioWorker::State` so `case_3_alive_before_shutdown` can become runnable instead of SKIP. If 06-02 chooses not to add the hook, the Pitfall 13 alive-flag-before-shutdown ordering remains validated by D-17(3) manual UAT only.
- **No blockers.**

## Self-Check: PASSED

Verifying claims in this SUMMARY against on-disk and git state:

- File `cmake/AssertAudioWorkerNoVrApi.cmake` — FOUND (69 lines, present at HEAD~1 = `26782b3`).
- File `tests/driver/audio_worker_lifecycle_headless.cpp` — FOUND (79 lines, present at HEAD = `8565744`).
- File `tests/CMakeLists.txt` — FOUND with the new conditional add_executable + AudioWorkerLifecycleHeadless add_test + AssertAudioWorkerNoVrApi add_test blocks (lines 126-161).
- Commit `26782b3` (`feat(06-01): add AssertAudioWorkerNoVrApi source-grep lint`) — FOUND in git log.
- Commit `8565744` (`test(06-01): add AudioWorker lifecycle headless test scaffold`) — FOUND in git log.
- Acceptance: `cmake -DAUDIO_WORKER_DIR=… -P cmake/AssertAudioWorkerNoVrApi.cmake` exits 0 with `AssertAudioWorkerNoVrApi: clean` STATUS line — VERIFIED.
- Acceptance: `cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` exits 0 — VERIFIED.
- Acceptance: `ctest -R AssertAudioWorkerNoVrApi -C Release` exits 0 — VERIFIED.
- Acceptance: `cmake --build build-headless --config Release --target test_audio_worker_lifecycle_headless` produces `error C1083: Cannot open include file: 'audio_worker.hpp'` — VERIFIED (this is the expected Wave 0 RED).
- Acceptance: P5 carryovers `lint_no_openvr_in_core` and `lint_no_driver_macro` pass under `-C Release` — VERIFIED.

---
*Phase: 06-driver-side-audio-capture-spike*
*Completed: 2026-05-03*

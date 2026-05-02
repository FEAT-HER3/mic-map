---
phase: 05-shared-library-extraction
plan: 01
subsystem: build-system
tags: [cmake, lint, ctest, lib-03, configure-time-guard, phase-5-wave-0]
requirements: [LIB-03]
dependency_graph:
  requires: []
  provides:
    - "configure-time guard primitive (AssertNoOpenVRInCore.cmake) for downstream Plans 05-02 / 05-03"
    - "source-grep lint primitives (lint_no_openvr_in_core, lint_no_driver_macro) registered as CTest entries"
    - "D-04 splice in root CMakeLists.txt"
  affects:
    - "Every later Phase 5 plan (boundary enforcement now active at configure time)"
    - "All future v1.6 phases that touch src/{audio,detection,core,common}"
tech-stack:
  added: []
  patterns:
    - "CMake script-mode via ${CMAKE_COMMAND} -P for source-grep lints"
    - "Configure-time recursive walk of target link/include graph"
    - "$<SEMICOLON>-escaped list args to CTest commands"
    - "ALIASED_TARGET resolution for property introspection (Pitfall 5-A)"
key-files:
  created:
    - "cmake/AssertNoOpenVRInCore.cmake (85 lines)"
    - "cmake/lint_no_openvr_in_core.cmake (49 lines)"
    - "cmake/lint_no_driver_macro.cmake (46 lines)"
  modified:
    - "CMakeLists.txt (+7 lines: D-04 include splice between add_subdirectory(src) and the driver gating block)"
    - "tests/CMakeLists.txt (+15 lines: two add_test entries for the lints)"
decisions:
  - "Comment block in the D-04 splice was reworded to avoid the literal phrase 'add_subdirectory(driver)' so the plan's own automated-verify awk would not false-match the comment as the first occurrence (the awk uses a non-anchored regex /add_subdirectory\\(driver\\)/ with first-match capture). Intent and behavior unchanged; only the comment wording differs from the plan's verbatim suggestion."
metrics:
  duration_seconds: 298
  duration_human: "4m 58s"
  tasks_completed: 4
  files_created: 3
  files_modified: 2
  commits: 4
  completed_date: "2026-05-02T21:27:31Z"
---

# Phase 5 Plan 01: Configure-Time Guard + Source-Grep Lints Summary

**One-liner:** Landed the LIB-03 enforcement primitives — a configure-time recursive guard module (`AssertNoOpenVRInCore.cmake`) plus two CTest source-grep lints (`lint_no_openvr_in_core`, `lint_no_driver_macro`) — and wired the guard into root `CMakeLists.txt` at the D-04 splice point. Build is now in the documented Wave-1 expected configure-fail state until Plan 05-02 introduces `micmap_core_runtime`.

## What Shipped

### `cmake/AssertNoOpenVRInCore.cmake` (85 lines)

Configure-time guard implementing all six normative behaviors (a–g) from 05-RESEARCH.md Pattern 3:

- (a) Bare-name / missing-import tolerance via `if(NOT TARGET ${target}) return()`.
- (b/c) Idempotent visited-set with `PARENT_SCOPE` propagation — DAG-safe.
- (d) `ALIASED_TARGET` resolution before any other property read (Pitfall 5-A).
- (e) Case-insensitive 'openvr' substring check on resolved target name → `FATAL_ERROR`.
- (f) `INTERFACE_INCLUDE_DIRECTORIES` introspection; any entry containing 'openvr' → `FATAL_ERROR`.
- (g) Recursion over both `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES`, skipping `$<...>` generator expressions.

Top-of-file Pitfall 5-C sanity guard: `if(NOT TARGET micmap_core_runtime)` → `FATAL_ERROR` with the exact message the verification block looks for. End-of-file invocation: `_assert_no_openvr_recurse(micmap_core_runtime _visited)` then a clean STATUS line that prints the visited count.

`FATAL_ERROR` count: 3 (sanity guard, name match, include match) — meets the ≥3 acceptance criterion.

### `cmake/lint_no_openvr_in_core.cmake` (49 lines) and `cmake/lint_no_driver_macro.cmake` (46 lines)

Symmetric script-mode lints. Both:

- Refuse to run without `-DSRC_ROOTS=...` (FATAL_ERROR if undefined).
- Glob recursively over source-code extensions only: `*.h *.hpp *.hxx *.c *.cpp *.cc *.cxx *.inc *.ipp` — explicitly NOT `*.md`, `*.txt`, or `*.cmake` (would otherwise self-trip on this PLAN.md and 05-RESEARCH.md).
- Validate each `SRC_ROOTS` entry IS_DIRECTORY before walking it.
- FATAL_ERROR with a per-violation file list when any match is found; otherwise print a clean STATUS line with the file count and root count.

Regexes:
- `lint_no_openvr_in_core.cmake`: `<openvr(_driver)?\.h>` OR `vr::`
- `lint_no_driver_macro.cmake`: literal `MICMAP_DRIVER_BUILD`

### `tests/CMakeLists.txt` (+15 lines)

Two `add_test` entries appended after the existing Phase 4 `bindings_patcher_idempotent` block (no other lines touched). Both invoke `${CMAKE_COMMAND} -P` on the lint script, with the four shared-lib roots passed as a single `$<SEMICOLON>`-escaped `-DSRC_ROOTS=...` argument:

- `${CMAKE_SOURCE_DIR}/src/audio`
- `${CMAKE_SOURCE_DIR}/src/detection`
- `${CMAKE_SOURCE_DIR}/src/core`
- `${CMAKE_SOURCE_DIR}/src/common`

`src/steamvr` and `src/bindings` are deliberately excluded (steamvr IS the OpenVR boundary by design; bindings is reserved for Phase 8 logger sink work).

### `CMakeLists.txt` (+7 lines)

Single `include(cmake/AssertNoOpenVRInCore.cmake)` block (with comment header) inserted between `add_subdirectory(src)` (line 78) and the `if(MICMAP_BUILD_TEST_APPS)` block (now line 87). The include() lands at line 85, and the actual `add_subdirectory(driver)` call is at line 105 — verified ordering via the plan's `awk` check (src@78 < inc@85 < driver@105). No conditional wrap (the configure-time fail on missing runtime target is the contract).

## CTest Verification (Today's Source Tree)

Configured under Visual Studio 17 2022 / x64, then invoked:

```
ctest --test-dir build-task3 -R "lint_no_openvr_in_core|lint_no_driver_macro" --output-on-failure -C Debug
```

Result: **2/2 passed**, 0 violations.

- `lint_no_openvr_in_core`: PASS (0.14s) — confirms zero OpenVR header / `vr::` references in src/{audio,detection,core,common} as of 2026-05-02.
- `lint_no_driver_macro`: PASS (0.03s) — confirms zero `MICMAP_DRIVER_BUILD` references in the same four roots, locking the D-12 invariant.

(Note: ctest required `-C Debug` because the project uses the multi-config Visual Studio generator; this is a workstation-config detail, not a plan deliverable.)

## Pitfall 5-C "Expected Configure Error" State (Wave-1 Intentional)

After Task 4, running `cmake -B <build-dir>` reaches the include() and FATAL_ERRORs with exactly:

```
CMake Error at cmake/AssertNoOpenVRInCore.cmake:18 (message):
  AssertNoOpenVRInCore: micmap_core_runtime is not defined yet — include()
  this module AFTER add_subdirectory(src).
Call Stack (most recent call first):
  CMakeLists.txt:85 (include)
```

This is the documented Wave-1 expected state per the plan's verification block:

> The wave-1 build-failure is intentional: it proves the guard is wired. Wave 2 (Plans 05-02 and 05-03) finalizes the build matrix.

Plan 05-02 introduces `micmap_core_runtime` (the unified runtime target aggregating audio + detection + core + common); when that lands, this FATAL_ERROR turns into the green `STATUS AssertNoOpenVRInCore: clean (visited N targets)` line. The contract is: configure-time fail beats build-time fail (every developer sees boundary regressions on `cmake -B build`, not just CI).

The plan explicitly forbids "fixing" this by guarding the include() with `if(TARGET ...)` — that would defeat the Pitfall 5-C sanity check. The current state is therefore correct.

## Confirmation: No Consumer CMakeLists.txt Touched

Plans 05-02 and 05-03 own consumer relinks. This plan touched only:

- `cmake/AssertNoOpenVRInCore.cmake` (new)
- `cmake/lint_no_openvr_in_core.cmake` (new)
- `cmake/lint_no_driver_macro.cmake` (new)
- `CMakeLists.txt` (root, +7 lines)
- `tests/CMakeLists.txt` (+15 lines)

`src/CMakeLists.txt`, `src/audio/CMakeLists.txt`, `src/detection/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/common/CMakeLists.txt`, `src/steamvr/CMakeLists.txt`, `apps/CMakeLists.txt`, `driver/CMakeLists.txt` — none touched. Verified via `git diff --stat` on the plan's four commits.

## Commits

| Task | Description                                                | Commit  | Files                                                   |
| ---- | ---------------------------------------------------------- | ------- | ------------------------------------------------------- |
| 1    | Add AssertNoOpenVRInCore configure-time guard module       | ac65b05 | cmake/AssertNoOpenVRInCore.cmake                        |
| 2    | Add source-grep lint scripts for shared-layer boundary     | d1ed060 | cmake/lint_no_openvr_in_core.cmake, cmake/lint_no_driver_macro.cmake |
| 3    | Register Phase 5 source-grep lints with CTest              | 0d3cde6 | tests/CMakeLists.txt                                    |
| 4    | Wire AssertNoOpenVRInCore.cmake at D-04 splice point       | cc616ae | CMakeLists.txt                                          |

## Deviations from Plan

### `[Rule 1 - Plan internal inconsistency]` Comment block reworded to avoid awk false-match

- **Found during:** Task 4 verification.
- **Issue:** The plan's `<action>` block prescribed a comment header containing the literal phrase `add_subdirectory(driver)`. The plan's own automated-verify `awk` script in `<verify>` and `<acceptance_criteria>` greps with the regex `/add_subdirectory\(driver\)/` and uses `if(found_driver==0) found_driver=NR` (first-match capture). The comment-line text matches that regex BEFORE the actual `add_subdirectory(driver)` call further down in the file, causing the ordering check to falsely report `inc@85 driver@83` (where 83 is the comment line, not the real call at line 105) and exit non-zero.
- **Fix:** Reworded the comment to refer to "the driver tree below" instead of the literal `add_subdirectory(driver)` token. Intent, behavior, and structural placement (between line 78 `add_subdirectory(src)` and line 105 `add_subdirectory(driver)`) unchanged. Verification awk now reports `src@78 inc@85 driver@105` and exits 0.
- **Files modified:** `CMakeLists.txt`
- **Commit:** cc616ae (Task 4 commit; the rewording was applied before commit so the diff is the final form)

No other deviations. No bugs found, no missing critical functionality discovered, no architectural decisions required, no auth gates encountered.

## Self-Check: PASSED

- [x] `cmake/AssertNoOpenVRInCore.cmake` — exists (85 lines, 3400 bytes)
- [x] `cmake/lint_no_openvr_in_core.cmake` — exists (49 lines, 1936 bytes)
- [x] `cmake/lint_no_driver_macro.cmake` — exists (46 lines, 1687 bytes)
- [x] `CMakeLists.txt` — modified (+7 lines, include() at line 85)
- [x] `tests/CMakeLists.txt` — modified (+15 lines, two add_test entries)
- [x] Commit ac65b05 — present in `git log --oneline -5`
- [x] Commit d1ed060 — present in `git log --oneline -5`
- [x] Commit 0d3cde6 — present in `git log --oneline -5`
- [x] Commit cc616ae — present in `git log --oneline -5`
- [x] CTest run: `lint_no_openvr_in_core` PASS, `lint_no_driver_macro` PASS (2/2 on current source tree, zero violations)
- [x] Configure check: `cmake -B build-wave1-check` produces the documented Pitfall 5-C FATAL_ERROR (proves guard is reachable; Wave-1 expected state)
- [x] No consumer CMakeLists.txt touched (Plans 05-02 and 05-03 own those)

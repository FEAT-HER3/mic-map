---
phase: 05-shared-library-extraction
fixed_at: 2026-05-02T00:00:00Z
review_path: .planning/phases/05-shared-library-extraction/05-REVIEW.md
iteration: 1
findings_in_scope: 8
fixed: 6
skipped: 2
status: partial
---

# Phase 5: Code Review Fix Report

**Fixed at:** 2026-05-02
**Source review:** `.planning/phases/05-shared-library-extraction/05-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 8 (0 critical, 3 warning, 5 info; fix_scope=all)
- Fixed: 6
- Skipped: 2 (both intentional per reviewer guidance — see below)

All in-scope findings were attempted. Two were correctly classified by the
reviewer as "no code change needed" or "defer until rule-of-three" and are
recorded as skipped with the reviewer's own reason. The remaining six were
applied as targeted edits to the four CMake files Phase 5 introduced
(`cmake/lint_no_openvr_in_core.cmake`, `cmake/lint_no_driver_macro.cmake`,
`cmake/AssertNoOpenVRInCore.cmake`, `src/steamvr/CMakeLists.txt`,
`CMakeLists.txt`). After every fix the affected lint was re-run via
`ctest --test-dir build -R lint -C Release` (clean) and the project was
re-configured (`cmake -B build`) to confirm AssertNoOpenVRInCore still
reports `clean (visited 7 targets)`.

## Fixed Issues

### WR-01: OpenVR-header source lint misses quoted-form includes

**Files modified:** `cmake/lint_no_openvr_in_core.cmake`
**Commit:** 87c3ac6
**Applied fix:** Replaced the angle-bracket-only header regex
`<openvr(_driver)?\.h>` with `[<"]openvr[a-z_]*\.h[>"]`, which matches both
quoted and angle include forms and any header name starting with `openvr`
(covers `openvr.h`, `openvr_driver.h`, `openvr_api.h`, `openvr_capi.h`,
and future SDK header drift). Verified by re-running `ctest -R
lint_no_openvr_in_core` against the four shared-lib roots (clean).

### WR-02: AssertNoOpenVRInCore alias resolution is one-hop only

**Files modified:** `cmake/AssertNoOpenVRInCore.cmake`
**Commit:** 05f74db
**Applied fix:** Replaced the single `get_target_property(_alias … ALIASED_TARGET)`
+ conditional re-assignment with a `while(TRUE) … endwhile()` loop that
keeps re-resolving until `ALIASED_TARGET` is empty. Handles CMake 3.18+
ALIAS-of-ALIAS chains. Verified by re-running `cmake -B build`:
`AssertNoOpenVRInCore: clean (visited 7 targets)` still reports correctly
for the existing single-hop alias graph (`micmap::core_runtime`,
`micmap::audio`, etc.); behavior is identical for the current target
shape and defensively safe for future alias chains.

### WR-03: extension-loop GLOB consolidation (lint_no_openvr_in_core)

**Files modified:** `cmake/lint_no_openvr_in_core.cmake`
**Commit:** 87c3ac6 (combined with WR-01 / IN-01)
**Applied fix:** Replaced the `foreach(_ext ${_extensions})` wrapper +
inner `file(GLOB_RECURSE _files "${_root}/${_ext}")` with a single
`file(GLOB_RECURSE _files <all nine patterns>)` per root. One directory
walk per root instead of nine. The `_files_scanned` counter is now
incremented exactly once per file (the structural double-count risk the
reviewer called out is gone, even though it was theoretical at HEAD).

### WR-03: extension-loop GLOB consolidation (lint_no_driver_macro)

**Files modified:** `cmake/lint_no_driver_macro.cmake`
**Commit:** 94367ca
**Applied fix:** Mirrored the WR-03 consolidation from
`lint_no_openvr_in_core.cmake` (separate commit so the two scripts retain
independent history but stay structurally aligned). Verified by
`ctest -R lint_no_driver_macro` (clean, 0 violations across the four
shared-lib roots).

### IN-01: `vr::` substring lint may false-positive on identifiers ending in `vr`

**Files modified:** `cmake/lint_no_openvr_in_core.cmake`
**Commit:** 87c3ac6 (combined with WR-01 / WR-03)
**Applied fix:** Replaced the bare `vr::` substring with two alternating
patterns: `[^a-zA-Z0-9_]vr::` (non-identifier-char-before) and `^vr::`
(start-of-content). CMake regex does not support `\b`, so the explicit
char-class form is the portable equivalent. Verified clean against the
existing shared layer.

### IN-03: document `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>` co-location invariant

**Files modified:** `src/steamvr/CMakeLists.txt`
**Commit:** 5dd10e0
**Applied fix:** Added a multi-line comment block above the `add_custom_command(TARGET
micmap_steamvr POST_BUILD …)` explaining the multi-config vs single-config
behavior of `$<CONFIG>` and warning future contributors not to override
`RUNTIME_OUTPUT_DIRECTORY` on the three executable targets (`micmap`,
`mic_test`, `hmd_button_test`) in a way that bypasses the per-config
slot — doing so would land the OpenVR DLL in a directory the exes do not
read from, producing `STATUS_DLL_INIT_FAILED` at launch. Documentational
fix only; no behavior change.

### IN-05: document `MICMAP_BUILD_DRIVER` default-ON contract for SC1

**Files modified:** `CMakeLists.txt`
**Commit:** 8d81e1c
**Applied fix:** Added a five-line comment block above
`option(MICMAP_BUILD_DRIVER "Build OpenVR driver" ON)` linking the
default-ON choice to the SC1 acceptance criterion ("mic_test.exe must
build with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR absent"). Future
contributors flipping the default to OFF will see the comment and
coordinate the CI matrix change.

## Skipped Issues

### IN-02: Both lint scripts duplicate ~30 lines verbatim

**File:** `cmake/lint_no_openvr_in_core.cmake` and `cmake/lint_no_driver_macro.cmake`
**Reason:** Reviewer explicitly tagged this finding as
"Defer until a third lint is needed (rule of three). Optional." Hoisting
a `lint_grep_shared.cmake` helper for two callsites is premature
abstraction — the two scripts diverge in regex content and message
strings, so the helper would need to take both as parameters and the
shrinkage (~30 lines → ~6 lines per call) is not worth the indirection
cost while only two clients exist. Re-evaluate when (if) a third lint
script lands.
**Original issue:** The scaffold (SRC_ROOTS validation, extension list,
per-file READ + scan loop, violations aggregation, success message) is
byte-identical between the two scripts. Any fix to WR-01 / WR-03 / IN-01
must land in both files; drift is the obvious risk. The WR-03 + IN-01
follow-through on lint_no_driver_macro.cmake (commit 94367ca) demonstrates
the duplication-cost the reviewer flagged — but the cost is low enough
to absorb today.

### IN-04: `apps/micmap/CMakeLists.txt` retains direct `micmap::bindings` link (verification only)

**File:** `apps/micmap/CMakeLists.txt:46-52`
**Reason:** Reviewer concluded "**Fix:** None. Verification only." after
confirming via inspection that `micmap_steamvr` does NOT link
`micmap_bindings`, so the direct link in `apps/micmap` is required (not
redundant). The Phase 5 D-09 comment block in the apps file already
documents the rationale. No code change applies.
**Original issue:** Verified that `micmap` links `micmap::bindings` directly
because `micmap_steamvr` does not pull it transitively. The link is
necessary, the comment is correct, and no edit is warranted.

---

_Fixed: 2026-05-02_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

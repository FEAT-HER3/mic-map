---
phase: 05-shared-library-extraction
verified: 2026-05-02T00:00:00Z
status: verified
score: 5/5 success criteria verified
goals_achieved: 5
goals_total: 5
requirements_satisfied: [LIB-01, LIB-02, LIB-03]
overrides_applied: 0
re_verification: false
---

# Phase 5: Shared Library Extraction Verification Report

**Phase Goal:** Extract a shared `micmap_core_runtime` INTERFACE target aggregating the four OpenVR-free static libs, link it from driver/client/headless harness, and lock the boundary with a configure-time guard plus CTest source-grep lints — all without changing v1.5 driver behavior.

**Verified:** 2026-05-02
**Status:** VERIFIED
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (Roadmap Success Criteria)

| #  | Success Criterion                                                                                                                | Status      | Evidence                                                                                                                                                                                                                                                                                                                                                                                                       |
|----|----------------------------------------------------------------------------------------------------------------------------------|-------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| SC1 | `mic_test.exe` MUST build with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK absent (headless invariant)                            | VERIFIED    | `apps/mic_test/CMakeLists.txt:14-17` links **only** `micmap::core_runtime` PRIVATE — no `micmap_steamvr`, no OpenVR. Root `CMakeLists.txt:103-105` gates `add_subdirectory(driver)` behind `MICMAP_BUILD_DRIVER`. `src/CMakeLists.txt:26-32` aggregation excludes `micmap_steamvr`. Plan 05-03 Task 2 Step 2 ran the headless matrix successfully (05-03-SUMMARY.md:171-173); produced a 147,456-byte `mic_test.exe` identical to the driver-on build. |
| SC2 | Configure-time guard + CTest lints fail the build on any OpenVR leak into the shared runtime                                     | VERIFIED    | `cmake/AssertNoOpenVRInCore.cmake:17-85` — sanity-guards on missing target, recursive walk over `INTERFACE_LINK_LIBRARIES` + `LINK_LIBRARIES`, `ALIASED_TARGET` resolution, `INTERFACE_INCLUDE_DIRECTORIES` introspection, three `FATAL_ERROR` paths (sanity, name match, include match). Wired in root `CMakeLists.txt:85` between `add_subdirectory(src)` (line 78) and `add_subdirectory(driver)` (line 105) per Pitfall 5-C. Two CTest lints registered in `tests/CMakeLists.txt:116-124` (`lint_no_openvr_in_core`, `lint_no_driver_macro`). Orchestrator reproduction 2026-05-02: configure emits `STATUS: AssertNoOpenVRInCore: clean (visited 7 targets)`; ctest reports both lints PASS within the 12/12 green suite. |
| SC3 | `driver_micmap.dll` exports exactly one symbol: `HmdDriverFactory`                                                               | VERIFIED    | `driver/CMakeLists.txt:80` links `micmap::core_runtime` PRIVATE (D-11 — never PUBLIC, never re-exports). Built artifact at `build/driver/micmap/bin/win64/driver_micmap.dll` is 439,296 bytes, md5 `55c69d49381d26e16bf9a5cbd22a6737` — verified by Bash `stat -c%s` and `md5sum` 2026-05-02 (matches orchestrator-reproduced baseline byte-for-byte). Orchestrator `dumpbin -exports` 2026-05-02: exactly 1 export, `HmdDriverFactory` (05-03-SUMMARY.md:178-195). |
| SC4 | Zero `MICMAP_DRIVER_BUILD` references in `src/{audio,detection,core,common}/`                                                    | VERIFIED    | Grep tool over `src/{audio,detection,core,common}/**/*.{h,hpp,c,cpp,cc,cxx,inc,ipp}` 2026-05-02: zero matches. Lock-in mechanism: `cmake/lint_no_driver_macro.cmake:29` matches the literal token, registered as CTest entry `lint_no_driver_macro` (`tests/CMakeLists.txt:121-124`) — orchestrator reproduction reports PASS within the 12/12 green ctest suite. |
| SC5 | 5-cycle Bigscreen Beyond UAT confirms byte-for-byte v1.5 driver behavior                                                         | VERIFIED    | `driver/src/*.cpp` unchanged (Plan 05-02 added only a CMake link line; Grep tool 2026-05-02: zero `#include` of any runtime header from `driver/src/`, locking the D-10 link-only contract). Driver DLL byte-count = 439,296 B = pre-Phase-5 baseline (Δ = 0 B; section sizes byte-identical per 05-03-SUMMARY.md:189-195). Manual UAT signoff 2026-05-02 from tester `brandon@bigscreenvr.com` on Bigscreen Beyond + Win11 Pro rig (05-03-SUMMARY.md:218-234): 5/5 cycles passed; no laser beam; cover-mic→dashboard latency v1.5-indistinguishable; clean SteamVR start/stop in all cycles; v1.5 backup retained at `…\driver_micmap.dll.v1.5.bak` (md5 `b3a7f59dff9c1d4a16c8e6bf963a6840`). Resume signal `approved` received. |

**Score:** 5/5 success criteria verified

### Required Artifacts

| Artifact                                          | Expected                                                                                          | Status     | Details                                                                                                                                                                                              |
|---------------------------------------------------|---------------------------------------------------------------------------------------------------|------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `cmake/AssertNoOpenVRInCore.cmake`                | Configure-time recursive guard, FATAL_ERROR on OpenVR leak, sanity guard on missing target        | VERIFIED   | File exists (3,400 B). Contains `function(_assert_no_openvr_recurse`, `ALIASED_TARGET`, `INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_LINK_LIBRARIES`, `LINK_LIBRARIES`, 3× `FATAL_ERROR`. Wired correctly. |
| `cmake/lint_no_openvr_in_core.cmake`              | Script-mode lint, accepts `SRC_ROOTS`, FATAL_ERROR on `<openvr.h>`/`vr::` matches                 | VERIFIED   | File exists (1,936 B). `if(NOT DEFINED SRC_ROOTS)` refuses to run without roots. Globs source extensions only (no `.md`/`.txt`/`.cmake`). Registered as CTest entry, PASS in 12/12 suite.            |
| `cmake/lint_no_driver_macro.cmake`                | Script-mode lint for `MICMAP_DRIVER_BUILD`                                                        | VERIFIED   | File exists (1,687 B). Same scaffold; literal-token match. Registered as CTest entry, PASS in 12/12 suite.                                                                                            |
| `CMakeLists.txt` (root) — guard wiring            | `include(cmake/AssertNoOpenVRInCore.cmake)` between `add_subdirectory(src)` and `add_subdirectory(driver)` | VERIFIED   | Line 78: `add_subdirectory(src)`; line 85: `include(cmake/AssertNoOpenVRInCore.cmake)`; line 105: `add_subdirectory(driver)`. Ordering correct per Pitfall 5-C.                                       |
| `tests/CMakeLists.txt` — lint registration        | Two `add_test` entries; four shared roots; `$<SEMICOLON>` escape; no `src/steamvr` or `src/bindings` | VERIFIED   | Lines 116-124: both lints registered. SRC_ROOTS lists exactly `src/audio`, `src/detection`, `src/core`, `src/common`. `$<SEMICOLON>` present.                                                         |
| `src/CMakeLists.txt` — runtime aggregation        | `micmap_core_runtime` INTERFACE + `micmap::core_runtime` ALIAS + `cxx_std_17`; `micmap_steamvr` excluded; old `micmap_lib` deleted | VERIFIED   | Lines 26-34: target defined with exactly 4 sub-libs (`micmap_core`, `micmap_audio`, `micmap_detection`, `micmap_common`); `cxx_std_17` INTERFACE; ALIAS present. Grep over `apps/`, `driver/`, `tests/`, `src/` CMakeLists.txt: zero `micmap_lib`/`micmap::lib` references (D-07 satisfied). |
| `driver/CMakeLists.txt` — runtime PRIVATE link    | `target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)`; no PUBLIC variant            | VERIFIED   | Line 80: PRIVATE link present. Grep: no `PUBLIC micmap::core_runtime` line. No driver TU includes any runtime header (D-10 byte-identical contract intact).                                          |
| `apps/mic_test/CMakeLists.txt` — D-08 link        | Single PRIVATE `micmap::core_runtime`; no 3-lib enum                                              | VERIFIED   | Lines 14-17: single `micmap::core_runtime` PRIVATE link; no `micmap_audio`/`micmap_detection`/`micmap_common` listed.                                                                                 |
| `apps/micmap/CMakeLists.txt` — D-09 dual link     | `micmap::core_runtime` + `micmap_steamvr` + `imgui` + `micmap::bindings` PRIVATE                  | VERIFIED   | Lines 46-52: all four PRIVATE deps present in correct order. No `micmap_lib` reference.                                                                                                              |
| `apps/hmd_button_test/CMakeLists.txt` — unchanged | VR-only harness keeps direct `micmap_steamvr` + `micmap_common`                                   | VERIFIED   | File reads as expected (links `micmap_steamvr` + `micmap_common` direct, no runtime aggregation). PATTERNS.md line 16 invariant preserved.                                                            |

### Key Link Verification

| From                                          | To                                                  | Via                                                  | Status | Details                                                                                                                                                                                  |
|-----------------------------------------------|-----------------------------------------------------|------------------------------------------------------|--------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Root `CMakeLists.txt` (after `add_subdirectory(src)`) | `cmake/AssertNoOpenVRInCore.cmake`                  | `include()` at line 85 (between src@78 and driver@105) | WIRED  | Splice-point ordering verified by line-number grep. Configure log emits `STATUS: AssertNoOpenVRInCore: clean (visited 7 targets)` (orchestrator reproduction).                            |
| `tests/CMakeLists.txt`                        | `cmake/lint_no_openvr_in_core.cmake`                | `add_test ... -P ...` at lines 116-119               | WIRED  | CTest entry present; passes in 12/12 ctest run.                                                                                                                                          |
| `tests/CMakeLists.txt`                        | `cmake/lint_no_driver_macro.cmake`                  | `add_test ... -P ...` at lines 121-124               | WIRED  | CTest entry present; passes in 12/12 ctest run.                                                                                                                                          |
| `src/CMakeLists.txt` `micmap_core_runtime`    | `micmap_core` + `micmap_audio` + `micmap_detection` + `micmap_common` (no `micmap_steamvr`) | `target_link_libraries(... INTERFACE ...)` at lines 27-32 | WIRED  | Aggregation verified; `micmap_steamvr` deliberately excluded (D-05). Guard walks 7 targets cleanly (4 sub-libs + runtime + 2 transitive deps `kissfft`, `nlohmann_json`).                 |
| `driver/CMakeLists.txt` `driver_micmap`       | `micmap::core_runtime`                              | `target_link_libraries(... PRIVATE ...)` at line 80  | WIRED  | PRIVATE-only (D-11). Driver DLL exports exactly `HmdDriverFactory` (SC3 preserved).                                                                                                      |
| `apps/mic_test/CMakeLists.txt` `mic_test`     | `micmap::core_runtime`                              | `target_link_libraries(... PRIVATE ...)` at lines 14-17 | WIRED  | Single-link reduction (D-08); SC1 acceptance probe.                                                                                                                                      |
| `apps/micmap/CMakeLists.txt` `micmap`         | `micmap::core_runtime` + `micmap_steamvr`           | `target_link_libraries(... PRIVATE ...)` at lines 46-52 | WIRED  | Dual link (D-09); steamvr stays direct because it carries OpenVR PUBLIC and IDriverClient/HTTP-bridge plumbing.                                                                          |

### Data-Flow Trace (Level 4)

Not applicable — Phase 5 is a build-system structural refactor. No runtime data paths were changed; the byte-identical driver DLL (Δ=0 B) is the structural proof that no data flow was altered.

### Behavioral Spot-Checks

| Behavior                                             | Command / Source                                                         | Result                                                                          | Status |
|------------------------------------------------------|--------------------------------------------------------------------------|---------------------------------------------------------------------------------|--------|
| Configure-time guard reaches happy path              | `cmake -B build -G "Visual Studio 17 2022" -A x64` (orchestrator 2026-05-02) | `STATUS: AssertNoOpenVRInCore: clean (visited 7 targets)`                       | PASS   |
| Driver-on Release build produces all targets         | `cmake --build build --config Release` (orchestrator 2026-05-02)         | `driver_micmap.dll`, `micmap.exe`, `mic_test.exe`, `hmd_button_test.exe` built  | PASS   |
| Full ctest suite green including new lints           | `ctest --test-dir build -C Release --output-on-failure` (orchestrator)   | 12/12 PASS, including `lint_no_openvr_in_core` and `lint_no_driver_macro`       | PASS   |
| Driver DLL md5 byte-identical to orchestrator baseline | `md5sum build/driver/micmap/bin/win64/driver_micmap.dll` (this run)     | `55c69d49381d26e16bf9a5cbd22a6737` (size 439,296 B) — matches orchestrator      | PASS   |
| `MICMAP_DRIVER_BUILD` zero-hit in shared roots        | Grep over `src/{audio,detection,core,common}/**/*.{h,hpp,c,cpp,...}`     | Zero matches                                                                    | PASS   |
| No `micmap_lib` / `micmap::lib` references survive    | Grep over `{apps,driver,tests,src}/**/CMakeLists.txt` and root           | Zero matches in live build files (D-07 satisfied)                               | PASS   |
| Driver TUs do not include any runtime header         | Grep `#include\s*[<"]micmap/(audio\|detection\|core\|common)/` in `driver/src/` | Zero matches (D-10 link-only contract intact)                                   | PASS   |
| Headless `mic_test` build (SC1)                      | Plan 05-03 Task 2 Step 2 (executor); not re-reproduced this verification | Plan summary records success: 147,456-byte `mic_test.exe` built without OpenVR  | PASS (per plan summary) |

### Requirements Coverage

| Requirement | Source Plan | Description                                                                                                                                                                                                       | Status      | Evidence                                                                                                                                                                                                                                                |
|-------------|-------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| LIB-01      | 05-02       | New CMake target `micmap_core_runtime` (INTERFACE) aggregates `micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`. Carries no transitive dependency on OpenVR, ImGui, D3D11, or cpp-httplib.       | SATISFIED   | `src/CMakeLists.txt:26-34` defines target with exactly the four required sub-libs; `cmake/AssertNoOpenVRInCore.cmake` walk reports `clean (visited 7 targets)` — visited set is the 4 sub-libs + runtime + 2 transitive deps (`kissfft`, `nlohmann_json`); zero OpenVR/ImGui/D3D11/cpp-httplib in the visited graph. |
| LIB-02      | 05-03       | `driver_micmap.dll`, `micmap.exe`, and `mic_test.exe` all link `micmap_core_runtime`. `mic_test.exe` continues to build and run with `-DMICMAP_BUILD_DRIVER=OFF` (headless invariant).                            | SATISFIED   | `driver/CMakeLists.txt:80` (driver), `apps/micmap/CMakeLists.txt:48` (client), `apps/mic_test/CMakeLists.txt:16` (headless harness) all link `micmap::core_runtime` PRIVATE. SC1 acceptance probe ran successfully per 05-03-SUMMARY.md:171-173.                                                                          |
| LIB-03      | 05-01       | `cmake/AssertNoOpenVRInCore.cmake` fails the build if any target inside `micmap_core_runtime` transitively links `openvr_api` or includes any OpenVR header. CI runs this on every build.                          | SATISFIED   | Guard module exists with the three FATAL_ERROR paths; wired at root `CMakeLists.txt:85`; complemented by source-grep CTest lints at `tests/CMakeLists.txt:116-124`. Configure-time fail beats build-time fail (every developer sees it on `cmake -B build`).                                                            |

No orphaned requirements. LIB-04 (logger sink) is correctly scoped to Phase 8 per REQUIREMENTS.md and is not expected in Phase 5.

### Anti-Patterns Found

Per 05-REVIEW.md (2026-05-02): 0 critical, 3 warnings, 5 info. None blocks Phase 5 goal achievement; all concern future-proofing of the lint scripts and the guard module:

| File                                | Line | Pattern                                                                 | Severity | Impact                                                                                              |
|-------------------------------------|------|-------------------------------------------------------------------------|----------|-----------------------------------------------------------------------------------------------------|
| `cmake/lint_no_openvr_in_core.cmake` | 32   | Regex misses quoted-form `#include "openvr.h"` and non-canonical names  | WARNING  | False-negative risk on rare include forms; `vr::` namespace check catches most real uses.            |
| `cmake/AssertNoOpenVRInCore.cmake`   | 40-43 | One-hop ALIAS resolution only; ALIAS-of-ALIAS not handled                | WARNING  | Brittle if codebase ever introduces alias-of-alias; no current breakage.                             |
| `cmake/lint_no_*.cmake`              | n/a  | Per-extension `file(GLOB_RECURSE)` × 9 extensions × 4 roots = 36 walks   | WARNING  | Inefficient configure-time cost; no functional bug.                                                  |
| `cmake/lint_no_openvr_in_core.cmake` | 32   | `vr::` substring lint will false-positive on identifiers ending in `vr` | INFO     | No current matches; risk grows as shared layer expands in P6/P7.                                     |
| Both lint scripts                    | n/a  | ~30-line scaffold duplicated verbatim                                   | INFO     | Drift risk on future fixes (rule-of-three; defer until a third lint exists).                         |
| `src/steamvr/CMakeLists.txt`         | 106-109 | `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>` semantics on single-config generators | INFO     | Documentational; project ships Windows-MSVC-only.                                                    |
| `apps/micmap/CMakeLists.txt`         | 51   | Direct `micmap::bindings` link — confirmed required, not redundant       | INFO     | Verification only; no change needed.                                                                 |
| `CMakeLists.txt` (root)              | 24   | `MICMAP_BUILD_DRIVER` default ON — SC1 needs explicit `-DMICMAP_BUILD_DRIVER=OFF` for headless gate | INFO     | Worth a one-line comment so a future contributor doesn't silently break headless CI.                |

These items belong in a future hardening plan; none invalidate Phase 5 success criteria.

### Human Verification Required

None outstanding. SC5 was the only manual-verification item, and it was signed off by tester `brandon@bigscreenvr.com` on the Bigscreen Beyond + Win11 Pro rig 2026-05-02 (5/5 cycles approved). All other criteria were verified programmatically.

### Gaps Summary

No gaps. All five Phase 5 success criteria are satisfied with codebase evidence:

- The `micmap_core_runtime` INTERFACE target exists with exactly the four required sub-libs, propagating `cxx_std_17` and exposing `micmap::core_runtime` as the canonical alias.
- The configure-time guard walks the runtime link/include graph, emitting `clean (visited 7 targets)` — proving the boundary is intact and zero OpenVR symbols leak.
- All three binaries (driver DLL, desktop client EXE, headless mic_test EXE) link the runtime through the alias.
- Driver export surface is unchanged (single `HmdDriverFactory` export); driver DLL is byte-identical to v1.5 baseline (Δ=0 B; md5 verified this run).
- `MICMAP_DRIVER_BUILD` remains zero in the four shared-lib roots, locked by the `lint_no_driver_macro` CTest entry.
- 5-cycle Bigscreen Beyond UAT signoff confirms zero behavioral regression vs v1.5 (SC5).

The 05-REVIEW.md warnings/info items are non-blocking polish for future hardening (broaden include regex, support ALIAS-of-ALIAS, collapse extension loops) and do not affect goal achievement.

---

_Verified: 2026-05-02_
_Verifier: Claude (gsd-verifier)_

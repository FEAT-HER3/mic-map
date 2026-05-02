# Phase 5: Shared Library Extraction - Pattern Map

**Mapped:** 2026-05-01
**Files analyzed:** 7 (1 new, 6 modified)
**Analogs found:** 7 / 7

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `cmake/AssertNoOpenVRInCore.cmake` (NEW) | cmake-module / configure-time guard | introspection (read-only walk over target graph) | `cmake/FindOpenVR.cmake` (CMake module shape) + Pattern 3 in RESEARCH.md (walk algorithm — no in-tree precedent) | partial (module-shape only; algorithm is novel) |
| `src/CMakeLists.txt` (MOD) | cmake build file / aggregator | INTERFACE re-export | itself — current `micmap_lib` INTERFACE block at lines 11-21 | exact (same shape, narrower link list) |
| `apps/mic_test/CMakeLists.txt` (MOD) | cmake build file / consumer relink | PRIVATE link | `apps/hmd_button_test/CMakeLists.txt` (sibling test-app, same WIN32 + comctl32 idiom) | exact |
| `apps/micmap/CMakeLists.txt` (MOD) | cmake build file / consumer relink | PRIVATE link | itself lines 39-44 (current `micmap_lib` consumer) | exact |
| `driver/CMakeLists.txt` (MOD) | cmake build file / consumer relink | PRIVATE link | itself line 69 (existing `micmap::bindings` PRIVATE link — Phase 4 D-10 lift) | exact |
| `apps/hmd_button_test/CMakeLists.txt` (NO-OP — verify only) | cmake build file | n/a | itself — explicitly **not** modified by P5 (still links `micmap_steamvr` direct) | n/a |
| `CMakeLists.txt` (root, MOD) | cmake build file / orchestrator | `include()` + `add_subdirectory` ordering | itself lines 76-102 (existing `add_subdirectory(src)` → conditional `add_subdirectory(driver)` ordering) | exact |

**Note on hmd_button_test:** RESEARCH.md §"Architecture Patterns" line 163 and §Sources line 639 confirm `apps/hmd_button_test/CMakeLists.txt:8-12` is **deliberately untouched** — it is the VR-only harness and continues to link `micmap_steamvr` directly. Listed in the table for completeness, but no edit needed.

---

## Pattern Assignments

### `cmake/AssertNoOpenVRInCore.cmake` (NEW — cmake-module / configure-time guard)

**Analog (module shape only):** `cmake/FindOpenVR.cmake` lines 1-74

**Module-file conventions to mirror** (header comment block, `include(FindPackageHandleStandardArgs)`-style top-of-file include, `mark_as_advanced` tail):

```cmake
# FindOpenVR.cmake
# Find the OpenVR SDK
#
# This module defines:
#   OpenVR_FOUND - True if OpenVR was found
#   OpenVR_INCLUDE_DIRS - Include directories for OpenVR
#   OpenVR_LIBRARIES - Libraries to link against
#   OpenVR::openvr_api - Imported target for OpenVR

include(FindPackageHandleStandardArgs)
```
(`cmake/FindOpenVR.cmake:1-10`)

**Algorithm (novel — no in-tree precedent):** Use the reference skeleton from `05-RESEARCH.md` Pattern 3 / Code Examples §Example 1. The skeleton is normative on what the helper MUST do (walk both `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES`, resolve `ALIASED_TARGET`, check propagated `INTERFACE_INCLUDE_DIRECTORIES`, FATAL_ERROR on hit, idempotent visited-set). CONTEXT D-01 / D-04 lock the contract; helper-macro layout is Claude's discretion (CONTEXT line 51).

**Imported-target sanity guard to add (per RESEARCH.md Pitfall 5-C):**
```cmake
if(NOT TARGET micmap_core_runtime)
    message(FATAL_ERROR
        "AssertNoOpenVRInCore: micmap_core_runtime is not defined yet — "
        "include() this module AFTER add_subdirectory(src).")
endif()
```

**Existing CMake idiom for property-based imported-target inspection** (proves the API works in this codebase):
```cmake
# cmake/FindOpenVR.cmake:65-71
if(NOT TARGET OpenVR::openvr_api)
    add_library(OpenVR::openvr_api UNKNOWN IMPORTED)
    set_target_properties(OpenVR::openvr_api PROPERTIES
        IMPORTED_LOCATION "${OpenVR_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OpenVR_INCLUDE_DIR}"
    )
endif()
```
(Demonstrates `INTERFACE_INCLUDE_DIRECTORIES` is the property the guard must read on every visited node — D-01.)

**Existing in-tree precedent for `IMPORTED_LOCATION` reads + `get_target_property`** (the exact CMake API the guard relies on):
```cmake
# CMakeLists.txt:178-181 (root)
if(WIN32 AND TARGET OpenVR::openvr_api)
    get_target_property(_OPENVR_LOC OpenVR::openvr_api IMPORTED_LOCATION)
    if(_OPENVR_LOC)
        get_filename_component(_OPENVR_DIR "${_OPENVR_LOC}" DIRECTORY)
```
And `src/steamvr/CMakeLists.txt:70-73` follows the identical pattern. Both confirm `get_target_property` is the established introspection idiom in this tree.

---

### `src/CMakeLists.txt` (MOD — replace `micmap_lib` with `micmap_core_runtime`)

**Analog:** itself, lines 11-21 (current `micmap_lib`).

**Current shape to replace** (`src/CMakeLists.txt:11-21`):
```cmake
# Main library combining all modules
add_library(micmap_lib INTERFACE)
target_link_libraries(micmap_lib INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_steamvr
    micmap_common
)

add_library(micmap::lib ALIAS micmap_lib)
```

**Sub-dir ordering already in place** (`src/CMakeLists.txt:1-9`) — no change needed:
```cmake
# src/CMakeLists.txt
# MicMap source libraries

add_subdirectory(common)
add_subdirectory(audio)
add_subdirectory(bindings)
add_subdirectory(detection)
add_subdirectory(steamvr)
add_subdirectory(core)
```

**Target replacement (per CONTEXT D-05/D-06/D-07 + RESEARCH.md Code Example 2):**
- Drop `micmap_steamvr` from the link list (D-05; `src/steamvr/CMakeLists.txt:50` proves it links `OpenVR::openvr_api` PUBLIC, which would propagate OpenVR through any aggregator).
- Rename `micmap_lib` → `micmap_core_runtime` (D-05).
- Add `micmap::core_runtime` ALIAS (D-06).
- **Add `target_compile_features(micmap_core_runtime INTERFACE cxx_std_17)`** — the existing `micmap_lib` block omits this; the new target carries it explicitly (RESEARCH.md Standard Stack table; mirrors `micmap_bindings:36`'s `PUBLIC cxx_std_17`).
- Delete `micmap_lib` and `micmap::lib` entirely (D-07).

**Alias-pattern source of truth** (every existing sub-lib follows it — pick any; using `micmap_bindings`):
```cmake
# src/bindings/CMakeLists.txt:38-40
# Alias target for consistent namespace-style consumption from downstream
# targets: target_link_libraries(... PRIVATE micmap::bindings)
add_library(micmap::bindings ALIAS micmap_bindings)
```

Five-of-five sub-libs follow the `add_library(micmap::<short> ALIAS micmap_<short>)` shape — `micmap::common` (`src/common/CMakeLists.txt:18`), `micmap::audio` (`src/audio/CMakeLists.txt:41`), `micmap::detection` (`src/detection/CMakeLists.txt:26`), `micmap::core` (`src/core/CMakeLists.txt:30`), `micmap::steamvr` (`src/steamvr/CMakeLists.txt:67`). The new alias `micmap::core_runtime` keeps the convention.

---

### `apps/mic_test/CMakeLists.txt` (MOD — D-08, SC1 acceptance probe)

**Analog:** itself, lines 1-25 (Pattern 2 in RESEARCH.md).

**Current shape to modify** (`apps/mic_test/CMakeLists.txt:1-25`):
```cmake
# apps/mic_test/CMakeLists.txt
# MicMap Audio Test Application - Win32 GUI

add_executable(mic_test WIN32
    main.cpp
)

target_link_libraries(mic_test
    PRIVATE
        micmap_audio
        micmap_detection
        micmap_common
)

target_compile_features(mic_test PRIVATE cxx_std_17)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(mic_test PRIVATE comctl32 comdlg32)
endif()

# Set output directory
set_target_properties(mic_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

**Pattern delta (D-08):** Replace the 3-lib enum (`micmap_audio` / `micmap_detection` / `micmap_common`) with single `micmap::core_runtime`. Keep WIN32 `comctl32 comdlg32` block exactly as-is. No other lines change.

**Sibling consumer-relink shape** (showing the same WIN32 block style we keep): `apps/hmd_button_test/CMakeLists.txt:1-24` — note this file is **not** modified.

---

### `apps/micmap/CMakeLists.txt` (MOD — D-09)

**Analog:** itself, lines 39-44 (current `micmap_lib` consumer).

**Current link block** (`apps/micmap/CMakeLists.txt:39-44`):
```cmake
target_link_libraries(micmap
    PRIVATE
        micmap_lib
        imgui
        micmap::bindings
)
```

**Pattern delta (D-09):** Replace `micmap_lib` with **two** targets — `micmap::core_runtime` (the new aggregate) **and** `micmap_steamvr` (kept as a direct link because `apps/micmap/main.cpp` still uses `IDriverClient` / HTTP-bridge plumbing that lives in the steamvr lib). Keep `imgui` and `micmap::bindings` lines untouched.

**Pattern from RESEARCH.md Code Example §Pattern 2:**
```cmake
# AFTER:   PRIVATE micmap::core_runtime micmap_steamvr imgui micmap::bindings
#                                       └── direct link (D-09)
```

All other lines in `apps/micmap/CMakeLists.txt` are unrelated (vrmanifest configure_file, POST_BUILD copies, install rules) and stay verbatim.

**Verification recipe (RESEARCH.md A3, line 605):**
```bash
grep -rn 'micmap_lib\|micmap::lib' apps/ driver/ tests/ src/
# Expect: only the now-deleted lines in src/CMakeLists.txt and the
# now-replaced line in apps/micmap/CMakeLists.txt show up in the diff;
# zero hits remain after the change-set is applied.
```

---

### `driver/CMakeLists.txt` (MOD — D-10/D-11, append PRIVATE link only)

**Analog:** itself, line 69 (the existing Phase 4 D-10 `micmap::bindings` PRIVATE link).

**Exact analog line (the pattern to mirror):**
```cmake
# driver/CMakeLists.txt:65-69
# Phase 4 D-10: bindings_patcher was lifted to src/bindings/ so both
# driver_micmap.dll and micmap.exe can share one source of truth. The driver
# now links the shared lib + wraps DriverLog in a LogSink adapter (see
# device_provider.cpp).
target_link_libraries(driver_micmap PRIVATE micmap::bindings)
```

**Pattern delta (D-10/D-11):** Append a sibling line, same shape, `PRIVATE` linkage, alias form:
```cmake
target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)
```

with a Phase 5 comment explaining (a) link-only — no driver TU may `#include` runtime headers in P5 (D-10), (b) `PRIVATE` is mandatory — `PUBLIC` would re-export shared-lib symbols across the DLL boundary (D-11 / Pitfall 6).

**Anti-pattern reminder (RESEARCH.md line 343):** `target_link_libraries(driver_micmap PUBLIC micmap::core_runtime)` is forbidden — it re-exports symbols and breaks SC3 dumpbin (only `HmdDriverFactory` may be exported). The existing `driver/src/driver_main.cpp:26` is the only `__declspec(dllexport)` in the tree; keep it the only one (D-13).

**No other changes in `driver/CMakeLists.txt`** — the OpenVR / httplib / nlohmann_json blocks (lines 36-63), driver compile defs, output directories, and POST_BUILD resource copies all stay verbatim. SC5 demands byte-identical driver behavior; only the link list grows by one line.

---

### `CMakeLists.txt` (root, MOD — D-04 ordering)

**Analog:** itself, lines 76-102 (existing subdirectory + driver-gating shape).

**Current ordering to splice into** (`CMakeLists.txt:76-102`):
```cmake
# Add subdirectories
add_subdirectory(external)
add_subdirectory(src)

if(MICMAP_BUILD_TEST_APPS)
    add_subdirectory(apps)
endif()

# Phase 03 ad-hoc tools (register_manifest). Throwaway — delete after Phase 03 exit.
# Guarded on OpenVR availability (Phase 03 A2 test requires live SteamVR API).
if(WIN32 AND OpenVR_FOUND)
    add_subdirectory(tools)
endif()

if(MICMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build the OpenVR driver (requires OpenVR SDK)
if(MICMAP_BUILD_DRIVER)
    if(OpenVR_FOUND)
        add_subdirectory(driver)
    else()
        message(STATUS "Skipping driver build - OpenVR SDK not found")
    endif()
endif()
```

**Pattern delta (D-04 — surgical splice between line 78 and the rest):**
```cmake
add_subdirectory(src)

# Phase 5 D-04: walk the micmap_core_runtime link/include graph and FATAL_ERROR
# if any node touches OpenVR. Must run AFTER add_subdirectory(src) so all four
# sub-lib targets are defined, and BEFORE add_subdirectory(driver) so a clean
# fail message lands before the driver tries to link.
include(cmake/AssertNoOpenVRInCore.cmake)
```

**Why the splice point matters** (RESEARCH.md Pitfall 5-C, lines 378-382): the guard module reads target properties; if `add_subdirectory(src)` has not yet returned, `micmap_core_runtime` does not exist and the walk passes spuriously on an empty graph. The `if(NOT TARGET micmap_core_runtime)` sanity check listed in the new module's section above catches this regression.

**Module-path setup (already in place — no change):**
```cmake
# CMakeLists.txt:32
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
```
Means `include(cmake/AssertNoOpenVRInCore.cmake)` works directly with the relative path; no `find_file` or absolute path needed.

**CTest hook for the source-grep lint (Pattern 4 — Claude's discretion per CONTEXT line 53):**
The existing `tests/CMakeLists.txt:29` shape is the in-tree analog for `add_test`:
```cmake
# tests/CMakeLists.txt:27-29
# Placeholder test that always passes
add_executable(test_placeholder test_placeholder.cpp)
add_test(NAME test_placeholder COMMAND test_placeholder)
```
And the script-driven CTest form referenced in RESEARCH.md Pattern 4 is the recommended shape for the lint:
```cmake
# (new) cmake/lint_no_openvr_in_core.cmake — script-mode invocation
add_test(NAME lint_no_openvr_in_core
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)
```
RESEARCH.md Open Question 1 (lines 611-614) recommends the CTest form because (a) CTest is already wired (`tests/CMakeLists.txt:29` proof), (b) `cmake --build && ctest` runs both the configure-time guard and the lint without external CI tooling, (c) co-located in `cmake/`. **Planner discretion call.**

---

## Shared Patterns

### Alias-style namespace consumption

**Source:** every sub-lib in `src/{audio,common,core,detection,steamvr,bindings}/CMakeLists.txt`.
**Apply to:** the new `micmap_core_runtime` target.
**Concrete excerpts:**
- `src/bindings/CMakeLists.txt:40` — `add_library(micmap::bindings ALIAS micmap_bindings)`
- `src/audio/CMakeLists.txt:41` — `add_library(micmap::audio ALIAS micmap_audio)`
- `src/common/CMakeLists.txt:18` — `add_library(micmap::common ALIAS micmap_common)`
- `src/core/CMakeLists.txt:30` — `add_library(micmap::core ALIAS micmap_core)`
- `src/detection/CMakeLists.txt:26` — `add_library(micmap::detection ALIAS micmap_detection)`
- `src/steamvr/CMakeLists.txt:67` — `add_library(micmap::steamvr ALIAS micmap_steamvr)`

5/5 STATIC sub-libs publish a `micmap::<short>` ALIAS. The new `micmap::core_runtime` continues the convention (CONTEXT D-06).

### `cxx_std_17` PUBLIC / INTERFACE propagation

**Source:** every sub-lib uses `target_compile_features(... PUBLIC cxx_std_17)`.
**Apply to:** the new INTERFACE target with `INTERFACE` keyword (no compile units, so PUBLIC is wrong; INTERFACE is the documented form per RESEARCH.md Standard Stack).
**Concrete excerpts:**
- `src/bindings/CMakeLists.txt:36` — `target_compile_features(micmap_bindings PUBLIC cxx_std_17)`
- `src/audio/CMakeLists.txt:21` — `target_compile_features(micmap_audio PUBLIC cxx_std_17)`
- `src/common/CMakeLists.txt:15` — `target_compile_features(micmap_common PUBLIC cxx_std_17)`
- `src/core/CMakeLists.txt:22` — `target_compile_features(micmap_core PUBLIC cxx_std_17)`
- `src/detection/CMakeLists.txt:23` — `target_compile_features(micmap_detection PUBLIC cxx_std_17)`
- `src/steamvr/CMakeLists.txt:39` — `target_compile_features(micmap_steamvr PUBLIC cxx_std_17)`

Note: the existing `micmap_lib` INTERFACE block (`src/CMakeLists.txt:12-21`) **omits** `target_compile_features` — the new `micmap_core_runtime` should include it explicitly to make the C++17 contract a propagated INTERFACE requirement (RESEARCH.md Standard Stack `Idiom` table line 102).

### Public-include layout via generator expressions

**Source:** every sub-lib publishes its public headers via `$<BUILD_INTERFACE:...>`/`$<INSTALL_INTERFACE:...>`.
**Apply to:** N/A — pure transitive propagation per CONTEXT D-14. The new INTERFACE target inherits these include directories automatically; no `target_include_directories` call needed on `micmap_core_runtime`.
**Concrete excerpts (one canonical form repeated in all four sub-libs):**
```cmake
# src/audio/CMakeLists.txt:10-14
target_include_directories(micmap_audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
```
Identical at `src/common/CMakeLists.txt:9-13`, `src/core/CMakeLists.txt:9-13`, `src/detection/CMakeLists.txt:10-14`, `src/steamvr/CMakeLists.txt:18-22`, `src/bindings/CMakeLists.txt:18-24`.

### `PRIVATE` consumer linkage

**Source:** every consumer that links a `micmap_*` lib uses `PRIVATE`.
**Apply to:** all consumer relinks (mic_test, micmap, driver_micmap).
**Concrete excerpts:**
- `apps/mic_test/CMakeLists.txt:8-13` — `target_link_libraries(mic_test PRIVATE ...)`
- `apps/micmap/CMakeLists.txt:39-44` — `target_link_libraries(micmap PRIVATE micmap_lib imgui micmap::bindings)`
- `apps/hmd_button_test/CMakeLists.txt:8-12` — `target_link_libraries(hmd_button_test PRIVATE micmap_steamvr micmap_common)`
- `driver/CMakeLists.txt:69` — `target_link_libraries(driver_micmap PRIVATE micmap::bindings)`

`PUBLIC` is forbidden on the driver consumer per CONTEXT D-11 (Pitfall 6 — symbol re-export across DLL boundary).

### Conditional-OpenVR safety pattern

**Source:** the codebase uses `if(TARGET OpenVR::openvr_api)` everywhere it touches OpenVR.
**Apply to:** none directly (P5 doesn't add new OpenVR consumers), but the new guard module **must** tolerate `OpenVR::openvr_api` not existing (RESEARCH.md Pitfall 5-B) — the recursion's `if(NOT TARGET ${target})` early-return handles this.
**Concrete excerpts:**
- `src/steamvr/CMakeLists.txt:49-57` — `if(TARGET OpenVR::openvr_api) ... else() ... message(STATUS "stub implementation") endif()`
- `driver/CMakeLists.txt:36-42` — `if(TARGET OpenVR::openvr_api) ... else() FATAL_ERROR ... endif()` (driver requires it)
- `CMakeLists.txt:178` (root) — `if(WIN32 AND TARGET OpenVR::openvr_api)` (install-time DLL copy)
- `CMakeLists.txt:69-74` (root) — `find_package(OpenVR QUIET)` + `if(OpenVR_FOUND)` status print

**SC1 invariant** (RESEARCH.md line 372-374): `mic_test.exe` builds with `-DMICMAP_BUILD_DRIVER=OFF` even when `OpenVR::openvr_api` does not exist; the runtime target must never reference `OpenVR::openvr_api` directly, and the guard's missing-target early-return is what keeps configure-time clean in the headless case.

---

## No Analog Found

No file requires a "no analog" classification. The walk algorithm in `cmake/AssertNoOpenVRInCore.cmake` is novel-to-this-tree but the **module shape**, **CMake API** (`get_target_property`, `if(TARGET ...)`, `message(FATAL_ERROR)`), and **error-message style** are all established in `cmake/FindOpenVR.cmake` and the inline `find_package(OpenVR)` blocks throughout the tree, and the algorithm itself is normatively specified in RESEARCH.md Pattern 3 / Code Example §Example 1 with all required behaviors enumerated (`(a)`–`(f)` discretion-call list at RESEARCH.md line 313).

---

## Metadata

**Analog search scope:**
- `cmake/` — module-style files (FindOpenVR.cmake, FindOpenXR.cmake)
- `src/` and all sub-dirs — every sub-lib `CMakeLists.txt` for STATIC + ALIAS + cxx_std_17 + INTERFACE-aggregation patterns
- `apps/{mic_test,micmap,hmd_button_test}/` — every consumer link list
- `driver/` — driver consumer link list + Phase 4 D-10 `micmap::bindings` precedent
- `tests/` — `add_test` form for the optional CTest-driven lint
- Root `CMakeLists.txt` — subdirectory ordering, OpenVR find idiom, MICMAP_BUILD_DRIVER gating

**Files scanned:** 14 (root CMake, 6 sub-lib CMakes, 3 app CMakes, 1 driver CMake, 1 tests CMake, FindOpenVR, FindOpenXR — directory listing).

**Pattern extraction date:** 2026-05-01

**Citations to RESEARCH.md:** every file:line reference above is also cited in `05-RESEARCH.md` §Sources lines 628-647 (HIGH confidence, in-tree, verified 2026-05-01).

## PATTERN MAPPING COMPLETE

---
phase: 05-shared-library-extraction
reviewed: 2026-05-02T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - cmake/AssertNoOpenVRInCore.cmake
  - cmake/lint_no_openvr_in_core.cmake
  - cmake/lint_no_driver_macro.cmake
  - CMakeLists.txt
  - tests/CMakeLists.txt
  - src/CMakeLists.txt
  - driver/CMakeLists.txt
  - apps/mic_test/CMakeLists.txt
  - apps/micmap/CMakeLists.txt
  - src/steamvr/CMakeLists.txt
findings:
  critical: 0
  warning: 3
  info: 5
  total: 8
status: issues_found
---

# Phase 5: Code Review Report

**Reviewed:** 2026-05-02
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

Phase 5's build-system refactor is structurally sound and the headline contracts hold. The
new `micmap_core_runtime` INTERFACE target is wired correctly (only the four OpenVR-free
sub-libs, `micmap_steamvr` deliberately excluded), `AssertNoOpenVRInCore.cmake` is invoked
between `add_subdirectory(src)` and `add_subdirectory(driver)` as documented in Pitfall 5-C,
PRIVATE link from the driver and apps prevents OpenVR re-export, and the steamvr
POST_BUILD `make_directory` + explicit-filename `copy_if_different` correctly side-steps the
CMake 3.26+ destination-as-bare-directory rejection. The two source-grep CTest lints pin
the regression invariants for SC2/SC4.

The configure-time guard's recursion logic is correct for the current dependency graph
(verified against `src/{audio,detection,core,common}/CMakeLists.txt` — none of the PRIVATE
deps contain `openvr` in their target name), and the `^\$<` skip is safe because the only
genex CMake auto-emits into `INTERFACE_LINK_LIBRARIES` of a STATIC lib for PRIVATE deps is
`$<LINK_ONLY:dep>`, which is also reachable via the same target's `LINK_LIBRARIES` property
on the next walker step. So the genex skip does not create a hole in the OpenVR-leak guard.

Three Warnings flag practical regression risks (false-negative in the OpenVR header lint,
ALIAS-target property-read brittleness, and a `_files_scanned` double-counting bug that
muddies status-line output). Five Info items capture polish and forward-compat concerns.

No Critical issues. No source-file modifications proposed in this review — the build
graph is correct and headless `mic_test.exe` builds remain achievable per the SC1 path.

## Warnings

### WR-01: OpenVR-header source lint misses quoted-form includes

**File:** `cmake/lint_no_openvr_in_core.cmake:32`
**Issue:**
The regex only matches angle-bracket OpenVR includes:

```cmake
if(_content MATCHES "<openvr(_driver)?\\.h>" OR _content MATCHES "vr::")
```

A `#include "openvr.h"` (quoted form), or any non-canonical OpenVR header name like
`<openvr_api.h>` / `<openvr_capi.h>`, will not trip the lint. The OpenVR SDK ships
multiple headers; while the project conventionally uses `<openvr.h>`, the lint exists
to catch accidental drift, and a developer who fixes a "missing include" by adding
`#include "openvr_api.h"` would silently bypass SC2. The companion `vr::` namespace
check would catch most real uses of those headers, but a header that only forward-
declares OpenVR types (or references the C ABI) would not surface `vr::`.

**Fix:** Broaden both the header pattern and the include-form alternation:

```cmake
if(_content MATCHES "[<\"]openvr[a-z_]*\\.h[>\"]" OR _content MATCHES "vr::")
```

Or split into a more explicit set that mirrors the OpenVR SDK `headers/` directory
contents (`openvr.h`, `openvr_capi.h`, `openvr_driver.h`, `openvr_api.json`).

### WR-02: AssertNoOpenVRInCore reads INTERFACE_INCLUDE_DIRECTORIES on ALIAS-resolved target before checking it is non-INTERFACE

**File:** `cmake/AssertNoOpenVRInCore.cmake:54`
**Issue:**
`get_target_property(_incs ${_resolved} INTERFACE_INCLUDE_DIRECTORIES)` is called
unconditionally on every visited target, including the root `micmap_core_runtime`
INTERFACE target. INTERFACE libs CAN carry `INTERFACE_INCLUDE_DIRECTORIES` (this is
how header-only libs publish includes), so the read is legal — but on CMake versions
where some properties are restricted on ALIAS targets the guard relies on the
`ALIASED_TARGET` resolution at lines 39-43.

The current resolution only handles a one-hop alias chain. CMake permits ALIAS-of-ALIAS
in 3.18+ (an ALIAS target may itself be ALIASED). If a contributor adds
`add_library(micmap::runtime ALIAS micmap::core_runtime)` later, the inner alias would
not be resolved and a property read on a not-fully-resolved alias may FATAL_ERROR on
older CMake. This is brittle but not currently broken.

**Fix:** Iterate alias resolution until `ALIASED_TARGET` is empty:

```cmake
set(_resolved ${target})
while(TRUE)
    get_target_property(_alias ${_resolved} ALIASED_TARGET)
    if(NOT _alias)
        break()
    endif()
    set(_resolved ${_alias})
endwhile()
```

Lower priority because the codebase has no alias-of-alias today, but the guard module
advertises itself as the long-lived SC2 enforcement and will outlive several refactors.

### WR-03: `_files_scanned` counter double-counts files matched by multiple extensions

**File:** `cmake/lint_no_openvr_in_core.cmake:30` and `cmake/lint_no_driver_macro.cmake:27`
**Issue:**
The scan loop is `foreach(_root) { foreach(_ext) { foreach(_file in GLOB(_root/_ext)) ... } }`,
incrementing `_files_scanned` once per match. A file always belongs to exactly one
extension, so this is technically OK today — but `file(GLOB_RECURSE _files "${_root}/${_ext}")`
where `_ext` is `*.h` matches `foo.h` once. So actual double-counting only occurs if a
filename ends in two of the listed extensions, which is impossible. **The current code is
correct on the count axis.**

The real issue: `file(GLOB_RECURSE)` is invoked with a single-pattern argument; the
extension loop wraps GLOB calls instead of passing all patterns at once. This is
inefficient (9 directory walks per root × 4 roots = 36 walks) and the per-file
`file(READ)` reads the entire content into a CMake string, which CMake variables cap
at ~64KB-1MB depending on the build (file(READ) does not have a documented size limit
but very large generated headers could slow configure noticeably).

For the current shared-lib roots (`src/audio`, `src/detection`, `src/core`, `src/common`)
the file count is small enough that this is a configure-time non-issue today. Flagging
as Warning because both lint scripts are duplicated and any fix should land in both.

**Fix:** Collapse the extension loop into one GLOB call per root:

```cmake
file(GLOB_RECURSE _files
    "${_root}/*.h"  "${_root}/*.hpp" "${_root}/*.hxx"
    "${_root}/*.c"  "${_root}/*.cpp" "${_root}/*.cc"
    "${_root}/*.cxx" "${_root}/*.inc" "${_root}/*.ipp")
foreach(_file ${_files})
    math(EXPR _files_scanned "${_files_scanned} + 1")
    ...
endforeach()
```

## Info

### IN-01: `vr::` substring lint will false-positive on identifiers ending in `vr`

**File:** `cmake/lint_no_openvr_in_core.cmake:32`
**Issue:**
`_content MATCHES "vr::"` will trip on any identifier ending with `vr` followed by a
`::` scope-resolution. A namespace named `mvr::`, `nvr::`, `srv_r::` (if collapsed) — or
a variable named `srv` followed by `r::Foo` after a comment-strip — would falsely flag.
None exist in the codebase today, but as the shared layer grows toward Phase 6/7
(driver-side audio, detection threads), naming collisions become more likely.

**Fix:** Anchor with a word boundary so only the bare `vr::` token matches:

```cmake
if(_content MATCHES "[^a-zA-Z0-9_]vr::" OR _content MATCHES "^vr::")
```

CMake regex does not support `\b`, so the explicit char-class form is the portable way.

### IN-02: Both lint scripts duplicate ~30 lines verbatim

**File:** `cmake/lint_no_openvr_in_core.cmake` and `cmake/lint_no_driver_macro.cmake`
**Issue:**
The scaffold (SRC_ROOTS validation, extension list, per-file READ + scan loop,
violations aggregation, success message) is byte-identical between the two scripts.
Any fix to WR-01 / WR-03 / IN-01 above must land in both files; drift is the obvious
risk.

**Fix:** Hoist a `lint_grep_shared.cmake` helper that takes the regex(es) and message
strings as cache vars, then both scripts shrink to ~6 lines each. Defer until a third
lint is needed (rule of three). Optional.

### IN-03: Steamvr POST_BUILD copy uses `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>` directly — single-config generators (Ninja, Makefile) get an empty `$<CONFIG>` slot

**File:** `src/steamvr/CMakeLists.txt:106-109`
**Issue:**
On single-config generators, `$<CONFIG>` expands to the value of `CMAKE_BUILD_TYPE`
(or empty if unset). The destination then becomes `build/bin//openvr_api.dll` (double
slash) which works on Windows + bash but is semantically odd. Multi-config (MSBuild)
correctly produces `build/bin/Debug/` etc.

The previous form (pre-Phase 5) presumably suffered the same issue, and the project
ships Windows-MSVC-only per `CLAUDE.md`, so this is documentational rather than a real
defect. The companion `apps/micmap/CMakeLists.txt` and `apps/mic_test/CMakeLists.txt`
override `RUNTIME_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/bin` (no `$<CONFIG>`),
which is *inconsistent* with the steamvr DLL drop location — on MSBuild
the DLL lands at `build/bin/Debug/openvr_api.dll` while micmap.exe lands at
`build/bin/Debug/micmap.exe` (per-target override puts both in the same place under
the per-config dir, so this works out). Worth a comment to lock the invariant.

**Fix:** Add a comment block above lines 104-111 documenting the assumption that
all three executable targets (`micmap`, `mic_test`, `hmd_button_test`) override
`RUNTIME_OUTPUT_DIRECTORY` to a path that, after multi-config genex expansion,
matches `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>`. Otherwise the DLL co-location
breaks silently.

### IN-04: `apps/micmap/CMakeLists.txt` retains `micmap::bindings` link that is also reachable transitively via micmap_steamvr → no, actually only via direct link

**File:** `apps/micmap/CMakeLists.txt:46-52`
**Issue:**
`micmap` links `micmap::bindings` directly (line 51). Inspection of
`src/steamvr/CMakeLists.txt:24-29` shows `micmap_steamvr` does NOT link
`micmap_bindings`, so the direct link in `apps/micmap` is required (not redundant).
The Phase 5 D-09 comment block correctly explains this. No code change needed —
flagging here purely so the reviewer chain knows this was checked.

**Fix:** None. Verification only.

### IN-05: `MICMAP_BUILD_DRIVER` option default is `ON` (root CMakeLists.txt:24); SC1 headless build requires explicit `-DMICMAP_BUILD_DRIVER=OFF`

**File:** `CMakeLists.txt:24`
**Issue:**
SC1 ("mic_test.exe builds with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK absent")
is gated on developers remembering the flag. The default-ON is correct for the dev
workflow (Bigscreen Beyond + SteamVR), but a CI job that should regression-test the
headless invariant must explicitly set the flag. Worth a one-line comment near the
option declaration linking to the SC1 acceptance criterion so a future contributor
who flips the default does not silently break headless builds.

**Fix:** Add a comment:

```cmake
# Phase 5 SC1: mic_test.exe must build with MICMAP_BUILD_DRIVER=OFF and
# OpenVR absent. CI's headless job sets -DMICMAP_BUILD_DRIVER=OFF explicitly;
# do not flip this default to OFF without coordinating the CI matrix.
option(MICMAP_BUILD_DRIVER "Build OpenVR driver" ON)
```

---

## Cross-cutting verification (not flagged)

The following were checked and are correct as written:

- **Guard ordering:** `include(cmake/AssertNoOpenVRInCore.cmake)` at root
  `CMakeLists.txt:85` lands AFTER `add_subdirectory(src)` (line 78) and BEFORE
  `add_subdirectory(driver)` (line 105). Pitfall 5-C is honored.
- **PRIVATE link from driver:** `driver/CMakeLists.txt:80` links
  `micmap::core_runtime` PRIVATE — SC3 (only `HmdDriverFactory` exported from the
  DLL) preserved.
- **micmap_steamvr exclusion:** `src/CMakeLists.txt:26-32` does NOT include
  `micmap_steamvr` in the runtime aggregate. With `micmap_steamvr` linking
  `OpenVR::openvr_api` PUBLIC at `src/steamvr/CMakeLists.txt:50`, including it
  would propagate OpenVR include paths and link deps into every consumer of the
  runtime, violating SC1/SC2.
- **CMake 4.3 POST_BUILD fix:** the two-step `make_directory` + explicit-filename
  `copy_if_different` at `src/steamvr/CMakeLists.txt:104-111` is the documented
  workaround for the CMake 3.26+ rejection of bare-directory destinations. Behavior
  is identical on CMake 3.20-3.25 (the project's `cmake_minimum_required(VERSION 3.20)`
  baseline at root + driver) — no backward-compat regression.
- **Genex skip in guard recursion:** `if(_dep MATCHES "^\$<")` at
  `AssertNoOpenVRInCore.cmake:72` skips `$<LINK_ONLY:foo>` and other generator
  expressions. This does NOT create a hole because: for STATIC libs, CMake
  auto-emits PRIVATE deps into `INTERFACE_LINK_LIBRARIES` only as
  `$<LINK_ONLY:foo>`, but the same `foo` is ALSO present (un-wrapped) in
  `LINK_LIBRARIES`, which the walker reads via the second iteration of the
  `foreach(_prop ...)` loop at lines 67-79.
- **Headless invariant link path:** `apps/mic_test/CMakeLists.txt:14-17` links
  `micmap::core_runtime` only — no `micmap_steamvr`, no OpenVR. With
  `MICMAP_BUILD_DRIVER=OFF` and OpenVR absent, root `find_package(OpenVR QUIET)`
  silently sets `OpenVR_FOUND=FALSE`, the `OpenVR::openvr_api` target is never
  created, `micmap_steamvr` falls into its stub branch, and apps still build (the
  steamvr lib is still produced for `apps/micmap` and `apps/hmd_button_test`, but
  without OpenVR linkage). SC1 holds.
- **Lint regex for `MICMAP_DRIVER_BUILD`:** `cmake/lint_no_driver_macro.cmake:29`
  uses bare substring match with no word boundary. Acceptable here because the
  macro name is sufficiently unique (the only way to false-positive is to
  intentionally embed the substring). Confirmed via project-wide grep: zero
  current matches under the four shared-lib roots.

---

_Reviewed: 2026-05-02_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

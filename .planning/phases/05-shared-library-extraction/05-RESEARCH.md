# Phase 5: Shared Library Extraction — Research

**Researched:** 2026-05-01
**Domain:** CMake target topology / build-system refactor (brownfield, no source movement)
**Confidence:** HIGH

## Summary

Phase 5 is a pure CMake topology change: introduce a new `INTERFACE` aggregation target, `micmap_core_runtime`, that re-exports the four existing `STATIC` libraries (`micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`) — and explicitly omits `micmap_steamvr`. The driver DLL, the client EXE, and the headless harness all relink through this single target. A configure-time CMake guard module walks the runtime's transitive link-and-include graph and fails the build if any node touches OpenVR. A grep lint pins the no-`MICMAP_DRIVER_BUILD` invariant.

All preconditions for the refactor are already in place: the four sub-libs are `STATIC`, publish their headers via `PUBLIC $<BUILD_INTERFACE:...>`, link no OpenVR/ImGui/D3D11/cpp-httplib symbols today, and the v1.5 D-10 lift (`src/bindings/CMakeLists.txt`) supplies the canonical alias + injected-sink shape to mirror. Zero `MICMAP_DRIVER_BUILD` hits exist anywhere in `src/{audio,detection,core,common}/` today, so locking the invariant is bookkeeping, not removal. The only `__declspec(dllexport)` in the driver tree is `HmdDriverFactory` in `driver/src/driver_main.cpp:26`, so the dumpbin tertiary check is already passing — Phase 5 only has to keep it that way.

**Primary recommendation:** Mirror `src/bindings/CMakeLists.txt:14-40` (the v1.5 D-10 precedent) for naming/alias style; add `src/CMakeLists.txt` `INTERFACE` target that links the four sub-libs (drop `micmap_steamvr`); replace `apps/mic_test/CMakeLists.txt:8-13`'s 3-lib link list with `PRIVATE micmap::core_runtime` and add the same to `apps/micmap/CMakeLists.txt:39-44` (alongside the existing `micmap::bindings` and `imgui` deps) and `driver/CMakeLists.txt:69` (alongside `micmap::bindings`); make the runtime target unconditional so it survives `-DMICMAP_BUILD_DRIVER=OFF` with OpenVR absent; add `cmake/AssertNoOpenVRInCore.cmake` invoked from root `CMakeLists.txt` after `add_subdirectory(src)` and before `add_subdirectory(driver)` (D-04 ordering).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**CI guard (LIB-03):**
- **D-01:** Primary guard is a configure-time CMake module `cmake/AssertNoOpenVRInCore.cmake` that recursively walks `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES` of `micmap_core_runtime` and every transitive dependency, failing CMake configure if any node resolves to `openvr_api` / `OpenVR::openvr_api` or if any propagated `INTERFACE_INCLUDE_DIRECTORIES` entry matches `*/openvr*`. Configure-time fail beats build-time — every developer sees it on first cmake run, not just CI.
- **D-02:** Secondary guard is a source-grep CI step that fails when `<openvr.h>`, `<openvr_driver.h>`, or `vr::` namespace appears under `src/audio/`, `src/detection/`, `src/core/`, or `src/common/`. Catches violations the linker walk can't see (header-only dependencies, stray includes).
- **D-03:** Existing v1.5 `dumpbin /exports driver_micmap.dll` (must show only `HmdDriverFactory`) stays as tertiary defense on the driver side — it is *not* the shared-layer guard, but it remains a hard CI gate to detect leaks introduced by anything below the runtime.
- **D-04:** Root `CMakeLists.txt` calls `include(cmake/AssertNoOpenVRInCore.cmake)` after `add_subdirectory(src)` and before `add_subdirectory(driver)`. Ordering matters — guard must see all four sub-lib targets defined before it walks them.

**`micmap_lib` disposition:**
- **D-05:** Replace `micmap_lib` (currently in `src/CMakeLists.txt`, aggregates all 5 libs including `micmap_steamvr`) with a new `micmap_core_runtime` INTERFACE target that explicitly drops `micmap_steamvr`. The OpenVR-touching layer is the boundary, not part of the shared runtime.
- **D-06:** Provide `micmap::core_runtime` ALIAS for namespace-style consumption, mirroring the `micmap::core` / `micmap::bindings` precedent.
- **D-07:** Delete `micmap_lib` entirely once consumers are switched. No deprecation period — single source of truth, no parallel paths to drift.

**Consumer linkage:**
- **D-08:** `apps/mic_test/CMakeLists.txt` replaces its 3-lib link list (`micmap_audio`, `micmap_detection`, `micmap_common`) with single `PRIVATE micmap::core_runtime`. This is the SC1 acceptance probe — `mic_test.exe` must build with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK absent.
- **D-09:** `apps/micmap/CMakeLists.txt` links `micmap::core_runtime` for the shared portion and continues to link `micmap_steamvr` directly for `IDriverClient` / HTTP-bridge plumbing.
- **D-10:** `driver/CMakeLists.txt` adds `PRIVATE micmap::core_runtime`. **No driver TU may `#include` any runtime header in this phase** — link-only, byte-for-byte identical behavior to v1.5 (SC5).
- **D-11:** `target_link_libraries(driver_micmap PUBLIC ...)` against the runtime is forbidden (Pitfall 6 — symbol re-export across DLL boundary). PRIVATE only.

**Macro and ODR hygiene:**
- **D-12:** Zero `#ifdef MICMAP_DRIVER_BUILD` (or any other host-switching macro) inside `src/{audio,detection,core,common}/`. Confirmed zero hits today (2026-05-01); P5 locks the invariant as a CI grep assertion.
- **D-13:** No `__declspec(dllexport)` / `__declspec(dllimport)` annotations anywhere in the shared layer (Pitfall 6).

**Header layout:**
- **D-14:** Pure transitive INTERFACE propagation — no umbrella header, no new include directories. Revisit only if Phase 7 finds the lack of facade hurts driver consumption.

**cpp-httplib CVE bump:**
- **D-15:** v0.14.3 → v0.20.1 (CVE-2025-46728) is **deferred to Phase 8 (IPC Reshape)**. Out of P5 scope.

**Pitfall 15 (json bloat) hedge:**
- **D-16:** No nlohmann/json serializer centralization in P5. Behavior-changing optimization; defer to Phase 8.

### Claude's Discretion
- Exact wording / layout of the configure-time guard module (helper macros, error message format)
- Phase 4 D-10 alias style (`micmap::bindings`) vs `micmap::core_runtime` namespace formatting — mirror the v1.5 convention exactly
- Whether the source-grep CI step lives in a new `cmake/lint_no_openvr_in_core.cmake` module driven by `add_test` or in a dedicated CI job — implementation detail, either is acceptable as long as it runs on every PR

### Deferred Ideas (OUT OF SCOPE)
- **cpp-httplib v0.14.3 → v0.20.1 bump** — Phase 8 prerequisite plan
- **nlohmann/json serializer centralization (Pitfall 15)** — Phase 8
- **Logger sink wiring (LIB-04)** — Phase 8
- **Umbrella header (`include/micmap_runtime/runtime.hpp`)** — reconsider in Phase 7 if needed
- **Headless canary CMake target (Pitfall 9 mitigation)** — Phase 7
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| LIB-01 | New CMake target `micmap_core_runtime` (INTERFACE library) aggregates `micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`. Carries no transitive dependency on OpenVR, ImGui, D3D11, or cpp-httplib. | Architecture Patterns §INTERFACE Aggregation; Standard Stack table; Code Examples §Pattern 1; Don't Hand-Roll table (CMake INTERFACE) |
| LIB-02 | `driver_micmap.dll`, `micmap.exe`, `mic_test.exe` all link `micmap_core_runtime`. `mic_test.exe` continues to build and run with `-DMICMAP_BUILD_DRIVER=OFF` (headless invariant). | Code Examples §Pattern 2 (consumer linkage); Validation Architecture §SC1 build matrix |
| LIB-03 | `cmake/AssertNoOpenVRInCore.cmake` fails the build if any target inside `micmap_core_runtime` transitively links `openvr_api` or includes any OpenVR header. CI runs this on every build. | Code Examples §Pattern 3 (configure-time walk); Validation Architecture §SC2 |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Define shared-runtime boundary (the new INTERFACE target) | Build system (CMake) | — | Pure metadata; INTERFACE libs produce no artifact, just propagate link/include requirements |
| Enforce no-OpenVR invariant | Build system (CMake configure-time) + CI lint | — | D-01 walks the link graph at configure; D-02 grep is CI safety net for header-only leaks |
| Aggregate the four STATIC sub-libs | Build system (INTERFACE → STATIC) | — | Sub-libs already STATIC with PUBLIC includes; INTERFACE re-exports without recompilation |
| Link the runtime into driver DLL | Driver build (`driver/CMakeLists.txt`) | — | `PRIVATE` linkage only (D-11); driver TUs must not include runtime headers in P5 (D-10/SC5) |
| Link the runtime into client EXE | Client build (`apps/micmap/CMakeLists.txt`) | — | Co-exists with `micmap_steamvr` (D-09) which still owns OpenVR-touching client code |
| Link the runtime into headless harness | Test-app build (`apps/mic_test/CMakeLists.txt`) | — | SC1 acceptance probe — must build without OpenVR present (`-DMICMAP_BUILD_DRIVER=OFF`) |
| Lock no-`MICMAP_DRIVER_BUILD` invariant | Source-grep CI step | — | No code change required today (zero hits); the assertion is the lock |
| Maintain dumpbin export surface | Driver build / CI | — | Inherited v1.5 invariant; P5 must not regress (only `HmdDriverFactory` exported) |

## Standard Stack

### Core (already in tree, no version bumps)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| CMake | 3.20+ (locked at root `CMakeLists.txt:5`) | Build system; INTERFACE libraries are stable since CMake 3.0 | Project-locked stack [VERIFIED: CMakeLists.txt:5] |
| MSVC (Visual Studio 2022) | Project default; `/W4` `/MP` (root `CMakeLists.txt:46-49`) | Compiler; archives the STATIC sub-libs | v1.5 baseline [VERIFIED: STACK.md] |

### Supporting (CMake idioms used by P5)

| Idiom | Where Used | Purpose | When to Use |
|-------|------------|---------|-------------|
| `add_library(name INTERFACE)` | New `src/CMakeLists.txt` runtime target | Header/dep aggregator with no compile units | Always for pure aggregation [CITED: cmake.org add_library] |
| `add_library(micmap::name ALIAS name)` | `src/{audio,detection,core,common,bindings}/CMakeLists.txt:25/40/30/17/40` | Namespace-style consumption | Already the project convention [VERIFIED: in-tree] |
| `target_link_libraries(name INTERFACE ...)` | New runtime target | Re-export deps to consumers | Standard INTERFACE pattern [CITED: cmake.org] |
| `target_compile_features(name INTERFACE cxx_std_17)` | New runtime target | Propagate C++17 requirement | Mirrors `micmap_bindings:36` PUBLIC equivalent [VERIFIED] |
| `get_target_property(... INTERFACE_LINK_LIBRARIES)` | New `cmake/AssertNoOpenVRInCore.cmake` | Walk the transitive link graph at configure time | Standard CMake introspection [CITED: cmake.org get_target_property] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `INTERFACE` aggregation library | Combine sources into one larger STATIC lib | Forces source movement; violates SC5 byte-identical exit criterion; loses sub-lib boundaries that other phases rely on |
| Configure-time CMake walk (D-01) | Build-time post-link `dumpbin` check only | Build-time fails are slower feedback and only catch what the linker actually pulled (misses header-only OpenVR includes); D-02 grep + D-01 walk together cover both vectors |
| Umbrella header `<micmap_runtime/runtime.hpp>` | Pure transitive include propagation (D-14) | Consumers continue using existing per-sub-lib paths; umbrella adds maintenance cost and a new public-API surface for zero P5 benefit |

**Installation:** No new dependencies. The phase is build-system metadata only.

**Version verification:** N/A — no new third-party deps in P5 (D-15 defers cpp-httplib bump).

## Architecture Patterns

### System Architecture Diagram (build-time link graph)

```
                              ┌──────────────────────────────────────┐
                              │   cmake/AssertNoOpenVRInCore.cmake   │
                              │  (CONFIGURE-TIME WALK over runtime) │
                              │  fails if any node → openvr_api      │
                              │  or include dir matches */openvr*    │
                              └─────────────────┬────────────────────┘
                                                │ inspects
                                                ▼
                              ┌──────────────────────────────────────┐
                              │  micmap_core_runtime (INTERFACE)     │
                              │  — no compile units, no artifacts —  │
                              │  propagates PUBLIC includes/cxx_std  │
                              └────┬───────┬───────┬───────┬─────────┘
                                   │       │       │       │
                                   ▼       ▼       ▼       ▼
                           micmap_audio  micmap_detection  micmap_core  micmap_common
                           (STATIC)      (STATIC)          (STATIC)     (STATIC)
                              │ PUBLIC      │ PUBLIC          │ PUBLIC      │ no deps
                              ▼             ▼                 ▼
                           micmap_common ◄─────────────────── micmap_common
                           (already linked transitively — DAG, not tree)

  CONSUMERS                  (INTERFACE re-export — link-only)
  ─────────                                                          ┌───────────────┐
  driver_micmap.dll  ──── PRIVATE micmap::core_runtime ─────────────►│  runtime DAG  │
  + PRIVATE OpenVR::openvr_api + httplib + nlohmann + micmap::bindings    above
                                                                     └───────────────┘
  micmap.exe         ──── PRIVATE micmap::core_runtime ────────────►   (same DAG)
  + PRIVATE imgui + micmap::bindings + (current micmap_lib drops)

  mic_test.exe       ──── PRIVATE micmap::core_runtime ────────────►   (same DAG)
  + PRIVATE comctl32 + comdlg32   ← no OpenVR, no ImGui, no httplib
                                  ← SC1: builds with -DMICMAP_BUILD_DRIVER=OFF
                                    even if find_package(OpenVR) returns NOT FOUND

  EXCLUDED FROM SHARED LAYER (still standalone STATIC libs):
  ──────────────────────────────────────────────────────────
  micmap_steamvr (links OpenVR PUBLIC) — consumed directly by:
    - driver_micmap.dll (NO — driver links openvr directly, not via steamvr)
    - micmap.exe (YES — for IDriverClient / HTTP-bridge plumbing, D-09)
    - hmd_button_test (YES — VR-only test harness)
    - tests/test_manifest_registrar, test_vr_input_quit_ordering (YES)
  micmap_bindings (PUBLIC nlohmann_json, NO OpenVR) — kept as parallel STATIC
    not aggregated into micmap_core_runtime; consumed directly by driver +
    micmap.exe per Phase 4 D-10 lift; preserves byte-identical behavior (SC5).
```

### Recommended Project Structure (delta only)

```
mic-map/
├── cmake/
│   ├── FindOpenVR.cmake          # existing
│   ├── FindOpenXR.cmake          # existing
│   └── AssertNoOpenVRInCore.cmake  # NEW — D-01 configure-time guard
├── src/
│   └── CMakeLists.txt            # MODIFIED — replace micmap_lib with micmap_core_runtime
├── apps/
│   ├── mic_test/CMakeLists.txt   # MODIFIED — link micmap::core_runtime (D-08)
│   └── micmap/CMakeLists.txt     # MODIFIED — replace micmap_lib with micmap::core_runtime (D-09)
├── driver/CMakeLists.txt         # MODIFIED — add PRIVATE micmap::core_runtime (D-10)
└── CMakeLists.txt                # MODIFIED — include(AssertNoOpenVRInCore) after add_subdirectory(src) (D-04)
```

### Pattern 1: INTERFACE Aggregation Target

**What:** Use `add_library(<name> INTERFACE)` to define a build-system façade with no source files. The target carries link requirements and propagates them transitively to consumers. Consumers `target_link_libraries(... PRIVATE <name>)` and inherit the entire dependency closure.

**When to use:** Aggregating multiple STATIC libs under a single name without recompiling sources.

**Example:** [VERIFIED: in-tree precedent at `src/CMakeLists.txt:11-20`]

```cmake
# src/CMakeLists.txt — replaces existing micmap_lib (D-05/D-07)
add_library(micmap_core_runtime INTERFACE)
target_link_libraries(micmap_core_runtime INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_common
    # NOTE: micmap_steamvr deliberately omitted (D-05) — that lib links OpenVR PUBLIC
)
target_compile_features(micmap_core_runtime INTERFACE cxx_std_17)
add_library(micmap::core_runtime ALIAS micmap_core_runtime)
```

The four sub-libs already publish their headers via `target_include_directories(... PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)` (verified at `src/audio/CMakeLists.txt:10-14`, `src/detection/CMakeLists.txt:10-14`, `src/core/CMakeLists.txt:9-13`, `src/common/CMakeLists.txt:9-13`), so consumers automatically inherit include paths through the INTERFACE target — no extra `target_include_directories` call is needed (D-14).

### Pattern 2: Consumer Relinking

**What:** Replace per-sub-lib link lists with a single INTERFACE target reference. Use `PRIVATE` linkage (D-11) — the runtime is an implementation detail of each consumer, never re-exported.

**When to use:** Each consumer that needs the four sub-libs.

**Example:** [VERIFIED: current targets to be modified]

```cmake
# apps/mic_test/CMakeLists.txt:8-13 — D-08 (SC1 acceptance probe)
# BEFORE:
target_link_libraries(mic_test
    PRIVATE
        micmap_audio
        micmap_detection
        micmap_common
)
# AFTER:
target_link_libraries(mic_test
    PRIVATE
        micmap::core_runtime
)

# apps/micmap/CMakeLists.txt:39-44 — D-09 (keep micmap_steamvr direct)
# BEFORE:  PRIVATE micmap_lib imgui micmap::bindings
# AFTER:   PRIVATE micmap::core_runtime micmap_steamvr imgui micmap::bindings
#                                       └── direct link (D-09)

# driver/CMakeLists.txt:69 — D-10 (link-only, no header includes in P5)
# AFTER (append to existing target_link_libraries(... PRIVATE ...)):
target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)
```

### Pattern 3: Configure-Time No-OpenVR Guard

**What:** Walk `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES` of the runtime target and every transitive dependency at configure time. Fail with `message(FATAL_ERROR ...)` if any visited node matches `openvr_api` (case-insensitive) or carries an `INTERFACE_INCLUDE_DIRECTORIES` entry whose path contains `openvr` (lowercased).

**When to use:** Once, in `cmake/AssertNoOpenVRInCore.cmake`, included from root `CMakeLists.txt` after `add_subdirectory(src)` (D-04) so all four sub-lib targets exist before introspection runs.

**Example skeleton:** [CITED: cmake.org `get_target_property`, `if(TARGET ...)`]

```cmake
# cmake/AssertNoOpenVRInCore.cmake
function(_assert_no_openvr_recurse target visited_var)
    if(NOT TARGET ${target})
        # Bare library names (e.g., system libs) are leaves — nothing to walk
        return()
    endif()

    list(FIND ${visited_var} ${target} _seen)
    if(NOT _seen EQUAL -1)
        return()
    endif()
    list(APPEND ${visited_var} ${target})
    set(${visited_var} ${${visited_var}} PARENT_SCOPE)

    # 1. Forbidden link node by name (handles aliases via aliased-target lookup).
    set(_resolved ${target})
    get_target_property(_alias ${target} ALIASED_TARGET)
    if(_alias)
        set(_resolved ${_alias})
    endif()
    string(TOLOWER "${_resolved}" _lc)
    if(_lc MATCHES "openvr")
        message(FATAL_ERROR
            "AssertNoOpenVRInCore: target '${_resolved}' is reachable from "
            "micmap_core_runtime — OpenVR must not leak into the shared layer.")
    endif()

    # 2. Forbidden include path on the propagated INTERFACE_INCLUDE_DIRECTORIES.
    get_target_property(_incs ${_resolved} INTERFACE_INCLUDE_DIRECTORIES)
    if(_incs)
        foreach(_inc ${_incs})
            string(TOLOWER "${_inc}" _inc_lc)
            if(_inc_lc MATCHES "openvr")
                message(FATAL_ERROR
                    "AssertNoOpenVRInCore: target '${_resolved}' propagates "
                    "include dir '${_inc}' (matches openvr).")
            endif()
        endforeach()
    endif()

    # 3. Recurse into INTERFACE_LINK_LIBRARIES + LINK_LIBRARIES.
    foreach(_prop INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
        get_target_property(_deps ${_resolved} ${_prop})
        if(_deps)
            foreach(_dep ${_deps})
                # Skip generator expressions and $<LINK_ONLY:...> wrappers as needed.
                if(_dep MATCHES "^\\$<")
                    continue()
                endif()
                _assert_no_openvr_recurse(${_dep} ${visited_var})
                set(${visited_var} ${${visited_var}} PARENT_SCOPE)
            endforeach()
        endif()
    endforeach()
endfunction()

set(_visited "")
_assert_no_openvr_recurse(micmap_core_runtime _visited)
message(STATUS "AssertNoOpenVRInCore: clean (visited ${CMAKE_CURRENT_LIST_LINE} targets)")
```

> **Discretion call (planner):** the helper-macro layout above is one acceptable shape. The CONTEXT marks "exact wording / layout of the configure-time guard module" as Claude's discretion. The shape MUST: (a) be triggered at configure time, not build time; (b) walk both `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES`; (c) follow ALIAS targets to their aliased targets (`get_target_property(... ALIASED_TARGET)`); (d) check both target name AND propagated include directories; (e) FATAL_ERROR on any hit; (f) be idempotent (visited-set guard) so the recursion terminates on DAG cycles or shared leaves.

### Pattern 4: Source-Grep Lint (D-02)

**What:** Reject any direct OpenVR header include or `vr::` namespace use under the four shared-lib source roots.

**When to use:** As a CI step on every PR. The CONTEXT marks the implementation choice (custom `cmake/lint_no_openvr_in_core.cmake` driven by `add_test`, vs a dedicated CI-job script) as Claude's discretion. Either is acceptable.

**Reference command (CI-job form):**

```bash
# Returns non-zero if anything matches → CI fails.
! grep -rEn '<openvr(_driver)?\.h>|vr::' \
    src/audio/ src/detection/ src/core/ src/common/
```

**Reference command (CMake/CTest form):**

```cmake
# cmake/lint_no_openvr_in_core.cmake (alternative implementation)
add_test(NAME lint_no_openvr_in_core
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)
```

**Verified zero-hits today (2026-05-01):** `Grep` over `src/audio/`, `src/detection/`, `src/core/`, `src/common/` for `openvr|OpenVR|vr::` returns no files. The lint is a lock, not a sweep.

### Anti-Patterns to Avoid

- **`PUBLIC` linkage on the driver consumer.** `target_link_libraries(driver_micmap PUBLIC micmap::core_runtime)` re-exports every shared-lib symbol from the DLL surface. Pitfall 6 / D-11. Use `PRIVATE`. [VERIFIED: PITFALLS.md:221]
- **Adding `__declspec(dllexport)` to any shared-lib TU.** D-13. The shared lib is internal to each consumer; static visibility only. The only export in the entire driver tree today is `HmdDriverFactory` at `driver/src/driver_main.cpp:26` — keep that the only one. [VERIFIED: project-wide grep returned exactly one hit in `driver/src/`]
- **Compile-time host-switching macros (`#ifdef MICMAP_DRIVER_BUILD`) inside the four sub-libs.** D-12. Diverges driver and `mic_test` paths and re-introduces Pitfall 9. [VERIFIED: PITFALLS.md:319; zero hits today across `src/`]
- **Static globals / singletons (`Logger::instance()`) in the shared lib.** Each binary gets its own copy → file-corrupting dual-writer. Sink injection is the established pattern (LIB-04 / Phase 8). [CITED: PITFALLS.md:209,223]
- **Aggregating `micmap_steamvr` into the runtime.** It links `OpenVR::openvr_api` PUBLIC at `src/steamvr/CMakeLists.txt:50`, which would propagate OpenVR into every shared-lib consumer and break SC1 + SC2.
- **Adding the runtime target inside an `if(MICMAP_BUILD_DRIVER)` / `if(OpenVR_FOUND)` guard.** Defeats SC1. Define it unconditionally so `mic_test.exe` can link it without OpenVR present.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Aggregate multiple STATIC libs under one name | A wrapper STATIC lib with empty `.cpp` | `add_library(... INTERFACE)` | INTERFACE is the documented CMake idiom for header/dep-only targets; produces no spurious archive [CITED: cmake.org] |
| Walk a target's transitive deps | Custom file-parsing of `CMakeCache.txt` | `get_target_property(... INTERFACE_LINK_LIBRARIES)` recursively | First-class CMake API; handles ALIAS targets via `ALIASED_TARGET` [CITED: cmake.org get_target_property] |
| Detect OpenVR include leaks | Compile-time `#error` macros in shared headers | Configure-time CMake walk + source grep | Compile-time macros only fire when the shared header is included by something that includes OpenVR — cannot detect a sub-lib's PRIVATE OpenVR include since shared headers don't see it. The CMake walk catches link leaks; the grep catches include leaks. |
| Namespace-style consumption | Manual `target_link_libraries(... mylib_full_name)` everywhere | `add_library(ns::name ALIAS name)` per sub-lib | Already the project convention [VERIFIED: 5/5 sub-libs have ALIAS targets] |

**Key insight:** Phase 5 is a structural refactor in a build system that already has every primitive needed. The risk is not "can we build the right CMake target" — it's "can we keep the boundary clean across the next 6 phases of behavioral migration." That's why the configure-time guard (D-01), grep lint (D-02), and dumpbin tertiary (D-03) are all present: layered defense, each catching a different leak vector. The same source code must compile identically into 3 binaries with very different deps, indefinitely.

## Common Pitfalls

### Pitfall 5-A: ALIAS targets are not walked the same way as direct targets

**What goes wrong:** The recursion in `AssertNoOpenVRInCore.cmake` calls `get_target_property(... INTERFACE_LINK_LIBRARIES)` on a name like `micmap::core` (an alias). Some CMake versions return the property as expected; others return `<TARGET>-NOTFOUND` because property reads on alias targets are restricted.
**Why it happens:** ALIAS targets are first-class for `target_link_libraries` consumption but second-class for `get_target_property` introspection.
**How to avoid:** Always resolve aliases with `get_target_property(_alias <name> ALIASED_TARGET)` and recurse on the resolved name. The reference snippet (Pattern 3) does this.
**Warning signs:** Configure-time pass succeeds even when an OpenVR-touching alias is reachable from the runtime; the recursion stops at the alias node without descending.

### Pitfall 5-B: `find_package(OpenVR QUIET)` defines `OpenVR::openvr_api` only if found

**What goes wrong:** SC1 demands a successful `mic_test.exe` build with the OpenVR SDK absent. If any unconditional CMake reference to `OpenVR::openvr_api` exists in the runtime path, configure fails before the guard ever runs.
**Why it happens:** Imported targets only exist after a successful `find_package`. The guard module reads target properties only on demand; a missing target is treated as a leaf (returns harmlessly).
**How to avoid:** The runtime target itself never references `OpenVR::openvr_api` directly. The guard's `if(NOT TARGET ${target})` early-return handles missing imports gracefully. The driver subdirectory is already gated on `OpenVR_FOUND` at root `CMakeLists.txt:97-102`, so the driver target simply doesn't exist in headless builds — `mic_test` builds fine.
**Warning signs:** `cmake -B build -DMICMAP_BUILD_DRIVER=OFF` errors out with `Cannot specify link libraries for target which is not built by this project` — means a non-driver target is referencing the OpenVR imported target unconditionally.

### Pitfall 5-C: Nested `add_subdirectory` ordering breaks the guard

**What goes wrong:** Root `CMakeLists.txt` includes `cmake/AssertNoOpenVRInCore.cmake` before all four sub-lib targets are defined. The guard runs against an incomplete graph, passes spuriously, and ships a broken invariant.
**Why it happens:** `include()` runs at the point of inclusion. Targets created by `add_subdirectory(src)` only exist after that call returns.
**How to avoid:** Place `include(cmake/AssertNoOpenVRInCore.cmake)` immediately after `add_subdirectory(src)` and before `add_subdirectory(driver)` (D-04 explicit). Verify with `if(NOT TARGET micmap_core_runtime) message(FATAL_ERROR "guard included before runtime target defined")` at the top of the guard module.
**Warning signs:** Guard reports `clean (visited 0 targets)` — silent pass on an empty walk.

### Pitfall 5-D: Driver TUs accidentally `#include` a runtime header

**What goes wrong:** SC5 demands byte-identical driver behavior. If a driver TU starts including `<micmap/audio/audio_capture.hpp>` "just to use the type alias" in P5, the driver binary changes (template instantiations, debug-info bloat) and v1.5 UAT correlation breaks.
**Why it happens:** Once `micmap::core_runtime` is linked PRIVATE to `driver_micmap`, every PUBLIC include path of every sub-lib is reachable from any driver TU. Compilation succeeds even if no symbol is referenced.
**How to avoid:** D-10 is explicit — link only, no driver TU may `#include` any runtime header in P5. Enforce via grep CI: `! grep -rEn '#include\s*[<"]micmap/(audio|detection|core|common)/' driver/src/`. (Optional belt-and-braces; planner can choose to add as a guard or rely on code review since the change set is small.)
**Warning signs:** Driver DLL size changes by more than a few KB after linking the runtime. Run `dumpbin /headers driver_micmap.dll | findstr /i size` before and after; expect ~zero delta.

### Pitfall 5-E: `tests/` targets quietly become OpenVR-dependent

**What goes wrong:** Several `tests/` targets currently link `micmap::common` and `micmap::core` directly (e.g., `test_config_manager`, `test_cli_flags_parse`). If a developer "modernizes" them to use `micmap::core_runtime`, the test surface picks up everything in the runtime — fine — but if someone later adds a new sub-lib to the runtime that does carry OpenVR, every test pulls it in. Phase 5's invariant must apply to the runtime's content, not to test linkage.
**Why it happens:** Convenience refactors expand link surface without thinking about transitive cost.
**How to avoid:** Phase 5 does NOT refactor `tests/` link lists. Tests continue to depend on the narrowest sub-lib they need. The runtime exists for application binaries (driver, client, mic_test); tests are diagnostic scaffolding and may keep direct links. Document this explicitly in plan rationale.
**Warning signs:** A test suite starts requiring OpenVR to run after a future runtime expansion — points back to a Phase 5 plan that didn't draw this line.

## Code Examples

### Example 1: Complete `cmake/AssertNoOpenVRInCore.cmake` (reference)

See Pattern 3 above — `_assert_no_openvr_recurse` function plus a single top-level invocation against `micmap_core_runtime`. Source-of-truth: this RESEARCH.md (no in-tree precedent yet, since this file is being created in P5).

### Example 2: Replacing `micmap_lib` with `micmap_core_runtime`

Current state (`src/CMakeLists.txt:11-21`) [VERIFIED]:

```cmake
add_library(micmap_lib INTERFACE)
target_link_libraries(micmap_lib INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_steamvr  # <-- to be dropped
    micmap_common
)
add_library(micmap::lib ALIAS micmap_lib)
```

Target state per D-05/D-06/D-07:

```cmake
add_library(micmap_core_runtime INTERFACE)
target_link_libraries(micmap_core_runtime INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_common
    # micmap_steamvr deliberately excluded — see cmake/AssertNoOpenVRInCore.cmake
)
target_compile_features(micmap_core_runtime INTERFACE cxx_std_17)
add_library(micmap::core_runtime ALIAS micmap_core_runtime)

# micmap_lib + micmap::lib deleted (D-07). Verify with:
#   grep -rn 'micmap_lib\|micmap::lib' apps/ driver/ tests/ src/
# All hits must be removed in this same change-set.
```

### Example 3: Verifying SC1 — headless build with OpenVR absent

```bash
# 1. Simulate OpenVR-absent environment by unsetting OPENVR_SDK_PATH and
#    moving any external/openvr/ aside.
mv external/openvr external/openvr.bak 2>/dev/null || true
unset OPENVR_SDK_PATH

# 2. Clean configure with driver explicitly disabled.
rm -rf build-headless
cmake -B build-headless -DMICMAP_BUILD_DRIVER=OFF -G "Visual Studio 17 2022" -A x64

# 3. Configure must succeed — root CMakeLists.txt:69-74 prints
#    "OpenVR not found - SteamVR features will use stub implementation"
#    and skips driver subdir at line 96-102.

# 4. Build mic_test specifically.
cmake --build build-headless --target mic_test --config Release

# 5. Restore.
mv external/openvr.bak external/openvr 2>/dev/null || true
```

### Example 4: Verifying SC3 — driver export surface

```bash
# Visual Studio Developer Command Prompt
dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll
# Expected output (excerpt):
#     ordinal hint RVA      name
#           1    0 0000XXXX HmdDriverFactory
# Exactly one entry. Any other export is a regression and SC3 fails.
```

[VERIFIED: only `__declspec(dllexport)` in entire tree is `HmdDriverFactory` at `driver/src/driver_main.cpp:26`]

### Example 5: Verifying SC4 — no `MICMAP_DRIVER_BUILD` in shared layer

```bash
grep -rn 'MICMAP_DRIVER_BUILD' src/audio/ src/detection/ src/core/ src/common/
# Expected: no matches, exit code 1.
echo $?  # Must be 1.
```

[VERIFIED: zero hits today, 2026-05-01]

## Runtime State Inventory

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — Phase 5 changes no runtime data formats; `config.json`, `training_data.bin`, `app.vrmanifest`, `driver.vrdrivermanifest` all unchanged | None |
| Live service config | None — SteamVR's `appconfig.json`, `openvrpaths.vrpath`, and Inno Setup registry entries are not touched. Driver remains registered the same way (no manifest changes). | None |
| OS-registered state | None — no Task Scheduler entries, no service registrations, no auto-start changes (Phase 3 owns those). | None |
| Secrets/env vars | Only `OPENVR_SDK_PATH` (existing dev convenience var); P5 does not change how it's read. SC1 explicitly tests the unset case. | None |
| Build artifacts | `micmap_lib.lib` archive will disappear from `build/lib/` after D-07. Sub-lib archives (`micmap_audio.lib`, `micmap_detection.lib`, `micmap_core.lib`, `micmap_common.lib`) are unchanged. INTERFACE targets produce no archive — `micmap_core_runtime` is metadata-only. | Clean rebuild after the rename to flush stale `micmap_lib.lib` from existing build directories; document in plan acceptance: "first build after merge: `rm -rf build && cmake -B build`". |

**The canonical question:** *After every file in the repo is updated, what runtime systems still have the old string cached, stored, or registered?* — **Answer: none.** Phase 5 is a build-system refactor with no runtime-state surface beyond the `micmap_lib.lib` build artifact.

## Project Constraints (from CLAUDE.md)

Extracted from `./CLAUDE.md`:

- **Locked stack this milestone:** C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. **No framework changes** in P5 (D-15 confirms cpp-httplib stays at v0.14.3 for this phase).
- **Bash via Git Bash; Unix-style paths** in shell commands (forward slashes, `/dev/null`).
- **Windows-only:** WASAPI, OpenVR driver DLL, Inno Setup. P5 verification commands assume Visual Studio Developer Command Prompt for `dumpbin`.
- **Phase order is load-bearing.** Phase 5 is keystone; downstream phases depend on the boundary existing. Do not defer guard work.
- **Do not skip phase artifacts.** Plan/research/verify/discuss artifacts are project memory.
- **Visual validation on real HMD is mandatory** for v1.5 UAT regression (SC5). `cmake --build` success and dumpbin checks do not substitute. Plan must include "5-cycle SteamVR start/stop on Bigscreen Beyond rig with v1.5 trigger path still working" as the SC5 acceptance probe.
- **`mic_test.exe`** is the audio/detection harness without VR; SC1 acceptance probe binds here.
- **`hmd_button_test.exe`** is the VR-input harness without audio; not modified by P5 (still links `micmap_steamvr` directly per `apps/hmd_button_test/CMakeLists.txt:8-12`).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build | ✓ (project-locked) | 3.20+ [VERIFIED: root CMakeLists.txt:5] | — |
| MSVC toolchain (Visual Studio 2022) | Compile | ✓ (assumed dev environment) | 2022, /MP /W4 | — |
| OpenVR SDK | Driver build only | conditional (`find_package(OpenVR QUIET)` at root `CMakeLists.txt:69-74`) | varies | `MICMAP_BUILD_DRIVER=OFF` skips driver; SC1 *requires* this fallback works |
| `dumpbin.exe` | SC3 export-surface check | ✓ (ships with MSVC) | matches MSVC | None — SC3 verification needs MSVC dev env |
| `grep` (Git Bash) | SC4 + D-02 lint | ✓ | any | `findstr` is a Windows fallback but its regex is weaker; prefer Git Bash for parity with CONTEXT commands |
| Bigscreen Beyond + Win11 Pro rig | SC5 v1.5 UAT regression | ✓ (project hardware) | — | None — visual + behavioral validation required |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:** OpenVR SDK absence is *required* for SC1; the project already handles it correctly.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest + ad-hoc test executables (no GoogleTest yet — `MICMAP_USE_GTEST` is OFF per `tests/CMakeLists.txt:8`) |
| Config file | `tests/CMakeLists.txt` (per-target) + root `enable_testing()` at `CMakeLists.txt:91` |
| Quick run command | `cmake --build build --target <target> --config Release && ctest --test-dir build -C Release -R <regex>` |
| Full suite command | `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| LIB-01 | `micmap_core_runtime` INTERFACE target exists; aggregates the four sub-libs; carries no OpenVR/ImGui/D3D11/cpp-httplib transitive | configure-time CMake assertion | `cmake -B build && cmake --build build --target micmap_core_runtime` (INTERFACE has no archive — verifies the target name resolves, not that it builds an artifact). Combine with `cmake -B build` running `AssertNoOpenVRInCore.cmake` to FATAL_ERROR on any leak. | ❌ Wave 0 — `cmake/AssertNoOpenVRInCore.cmake` and modified `src/CMakeLists.txt` |
| LIB-02 | All three binaries link the runtime; `mic_test.exe` builds with `-DMICMAP_BUILD_DRIVER=OFF` | build-matrix integration | (a) `cmake -B build-driver -DMICMAP_BUILD_DRIVER=ON && cmake --build build-driver --config Release` — all three binaries produced. (b) `cmake -B build-headless -DMICMAP_BUILD_DRIVER=OFF && cmake --build build-headless --target mic_test --config Release` — succeeds with OpenVR absent. | ❌ Wave 0 — modified consumer CMakeLists.txt files |
| LIB-03 | Build fails if any runtime-reachable target links openvr or includes OpenVR header | configure-time + grep | (a) `cmake -B build` — passes today. (b) Negative test: temporarily add `target_link_libraries(micmap_audio PUBLIC OpenVR::openvr_api)` then `cmake -B build-poison` — must FATAL_ERROR. (c) `! grep -rEn '<openvr(_driver)?\.h>\|vr::' src/audio/ src/detection/ src/core/ src/common/` — exit code 1. | ❌ Wave 0 — `cmake/AssertNoOpenVRInCore.cmake` + lint script |
| SC3 (drives LIB-01/03) | Driver DLL exports only `HmdDriverFactory` | post-build manual | `dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll \| findstr /R "^[ ]*[0-9]"` — exactly one line | ✓ Already passing today |
| SC4 (drives LIB-01) | Zero `MICMAP_DRIVER_BUILD` hits in shared layer | grep | `! grep -rn 'MICMAP_DRIVER_BUILD' src/audio/ src/detection/ src/core/ src/common/` — exit code 1 | ✓ Zero hits today (lock the invariant) |
| SC5 (drives LIB-02) | Driver behavior byte-identical to v1.5 | manual UAT on Bigscreen Beyond | 5-cycle SteamVR start/stop with v1.5 trigger path: launch `micmap.exe` → see no laser beam → cover-mic triggers dashboard → repeat 5×. Identical visual behavior to v1.5. **Cannot be automated.** | manual-only — required by CLAUDE.md "visual validation mandatory" rule |

### Sampling Rate

- **Per task commit:** `cmake -B build && cmake --build build --target <changed-target> --config Release` (~30s incremental). Reuse for the configure-time guard which fires automatically.
- **Per wave merge:** Full headless + full driver-on builds plus `ctest --output-on-failure`.
- **Phase gate:** All 5 success criteria pass; v1.5 UAT regression on Bigscreen Beyond rig signed off; `/gsd-verify-work` invoked.

### Wave 0 Gaps

- [ ] `cmake/AssertNoOpenVRInCore.cmake` — implements LIB-03 configure-time guard (Pattern 3 reference)
- [ ] `cmake/lint_no_openvr_in_core.cmake` (or equivalent CI script) — implements D-02 source-grep lint
- [ ] Modified `src/CMakeLists.txt` — replace `micmap_lib` with `micmap_core_runtime` (Pattern 1)
- [ ] Modified `apps/mic_test/CMakeLists.txt` — D-08 SC1 acceptance probe (Pattern 2)
- [ ] Modified `apps/micmap/CMakeLists.txt` — D-09 (Pattern 2)
- [ ] Modified `driver/CMakeLists.txt` — D-10/D-11 (Pattern 2)
- [ ] Modified root `CMakeLists.txt` — `include(cmake/AssertNoOpenVRInCore.cmake)` per D-04
- [ ] Negative-test recipe documented in plan: temporarily poison `micmap_audio` with OpenVR link, confirm guard FATAL_ERRORs, revert. (Single-shot dev sanity check, not committed.)

*No test framework install needed; existing CTest infrastructure suffices.*

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — (Phase 5 changes no IPC surface, no auth) |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | no | Phase 5 introduces no new inputs. The existing v1.5 `POST /button` JSON parse path is unchanged in P5 (D-15 defers cpp-httplib bump including CVE-2025-46728). |
| V6 Cryptography | no | — |

### Known Threat Patterns for CMake Build-System Refactor

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Symbol bleed across DLL boundary (driver re-exports shared-lib symbols) | Tampering / Information disclosure | `PRIVATE` linkage only (D-11); `dumpbin /exports` SC3 check (D-03); no `__declspec(dllexport)` in shared TUs (D-13) |
| Unintended OpenVR linkage → headless harness depends on a runtime-only API | Denial of service (build break) | Configure-time CMake walk (D-01); source-grep lint (D-02); SC1 explicit headless build matrix |
| Compile-time host-switching macros enabling driver-only code paths in `mic_test` | Tampering (silent behavior drift) | `MICMAP_DRIVER_BUILD` grep assertion (D-12 + SC4) |
| CVE-2025-46728 in cpp-httplib v0.14.3 | Information disclosure (HTTP response splitting) | **Deferred to Phase 8** (D-15) — Phase 5 is build-only, does not enlarge IPC attack surface |

**Note:** This phase is build-system metadata only. The shared-layer guards are themselves the security control — they prevent later phases from violating the architectural boundary that protects the no-symbol-leak invariant.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Single `micmap_lib` INTERFACE aggregating all 5 sub-libs (incl. `micmap_steamvr`) | `micmap_core_runtime` INTERFACE aggregating only the 4 OpenVR-free sub-libs; `micmap_steamvr` consumed directly by binaries that need it | This phase (P5) | Headless harness can build without OpenVR; downstream phases can move detection into the driver without violating the boundary |
| Per-binary handcrafted link lists (3-lib enum in `mic_test`, individual sub-libs everywhere) | Single `micmap::core_runtime` reference per consumer | This phase | Reduces drift; future sub-lib additions to the runtime require no consumer edits |
| Implicit "no OpenVR in shared layer" by convention | Configure-time CMake walk + source-grep lint as build-fail gates | This phase | Convention becomes enforcement; protects every downstream phase mechanically |

**Deprecated/outdated:** None. v1.5 stack is locked.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The `_assert_no_openvr_recurse` skeleton handles all CMake versions ≥ 3.20 correctly, including the ALIASED_TARGET resolution and generator-expression skip. | Pattern 3 | LOW — pattern is widely used; if the project's MSVC + CMake combination surfaces an edge case, the negative-test recipe (deliberate poison link) catches it before merge. Pitfall 5-A documents the alias-resolution risk explicitly. | [ASSUMED — verified via cmake.org docs but not exercised in this codebase] |
| A2 | CMake `INTERFACE` libraries can carry `target_compile_features(... INTERFACE cxx_std_17)` and propagate it to consumers without producing an archive. | Pattern 1 | NONE — well-documented CMake behavior; mirrors how STATIC libs propagate PUBLIC features. [CITED: cmake.org `target_compile_features`] |
| A3 | Removing `micmap_lib` and `micmap::lib` requires no edits beyond `apps/micmap/CMakeLists.txt:42` (single in-tree consumer). Verified via `grep`: no other file references the old name. | D-07 | LOW — the planner should re-grep at plan time to confirm no new consumers landed. Recipe in Example 2. | [VERIFIED: 2026-05-01 grep over apps/, driver/, tests/, src/ shows only `apps/micmap/CMakeLists.txt:42` references `micmap_lib`] |

**If this table is empty:** N/A — three assumptions documented above.

## Open Questions

1. **Should the source-grep lint live in CMake/CTest (`add_test` form) or in a standalone CI script?**
   - What we know: CONTEXT marks this as Claude's discretion. Both forms are functionally equivalent.
   - What's unclear: Project doesn't yet have an explicit CI runner config (no `.github/workflows/`, no `azure-pipelines.yml` visible at the repo root); `MICMAP_BUILD_TESTS=ON` and `enable_testing()` exist, so CTest is the established hook.
   - Recommendation: **Use the CTest form** (`cmake/lint_no_openvr_in_core.cmake` driven by `add_test`). Reasons: (a) CTest is already wired; (b) every `cmake --build && ctest` run picks up the lint without external tooling; (c) co-locates with the configure-time guard (`cmake/` directory). This is the planner's call.

2. **Does the planner add a "no driver TU includes runtime headers" lint as a Wave 0 deliverable, or rely on code review?**
   - What we know: D-10 forbids it; SC5 demands byte-identical behavior. Driver includes only what `driver/src/*.cpp` already includes today.
   - What's unclear: Whether automated enforcement is worth the marginal CI complexity for a one-phase invariant. Once Phase 6/7 starts using runtime headers, the lint must be retired anyway.
   - Recommendation: **Defer the lint; rely on plan-checker + code review.** Belt-and-braces but P5 changeset is small (≤6 files modified) and the invariant is temporary. Planner may include it if the team prefers automated guard. Suggested grep: `! grep -rEn '#include\s*[<"]micmap/(audio|detection|core|common)/' driver/src/`.

3. **Does `nlohmann_json` propagate transitively from `micmap_core` (PRIVATE link at `src/core/CMakeLists.txt:19`) far enough to cause Pitfall 15 bloat in the headless `mic_test.exe`?**
   - What we know: `micmap_core` links `nlohmann_json PRIVATE`, so the headers don't propagate to consumers, only the linker does (and only if `micmap_core` itself uses the symbols). `mic_test.exe` already pulls `micmap_core` today via `apps/mic_test/CMakeLists.txt:8-13`; Phase 5 doesn't change this.
   - What's unclear: Nothing. P5 changes nothing here.
   - Recommendation: **No action.** Pitfall 15 mitigation belongs to Phase 8 (D-16). Document in plan that P5 binary sizes will be unchanged for `mic_test.exe` (modulo build-system metadata) — a quick before/after `dir build/bin/Release/mic_test.exe` is sufficient verification.

## Sources

### Primary (HIGH confidence — in-tree, verified 2026-05-01)
- `CMakeLists.txt` (root) lines 5, 22-24, 69-74, 76-103 — build options, OpenVR find, subdirectory order
- `src/CMakeLists.txt` lines 1-21 — current `micmap_lib` aggregation
- `src/audio/CMakeLists.txt` lines 1-41 — sub-lib pattern; PUBLIC includes; PUBLIC `cxx_std_17`; ALIAS
- `src/detection/CMakeLists.txt` lines 1-26 — same shape; PRIVATE `kissfft`
- `src/core/CMakeLists.txt` lines 1-30 — same shape; PRIVATE `nlohmann_json` + `shell32`
- `src/common/CMakeLists.txt` lines 1-18 — same shape; no third-party deps
- `src/steamvr/CMakeLists.txt` lines 12-67 — confirms PUBLIC OpenVR linkage (this lib is the *boundary*; deliberately excluded from runtime aggregation per D-05)
- `src/bindings/CMakeLists.txt` lines 1-41 — Phase 4 D-10 precedent (STATIC + ALIAS + injected sink)
- `apps/mic_test/CMakeLists.txt` lines 1-25 — current 3-lib link list (D-08 target)
- `apps/micmap/CMakeLists.txt` lines 39-44 — current `micmap_lib` consumer (D-09 target)
- `apps/hmd_button_test/CMakeLists.txt` lines 1-24 — confirms direct `micmap_steamvr` link (unchanged by P5)
- `driver/CMakeLists.txt` lines 23-74 — driver target topology, current PRIVATE deps, `micmap::bindings` link (D-10 target)
- `driver/src/driver_main.cpp` lines 26-41 — only `__declspec(dllexport)` in tree (`HmdDriverFactory`)
- `cmake/FindOpenVR.cmake` lines 1-74 — confirms `OpenVR::openvr_api` only created on `OpenVR_FOUND`
- `tests/CMakeLists.txt` lines 1-110 — confirms test targets link narrowest sub-libs directly (Pitfall 5-E rationale)
- `.planning/research/PITFALLS.md` §6, §9, §15 — D-12/D-13/D-14 rationale
- `.planning/research/SUMMARY.md` §"Phase 1: Shared Library Extraction" — research-derived rationale
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` — locked decisions D-01..D-16 (verbatim above)

### Secondary (HIGH confidence — official docs)
- https://cmake.org/cmake/help/latest/command/add_library.html — `INTERFACE` library semantics
- https://cmake.org/cmake/help/latest/command/get_target_property.html — property introspection including `ALIASED_TARGET`
- https://cmake.org/cmake/help/latest/command/target_link_libraries.html — `PRIVATE`/`PUBLIC`/`INTERFACE` propagation rules

### Tertiary (LOW confidence)
- None. Phase 5 is fully grounded in in-tree precedent + first-party CMake docs.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — CMake 3.20+ INTERFACE pattern is documented, vendored, and exercised in-tree at `src/CMakeLists.txt:11-21` (existing `micmap_lib` is the same shape with one wider link list).
- Architecture (target topology): HIGH — every link edge verified by reading the CMakeLists.txt files involved; current state is well-formed and the proposed change is a narrow re-aggregation, not a restructure.
- Pitfalls: HIGH — five P5-specific failure modes (5-A through 5-E) anchored in CMake property semantics, in-tree file evidence, and v1.5 PITFALLS catalog. No new pitfalls discovered during research.
- Validation Architecture: HIGH — every success criterion has a concrete pass condition and at least one automatable check; SC5 is correctly classified as manual-only per CLAUDE.md visual-validation rule.

**Research date:** 2026-05-01
**Valid until:** 2026-05-31 (30 days — stack locked, no fast-moving deps in scope)

## RESEARCH COMPLETE

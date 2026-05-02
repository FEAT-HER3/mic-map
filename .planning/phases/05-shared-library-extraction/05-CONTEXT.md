# Phase 5: Shared Library Extraction - Context

**Gathered:** 2026-05-01
**Status:** Ready for planning

<domain>
## Phase Boundary

Add a new CMake INTERFACE target `micmap_core_runtime` that aggregates the four existing static libraries (`micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`). The driver DLL (`driver_micmap.dll`), the desktop client (`micmap.exe`), and the headless harness (`mic_test.exe`) all consume the runtime through this single target. A configure-time CMake guard plus a source-grep lint reject any OpenVR symbol from leaking into the shared layer. Phase 5 is a structural refactor — no source movement, no behavior change versus shipped v1.5.

In scope: CMake plumbing (new INTERFACE target, alias, guard module), consumer relinking (driver/client/mic_test), replacement of the existing `micmap_lib` INTERFACE target, headless invariant proof (`-DMICMAP_BUILD_DRIVER=OFF` build with OpenVR SDK absent), v1.5 UAT regression pass.

Out of scope: any consumption of runtime headers from driver translation units (driver links it but does not yet use it — SC5), logger sink wiring (LIB-04 → Phase 8), nlohmann/json centralization (Pitfall 15 hedge → Phase 8), cpp-httplib CVE bump (→ Phase 8), training/IPC/detection migration (P6+).

</domain>

<decisions>
## Implementation Decisions

### CI guard (LIB-03)
- **D-01:** Primary guard is a configure-time CMake module `cmake/AssertNoOpenVRInCore.cmake` that recursively walks `INTERFACE_LINK_LIBRARIES` and `LINK_LIBRARIES` of `micmap_core_runtime` and every transitive dependency, failing CMake configure if any node resolves to `openvr_api` / `OpenVR::openvr_api` or if any propagated `INTERFACE_INCLUDE_DIRECTORIES` entry matches `*/openvr*`. Configure-time fail beats build-time — every developer sees it on first cmake run, not just CI.
- **D-02:** Secondary guard is a source-grep CI step that fails when `<openvr.h>`, `<openvr_driver.h>`, or `vr::` namespace appears under `src/audio/`, `src/detection/`, `src/core/`, or `src/common/`. Catches violations the linker walk can't see (header-only dependencies, stray includes).
- **D-03:** Existing v1.5 `dumpbin /exports driver_micmap.dll` (must show only `HmdDriverFactory`) stays as tertiary defense on the driver side — it is *not* the shared-layer guard, but it remains a hard CI gate to detect leaks introduced by anything below the runtime.
- **D-04:** Root `CMakeLists.txt` calls `include(cmake/AssertNoOpenVRInCore.cmake)` after `add_subdirectory(src)` and before `add_subdirectory(driver)`. Ordering matters — guard must see all four sub-lib targets defined before it walks them.

### `micmap_lib` disposition
- **D-05:** Replace `micmap_lib` (currently in `src/CMakeLists.txt`, aggregates all 5 libs including `micmap_steamvr`) with a new `micmap_core_runtime` INTERFACE target that explicitly drops `micmap_steamvr`. The OpenVR-touching layer is the boundary, not part of the shared runtime.
- **D-06:** Provide `micmap::core_runtime` ALIAS for namespace-style consumption, mirroring the `micmap::core` / `micmap::bindings` precedent.
- **D-07:** Delete `micmap_lib` entirely once consumers are switched. No deprecation period — single source of truth, no parallel paths to drift.

### Consumer linkage
- **D-08:** `apps/mic_test/CMakeLists.txt` replaces its 3-lib link list (`micmap_audio`, `micmap_detection`, `micmap_common`) with single `PRIVATE micmap::core_runtime`. This is the SC1 acceptance probe — `mic_test.exe` must build with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK absent.
- **D-09:** `apps/micmap/CMakeLists.txt` links `micmap::core_runtime` for the shared portion and continues to link `micmap_steamvr` directly for `IDriverClient` / HTTP-bridge plumbing.
- **D-10:** `driver/CMakeLists.txt` adds `PRIVATE micmap::core_runtime`. **No driver TU may `#include` any runtime header in this phase** — link-only, byte-for-byte identical behavior to v1.5 (SC5).
- **D-11:** `target_link_libraries(driver_micmap PUBLIC ...)` against the runtime is forbidden (Pitfall 6 — symbol re-export across DLL boundary). PRIVATE only.

### Macro and ODR hygiene (locked in this phase)
- **D-12:** Zero `#ifdef MICMAP_DRIVER_BUILD` (or any other host-switching macro) inside `src/{audio,detection,core,common}/`. Confirmed zero hits today (2026-05-01); P5 locks the invariant as a CI grep assertion: `grep -rn 'MICMAP_DRIVER_BUILD' src/audio/ src/detection/ src/core/ src/common/` returns zero.
- **D-13:** No `__declspec(dllexport)` / `__declspec(dllimport)` annotations anywhere in the shared layer (Pitfall 6). The shared lib is internal to each consumer.

### Header layout
- **D-14:** Pure transitive INTERFACE propagation — no umbrella header, no new include directories. The four sub-libs already publish their headers via `target_include_directories(... PUBLIC $<BUILD_INTERFACE:...>)`; the new INTERFACE target re-exports them automatically. Consumers continue including `<micmap/audio/...>`, `<micmap/detection/...>`, etc. Revisit only if Phase 7 finds the lack of facade hurts driver consumption.

### cpp-httplib CVE bump
- **D-15:** v0.14.3 → v0.20.1 (CVE-2025-46728) is **deferred to Phase 8 (IPC Reshape)**. Reasons: (a) Phase 5 SC5 demands byte-identical v1.5 behavior — a wire-format-adjacent dep bump pollutes UAT correlation; (b) Phase 8 reshapes the IPC surface heavily and is the natural home for the bump; (c) STATE.md flagged this as a candidate, not a commitment. Backlog reassignment from "P5 standalone plan" → "P8 prerequisite plan".

### Pitfall 15 (json bloat) hedge
- **D-16:** No nlohmann/json serializer centralization in P5. The Pitfall 15 mitigation (single-TU `from_json`/`to_json` definitions in shared lib) is a behavior-changing optimization that violates the byte-identical exit criterion. Defer to Phase 8 IPC reshape, where the new `PUT /settings` / `GET /state` JSON shapes land anyway.

### Claude's Discretion
- Exact wording / layout of the configure-time guard module (helper macros, error message format)
- Phase 4 D-10 alias style (`micmap::bindings`) vs `micmap::core_runtime` namespace formatting — mirror the v1.5 convention exactly
- Whether the source-grep CI step lives in a new `cmake/lint_no_openvr_in_core.cmake` module driven by `add_test` or in a dedicated CI job — implementation detail, either is acceptable as long as it runs on every PR

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 5: Shared Library Extraction" — goal, dependencies, success criteria 1–5, research flag (STANDARD)
- `.planning/REQUIREMENTS.md` §"Shared Library (LIB)" — LIB-01, LIB-02, LIB-03 (LIB-04 belongs to Phase 8)
- `.planning/PROJECT.md` §"Current Milestone: v1.6 Feature Migration" — milestone framing, locked stack constraints

### Pitfall mitigations Phase 5 owns
- `.planning/research/PITFALLS.md` §"Pitfall 6: Static lib linked into both DLL (driver) and EXE (client) — symbol-visibility ODR violations" — hard rules: STATIC sub-libs, no dllexport, no static globals, sink injection, dumpbin assertion
- `.planning/research/PITFALLS.md` §"Pitfall 9: `mic_test.exe` (headless harness) drifts away from driver's reality" — establishes the no-`MICMAP_DRIVER_BUILD` rule and headless canary expectation
- `.planning/research/PITFALLS.md` §"Pitfall 15: Symbol bloat from `nlohmann/json` in three binaries" — explicitly deferred (D-16) but read for context on why P5 does not centralize serializers

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 1: Shared Library Extraction" — research-derived rationale for INTERFACE-target approach, dep root status
- `.planning/research/ARCHITECTURE.md` — driver vs client decomposition target state
- `.planning/codebase/STRUCTURE.md` — current lib/binary layout (read to understand what's being aggregated)
- `.planning/codebase/STACK.md` — locked stack (CMake 3.20+, C++17, no new deps in P5)
- `.planning/codebase/CONVENTIONS.md` — alias naming style, target hygiene precedent

### In-tree precedents
- `src/bindings/CMakeLists.txt` — Phase 4 D-10 lift; the canonical "shared static lib with injected sink, no driver-only symbols" pattern. P5's INTERFACE target reuses the alias / `cxx_std_17` / `BUILD_INTERFACE`-include shape.
- `src/CMakeLists.txt` — current `micmap_lib` INTERFACE target (to be replaced per D-05/D-07)
- `CMakeLists.txt` (root) — `option(MICMAP_BUILD_DRIVER)` and `find_package(OpenVR QUIET)` already gate driver build; SC1 headless invariant relies on these continuing to work

### v1.5 invariant carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. Phase 5 does not touch this invariant; downstream phases must preserve it.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/bindings/CMakeLists.txt` — `micmap_bindings` STATIC lib + `micmap::bindings` ALIAS + injected-sink pattern. Direct template for the new runtime target's CMake idioms (`cxx_std_17` PUBLIC, `BUILD_INTERFACE`/`INSTALL_INTERFACE` include generators).
- `src/CMakeLists.txt:11-19` — existing `micmap_lib` INTERFACE aggregation pattern. Same shape, narrower aggregation list (drop `micmap_steamvr`).
- `src/audio/CMakeLists.txt`, `src/detection/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/common/CMakeLists.txt` — already STATIC, already publish includes via `target_include_directories(... PUBLIC $<BUILD_INTERFACE:...>)`. No internal changes required for P5.

### Established Patterns
- Alias naming: `micmap::<short>` (e.g., `micmap::core`, `micmap::bindings`) — extends to `micmap::core_runtime`.
- C++ standard: every lib calls `target_compile_features(... PUBLIC cxx_std_17)` — runtime target should propagate the same.
- Public-include layout: each lib publishes `include/` via generator-expression include dirs. INTERFACE target inherits these transitively.
- `find_package(OpenVR QUIET)` + `option(MICMAP_BUILD_DRIVER)` already give the headless-build escape hatch SC1 needs.

### Integration Points
- Root `CMakeLists.txt:78` (`add_subdirectory(src)`) — runtime target appears here; guard `include()` lands immediately after.
- Root `CMakeLists.txt:96-102` (driver gating) — already conditional on `OpenVR_FOUND`; runtime target must be defined unconditionally so `mic_test` can link it without OpenVR.
- `apps/mic_test/CMakeLists.txt:8-13` — single source of truth for SC1 acceptance probe; replaces 3 individual libs with `micmap::core_runtime`.
- `apps/micmap/CMakeLists.txt` (not yet read in detail by planner) — must keep `micmap_steamvr` as a direct link alongside `micmap::core_runtime`.
- `driver/CMakeLists.txt` (not yet read in detail) — adds `PRIVATE micmap::core_runtime`; no source-level changes (SC5 byte-identical).

</code_context>

<specifics>
## Specific Ideas

- "P5 is the keystone refactor" (SUMMARY) — every other v1.6 phase requires the boundary to exist cleanly. Treat the configure-time guard and the `MICMAP_DRIVER_BUILD` grep assertion as load-bearing — they are the contract that protects every downstream phase from Pitfall 6 / 9 / 15 regressions.
- Mirror the `micmap_bindings` shape exactly. v1.5 Phase 4 D-10 lift is the in-tree template: STATIC lib + ALIAS + injected sink + no driver-only symbols. The runtime aggregation extends this by being INTERFACE-only (no compile units).
- Headless build verification is non-negotiable — clean checkout, `cmake -B build -DMICMAP_BUILD_DRIVER=OFF`, build target `mic_test` succeeds even when OpenVR SDK is not on disk. This is the primary SC1 acceptance probe.

</specifics>

<deferred>
## Deferred Ideas

- **cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728)** — moved from "candidate P5 standalone plan" (per STATE.md) to **Phase 8 (IPC Reshape) prerequisite plan**. Wire-format risk is non-zero; bundle with the IPC surface change so UAT correlation is clean.
- **nlohmann/json serializer centralization (Pitfall 15)** — single-TU `from_json`/`to_json` definitions in shared lib. Behavior-changing optimization; defer to Phase 8 where the new `PUT /settings` / `GET /state` JSON shapes land. Pitfall 15 risk is "DLL bloat ~600KB", not a correctness issue, so deferral is safe.
- **Logger sink wiring (LIB-04)** — driver `DriverLogSink` + `FileLogSink`, client `StdoutLogSink` + `FileLogSink`. Architecturally compatible with the runtime boundary but not required for P5 link-only proof. **Phase 8** owns it.
- **Umbrella header (`include/micmap_runtime/runtime.hpp`)** — pure transitive propagation suffices for P5 and likely P6/P7. Reconsider if Phase 7 driver-resident detection finds the per-sub-lib include paths painful.
- **Headless canary CMake target** (Pitfall 9 mitigation) — runtime test that exercises full detection pipeline on stub input via `mic_test.exe`. Belongs to **Phase 7** (driver-side detection thread) where the path being canaried actually matures; building it in P5 would test only stubs.

</deferred>

---

*Phase: 05-shared-library-extraction*
*Context gathered: 2026-05-01*

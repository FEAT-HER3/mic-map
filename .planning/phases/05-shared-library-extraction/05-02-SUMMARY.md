---
phase: 05-shared-library-extraction
plan: 02
subsystem: build-system
tags: [cmake, interface-target, alias, driver-link, lib-01, phase-5-wave-2, byte-identical]
requirements: [LIB-01]
dependency_graph:
  requires:
    - "Plan 05-01: AssertNoOpenVRInCore.cmake guard module wired at D-04 splice (must report STATUS clean once micmap_core_runtime exists)"
  provides:
    - "micmap::core_runtime INTERFACE target — the shared-runtime aggregation boundary every downstream v1.6 phase consumes"
    - "Driver-side PRIVATE link of micmap::core_runtime — proves the link-only contract (D-10) and locks the SC3 export surface"
  affects:
    - "Plan 05-03 (parallel Wave 2): apps/micmap relink onto micmap::core_runtime"
    - "Phases 6/7: lift the link-only restriction to start including runtime headers in driver TUs"
tech-stack:
  added: []
  patterns:
    - "INTERFACE library aggregation (transitive PUBLIC includes from sub-libs propagate without an umbrella header — D-14)"
    - "micmap::<short> ALIAS pattern (mirrors src/bindings/CMakeLists.txt:38-40 precedent)"
    - "PRIVATE link discipline on driver consumers (D-11) — preserves SC3 single-export DLL surface"
    - "Link-only contract (D-10) — no driver TU includes shared-runtime headers; linker dead-strips unused symbols, byte-identical to v1.5 baseline"
key-files:
  created: []
  modified:
    - "src/CMakeLists.txt (+19 / -6 lines: micmap_lib block deleted, micmap_core_runtime + cxx_std_17 + ALIAS added with comment header)"
    - "driver/CMakeLists.txt (+11 / -0 lines: single new PRIVATE link line for micmap::core_runtime, comment header pinning D-10/D-11)"
decisions:
  - "Used the OpenVR SDK from sister project bey-closer-t1 (extern/openvr/) via OPENVR_SDK_PATH for the build smoke test — MicMap's worktree has no local OpenVR. FindOpenVR.cmake supports OPENVR_SDK_PATH env var; configure switched from 'OpenVR not found / Skipping driver build' to 'OpenVR support enabled / driver_micmap built'. Workstation-config detail, not a plan deliverable."
metrics:
  duration_seconds: 720
  duration_human: "12m"
  tasks_completed: 2
  files_created: 0
  files_modified: 2
  commits: 2
  completed_date: "2026-05-02T21:37:50Z"
---

# Phase 5 Plan 02: Shared Runtime Aggregation + Driver Link Summary

**One-liner:** Replaced `micmap_lib` with `micmap_core_runtime` INTERFACE target (four OpenVR-free sub-libs, no `micmap_steamvr`) and added the driver-side PRIVATE link of `micmap::core_runtime` — flipping Plan 05-01's AssertNoOpenVRInCore guard from FATAL_ERROR to `STATUS clean (visited 7 targets)` while preserving the SC3 single-export-symbol invariant and producing a driver DLL byte-identical to the v1.5 baseline.

## What Shipped

### `src/CMakeLists.txt` diff summary

Replaced the existing 11-line `micmap_lib INTERFACE` block at lines 11-21 with a 24-line `micmap_core_runtime INTERFACE` block (comment header + target + link list + cxx_std_17 + ALIAS). Net: +19 lines, -6 lines, no source-tree changes, no `add_subdirectory(...)` changes.

Key shape changes vs the deleted `micmap_lib`:

- `micmap_steamvr` is **dropped** from the aggregation list (D-05). The four kept sub-libs are `micmap_core`, `micmap_audio`, `micmap_detection`, `micmap_common`.
- Explicit `target_compile_features(micmap_core_runtime INTERFACE cxx_std_17)` added (the old `micmap_lib` block omitted this; consumers now inherit C++17 transitively).
- `add_library(micmap::core_runtime ALIAS micmap_core_runtime)` added (D-06; mirrors the canonical `micmap::bindings` precedent at src/bindings/CMakeLists.txt:38-40).
- `micmap_lib` and `micmap::lib` are **deleted entirely** (D-07, no parallel paths).

Comment header explicitly names D-05/D-06/D-07 and explains why `micmap_steamvr` is excluded (it links `OpenVR::openvr_api` PUBLIC at src/steamvr/CMakeLists.txt:50 and aggregating it would propagate OpenVR into every consumer of the runtime).

### `driver/CMakeLists.txt` diff summary

Single 11-line insertion (comment header + one `target_link_libraries` line) immediately after the existing Phase 4 `micmap::bindings` PRIVATE link at line 69. Net: +11 lines, -0 lines. No other lines touched — OpenVR / cpp-httplib / nlohmann_json blocks, compile defs, `/WX` warning-as-error, output-directory properties, POST_BUILD copies, and install rules all preserved verbatim.

The new line:

```cmake
target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)
```

Comment header explicitly names D-10 (link-only — no driver TU may `#include` any runtime header in P5) and D-11 (PRIVATE only — PUBLIC would re-export shared-lib symbols and regress SC3).

## CMake Configure: AssertNoOpenVRInCore Guard Status

`cmake -B build -G "Visual Studio 17 2022" -A x64` (with `OPENVR_SDK_PATH` pointing at bey-closer-t1's bundled SDK):

```
-- micmap_steamvr: OpenVR support enabled
-- Found OpenVR DLL: C:/Users/decid/Documents/projects/bey-closer-t1/extern/openvr/bin/win64/openvr_api.dll
-- AssertNoOpenVRInCore: clean (visited 7 targets)
-- driver_micmap: OpenVR support enabled
```

The Wave-1 `FATAL_ERROR: AssertNoOpenVRInCore: micmap_core_runtime is not defined yet` (Plan 05-01 expected state) has flipped to `STATUS: AssertNoOpenVRInCore: clean (visited 7 targets)` exactly as predicted — the guard now successfully recurses through the four sub-libs (`micmap_core`, `micmap_audio`, `micmap_detection`, `micmap_common`) plus `micmap_core_runtime` itself plus the two transitive third-party dependencies (`kissfft`, `nlohmann_json`) reachable through the link graph, and finds zero OpenVR substring matches in target names or `INTERFACE_INCLUDE_DIRECTORIES`. Visited-target count is 7.

## Driver Build + dumpbin /exports (SC3 Verification)

`cmake --build build --target driver_micmap --config Release` succeeds cleanly with `/WX` warnings-as-errors enabled, producing `build/driver/micmap/bin/win64/driver_micmap.dll`.

`dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll`:

```
Section contains the following exports for driver_micmap.dll

    00000000 characteristics
    FFFFFFFF time date stamp
        0.00 version
           1 ordinal base
           1 number of functions
           1 number of names

    ordinal hint RVA      name

          1    0 000016A0 HmdDriverFactory
```

**Exactly one exported symbol: `HmdDriverFactory`.** SC3 invariant unchanged from v1.5 baseline. The `micmap::core_runtime` link did not leak any shared-lib symbols across the DLL boundary — proof that PRIVATE linkage (D-11) does what it says.

## Driver DLL Size Delta (Pitfall 5-D Early Warning)

| Build                                                      | DLL size      | Delta |
| ---------------------------------------------------------- | ------------- | ----- |
| Baseline (driver/CMakeLists.txt without core_runtime link) | 439,296 bytes | —     |
| Phase 5 (driver/CMakeLists.txt WITH core_runtime PRIVATE)  | 439,296 bytes | **0 bytes** |

**0-byte delta.** The two DLLs differ only at PE-header byte offset 265 (the build timestamp), with identical section sizes (.text 0x4D000, .rdata 0x17000, .data 0x4000, .pdata 0x4000, .reloc 0x1000, .rsrc 0x1000). This is the strongest possible form of SC5 byte-identical-behavior: because no driver TU `#include`s any runtime header, the linker dead-strips the entire shared-runtime archive — no code/data is pulled in. Pitfall 5-D's "DLL size delta hints at accidental header include" early-warning indicator is at the floor (0).

Methodology: built the Phase 5 form, copied DLL to `/tmp/driver_phase5.dll`, `git stash`-ed the driver edit (system-reminder confirmed the stash captured the Phase 5 line), rebuilt for baseline, compared with `cmp` and `stat -c%s`, then `git stash pop`-ed to restore the Task 2 edit before commit.

## D-10 Invariant (Link-Only Contract) Verification

`grep -rEn "#include\s*[<\"]micmap/(audio|detection|core|common)/" driver/src/` exits with no matches. Zero driver translation units (`driver/src/driver_main.cpp`, `driver/src/device_provider.cpp`, `driver/src/http_server.cpp`) reference any header from the four shared-runtime sub-libs. The link-only contract is intact and is the structural reason the DLL size delta is 0.

## Commits

| Task | Description                                                | Commit  | Files                  |
| ---- | ---------------------------------------------------------- | ------- | ---------------------- |
| 1    | Replace micmap_lib with micmap_core_runtime (D-05/D-06/D-07) | d9e8ec4 | src/CMakeLists.txt     |
| 2    | Link micmap::core_runtime PRIVATE into driver_micmap (D-10/D-11) | bff2276 | driver/CMakeLists.txt  |

## Deviations from Plan

None. Plan 05-02 executed exactly as written.

No bugs found, no missing critical functionality discovered, no architectural decisions required, no auth gates encountered. The single workstation-config note (using bey-closer-t1's OpenVR SDK via `OPENVR_SDK_PATH` for the build smoke test) is recorded under `decisions:` but is not a deviation from plan content — the plan's verification block just says `cmake -B build && cmake --build build`, which works whether OpenVR is found locally or sourced from a sibling project.

## Threat Flags

None. As anticipated by the plan's `<threat_model>`, both edits are pure CMake metadata changes — no new IPC, no new I/O, no new runtime code paths, no new third-party dependencies. The PRIVATE link verb on `driver_micmap` IS the safety control (prevents shared-lib symbol re-export across the DLL boundary), and the dumpbin verification above is the post-condition proof.

## Self-Check: PASSED

- [x] `src/CMakeLists.txt` — modified (`add_library(micmap_core_runtime INTERFACE)` present; `micmap_lib` and `micmap::lib` absent)
- [x] `driver/CMakeLists.txt` — modified (`target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)` present, exactly 1 occurrence; no PUBLIC variant)
- [x] Commit d9e8ec4 — present in `git log --oneline -5`
- [x] Commit bff2276 — present in `git log --oneline -5`
- [x] AssertNoOpenVRInCore guard reports `STATUS: AssertNoOpenVRInCore: clean (visited 7 targets)` — flipped from Wave-1 FATAL_ERROR
- [x] `cmake --build build --target driver_micmap --config Release` exits 0 — driver DLL built
- [x] `dumpbin /exports` shows exactly one exported symbol: `HmdDriverFactory` — SC3 invariant preserved
- [x] Driver DLL size delta vs baseline: 0 bytes (well within the ≤~5 KB Pitfall 5-D early-warning band)
- [x] `! grep -rEn "#include\s*[<\"]micmap/(audio|detection|core|common)/" driver/src/` exits 0 — D-10 byte-identical contract intact

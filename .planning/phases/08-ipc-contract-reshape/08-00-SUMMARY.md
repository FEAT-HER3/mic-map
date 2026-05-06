---
phase: 08-ipc-contract-reshape
plan: 00
subsystem: build / lints / test-scaffolds / deps
tags: [phase-8, ipc, wave-0, lints, red-tolerant, cpp-httplib-bump]
dependency_graph:
  requires:
    - "tests/CMakeLists.txt P5/P6/P7 EXISTS-gated source-list pattern (lines 197-214)"
    - "cmake/AssertDetectionRunnerNoVrApi.cmake (P7 D-22) — verbatim shape for the 2 narrow-scope lints"
    - "cmake/lint_no_openvr_in_core.cmake (P5 D-02) — verbatim shape for the 2 GLOB_RECURSE lints"
    - "tests/driver/detection_settings_propagation_test.cpp — plain-main + MM_CHECK convention"
    - "external/CMakeLists.txt cpp_httplib FetchContent_Declare block"
  provides:
    - "4 new CMake lint scripts under cmake/"
    - "14 new RED-tolerant test scaffolds under tests/ and tests/driver/"
    - "16 new ctest registrations: 2 lints (AssertHttpServerLocalhostOnly, AssertHttpServerNoVrApi) + 14 test exes"
    - "cpp-httplib bumped v0.14.3 -> v0.20.1 (CVE-2025-46728)"
    - "deferred-items.md documenting pre-existing headless-build openvr.h limitation"
  affects:
    - "tests/CMakeLists.txt — appended ~330 lines of Wave 0 RED scaffold block"
    - "external/CMakeLists.txt — single GIT_TAG line bumped"
tech_stack:
  added: []
  patterns:
    - "Wave 0 RED-tolerant lints (skip-on-NOT-EXISTS)"
    - "Wave 0 RED-tolerant test scaffolds (EXISTS-gated source-list)"
    - "isolated-commit / isolated-revert dependency bump (D-05)"
key_files:
  created:
    - "cmake/AssertNoJsonInCore.cmake"
    - "cmake/AssertHttpServerLocalhostOnly.cmake"
    - "cmake/AssertHttpServerNoVrApi.cmake"
    - "cmake/AssertNoConfigWriteInClient.cmake"
    - "tests/driver/get_state_shape_test.cpp"
    - "tests/driver/get_telemetry_level_test.cpp"
    - "tests/driver/get_devices_cache_test.cpp"
    - "tests/driver/get_settings_shape_test.cpp"
    - "tests/driver/put_settings_round_trip_test.cpp"
    - "tests/driver/put_settings_validation_test.cpp"
    - "tests/driver/put_settings_stress100_test.cpp"
    - "tests/driver/init_config_share_violation_test.cpp"
    - "tests/driver/state_clear_error_test.cpp"
    - "tests/driver/config_io_atomic_persist_test.cpp"
    - "tests/test_multi_sink_logger.cpp"
    - "tests/test_settings_validator.cpp"
    - "tests/test_client_driver_loaded_indicator.cpp"
    - "tests/test_client_level_meter_cadence.cpp"
    - ".planning/phases/08-ipc-contract-reshape/deferred-items.md"
  modified:
    - "tests/CMakeLists.txt"
    - "external/CMakeLists.txt"
decisions:
  - "AssertHttpServerLocalhostOnly scans http_server.{hpp,cpp} as a combined unit for the bind_to_port + 127.0.0.1 paired check (Rule 1 fix during Task 1 verification — per-file scan would have false-positived on the .cpp because the literal lives in the .hpp ctor default)."
  - "AssertNoJsonInCore + AssertNoConfigWriteInClient lint scripts ship at Wave 0 but their ctest registrations are deferred (08-02 / 08-04) per CONTEXT D-29 — they currently fire on existing v1.5 reality (config_manager.cpp uses nlohmann/json; main.cpp:498 calls saveDefault)."
  - "cpp-httplib bump landed as Task 4 of 08-00 per D-05 — single-file isolated commit; isolated revert path preserved; bundling with the rename in 08-01 would conflate any wire-format regression with the rename diff."
  - "Pre-existing headless-build limitation around src/steamvr/ openvr.h unconditional includes documented in deferred-items.md — surfaced during Task 4 smoke but unrelated to D-05."
metrics:
  duration_minutes: 25
  completed_date: "2026-05-05"
  commits: 5
  files_created: 19
  files_modified: 2
  tasks: 4
---

# Phase 8 Plan 00: Wave 0 Prereq Summary

JSON-in-driver, HTTP-localhost, and no-config-write-in-client lints landed under cmake/; 14 RED-tolerant test scaffolds wired into ctest with EXISTS-gated source lists; cpp-httplib bumped v0.14.3 → v0.20.1 as the D-05 isolated commit.

## What was delivered

### 4 CMake source-grep lints (Task 1)

All four scripts under `cmake/` follow the verbatim shape of either `lint_no_openvr_in_core.cmake` (GLOB_RECURSE form, P5 D-02) or `AssertDetectionRunnerNoVrApi.cmake` (explicit-target-list + skip-on-NOT-EXISTS, P7 D-22).

| Script | Form | Scope | Status |
|--------|------|-------|--------|
| `AssertNoJsonInCore.cmake` | GLOB_RECURSE / SRC_ROOTS | `src/audio`, `src/detection`, `src/core`, `src/common` | clean against single-root smoke; ctest registration deferred to 08-02 |
| `AssertHttpServerLocalhostOnly.cmake` | explicit-target / HTTP_SERVER_DIR | `driver/src/http_server.{hpp,cpp}` | clean against existing v1.5 — ctest GREEN |
| `AssertHttpServerNoVrApi.cmake` | explicit-target / HTTP_SERVER_DIR | `driver/src/http_server.{hpp,cpp}` | clean — ctest GREEN |
| `AssertNoConfigWriteInClient.cmake` | GLOB_RECURSE / CLIENT_ROOTS | `apps/micmap`, `src/steamvr` | clean against `src/steamvr` smoke; ctest registration deferred to 08-04 |

### 14 RED-tolerant test scaffolds (Task 2)

All scaffolds follow the plain-main + MM_CHECK convention from `tests/driver/detection_settings_propagation_test.cpp`. Each `#include`s a header that does not yet exist — the build-time compile failure is the Nyquist gate per CONTEXT 08-VALIDATION.md.

| File | Requirement | RED until | Awaits impl |
|------|-------------|-----------|-------------|
| `tests/driver/get_state_shape_test.cpp` | IPC-01 | 08-03 | `driver_state.hpp`, GET /state route, stateGetter ctor param |
| `tests/driver/get_telemetry_level_test.cpp` | IPC-02 | 08-03 | rmsGetter ctor param + GET /telemetry/level handler |
| `tests/driver/get_devices_cache_test.cpp` | IPC-03 / D-17 | 08-03 | `device_info.hpp`, deviceLister ctor param + 1 s cache |
| `tests/driver/get_settings_shape_test.cpp` | IPC-04 | 08-03 | configGetter ctor param + AppConfig to_json/from_json ADL |
| `tests/driver/put_settings_round_trip_test.cpp` | IPC-04 / D-09 | 08-04 | PUT /settings + `config_io.hpp::saveConfigJson` |
| `tests/driver/put_settings_validation_test.cpp` | IPC-04 / D-14 | 08-04 | `settings_validator.hpp::validateSettings` + 400 envelope |
| `tests/driver/put_settings_stress100_test.cpp` | D-28 | 08-04 | PUT route + persist (atomic_store + ReplaceFileW) |
| `tests/driver/init_config_share_violation_test.cpp` | D-10 | 08-02 | `config_io.hpp::loadConfigJson` 3-attempt SHARING_VIOLATION retry |
| `tests/driver/state_clear_error_test.cpp` | HEALTH-05 / D-16 | 08-04 | errorClearer ctor param + POST /state/clear-error |
| `tests/driver/config_io_atomic_persist_test.cpp` | D-14 | 08-02 | saveConfigJson with ReplaceFileW atomic-swap |
| `tests/test_multi_sink_logger.cpp` | LIB-04 / D-19 | 08-01 | `log_sink.hpp::ILogSink` + `multi_sink_logger.cpp` |
| `tests/test_settings_validator.cpp` | D-14 / D-15 | 08-04 | `settings_validator.cpp` (5 case coverage) |
| `tests/test_client_driver_loaded_indicator.cpp` | HEALTH-01 / Pitfall 6 | 08-01 | `driver_api.hpp` (rename) + ConnectResult enum |
| `tests/test_client_level_meter_cadence.cpp` | HEALTH-06 | 08-05 | level-meter polling factory in driver_api.hpp |

### ctest wiring (Task 3)

Appended `# ---- Phase 8 Wave 0 (RED scaffold) ----` block (~330 lines) to `tests/CMakeLists.txt`. Registers:
- 2 lint ctests: `AssertHttpServerLocalhostOnly`, `AssertHttpServerNoVrApi`
- 14 test exe registrations under EXISTS-gated source lists (9 driver tests gated on `OpenVR_FOUND`; 5 non-driver/config-only tests built unconditionally)

`AssertNoJsonInCore` + `AssertNoConfigWriteInClient` ctest registrations deliberately deferred per D-29 (would FATAL on existing v1.5 reality).

### cpp-httplib v0.14.3 → v0.20.1 (Task 4)

Single-file `external/CMakeLists.txt` change. D-05 isolated-commit/isolated-revert guarantee preserved (commit `ba38693` touches ONLY that one file). FetchContent reconfigure verifies the new tag fetches; existing sync tests `test_command_queue`, `test_bindings_patcher`, `bindings_patcher_idempotent` all PASS under v0.20.1.

## Commits

| # | Hash | Message |
|---|------|---------|
| 1 | `44faf68` | feat(08-00): add 4 Phase 8 source-grep lint scripts |
| 2 | `3658784` | test(08-00): add 14 RED-tolerant Phase 8 test scaffolds |
| 3 | `befc170` | build(08-00): wire Phase 8 Wave 0 lints + 14 test scaffolds into ctest |
| 4 | `ba38693` | deps(cpp-httplib): bump v0.14.3 -> v0.20.1 (CVE-2025-46728) per D-05 |
| 5 | `cc5eead` | docs(08): log pre-existing headless-build openvr.h limitation |

## Verification

- `cmake -S . -B build-headless-p8 -DMICMAP_BUILD_DRIVER=OFF` configures cleanly (no FATAL_ERROR).
- `ctest -N --test-dir build-headless-p8` lists all 21 expected tests including the 2 new lints + 5 newly registered Wave 0 test exe targets (the OpenVR-gated 9 are correctly reported as "skipped (OpenVR SDK not found)").
- `ctest -C Debug -R "AssertHttpServer" --test-dir build-headless-p8` → both lints PASS.
- `ctest -C Debug -R "test_command_queue|test_bindings_patcher" --test-dir build-headless-p8` → all PASS under cpp-httplib v0.20.1 (D-06 smoke).
- `build-headless-p8/_deps/cpp_httplib-src/git describe --tags` → `v0.20.1`.
- All 4 lint scripts when invoked with `cmake -P ...` emit `-- <Lint>: clean (N files scanned)` STATUS lines.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] AssertHttpServerLocalhostOnly false-positive on bind_to_port-without-127.0.0.1 split between hpp and cpp**

- **Found during:** Task 1 cmake -P verification
- **Issue:** The plan's spec `(_content MATCHES "bind_to_port" AND NOT _content MATCHES "127\\.0\\.0\\.1")` scans each file in isolation. In the existing tree, `http_server.hpp:66` owns the `"127.0.0.1"` ctor default and `http_server.cpp:196` owns the `bind_to_port` call site, so the per-file check FATALs on the .cpp despite the pair being correct. The plan's `<done>` block claims the lint should be GREEN against existing v1.5 — confirming this was the intended behavior.
- **Fix:** Two-pass strategy. Per-file scan still flags any `0.0.0.0` / `INADDR_ANY` literal (those are unambiguous violations regardless of which file mentions them). The bind_to_port + 127.0.0.1 paired check is hoisted to a combined-content pass that concatenates both files' content. If `bind_to_port` appears anywhere across the pair without a 127.0.0.1 anywhere across the pair, the violation is attributed to the .cpp (call-site burden of proof).
- **Files modified:** `cmake/AssertHttpServerLocalhostOnly.cmake`
- **Commit:** `44faf68` (incorporated into Task 1's atomic commit since this was discovered during initial verification of that task's deliverables)

### Deferred Items (out-of-scope, logged not fixed)

**1. Pre-existing headless-build openvr.h limitation**
- **Found during:** Task 4 cpp-httplib smoke verification
- **Issue:** `src/steamvr/src/{vr_input,vr_input_events,manifest_registrar}.cpp` and `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` `#include <openvr.h>` unconditionally. With `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK absent, building `micmap_steamvr` fails. Same failure exists in `main` repo's prior build-headless directory under cpp-httplib v0.14.3 — confirms NOT a regression from the bump.
- **Status:** Documented in `.planning/phases/08-ipc-contract-reshape/deferred-items.md`. Out of scope for 08-00 per executor scope-boundary rule. Remediation candidate (future plan): wrap `#include <openvr.h>` in `#ifdef MICMAP_HAS_OPENVR` or gate `add_library(micmap_steamvr ...)` on `OpenVR_FOUND`.

## Self-Check

**Files claimed created:**
- cmake/AssertNoJsonInCore.cmake — FOUND
- cmake/AssertHttpServerLocalhostOnly.cmake — FOUND
- cmake/AssertHttpServerNoVrApi.cmake — FOUND
- cmake/AssertNoConfigWriteInClient.cmake — FOUND
- tests/driver/get_state_shape_test.cpp — FOUND
- tests/driver/get_telemetry_level_test.cpp — FOUND
- tests/driver/get_devices_cache_test.cpp — FOUND
- tests/driver/get_settings_shape_test.cpp — FOUND
- tests/driver/put_settings_round_trip_test.cpp — FOUND
- tests/driver/put_settings_validation_test.cpp — FOUND
- tests/driver/put_settings_stress100_test.cpp — FOUND
- tests/driver/init_config_share_violation_test.cpp — FOUND
- tests/driver/state_clear_error_test.cpp — FOUND
- tests/driver/config_io_atomic_persist_test.cpp — FOUND
- tests/test_multi_sink_logger.cpp — FOUND
- tests/test_settings_validator.cpp — FOUND
- tests/test_client_driver_loaded_indicator.cpp — FOUND
- tests/test_client_level_meter_cadence.cpp — FOUND
- .planning/phases/08-ipc-contract-reshape/deferred-items.md — FOUND

**Commits claimed:**
- 44faf68 — FOUND
- 3658784 — FOUND
- befc170 — FOUND
- ba38693 — FOUND
- cc5eead — FOUND

## Self-Check: PASSED

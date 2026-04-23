---
phase: 02-config-read-back
plan: 01
subsystem: config-persistence
tags: [cpp, cmake, ctest, config-persistence, json, test-scaffold, nlohmann-json, wave-0, red]
requirements: [CFG-01, CFG-02, CFG-03, CFG-04, CFG-05]
dependency_graph:
  requires:
    - external/CMakeLists.txt nlohmann_json INTERFACE target (pinned v3.11.2)
    - src/core/include/micmap/core/config_manager.hpp (public IConfigManager + createConfigManager factory)
    - tests/test_placeholder.cpp (CTest-standalone style analog)
  provides:
    - Wave 0 RED test scaffold — five scenarios (T-1..T-5) covering round-trip, corruption, clamp, missing-file, retention
    - micmap_core PRIVATE link to nlohmann_json (compile-time access for Plan 02's implementation)
    - CTest registration `test_config_manager` (the canary that Plan 02 must flip to GREEN)
  affects:
    - .planning/phases/02-config-read-back/02-02-PLAN.md (hand-off: this test is the GREEN target)
    - .planning/phases/02-config-read-back/02-VALIDATION.md (Wave 0 row now actionable)
tech_stack:
  added: []
  patterns:
    - CTest-standalone (`int main()` returning 0/1; no framework)
    - Anonymous-namespace file-local test helper macro (MM_CHECK)
    - std::filesystem::temp_directory_path() isolation
    - Factory-only test surface (never touches ConfigManagerImpl)
key_files:
  created:
    - tests/test_config_manager.cpp
  modified:
    - src/core/CMakeLists.txt
    - tests/CMakeLists.txt
decisions:
  - "Chose Shape 1 (merged PUBLIC+PRIVATE) over Shape 2 for src/core/CMakeLists.txt — diff minimalism."
  - "Generator: Visual Studio 17 2022 (multi-config; use -C Debug on ctest). Plan 02's executor should reuse this generator to keep build/ warm."
  - "RED mode: build succeeds + ctest fails at Test 1 line 56 (round-trip Unicode identity). Linker errors were avoided because nlohmann_json is header-only and isn't yet #include'd by config_manager.cpp — the build passes but the stub save/load discards data, which is the expected Wave 0 failure mode."
metrics:
  duration_minutes: 12
  tasks_completed: 3
  files_created: 1
  files_modified: 2
  commits: 3
  completed_date: "2026-04-23"
commits:
  - hash: fc37791
    message: "build(02-01): link nlohmann_json PRIVATE into micmap_core"
  - hash: 361057c
    message: "test(02-01): add RED test scaffold for config_manager round-trip"
  - hash: d249591
    message: "build(02-01): register test_config_manager in CTest (RED confirmed)"
---

# Phase 02 Plan 01: Test Scaffold + nlohmann_json Wiring Summary

Wave 0 RED test scaffold installed: five CTest scenarios exercise the full CFG-01..CFG-05 surface via the public `createConfigManager()` factory, `nlohmann_json` is now wired PRIVATE into `micmap_core`, and `ctest -R test_config_manager --output-on-failure -C Debug` exits non-zero as required.

## What Was Built

- **`tests/test_config_manager.cpp`** (NEW, 156 lines) — five scenarios T-1..T-5 from RESEARCH §Recipe 10:
  - **T-1 round-trip identity** (CFG-04, CFG-05): sets Unicode `deviceNamePattern = L"Beyond™ Test 🎙"`, UNC `deviceId = L"\\\\?\\Global\\{abc-123}"`, non-default numerics, `customActionBinding = "action:/actions/micmap/in/click"`, `lastTrainedTimestamp = from_time_t(1700000000)`, saves, reloads in a fresh manager, and checks every field identical.
  - **T-2 corruption → backup + defaults** (CFG-02): seeds a trailing-comma JSON, asserts `load()` returns true, asserts `audio.bufferSizeMs == 10` (default from header), asserts a `config.json.corrupted.*` backup appears.
  - **T-3 clamp + pow2-snap** (CFG-03): out-of-range values get clamped to the D-04/D-01/D-02/D-03 bands; `fftSize == 2048 || 4096`.
  - **T-4 missing file is not corruption** (D-16): no config → `load()` true, defaults present, no `config.json.corrupted.*` backup created.
  - **T-5 retention pruning** (D-11): pre-seeds 7 backup files, triggers a new corruption-detect, asserts exactly 5 remain after prune.
  - Isolated to `std::filesystem::temp_directory_path() / "micmap_test_config"`; `fs::remove_all(tmpDir)` at the start of each scoped block. Never calls `loadDefault()` or `saveDefault()`. Uses only the `createConfigManager()` factory (no `ConfigManagerImpl` reference).
- **`src/core/CMakeLists.txt`** — one-block edit: `nlohmann_json` PRIVATE added inside the existing `target_link_libraries(micmap_core ...)` call (Shape 1, `+2/-0` lines). Alias and WIN32 `shell32` block untouched.
- **`tests/CMakeLists.txt`** — new 5-line registration block for `test_config_manager` placed AFTER the existing `test_placeholder` block. Links PRIVATE against `micmap::core` alias. `MICMAP_USE_GTEST` option stays `OFF`.

## Commits

| Hash | Type | Task | Description |
|------|------|------|-------------|
| `fc37791` | build | 1 | Link `nlohmann_json` PRIVATE into `micmap_core` |
| `361057c` | test  | 2 | RED test scaffold, 5 scenarios, factory-only |
| `d249591` | build | 3 | Register `test_config_manager` in CTest; RED confirmed |

## RED Failure Mode Captured (for Plan 02)

Generator: `Visual Studio 17 2022` (multi-config). CMake configure took ~25s (first cold run; includes FetchContent populate of nlohmann_json, KissFFT, cpp-httplib, ImGui).

### Build result

```
cmake --build build --target test_config_manager --config Debug
```

**SUCCEEDS.** All targets compile and link cleanly:

```
  state_machine.cpp
  config_manager.cpp
  micmap_core.vcxproj -> ...\build\lib\Debug\micmap_core.lib
  test_config_manager.cpp
  test_config_manager.vcxproj -> ...\build\bin\Debug\test_config_manager.exe
```

Link success is expected even without `#include <nlohmann/json.hpp>` in `config_manager.cpp` because `nlohmann_json` is an INTERFACE/header-only target — it contributes include directories, not symbols. Plan 02 will add the include and actually use the API.

### Test result

```
ctest --test-dir build -R test_config_manager --output-on-failure -C Debug
```

**FAILS** with exit code `8` (non-zero). First failing assertion:

```
[INFO] Saved config to: ...\Temp\micmap_test_config\config.json
[INFO] Loaded config from: ...\Temp\micmap_test_config\config.json
FAIL: c2.audio.deviceNamePattern == c.audio.deviceNamePattern at line 56

0% tests passed, 1 tests failed out of 1
The following tests FAILED:
	  2 - test_config_manager (Failed)
```

**Interpretation:** The current stub `save()` + `load()` in `src/core/src/config_manager.cpp` does not actually persist / reconstitute data — after a save-then-load round trip the reloaded `AppConfig` still holds defaults (or some subset), so T-1's first cross-field assertion trips. This matches PATTERNS.md Pattern D which calls out that Plan 02 replaces the two method bodies in-place.

**Canary check:** `test_placeholder` still passes (1/1 green). Pre-existing test infrastructure is unaffected.

## Hand-off to Plan 02

Plan 02's executor should consume this as the GREEN target. Pointers:

- **GREEN criterion:** `ctest --test-dir build -R test_config_manager --output-on-failure -C Debug` exits 0 with stdout containing `all tests passed`.
- **Feedback latency:** warm incremental rebuild + test is < 5s (VALIDATION.md budget); `build/` tree is already configured with Visual Studio 17 2022.
- **Implementation recipes:** RESEARCH §Recipes 1-9 cover defensive parse, UTF-8 converters, clamp/snap, corruption backup + rotation, atomic Windows save. Recipe 10 is already consumed by the test file.
- **Patterns already in force:** anonymous-namespace helpers (PATTERNS.md Pattern A), include ordering (Pattern B), `#ifdef _WIN32`/fallback (Shared Pattern), `MICMAP_LOG_*` fold-expression logging (Pattern E), return-code / `std::error_code` style (Pattern F), interface+factory stability (Pattern H — header and factory are stable).
- **Warning-4 guard:** Task 1 of Plan 02 should stub `save()` to `return false` while the old `JsonValue`/`toJson` scaffolding comes out — prevents an interrupted-state partial write from shipping.

## Deviations from Plan

None — plan executed exactly as written. All verify-automated greps passed first-try, the RED mode was the expected "build-green, test-red" variant (acceptable per Plan 01 Task 3 action block), no Rule 1-4 deviations triggered.

## Verification Results

| # | Check | Result |
|---|-------|--------|
| 1 | `grep -n "nlohmann_json" src/core/CMakeLists.txt` shows bare target | PASS (line 19, PRIVATE merge) |
| 2 | `grep -n "add_test(NAME test_config_manager" tests/CMakeLists.txt` | PASS (1 match, after placeholder) |
| 3 | `grep -c "MM_CHECK" tests/test_config_manager.cpp` ≥ 25 | PASS (29 — 1 macro def + 28 usages) |
| 4 | `cmake --build build --target test_config_manager` succeeds OR ctest non-zero | PASS (build green, ctest=8) |
| 5 | `grep -n "ConfigManagerImpl" tests/test_config_manager.cpp` empty | PASS (0 matches — factory only) |
| 6 | `grep -n "MICMAP_USE_GTEST" tests/CMakeLists.txt \| grep "OFF"` | PASS (line 8 OFF) |
| 7 | `test_placeholder` canary passes | PASS (1/1 green) |
| 8 | No changes to `src/core/src/config_manager.cpp` | PASS (git diff --name-only shows only the 3 declared files) |
| 9 | `grep -n "Beyond" tests/test_config_manager.cpp` (Unicode fixture) | PASS (literal `Beyond™ Test 🎙` in file at line 38) |
| 10 | `grep -n "1700000000" tests/test_config_manager.cpp` | PASS (line 49, `from_time_t` fixture) |

## Self-Check: PASSED

- **Files exist:** `tests/test_config_manager.cpp`, `src/core/CMakeLists.txt` (modified), `tests/CMakeLists.txt` (modified) — all present.
- **Commits exist:** `fc37791`, `361057c`, `d249591` — all in `git log`.
- **Forbidden file untouched:** `src/core/src/config_manager.cpp` not in diff-name-only output.
- **RED status:** `ctest -R test_config_manager -C Debug` exits 8 (non-zero) with `FAIL:` line in output.
- **Placeholder canary:** still green (1/1).

```
$ git log --oneline 9276acc..HEAD
d249591 build(02-01): register test_config_manager in CTest (RED confirmed)
361057c test(02-01): add RED test scaffold for config_manager round-trip
fc37791 build(02-01): link nlohmann_json PRIVATE into micmap_core

$ git diff --name-only 9276acc..HEAD
src/core/CMakeLists.txt
tests/CMakeLists.txt
tests/test_config_manager.cpp
```

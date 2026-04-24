---
phase: 03-auto-start
plan: 03
subsystem: core/config
tags: [config, persistence, json, tdd, q4-resolution]
requires:
  - "Phase 2 defensive reader (readBool helper, readSubObject pattern)"
  - "Phase 2 atomic writer (appConfigToJson, writeAtomicWindows)"
provides:
  - "AppConfig::shownTrayNotification — persisted bool flag for first-launch tray balloon (Plan 06 dependency)"
  - "Empirical proof: Phase 2 writer requires explicit per-field wiring (does not auto-serialize struct members)"
affects:
  - "src/core/include/micmap/core/config_manager.hpp"
  - "src/core/src/config_manager.cpp"
  - "tests/test_config_manager.cpp"
  - ".planning/phases/03-auto-start/03-VALIDATION.md (Q4 checked off)"
tech-stack:
  added: []
  patterns:
    - "Defensive reader (readBool with default fallback) — extends Phase 2's pattern to a top-level field"
    - "TDD RED-then-GREEN gates: test first, then minimal wiring"
key-files:
  created: []
  modified:
    - "src/core/include/micmap/core/config_manager.hpp (1 line added — field declaration)"
    - "src/core/src/config_manager.cpp (2 lines added — read + write wiring)"
    - "tests/test_config_manager.cpp (74 lines added — 5 round-trip cases)"
    - ".planning/phases/03-auto-start/03-VALIDATION.md (Q4 checkbox flipped)"
decisions:
  - "Field placed top-level on AppConfig (not nested under a sub-struct) per D-09 — single bool, no need for a 'tray' sub-struct"
  - "Reused existing readBool helper (no new helper required) — graceful degrade on missing/wrong-type, identical semantics to Phase 2 fields"
  - "Default value = false (off-by-default; flipped to true after balloon fires once)"
metrics:
  duration_seconds: 148
  completed_date: "2026-04-23"
  tasks_completed: 1
  files_changed: 4
  commits: 2
---

# Phase 03 Plan 03: shownTrayNotification Field Summary

Added `AppConfig::shownTrayNotification` bool (default `false`), wired through the Phase 2 defensive reader + writer, and added a 5-case round-trip test that empirically closes RESEARCH Open Question #4 GREEN.

## What Changed

### Field Declaration (`config_manager.hpp:60`)
```cpp
struct AppConfig {
    int version = 1;
    AudioConfig audio;
    DetectionConfig detection;
    SteamVRConfig steamvr;
    TrainingConfig training;
    bool shownTrayNotification = false; ///< Set once after first silent-launch tray balloon fires (Phase 3 D-09)
};
```

### Writer (`config_manager.cpp:254`)
```cpp
j["training"]  = trainingToJson(c.training);
j["shownTrayNotification"] = c.shownTrayNotification;  // <-- new
return j;
```

### Reader (`config_manager.cpp:408`)
```cpp
readTraining(j, config_.training);
config_.shownTrayNotification = readBool(j, "shownTrayNotification", false);  // <-- new
```

## Q4 Resolution Evidence

**RESEARCH Open Question #4:** *Does the Phase 2 JSON writer serialize newly-added struct fields automatically, or does it require explicit wiring?*

**Answer (empirical, GREEN test):** **It does NOT auto-serialize.** Explicit wiring required.

**How proven:**

1. **RED phase (commit `80301b1`):** Added the `bool shownTrayNotification` field to the struct AND added 5 round-trip test cases. **Did not touch `appConfigToJson` or the load path.** Built `test_config_manager` and ran ctest:
   ```
   [shownTrayNotification case 1] default value is false      <- PASSED (default ctor)
   [shownTrayNotification case 2] write true; reload; reads true
   FAIL: mgr2->getConfig().shownTrayNotification == true at line 171
   ```
   Case 2 failed because the writer simply did not emit the new field — `nlohmann::json` has no struct introspection; without an explicit assignment in `appConfigToJson`, the field was silently dropped on save.

2. **GREEN phase (commit `84bd114`):** Added two literal lines:
   - `j["shownTrayNotification"] = c.shownTrayNotification;` in `appConfigToJson`
   - `config_.shownTrayNotification = readBool(j, "shownTrayNotification", false);` in `load()`

   Re-ran `ctest --test-dir build -C Release -R test_config_manager`:
   ```
   1/1 Test #2: test_config_manager ..............   Passed    0.06 sec
   100% tests passed, 0 tests failed out of 1
   ```

**Implication for future plans (Plan 06 + beyond):** Any new `AppConfig` field requires three locations to be edited:
1. Declaration in `config_manager.hpp`
2. Read line in `ConfigManagerImpl::load()` (using the appropriate `readBool`/`readInt`/`readFloat`/`readString`/`readWString` helper)
3. Write line in `appConfigToJson()`

This is now codified by VALIDATION.md (Q4 checkbox closed) and will be referenced by Plan 03-06 when wiring the balloon-fire logic.

## Test Coverage Added (5 cases)

| Case | Scenario | Expected | Result |
|------|----------|----------|--------|
| 1 | Default-constructed AppConfig | `shownTrayNotification == false` | GREEN |
| 2 | Set true → save → reload | `== true` (round-trip identity) | GREEN |
| 3 | Otherwise-valid config missing the key | `== false` (defensive default) | GREEN |
| 4 | Wrong type (`"shownTrayNotification": "yes"`) | `== false` (readBool fallback) | GREEN |
| 5 | Seeded `"shownTrayNotification": true` | `== true` (read path works) | GREEN |

All cases use the existing test harness pattern (`fs::temp_directory_path() / "micmap_test_config"`, `fs::remove_all` between cases, `MM_CHECK` macro). User's real `%APPDATA%\MicMap\config.json` was never touched.

## Decisions Made

- **Top-level field, not a sub-struct:** Per D-09 in CONTEXT.md. A single bool doesn't justify a `TrayConfig` sub-struct. If future plans add more tray-related state (e.g., `lastTrayShownAt` timestamp, `trayBalloonCount`), revisit by introducing a `TrayConfig` struct and migrating the field.
- **Reused `readBool` helper:** Phase 2 already established the defensive-default pattern. Using a fresh helper would have introduced inconsistency with no semantic benefit.
- **Default `false`:** The flag starts cleared so the balloon fires on first launch. Plan 06 will flip it to true after the balloon shows, and `saveDefault()` to persist.

## Deviations from Plan

None — plan executed exactly as written. The TDD sequence (RED commit → GREEN commit) matched the plan's `tdd="true"` annotation precisely, and the empirical Q4 result matched the planner's prediction (writer requires explicit wiring).

## Surprises

**None.** The build succeeded on first try in both phases — no namespace-visibility issues with `readBool` (already proven in `load()` for `version`), no JSON serialization quirks, no test-harness flakiness. The plan's PATTERNS.md guidance was directly applicable.

## Commits

| Phase | Hash | Type | Message |
|-------|------|------|---------|
| RED | `80301b1` | test | add failing round-trip test for shownTrayNotification |
| GREEN | `84bd114` | feat | wire shownTrayNotification through config reader/writer |

## Verification

- `cmake --build build --config Release --target test_config_manager` — clean build, no warnings
- `ctest --test-dir build -C Release -R test_config_manager --output-on-failure` — 1/1 passed
- `cmake --build build --config Release --target micmap_core` — clean build, no warnings
- `grep -n "shownTrayNotification" src/core/include/micmap/core/config_manager.hpp` — 1 line (struct member)
- `grep -cn "shownTrayNotification" src/core/src/config_manager.cpp` — 2 lines (read + write)
- Phase 2 regression: CFG-01 through CFG-05 still GREEN within the same `test_config_manager` binary

**Pre-existing non-failures (out of scope):** `test_cli_flags_parse`, `test_manifest_registrar`, `test_vr_input_quit_ordering`, `test_tray_balloon_once` — these are RED-scaffold tests added by Plan 03-01 for future plans (03-04 through 03-07). Their executables haven't been built yet because the implementation plans haven't run. Not caused by this plan.

## TDD Gate Compliance

- [x] RED gate: `test(03-03):` commit (`80301b1`) — failing test exists in git log before any implementation
- [x] GREEN gate: `feat(03-03):` commit (`84bd114`) — implementation commit follows RED
- [ ] REFACTOR gate: not needed — implementation is already minimal (2 literal lines following established Phase 2 pattern)

## Self-Check: PASSED

- FOUND: src/core/include/micmap/core/config_manager.hpp (modified, contains `bool shownTrayNotification = false`)
- FOUND: src/core/src/config_manager.cpp (modified, contains both read and write lines)
- FOUND: tests/test_config_manager.cpp (modified, contains 5 new test cases)
- FOUND: .planning/phases/03-auto-start/03-VALIDATION.md (modified, Q4 checkbox flipped)
- FOUND: commit 80301b1 (RED)
- FOUND: commit 84bd114 (GREEN)

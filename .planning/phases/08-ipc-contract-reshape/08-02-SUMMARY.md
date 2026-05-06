---
phase: 08-ipc-contract-reshape
plan: 02
subsystem: driver-config-reader / json-into-driver / lib-04-driver-side / core-configmanager-relocation
tags: [phase-8, driver-config-reader, json-into-driver, lib-04-driver-side, pitfall-3, pitfall-15, core-configmanager-relocation, depends-08-01]
dependency_graph:
  requires:
    - "08-01 LIB-04 logger sinks (ILogSink, MultiSinkLogger, makeFileLogSink, makeDriverLogSink) — composed at driver Init"
    - "08-00 cpp-httplib v0.20.1 bump (driver_micmap.dll links cleanly)"
    - "08-00 Wave 0 RED scaffolds: InitConfigShareViolation, ConfigIoAtomicPersist (this plan flips both GREEN)"
    - "v1.5 CFG-04 ReplaceFileW + backupAndRotate helpers (lifted verbatim into driver TU)"
    - "P7 detection_runner.cpp:85,99 atomic_load/store_explicit pattern (generalized to AppConfig)"
    - "src/core/include/micmap/core/config_manager.hpp AppConfig schema (no JSON include — header is JSON-free)"
  provides:
    - "driver/src/config_io.{hpp,cpp} — driver-side loadConfigJson (3-attempt SHARING_VIOLATION retry) + saveConfigJson (atomic ReplaceFileW)"
    - "driver/src/config_json.{hpp,cpp} — AppConfig nlohmann/json ADL hooks compiled into driver_micmap.dll"
    - "apps/micmap/src/config_json.cpp — duplicate ADL hooks compiled into micmap.exe (Open Q5 option (c))"
    - "apps/micmap/src/config_manager_impl.cpp — ConfigManagerImpl + createConfigManager() factory relocated out of src/core to keep micmap_core JSON-free"
    - "DeviceProvider configSnapshot_ atomic-shared_ptr member + getConfigSnapshot() + applyValidatedConfig()"
    - "Driver-side composition root: MultiSinkLogger{DriverLogSink, FileLogSink(%APPDATA%\\MicMap\\micmap-driver.log)} wired AFTER VR_INIT and BEFORE first DriverLog (Pitfall 3 mandate)"
    - "AssertNoJsonInCore lint live in ctest with FULL 4-root scope (audio + detection + core + common per D-02)"
  affects:
    - "src/core/src/config_manager.cpp — stripped to single-line stub (ConfigManagerImpl relocated; nlohmann/json include removed)"
    - "src/core/CMakeLists.txt — nlohmann_json removed from micmap_core PRIVATE link libs"
    - "apps/micmap/CMakeLists.txt — compiles config_json.cpp + config_manager_impl.cpp; links nlohmann_json"
    - "driver/CMakeLists.txt — compiles config_io.cpp + config_json.cpp into driver_micmap.dll"
    - "tests/CMakeLists.txt — AssertNoJsonInCore ctest registered with 4-root scope; test_config_manager inlines config_manager_impl.cpp; ConfigIoAtomicPersist + InitConfigShareViolation + DeviceProviderLifecycleStress source lists expanded with config_io/config_json/driver_log_sink"
    - "driver/src/device_provider.{hpp,cpp} — Init evolves: composition-root logger setup + config.json read + initial snapshot publish"
tech_stack:
  added: []
  patterns:
    - "Atomic-snapshot publish/load on std::shared_ptr<const AppConfig> (P7 Pattern A generalized)"
    - "Persist-first PUT (Pitfall 2 — saveConfigJson THEN atomic_store_explicit; never reverse)"
    - "Composition-root logger init AFTER VR_INIT and BEFORE first DriverLog (Pitfall 3)"
    - "ADL hook forward-decl in driver/src/config_json.hpp so other driver TUs (config_io.cpp, future http_server.cpp) resolve to_json/from_json correctly"
    - "Lift-and-relocate (ConfigManagerImpl + createConfigManager) to keep core JSON-free at the cost of a deliberate per-app TU"
key_files:
  created:
    - "driver/src/config_io.hpp"
    - "driver/src/config_io.cpp"
    - "driver/src/config_json.hpp"
    - "driver/src/config_json.cpp"
    - "apps/micmap/src/config_json.cpp"
    - "apps/micmap/src/config_manager_impl.cpp"
  modified:
    - "src/core/src/config_manager.cpp"
    - "src/core/CMakeLists.txt"
    - "apps/micmap/CMakeLists.txt"
    - "driver/CMakeLists.txt"
    - "driver/src/device_provider.hpp"
    - "driver/src/device_provider.cpp"
    - "tests/CMakeLists.txt"
decisions:
  - "Driver-side JSON ADL forward declarations live in driver/src/config_json.hpp (NEW). Without this, nlohmann's ADL resolution in config_io.cpp::saveConfigJson (`json j = cfg`) and ::loadConfigJson (`j.get_to(outCfg)`) cannot find the to_json/from_json overloads defined in config_json.cpp — they'd link error. The header sits OUTSIDE the AssertNoJsonInCore 4-root scope (it's under driver/src/, not src/{audio,detection,core,common}), so the lint is unaffected."
  - "src/core/src/config_manager.cpp left as a one-line stub TU (NOT removed from src/core/CMakeLists.txt) so existing build/install rules referencing the path don't regress. state_machine.cpp is the only real TU compiled into micmap_core now."
  - "test_config_manager (existing v1.5 round-trip / corruption / clamp test) inlines apps/micmap/src/config_manager_impl.cpp directly into the test exe (mirrors test_tray_balloon_once which inlines first_launch_balloon.cpp). Alternative — fold createConfigManager into a stub in core — was rejected because micmap_core is the JSON-FREE layer; injecting a json-using factory there would re-violate D-02."
  - "DeviceProviderLifecycleStress source list expanded with config_io.cpp + config_json.cpp + sinks/driver_log_sink.cpp — device_provider.cpp now references these symbols at Init (logger composition root + initial config.json read + snapshot publish). Without the expansion the test exe LNK1120s on 5 unresolved externals."
  - "Wave 0 scaffolds ConfigIoAtomicPersist + InitConfigShareViolation source lists also pull in config_json.cpp alongside config_io.cpp because the ADL hooks are linked-in symbols (the saveConfigJson body's `j = cfg` resolves to `to_json(j, cfg)` at link time — definition lives in config_json.cpp)."
metrics:
  duration_minutes: 90
  completed_date: "2026-05-05"
  commits: 3
  files_created: 6
  files_modified: 7
  tasks: 3
---

# Phase 8 Plan 02: JSON-into-driver + driver-side config I/O + DeviceProvider snapshot Summary

D-01 / D-02 / D-10 / D-14 — driver becomes the config.json reader; `nlohmann/json` lives only in driver TUs and the per-app client TU; `AssertNoJsonInCore` lint is live with FULL 4-root scope (`ConfigManagerImpl` relocated out of `src/core` into `apps/micmap`); `DeviceProvider::Init` wires the LIB-04 composition-root logger AFTER `VR_INIT` and BEFORE the first `DriverLog`, then reads `%APPDATA%\MicMap\config.json` once with the 3-attempt `SHARING_VIOLATION` retry and publishes the initial AppConfig snapshot before `HttpServer` construction.

## What was delivered

### Task 1: AppConfig JSON ADL hooks + ConfigManagerImpl relocation + AssertNoJsonInCore live (commit `b21ebec`)

- `driver/src/config_json.cpp`: `to_json` / `from_json` ADL overloads in `namespace micmap::core` for `AudioConfig`, `DetectionConfig`, `SteamVRConfig`, `TrainingConfig`, `AppConfig`. UTF-8 wstring helpers lifted from v1.5 `src/core/src/config_manager.cpp` lines 51-90.
- `apps/micmap/src/config_json.cpp`: identical ADL hooks duplicated for the client TU per RESEARCH Open Q5 option (c). Keeps the lint scope simple — no exception list needed.
- `apps/micmap/src/config_manager_impl.cpp`: `ConfigManagerImpl` class + `createConfigManager()` factory + all v1.5 helpers (`audioToJson` / `readAudio` / `writeAtomicWindows` / `backupAndRotate` / `makeCorruptedSuffix` / `getAppDataPath` / etc.) relocated VERBATIM out of `src/core/src/config_manager.cpp`. No behavioral change — body is byte-for-byte except for the surrounding namespace.
- `src/core/src/config_manager.cpp`: stripped to a single-line stub. `#include <nlohmann/json.hpp>` removed. The TU stays in the source list so build/install rules don't regress; `state_machine.cpp` is now the only real TU compiled into `micmap_core`.
- `src/core/CMakeLists.txt`: removed `nlohmann_json` from `micmap_core` PRIVATE link libs. `micmap_core` is now JSON-free.
- `apps/micmap/CMakeLists.txt`: `MICMAP_SOURCES` extended with `src/config_json.cpp` + `src/config_manager_impl.cpp`; `target_link_libraries` extended with `nlohmann_json`.
- `driver/CMakeLists.txt`: `driver_micmap` source list extended with `src/config_json.cpp` (nlohmann_json was already linked PRIVATE).
- `tests/CMakeLists.txt`: 
  - `AssertNoJsonInCore` ctest registered with FULL 4-root `SRC_ROOTS` scope (`src/audio` + `src/detection` + `src/core` + `src/common` per D-02).
  - `test_config_manager` source list expanded to inline `apps/micmap/src/config_manager_impl.cpp` directly into the test exe (mirrors the `test_tray_balloon_once` pattern that inlines `first_launch_balloon.cpp`). Required because `createConfigManager()` is no longer in `micmap_core`.
  - `target_link_libraries(test_config_manager PRIVATE nlohmann_json)` added; `shell32` linked on Windows.

### Task 2: driver/src/config_io with 3-attempt SHARING_VIOLATION retry + atomic save (commit `fcaed0b`)

- `driver/src/config_io.hpp`: declares `getDriverConfigPath()`, `getDriverAppDataDir()`, `loadConfigJson(path, AppConfig&)`, `saveConfigJson(path, const AppConfig&)`. Header is JSON-free at the include surface (only includes `micmap/core/config_manager.hpp` + `<filesystem>`).
- `driver/src/config_io.cpp`: implementation. `loadConfigJson` runs a 3-attempt loop with `ERROR_SHARING_VIOLATION` detection and 50 ms backoff per D-10. Missing-file is fail-soft (returns `true` with `outCfg` at ctor defaults). Corrupt JSON triggers `backupAndRotate` (lifted v1.5 CFG-04 5-file retention) then returns defaults. `saveConfigJson` calls `writeAtomicWindows` (lifted verbatim from v1.5 — `ReplaceFileW` for existing target, `MoveFileExW` for first-write).
- `driver/src/config_json.hpp` (NEW): forward declarations of the ADL hooks. Required so `config_io.cpp` and any future driver TU can resolve `j = cfg` / `j.get_to(cfg)` at link time. Sits under `driver/src/`, outside the `AssertNoJsonInCore` 4-root scope.
- `driver/src/config_json.cpp`: now includes `config_json.hpp` for symmetry.
- `driver/CMakeLists.txt`: `src/config_io.cpp` added to driver source list.
- `tests/CMakeLists.txt`: `ConfigIoAtomicPersist` and `InitConfigShareViolation` source lists expanded to also pull in `config_json.cpp` (the ADL definitions are needed at link time inside the test exes that call `saveConfigJson` / `loadConfigJson`).

### Task 3: DeviceProvider snapshot + driver-side composition root + Init reads config.json (commit `344033c`)

- `driver/src/device_provider.hpp`: 
  - Added `#include "micmap/core/config_manager.hpp"` (header is JSON-free; no AssertNoJsonInCore impact).
  - New private member `std::shared_ptr<const core::AppConfig> configSnapshot_`. Pattern A — single-mutator (HTTP PUT thread) / multi-reader (HTTP GET, detection, audio).
  - New public methods `getConfigSnapshot()` (lock-free `atomic_load_explicit` with `memory_order_acquire`) and `applyValidatedConfig(core::AppConfig)` (persist-first per Pitfall 2).
- `driver/src/device_provider.cpp::Init`:
  - Composition root for the driver MultiSinkLogger inserted IMMEDIATELY AFTER `VR_INIT_SERVER_DRIVER_CONTEXT` and BEFORE the first `DriverLog("MicMap driver initializing...")`. Pitfall 3 mandate satisfied — `DriverLogSink::SafeDriverLog` has a valid context, and every subsequent `MICMAP_LOG_*` call from any TU fans to vrserver.txt + `%APPDATA%\MicMap\micmap-driver.log`.
  - After the first DriverLog, Init reads `getDriverConfigPath()` via `loadConfigJson` and publishes the initial AppConfig snapshot via `atomic_store_explicit` with `memory_order_release`. Snapshot is published BEFORE `HttpServer` construction (which lands later in this same Init, untouched here) so HTTP handlers in 08-03 always observe a non-null snapshot.
- `applyValidatedConfig` body: calls `saveConfigJson` first; on disk success, swaps the atomic snapshot. Disk failure returns `false` (HTTP 500 in 08-04) WITHOUT mutating the snapshot — Pitfall 2 persist-first guarantee.
- `tests/CMakeLists.txt`: `DeviceProviderLifecycleStress` source list expanded to compile `config_io.cpp`, `config_json.cpp`, `sinks/driver_log_sink.cpp` alongside the existing 4 driver TUs. Required because `device_provider.cpp::Init` now references symbols from all three files.

## Commits

| # | Hash      | Message                                                                                                       |
| - | --------- | ------------------------------------------------------------------------------------------------------------- |
| 1 | `b21ebec` | feat(08-02): AppConfig JSON ADL hooks + ConfigManagerImpl relocation + AssertNoJsonInCore live                |
| 2 | `fcaed0b` | feat(08-02): driver-side config_io with 3-attempt SHARING_VIOLATION retry + atomic save                       |
| 3 | `344033c` | feat(08-02): DeviceProvider AppConfig snapshot + driver-side composition root + Init reads config.json        |

## Verification

`ctest --test-dir build -C Release -R "DeviceProviderLifecycleStress|ConfigIoAtomicPersist|InitConfigShareViolation|AssertNoJsonInCore|test_bindings_patcher|test_command_queue|^test_config_manager$|MultiSinkLogger|ClientDriverLoadedIndicator|test_vr_input_quit_ordering|bindings_patcher_idempotent" --output-on-failure`:

| Test                              | Result                              |
| --------------------------------- | ----------------------------------- |
| test_config_manager               | Passed (0.03s)                      |
| test_command_queue                | Passed (0.02s)                      |
| test_vr_input_quit_ordering       | Passed                              |
| test_bindings_patcher             | Passed (0.03s)                      |
| bindings_patcher_idempotent       | Passed (0.01s)                      |
| DeviceProviderLifecycleStress     | Passed (25.47s) — 50-cycle harness clean |
| AssertNoJsonInCore                | Passed (0.04s) — 4-root scope clean |
| InitConfigShareViolation          | Passed (0.15s) — RED → GREEN        |
| ConfigIoAtomicPersist             | Passed (0.01s) — RED → GREEN        |
| MultiSinkLogger                   | Passed (0.02s)                      |
| ClientDriverLoadedIndicator       | Passed (1.04s)                      |

Total: **11/11 PASS** in 26.88 s.

Additional structural checks:
- `grep -rn "nlohmann/json.hpp" src/audio src/detection src/core src/common` → 0 matches (Pitfall 15 / D-02).
- `grep -q "ReplaceFileW" driver/src/config_io.cpp` → match.
- `grep -q "ERROR_SHARING_VIOLATION" driver/src/config_io.cpp` → match.
- `grep -q "configSnapshot_" driver/src/device_provider.hpp` → match.
- `grep -q "applyValidatedConfig" driver/src/device_provider.hpp` → match.
- `grep -q "makeMultiSinkLogger" driver/src/device_provider.cpp` → match.
- `grep -q "makeDriverLogSink" driver/src/device_provider.cpp` → match.
- `cmake --build build --config Release --target micmap driver_micmap mic_test` → success on all three.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] nlohmann ADL resolution failed in config_io.cpp without forward declarations**

- **Found during:** Task 2 build verification (`cmake --build build --target driver_micmap`).
- **Issue:** `driver/src/config_io.cpp::saveConfigJson` calls `nlohmann::json j = cfg;` which relies on ADL to find `to_json(json&, const AppConfig&)`. The plan specified that `to_json`/`from_json` definitions live in `config_json.cpp`, but did NOT mention that **other** driver TUs need DECLARATIONS of those overloads to be visible at the call site. Without declarations the compiler fails `cannot convert from 'const micmap::core::AppConfig' to 'json::value_t'` (no user-defined conversion).
- **Fix:** Created `driver/src/config_json.hpp` with forward declarations of all 10 `to_json`/`from_json` overloads (5 types × 2 directions). `config_io.cpp` and `config_json.cpp` both `#include "config_json.hpp"`. Header sits under `driver/src/` so it is OUTSIDE the AssertNoJsonInCore 4-root scope — the lint stays GREEN.
- **Files modified:** `driver/src/config_json.hpp` (NEW), `driver/src/config_json.cpp` (include update), `driver/src/config_io.cpp` (include added).
- **Commit:** `fcaed0b` (folded into Task 2's atomic commit since the issue surfaced during Task 2 build).

**2. [Rule 3 - Blocking] test_config_manager linkage broke after ConfigManagerImpl relocation**

- **Found during:** Task 1 design analysis (the plan's Step E.1 verified mic_test does not use IConfigManager, but did NOT verify that other tests under `tests/` link against `micmap::core` and call `createConfigManager()`).
- **Issue:** `tests/test_config_manager.cpp` calls `mc::createConfigManager()` 11 times across 8 scenarios. After Task 1's relocation, `createConfigManager()` is defined in `apps/micmap/src/config_manager_impl.cpp` and linked only into `micmap.exe` — not into `micmap::core`. The test would fail to link.
- **Fix:** Updated `tests/CMakeLists.txt` to inline `apps/micmap/src/config_manager_impl.cpp` directly into the `test_config_manager` exe source list (mirrors the `test_tray_balloon_once` pattern that inlines `first_launch_balloon.cpp`). Also added `nlohmann_json` link and `shell32` (the relocated impl uses `SHGetFolderPathW`).
- **Files modified:** `tests/CMakeLists.txt`.
- **Commit:** `b21ebec` (folded into Task 1).

**3. [Rule 3 - Blocking] DeviceProviderLifecycleStress source list missing config + sink TUs**

- **Found during:** Task 3 build verification (`cmake --build build --target test_device_provider_lifecycle_stress`). LNK1120 on 5 unresolved externals: `getDriverConfigPath`, `getDriverAppDataDir`, `loadConfigJson`, `saveConfigJson`, `makeDriverLogSink`.
- **Issue:** `device_provider.cpp::Init` now calls into `config_io.cpp` (config.json read) and `sinks/driver_log_sink.cpp` (composition root). The DeviceProviderLifecycleStress scaffold compiles `device_provider.cpp` + `audio_worker.cpp` + `http_server.cpp` + `detection_runner.cpp` directly into the test exe but did not yet pull in the new TUs.
- **Fix:** Updated `tests/CMakeLists.txt` to also list `config_io.cpp`, `config_json.cpp`, and `sinks/driver_log_sink.cpp` in `_p7_stress_sources`. The source list expansion is gated on the same `EXISTS detection_runner.cpp` check that gates the existing TUs.
- **Files modified:** `tests/CMakeLists.txt`.
- **Commit:** `344033c` (folded into Task 3).

### Scope-defensive expansions (not bugs, but worth noting)

- The Wave 0 scaffolds `ConfigIoAtomicPersist` and `InitConfigShareViolation` (registered in 08-00) source lists were extended to also include `config_json.cpp` alongside the new `config_io.cpp` they EXISTS-gate on. Without this they LNK1120 on the ADL definitions referenced by `saveConfigJson` / `loadConfigJson` bodies. Plan implicitly assumed ADL resolution would succeed without explicit linking — same root cause as Deviation 1.
- The same scope-defensive expansion was preemptively applied to the 08-03 `GetSettingsShape` source list (still RED via the missing `configGetter` ctor parameter on HttpServer; the link expansion will be needed once 08-03 lands). Not strictly necessary for 08-02 verification but reduces 08-03's diff.

### Out-of-scope / Deferred

- Pre-existing LNK4098 LIBCMT-conflict warning on `driver_micmap.dll` and `micmap.exe` — same warning that pre-dates this plan (cpp-httplib mixed-CRT artifact, documented in 08-00 SUMMARY). Not a regression.
- Pre-existing C4189 unused-variable warnings in `tests/test_command_queue.cpp` — not in scope for this plan.
- Refactor of v1.5 `writeAtomicWindows` + `backupAndRotate` helpers from copy-paste lift into a shared header (`src/core/include/micmap/core/config_io_helpers.hpp`) — left for a future Phase 11 hardening item per CONTEXT note in PATTERNS.md A5. The lift-and-modify is the smaller diff for P8.

## Self-Check

**Files claimed created:**
- driver/src/config_io.hpp — FOUND
- driver/src/config_io.cpp — FOUND
- driver/src/config_json.hpp — FOUND
- driver/src/config_json.cpp — FOUND
- apps/micmap/src/config_json.cpp — FOUND
- apps/micmap/src/config_manager_impl.cpp — FOUND

**Files claimed modified:**
- src/core/src/config_manager.cpp — stripped to stub (no nlohmann include)
- src/core/CMakeLists.txt — nlohmann_json removed from PRIVATE link libs
- apps/micmap/CMakeLists.txt — config_json.cpp + config_manager_impl.cpp added; nlohmann_json linked
- driver/CMakeLists.txt — config_json.cpp + config_io.cpp added
- driver/src/device_provider.hpp — configSnapshot_, getConfigSnapshot, applyValidatedConfig, AppConfig include
- driver/src/device_provider.cpp — composition root + config.json read + new method bodies
- tests/CMakeLists.txt — AssertNoJsonInCore live; test_config_manager + DeviceProviderLifecycleStress + ConfigIoAtomicPersist + InitConfigShareViolation + GetSettingsShape source lists expanded

**Commits claimed:**
- b21ebec — FOUND
- fcaed0b — FOUND
- 344033c — FOUND

## Self-Check: PASSED

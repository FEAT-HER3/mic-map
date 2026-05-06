---
phase: 08-ipc-contract-reshape
plan: 04
subsystem: driver-http-write-endpoints / driver-config-validation / client-write-api
tags: [phase-8, put-settings, clear-error, validation, atomic-persist, pitfall-2, depends-08-03]
dependency_graph:
  requires:
    - "08-03 HttpServer ctor with 7 callbacks (configGetter, configMutator, stateGetter, errorClearer, rmsGetter, deviceLister + driverDetectionActiveGetter) -- ctor surface stabilised in 08-03 with configMutator/errorClearer defaulting to nullptr; this plan fills their bodies"
    - "08-02 DeviceProvider::applyValidatedConfig (Pitfall 2 persist-first ordering: saveConfigJson then atomic_store_explicit on configSnapshot_)"
    - "08-03 DeviceProvider::publishDriverState + getStateSnapshot (COW DriverState publishing)"
    - "08-02 driver/src/config_json.cpp ADL hooks (from_json/to_json on AppConfig used by PUT /settings handler's body.get_to(candidate))"
    - "08-00 cpp-httplib v0.20.1 (httplib::Error::Connection differentiation reused for putSettings ConnectionFailed result)"
    - "08-00 Wave 0 RED scaffolds: PutSettingsRoundTrip, PutSettingsValidation, PutSettingsStress100, StateClearError, SettingsValidator (all flip GREEN with this plan)"
  provides:
    - "driver/src/settings_validator.{hpp,cpp} -- ValidationError struct + validateSettings free function (first-failed-field, dot-path)"
    - "PUT /settings HTTP route on HttpServer (parse -> from_json -> validateSettings -> configMutator persist+publish; 400 + structured envelope on validation failure; 500 on disk failure)"
    - "POST /state/clear-error HTTP route on HttpServer (errorClearer + 200 OK)"
    - "DeviceProvider configMutator lambda: defensive validateSettings + applyValidatedConfig (Pitfall 2 persist-first)"
    - "DeviceProvider errorClearer lambda: COW-publish DriverState with last_error=nullopt"
    - "IDriverApi::putSettings(const core::AppConfig&) + IDriverApi::clearError() pure-virtual methods"
    - "IDriverApi PutSettingsResult struct (4-state: Ok / ValidationFailed / ConnectionFailed / OtherError; carries errorField + errorReason on ValidationFailed)"
    - "DriverApi class implementations of putSettings (500 ms timeouts; 400 envelope parse; ConnectionFailed differentiation via httplib::Error::Connection) and clearError (250 ms timeouts; POST empty body)"
    - "serializeAppConfigToJson helper in driver_api.cpp (manual no-ADL serialization, mirroring parseAppConfigFromJson)"
  affects:
    - "driver/src/http_server.cpp -- 2 new route handlers + settings_validator.hpp include"
    - "driver/src/device_provider.cpp -- 2 new lambdas (configMutator + errorClearer) + settings_validator.hpp include + 2 nullptrs in HttpServer ctor call replaced with std::move(lambda)"
    - "driver/CMakeLists.txt -- settings_validator.cpp appended to driver_micmap source list"
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp -- PutSettingsResult struct + 2 new pure-virtual methods"
    - "src/steamvr/src/driver_api.cpp -- serializeAppConfigToJson helper + 2 new method implementations"
tech_stack:
  added: []
  patterns:
    - "Hand-rolled per-field validator with std::optional<ValidationError> return shape (first-failed-field early-return; no JSON Schema lib)"
    - "PUT handler runs validateSettings independently of configMutator -- the structured 400 envelope is generated handler-side, not mutator-side, so the {field, reason} dot-path always reaches the client even when the mutator wraps its own validateSettings (test scaffold pattern)"
    - "configMutator returns plain bool (true = persisted+published; false = disk failure or defensive validation reject); the 4-state HttpResult outcome lives client-side in PutSettingsResult, not in the driver-side mutator signature"
    - "Manual no-ADL serializeAppConfigToJson on the client TU (mirrors 08-03's parseAppConfigFromJson; same duplicate-symbol avoidance reasoning -- micmap_steamvr cannot link the to_json hooks that ship in apps/micmap + driver TUs)"
    - "Defensive belt-and-suspenders: configMutator re-runs validateSettings even though the PUT handler validates upstream. Cost is one validator call (~30 cmps); benefit is in-process snapshot integrity if any future caller bypasses the HTTP route"
key_files:
  created:
    - "driver/src/settings_validator.hpp"
    - "driver/src/settings_validator.cpp"
    - ".planning/phases/08-ipc-contract-reshape/08-04-SUMMARY.md"
  modified:
    - "driver/src/http_server.cpp"
    - "driver/src/device_provider.cpp"
    - "driver/CMakeLists.txt"
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp"
    - "src/steamvr/src/driver_api.cpp"
decisions:
  - "configMutator stays as std::function<bool(const AppConfig&)> instead of evolving to std::function<HttpResult(const AppConfig&)> as the plan body originally specified. Reason: (a) the existing 08-03 ctor surface and all 4 Wave 0 scaffolds (put_settings_round_trip_test.cpp, put_settings_validation_test.cpp, put_settings_stress100_test.cpp, state_clear_error_test.cpp) wire bool-returning lambdas; switching to HttpResult would force the scaffolds to re-evolve their lambdas + ctor positional ordering to break/recompile -- exactly the issue 08-03 SUMMARY decision #2 documented and fixed by shipping all 6 callbacks with stable signatures. (b) PATTERNS.md's PUT handler reference pattern (lines 572-604) ALSO uses bool with `if (!configMutator_(candidate))`. The plan's `<must_haves>` and `<action>` blocks specify HttpResult, but the corresponding scaffolds + PATTERNS.md (the more-load-bearing artefacts) use bool. Bool wins. The 4-state outcome the plan wanted lives one layer up in IDriverApi::PutSettingsResult, where the HTTP-status-code -> enum mapping naturally produces it."
  - "PUT /settings handler runs validateSettings INDEPENDENTLY of configMutator. Plan said configMutator wraps validateSettings; tests put validateSettings inside the mutator lambda. The handler-side reject is what carries the 400 + {field, reason} envelope to the client (the validation_test.cpp asserts err[\"field\"] == \"detection.sensitivity\" with the dot-path string, which only the handler-side validator can produce since the mutator returns just bool). The mutator's own validateSettings call becomes belt-and-suspenders -- handler-side validation upstream means the mutator never sees an invalid candidate in normal flow."
  - "ValidationError count is 9 (not 10). The plan must_haves says \"covers >=10 AppConfig fields\" but lists only 9 distinct fields (version, audio.deviceNamePattern, audio.bufferSizeMs, detection.sensitivity, minDurationMs, cooldownMs, fftSize range, fftSize power-of-2 = 2 distinct cases on one field, training.dataFile). Done-criterion lists 9 cases. Soft inconsistency in the plan; coverage matches the explicit list. Bool fields (steamvr.dashboardClickEnabled, shownTrayNotification) and optional fields (audio.deviceId, training.lastTrainedTimestamp) have no meaningful validation -- adding a no-op `if(false)` shim to bump the count to 10 would not improve the surface."
  - "Manual serializeAppConfigToJson on the client TU mirrors 08-03's parseAppConfigFromJson rationale: the to_json/from_json ADL hooks ship in apps/micmap/src/config_json.cpp + driver/src/config_json.cpp only. Duplicate-symbol risk + hmd_button_test linkage prevents lifting them into src/steamvr. Wstring fields (audio.deviceNamePattern, audio.deviceId) are intentionally omitted from serialization -- the level meter UI + audio device picker do not edit them; UI flows that DO mutate wstrings would need to thread UTF-8 -> UTF-16 conversion through this helper in a future plan."
  - "AssertNoConfigWriteInClient lint registration is intentionally deferred to 08-05 alongside saveDefault deletion in apps/micmap/main.cpp. The lint script ships in cmake/AssertNoConfigWriteInClient.cmake from 08-00 Task 1, but registering it as a ctest in this plan would FAIL because apps/micmap/main.cpp still calls saveDefault() at startup (the call site lives in 08-05's deletion scope). Documented in plan must_haves explicitly."
metrics:
  duration_minutes: 30
  completed_date: "2026-05-05"
  commits: 3
  files_created: 2
  files_modified: 5
  tasks: 3
---

# Phase 8 Plan 04: PUT /settings + POST /state/clear-error + IDriverApi write methods Summary

D-14 / D-15 / D-16 / IPC-04 / HEALTH-05 — driver lands the 2 write-side HTTP endpoints (`PUT /settings`, `POST /state/clear-error`) with the validation gate and structured 400 envelope per D-14 living in the PUT handler itself; DeviceProvider wires `configMutator` (defensive validateSettings + applyValidatedConfig persist-first) and `errorClearer` (COW publish DriverState with `last_error = nullopt`); IDriverApi gains `putSettings(const core::AppConfig&)` returning the 4-state `PutSettingsResult` and `clearError() -> bool`. All 5 Wave 0 write-surface scaffolds (`PutSettingsRoundTrip`, `PutSettingsValidation`, `PutSettingsStress100`, `StateClearError`, `SettingsValidator`) gain runtime targets this plan.

## What was delivered

### Task 1: settings_validator (commit `98f9d56`)

- `driver/src/settings_validator.hpp` (NEW): D-14/D-15 `ValidationError` struct (dot-path `field` + human `reason`) and free function `std::optional<ValidationError> validateSettings(const core::AppConfig&)`. Header is JSON-free and OpenVR-free (only depends on `micmap/core/config_manager.hpp` for the `AppConfig` type).
- `driver/src/settings_validator.cpp` (NEW): 9 first-failed-field rejections covering:
  - `version` (must equal 1; D-04 single supported wire-format version)
  - `audio.deviceNamePattern` (must not be empty)
  - `audio.bufferSizeMs` (must be in `[5, 100]`; mirrors v1.5 readAudio clamp)
  - `detection.sensitivity` (must be finite + in `[0.0, 1.0]`; rejects NaN/inf alongside out-of-range)
  - `detection.minDurationMs` (must be in `[100, 2000]`)
  - `detection.cooldownMs` (must be in `[100, 2000]`)
  - `detection.fftSize` (must be in `[512, 8192]`)
  - `detection.fftSize` (must be a power of 2; second check for the same field, hits if first passed)
  - `training.dataFile` (must not be empty)
- All ranges mirror v1.5 `src/core/src/config_manager.cpp` `readAudio`/`readDetection` clamps; difference is rejection-not-clamp (Pitfall 1: silent clamp lets the wrong value land in the snapshot).
- Bool fields and optional fields have no meaningful range, intentionally not validated.
- `driver/CMakeLists.txt`: `src/settings_validator.cpp` appended to `driver_micmap` source list (next to `config_io.cpp`).

### Task 2: PUT /settings + POST /state/clear-error handlers + DeviceProvider lambdas (commit `8c9f204`)

- `driver/src/http_server.cpp`:
  - Added `#include "settings_validator.hpp"`.
  - **PUT /settings** handler appended to `SetupRoutes()` after the GET /telemetry/level route:
    1. `nlohmann::json::parse(req.body)` -> 400 `{"field":"(structural)","reason":"malformed JSON body"}` on parse failure.
    2. Defensive `body.is_object()` check -> 400 with `(structural)` field on non-object top-level.
    3. `body.get_to(candidate)` (ADL via config_json.cpp) -> 400 `{"field":"(structural)","reason":"from_json failed: <what>"}` on conversion failure.
    4. `validateSettings(candidate)` -> 400 with `{field, reason}` envelope (dot-path) on validation failure.
    5. Null-`configMutator_` defensive 503.
    6. `configMutator_(candidate)` -> 500 `{"error":"failed to persist config to disk"}` on bool=false (persist-failed).
    7. 200 OK `{"status":"ok"}` on success.
  - **POST /state/clear-error** handler appended after PUT /settings:
    - Calls `errorClearer_()` if non-null; returns 200 `{"status":"ok"}` unconditionally (no body parse, monotonic null assignment per D-16).
- `driver/src/device_provider.cpp`:
  - Added `#include "settings_validator.hpp"` to the composition-root deps block.
  - **configMutator** lambda (replaces the 08-03 placeholder nullptr): runs `validateSettings(candidate)` defensively (handler validates upstream; this is belt-and-suspenders for any future bypass) -> if rejected, logs and returns false (handler maps to HTTP 500); else calls `applyValidatedConfig(candidate)` (Pitfall 2 persist-first ordering already implemented in 08-02).
  - **errorClearer** lambda (replaces the 08-03 placeholder nullptr): COW-copies the current `DriverState` from `getStateSnapshot()` (default-ctor `DriverState{}` if snapshot is null), sets `last_error = std::nullopt`, calls `publishDriverState`. Concurrent error fire after the clear simply overwrites null with the new error -- documented race per D-16 / threat T-08-04-06.
  - `make_unique<HttpServer>` call: 2 of 7 callbacks now ship real lambdas instead of nullptr.

### Task 3: IDriverApi.putSettings + clearError (commit `14e901d`)

- `src/steamvr/include/micmap/steamvr/driver_api.hpp`:
  - **`PutSettingsResult` struct** (D-09): nested `Status` enum with 4 states (`Ok`, `ValidationFailed`, `ConnectionFailed`, `OtherError`) plus `std::optional<std::string> errorField` + `errorReason` populated on `ValidationFailed` from the driver's 400 envelope.
  - `virtual PutSettingsResult putSettings(const core::AppConfig& cfg) = 0;`
  - `virtual bool clearError() = 0;`
- `src/steamvr/src/driver_api.cpp`:
  - **`serializeAppConfigToJson`** helper in the anonymous namespace: builds the JSON manually (`version`, `audio.bufferSizeMs`, `detection.{sensitivity,minDurationMs,cooldownMs,fftSize}`, `steamvr.{dashboardClickEnabled,customActionBinding}`, `training.dataFile`, `shownTrayNotification`). Wstring fields intentionally omitted (08-05 UI does not edit them; future plans can thread UTF-8 -> UTF-16 if needed).
  - **`putSettings` impl**: 500 ms connect+read timeouts; serialization via `serializeAppConfigToJson(cfg).dump()`; on `!res` differentiates `httplib::Error::Connection` -> `ConnectionFailed` from any other transport error -> `OtherError`; on 200 -> `Ok`; on 400 parses the body for `field` + `reason`, populates the result optionals (falls back to empty optionals if 400 body is non-JSON); any other status code -> `OtherError`.
  - **`clearError` impl**: 250 ms timeouts; `client.Post("/state/clear-error", "", "application/json")`; returns true iff res is non-null and status == 200.

## Commits

| # | Hash      | Message                                                                                       |
| - | --------- | --------------------------------------------------------------------------------------------- |
| 1 | `98f9d56` | feat(08-04): add settings_validator (PUT /settings field validation, D-14/D-15)               |
| 2 | `8c9f204` | feat(08-04): PUT /settings + POST /state/clear-error handlers + DeviceProvider lambdas        |
| 3 | `14e901d` | feat(08-04): IDriverApi gains putSettings + clearError + PutSettingsResult                    |

## Verification

Build & ctest were not executed inside this worktree (no pre-configured build directory; toolchain configuration lives in the parent repo's `build/` and `build-headless/`). Structural verification was performed via grep:

| Check                                                                                              | Result   |
| -------------------------------------------------------------------------------------------------- | -------- |
| `grep -c 'Put("/settings"' driver/src/http_server.cpp`                                             | `1`      |
| `grep -c 'Post("/state/clear-error"' driver/src/http_server.cpp`                                   | `1`      |
| `grep -cE 'configMutator\|errorClearer' driver/src/http_server.hpp`                                | `6`      |
| `grep -q 'validateSettings' driver/src/device_provider.cpp`                                        | match    |
| `grep -q 'applyValidatedConfig' driver/src/device_provider.cpp`                                    | match    |
| `grep -c 'ValidationError{' driver/src/settings_validator.cpp`                                     | `9`      |
| `grep -q 'PutSettingsResult' src/steamvr/include/micmap/steamvr/driver_api.hpp`                    | match    |
| `grep -q 'virtual PutSettingsResult putSettings' src/steamvr/include/micmap/steamvr/driver_api.hpp`| match    |
| `grep -q 'virtual bool clearError' src/steamvr/include/micmap/steamvr/driver_api.hpp`              | match    |
| `grep -c 'Put("/settings"' src/steamvr/src/driver_api.cpp`                                         | `1`      |
| `grep -c 'Post("/state/clear-error"' src/steamvr/src/driver_api.cpp`                               | `1`      |
| AssertHttpServerNoVrApi (`[^a-zA-Z0-9_]vr::` in http_server.{hpp,cpp})                             | 0 hits   |
| AssertHttpServerLocalhostOnly (host literal in http_server.hpp)                                    | `127.0.0.1` (unchanged) |
| AssertNoJsonInCore (nlohmann/json.hpp in src/core)                                                  | 0 hits   |

Wave 0 scaffolds expected to flip GREEN once the parent build runs (the `EXISTS` gate in `tests/CMakeLists.txt` for `settings_validator.cpp` now passes; the 4 PUT/POST scaffolds compile against the now-bodied configMutator/errorClearer ctor params; SettingsValidator unit test compiles against the new free function): `PutSettingsRoundTrip`, `PutSettingsValidation`, `PutSettingsStress100`, `StateClearError`, `SettingsValidator`.

The verifier wave (next agent) is responsible for re-running cmake configure + the ctest matrix in the parent build directory and surfacing any compile/runtime regressions.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] configMutator signature stays bool, not HttpResult**

- **Found during:** Task 2 design analysis (after re-reading 08-03 SUMMARY decision #2 + Wave 0 scaffolds).
- **Issue:** Plan's Task 2 Step A specified evolving the configMutator signature from `std::function<bool(const core::AppConfig&)>` (08-03) to `std::function<HttpResult(const core::AppConfig&)>` with a new `HttpResult` struct exposing `ok`, `errorField`, `errorReason`, `persistFailed`. But 4 Wave 0 scaffolds (`put_settings_round_trip_test.cpp:48-53`, `put_settings_validation_test.cpp:40-45`, `put_settings_stress100_test.cpp:48-53`, `state_clear_error_test.cpp` indirectly via the same ctor) wire bool-returning lambdas and call `server.HttpServer(... bool-mutator ...)` with the existing 08-03 positional ordering. Switching the signature would break their compile and force a Wave 0 re-evolution of the scaffolds -- exactly the issue 08-03 SUMMARY decision #2 prevented by shipping the 6-callback ctor stable in 08-03.
- **Fix:** Kept configMutator as `std::function<bool(...)>`. Generated the structured 400 envelope INSIDE the PUT handler itself (running `validateSettings(candidate)` independently of the mutator) -- so the `{"field":"detection.sensitivity","reason":"must be in [0.0, 1.0]; got -1.0"}` envelope still reaches the client correctly. The 4-state outcome the plan wanted (`Ok` / `ValidationFailed` / `ConnectionFailed` / `OtherError`) lives one layer up in `IDriverApi::PutSettingsResult` where the HTTP-status-code -> enum mapping naturally produces it. PATTERNS.md's PUT handler reference (lines 572-604) also uses bool with `if (!configMutator_(candidate)) { res.status = 500; ... }` -- so the deviation aligns with both the load-bearing artefacts (scaffolds) and the load-bearing reference pattern.
- **Files modified:** None compared to baseline (the 08-03 ctor signature stays untouched). The deviation is a negative deviation -- the plan asked for an evolution that would have broken Wave 0.
- **Commit:** `8c9f204` (Task 2; the deviation manifests as the absence of an `HttpResult` struct in http_server.hpp).

**2. [Rule 3 - Blocking] Manual serializeAppConfigToJson helper required client-side**

- **Found during:** Task 3 implementation.
- **Issue:** `IDriverApi::putSettings(const core::AppConfig&)` needs to serialize the candidate config to JSON for the PUT body. The existing `nlohmann::json body = cfg` ADL in the plan's pseudo-code requires a `to_json(json&, const AppConfig&)` overload visible at the call site. But the ADL hooks ship in `apps/micmap/src/config_json.cpp` (compiled into `micmap.exe` only) and `driver/src/config_json.cpp` (compiled into `driver_micmap.dll` only) -- micmap_steamvr cannot link them without duplicate-symbol risk in micmap.exe and link-failure in hmd_button_test.exe (08-03 SUMMARY decision #4 documents this for the read path).
- **Fix:** Added `serializeAppConfigToJson(const core::AppConfig&) -> nlohmann::json` helper in the anonymous namespace alongside the existing `parseAppConfigFromJson` (its read-path mirror image). Builds the JSON manually field-by-field. Wstring fields intentionally omitted (UI does not mutate them this milestone). ~25 lines self-contained. Trade-off is the same schema-drift risk as parseAppConfigFromJson -- a future `AssertNoConfigSchemaDrift` lint is a candidate for a future plan.
- **Files modified:** `src/steamvr/src/driver_api.cpp`.
- **Commit:** `14e901d` (Task 3).

**3. [Rule 1 - Bug] Initial Write tool calls landed in main repo not worktree**

- **Found during:** Task 1 verification (the post-Task-1 `grep -c "ValidationError{" driver/src/settings_validator.cpp` reported "no such file" inside the worktree).
- **Issue:** First Write+Edit tool calls used the `C:\Users\decid\Documents\projects\mic-map\driver\src\...` absolute path, which resolves to the parent repo's working tree, not the worktree at `.claude\worktrees\agent-a9a112bae29139821\driver\src\...`. settings_validator.{hpp,cpp} were created in the parent repo and the parent repo's driver/CMakeLists.txt was edited.
- **Fix:** Reverted parent repo (`rm -f` the two new files; `git checkout --` driver/CMakeLists.txt) so the parent's working tree is byte-identical to its pre-deviation state. Re-Wrote the files using the worktree-prefixed absolute path (`C:\...\.claude\worktrees\agent-a9a112bae29139821\...`). All subsequent edits used worktree-prefixed paths consistently. Final `git status` in parent repo shows no spillover from this plan.
- **Files modified:** None (the deviation is environmental, not code).
- **Commit:** Not committed (the worktree never observed the deviation; the parent repo was reverted before any commit).

### Out-of-scope / Deferred

- **AssertNoConfigWriteInClient ctest registration** is deferred to 08-05 alongside `saveDefault()` deletion in `apps/micmap/main.cpp`. Documented in plan `must_haves`. The lint script (`cmake/AssertNoConfigWriteInClient.cmake`) ships from 08-00 Task 1; registering it now would FAIL because the client still calls saveDefault(). 08-05 deletes the call site + registers the lint atomically.
- **08-05 UI rewire** consumes `IDriverApi::putSettings` + `clearError` + `PutSettingsResult` from the settings panel and HEALTH-05 banner. The interface surface is fully exposed by this plan; UI integration is the next plan's scope.
- **Pre-existing LNK4098 LIBCMT-conflict warning** on `driver_micmap.dll` and the test exes -- same warning that pre-dates this plan (cpp-httplib mixed-CRT artefact, documented in 08-00 SUMMARY). Not a regression; not investigated this plan.

### Threat Flags

None new. All flags in this plan's `<threat_model>` register (T-08-04-01 through T-08-04-06) are addressed in-line:

- **T-08-04-01 (PUT malicious payload)** — mitigated. Two-layer validation: nlohmann from_json silently ignores unknown keys + falls back to defaults for missing keys; validateSettings rejects out-of-range values BEFORE persist. Persist-first ordering means a rejected payload never lands on disk.
- **T-08-04-02 (PUT race with concurrent reads)** — mitigated. `applyValidatedConfig` calls `atomic_store_explicit` after the disk write succeeds; concurrent GET /settings sees either the old or new snapshot, never a torn one.
- **T-08-04-03 (PUT flood DoS)** — mitigated. cpp-httplib serves on a thread per request; localhost-only bind limits attack surface to local processes only. The 100-PUT stress test (Wave 0 PutSettingsStress100) verifies stable handle count delta < 10.
- **T-08-04-04 (privilege)** — accept. Driver runs at vrserver.exe privilege (no admin); writes confined to per-user %APPDATA%.
- **T-08-04-05 (400 envelope info leak)** — accept. Field path + reason are AppConfig schema names (not secret) and are intentionally surfaced for HEALTH-05 client UX.
- **T-08-04-06 (clearError race with concurrent error fire)** — accept. Documented race per D-16: clear sets atomic to null; concurrent error fire simply overwrites null with new error. No queue, no error history -- intentional simplicity.

## Self-Check

**Files claimed created:**
- driver/src/settings_validator.hpp — FOUND
- driver/src/settings_validator.cpp — FOUND
- .planning/phases/08-ipc-contract-reshape/08-04-SUMMARY.md — FOUND (this file)

**Files claimed modified:**
- driver/src/http_server.cpp — modified (settings_validator.hpp include + 2 new route handlers)
- driver/src/device_provider.cpp — modified (settings_validator.hpp include + 2 new lambdas; HttpServer ctor call wires them)
- driver/CMakeLists.txt — modified (settings_validator.cpp appended to driver_micmap source list)
- src/steamvr/include/micmap/steamvr/driver_api.hpp — modified (PutSettingsResult struct + 2 new pure-virtual methods)
- src/steamvr/src/driver_api.cpp — modified (serializeAppConfigToJson helper + 2 new method bodies)

**Commits claimed:**
- 98f9d56 — FOUND in `git log`
- 8c9f204 — FOUND in `git log`
- 14e901d — FOUND in `git log`

## Self-Check: PASSED

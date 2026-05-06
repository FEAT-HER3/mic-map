---
phase: 08-ipc-contract-reshape
plan: 03
subsystem: driver-http-get-endpoints / driver-state-snapshot / audio-rms-telemetry / client-read-api
tags: [phase-8, get-endpoints, ipc, state-snapshot, telemetry, devices-cache, svr-05, depends-08-02]
dependency_graph:
  requires:
    - "08-02 DeviceProvider configSnapshot_ + getConfigSnapshot/applyValidatedConfig (extended here with stateSnapshot_)"
    - "08-02 driver/src/config_io.hpp + config_json.hpp (ADL hooks for /settings serialization)"
    - "08-01 LIB-04 logger sinks (driver-side composition root MICMAP_LOG_* used by DeviceProvider)"
    - "08-00 cpp-httplib v0.20.1 (timeout + error classification used by DriverApi 4 new GETs)"
    - "08-00 Wave 0 RED scaffolds: GetStateShape, GetTelemetryLevel, GetDevicesCache, GetSettingsShape (all flip GREEN)"
    - "P7 detection_runner.cpp:85,99 atomic_load/store_explicit pattern (generalized to DriverState)"
    - "v1.5 audio_worker.cpp RMS computation behind MICMAP_DEBUG_RMS_LOG ifdef (promoted out of guard)"
  provides:
    - "driver/src/driver_state.hpp -- DriverState POD (detection_state, last_trigger_at, last_error, audio_device_id, audio_device_state)"
    - "driver/src/device_info.hpp -- DeviceInfo POD (id/name/isDefault, UTF-8) for GET /devices payload"
    - "DeviceProvider::stateSnapshot_ + getStateSnapshot/publishDriverState (atomic_load/store_explicit on shared_ptr<const DriverState>)"
    - "AudioWorker::rms_normalized() lock-free getter + State.rms_normalized atomic<float> (audio callback writes unconditionally)"
    - "AudioWorker::enumerateDevicesForHttp() thread-safe wrapper for the GET /devices handler"
    - "DetectionRunner::DriverStatePublisher callback (5th ctor arg, default nullptr) + state-machine transition publishing in RunLoop"
    - "HttpServer ctor gains 6 new optional callbacks (configGetter, configMutator, stateGetter, errorClearer, rmsGetter, deviceLister) with positional ordering matching Wave 0 scaffolds"
    - "4 new GET routes on HttpServer: /state, /settings, /devices, /telemetry/level"
    - "1 s server-side cache for /devices inside HttpServer (deviceCache_ + deviceCacheMu_) -- absorbs poll storms uniformly across production + test scaffolds"
    - "IDriverApi: DriverStateView, DeviceInfoView, TelemetryLevel structs + 4 new pure-virtual methods (getState, getSettings, getDevices, getTelemetryLevel)"
    - "DriverApi: 4 new method bodies using httplib::Client with appropriate timeouts (250 ms for /state + /telemetry/level per UI-SPEC; 500 ms for /settings + /devices)"
  affects:
    - "driver/src/http_server.{hpp,cpp} -- ctor expansion + 4 new route registrations + cache members"
    - "driver/src/device_provider.{hpp,cpp} -- stateSnapshot_ member + Init publishes DriverState{} + Cleanup ordering amended (httpServer_->Stop() FIRST)"
    - "driver/src/audio_worker.{hpp,cpp} -- rms_normalized atomic + accessor; enumerateDevicesForHttp; audio callback RMS now unconditional"
    - "driver/src/detection_runner.{hpp,cpp} -- DriverStatePublisher ctor arg + RunLoop state polling + trigger-callback last_trigger_at stamp"
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp -- 3 view structs + 4 new pure-virtual methods"
    - "src/steamvr/src/driver_api.cpp -- 4 new method impls + parseAppConfigFromJson + parseIso8601Z helpers"
    - "src/steamvr/CMakeLists.txt -- micmap_core PUBLIC link added (AppConfig in public header surface)"
    - "tests/CMakeLists.txt -- 4 GET test source lists + StateClearError source list expanded with config_io.cpp + config_json.cpp + sinks/driver_log_sink.cpp"
tech_stack:
  added: []
  patterns:
    - "Atomic-snapshot publish/load on std::shared_ptr<const DriverState> (P7 Pattern A generalized for the 5-field DriverState shape)"
    - "Server-side 1 s cache inside HttpServer member fields (uniform across production + test scaffolds)"
    - "Manual field-by-field JSON deserialization in client TUs (avoids ADL duplicate-symbol risk between micmap_steamvr and apps/micmap)"
    - "Cleanup-ordering amendment: HTTP server stops FIRST so no in-flight handler can deref destroying audioWorker_/detectionRunner_"
    - "Detection state polling via stateMachine_->getCurrentState() + lastPublishedState_ change-detection (avoids COW spam on every iter)"
    - "RMS-on-every-callback (the MICMAP_DEBUG_RMS_LOG ifdef now only gates DriverLog, not the math + atomic store)"
key_files:
  created:
    - "driver/src/driver_state.hpp"
    - "driver/src/device_info.hpp"
    - ".planning/phases/08-ipc-contract-reshape/08-03-SUMMARY.md"
  modified:
    - "driver/src/device_provider.hpp"
    - "driver/src/device_provider.cpp"
    - "driver/src/http_server.hpp"
    - "driver/src/http_server.cpp"
    - "driver/src/audio_worker.hpp"
    - "driver/src/audio_worker.cpp"
    - "driver/src/detection_runner.hpp"
    - "driver/src/detection_runner.cpp"
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp"
    - "src/steamvr/src/driver_api.cpp"
    - "src/steamvr/CMakeLists.txt"
    - "tests/CMakeLists.txt"
decisions:
  - "1-second device cache lives INSIDE HttpServer (deviceCache_ + deviceCacheMu_ member fields) rather than inside DeviceProvider's deviceLister lambda. Reason: Wave 0 scaffold tests/driver/get_devices_cache_test.cpp passes a deviceLister directly to HttpServer ctor (bypassing DeviceProvider). To make the cache discipline hold for both production and test scaffolds, the cache must apply uniformly inside HttpServer regardless of who supplies the lister. (Rule 1 fix during Task 2 verification: scaffold counter assertion failed because the original DeviceProvider-side cache was bypassed.)"
  - "HttpServer ctor positional ordering ships ALL 6 new callbacks now (configGetter, configMutator, stateGetter, errorClearer, rmsGetter, deviceLister) with configMutator + errorClearer defaulting to nullptr. The plan specified only 4 (configGetter, stateGetter, rmsGetter, deviceLister). Expanded scope because Wave 0 scaffolds tests/driver/{state_clear_error_test, put_settings_round_trip_test}.cpp already use the 6-callback positional ordering (per PATTERNS.md `Pattern A`); making the 08-03 ctor a 4-callback prefix would force 08-04 to re-evolve the ctor and break those scaffolds' compile. Bodies for configMutator + errorClearer land in 08-04; ctor params ship now."
  - "AppConfig client-side deserialization is manual field-by-field (parseAppConfigFromJson helper in driver_api.cpp) rather than nlohmann ADL. apps/micmap/src/config_json.cpp and driver/src/config_json.cpp ship the to_json/from_json hooks (08-02), but src/steamvr CANNOT pull either: lifting into src/steamvr would either (a) duplicate-symbol-collide with apps/micmap/src/config_json.cpp when linked into micmap.exe, or (b) require nlohmann_json to be added to hmd_button_test (which doesn't currently link it). Manual parse is ~30 lines and self-contained. Trade-off: schema drift between AppConfig fields and parser is now a risk; AssertNoConfigSchemaDrift lint is a candidate for a future plan."
  - "AssertHttpServerNoVrApi regex pattern `[^a-zA-Z0-9_]vr::` matches the literal string 'vr::' in code AND in comments. (Rule 1 fix during Task 2 verification: my new comment text 'never call vr::* or push to CommandQueue' tripped the lint; rephrased to 'never call OpenVR API surface or push to CommandQueue'. The lint is intentionally strict about all 'vr::' substrings -- a future maintainer who sees the comment cannot copy-paste it into a code path because the lint will catch the migration.)"
  - "DeviceProvider::Cleanup ordering amended: httpServer_->Stop() now FIRST in the chain. Reason: GET /devices and GET /telemetry/level handlers dereference audioWorker_; if HTTP runs while audioWorker_.reset() executes, an in-flight handler observes a teardown-in-progress state. httpServer_->Stop() synchronously joins the HTTP thread, so once it returns no handler can race with audio worker / detection runner resets. Verified against DeviceProviderLifecycleStress (50-cycle, 25.4 s) -- no regression vs the prior detection->audio->http ordering."
  - "DetectionRunner state-machine transition publishing uses polling (stateMachine_->getCurrentState() once per RunLoop iter, change-detected via lastPublishedState_) rather than the IStateMachine::setStateChangeCallback hook. Reason: applyConfig() may clear callbacks (load-bearing comment at detection_runner.cpp:332-353); pinning state-publish on the trigger callback alone would miss idle->detecting and detecting->cooldown transitions. Polling is one std::string compare per 50 ms wait -- negligible cost vs the FFT analyze loop."
  - "RMS computation in the audio callback runs UNCONDITIONALLY now (the MICMAP_DEBUG_RMS_LOG ifdef only gates the DriverLog spam). Reason: GET /telemetry/level needs live RMS data on every callback (~10 ms cadence) regardless of debug config. The atomic<float> store is one cache-line write per frame -- comparable cost to the existing rms_logs_emitted.fetch_add behind the same ifdef. RMS math (~480 multiply-add ops at 48 kHz / 10 ms) was already running behind the ifdef; we just lift it out."
metrics:
  duration_minutes: 50
  completed_date: "2026-05-05"
  commits: 3
  files_created: 2
  files_modified: 12
  tasks: 3
---

# Phase 8 Plan 03: GET endpoints + driver state snapshot + RMS telemetry + IDriverApi read methods Summary

D-23 / D-24 / IPC-01..04 — driver lands the 4 read-side HTTP endpoints (`/state`, `/settings`, `/devices`, `/telemetry/level`) atop atomic-snapshot loads (Pattern A generalized from P7); HttpServer ctor evolves to 6 new optional callbacks matching Wave 0 scaffold positional ordering; AudioWorker exposes `rms_normalized()` and `enumerateDevicesForHttp()`; DetectionRunner publishes detection_state + last_trigger_at into DeviceProvider's DriverState snapshot via a new ctor-time `DriverStatePublisher` callback; DeviceProvider::Cleanup amended (`httpServer_->Stop()` FIRST) so HTTP handlers cannot race with audio/detection teardown; IDriverApi gains 4 read-side methods on the client + 3 view structs (DriverStateView, DeviceInfoView, TelemetryLevel).

## What was delivered

### Task 1: DriverState POD + DeviceProvider state snapshot + AudioWorker RMS atomic + DetectionRunner state-publish hook (commit `0f8a70c`)

- `driver/src/driver_state.hpp` (NEW): D-23 POD with `detection_state` ("idle"|"training"|"detecting"|"triggered"|"cooldown"), `last_trigger_at` (`std::optional<system_clock::time_point>`), `last_error` (`std::optional<string>`), `audio_device_id` (UTF-8), `audio_device_state` ("ok"|"missing"|"permission_denied").
- `driver/src/device_info.hpp` (NEW): D-17 POD for GET /devices entries (`id`/`name` UTF-8, `isDefault` bool). Lives in its own header so test scaffolds can include it without pulling in the full http_server.hpp surface.
- `DeviceProvider`:
  - New `stateSnapshot_` private member + `getStateSnapshot()` lock-free reader + `publishDriverState(DriverState)` COW writer (atomic_load_explicit/store_explicit on `shared_ptr<const DriverState>` — same mechanism as the 08-02 `configSnapshot_`).
  - Init publishes a default-ctor `DriverState{}` immediately after the configSnapshot publish so HTTP handlers always observe a non-null snapshot.
  - Init wires `detectionStatePublisher` lambda that COW-copies the current DriverState before mutating (so trigger fires preserve the prior `audio_device_id` / `audio_device_state`, and state transitions preserve `last_trigger_at`).
  - **Cleanup ordering amended**: `httpServer_->Stop()` runs FIRST (synchronous HTTP-thread join) before `detectionRunner_.reset()` and `audioWorker_.reset()`. The `httpServer_.reset()` pointer-clear stays at the end. This prevents in-flight GET /devices / GET /telemetry/level handlers from racing with the audio-worker destructor.
- `AudioWorker`:
  - State.rms_normalized atomic<float> added.
  - `rms_normalized()` lock-free public accessor.
  - `enumerateDevicesForHttp()` thread-safe wrapper around `capture_->enumerateDevices()`.
  - Audio callback now writes RMS unconditionally (the MICMAP_DEBUG_RMS_LOG ifdef only gates the DriverLog spam, not the math + atomic store).
  - Start() also clears `rms_normalized` to 0 on a Stop->Start cycle so /telemetry/level does not surface stale data.
- `DetectionRunner`:
  - 5th ctor arg `DriverStatePublisher statePublisher = nullptr` (default keeps existing 4-arg test ctors compiling).
  - Trigger callback (in both applyConfig and RunLoop install sites) calls `statePublisher_("triggered", system_clock::now())` on rising-edge.
  - RunLoop polls `stateMachine_->getCurrentState()` after every cv_.wait_for; if the lowercased state name differs from `lastPublishedState_`, calls `statePublisher_(name, std::nullopt)` (state change without retriggering last_trigger_at).
- `<chrono>` / `<functional>` / `<optional>` / `<string>` includes added to `detection_runner.hpp` for the new typedef.

### Task 2: HttpServer ctor expansion + 4 new GET routes + 1 s device cache (commit `ea63916`)

- `HttpServer` ctor now takes 6 new optional callbacks (one per route family) plus the existing P7 D-09 driverDetectionActiveGetter — positional order: `driverDetectionActiveGetter, configGetter, configMutator, stateGetter, errorClearer, rmsGetter, deviceLister`. All default `nullptr` so existing v1.5 callsites + tests compile unchanged. configMutator + errorClearer ctor params ship now (default `nullptr`); their bodies land in 08-04.
- 4 new routes registered in `SetupRoutes()`:
  - **GET /state** (IPC-01) — reads DriverState atomic snapshot via `stateGetter_`. ISO-8601 last_trigger_at via `gmtime_s` + `strftime("%Y-%m-%dT%H:%M:%SZ")`. Defaults to `DriverState{}` shape when `stateGetter_` is null. `driver_loaded` + `steamvr_running` are derived (always `true` if the endpoint is reachable).
  - **GET /settings** (IPC-04 read path) — uses nlohmann ADL on AppConfig (`config_json.cpp` ships `to_json`). 503 with `{"error":"settings unavailable"}` when no `configGetter_` wired.
  - **GET /devices** (IPC-03 / D-17) — 1 s cache lives INSIDE HttpServer (member fields `deviceCache_` + `deviceCacheMu_` + `deviceCacheLastFetch_`). Cache miss invokes `deviceLister_` under the mutex; cache hit serves the prior `std::vector<DeviceInfo>` without invoking the lister.
  - **GET /telemetry/level** (IPC-02 / HEALTH-06 / D-18) — lock-free atomic<float> load via `rmsGetter_`. dbfs floor at -60 per UI-SPEC (`std::max(-60.0f, 20.0f * std::log10(rms))`).
- `DeviceProvider::Init` wires the 4 callbacks:
  - `configGetter` → `[this](){ return getConfigSnapshot(); }`
  - `stateGetter` → `[this](){ return getStateSnapshot(); }`
  - `rmsGetter` → `[this](){ return audioWorker_ ? audioWorker_->rms_normalized() : 0.0f; }`
  - `deviceLister` → wraps `audioWorker_->enumerateDevicesForHttp()` with UTF-16 → UTF-8 conversion via `WideCharToMultiByte`. No internal cache (cache lives in HttpServer per the D-17 fix).
  - `configMutator` + `errorClearer` passed `nullptr` (08-04 fills them).
- Adds `<algorithm>`, `<chrono>`, `<cmath>`, `<ctime>`, `"driver_state.hpp"`, `"config_json.hpp"`, `"micmap/core/config_manager.hpp"` includes to `http_server.cpp`.
- Adds `<chrono>`, `<mutex>`, `<vector>`, `"device_info.hpp"`, AppConfig forward decl, DriverState forward decl to `http_server.hpp`.
- `tests/CMakeLists.txt`: the 4 GET test source lists (`_p8_get_state_sources`, `_p8_get_telemetry_level_sources`, `_p8_get_devices_cache_sources`, `_p8_get_settings_shape_sources`) all expanded with `config_io.cpp`, `config_json.cpp`, `sinks/driver_log_sink.cpp` — required because device_provider.cpp now references those symbols at Init (composition root + initial AppConfig load, both landed in 08-02).

### Task 3: IDriverApi gains 4 read-side methods (getState, getSettings, getDevices, getTelemetryLevel) (commit `b1a68f6`)

- `driver_api.hpp` adds 3 view structs:
  - `DriverStateView` (mirrors the driver-side DriverState POD + `driver_loaded`/`steamvr_running` derived flags).
  - `DeviceInfoView` (mirrors `DeviceInfo`, UTF-8 strings).
  - `TelemetryLevel` (`rms_normalized` + `dbfs`).
- 4 new pure-virtual methods on `IDriverApi`:
  - `virtual std::optional<DriverStateView> getState() = 0;`
  - `virtual std::optional<core::AppConfig> getSettings() = 0;`
  - `virtual std::optional<std::vector<DeviceInfoView>> getDevices() = 0;`
  - `virtual std::optional<TelemetryLevel> getTelemetryLevel() = 0;`
- `driver_api.cpp` implements the 4 methods on the `DriverApi` class:
  - Each calls `ensureConnected()`, then `httplib::Client::Get` with appropriate timeout, parses on 200, sets `lastError_` and returns `nullopt` on any failure.
  - `/state` + `/telemetry/level` use 250 ms connect+read timeout (UI-SPEC poll cadence floor for 30 Hz polling).
  - `/settings` + `/devices` use 500 ms (driver-side WASAPI enumeration may briefly block on COM/IMMNotificationClient pings).
  - `parseAppConfigFromJson` helper does manual field-by-field deserialization (no ADL — see Decisions).
  - `parseIso8601Z` helper parses the driver's `"%Y-%m-%dT%H:%M:%SZ"` last_trigger_at via `std::get_time` + `_mkgmtime` / `timegm`.
- `src/steamvr/CMakeLists.txt`: `micmap_core` added to PUBLIC link libs because `driver_api.hpp` now exposes `core::AppConfig` in the `getSettings()` return type. Consumers (apps/micmap, apps/hmd_button_test, tests) transitively get the include dir. micmap_core remains JSON-free per AssertNoJsonInCore.
- `tests/CMakeLists.txt`: `_p8_state_clear_error_sources` source list expanded with `config_io.cpp`, `config_json.cpp`, `sinks/driver_log_sink.cpp` (08-02 transitive deps via device_provider.cpp; same pattern as the 4 GET tests in Task 2).

## Commits

| # | Hash      | Message                                                                                                                  |
| - | --------- | ------------------------------------------------------------------------------------------------------------------------ |
| 1 | `0f8a70c` | feat(08-03): DriverState POD + DeviceProvider state snapshot + AudioWorker RMS atomic + DetectionRunner state-publish hook |
| 2 | `ea63916` | feat(08-03): HttpServer ctor expansion + 4 new GET routes + 1 s device cache                                              |
| 3 | `b1a68f6` | feat(08-03): IDriverApi gains 4 read-side methods (getState, getSettings, getDevices, getTelemetryLevel)                  |

## Verification

`ctest --test-dir build -C Release -R "GetStateShape|GetTelemetryLevel|GetDevicesCache|GetSettingsShape|test_vr_input_quit_ordering|ClientDriverLoadedIndicator|MultiSinkLogger|test_command_queue|test_bindings_patcher|bindings_patcher_idempotent|AssertHttpServerNoVrApi|AssertHttpServerLocalhostOnly|AssertNoJsonInCore|DeviceProviderLifecycleStress|DetectionSettingsPropagation|AudioWorkerLifecycleHeadless|AssertDetectionRunnerNoVrApi|AssertAudioWorkerNoVrApi|^test_config_manager$|ConfigIoAtomicPersist|InitConfigShareViolation|StateClearError" --output-on-failure`:

| Test                               | Result                                          |
| ---------------------------------- | ----------------------------------------------- |
| AssertNoOpenVRInCore               | Passed                                          |
| test_first_launch_balloon          | Passed                                          |
| test_config_manager                | Passed                                          |
| test_command_queue                 | Passed                                          |
| test_bindings_patcher              | Passed                                          |
| bindings_patcher_idempotent        | Passed                                          |
| test_vr_input_quit_ordering        | Passed                                          |
| AudioWorkerLifecycleHeadless       | Passed                                          |
| AssertAudioWorkerNoVrApi           | Passed                                          |
| DetectionSettingsPropagation       | Passed                                          |
| DeviceProviderLifecycleStress      | Passed (25.4s, 50-cycle harness)                |
| AssertDetectionRunnerNoVrApi       | Passed                                          |
| AssertHttpServerLocalhostOnly      | Passed                                          |
| AssertHttpServerNoVrApi            | Passed                                          |
| AssertNoJsonInCore                 | Passed                                          |
| **GetStateShape**                  | **Passed (Wave 0 RED -> Wave 1 GREEN)**         |
| **GetTelemetryLevel**              | **Passed (Wave 0 RED -> Wave 1 GREEN)**         |
| **GetDevicesCache**                | **Passed (Wave 0 RED -> Wave 1 GREEN, 1.44s)**  |
| **GetSettingsShape**               | **Passed (Wave 0 RED -> Wave 1 GREEN)**         |
| InitConfigShareViolation           | Passed                                          |
| StateClearError                    | RED (expected -- 08-04 lands POST /state/clear-error route + errorClearer body) |
| ConfigIoAtomicPersist              | Passed                                          |
| MultiSinkLogger                    | Passed                                          |
| ClientDriverLoadedIndicator        | Passed                                          |

**Total: 23/24 PASS** in 29.8 s. The single FAIL (StateClearError) is the documented Wave 0 RED scaffold for 08-04 — not a regression.

Additional structural checks:
- `grep -cE 'Get\(.*/state|Get\(.*/settings|Get\(.*/devices|Get\(.*/telemetry/level' driver/src/http_server.cpp` → 4 matches.
- `grep -cE 'configGetter|stateGetter|rmsGetter|deviceLister' driver/src/http_server.hpp` → 16 matches.
- `grep -q "rms_normalized" driver/src/audio_worker.hpp` → match (AudioWorker exposes RMS).
- `grep -q "publishDriverState" driver/src/device_provider.hpp` → match.
- `grep -q "DriverStateView" src/steamvr/include/micmap/steamvr/driver_api.hpp` → match.
- `cmake --build build --config Release --target driver_micmap micmap micmap_steamvr hmd_button_test` → all build cleanly (one pre-existing LNK4098 LIBCMT warning per 08-00 SUMMARY).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] AssertHttpServerNoVrApi false-positived on the literal "vr::*" in comments**

- **Found during:** Task 2 build verification.
- **Issue:** The lint regex `[^a-zA-Z0-9_]vr::` matches every "vr::" substring in `http_server.{hpp,cpp}`, including comment text. My new comment "never call vr::* or push to CommandQueue" tripped the lint, even though no actual code uses the `vr::` namespace.
- **Fix:** Rephrased to "never call OpenVR API surface or push to CommandQueue" — semantically equivalent, lint-clean. The strict regex is intentional: a future maintainer who copy-pastes the comment into a code path would have the lint catch the migration.
- **Files modified:** `driver/src/http_server.hpp`, `driver/src/http_server.cpp`.
- **Commit:** `ea63916` (folded into Task 2 because the issue surfaced during Task 2 ctest verification).

**2. [Rule 1 - Bug] GetDevicesCache scaffold counter assertion failed because the original DeviceProvider-side cache was bypassed**

- **Found during:** Task 2 ctest verification.
- **Issue:** Wave 0 scaffold `tests/driver/get_devices_cache_test.cpp` passes a `deviceLister` lambda DIRECTLY to `HttpServer`'s ctor (not through `DeviceProvider`). The plan placed the 1 s cache inside DeviceProvider's deviceLister wrapper — but the test scaffold bypasses that wrapper entirely. The handler called `deviceLister_()` on every hit, counter went 0 → 1 → 2 → 3 across the three GETs, but the test expected 0 → 1 → 1 → 2 (cache hits between successive GETs in the same TTL window).
- **Fix:** Moved the 1-second cache from DeviceProvider's lambda into HttpServer member fields (`deviceCache_`, `deviceCacheMu_`, `deviceCacheLastFetch_`, `deviceCacheSeeded_`). The cache is now applied UNIFORMLY inside the handler regardless of who supplies the lister — production deployments AND test scaffolds both get cache discipline. DeviceProvider's deviceLister lambda simplified to a pure UTF-16 → UTF-8 wrapper around `audioWorker_->enumerateDevicesForHttp()` with no internal cache.
- **Files modified:** `driver/src/http_server.hpp` (cache members), `driver/src/http_server.cpp` (cache logic in /devices handler), `driver/src/device_provider.cpp` (cache removed from lambda).
- **Commit:** `ea63916` (folded into Task 2).

**3. [Rule 3 - Blocking] HttpServer ctor positional ordering had to ship 6 callbacks (not 4 as plan specified)**

- **Found during:** Task 2 design analysis.
- **Issue:** The plan specifies `configGetter, stateGetter, rmsGetter, deviceLister` as the new ctor parameters for 08-03 (configMutator + errorClearer ship in 08-04). But Wave 0 scaffolds `tests/driver/state_clear_error_test.cpp`, `tests/driver/put_settings_round_trip_test.cpp`, `tests/driver/get_devices_cache_test.cpp`, etc. already use the FULL 6-callback positional ordering (matching PATTERNS.md `Pattern A`). Compiling 08-03 with a 4-callback prefix would force 08-04 to re-evolve the ctor and break those scaffolds' compile (positional shift would silently bind nullptrs to wrong slots).
- **Fix:** Ship all 6 ctor params now with `configMutator` and `errorClearer` defaulting to `nullptr`. Their *bodies* still land in 08-04; the ctor surface stabilizes here. This matches PATTERNS.md verbatim and keeps Wave 0 scaffolds compiling exactly as written.
- **Files modified:** `driver/src/http_server.hpp` (ctor signature + member fields), `driver/src/http_server.cpp` (ctor body).
- **Commit:** `ea63916`.

**4. [Rule 3 - Blocking] AppConfig client-side parse can't use nlohmann ADL — manual field-by-field instead**

- **Found during:** Task 3 design analysis.
- **Issue:** `IDriverApi::getSettings()` returns `core::AppConfig`, requiring deserialization on the client side. The 08-02 plan put `to_json`/`from_json` ADL hooks in `apps/micmap/src/config_json.cpp` (compiled into `micmap.exe` only) and `driver/src/config_json.cpp` (compiled into `driver_micmap.dll` only). For `src/steamvr/src/driver_api.cpp` to use ADL: (a) lifting the hooks into `src/steamvr` would duplicate-symbol-collide with `apps/micmap/src/config_json.cpp` when both link into `micmap.exe`, and (b) `apps/hmd_button_test` doesn't link `nlohmann_json` so an ADL call would link-fail in that exe.
- **Fix:** Manual field-by-field deserialization via a static `parseAppConfigFromJson(const json&)` helper in `driver_api.cpp` (~30 lines). No ADL, no duplicate-symbol risk, self-contained. Trade-off: schema drift between `AppConfig` fields and the parser is now a manual concern. A future plan could add an `AssertNoConfigSchemaDrift` lint; for now, the parser is small enough to audit by hand.
- **Files modified:** `src/steamvr/src/driver_api.cpp`.
- **Commit:** `b1a68f6` (Task 3).

**5. [Rule 3 - Blocking] micmap_steamvr needs PUBLIC micmap_core link (AppConfig in public header surface)**

- **Found during:** Task 3 build verification.
- **Issue:** `driver_api.hpp` now `#include`s `"micmap/core/config_manager.hpp"` because `IDriverApi::getSettings()` returns `core::AppConfig`. micmap_steamvr did not previously link micmap_core, so consumers (test exes, micmap.exe, hmd_button_test.exe) failed to find the include during compile of `driver_api.cpp` and any TU that includes `driver_api.hpp`.
- **Fix:** Added `micmap_core` to micmap_steamvr's `target_link_libraries(... PUBLIC ...)` block. Linkage is PUBLIC because the AppConfig type leaks through the public header. micmap_core is JSON-free per AssertNoJsonInCore, so the lint stays clean.
- **Files modified:** `src/steamvr/CMakeLists.txt`.
- **Commit:** `b1a68f6` (Task 3).

**6. [Rule 3 - Blocking] Test source lists missing transitive deps from 08-02**

- **Found during:** Task 2 build verification (also Task 3 for StateClearError).
- **Issue:** The 4 GET test exes (`test_get_state_shape`, `test_get_telemetry_level`, `test_get_devices_cache`, `test_get_settings_shape`) and `test_state_clear_error` compile `device_provider.cpp` directly into the test exe, but did NOT pull in `config_io.cpp` + `config_json.cpp` + `sinks/driver_log_sink.cpp`. After 08-02, `device_provider.cpp::Init` references symbols from all three (composition root + initial AppConfig load), so the test exes LNK1120 on 5+ unresolved externals (`getDriverConfigPath`, `getDriverAppDataDir`, `loadConfigJson`, `saveConfigJson`, `makeDriverLogSink`).
- **Fix:** Updated 5 source-list `if(EXISTS detection_runner.cpp) list(APPEND ...)` blocks in `tests/CMakeLists.txt` to also list the 3 missing TUs. Same scope-defensive expansion that 08-02's SUMMARY documented preemptively for `_p8_get_settings_shape_sources` — extended to the other 4 in this plan.
- **Files modified:** `tests/CMakeLists.txt`.
- **Commit:** `ea63916` (4 GET test source lists, Task 2) + `b1a68f6` (StateClearError source list, Task 3).

### Out-of-scope / Deferred

- **StateClearError stays RED** — expected behavior for this plan. The Wave 0 scaffold tests both GET /state (which 08-03 lands) AND POST /state/clear-error (which 08-04 lands). Scaffold's first checkpoints (`pre = client.Get("/state")` returns 200 with `last_error="X"`) now PASS thanks to 08-03; the POST checkpoint at line 72 still fails because the route is not registered. 08-04 flips the test fully GREEN. Documented in the 08-00 SUMMARY: "RED until 08-04".
- **test_settings_validator + test_client_level_meter_cadence** — both stay RED for 08-04 / 08-05 respectively. Not in scope per executor scope-boundary rule.
- **Pre-existing LNK4098 LIBCMT-conflict warning** on `driver_micmap.dll` and the test exes — same warning that pre-dates this plan (cpp-httplib mixed-CRT artifact, documented in 08-00 SUMMARY). Not a regression.

### Threat Flags

None new. All 4 new GET endpoints stay within the existing client → driver HTTP trust boundary documented in the plan's `<threat_model>`. The threat register's T-08-03-01..05 are all `mitigate` dispositions and the mitigations are in place:
- T-08-03-01 (last_error info disclosure) — local-only HTTP via AssertHttpServerLocalhostOnly; lint stays GREEN.
- T-08-03-02 (devices info disclosure) — same local-only mitigation.
- T-08-03-03 (devices poll-storm DoS) — 1 s cache inside HttpServer (Pattern note: cache lives in HttpServer member fields, not DeviceProvider lambda — see Decisions).
- T-08-03-04 (DriverState COW tamper) — `publishDriverState` reads-current-then-mutates-then-stores; documented in driver_state.hpp + device_provider.cpp.
- T-08-03-05 (iso-8601 spoof) — `parseIso8601Z` falls back to `nullopt` on `_mkgmtime/timegm` returning -1 or `get_time` failing.

## Self-Check

**Files claimed created:**
- driver/src/driver_state.hpp — FOUND
- driver/src/device_info.hpp — FOUND
- .planning/phases/08-ipc-contract-reshape/08-03-SUMMARY.md — FOUND (this file)

**Files claimed modified:**
- driver/src/device_provider.hpp — modified (stateSnapshot_ + getStateSnapshot/publishDriverState + driver_state.hpp include)
- driver/src/device_provider.cpp — modified (publishDriverState body + Init publishes DriverState{} + Init wires 4 new HttpServer callbacks + Cleanup ordering amended)
- driver/src/http_server.hpp — modified (ctor expansion + 6 callback members + cache members + new includes)
- driver/src/http_server.cpp — modified (ctor body + 4 new GET handlers + cache logic in /devices)
- driver/src/audio_worker.hpp — modified (rms_normalized atomic + accessor + enumerateDevicesForHttp + AudioDevice forward decl)
- driver/src/audio_worker.cpp — modified (rms_normalized impl + enumerateDevicesForHttp impl + RMS-on-every-callback + Start clears RMS)
- driver/src/detection_runner.hpp — modified (DriverStatePublisher typedef + 5th ctor arg + statePublisher_ + lastPublishedState_ members + extra includes)
- driver/src/detection_runner.cpp — modified (ctor inits statePublisher_ + trigger callback stamps last_trigger_at + RunLoop polls state-machine state)
- src/steamvr/include/micmap/steamvr/driver_api.hpp — modified (3 view structs + 4 new pure-virtual methods + AppConfig include)
- src/steamvr/src/driver_api.cpp — modified (parseAppConfigFromJson + parseIso8601Z helpers + 4 new method bodies)
- src/steamvr/CMakeLists.txt — modified (micmap_core PUBLIC link added)
- tests/CMakeLists.txt — modified (5 source lists expanded with config_io + config_json + driver_log_sink)

**Commits claimed:**
- 0f8a70c — FOUND
- ea63916 — FOUND
- b1a68f6 — FOUND

## Self-Check: PASSED

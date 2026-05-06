---
phase: 08
slug: ipc-contract-reshape
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-05
revised: 2026-05-05
---

# Phase 08 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> See `08-RESEARCH.md` § "Validation Architecture" for the full mapping of success criteria to automated/manual verification.
> **Revision:** populated per-task verification table from PLAN.md task IDs after planner revision (checker task_completeness warning fix).

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (existing) + ctest source-grep lints (Phase 5/6/7 pattern) + plain-main MM_CHECK exit-code tests |
| **Config file** | `tests/CMakeLists.txt`, `cmake/Assert*.cmake` |
| **Quick run command** | `cmake --build build --target test_micmap_core && ctest --test-dir build -R '08_' --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~30s quick / ~3 min full |

---

## Sampling Rate

- **After every task commit:** Run quick command (Phase 8-scoped tests + lints)
- **After every plan wave:** Run full suite
- **Before `/gsd-verify-work`:** Full suite green + manual UAT signed off
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Plan | Task | Verification Type | Mechanism / ctest target |
|------|------|-------------------|---------------------------|
| 08-00 | Task 1: Create 4 lint scripts under cmake/ | automated | `cmake -P cmake/Assert*.cmake` script-mode dry runs (each emits `clean` STATUS or FATAL) |
| 08-00 | Task 2: Land 14 RED-tolerant test scaffolds | automated | filesystem `test -f` + `grep -q "MM_CHECK"` per scaffold |
| 08-00 | Task 3: Wire lints + scaffolds into tests/CMakeLists.txt | automated | `cmake -S . -B build-headless -DMICMAP_BUILD_DRIVER=OFF` configures clean; `ctest -N -R AssertHttpServer` lists both lints |
| 08-00 | Task 4: cpp-httplib v0.14.3 -> v0.20.1 (D-05 isolated commit) | automated | `grep -c "GIT_TAG v0.20.1" external/CMakeLists.txt` == 1 AND existing v1.5 trigger tests still pass: `ctest -R "test_command_queue\|test_vr_input_quit_ordering\|test_bindings_patcher" --output-on-failure` |
| 08-01 | Task 1: IDriverClient -> IDriverApi rename + Pitfall 6 fix | automated | grep across src/, apps/, tests/, driver/ returns ZERO matches for IDriverClient/createDriverClient; `grep -q ConnectResult driver_api.hpp`; `grep -q "httplib::Error::Connection" driver_api.cpp`; `ctest -R ClientDriverLoadedIndicator --output-on-failure` |
| 08-01 | Task 2: LIB-04 logger sinks + client composition root | automated | filesystem checks for log_sink.hpp + 4 sink cpp + 1 driver sink hpp/cpp; `grep -q MultiSinkLogger apps/micmap/main.cpp`; `ctest -R MultiSinkLogger --output-on-failure` |
| 08-02 | Task 1: AppConfig JSON ADL hooks (driver + client TUs) + ConfigManagerImpl relocation + 4-root AssertNoJsonInCore | automated | `! grep nlohmann/json src/core/src/config_manager.cpp`; `test -f apps/micmap/src/config_manager_impl.cpp`; `ctest -R AssertNoJsonInCore --output-on-failure` (4-root scope: audio + detection + core + common) |
| 08-02 | Task 2: driver/src/config_io.{hpp,cpp} (3-attempt SHARING_VIOLATION retry + atomic save) | automated | `grep -q ReplaceFileW driver/src/config_io.cpp`; `grep -q ERROR_SHARING_VIOLATION driver/src/config_io.cpp`; `ctest -R "ConfigIoAtomicPersist\|InitConfigShareViolation" --output-on-failure` |
| 08-02 | Task 3: DeviceProvider snapshot + driver-side composition root + Init reads config.json | automated | `grep -q configSnapshot_ driver/src/device_provider.hpp`; `grep -q makeMultiSinkLogger driver/src/device_provider.cpp`; `ctest -R "DeviceProviderLifecycleStress\|AssertNoJsonInCore\|ConfigIoAtomicPersist\|InitConfigShareViolation" --output-on-failure` |
| 08-03 | Task 1: DriverState POD + DeviceProvider snapshot + AudioWorker RMS atomic + state event wiring | automated | `test -f driver/src/driver_state.hpp`; `grep -q stateSnapshot_ driver/src/device_provider.hpp`; `grep -q rms_normalized driver/src/audio_worker.hpp`; `grep -q DriverStatePublisher driver/src/detection_runner.hpp`; `ctest -R "DeviceProviderLifecycleStress\|DetectionSettingsPropagation\|AudioWorkerLifecycleHeadless" --output-on-failure` |
| 08-03 | Task 2: HttpServer ctor evolution + 4 GET handlers + 1-second device cache | automated | `grep -c "Get(\"/state\\\|Get(\"/settings\\\|Get(\"/devices\\\|Get(\"/telemetry/level\"" driver/src/http_server.cpp` >= 4; `grep -q enumerateDevicesForHttp driver/src/audio_worker.hpp`; `ctest -R "GetStateShape\|GetTelemetryLevel\|GetDevicesCache\|GetSettingsShape\|DeviceProviderLifecycleStress\|AssertHttpServerNoVrApi\|AssertHttpServerLocalhostOnly" --output-on-failure` (executor note: run after EVERY sub-step of Cleanup ordering changes per checker scope_sanity warning) |
| 08-03 | Task 3: IDriverApi gains 4 new client methods | automated | `grep -q DriverStateView driver_api.hpp`; `grep -q DeviceInfoView driver_api.hpp`; `grep -q TelemetryLevel driver_api.hpp`; `ctest -R "GetStateShape\|GetTelemetryLevel\|GetDevicesCache\|GetSettingsShape\|test_vr_input_quit_ordering\|ClientDriverLoadedIndicator" --output-on-failure` |
| 08-04 | Task 1: settings_validator + scaffolds GREEN | automated | `test -f driver/src/settings_validator.hpp`; `grep -c "ValidationError{" driver/src/settings_validator.cpp` >= 9; `ctest -R "SettingsValidator\|PutSettingsValidation" --output-on-failure` |
| 08-04 | Task 2: HttpServer PUT /settings + POST /state/clear-error handlers | automated | `grep -q "Put(\"/settings\"" driver/src/http_server.cpp`; `grep -q "Post(\"/state/clear-error\"" driver/src/http_server.cpp`; `grep -q configMutator driver/src/http_server.hpp`; `ctest -R "PutSettingsRoundTrip\|PutSettingsValidation\|PutSettingsStress100\|StateClearError\|GetStateShape\|GetSettingsShape\|AssertHttpServerNoVrApi\|AssertHttpServerLocalhostOnly" --output-on-failure` |
| 08-04 | Task 3: IDriverApi.putSettings + clearError | automated | `grep -q PutSettingsResult driver_api.hpp`; `grep -q "virtual PutSettingsResult putSettings" driver_api.hpp`; `ctest -R "PutSettings\|StateClearError\|SettingsValidator\|GetState\|GetSettings\|GetDevices\|GetTelemetry\|DeviceProviderLifecycleStress" --output-on-failure` |
| 08-05 | Task 1: Driver Health pane + level meter rewire + state polling loop | automated | `grep -q "Driver Health" apps/micmap/main.cpp`; `grep -q pollDriverHealth apps/micmap/main.cpp`; `grep -E "case steamvr::ConnectResult::(Connected\|NotFound\|Timeout\|OtherError)" apps/micmap/main.cpp \| wc -l` == 4 (3-way branch per checker context_compliance fix); `ctest -R "ClientLevelMeterCadence\|GetStateShape\|GetTelemetryLevel" --output-on-failure` |
| 08-05 | Task 2: Settings rewire + device picker rewire + saveDefault deletion + AssertNoConfigWriteInClient go-live | automated | `! grep saveDefault apps/micmap/`; `grep -q AssertNoConfigWriteInClient tests/CMakeLists.txt`; `ctest -R "AssertNoConfigWriteInClient\|test_tray_balloon_once\|ClientLevelMeterCadence\|GetSettingsShape\|PutSettingsRoundTrip\|PutSettingsValidation\|GetDevicesCache" --output-on-failure` |
| 08-05 | Task 3: D-27(1)..(4) manual UAT checkpoint | manual UAT | `checkpoint:human-verify` - operator runs 4 scenarios on Bigscreen Beyond + Win11 Pro rig per CONTEXT D-27(1)..(4); records PASS/FAIL evidence in 08-UAT.md (created in 08-06 Task 1) |
| 08-06 | Task 1: Scaffold 08-UAT.md + D-30 default-OFF sanity check | automated | `test -f .planning/phases/08-ipc-contract-reshape/08-UAT.md`; `! grep '"enable_driver_detection"\\s*:\\s*true' driver/resources/settings/default.vrsettings` |
| 08-06 | Task 2: Run D-27(1)..(7) + D-28 stress on real rig | manual UAT | `checkpoint:human-verify` - operator runs 7 D-27 scenarios + D-28 100-PUT stress test on Bigscreen Beyond + Win11 Pro; records evidence inline in 08-UAT.md; sign-off frontmatter `status: complete` |

---

### Success Criterion → Verification Type

| SC | Description | Type | Mechanism |
|----|-------------|------|-----------|
| SC1 | PUT /settings atomic validate→persist→publish, HTTP 400 with `{field,reason}` on bad input | automated | plain-main test against in-process http_server with mock CommandQueue + tmpdir for ReplaceFileW (PutSettingsRoundTrip + PutSettingsValidation Wave 0 scaffolds, GREEN after 08-04) |
| SC2 | Driver sole writer of config.json | automated | `AssertNoConfigWriteInClient.cmake` ctest lint (5-condition regex per checker fix: rejects saveDefault / writeAtomicWindows / ReplaceFileW / `config.json` literal coexisting with fopen/CreateFileW/MoveFileExW/WriteFile/fwrite / std::ofstream coexisting with `config.json`); GREEN after 08-05 Task 2 saveDefault deletion |
| SC3 | Client UI live indicator (red on ECONNREFUSED, green on success), pill, level meter, cadence (5 Hz visible / 0.5 Hz minimized) | automated + manual UAT | ClientLevelMeterCadence Wave 0 scaffold (4..6 calls/s visible; 0..1 calls/s tray); full integration on Bigscreen Beyond rig D-27(3) |
| SC4 | All driver routes bind to 127.0.0.1 only | automated + UAT | `AssertHttpServerLocalhostOnly.cmake` lint freezes literal; `netstat -an` UAT D-27(5) confirms at runtime |
| SC5 | HTTP handlers never call `vr::*` (SVR-05 invariant survives) | automated | `AssertHttpServerNoVrApi.cmake` ctest lint (sibling of P7's `AssertDetectionRunnerNoVrApi.cmake`) |
| SC6 | Driver/client log to separate files via injected sinks; no `#ifdef MICMAP_DRIVER_BUILD` in `micmap_core_runtime` | automated + manual | Existing P5 grep lint catches `#ifdef`; manual file-existence check D-27(6) post-UAT for both log files |

---

## Wave 0 Requirements

Wave 0 lands before any production code (RED-tolerant scaffolds — Phase 6/7 pattern). All items below are satisfied by 08-00:

- [x] `cmake/AssertHttpServerLocalhostOnly.cmake` — freezes `127.0.0.1` literal in cpp-httplib bind site (08-00 Task 1)
- [x] `cmake/AssertHttpServerNoVrApi.cmake` — sibling of `AssertDetectionRunnerNoVrApi.cmake`, freezes SVR-05 for new routes (08-00 Task 1)
- [x] `cmake/AssertNoConfigWriteInClient.cmake` — single-writer rule grep with 5-condition regex (08-00 Task 1, expanded per checker requirement_coverage fix)
- [x] `cmake/AssertNoJsonInCore.cmake` — keeps nlohmann/json out of `micmap_core_runtime` (Option C ADL placement); ctest wired in 08-02 with FULL 4-root scope per D-02 (08-00 Task 1 creates the script; 08-02 Task 1 wires the ctest)
- [x] `tests/driver/put_settings_round_trip_test.cpp` — plain-main scaffold for PUT /settings validate-persist-publish (RED-tolerant; turns GREEN in 08-04)
- [x] `tests/driver/state_clear_error_test.cpp` — RED-tolerant scaffold for `/state/clear-error` (turns GREEN in 08-04)
- [x] `tests/driver/get_state_shape_test.cpp` — RED-tolerant scaffold for `/state` (turns GREEN in 08-03)
- [x] `tests/driver/get_devices_cache_test.cpp` — RED-tolerant scaffold for `/devices` (turns GREEN in 08-03)
- [x] `tests/driver/get_telemetry_level_test.cpp` — RED-tolerant scaffold for `/telemetry/level` (turns GREEN in 08-03)
- [x] `tests/driver/get_settings_shape_test.cpp` — RED-tolerant scaffold for `/settings` (turns GREEN in 08-03)
- [x] `tests/driver/put_settings_validation_test.cpp` — RED-tolerant scaffold for PUT 400 envelope (turns GREEN in 08-04)
- [x] `tests/driver/put_settings_stress100_test.cpp` — RED-tolerant 100-PUT stress (turns GREEN in 08-04)
- [x] `tests/driver/init_config_share_violation_test.cpp` — RED-tolerant SHARING_VIOLATION 3-attempt retry (turns GREEN in 08-02)
- [x] `tests/driver/config_io_atomic_persist_test.cpp` — RED-tolerant ReplaceFileW atomic save (turns GREEN in 08-02)
- [x] `tests/test_multi_sink_logger.cpp` — RED-tolerant scaffold for `LIB-04` injected sinks (turns GREEN in 08-01)
- [x] `tests/test_settings_validator.cpp` — RED-tolerant scaffold for validator first-failed-field (turns GREEN in 08-04)
- [x] `tests/test_client_driver_loaded_indicator.cpp` — RED-tolerant scaffold for `Error::Connection` -> NotFound (turns GREEN in 08-01)
- [x] `tests/test_client_level_meter_cadence.cpp` — RED-tolerant scaffold for IsIconic-gated 5 Hz / 0.5 Hz cadence (turns GREEN in 08-05)
- [x] CTest registrations for all of the above (08-00 Task 3; AssertNoJsonInCore + AssertNoConfigWriteInClient deferred to 08-02 / 08-05 respectively)

*All scaffolds compile and run RED until production code lands. Phase 7 precedent: same pattern, no surprises. Per 08-00 Task 4, the cpp-httplib v0.14.3 -> v0.20.1 bump also lands in Wave 0 as an isolated commit per CONTEXT D-05.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| ImGui live driver-loaded indicator color flip on real driver | HEALTH-01 / SC3 | Real ImGui frame rendering can't be unit-tested cleanly without a D3D11 fixture | Launch `micmap.exe` with driver loaded → green; quit driver → red within 1 poll cycle (D-27(3)) |
| Detection-state pill update | HEALTH-03 / SC3 | Same — visual ImGui validation | Trigger detection in driver, observe pill cycles armed→triggered→armed |
| Level meter cadence (5 Hz visible / 0.5 Hz minimized) | HEALTH-06 / SC3 | Cadence visible only at runtime; the unit-test scaffold ClientLevelMeterCadence covers the call-count assertion (4..6 / 0..1 calls per second) but full visual confirmation belongs in D-27(3) | Observe meter at ~5 Hz when window open; minimize to tray, confirm poll drops to ~0.5 Hz via driver log timestamps |
| Last-trigger-relative timestamp updates | HEALTH-04 / SC3 | Wall-clock formatting validation | Trigger detection, observe "X s ago" / "X min ago" tick |
| `netstat -an` shows only `127.0.0.1` binds | IPC-07 / SC4 | Runtime port-binding observation | D-27(5): run driver, run `netstat -an | findstr <port>`, confirm no `0.0.0.0` |
| `%APPDATA%\MicMap\micmap-driver.log` and `micmap.log` written separately | LIB-04 / SC6 | Filesystem state check | D-27(6): trigger driver activity + client activity, confirm both files exist with non-zero size and distinct content |
| HMD wake/sleep cycle does not strand the IPC server | (regression) | Timing-sensitive; covered by P6/P7 manual UAT pattern | Sleep HMD → wake → `GET /state` still returns within 1s |
| 100-PUT stress (D-28) | IPC-04 + Pitfall 2 atomic-persist | Process Explorer handle-count delta cannot be asserted in-process | D-28: 100 curl-PUT loop; observe handle delta < 10; final config.json parses cleanly |
| cpp-httplib bump regression (D-27(7)) | D-05 / D-06 | The wire-format regression check requires the full v1.5 trigger pipeline (POST /button → CommandQueue → /input/system/click) on a real HMD | Stop client; with driver running, `curl -X POST -d '{"kind":"tap"}' http://127.0.0.1:27015/button`; SteamVR dashboard toggles 3/3 |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or are checkpoint:human-verify (D-27 manual UAT scenarios)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify (08-05 Task 3 + 08-06 Task 2 are the only checkpoints; both are bounded between automated tasks)
- [x] Wave 0 covers all MISSING references (14 RED-tolerant scaffolds + 4 lint scripts; cpp-httplib bump landed in 08-00 Task 4)
- [x] No watch-mode flags
- [x] Feedback latency < 30s (quick command); full suite ~3 min
- [x] `nyquist_compliant: true` set in frontmatter (post-revision audit complete)

**Approval:** ready for execution — per-task verification table populated; Wave 0 scaffolds + lint scripts committed; AssertNoConfigWriteInClient lint regex expanded (5-condition disjunction per checker requirement_coverage fix); AssertNoJsonInCore wired with FULL 4-root scope per D-02 (08-02 Task 1); cpp-httplib bump moved to 08-00 Task 4 per checker scope_reduction fix on D-05 isolation; pollDriverHealth in 08-05 implements 3-way ConnectResult branch per checker context_compliance fix (NotFound -> red, Timeout -> keep prior, Connected -> green, OtherError -> red); 08-06 requirements field renamed to requirements_verified (UAT-only plan does not own production reqs) and IPC-06 removed from P8 entirely (re-mapped to P9 in ROADMAP/REQUIREMENTS).

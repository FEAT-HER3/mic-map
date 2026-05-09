# Roadmap: MicMap — v1.6 Feature Migration

**Defined:** 2026-04-30
**Granularity:** standard
**Coverage:** 45/45 v1.6 requirements mapped (LIB-01..04, MIG-01..06, IPC-01..08, HEALTH-01..08, TRAIN-01..06, TEST-01..05, FAIL-01..05, INST-09, DOC-01..02)
**Phase numbering:** continues from v1.5 (Phase 4 was the last shipped phase). v1.6 phases 5–11.

**Milestone goal:** Relocate WASAPI audio capture, FFT detection, state machine, config persistence, and the trigger pipeline from `micmap.exe` into `driver_micmap.dll`. Extract a shared static library so the same source compiles into driver, client, and `mic_test.exe` headless harness. Demote the client to a settings + driver-health UI. Roll in v1.5 Phase 5 documentation carryover.

**Load-bearing invariant (from v1.5 SVR-05):** HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. No phase may violate this. Detection thread becomes a new producer for the same CommandQueue; the boundary is unchanged.

## Milestones

- ✅ **v1.5 Seamless SteamVR Integration** — Phases 1-4 (shipped 2026-04-24) — see [`milestones/v1.5-ROADMAP.md`](milestones/v1.5-ROADMAP.md)
- 🚧 **v1.6 Feature Migration** — Phases 5-11 (in progress)

## Phases

<details>
<summary>✅ v1.5 Seamless SteamVR Integration (Phases 1-4) — SHIPPED 2026-04-24</summary>

- [x] Phase 1: Driver Sidecar Migration (5/5 plans) — completed 2026-04-23
- [x] Phase 2: Config Read-Back (3/3 plans) — completed 2026-04-23
- [x] Phase 3: Auto-Start (7/7 plans) — completed 2026-04-23
- [x] Phase 4: Installer (9/9 plans) — completed 2026-04-24

Full archive: [`milestones/v1.5-ROADMAP.md`](milestones/v1.5-ROADMAP.md)
Audit: [`milestones/v1.5-MILESTONE-AUDIT.md`](milestones/v1.5-MILESTONE-AUDIT.md)

</details>

### 🚧 v1.6 Feature Migration (Active)

- [x] **Phase 5: Shared Library Extraction** (3/3 plans) — completed 2026-05-02; `micmap_core_runtime` INTERFACE target landed; driver/client/mic_test linked; configure-time guard + CTest lints active; SC5 UAT signed off byte-identical on Bigscreen Beyond.
- [x] **Phase 6: Driver-Side Audio Capture Spike** (4/4 plans) — completed 2026-05-03; WASAPI capture validated inside `driver_micmap.dll` on real Bigscreen Beyond + Win11 Pro (D-17(1)-(4) all PASS, spike GO); AudioWorker pattern (MTA worker thread, `weak_ptr<State>` UAF guard, 2s watchdog teardown) shipped behind `enable_driver_audio` flag default OFF.
- [x] **Phase 7: Driver-Side Detection Thread** — Detection runs in-process inside the driver; trigger collapses to direct CommandQueue push; client-side detection still active behind feature flag. (UAT GO 2026-05-04)
- [ ] **Phase 8: IPC Contract Reshape** — New endpoints (`/state`, `/settings`, `/devices`, `/telemetry/level`); driver becomes sole `config.json` writer; client UI surfaces driver health by polling.
- [ ] **Phase 9: Training Migration** — Driver becomes sole microphone owner during training; client is observer-only; `training_data.bin` ownership transfers to driver; `mic_test.exe --replay` lands.
- [ ] **Phase 10: Cutover & Cleanup** — Flip the flag, delete `POST /button` and `IDriverClient::tap()`, ship FAIL cluster, tray-icon state glyphs, `--debug-trigger`, installer co-versioning bake.
- [ ] **Phase 11: Documentation** — README sync (DOC-01) + `docs/architecture.md` (DOC-02) reflect post-migration architecture.

## Phase Details

### Phase 5: Shared Library Extraction
**Goal**: A new `micmap_core_runtime` INTERFACE library aggregates the existing static libs (`micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`) into a single boundary that compiles into the driver DLL, the client EXE, and `mic_test.exe` with no OpenVR/ImGui/D3D11/cpp-httplib symbols leaking into the shared layer.
**Depends on**: Nothing (first phase of v1.6; v1.5 shipped)
**Requirements**: LIB-01, LIB-02, LIB-03
**Success Criteria** (what must be TRUE):
  1. `driver_micmap.dll`, `micmap.exe`, and `mic_test.exe` all link `micmap_core_runtime`; clean build succeeds with `-DMICMAP_BUILD_DRIVER=OFF` and produces a working `mic_test.exe` even with OpenVR SDK absent (headless invariant preserved).
  2. CI grep check (`cmake/AssertNoOpenVRInCore.cmake`) fails the build if any target inside `micmap_core_runtime` transitively links `openvr_api` or includes `<openvr.h>`/`<openvr_driver.h>`.
  3. `dumpbin /exports driver_micmap.dll` shows exactly one exported symbol (`HmdDriverFactory`); no shared-lib symbols leaked through the DLL boundary.
  4. `grep -rn 'MICMAP_DRIVER_BUILD' src/audio/ src/detection/ src/core/ src/common/` returns zero hits — no compile-time host-switching inside the shared lib.
  5. Driver behavior is byte-for-byte identical to v1.5 (driver links `micmap_core_runtime` but does not yet *use* it); v1.5 UAT scenarios still pass on Bigscreen Beyond rig.
**Plans**: 3 plans
- [x] 05-01-PLAN.md — Wave 1: configure-time guard (cmake/AssertNoOpenVRInCore.cmake) + CTest source-grep lints (no-OpenVR, no-MICMAP_DRIVER_BUILD); wire include() at D-04 splice point in root CMakeLists.txt
- [x] 05-02-PLAN.md — Wave 2: replace micmap_lib with micmap_core_runtime in src/CMakeLists.txt (drop micmap_steamvr, add cxx_std_17 INTERFACE, micmap::core_runtime ALIAS); link driver_micmap PRIVATE (D-10/D-11)
- [x] 05-03-PLAN.md — Wave 2: relink mic_test (D-08, SC1 acceptance probe) and micmap (D-09); run full build matrix (driver-on + headless); SC5 manual UAT on Bigscreen Beyond
**Research flag**: STANDARD — well-documented CMake INTERFACE pattern; `micmap_bindings` precedent already in-tree from v1.5 Phase 4 D-10 lift.

### Phase 6: Driver-Side Audio Capture Spike
**Goal**: WASAPI capture proven feasible inside the `vrserver.exe` DLL host on real Bigscreen Beyond + Windows 11 hardware. Validates the highest-risk unknown of the milestone before any detection/IPC work begins. Behind a `enableDriverAudio` flag, default OFF.
**Depends on**: Phase 5
**Requirements**: MIG-01
**Success Criteria** (what must be TRUE):
  1. With `enableDriverAudio=1`, `vrserver.txt` shows the driver's audio worker thread successfully calling `CoInitializeEx(COINIT_MULTITHREADED)`, opening the configured WASAPI capture device, and logging the first 1 second of RMS readings on real Bigscreen Beyond + Win11 Pro rig.
  2. `RPC_E_CHANGED_MODE` (`0x80010106`) is handled distinctly in driver logs (treated as "this thread already in another apartment — bail out and start own thread"), not silently ignored.
  3. WASAPI capture lifecycle is owned by a dedicated audio worker thread that the driver constructs in `DeviceProvider::Init` and joins in `Cleanup`; `CoInitializeEx` is never called on the calling/RunFrame thread.
  4. With `enableDriverAudio=0` (default), driver behavior is identical to Phase 5; SteamVR start/stop is healthy and the v1.5 trigger path through HTTP `POST /button` still works.
  5. `IMMNotificationClient` is registered on the audio worker thread and unregistered cleanly in `Cleanup`; callbacks check an `atomic<bool> alive` flag before dereferencing driver state (Pitfall 13 mitigation).
**Plans**: 4 plans
- [x] 06-01-PLAN.md — Wave 0: cmake/AssertAudioWorkerNoVrApi.cmake + tests/driver/audio_worker_lifecycle_headless.cpp + ctest registrations (RED-tolerant scaffold)
- [x] 06-02-PLAN.md — Wave 1: AudioWorker class (driver/src/audio_worker.{hpp,cpp}) + default.vrsettings enable_driver_audio flag + driver/CMakeLists.txt source registration
- [x] 06-03-PLAN.md — Wave 2: device_provider.{hpp,cpp} Init flag-read + conditional AudioWorker construction (D-14) + Cleanup audioWorker_.reset() FIRST (D-13)
- [x] 06-04-PLAN.md — Wave 3: D-17 manual real-hardware UAT on Bigscreen Beyond + Win11 Pro (autonomous: false; (1) flag-ON capture / SC1, (2) HMD wake/sleep ×2, (3) SteamVR-restart-without-quit / SC2/SC5, (4) flag-OFF regression / SC4); 06-UAT.md sign-off
**Research flag**: NEEDS VALIDATION — WASAPI inside vrserver DLL host validated once in sister project `bey-closer-t1` but not in this driver. Real-hardware spike on Bigscreen Beyond + Win11 Pro is mandatory before Phase 7. If WASAPI fails in DLL context, escalate before proceeding.

### Phase 7: Driver-Side Detection Thread
**Goal**: Full in-process detection pipeline runs inside `driver_micmap.dll`: a `DetectionRunner` thread drains an SPSC sample ring from the WASAPI callback, runs FFT + state machine, and emits `TapCommand` directly into the existing `CommandQueue`. Trigger collapses from 7 hops cross-process to 4 hops in-process. Behind `enableDriverDetection` flag (default OFF) so client-side detection and `POST /button` remain the rollback path.
**Depends on**: Phase 6
**Requirements**: MIG-02, MIG-03, MIG-04, MIG-06
**Success Criteria** (what must be TRUE):
  1. With `enableDriverDetection=1` on the Bigscreen Beyond rig, covering the microphone toggles the SteamVR dashboard via the in-process trigger path (`detection_runner.cpp` → `commandQueue_->push(TapCommand{})` → `RunFrame::UpdateBooleanComponent`); zero HTTP traffic on the trigger path during the test.
  2. CI grep check confirms `VRDriverInput`, `VRProperties`, `VRServerDriverHost`, `VRSettings` only appear in `driver/src/device_provider.cpp` and `driver/src/manifest_registrar.cpp` — never in `detection_runner.cpp`, `sample_ring.hpp`, or any audio TU (Pitfall 3 mitigation).
  3. Detection runs continuously while SteamVR is running regardless of HMD activation state; `EnterStandby` pauses the detection thread cleanly and `LeaveStandby` resumes; HMD sleep/wake cycle does not crash vrserver or strand audio device handles.
  4. 50-cycle Init→500ms→Cleanup stress test passes with zero leaked handles in Process Explorer; `IMMNotificationClient` is unregistered before COM `Release()` on every Cleanup; reverse-order teardown verified (detection thread → audio worker → HTTP server) (Pitfall 4).
  5. `PUT /settings` (sensitivity, threshold, cooldown) propagates to the detection hot path within 50 ms via lock-free `std::atomic<std::shared_ptr<const AppConfig>>` snapshot; no audio-thread blocking measured under stress.
**Plans**: 6 plans
- [ ] 07-01-PLAN.md — Wave 0: SPSC ring header (sample_ring.hpp) + AssertDetectionRunnerNoVrApi.cmake sibling lint + RED-tolerant test scaffolds (DetectionSettingsPropagation, DeviceProviderLifecycleStress) + ctest registrations + test_command_queue two-producer extension
- [ ] 07-02-PLAN.md — Wave 1: default.vrsettings 5 new keys (enable_driver_detection + 4 detection_* tunables; D-13/D-27 default-OFF)
- [ ] 07-03-PLAN.md — Wave 2: DetectionRunner class (detection_runner.{hpp,cpp}) — thread loop, MIG-06 atomic-snapshot publish/load, idempotent Pause/Resume, 2s watchdog Stop, no vr::* surface (D-17/D-18/D-22/D-23)
- [ ] 07-04-PLAN.md — Wave 3: DeviceProvider Init/Cleanup/Standby splice + AudioWorker callback rewire (push to ring + NotifyOne; weak_ptr UAF guard preserved; RMS log gated by MICMAP_DEBUG_RMS_LOG; D-05/D-13/D-19/D-20/D-21)
- [ ] 07-05-PLAN.md — Wave 4: /health JSON gains driver_detection_active (D-09) + IDriverClient::isDriverDetectionActive with 1s cache + MicMapApp::onTrigger suppression gate (D-10/D-11)
- [ ] 07-06-PLAN.md — Wave 5: 07-UAT.md scaffold + manual D-25(1)..D-25(6) sign-off on Bigscreen Beyond + Win11 Pro + post-UAT default-OFF restore (D-25/D-26/D-27)
**Research flag**: STANDARD for threading pattern (CommandQueue boundary preserved from v1.5); NEEDS VALIDATION for real-hardware trigger latency and HMD sleep/wake cycle behavior.

### Phase 8: IPC Contract Reshape
**Goal**: New IPC surface — `GET /state`, `GET /telemetry/level`, `GET /devices`, `GET /settings`, `PUT /settings`, plus a `POST /state/clear-error` endpoint. Driver becomes the sole writer of `%APPDATA%\MicMap\config.json` (single-writer rule, file-watching rejected per Pitfall 5). `IDriverClient` renamed to `IDriverApi`. Client UI surfaces driver health by polling. Logger sinks injected at construction (LIB-04). `POST /button` persists until Phase 10.
**Depends on**: Phase 5 (driver-side endpoints can be built before Phase 7 lands; full integration after Phase 7)
**Requirements**: IPC-01, IPC-02, IPC-03, IPC-04, IPC-05, IPC-07, IPC-08, LIB-04, HEALTH-01, HEALTH-02, HEALTH-03, HEALTH-04, HEALTH-05, HEALTH-06, HEALTH-07
**Success Criteria** (what must be TRUE):
  1. Editing the sensitivity slider in the client UI sends `PUT /settings` with the full `AppConfig`; driver validates atomically (HTTP 400 with `{"field":"...","reason":"..."}` on rejection, no partial state mutation), persists via `ReplaceFileW`, and the new value is observable in `GET /settings` on the next poll.
  2. Driver is the sole writer of `config.json`: `grep -rn 'config.json' apps/micmap/src/ src/steamvr/src/` shows no client-side write path; client edits flow exclusively through `PUT /settings`.
  3. Client UI shows live driver-loaded indicator (red on `ECONNREFUSED`, green on success), detection-state pill, last-trigger-relative timestamp, and a 5 Hz RMS/dBFS level meter when visible (0.5 Hz when minimized to tray); ECONNREFUSED *is* the canonical "driver down" signal — no separate liveness ping exists.
  4. `netstat -an` confirms every driver HTTP route (`/health`, `/port`, `/state`, `/settings`, `/devices`, `/telemetry/level`) binds to `127.0.0.1` only — never `0.0.0.0` (Pitfall 7 mitigation).
  5. The HTTP-thread → CommandQueue → RunFrame v1.5 SVR-05 boundary survives unchanged: HTTP handlers for `PUT /settings` mutate the atomic snapshot directly (data-only) and never call any `vr::*` API; verified by inspection.
  6. Driver writes its own log to `%APPDATA%\MicMap\micmap-driver.log` via injected `FileLogSink`; client writes to `%APPDATA%\MicMap\micmap.log` via injected `StdoutLogSink`+`FileLogSink`; no `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime` (LIB-04).
**Plans**: 7 plans
- [x] 08-00-PLAN.md — Wave 0: 4 new CMake source-grep lints (AssertNoJsonInCore, AssertHttpServerLocalhostOnly, AssertHttpServerNoVrApi, AssertNoConfigWriteInClient) + 14 RED-tolerant test scaffolds + EXISTS-gated ctest registrations
- [x] 08-01-PLAN.md — Wave 1: cpp-httplib v0.14.3 → v0.20.1 (D-05); IDriverClient → IDriverApi rename + Pitfall 6 ECONNREFUSED-vs-timeout fix (D-22); LIB-04 logger sinks (ILogSink + FileLogSink + StdoutLogSink + DriverLogSink + MultiSinkLogger); client WinMain composition root
- [x] 08-02-PLAN.md — Wave 2: nlohmann/json into driver TUs (D-01) + AssertNoJsonInCore lint go-live (3-of-4 roots; src/core deferred to P11) + driver-side config_io.{hpp,cpp} (loadConfigJson 3-attempt SHARING_VIOLATION retry + saveConfigJson via ReplaceFileW, D-10/D-14) + DeviceProvider AppConfig snapshot member + driver-side composition root logger
- [x] 08-03-PLAN.md — Wave 3: GET /state, /settings, /devices, /telemetry/level (read-side IPC); HttpServer ctor evolves to 5 read callbacks; AudioWorker rms_normalized atomic + enumerateDevicesForHttp; DeviceProvider stateSnapshot_ + DetectionRunner state-event publisher; IDriverApi 4 new client methods
- [x] 08-04-PLAN.md — Wave 4: settings_validator (D-14/D-15); PUT /settings with Pitfall 2 persist-first; POST /state/clear-error (HEALTH-05/D-16); HttpServer ctor extends to 7 callbacks; IDriverApi.putSettings + clearError
- [x] 08-05-PLAN.md — Wave 5: client UI Driver Health pane (HEALTH-01..07); settings + device picker rewire through PUT /settings (D-09 ladder); driver-loaded gate; saveDefault deletion + AssertNoConfigWriteInClient ctest go-live (single-writer cutover); manual UAT D-27(1)..(4) checkpoint
- [x] 08-06-PLAN.md — Wave 6: full UAT D-27(1)..(7) + D-28 stress on Bigscreen Beyond + Win11 Pro; success-criteria audit; default.vrsettings enable_driver_detection=false preservation (D-30); 08-UAT.md sign-off
**UI hint**: yes
**Research flag**: STANDARD — extends existing cpp-httplib pattern from v1.5 Phase 1; same CommandQueue discipline.

### Phase 9: Training Migration
**Goal**: Driver becomes the sole owner of the microphone end-to-end during training; client becomes the observer that visualizes progress and confirms thresholds. New endpoints: `POST /training/start`, `GET /training/progress`, `POST /training/finalize`, `POST /training/cancel`, `POST /training/recompute`. `training_data.bin` ownership transfers to driver. `mic_test.exe --replay <wav>` enables reproducible regression testing.
**Depends on**: Phase 7 (driver owns audio), Phase 8 (IPC surface with `/state` and `/settings`)
**Requirements**: TRAIN-01, TRAIN-02, TRAIN-03, TRAIN-04, TRAIN-05, TRAIN-06, TEST-04, IPC-06
**Success Criteria** (what must be TRUE):
  1. User clicks "Train" in the client; driver enters training mode (detection mutex-paused), collects ~150 samples while client polls `GET /training/progress` at 5–10 Hz and renders a live progress bar; on `POST /training/finalize` driver writes `training_data.bin` atomically and returns to detection mode using the new thresholds — verified end-to-end on real Bigscreen Beyond + Win11 hardware.
  2. Anti-feature TRAIN-AF-01 enforced: client never opens its own WASAPI capture during training; `grep -rn 'IAudioCapture\|createAudioCapture' apps/micmap/src/` returns no calls to `start()` during training mode (single-owner WASAPI invariant).
  3. `POST /training/cancel` aborts an in-flight session, discards collected samples, returns the driver to detection mode without modifying `training_data.bin`; `POST /training/recompute {"sensitivity":0.7}` recomputes thresholds over the most-recent stored sample set without re-collecting and returns a preview the client can confirm or discard.
  4. `mic_test.exe --replay <path-to-wav>` feeds a WAV file into the detection pipeline as if it were live mic input; reproducible regression test against a corpus of known-positive and known-negative samples emits the expected count of triggers (verified for at least one positive and one negative sample).
  5. Driver is the sole writer of `training_data.bin`: client UI never touches the file directly; driver reads at `Init` and writes only on `POST /training/finalize`.
**Plans**: 6 plans
- [x] 09-00-PLAN.md — Wave 0: cmake/AssertNoClientTraining.cmake + cmake/AssertReplayNoVrApi.cmake + 3 RED-tolerant test scaffolds (training_session_test, training_endpoint_validation_test, wav_replay_test) + EXISTS-gated ctest registrations (D-37 / D-38)
- [x] 09-01-PLAN.md — Wave 1: TrainingSession class (driver/src/training_session.{hpp,cpp}) + training_io.{hpp,cpp} (ReplaceFileW + corruption-backup ring) + DetectionRunner DriverMode atomic-load + per-iter branch + DeviceProvider lazy unique_ptr<TrainingSession> wiring (D-01..D-04, D-09..D-22, D-23, D-27)
- [x] 09-02-PLAN.md — Wave 2: 5 new HTTP routes (POST /training/start, GET /training/progress, POST /training/finalize, POST /training/cancel, POST /training/recompute) + GET /health driver_training_active field + IDriverApi 5 new methods + settings_validator training payload validators (D-07, D-09, D-13..D-22, D-40)
- [x] 09-03-PLAN.md — Wave 3: client UI training pane rewire — DELETE apps/micmap/main.cpp:91-92 + 403-405 + 617-618 + 953-1034 (planner-estimated 962-1027 + :404 + :618 + :974 + :991 + :91; as-executed range expanded by 5 lines for the section header); INSERT endpoint-driven Training pane per 09-UI-SPEC.md (Train Pattern / Cancel Training / Recompute Thresholds / Confirm & Save / Discard Preview / Discard Profile) + 5 Hz GET /training/progress poll + GET /health full-envelope + Discard Profile destructive modal; AssertNoClientTraining ctest go-live (single-writer cutover; lint regex narrowed to `detector->X` qualifier-form per Rule-3 deviation) (D-05, D-23) — see 09-03-SUMMARY.md
- [x] 09-04-PLAN.md — Wave 4: vendor/dr_wav/dr_wav.h + apps/mic_test/src/wav_replay.{hpp,cpp} + 9 new mic_test CLI flags + tests/corpus/replay/ seed (3 WAVs + manifest.json + README.md) + mic_test_replay_corpus ctest + AssertReplayNoVrApi go-live (D-28..D-37) — see 09-04-SUMMARY.md
- [ ] 09-05-PLAN.md — Wave 5: 09-UAT.md scaffold + manual D-39(1)..(10) sign-off on Bigscreen Beyond + Win11 Pro + post-UAT default-OFF flag restore (D-39, D-40)
**UI hint**: yes
**Research flag**: NEEDS VALIDATION — training UX commit/discard pattern designed from first principles (no v1.5 prior art); validate with a real training session on hardware before declaring phase done.

### Phase 10: Cutover & Cleanup
**Goal**: All paths proven in prior phases. Flip `enableDriverDetection` default to ON, delete `POST /button` and `IDriverClient::tap()`, delete the client-side WASAPI/FFT/state-machine body from `apps/micmap/main.cpp` (~500 LoC reduction). Ship the FAIL cluster (graceful failure modes), tray-icon state glyphs (HEALTH-08), `--debug-trigger` CLI flag, and the installer co-versioning bake (INST-09). Largest code-deletion phase of the milestone.
**Depends on**: Phase 9
**Requirements**: MIG-05, FAIL-01, FAIL-02, FAIL-03, FAIL-04, FAIL-05, HEALTH-08, TEST-01, TEST-02, TEST-03, TEST-05, INST-09
**Success Criteria** (what must be TRUE):
  1. After cutover, `grep -rn 'IDriverClient::tap\|/button\|TapCommand' apps/ src/steamvr/` returns zero hits in client code; the `POST /button` route is removed from `driver/src/http_server.cpp`; client EXE binary size drops noticeably (KissFFT and audio capture no longer linked into client).
  2. Tray icon glyph/color reflects driver state on the desktop — armed (green), triggered (pulse), error (red) — and updates within one health-poll cycle of an actual state change on the Bigscreen Beyond rig (HEALTH-08).
  3. Failure modes show actionable, user-visible UX: mic permission denied → "Mic access blocked" with deep-link to Windows mic settings (FAIL-01); driver not loaded → "Driver not installed — run installer or enable in SteamVR" (FAIL-02); SteamVR not running → "SteamVR not running" with 1 Hz polite poll, no retry storm (FAIL-03); double client instance → second instance foregrounds the first via named mutex and exits silently (FAIL-04); audio device removed mid-session → driver pauses with a clear `last_error` and auto-recovers when the device reappears (FAIL-05).
  4. Installer (`MicMap-Setup-vX.Y.Z.exe`) places driver, client, vrmanifest, and shared-lib artifacts in lock-step; installing then upgrading then uninstalling on a clean Win11 VM cleans the matched set; client logs a warning at startup if `GET /health` reports a driver version mismatch with the client's compiled-in version (INST-09).
  5. `mic_test.exe` continues to build and run against `micmap_core_runtime` with no SteamVR/driver dependency (TEST-01); `micmap.exe --debug-trigger` issues a synthetic trigger via a debug-build-gated endpoint without going through audio (TEST-02); driver writes `micmap-driver.log` with size-cap rotation at 5 MB and 5 retained generations (TEST-03); `hmd_button_test.exe` is preserved as a developer tool (TEST-05).
  6. v1.5 SVR-05 invariant survives: full grep audit confirms `VRDriverInput`/`VRProperties`/`VRServerDriverHost` calls only appear in `device_provider.cpp` and `manifest_registrar.cpp`; HMD sleep/wake stress test passes on Bigscreen Beyond.
**Plans**: TBD
**UI hint**: yes
**Research flag**: STANDARD — deletion phase; v1.5 SVR-04 rip-out discipline (eliminate fallback paths, no dual-mode runtime) is the template.

### Phase 11: Documentation
**Goal**: README and architecture documentation reflect the post-migration shipped reality (driver runs detection end-to-end; client is settings + health UI; install via single `.exe`; no batch scripts; auto-launch is SteamVR-native via `app.vrmanifest`). Carries forward the v1.5 Phase 5 commitment, re-scoped against v1.6 architecture.
**Depends on**: Phase 10
**Requirements**: DOC-01, DOC-02
**Success Criteria** (what must be TRUE):
  1. `README.md` reflects the v1.6 reality (driver hosts detection end-to-end; client is settings + health UI; install via single `.exe`; auto-launch via SteamVR-native `app.vrmanifest`); all crossed-out v1.5 sections removed.
  2. New `docs/architecture.md` documents the post-migration architecture: driver-resident detection pipeline, `micmap_core_runtime` shared library boundary, OpenVR `RunFrame` + `CommandQueue` boundary, settings/health/training IPC contract, `training_data.bin` ownership, HMD reactivation lifecycle, and the v1.5 sidecar-on-HMD `/input/system/click` technique.
  3. Diagrams cover both the in-process trigger path (4 hops) and the IPC reshape (settings push, health pull, training coordination); a fresh reader can understand the architecture without reading v1.5 milestone artifacts.
**Plans**: TBD
**Research flag**: STANDARD — writing docs, no technical unknowns.

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Driver Sidecar Migration | v1.5 | 5/5 | Complete | 2026-04-23 |
| 2. Config Read-Back | v1.5 | 3/3 | Complete | 2026-04-23 |
| 3. Auto-Start | v1.5 | 7/7 | Complete | 2026-04-23 |
| 4. Installer | v1.5 | 9/9 | Complete | 2026-04-24 |
| 5. Shared Library Extraction | v1.6 | 3/3 | Complete | 2026-05-02 |
| 6. Driver-Side Audio Capture Spike | v1.6 | 4/4 | Complete | 2026-05-03 |
| 7. Driver-Side Detection Thread | v1.6 | 0/6 | Planned | — |
| 8. IPC Contract Reshape | v1.6 | 0/7 | Planned | — |
| 9. Training Migration | v1.6 | 4/6 | In Progress|  |
| 10. Cutover & Cleanup | v1.6 | 0/0 | Not started | — |
| 11. Documentation | v1.6 | 0/0 | Not started | — |

## Phase Ordering Rationale

- **Phase 5 must be first.** Without the shared lib boundary, every other phase duplicates code or breaks the `mic_test.exe` headless invariant. This is the keystone refactor (Pitfalls 6, 9, 15 all addressed here).
- **Phase 6 before Phase 7.** WASAPI inside vrserver DLL host is the highest-risk unknown of the milestone. Validate feasibility on real hardware before building detection on top of it. If it fails, escalate before Phase 7.
- **Phase 7 before Phase 8 IPC integration cutover.** Driver-side detection must work end-to-end with the in-process trigger path before client UI gets rewired to the new IPC surface (otherwise we have new endpoints sitting unused while client still uses `POST /button`).
- **Phase 8 before Phase 9.** Training endpoints depend on `/state` and `/settings` IPC surface plus the driver-as-sole-writer rule.
- **Phase 9 before Phase 10.** Cutover deletes the client-side training path; without Phase 9, users would lose the ability to retrain after the flag flip.
- **Phase 10 before Phase 11.** Docs describe shipped reality, not aspirational architecture.

## Coverage Notes

All 45 v1.6 requirements mapped to exactly one phase. No orphans, no duplicates.

| Cluster | Count | Phases |
|---------|-------|--------|
| LIB (4) | 4 | LIB-01..03 → P5; LIB-04 → P8 |
| MIG (6) | 6 | MIG-01 → P6; MIG-02..04, MIG-06 → P7; MIG-05 → P10 |
| IPC (8) | 8 | IPC-01..05, IPC-07..08 → P8; IPC-06 → P9 |
| HEALTH (8) | 8 | HEALTH-01..07 → P8; HEALTH-08 → P10 |
| TRAIN (6) | 6 | TRAIN-01..06 → P9 (with IPC-06 grouped here) |
| TEST (5) | 5 | TEST-04 → P9; TEST-01, TEST-02, TEST-03, TEST-05 → P10 |
| FAIL (5) | 5 | FAIL-01..05 → P10 |
| INST (1) | 1 | INST-09 → P10 |
| DOC (2) | 2 | DOC-01, DOC-02 → P11 |
| **Total** | **45** | **45 mapped, 0 orphans** |

---

*Roadmap defined: 2026-04-30 — derived from REQUIREMENTS.md (45 v1.6 reqs) and `.planning/research/` (SUMMARY.md proposed 7-phase shape, refined here to align dependencies and pitfall mitigations from PITFALLS.md).*

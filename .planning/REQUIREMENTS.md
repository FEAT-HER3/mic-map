# Requirements: MicMap — v1.6 Feature Migration

**Defined:** 2026-04-30
**Core Value:** Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.

**Milestone goal:** Relocate audio capture, FFT detection, state machine, config persistence, and the trigger pipeline from `micmap.exe` into `driver_micmap.dll` so the driver runs end-to-end without the client. Extract a shared static library so the same source compiles into driver, client, and `mic_test.exe` headless harness. Demote the client to a settings + driver-health UI. Roll in v1.5 Phase 5 documentation carryover.

## v1.6 Requirements

Requirements for the "Feature Migration" milestone. REQ-IDs continue category numbering from v1.5 where applicable. Each requirement maps to exactly one roadmap phase.

### Shared Library (LIB)

Extract audio + detection + state machine + config schema into a shared static-library boundary consumable by driver, client, and headless harness with no SteamVR symbols crossing the seam.

- [ ] **LIB-01**: A new CMake target `micmap_core_runtime` (INTERFACE library) aggregates the existing static libs `micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`. The target carries no transitive dependency on OpenVR, ImGui, D3D11, or cpp-httplib.
- [ ] **LIB-02**: `driver_micmap.dll`, `micmap.exe`, and `mic_test.exe` all link `micmap_core_runtime`. `mic_test.exe` continues to build and run with `-DMICMAP_BUILD_DRIVER=OFF` (headless invariant).
- [ ] **LIB-03**: A CMake check (`cmake/AssertNoOpenVRInCore.cmake`) fails the build if any target inside `micmap_core_runtime` transitively links `openvr_api` or includes any OpenVR header. CI runs this on every build.
- [ ] **LIB-04**: The shared lib's `Logger` accepts an injected sink at construction. Driver build wires a `DriverLogSink` that writes via `vr::VRDriverLog()` plus a `FileLogSink` to `%APPDATA%\MicMap\micmap-driver.log`. Client build wires a `StdoutLogSink` plus a `FileLogSink` to `%APPDATA%\MicMap\micmap.log`. No `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime`.

### Driver-Resident Detection (MIG)

Move WASAPI capture, FFT detection, state machine, and the trigger pipeline into the driver process. Trigger no longer crosses IPC.

- [ ] **MIG-01**: Driver hosts a dedicated audio worker thread that owns `CoInitializeEx(COINIT_MULTITHREADED)`, opens the configured WASAPI capture device, and pushes captured frames into a single-producer/single-consumer ring buffer.
- [ ] **MIG-02**: Driver hosts a dedicated detection thread that drains the audio ring, runs FFT + RMS + state-machine logic, and emits `TapCommand` to the existing `CommandQueue` on a positive trigger. The detection thread never touches `vr::*` API directly.
- [ ] **MIG-03**: Detection runs continuously while SteamVR is running, regardless of HMD activation state. `EnterStandby` pauses detection cleanly; `LeaveStandby` resumes; HMD reactivation cycles do not leak audio device handles or threads.
- [ ] **MIG-04**: Driver `Cleanup()` tears down detection thread → audio worker thread (join + `IMMNotificationClient::Unregister` + COM Release + `CoUninitialize`) → HTTP server, in reverse construction order. A 50-cycle Init/Cleanup stress test passes with zero leaked handles (verified via Process Explorer or equivalent).
- [ ] **MIG-05**: `POST /button` HTTP route is deleted and `IDriverClient::tap()` is removed. Trigger pipeline collapses from 7 hops cross-process to 4 hops in-process.
- [ ] **MIG-06**: Detection thread reads settings via a lock-free `std::atomic<std::shared_ptr<const AppConfig>>` snapshot. Sensitivity / threshold / cooldown changes pushed through `PUT /settings` propagate to the detection hot path within 50 ms with no audio-thread blocking.

### IPC Reshape (IPC)

Drop the trigger HTTP path; add settings push, health pull, device enumeration, and training endpoints. Localhost-only binding preserved.

- [ ] **IPC-01**: `GET /state` returns a JSON document with: `driver_loaded`, `steamvr_running` (always true if endpoint reachable), `detection_state` (idle/training/detecting/triggered/cooldown), `last_trigger_at` (UTC timestamp or null), `last_error` (string or null), `audio_device_id`, `audio_device_state` (active/missing/permission_denied).
- [ ] **IPC-02**: `GET /telemetry/level` returns the current RMS / dBFS reading for the live level meter. Designed for 5–10 Hz polling with 250 ms client-side timeout.
- [ ] **IPC-03**: `GET /devices` returns the enumerated WASAPI capture device list (id, friendly name, default flag) so the client device picker reflects what the driver sees, not what the client process sees.
- [ ] **IPC-04**: `GET /settings` returns the current `AppConfig` JSON. `PUT /settings` accepts a full `AppConfig` JSON, validates atomically, applies in-memory, and persists to `config.json` via atomic `ReplaceFileW`. Validation failures return a structured error (HTTP 400 with JSON `{"field": "...", "reason": "..."}`); driver state is unchanged on rejection.
- [ ] **IPC-05**: Driver is the sole writer of `%APPDATA%\MicMap\config.json`. Client never writes the file directly; client edits flow through `PUT /settings`. Driver reads the file once at `Init` with a 3-attempt retry on `ERROR_SHARING_VIOLATION`; thereafter the in-memory snapshot is authoritative.
- [ ] **IPC-06**: Driver is the sole writer of `%APPDATA%\MicMap\training_data.bin` (after `POST /training/finalize`).
- [ ] **IPC-07**: All IPC endpoints bind to `127.0.0.1` only. `GET /port` and `GET /health` from v1.5 remain unchanged for client-side discovery and liveness.
- [ ] **IPC-08**: The HTTP-thread → RunFrame `CommandQueue` boundary (v1.5 SVR-05) survives unchanged. Detection thread is the new producer; HTTP server is no longer a producer.

### Driver-Health UI (HEALTH)

Client UI surfaces driver health by polling the new IPC endpoints.

- [ ] **HEALTH-01**: Client shows a "driver loaded" indicator that turns red when `GET /health` returns ECONNREFUSED, green otherwise. ECONNREFUSED IS the canonical "driver down" signal — no separate liveness ping.
- [ ] **HEALTH-02**: Client shows a "SteamVR running" indicator. Since `GET /health` only succeeds when the driver is loaded and SteamVR is up, this is derived from the same poll.
- [ ] **HEALTH-03**: Client shows the current detection state (idle / training / detecting / triggered / cooldown) sourced from `GET /state`.
- [ ] **HEALTH-04**: Client shows the timestamp of the last trigger (formatted relative — "3 s ago", "12 m ago", "—" if never) sourced from `GET /state.last_trigger_at`.
- [ ] **HEALTH-05**: Client shows `last_error` when non-null and offers a one-click "Clear" that calls a `POST /state/clear-error` endpoint.
- [ ] **HEALTH-06**: Client renders a live RMS / dBFS level meter polling `GET /telemetry/level` at 5 Hz when the window is visible, 0.5 Hz when minimized to tray.
- [ ] **HEALTH-07**: Client shows a clear "device disappeared" indicator when `GET /state.audio_device_state` is `missing` or `permission_denied`, with a one-click "Re-pick device" that opens the device picker.
- [ ] **HEALTH-08**: Tray icon glyph/color reflects driver state — armed (green), triggered (pulse), error (red). (HEALTH-D4 differentiator — confirmed in v1.6 scope.)

### Training Migration (TRAIN)

Driver becomes sole owner of the microphone end-to-end during training; client becomes the observer that visualizes progress and confirms thresholds.

- [ ] **TRAIN-01**: `POST /training/start` puts the driver into training mode, holds the mic, begins sample collection. Detection mode is mutex-paused while training is active.
- [ ] **TRAIN-02**: `GET /training/progress` returns `{"samples_collected": N, "target": M, "thresholds_preview": {...} | null, "state": "collecting"|"computing"|"ready"|"cancelled"}`. Client polls at 5–10 Hz to drive the training progress UI.
- [ ] **TRAIN-03**: `POST /training/finalize` accepts the final thresholds (or `confirm: true` to accept the preview) and persists `training_data.bin`. Driver returns to detection mode using the new thresholds.
- [ ] **TRAIN-04**: `POST /training/cancel` aborts an in-flight training session, discards collected samples, returns the driver to detection mode without modifying `training_data.bin`.
- [ ] **TRAIN-05**: Client never takes the mic back during training (anti-feature TRAIN-AF-01 enforced — single-owner WASAPI invariant).
- [ ] **TRAIN-06**: `POST /training/recompute` accepts a payload of `{"sensitivity": float}` and recomputes thresholds over the most-recent stored sample set without re-collecting. Returns the new threshold preview for client confirm/discard. (TRAIN-D1 differentiator — confirmed in v1.6 scope.)

### Test Affordances (TEST)

`mic_test.exe` continues to be the headless harness; new `--replay` mode enables reproducible regression testing once detection is driver-resident.

- [ ] **TEST-01**: `mic_test.exe` continues to build and run against `micmap_core_runtime` with no SteamVR / driver dependency. Test surface is identical to the driver's detection path (same source, same code).
- [ ] **TEST-02**: Main client gains a `--debug-trigger` CLI flag that issues a `POST /training/cancel`-style internal trigger (or a dedicated `POST /debug/trigger` endpoint, gated by a debug build define) for VR-input regression checks without going through audio.
- [ ] **TEST-03**: Driver writes its own log to `%APPDATA%\MicMap\micmap-driver.log` (separate file from the client's `micmap.log` to avoid cross-process file-locking). Both files rotate via atomic `ReplaceFileW`-style swap when they exceed a 5 MB size cap, retaining 5 generations.
- [ ] **TEST-04**: `mic_test.exe --replay <path-to-wav>` feeds a WAV file into the detection pipeline as if it were live mic input. Reproducible regression testing against a corpus of known-positive and known-negative samples. (TEST-D1 differentiator — confirmed in v1.6 scope.)
- [ ] **TEST-05**: `hmd_button_test.exe` is preserved as a developer tool (kept in CMake build but not promoted in user-facing docs). Useful for VR-input regression when the client is broken or pre-launch.

### Failure Modes (FAIL)

Graceful, user-visible behavior for the failure cases the migration introduces or amplifies.

- [ ] **FAIL-01**: Mic permission denied — driver logs the error, client shows "Mic access blocked" with a "Open Windows mic settings" deep-link.
- [ ] **FAIL-02**: Driver not loaded (DLL missing or `vrpathreg` unregistered) — client detects via ECONNREFUSED and shows "Driver not installed — run installer or enable in SteamVR".
- [ ] **FAIL-03**: SteamVR not running — client shows "SteamVR not running — start SteamVR to enable detection". No retry storms; client polls `GET /health` at 1 Hz when the driver is unreachable.
- [ ] **FAIL-04**: Double client instance — second instance detects the first via a named mutex, foregrounds the existing window, and exits silently.
- [ ] **FAIL-05**: Audio device removed mid-session — driver `IMMNotificationClient` callback enqueues a "rebind" command on the HTTP/RunFrame thread; detection pauses with a clear `last_error`; auto-recovers when the device reappears.

### Installer Co-Versioning (INST)

Single installer upgrades both driver and client atomically. Mismatched installs are prevented at install time.

- [ ] **INST-09**: Installer (`MicMap-Setup-vX.Y.Z.exe`) places driver, client, vrmanifest, and any new shared-lib artifacts in lock-step. Upgrading installs the matched set; uninstalling cleans the matched set. Version embedded in both binaries; client at startup logs a warning if `GET /health` reports a driver version that does not match the client's compiled-in version.

### Documentation (DOC) — v1.5 carryover

Re-scoped from v1.5 Phase 5 against the post-migration architecture.

- [ ] **DOC-01**: `README.md` reflects the v1.6 reality: driver hosts detection end-to-end; client is settings + health UI; install via single `.exe`; no batch scripts; auto-launch is SteamVR-native via `app.vrmanifest`. All crossed-out v1.5 sections removed.
- [ ] **DOC-02**: `docs/architecture.md` documents the post-migration architecture: driver-resident detection pipeline, `micmap_core_runtime` shared library, OpenVR `RunFrame` + `CommandQueue` boundary, settings/health/training IPC contract, training-data ownership, HMD reactivation lifecycle, and the v1.5 sidecar-on-HMD technique. Diagrams cover the in-process trigger path and the IPC reshape.

## Future Requirements (Deferred)

Carryover candidates not committed to v1.6:

- **OBS-01**: File-sink logger consolidation (driver + client write to a unified rotating log) — partially addressed by LIB-04 + TEST-03 (separate files); unification deferred.
- **HEALTH-D1**: Per-component health badges (mic / driver / SteamVR / IPC) with hover tooltips — deferred to a future GUI revamp milestone.
- **HEALTH-D2**: Trigger-history sparkline.
- **HEALTH-D3**: In-VR overlay status pill (UX-02).
- **TRAIN-D2**: A/B threshold preview.
- **TEST-D2**: `/debug/snapshot` driver endpoint dumping detection internals.
- **UX-01**: In-app auto-start toggle.
- **DIST-01/02/03**: Installer finished-page launch checkbox; silent-install CLI documentation; non-default Steam path support.
- **DET-01/02**: Detection-accuracy work for noisy environments.
- **CPP-HTTPLIB-BUMP**: cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728). Defer to a standalone deps-refresh plan; risk to wire format is low but non-zero.

## Out of Scope

Explicit milestone boundaries:

- **In-VR settings overlay** (UX-02) — out of v1.6 scope; the v1.5 overlay stubs in `dashboard_manager.cpp` remain as tracked tech debt.
- **Replacing localhost HTTP with named pipes / shared memory** — IPC-AF-02 anti-feature; trigger goes in-process so latency benefit disappears, no justification for the swap.
- **Driver watching `config.json` with `ReadDirectoryChangesW`** — IPC-AF-01 anti-feature; torn reads, no rejection path. Settings flow exclusively through `PUT /settings`.
- **Client-side audio fallback** (MIG-AF-02) — once cutover lands, removed entirely. Two code paths, two test matrices, drift inevitable.
- **Linux/macOS** — Windows-only by design; non-Windows audio stubs remain as-is.
- **Detection-accuracy work in noisy environments** — orthogonal to architecture migration; deferred to a future milestone.
- **`hmd_button_test.exe` retirement** — kept as developer tool per scoping decision; no plans to remove this milestone.

## Traceability

Filled by `/gsd-roadmapper` when the roadmap maps each REQ-ID to a phase.

| REQ-ID | Phase | Status |
|--------|-------|--------|
| (populated by roadmapper) | | |

---

*Last updated: 2026-04-30 — initial v1.6 requirements*

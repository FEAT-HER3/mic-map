# Feature Research — v1.6 Feature Migration

**Domain:** Driver-resident audio capture + detection + state machine + config + trigger inside a SteamVR/OpenVR driver DLL, with a thin client EXE for settings + driver-health UI
**Researched:** 2026-04-30
**Confidence:** HIGH on lifecycle and IPC contract (validated v1.5 prior art is in-tree); MEDIUM on training-mode UX and health-surface refresh cadence (no direct prior art — opinionated recommendations).

> **Note:** This file replaces the v1.5-era FEATURES.md (researched 2026-04-22). The v1.5 catalog is captured in `.planning/milestones/v1.5-REQUIREMENTS.md` and the shipped feature set is documented in `MILESTONES.md`. This document is scoped strictly to the v1.6 migration delta.

## Orientation

This is a brownfield migration milestone, not a greenfield product definition. The shipped v1.5 feature set (mic capture, detection, state machine, training, sensitivity controls, tray UI, sidecar HMD trigger, installer, auto-launch) is **not** re-listed here. The catalog below covers only what's new or different *because* the audio/detection/state/config/trigger pipeline relocates from `micmap.exe` into `driver_micmap.dll`.

Six topic clusters drive the table-stakes inventory:

| Cluster | Code-prefix | Anchor question |
|---------|-------------|-----------------|
| Driver-resident detection lifecycle | `MIG-` | When does capture start/stop relative to vrserver and HMD lifecycle? |
| Shared library extraction | `LIB-` | What audio/detection code now compiles into both driver and client? |
| Settings flow client → driver | `IPC-S` | How do settings changes apply at runtime? |
| Training flow with driver-resident audio | `TRAIN-` | Who owns the mic during training, and how do thresholds get computed and visualized? |
| Driver-health surface client ← driver | `HEALTH-` | What does the client show about the driver, and how does it stay current? |
| Headless test mode | `TEST-` | How do we develop and validate detection without SteamVR running? |
| Failure modes (cross-cluster) | `FAIL-` | What does the user see when each thing breaks? |

The two existing v1.5 IPC primitives (`POST /button {"kind":"tap"}` and `GET /health`/`GET /port`/`GET /status`) survive structurally. The trigger goes away as a wire-crossing operation (it becomes a driver-internal call). Two new traffic shapes appear: settings push (client → driver) and training-mode coordination (bidirectional).

---

## Feature Landscape

### Table Stakes (Users Expect These)

| ID | Feature | Why Expected | Complexity | Cluster | Notes |
|----|---------|--------------|------------|---------|-------|
| **MIG-01** | Driver hosts WASAPI capture loop on its own thread; `CoInitializeEx` + `IAudioClient` lifecycle aligned with `IServerTrackedDeviceProvider::Init`/`Cleanup` | The whole point of the migration: trigger pipeline runs end-to-end with no client | M | Lifecycle | Capture thread starts in `Init` after `VR_INIT_SERVER_DRIVER_CONTEXT`; stops in `Cleanup` before `VR_CLEANUP_SERVER_DRIVER_CONTEXT`. Never call WASAPI from `RunFrame` (Pitfall 12 from v1.5 carries over: RunFrame budget < 1 ms). |
| **MIG-02** | Detection runs continuously while SteamVR is running, regardless of HMD activation state | Users expect "cover mic toggles dashboard" to work the moment they put the headset on, including from cold | M | Lifecycle | Don't gate detection on HMD `state_ == Ready` — gate only the *output* (the `UpdateBooleanComponent` call). Detection produces "trigger pending" events that are no-ops when the component is `Invalidated`. Surface as a health-state warning, not silent failure. |
| **MIG-03** | Detection pauses cleanly when HMD enters standby (`EnterStandby`) and resumes on `LeaveStandby` | SteamVR explicitly signals these; ignoring them wastes CPU and risks WASAPI device contention with other apps that wake during user idle | S | Lifecycle | `IServerTrackedDeviceProvider::EnterStandby/LeaveStandby` overrides exist on the provider. Audio thread already supports stop/start; just wire it up. |
| **MIG-04** | Trigger is now a driver-internal function call, not an HTTP round-trip | Removes the HTTP hop on the hot path; one fewer thing to fail; lower latency | S | Trigger | Detection code already runs in driver process — call into `CommandQueue::push(TapCommand{})` directly, same pattern as v1.5 HTTP route. Delete `IDriverClient::tap()` and the cross-process HTTP `POST /button` route. |
| **LIB-01** | New `src/audio_detection/` (or rename) static library: WASAPI capture, FFT spectral analyzer, noise detector, pattern trainer, state machine, config types — *no* OpenVR dependency, *no* ImGui dependency | Same source compiles into driver DLL, client EXE, and `mic_test.exe` headless test harness — the migration's core architectural promise | M | Build | Move existing `src/audio/`, `src/detection/`, `src/core/state_machine`, `src/core/config_manager` into the shared lib. Do NOT include `src/steamvr/` — that stays app-side. CMake target with `INTERFACE` includes consumed by both `driver/` and `apps/micmap/`. |
| **LIB-02** | Common `Logger` abstraction with two implementations: client-side stdout/file logger and driver-side `DriverLog` adapter | Otherwise the shared lib pulls in MICMAP_LOG_* macros that resolve differently per host | S | Build | Promote `src/common/Logger` to a polymorphic interface. Driver provides `DriverLogSink`; client provides `StdoutLogSink`. `mic_test.exe` uses stdout sink. Existing `MICMAP_LOG_*` macros stay; sink selection happens at link time. |
| **LIB-03** | Config-file ownership negotiation: driver reads on boot, client writes on save, both detect external changes | Both processes touch the same JSON file. Without negotiation, settings change in client UI never reach the running driver, or worse: torn writes corrupt the file | M | Build | See IPC-S-01: settings flow is client → IPC → driver, NOT client → file → driver. The file is the persistence layer; the live config is pushed over IPC. Driver reads file once at `Init` and applies updates only via IPC. Client writes file as the sole writer. Eliminates polling and torn-read races. |
| **IPC-S-01** | Settings push from client → driver as a single atomic JSON object: `POST /settings` with `{audio: {deviceId, ...}, detection: {sensitivity, minDurationMs, cooldownMs, fftSize, ...}}` | Users expect "change sensitivity slider → next trigger uses new sensitivity" without restarting SteamVR | M | IPC | Driver applies under a mutex on RunFrame thread (or a dedicated "settings apply" thread that owns the audio/detector handles). Atomic — entire object replaces previous live config or driver rejects with structured error. No partial application. |
| **IPC-S-02** | Hot-apply: sensitivity, min-duration, cooldown, sample threshold values change without restarting capture or detector | These are scalar parameters; users expect slider-drag immediacy | S | IPC | These map to `INoiseDetector::setSensitivity()` and `IStateMachine::setMinDuration()` etc. — already present, just need IPC wiring. |
| **IPC-S-03** | Re-init-required: audio device change, sample rate change, FFT size change → driver tears down capture + detector, applies new config, restarts capture, reports new live state | Audio device change can't hot-swap mid-stream — WASAPI client must be released and re-acquired | M | IPC | Client UI shows transient "Switching device…" indicator (~500 ms typical) using health-surface state machine. If new device fails to open, driver rolls back to previous device + reports `last_settings_apply_error` (see HEALTH-04). |
| **IPC-S-04** | Settings rejection with structured error: out-of-range value, unknown device id, unsupported sample rate | Otherwise client UI shows "saved" while driver silently used the old value | S | IPC | `POST /settings` returns 400 + JSON error body (`{error: "...", field: "audio.deviceId"}`). Client surfaces as a non-blocking toast and reverts the offending control. |
| **IPC-S-05** | Settings persistence: client writes `%APPDATA%/MicMap/config.json` after IPC ACKs success — file is the durable record, IPC is the live channel | Otherwise restart loses settings or settings are written before driver accepts them | S | IPC | Order: client UI mutates → POST /settings → wait for 200 → write config.json. If 4xx, do not write file. Driver does not write the config file. (LIB-03 negotiation point.) |
| **IPC-S-06** | Audio device enumeration available client-side via IPC (`GET /devices`) — driver enumerates, client renders combo | Client can't enumerate WASAPI for itself anymore (it's not capturing); but the device picker UI must still list real devices | S | IPC | `GET /devices` returns `[{id, name, isDefault, sampleRate}]`. Client refreshes on combo open and on `IMMNotificationClient` events the driver forwards (or simply on a 2 s poll while the dropdown is open). |
| **TRAIN-01** | Training is initiated by the client over IPC (`POST /training/start`), runs entirely in the driver, and reports progress via the health-status surface | The driver owns the mic; the client cannot collect samples without contending for the device | M | Training | Avoid the "client takes the mic back" pattern (see anti-feature TRAIN-AF-01). One mic owner per device-handle simplifies WASAPI lifecycle and matches MIG-01. |
| **TRAIN-02** | Training samples (count + acceptance status) stream to the client at ~5–10 Hz so the UI shows "X / 150 samples collected" without manual polling | Existing client UI today shows live sample count via shared in-process atomic; users expect that progress feedback to survive the migration | S | Training | Either piggyback on the health-status poll (5 Hz, see HEALTH-06) and include `training: {sampleCount: N, accepted: M, state: "collecting"\|"computing"\|"finished"}`, or open a brief Server-Sent-Events / chunked-response stream during training mode. SSE is unnecessary; polling at 10 Hz is enough for human perception. |
| **TRAIN-03** | Training threshold preview: when sample collection finishes, driver computes thresholds, but does NOT auto-commit. Client shows "Training complete — N samples accepted, threshold = 0.NN" with Confirm / Discard buttons | Users expect to see a result before committing it (today's UI implicitly does this because computing the thresholds is fast and the user just sees "trained" — make it explicit with a confirm step) | M | Training | Driver computes thresholds in-memory, returns them in `POST /training/finish` response. Client shows preview. `POST /training/commit` writes `training_data.bin` and tells driver to begin using the new thresholds. `POST /training/discard` rolls back to prior thresholds. |
| **TRAIN-04** | Training data persistence is driver-owned: driver writes `%APPDATA%/MicMap/training_data.bin` because driver computes the data | Asymmetry with config.json (which is client-owned) is acceptable — training data is binary, computed in driver, has no UI editing surface | S | Training | Reverses today's split. Driver is the only writer/reader of `training_data.bin`; client never touches it. Eliminates the round-trip "client receives bytes → writes file → driver reads file." |
| **TRAIN-05** | Training mode is mutually exclusive with detection mode within the driver state machine | If detection runs concurrently with training, samples collected for training also fire trigger events — confusing UX, and existing state machine already enforces this | S | Training | Existing state enum already encodes this: `Idle → Training → Idle → Detecting`. Just enforce server-side in IPC handlers: `POST /training/start` rejects with 409 if `state != Idle && state != Detecting`. |
| **HEALTH-01** | Driver-loaded indicator (binary): is `driver_micmap.dll` registered with SteamVR and currently loaded by vrserver? | Users who install but don't restart SteamVR get a confusing "nothing works" experience — surface this clearly | S | Health | Client polls `GET /health` (existing endpoint). 200 + JSON ⇒ driver alive. Connection refused / timeout ⇒ driver not loaded. Transient (during SteamVR restart) ⇒ "Reconnecting…" pill in UI. Already partly present via `IDriverClient::isConnected()`. |
| **HEALTH-02** | SteamVR-running indicator: is vrserver alive at all? | Distinguish "SteamVR not running" from "SteamVR running, MicMap driver crashed" — different remediation paths | S | Health | Client uses `vr::VR_IsRuntimeInstalled()` + `vr::VR_IsHmdPresent()` + the existing `VRApplication_Background` session it already opens (per v1.5 commit `3187fbb`). No new SteamVR API surface needed. |
| **HEALTH-03** | Detection-state pill: current state machine state (Idle / Detecting / Triggered / Cooldown / Training) and time-in-state | Users want to know "is mic-cover armed right now?" without putting the headset on | S | Health | Driver returns current state in `GET /status` response body. Client renders as a colored pill. Refresh ~5 Hz. |
| **HEALTH-04** | Last-trigger timestamp + last-error code | "Did my last cover register?" is the most common debugging question; "why doesn't it work" is the second | S | Health | Driver tracks `lastTriggerAt` (`steady_clock` since process start, plus a wall-clock formatter), `lastError: {code, message, when}`. Both fields appear in the `GET /status` JSON. Last-error covers WASAPI failures, HMD-component invalidation, settings-apply rejections. |
| **HEALTH-05** | Audio-input level meter: live RMS / dB value of incoming audio so users can see "yes, mic is hearing me" before training | The single highest-value observability addition. v1.5 client already shows a live dB meter from in-process audio; migration must not regress that. | M | Health | Driver streams a level value at 10–20 Hz. Two implementations possible: (a) poll-based: `GET /status` returns most recent value, client polls at 10 Hz (acceptable, simple, ~1 KB/s overhead — RECOMMEND); (b) push-based: SSE or WebSocket on `/level/stream` (overkill for milestone). Choose (a). |
| **HEALTH-06** | Health UI refresh cadence: 5 Hz status poll while window visible, 0.5 Hz when minimized to tray, suspend on system sleep | Battery-conscious users will notice a 10 Hz HTTP poll in idle Task Manager; stay polite | S | Health | Client adapts poll rate based on `IsWindowVisible()` + `IsIconic()`. Resume on `WM_DEVICECHANGE` / `WM_POWERBROADCAST`. |
| **HEALTH-07** | WASAPI device disappearance surfaced in health status (mic unplugged mid-session) | Users unplug headsets/USB mics; without a surfaced error, "MicMap stopped working" is mysterious | M | Health | Driver subscribes to `IMMNotificationClient` (already in v1.5 client code at `audio_capture.cpp` lines 116-157 per CONCERNS.md). On `OnDeviceRemoved` for the active device: stop capture, set `last_error = "audio_device_lost"`, emit log line, expose in `GET /status`. Auto-recovery: on `OnDeviceAdded` matching previous device id, restart capture. |
| **TEST-01** | `mic_test.exe` continues to work as today: WASAPI capture + detection + training, no SteamVR required, runs against the shared library | Mandatory developer-loop affordance: detection algorithm changes can be validated in seconds without spinning up SteamVR | S | Testing | Now that audio+detection are in `LIB-01`, `mic_test.exe` becomes a thin Win32 GUI consuming the same library the driver consumes. Already structurally close to this — just refit links. |
| **TEST-02** | `--debug-trigger` CLI flag in client that POSTs a synthetic tap command to the driver, bypassing detection | Lets QA validate the trigger pipeline (driver alive, HMD component valid, dashboard toggles) independently of audio/detection issues | S | Testing | `micmap.exe --debug-trigger` connects, posts `{"kind":"tap","source":"debug"}`, prints driver response, exits. The existing `hmd_button_test.exe` covers this for v1.5; v1.6 absorbs it into the main client as a CLI mode (and `hmd_button_test.exe` may be retired or kept as a developer tool). |
| **TEST-03** | `mic_test.exe` log output to stdout for CI-friendly capture | Future automated tests want to grep for "trigger fired" without parsing GUI screenshots | S | Testing | LIB-02's StdoutLogSink already provides this. Run `mic_test.exe --headless --duration 30s --replay sample.wav` (TEST-D1 dependency) and assert N triggers in 30s. |
| **FAIL-01** | Mic permission denied / device in exclusive use → driver health shows `audio_device_unavailable`, client shows actionable message ("Microphone is in use by another app — close it and click Retry") | Windows 10/11 mic privacy settings + exclusive-mode apps (DAWs) can hold the device — this happens to real users | S | Failure | WASAPI returns `AUDCLNT_E_DEVICE_IN_USE` / `E_ACCESSDENIED`. Existing audio code already catches these; just propagate to health surface. |
| **FAIL-02** | Driver fails to load (DLL missing, SteamVR add-on disabled) → client shows "MicMap driver not loaded" with a "Reinstall" link | Most common post-install failure mode is "user upgraded SteamVR and add-on toggle reset" | S | Failure | Already detectable via HEALTH-01. UX: link to Steam path / running installer / opens driver-troubleshooting README section. |
| **FAIL-03** | SteamVR not running but client launched → client shows idle "Waiting for SteamVR…" state, retries connection at 5 s intervals | Users may launch the client manually before SteamVR (or run it as a tray-only utility) | S | Failure | Existing v1.5 reconnect logic in `vr_input.cpp` already does this (5 s reconnect interval). Keep behavior, update tray icon to reflect waiting state. |
| **FAIL-04** | Two clients launched (user double-clicks tray icon) → second instance detects existing one, sends a "show window" message, exits | Standard tray-app convention; without this, users see two icons and contradictory state | S | Failure | Use a named mutex (`Local\MicMapClient`); on collision, post `WM_USER_SHOWWINDOW` to existing window and exit. Standard Win32 idiom. |
| **FAIL-05** | Client launched without driver installed → distinct "MicMap driver not installed — run installer" state | Distinct from FAIL-02 (driver installed but not loaded) — different fix | S | Failure | Detection: client probes `vrpathreg show` output OR checks `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` for the MicMap entry. If absent, show install-required state. |

**Complexity legend:** S = small (≤1 day), M = medium (1–3 days), L = large (>3 days, multi-task plan).

### Differentiators (Polish Beyond Baseline)

| ID | Feature | Value Proposition | Complexity | Cluster | Notes |
|----|---------|-------------------|------------|---------|-------|
| **HEALTH-D1** | Per-component health badges with explanatory hover tooltips ("Driver loaded", "HMD component ready", "Audio capture active", "Trained") | Quick at-a-glance "everything is green" feedback; encourages users to retrain when they see "Trained: never" | S | Health | Pure UI work over existing fields. ~1 day to design + implement. |
| **HEALTH-D2** | Trigger-history graph (last 60 minutes of triggers as a sparkline) | Helps users diagnose "is detection firing too often / not enough" | M | Health | Driver retains a ring buffer of last N trigger timestamps in memory. Client renders ImGui plot. Bounded memory. |
| **TRAIN-D1** | "Recompute thresholds without re-collecting" — driver retains last training samples; tweak threshold-percentile slider, see trigger sensitivity recompute | Lets user dial in sensitivity without retraining; cheap because samples are already in memory | M | Training | Requires retaining raw training spectrums in `training_data.bin` (more disk space) or in memory only. Memory-only is simpler and acceptable. |
| **TRAIN-D2** | A/B threshold preview: capture 5 s of "should NOT trigger" audio after collection, show what threshold would and wouldn't have fired | Eliminates the "trained for cover, but my voice triggers it" feedback loop | L | Training | Defer — interesting but compounds training UX complexity. Pull forward if user reports come in. |
| **HEALTH-D3** | In-VR overlay status pill (using SteamVR overlay API) — green dot in HMD view when armed, red when triggered | Eliminates the "is it on?" question without taking off the HMD | L | Health | Out-of-scope per PROJECT.md (overlay stubs deferred to UX-02). Listed for completeness. |
| **TEST-D1** | `--replay` mode for `mic_test.exe`: feed a `.wav` file as if it were live mic input, run detection, report triggers | Reproducible regression tests, share-able bug repros ("here's a wav that should trigger but doesn't") | M | Testing | One adapter implementing `IAudioCapture` that reads from a wav file at the original sample rate. Pull forward into v1.6 — modest cost, large QA value. |
| **TEST-D2** | Driver debug HTTP endpoint (`/debug/snapshot`) returning current detector internals (last 16 frames of spectrum, current confidence, gating reasons) | Lets `mic_test.exe` mirror what the driver sees — debug parity between modes | M | Testing | Defer unless v1.6 phases hit detection-divergence bugs. Adds API surface area that needs maintaining. |
| **HEALTH-D4** | Tray-icon overlay reflects state (idle / armed / triggered / error) | Glanceable status without opening client | S | Health | Modify icon dynamically. ~half-day. |

### Anti-Features (Avoid Despite Surface Appeal)

| ID | Feature | Why Requested | Why Problematic | Alternative |
|----|---------|---------------|-----------------|-------------|
| **TRAIN-AF-01** | Client temporarily takes the mic back during training | "Lets the existing training UI work unchanged" — minimum migration delta | Two processes contending for one WASAPI device handle. Window of capture overlap or capture gap during the handoff. Loses MIG-01's invariant that the driver owns audio end-to-end. Doubles the failure surface (now both client *and* driver have to handle "device unavailable"). | TRAIN-01 / TRAIN-02 / TRAIN-03 — driver owns mic, streams sample-count progress to client UI, returns thresholds for confirmation. |
| **TRAIN-AF-02** | Training data binary streams over HTTP for the client to write to disk | "Symmetric with config.json" | The binary is large (raw spectrograms × 150 samples), serializing it twice (binary → HTTP → binary) is wasteful, and creates a client-side write path that has to match the driver's binary format byte-for-byte forever. | TRAIN-04: driver writes `training_data.bin` directly. Asymmetry with config.json is fine — these have different write semantics. |
| **IPC-AF-01** | Driver watches `config.json` with `ReadDirectoryChangesW` and auto-applies on change | "Eliminates IPC complexity for settings" | Torn reads (client mid-save, driver reads partial JSON). Race between save and apply. No structured rejection — driver discovers bad value silently. Forces driver to do JSON parsing on the hot path. | IPC-S-01: explicit `POST /settings` with structured response. File is persistence, IPC is live channel. |
| **IPC-AF-02** | Replace HTTP IPC with a named pipe / shared memory ring | "HTTP is slow / overkill for localhost" | HTTP is already ~1 ms localhost RTT — well under any human-perceptible threshold. cpp-httplib is in-tree, debugged, has stack traces, works with curl. Switching IPC mid-milestone explodes scope (re-validate threading, error semantics, port discovery, install scripts). | Keep HTTP. If perf or correctness ever justifies a swap, do it as its own milestone. |
| **HEALTH-AF-01** | Push-based health updates (SSE / WebSockets) | "Polling is wasteful" | At 5 Hz with a ~500-byte JSON body, polling is ~2.5 KB/s — negligible. SSE adds connection-state machine, reconnect logic, head-of-line concerns, and a long-lived TCP connection that cpp-httplib supports awkwardly. | HEALTH-06: adaptive polling. Drop to 0.5 Hz when minimized; 5 Hz when visible. |
| **HEALTH-AF-02** | Stop the audio capture loop entirely when "headset is asleep" to save CPU | "Saves CPU when user has the headset off" | Couples detection to HMD lifecycle in a way that means the user puts the headset on, expects mic-cover to work, and finds it doesn't until 1–2 seconds later when capture restarts. Worse UX than the (negligible) CPU savings. | MIG-02 (always run when SteamVR is running) plus MIG-03 (use `EnterStandby/LeaveStandby` for explicit standby — a stronger signal than HMD-deactivation). |
| **TEST-AF-01** | Build a separate "driver-internal CLI" that loads the driver DLL outside SteamVR for headless testing | "Test the driver code path directly" | Replicates SteamVR's loading conventions and the OpenVR driver context — work that mostly tests scaffolding rather than detection logic. | TEST-01 + TEST-D1: run detection-pipeline tests against the shared library; trust SteamVR loading on integration tests. |
| **MIG-AF-01** | Migrate IPC at the same time as the feature relocation | "Clean break, modernize everything" | Conflates two risky migrations. If IPC swap regresses, root-causing is harder because audio/detection also moved. v1.5 IPC works — preserve it. | Migrate features only. Surface unchanged: HTTP, cpp-httplib, port range 27015-27025. New endpoints added; old `/button` removed (but the path was always client→driver, never the inverse). |
| **MIG-AF-02** | Keep the client-side audio path as a runtime fallback "in case driver mode breaks" | "Defensive — users can still use the app if driver-side audio has bugs" | Same anti-pattern as v1.5 Pitfall AP-1 (feature-flagging the virtual controller). Two code paths means two test matrices. The shared library lets the same source compile both ways without fallback semantics — for *test* harnesses, not for *runtime* fallback. | If driver-side audio fails, fix it. Roll back via installer version if catastrophic. |
| **HEALTH-AF-03** | Persistent driver-side log file at a separate path from SteamVR's `vrserver.txt` | "Easier debugging" | Adds a logger configuration concern (paths, rotation, lock-contention with the client logger). v1.5 audit specifically flagged "file-sink logger" as a backlog candidate; not in v1.6 scope per PROJECT.md. | Driver continues using `DriverLog` → `vrserver.txt`. Client logs to `%APPDATA%/MicMap/logs/`. README documents both paths. |

---

## Feature Dependencies

```
LIB-01 (shared audio+detection lib)
   ├──blocks──> MIG-01 (driver hosts WASAPI)
   ├──blocks──> MIG-02 (driver runs detection)
   ├──blocks──> TEST-01 (mic_test consumes shared lib)
   ├──blocks──> TEST-D1 (replay mode in shared lib)
   └──blocks──> LIB-03 (config ownership only meaningful once detection moves)

LIB-02 (logger abstraction)
   └──enables──> LIB-01 (lib must compile without host-specific logger macros)

MIG-01 (driver hosts WASAPI)
   ├──blocks──> MIG-03 (standby alignment) — needs the audio thread to exist driver-side
   ├──blocks──> MIG-04 (in-process trigger) — trigger source must be in-process
   └──blocks──> TRAIN-01 (driver owns mic for training)

MIG-04 (in-process trigger)
   └──removes──> v1.5 IPC `POST /button` route (deletion, not dependency)

IPC-S-01 (settings push)
   ├──blocks──> IPC-S-02 (hot-apply scalars)
   ├──blocks──> IPC-S-03 (re-init on device change)
   ├──blocks──> IPC-S-04 (rejection / structured error)
   ├──blocks──> IPC-S-05 (persistence ordering)
   └──blocks──> IPC-S-06 (device enumeration via IPC)

LIB-03 (config ownership negotiation)
   ├──depends-on──> IPC-S-01 (live channel must exist)
   └──depends-on──> IPC-S-05 (persistence is client-only)

TRAIN-01 (driver-resident training)
   ├──depends-on──> MIG-01 (driver owns mic)
   ├──blocks──> TRAIN-02 (sample-count streaming)
   ├──blocks──> TRAIN-03 (threshold preview / commit)
   ├──blocks──> TRAIN-04 (driver writes training_data.bin)
   └──blocks──> TRAIN-05 (mutex with detection)

HEALTH cluster
   ├──depend-on──> nothing strictly within v1.6 — buildable on existing IPC scaffold
   ├──HEALTH-05 enhances──> TEST-01 (level meter useful in mic_test too)
   └──HEALTH-07 (device disappearance) overlaps──> FAIL-01 (permission denied) — same surface

TEST-01 (mic_test continues working)
   └──depends-on──> LIB-01

TEST-D1 (--replay)
   └──depends-on──> TEST-01 + LIB-01

FAIL cluster
   ├──FAIL-01, FAIL-02, FAIL-04 — independently buildable
   ├──FAIL-03 — already shipped in v1.5; carry behavior forward
   └──FAIL-05 — depends on a vrpathreg-query helper (small)

DOC-01 / DOC-02 (carryover from v1.5 Phase 5)
   └──depend-on──> all above being shipped (docs describe shipped reality)
```

### Dependency Notes

- **LIB-01 is the keystone.** Almost everything depends on the shared library being extracted first. It's a refactor with low semantic risk (moving files, adjusting CMake) but high ordering risk — start of milestone.
- **LIB-02 must precede LIB-01.** Today's `MICMAP_LOG_*` macros assume client-side logging. Need a sink abstraction so the same compilation unit works in driver and client.
- **MIG-04 is a deletion, not an addition.** v1.5's `POST /button` route is removed; existing in-process trigger (already used by v1.5 driver between HTTP handler and RunFrame via `CommandQueue`) extends to "detection callback → CommandQueue."
- **TRAIN-AF-01 (client takes mic back) is structurally tempting but architecturally fatal.** Calling out to keep the driver-resident-mic invariant pristine.
- **HEALTH cluster is mostly independent of MIG/LIB** — buildable in any order once IPC scaffold exists. Recommend bundling with MIG so the migration ships with end-user observability for free.
- **No conflicts between features in this list.** Anti-features document the rejected combinations.

---

## Migration Phasing (input to roadmap, not the roadmap itself)

A defensible phase order based on the dependency graph above. The roadmap-author should treat this as a strong recommendation:

| Phase suggestion | Features | Rationale |
|-------------------|----------|-----------|
| Shared library extraction | LIB-01, LIB-02 | Keystone refactor; nothing else compiles cleanly without it. Low semantic risk; mostly CMake + namespace + header shuffling. |
| Driver-resident detection lifecycle | MIG-01, MIG-02, MIG-03, MIG-04 | The actual relocation. Validate end-to-end trigger via in-process detection; delete `POST /button` route. |
| Settings IPC | IPC-S-01..06, LIB-03 | New IPC surface; can be designed and tested in isolation once driver-resident detection lives. |
| Training flow | TRAIN-01..05 | Depends on settings IPC pattern (validated) and driver-resident audio (built). |
| Health surface | HEALTH-01..07 | Independent of TRAIN; can run parallel with training flow if capacity. |
| Failure modes | FAIL-01..05 | Polish phase; some features overlap earlier work (FAIL-03 already exists; FAIL-01 falls out of HEALTH-07). |
| Test affordances | TEST-01, TEST-02, TEST-D1 | Threaded through earlier phases as each surface lands. TEST-D1 (replay mode) ideally lands with LIB-01. |
| Documentation | DOC-01, DOC-02 | After everything ships. README reflects post-migration architecture; `docs/architecture.md` describes shared lib, driver-resident detection, settings IPC, training flow. |

---

## Open Questions for Requirements Phase

The following are intentional gaps that the requirements phase (REQ-ID assignment) should resolve:

1. **Health-status poll cadence baseline.** Recommended 5 Hz when visible, 0.5 Hz minimized — defensible but not validated against user expectation. If real users find the level meter "laggy," bump to 10 Hz when visible.
2. **Where does the audio capture thread live in the driver?** Inside `DeviceProvider` (matches v1.5 ownership of `HttpServer`) or a separate `DetectionService` class? Architecture research will recommend; this doc is feature-level only.
3. **Settings rejection UX.** Toast-and-revert (recommended above) vs. inline-validation-disable-Save-button. Latter is friendlier but requires the client to know the driver's validation rules — duplicating logic.
4. **Should `mic_test.exe` get an in-process trigger plumb that talks to a real driver if one is loaded?** Today's mic_test is fully isolated; could optionally exercise the IPC path. Probably anti-feature (TEST-AF-01) — call out for explicit decision.
5. **Backwards compatibility with v1.5 client + v1.5 driver.** The v1.5 driver doesn't have new endpoints; the v1.6 client doesn't speak `POST /button` (with no fallback). Mismatched install (user upgrades client only or vice versa) breaks. Installer must enforce co-versioning, OR client gracefully degrades and reports "driver out of date." Recommend installer-enforces; cheaper than dual-version client.
6. **In-VR settings overlay (UX-02).** Confirmed out-of-scope per PROJECT.md. Verify before requirements close — could change if users surface this loudly during v1.5 wider release.
7. **Does `hmd_button_test.exe` survive v1.6?** It tests the v1.5 HMD-component pipeline. After migration, the same pipeline still runs (driver still owns `/input/system/click`); but with TEST-02 in the main client, the standalone harness may be redundant. Decision: keep as developer tool, demote from shipped artifact, or delete.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Lifecycle (MIG-*) | HIGH | OpenVR driver lifecycle (`Init`/`Cleanup`/`RunFrame`/`EnterStandby`/`LeaveStandby`) is documented; v1.5 already has driver-side threading working (HttpServer thread); WASAPI inside vrserver is mechanically the same as inside any user-space Windows process — needs `CoInitializeEx` on the capture thread, no special permissions. |
| Shared library (LIB-*) | HIGH | Pure C++/CMake refactor; existing `src/` is already nearly free of OpenVR client deps (only `src/steamvr/` pulls OpenVR). Logger abstraction is well-trodden territory. |
| Settings IPC (IPC-S-*) | HIGH | Extends the v1.5 `POST /button` pattern; cpp-httplib has all needed surface. Atomic-apply discipline is documented. |
| Training flow (TRAIN-*) | MEDIUM | Streaming-progress + commit/discard pattern is opinionated. No direct prior art in v1.5 — designing from first principles. Risk: users may resist the explicit "commit" step if today's flow is "click train → done." Mitigation: make Confirm the default action with one click. |
| Health surface (HEALTH-*) | MEDIUM | Polling cadence and refresh-while-minimized adaptation are guesses backed by general UX principles, not measurement. Validate during early phase UAT. |
| Test affordances (TEST-*) | HIGH | `mic_test.exe` already exists and works; LIB-01 just keeps it working. Replay mode is a small adapter. |
| Failure modes (FAIL-*) | HIGH | All of these have prior art either in v1.5 (FAIL-03) or in standard Win32/WASAPI patterns (FAIL-01, FAIL-04). |

---

## Sources

### Primary (HIGH confidence, in-tree)

- `.planning/PROJECT.md` (2026-04-30) — milestone scope, in/out-of-scope, target features
- `.planning/STATE.md` (2026-04-30) — open architectural questions for v1.6
- `.planning/codebase/ARCHITECTURE.md` (2026-04-23) — current two-process layout, library boundaries, data flow
- `.planning/codebase/INTEGRATIONS.md` (2026-04-23) — WASAPI surface, cpp-httplib version, port range, manifest layout
- `.planning/codebase/CONCERNS.md` (2026-04-23) — `IMMNotificationClient` for device disappearance, COM ref-counting concerns, single-mic-device limit
- `.planning/research/ARCHITECTURE.md` (v1.5 research, 2026-04-22) — sidecar driver lifecycle, CommandQueue pattern, HMD reactivation discussion
- `.planning/research/PITFALLS.md` (v1.5 research, 2026-04-22) — RunFrame budget (Pitfall 12), DriverLog wiring (Pitfall 11), virtual-controller removal patterns (Pitfall 7)
- `.planning/MILESTONES.md` (2026-04-30) — v1.5 closure, what's already shipped
- `driver/src/http_server.cpp` (2026-04-29) — current IPC surface (`POST /button`, `GET /health`, `GET /port`, `GET /status`)
- `driver/src/device_provider.hpp` (2026-04-29) — driver lifecycle hooks, `EnterStandby/LeaveStandby` overrides, CommandQueue ownership
- `src/steamvr/src/vr_input.cpp` (2026-04-29) — `IDriverClient` shape (`tap`, `getStatus`, `getPort`, `getLastError`, port-range scan)
- `src/core/src/config_manager.cpp` (2026-04-29) — config persistence, atomic save, UTF-8 boundary
- `apps/micmap/main.cpp` (2026-04-29) — current training UI flow, audio callback wiring, driver client lifecycle

### Primary (HIGH confidence, official docs)

- [OpenVR Driver API Documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md) — `IServerTrackedDeviceProvider::Init/Cleanup/RunFrame/EnterStandby/LeaveStandby` lifecycle; "RunFrame must not block"; "spin up own thread for periodic work"
- [About WASAPI (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi) — capture-client lifecycle, no driver-process restrictions; standard Win32 audio API surface from any user-space process

### Secondary (MEDIUM confidence)

- [WASAPI permission denied — Renoise forums](https://forum.renoise.com/t/solved-failed-to-initialize-wasapi-device-permission-denied/73267) — typical "device in exclusive use" failure mode (FAIL-01 anchor)
- [OpenVR Driver Documentation wiki](https://github.com/ValveSoftware/openvr/wiki/Driver-Documentation) — driver loading, threading conventions
- [Windows audio session sample (Microsoft Learn)](https://learn.microsoft.com/en-us/samples/microsoft/windows-universal-samples/windowsaudiosession/) — reference WASAPI capture loop

### Tertiary (informed by general patterns)

- General Windows tray-app conventions (named mutex single-instance, FAIL-04)
- General localhost-IPC HTTP patterns (cpp-httplib idioms already in use in v1.5)

---

*Feature research for: MicMap v1.6 Feature Migration milestone — driver-resident detection pipeline, shared library, settings IPC, training flow, driver-health surface*
*Researched: 2026-04-30*

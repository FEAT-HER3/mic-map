# Architecture Patterns — v1.6 Feature Migration

**Domain:** Migrating MicMap's audio capture, FFT detection, state machine, config, and trigger pipeline from `micmap.exe` into `driver_micmap.dll`. Extract a shared static library so client, driver, and headless test harness share one source of truth. Reshape client↔driver IPC away from "client triggers, driver injects" toward "driver detects + acts; client only configures and observes."

**Researched:** 2026-04-30
**Confidence:** HIGH (codebase grounded — every named symbol/path verified against the current tree on branch `hmd-button`)

## Pattern Overview

**Overall:** Layered shared-core library + driver-resident detection pipeline + thin settings/health UI client. The driver becomes the *primary* end-to-end runtime; the client demotes to a settings editor and connection indicator.

**Key shifts from v1.5:**

| Concern | v1.5 (today) | v1.6 (target) |
|---------|-------------|---------------|
| Detection runs in | `micmap.exe` (audio cb thread → FFT inline) | `driver_micmap.dll` (audio cb thread → detection thread → CommandQueue → RunFrame) |
| Trigger crosses IPC | Yes — `POST /button {"kind":"tap"}` from client | No — internal CommandQueue push from driver detection thread |
| Config writer | Client only | Client UI edits, but driver is sole disk writer |
| Audio device list | Client enumerates locally | Driver enumerates; exposes via `GET /devices` |
| Training data owner | Client (`%APPDATA%/MicMap/training_data.bin`) | **Driver** (writes after `/training/finalize`); training UI is observer-only |
| Shared lib | None — `micmap_audio`, `micmap_detection`, `micmap_core` are linked into `micmap.exe` only; driver doesn't link them | New `micmap_core_runtime` INTERFACE target aggregates audio + detection + core, linked into both DLL and EXE |

---

## 1. Shared Library Boundary — `libmicmap_core` (new aggregate target)

### Recommended target name and shape

The repo already uses `add_library(micmap::lib INTERFACE)` in `src/CMakeLists.txt:12` as an INTERFACE umbrella. **Do not reuse that name** — it transitively pulls in `micmap_steamvr` (which depends on OpenVR + cpp-httplib). Instead introduce a **new** INTERFACE target that aggregates ONLY the headless runtime bits:

```cmake
# src/CMakeLists.txt — add after existing add_subdirectory calls
add_library(micmap_core_runtime INTERFACE)
target_link_libraries(micmap_core_runtime INTERFACE
    micmap_common      # logger, types, cli_flags
    micmap_audio       # WASAPI capture, device enumeration
    micmap_detection   # FFT, noise detector, pattern trainer
    micmap_core        # state machine, config schema (config persistence stays here)
)
add_library(micmap::core_runtime ALIAS micmap_core_runtime)
```

The four underlying STATIC libs (`micmap_common`, `micmap_audio`, `micmap_detection`, `micmap_core`) **stay as-is** — no source movement, no namespace change. The migration is purely additive at the CMake level: a new aggregate label that both the driver and the client consume.

### What is IN the shared boundary

| Component | Path | Status | Notes |
|-----------|------|--------|-------|
| `micmap::common` (logger, Result, types, cli_flags) | `src/common/` | **unchanged** | Already linkage-clean |
| `micmap::audio` (`IAudioCapture`, `IDeviceEnumerator`, `AudioBuffer`) | `src/audio/` | **unchanged** | WASAPI is private impl detail; public surface is platform-neutral |
| `micmap::detection` (`INoiseDetector`, `ISpectralAnalyzer`, `IPatternTrainer`) | `src/detection/` | **unchanged** | KissFFT linkage is PRIVATE per `src/detection/CMakeLists.txt:21` — already clean |
| `micmap::core` (`IStateMachine`, `IConfigManager`, `AppConfig`) | `src/core/` | **unchanged** | nlohmann/json is PRIVATE per `src/core/CMakeLists.txt:19` |

### What is OUT — must NOT enter the shared lib

| Forbidden symbol | Where it lives today | Reason |
|------------------|---------------------|--------|
| `vr::*` (OpenVR) | `src/steamvr/`, `driver/src/` | OpenVR linked PUBLIC into `micmap_steamvr` (`src/steamvr/CMakeLists.txt:50`); shared lib must be vendor-neutral so `mic_test.exe` builds without OpenVR |
| `httplib::*` | `driver/src/http_server.{hpp,cpp}`, `src/steamvr/src/vr_input.cpp` (DriverClient) | HTTP is an IPC concern, not a runtime concern |
| `ImGui_*`, D3D11 | `external/imgui/`, `apps/micmap/main.cpp` | Settings UI only |
| `DriverLog` from `openvr_driver.h` | `driver/src/driver_log.hpp` | Driver-side logging adapter; shared code uses `MICMAP_LOG_*` macros |
| `nlohmann::json` in **public headers** | Currently private to `config_manager.cpp` | Keep PRIVATE; shared headers stay JSON-free. **Caveat:** `src/bindings/include/micmap/bindings/bindings_patcher.hpp` already exposes nlohmann::json publicly — that's a localized exception (bindings is bindings-patcher-only, doesn't go in `core_runtime`) |
| Threading primitives that differ between processes | n/a | Standard `<thread>`/`<mutex>` is fine and works in DLL — but **no global mutable state** that would conflict on static-init order in DLL load |
| `manifest_registrar` | `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` | Stays in `micmap_steamvr`; client-only concern (driver doesn't register vrmanifest) |
| `bindings_patcher` | `src/bindings/` | Stays in its own `micmap_bindings` lib; driver-only at install-time, client-only at uninstall-time. Already correctly factored |

### Headless test harness preservation (critical invariant)

`apps/mic_test/CMakeLists.txt:8-12` already links exactly `micmap_audio + micmap_detection + micmap_common`. Switching to `micmap_core_runtime` is a one-line change. The harness must remain: build-buildable with **`-DMICMAP_BUILD_DRIVER=OFF`** and NO OpenVR SDK present. This is the regression backstop for "did the migration violate the shared lib boundary?"

---

## 2. Driver-Side Threading Model

### OpenVR's hard constraint (verified against `driver/src/device_provider.cpp:113-204`)

`IServerTrackedDeviceProvider::RunFrame()` is the only thread allowed to call `VRDriverInput()->UpdateBooleanComponent` (and `CreateBooleanComponent`, `VRProperties()->TrackedDeviceToPropertyContainer`, `PollNextEvent`). bey-closer-t1's spike notes (per `HMD Button Stub.md`) and current driver code both confirm: any driver-API call from another thread is at best a hang, at worst a SteamVR crash. **The CommandQueue at `driver/src/command_queue.hpp:14` is the load-bearing safety primitive** that makes cross-thread events safe — a mutex-guarded `std::deque<TapCommand>` with drop-oldest at depth 8.

### Recommended thread map (4 owned threads + RunFrame)

```
┌────────────────────────────────────────────────────────────────────┐
│  driver_micmap.dll process (vrserver host)                         │
│                                                                    │
│  [WASAPI capture thread]    owned by: WASAPIAudioCapture           │
│      │  src/audio/src/audio_capture.cpp:435 (existing)             │
│      │  WaitForSingleObject(captureEvent_, 100)                    │
│      │  invokes AudioCallback synchronously                        │
│      ▼                                                             │
│  [Audio sample SPSC ring]   NEW — replaces inline FFT              │
│      │  bounded ring of (timestamp, frame_count, float[])          │
│      │  drop-oldest if detection thread falls behind               │
│      ▼                                                             │
│  [Detection thread]         NEW — `DetectionRunner`                │
│      │  pops sample frames, runs INoiseDetector::analyze           │
│      │  pushes through IStateMachine::update                       │
│      │  state-machine TriggerCallback fires →                      │
│      ▼                                                             │
│  [CommandQueue]             EXISTING — driver/src/command_queue.hpp│
│      │  push(TapCommand{}) — already drop-oldest, depth 8          │
│      ▼                                                             │
│  [vrserver RunFrame]        EXISTING — RunFrame at ~100Hz          │
│         drains queue, calls UpdateBooleanComponent                 │
│                                                                    │
│  [HTTP server thread]       EXISTING — HttpServer::ServerThread    │
│      │  cpp-httplib blocking listen                                │
│      │  routes:                                                    │
│      │    PUT  /settings              → atomic settings swap       │
│      │    POST /training/start        → toggles detector mode      │
│      │    POST /training/finalize     → finishTraining + persist   │
│      │    GET  /health, /state, /devices, /telemetry/*             │
│      ▼                                                             │
│  [shared settings snapshot]  NEW — std::atomic<std::shared_ptr>    │
│         read-mostly; HTTP thread swaps; detection thread reads     │
└────────────────────────────────────────────────────────────────────┘
```

### Thread ownership and lifecycle (anchored to `DeviceProvider::Init` / `Cleanup`)

| Thread | Owner | Started in | Stopped in |
|--------|-------|-----------|-----------|
| WASAPI capture | `WASAPIAudioCapture` (existing) | `DeviceProvider::Init` after CommandQueue + HttpServer (so capture errors don't strand other resources) | `DeviceProvider::Cleanup` BEFORE CommandQueue/HttpServer destruction |
| Detection | `DetectionRunner` (new class in driver/src/) | `DeviceProvider::Init`, AFTER detector is created from config | `DeviceProvider::Cleanup` BEFORE `audioCapture->stopCapture()` so the ring drains cleanly |
| HTTP server | `HttpServer` (existing) | `DeviceProvider::Init` (already does this, line 71) | `DeviceProvider::Cleanup` (already does this, line 89) |
| RunFrame | vrserver | not owned | not owned |

### COM apartment caveat (verified)

`src/audio/src/audio_capture.cpp:196` and `:521` already call `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the WASAPI capture thread. Driver DLL load occurs in vrserver, which has its own COM init. **Do not** add `CoInitialize` to `DeviceProvider::Init` — it's the capture thread's responsibility, and that code is already correct. Verified safe in DLL host: WASAPI capture works inside vrserver because the audio API only requires MTA on the *capture thread*, not on the DLL load thread. (Cross-reference: bey-closer-t1's audio-pipeline experiments under SteamVR validated this pattern; confidence: MEDIUM-HIGH — verified once in a sister project, not yet validated in this driver. Phase 2 spike below de-risks it.)

### CommandQueue extension (back-compat preserving)

The current `TapCommand{}` (empty struct, presence-as-signal) is fine. **Do not** change its shape; introduce a discriminated union only if needed:

```cpp
// driver/src/command_queue.hpp — extend ONLY if Phase 4 demands it
struct TapCommand {};
struct ReconfigureCommand {};   // optional — for "settings changed, reset detector"
using DriverCommand = std::variant<TapCommand, ReconfigureCommand>;
```

For v1.6, `TapCommand` alone suffices — settings changes are applied via the atomic settings snapshot; no explicit reset needed for sensitivity/threshold/cooldown (detection thread reads new snapshot next iteration). The `ReconfigureCommand` is only required if the audio device or sample rate changes mid-flight; even then it's cleaner to do the stop/restart synchronously inside the HTTP handler (see §4 atomic update).

### What dies in the audio callback

`apps/micmap/main.cpp:353-444` runs FFT, state-machine `update`, RMS, dB, and UI state mutation **inline in the WASAPI callback under `audioMutex`**. This is a known fragile pattern (see `.planning/codebase/CONCERNS.md:74-78` "Audio Buffer Accumulation" and `:94-98` "State Machine Thread Safety"). Migrating gives us a free fix:

- WASAPI callback writes to ring buffer only — non-blocking, no allocation.
- Detection thread does the FFT+analyze+state-machine work.
- `audioMutex` and `currentLevel`/`currentConfidence` UI fields disappear from the driver entirely (they only exist for UI display; client polls `/telemetry/level` instead).

### Concurrency primitive choices

- **Sample ring:** lock-free SPSC (single producer = WASAPI thread, single consumer = detection thread). Hand-rolled head/tail atomics over a `std::array<float, N>` is sufficient; no boost dep needed. Depth: 4× WASAPI period @ 48 kHz, ~10 ms each, so 4 × 480 = 1920 floats ≈ 7.7 KB. Drop-oldest on overflow.
- **Settings snapshot:** `std::atomic<std::shared_ptr<DriverSettings>>` — C++17 needs `std::atomic_load/atomic_store` free functions on shared_ptr (deprecated in C++20 but present); C++20 has native atomic shared_ptr. HTTP thread builds a new `DriverSettings`, atomic-stores it; detection thread atomic-loads at the top of each iteration.
- **CommandQueue:** keep existing `std::mutex + std::deque`. Correct, depth-8, low rate (≤2 Hz nominal — one tap per cover) makes contention noise.

---

## 3. New IPC Contract

### Transport: keep cpp-httplib + JSON. Versioned.

**Rationale:** the existing surface (`POST /button`, `GET /health`, `GET /port`, `GET /status` at `driver/src/http_server.cpp:124-170`) already uses cpp-httplib + nlohmann::json. The team has the muscle memory; debug tooling (curl, browser) just works; the `IDriverClient` retry/port-discovery logic in `src/steamvr/src/vr_input.cpp` is battle-tested. **Do not switch transport (named pipes, shared memory, gRPC) for v1.6.** Added complexity buys nothing — IPC traffic is low-rate (settings push on user action; training samples are not transmitted at all post-migration; telemetry is only when settings UI is open).

**Versioning:** add `"v": 1` field on every JSON request and response. Driver returns `400 {"error":"unsupported version"}` for unknown versions. Lets v1.7 add fields without breaking older clients.

### Endpoint catalog (v1.6 contract)

#### Health & state (client-pulls-driver)

| Method | Path | Body | Returns | Purpose |
|--------|------|------|---------|---------|
| GET | `/health` | — | `{"v":1,"status":"healthy"}` | Liveness — keep existing semantics |
| GET | `/state` | — | `{"v":1,"state":"Idle\|Training\|Detecting\|Cooldown","hasProfile":true,"deviceConnected":true,"hmdReady":true}` | Replaces `/status`; client polls @ 2 Hz for the indicator UI |
| GET | `/telemetry/level` | — | `{"v":1,"rms":0.04,"db":-28.0,"confidence":0.62,"isWhiteNoise":false}` | Replaces inline UI fields from v1.5 main.cpp; client polls @ 30 Hz only when settings window is open (rate-limited by client) |

#### Settings (client-pushes-driver)

| Method | Path | Body | Returns | Purpose |
|--------|------|------|---------|---------|
| GET | `/settings` | — | full `AppConfig` JSON | Driver returns its in-memory snapshot — source of truth at runtime |
| PUT | `/settings` | full `AppConfig` JSON | `{"v":1,"applied":true}` or `{"v":1,"applied":false,"errors":["..."]}` | **Whole-object** replacement (atomic). Driver validates (clamp/range-check), persists to `%APPDATA%/MicMap/config.json` via the existing `IConfigManager::save`, atomic-stores new settings snapshot. Detection thread sees new values on next iteration |
| GET | `/devices` | — | `{"v":1,"devices":[{"id":"...","name":"...","sampleRate":48000,...}],"selected":"..."}` | Driver enumerates via `IDeviceEnumerator`; client UI displays a picker |

**Whole-object vs per-field decision: whole-object.** Reasons:
- AppConfig is small (~10 fields).
- Atomic swap of the entire snapshot is trivially race-free; per-field would require per-field locks or CRDT-style merges.
- File write becomes deterministic — driver writes the same object it just received.
- Diffing is the *client's* job (compute new AppConfig, send it). Client already has the editing UI.

#### Training (client controls; driver owns audio)

| Method | Path | Body | Returns | Purpose |
|--------|------|------|---------|---------|
| POST | `/training/start` | `{"v":1}` | `{"v":1,"trainingId":"uuid","minSamples":150}` | Driver calls `INoiseDetector::startTraining()`, returns a session ID |
| GET | `/training/progress` | — | `{"v":1,"samplesCollected":120,"target":150,"spectralPreview":[...]}` | Client polls @ 5 Hz while training UI is open; renders progress bar + live FFT preview |
| POST | `/training/finalize` | `{"v":1}` | `{"v":1,"thresholds":{"energy":0.04,"correlation":0.78},"saved":true}` | Driver calls `INoiseDetector::finishTraining()` + `saveTrainingData(...)`. Persists to `%APPDATA%/MicMap/training_data.bin` |
| POST | `/training/cancel` | `{"v":1}` | `{"v":1,"cancelled":true}` | Discards in-progress training without persisting |

**Training-flow architectural pivot:** the v1.5 model has the client capturing audio and computing the profile. After migration, the **driver owns the audio device** — there's no other process for the user's mic to flow through. The client's training UI is now an *observer* of driver-side training, not a sample producer. So **no `POST /training/sample` endpoint is needed**: the entire flow is "client says start, driver collects N samples from its already-running WASAPI capture, client polls progress, client says finalize." This is the correct shape. (Add `/training/sample` only if v1.7 needs offline retraining from a recorded WAV — defer until justified.)

#### Trigger (NO ENDPOINT)

`POST /button` is **deleted** at Phase 6. The trigger event no longer crosses IPC. The current call path

```
apps/micmap/main.cpp:520 → driverClient->tap()
  → HTTP → http_server.cpp:129 → queue_.push(TapCommand{})
```

collapses to a direct call from the driver-internal detection thread to `commandQueue_->push(TapCommand{})`. This is the central trigger-pipeline simplification of the milestone.

### IPC client refactor on the EXE side

`src/steamvr/src/vr_input.cpp` houses `DriverClientImpl` (per `vr_input.hpp:149-198`). After migration, `IDriverClient::tap()` is dead code — delete it. Repurpose the file: add `IDriverClient::getState()`, `getTelemetry()`, `getSettings()`, `putSettings(const AppConfig&)`, `getDevices()`, `startTraining()`, `getTrainingProgress()`, `finalizeTraining()`, `cancelTraining()`. Keep the same port-discovery + retry mechanics. **Recommend rename `IDriverClient` → `IDriverApi`** to reflect the broader role; the old name no longer fits.

---

## 4. Config Ownership

### Writer/reader matrix

| Operation | v1.5 | v1.6 |
|-----------|------|------|
| `config.json` write | Client (`saveDefault()` in `MicMapApp::shutdown`) | **Driver** writes after `PUT /settings` succeeds; client never writes the file directly |
| `config.json` read | Client at startup; driver does NOT read | Driver at `Init` (boot); client reads on startup as a *fallback* if driver isn't reachable yet |
| `training_data.bin` write | Client (after `finishTraining`) | Driver after `POST /training/finalize` |
| `training_data.bin` read | Client at startup | Driver at `Init` |

**Consequence:** the client becomes a *cache* of the driver's state, not a writer. Edit flow:

```
User edits in client UI  →  client builds AppConfig  →  PUT /settings  →
driver validates  →  driver atomic-swaps in-memory snapshot  →
driver writes config.json  →  driver returns OK  →
client refreshes its local copy from GET /settings (truth-up call)
```

### Why driver-as-writer (not client-as-writer + driver-watches-file)

A file-watcher in the driver was considered. **Rejected.** Reasons:
- File-system notifications on Windows (`ReadDirectoryChangesW`) are racy with atomic-rename writes (the existing `ReplaceFileW` path at `src/core/src/config_manager.cpp` causes spurious change events).
- Two writers (client and the auto-init defaults from `loadDefault()`) creates lost-write races.
- Single-writer rule is the simplest correctness invariant.
- IPC round-trip latency on `PUT /settings` (~5 ms localhost) is imperceptible to a settings-form UX.

### Client startup race

If the client launches before the driver finishes `Init` (auto-launch order with SteamVR), `GET /settings` will fail. Mitigation:
1. Client reads `config.json` directly at startup (read-only, with the existing defensive nlohmann/json path).
2. Client polls `GET /settings` with the existing port-discovery retry loop; once available, **driver's snapshot wins** and client refreshes its local copy.
3. If the user edits before driver is up, disable the Apply button and show "connecting…" — already a UX pattern in the existing `IDriverClient::isConnected()` flow at `apps/micmap/main.cpp:534`.

### Atomic update propagation

Inside the driver, `PUT /settings` handler:
1. Validates → builds new `AppConfig`.
2. Calls `IConfigManager::save(...)` (atomic `ReplaceFileW` already in v1.5 — verified per CFG-04).
3. `std::atomic_store(&settingsSnapshot_, std::make_shared<const AppConfig>(newConfig))`.
4. Returns 200 to client.
5. Detection thread reads `std::atomic_load(&settingsSnapshot_)` at the top of each iteration; sensitivity / minDuration / cooldown changes propagate within ≤ one detection cycle (~20 ms).

For fields that require **detector reconstruction** (sample rate change, FFT size change, audio device change), the handler must:
1. Stop detection thread.
2. Stop audio capture.
3. Reconstruct `IAudioCapture` (potentially with new device) and `INoiseDetector` (potentially with new fftSize / sampleRate).
4. Restart capture + detection.

A controlled stop/start under the HTTP thread, with the CommandQueue draining naturally. UI shows "applying…" during the swap; expected duration < 500 ms. **No OpenVR API call** during this — preserves the SVR-05 invariant that HTTP thread never touches `vr::*`.

### `training_data.bin` ownership

Driver-only owner. Reasons:
- Driver is the only process running detection — only consumer at runtime.
- Client never needs to read it: the only client-side need is "do I have a profile?" which `GET /state.hasProfile` answers.
- Eliminates the v1.5 "two processes both think they own this file" risk.

**Migration nuance:** existing user installs have `training_data.bin` written by the v1.5 client. The v1.6 driver must read this file at boot — schema is unchanged, so it just works. No format migration. Confidence: HIGH (binary format is in `src/detection/src/noise_detector.cpp`'s `saveTrainingData/loadTrainingData` and is shared between client and driver via the unchanged `micmap_detection` static lib).

---

## 5. Build Order / Suggested Phase Shape

### Constraints driving the order

1. **No "big bang" cutover.** Each phase produces a buildable, UAT-able installer. v1.5 shipped UAT-on-real-hardware; v1.6 must keep that discipline.
2. **Backwards-compatible interim states.** During phases where both client-side and driver-side detection exist, the system must run with one or the other (feature-flagged in `default.vrsettings`).
3. **Risk frontloading.** Riskiest unknowns (WASAPI in DLL host; OpenVR audio-thread coexistence) come first so failures surface before cutover phases.
4. **`mic_test.exe` is the regression backstop.** It must build at every phase boundary — if it breaks, the shared-lib boundary has been violated.

### Recommended phase order

#### Phase 1 — Shared lib boundary + driver links it (NO behavior change)
**Risk:** LOW. **Buildable end:** YES. **UAT:** existing v1.5 UAT plus "driver still ships 100% v1.5 behavior."

- Add `micmap_core_runtime` INTERFACE target in `src/CMakeLists.txt`.
- `driver/CMakeLists.txt`: add `target_link_libraries(driver_micmap PRIVATE micmap_core_runtime)`. Driver doesn't *use* the lib yet.
- `apps/mic_test/CMakeLists.txt`: switch from individual libs to `micmap_core_runtime`. Verifies the aggregate target is correctly headless.
- Verify: clean build with `-DMICMAP_BUILD_DRIVER=OFF` still produces `mic_test.exe`.

**Exit criterion:** driver DLL grows by < 50 KB (audio + detection + core static linkage); behavior identical to v1.5; `mic_test.exe` still builds with no OpenVR present.

#### Phase 2 — Driver-side audio capture spike (parallel-running, off by default)
**Risk:** HIGH. **Buildable end:** YES. **UAT:** load driver, verify SteamVR start is healthy (no audio path active because feature-flag default OFF).

This is the **WASAPI-in-DLL feasibility validation**. If WASAPI doesn't work inside vrserver's DLL host (COM apartment quirks, audio session permissions when SteamVR runs under a different user), we need to know NOW, not three phases in.

- Add a feature flag `enableDriverAudio` in `default.vrsettings`.
- In `DeviceProvider::Init`, if flag is set: construct `IAudioCapture`, select device by name pattern, start capture, log the sample rate + channel count + first 1 second of RMS values, then stop.
- Don't run detection yet; don't push any commands.
- Validate on real Bigscreen Beyond + Win11 rig.

**Exit criterion:** `vrserver.txt` shows successful WASAPI capture inside the driver process. If this fails, escalate (research the workaround) BEFORE proceeding.

#### Phase 3 — Driver-side detection thread (still off by default)
**Risk:** MEDIUM. **Buildable end:** YES. **UAT:** flag-on test mode.

- Add `DetectionRunner` class (new) that owns the sample ring, `INoiseDetector`, `IStateMachine`.
- Wire WASAPI callback → ring → detection thread → state-machine `TriggerCallback` → `commandQueue_->push(TapCommand{})`.
- Driver loads `training_data.bin` from `%APPDATA%/MicMap/` at Init.
- Behind `enableDriverDetection` flag (default OFF). Client still does its own detection and still posts to (still-active) `POST /button`.
- Wire two simultaneous trigger paths in test mode for instrumentation: log when the driver-internal trigger fires AND when an HTTP-bridge trigger arrives, so we can correlate.

**Exit criterion:** with flag ON, covering the mic toggles dashboard. Confidence in the new path. Client-side detection still works with flag OFF (rollback path).

#### Phase 4 — IPC contract reshape (settings + state, no training migration yet)
**Risk:** MEDIUM. **Buildable end:** YES. **UAT:** new client UI talks to new driver endpoints; trigger still works (flag-gated).

- Driver: add `GET /state`, `GET /telemetry/level`, `GET /devices`, `GET /settings`, `PUT /settings`. Keep existing `POST /button` until Phase 6.
- Driver: load config at Init, atomic-swap on PUT, write config file on success.
- Client: extend `IDriverClient` (rename to `IDriverApi`) with new methods. Update settings UI to PUT instead of writing file directly. Show driver-pulled state in indicator.
- Client config file write path remains as Phase 4-fallback (in case PUT fails).

**Exit criterion:** all v1.5 settings flows now go through IPC; client no longer writes `config.json` on the success path; UAT proves UI changes propagate to driver detection (the flag still gates which side actually triggers).

#### Phase 5 — Training migration
**Risk:** MEDIUM. **Buildable end:** YES. **UAT:** train new pattern through new flow.

- Driver: add `POST /training/{start,finalize,cancel}`, `GET /training/progress`. Wire to `INoiseDetector` training mode.
- Driver: write `training_data.bin` at finalize.
- Client: replace local training pipeline with IPC calls. Keep client-local training data display ("trained at: …") via `GET /state.hasProfile`.
- v1.5 `training_data.bin` files are forward-compatible (same format). Confidence: HIGH.

**Exit criterion:** retrain end-to-end on real hardware. Trigger fires from new pattern.

#### Phase 6 — Cutover (flip the flag, delete dead code)
**Risk:** LOW (all paths proven). **Buildable end:** YES. **UAT:** v1.5-style end-to-end UAT.

- `enableDriverDetection` flag default flipped to ON.
- Delete `POST /button` endpoint from `http_server.cpp`.
- Delete `IDriverClient::tap()` and the associated path in client.
- Delete client-side audio capture, detector, state machine wiring from `apps/micmap/main.cpp`. Keep only: settings UI, training UI (now IPC-driven), tray icon, manifest registrar, bindings patcher.
- Delete the audio callback's FFT/state-machine work — was already a hot-path violator.

**Exit criterion:** real-hardware UAT; client EXE binary size drops noticeably (no more KissFFT, no more audio capture in-proc); driver DLL takes over.

#### Phase 7 — Documentation (v1.5 carryover)
DOC-01 (README sync) + DOC-02 (`docs/architecture.md`) reflecting post-migration architecture.

### Parallel opportunities

- Phase 1 and Phase 2 can be done by the same engineer in sequence (each is small).
- Phase 4 can begin in parallel with Phase 3 once the shared-lib boundary (Phase 1) is in. Phase 4 is mostly client-side IPC plumbing; Phase 3 is driver-side detection.
- Phase 5 must follow Phase 4 (depends on the new IPC surface) but can begin once Phase 4's `/state` and `/settings` routes are stable.

### Risks per ordering choice

| Choice | Risk if WRONG order |
|--------|---------------------|
| Phase 1 before Phase 2 | If Phase 2 happens first, the driver gets a one-off ad-hoc copy of audio code that has to be reverted |
| Phase 2 before Phase 3 | If detection comes first, we hit "driver doesn't capture audio" with detection code in flight — wasted work |
| Phase 3 before Phase 4 | If IPC reshape comes first, the driver has new endpoints but client is still doing its own detection — endpoints sit unused, regression risk on the client side |
| Phase 5 before Phase 4 | Training endpoints need `/state` and `/settings` to exist for UI integration — Phase 5 can't even ship a usable UI without them |
| Phase 6 (flip + delete) before Phase 5 | Without training migration, the cutover removes the only way users can train new patterns. Hard blocker |

### Hard ordering invariant

**Phase 1 first. Always.** Without the shared lib, every other phase requires duplicating code into the driver, which then has to be unduplicated at cutover. The lift-to-shared-lib pattern is already proven in this repo by `src/bindings/` (the Phase 4 D-10 lift in v1.5) — same recipe.

---

## 6. Per-File / Per-Directory Component Delta

### Component status legend
- **NEW** — file/directory created in v1.6
- **MODIFIED** — existing file changed in scope (signature, behavior, or linkage)
- **DELETED** — removed from build at end of v1.6
- **UNCHANGED** — touched by linkage only or not at all

| Path | Status | What changes |
|------|--------|--------------|
| `src/CMakeLists.txt` | **MODIFIED** | Add `micmap_core_runtime` INTERFACE aggregate target. Existing `micmap::lib` umbrella stays for client EXE convenience |
| `src/audio/` | **UNCHANGED** | Headers + impl as-is. Linked into both client and driver via shared lib |
| `src/audio/CMakeLists.txt` | **UNCHANGED** | Already cleanly factored (PRIVATE platform deps) |
| `src/detection/` | **UNCHANGED** | Same as audio — no source changes |
| `src/detection/CMakeLists.txt` | **UNCHANGED** | KissFFT linkage already PRIVATE |
| `src/core/include/micmap/core/state_machine.hpp` | **UNCHANGED** | Interface stays; both processes use it (driver authoritatively, client reads state via IPC) |
| `src/core/include/micmap/core/config_manager.hpp` | **UNCHANGED** | Schema is shared; both processes link it. Client reads as fallback, driver is sole writer |
| `src/core/CMakeLists.txt` | **UNCHANGED** | nlohmann/json PRIVATE — clean |
| `src/common/` | **UNCHANGED** | Logger, types — both processes use |
| `src/steamvr/include/micmap/steamvr/vr_input.hpp` | **MODIFIED** | `IDriverClient::tap()` deleted at Phase 6. New methods: `getState`, `getTelemetry`, `getDevices`, `getSettings`, `putSettings`, `startTraining`, `getTrainingProgress`, `finalizeTraining`, `cancelTraining`. Recommend rename `IDriverClient` → `IDriverApi` |
| `src/steamvr/src/vr_input.cpp` | **MODIFIED** | `DriverClientImpl` rewritten around new endpoints; port discovery + retry logic preserved |
| `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` | **UNCHANGED** | Auto-launch is client-only |
| `src/bindings/` | **UNCHANGED** | Already correctly factored as a shared lib in v1.5 (Phase 4 D-10 lift) |
| `driver/CMakeLists.txt` | **MODIFIED** | Add `target_link_libraries(driver_micmap PRIVATE micmap_core_runtime)`. nlohmann::json continues to be linked for new IPC routes |
| `driver/src/driver_main.cpp` | **UNCHANGED** | `HmdDriverFactory` glue stays |
| `driver/src/device_provider.hpp` | **MODIFIED** | Add members: `std::unique_ptr<IAudioCapture> audioCapture_`, `std::unique_ptr<INoiseDetector> detector_`, `std::unique_ptr<IStateMachine> stateMachine_`, `std::unique_ptr<DetectionRunner> detectionRunner_`, `std::unique_ptr<IConfigManager> configManager_`, `std::atomic<std::shared_ptr<const AppConfig>> settings_` |
| `driver/src/device_provider.cpp` | **MODIFIED** | `Init` constructs the above and starts capture+detection. `Cleanup` reverses the order. RunFrame is **untouched** — still drains CommandQueue, still writes UpdateBooleanComponent. The whole detection pipeline is invisible to RunFrame, which is the architectural prize |
| `driver/src/command_queue.{hpp,cpp}` | **UNCHANGED** | Existing primitive is reused as-is. Producer changes from "HTTP thread" to "detection thread" — wiring change, not a primitive change |
| `driver/src/http_server.hpp` | **MODIFIED** | New routes wired in `SetupRoutes`. New collaborator pointer (`DeviceProvider*` or thinner facade) so handlers can read state and call into config/training operations from outside RunFrame |
| `driver/src/http_server.cpp` | **MODIFIED** | `POST /button` deleted at Phase 6. New routes: `/state`, `/telemetry/level`, `/devices`, `/settings`, `/training/*`. Discipline preserved: HTTP thread does NOT call any OpenVR API (still SVR-05) |
| `driver/src/detection_runner.{hpp,cpp}` | **NEW** | Owns the sample ring, the detection thread loop, and the state-machine TriggerCallback that pushes `TapCommand{}` into the existing CommandQueue |
| `driver/src/sample_ring.hpp` | **NEW** | SPSC lock-free ring used by WASAPI callback (producer) and DetectionRunner thread (consumer). Header-only |
| `driver/src/settings_snapshot.hpp` | **NEW** | Tiny wrapper around `std::atomic<std::shared_ptr<const AppConfig>>` with API: `load()` / `swap(new)`. Tests can construct one directly |
| `driver/src/training_session.{hpp,cpp}` | **NEW** | Tracks one training session's lifecycle (start, sample count, finalize, cancel). HTTP routes call into it. Delegates to the underlying `INoiseDetector` |
| `apps/micmap/main.cpp` | **HEAVILY MODIFIED → SLIMMED** | Delete: WASAPI callback body that runs FFT+state-machine (lines 353-444), `audioCapture` member, `detector` member, `stateMachine` member, `onTrigger` (line 515), `audioMutex`, RMS/dB inline computation. Keep: ImGui UI, tray icon, window message pump, async `IDriverApi::connect()` retry, `vrInput` for SteamVR Quit-event lifecycle (still need this for tray-icon dismissal on SteamVR shutdown), `manifestRegistrar` for `--register-vrmanifest` CLI mode, settings form that builds an `AppConfig` and PUTs it. Roughly: 1061 lines → ~500 lines |
| `apps/micmap/CMakeLists.txt` | **MODIFIED** | Drop direct dependency on `micmap_audio`+`micmap_detection` if removed via `micmap_lib` umbrella unlinking; cleaner: keep umbrella, but document the slimmed role. Continue linking `micmap::core_runtime` (for AppConfig schema), `micmap::steamvr` (for vrInput + manifest_registrar + IDriverApi), `micmap::bindings` (for uninstall path), ImGui |
| `apps/mic_test/CMakeLists.txt` | **MODIFIED (one line)** | `target_link_libraries(mic_test PRIVATE micmap_core_runtime)` instead of three libs. Verifies headless-build invariant |
| `apps/mic_test/main.cpp` | **UNCHANGED** | Still tests the headless detection pipeline against real microphones, no driver, no SteamVR. Regression backstop |
| `apps/hmd_button_test/main.cpp` | **UNCHANGED** | Pure VR-input test; doesn't touch detection |
| `apps/hmd_button_test/CMakeLists.txt` | **UNCHANGED** | |
| `installer/MicMap.iss` | **UNCHANGED** | Same files installed; no installer changes needed for the migration |
| `driver/resources/settings/default.vrsettings` | **MODIFIED** | Add new keys: `enableDriverDetection` (Phase 3 feature flag, default OFF until Phase 6), `enableDriverAudio` (Phase 2 spike flag — can be removed at Phase 6), `audioDeviceNamePattern` (default fallback if no config.json) |
| `tests/test_placeholder.cpp` | **UNCHANGED** | No new tests required by this analysis, though Phase 3 would benefit from a CommandQueue+DetectionRunner integration test fixture |
| `external/` | **UNCHANGED** | No new third-party deps |

---

## Data Flow — Before vs After

### Before (v1.5)

```
WASAPI thread (in micmap.exe)
  └─→ AudioCallback (under audioMutex)
        ├─→ RMS / dB calc
        ├─→ INoiseDetector::analyze (FFT, KissFFT)
        ├─→ IStateMachine::update
        │     └─→ TriggerCallback fires
        │           └─→ MicMapApp::onTrigger
        │                 └─→ IDriverClient::tap
        │                       └─→ POST /button {"kind":"tap"}  ─ HTTP ─→
                                                                    │
                                                                    ▼
                                                  HTTP thread (in driver_micmap.dll)
                                                    └─→ http_server::SetupRoutes
                                                          └─→ CommandQueue::push(TapCommand)
                                                                    │
                                                  RunFrame (vrserver) drains:
                                                    └─→ UpdateBooleanComponent(/input/system/click, true)
                                                    └─→ ... 150ms later: ...UpdateBooleanComponent(false)
                                                          └─→ SteamVR ToggleDashboard
```

### After (v1.6)

```
WASAPI thread (in driver_micmap.dll)
  └─→ AudioCallback
        └─→ SampleRing::push (lock-free, drop-oldest)

DetectionRunner thread (in driver_micmap.dll)
  └─→ loop: SampleRing::pop_chunk
        ├─→ INoiseDetector::analyze
        ├─→ IStateMachine::update
        │     └─→ TriggerCallback fires
        │           └─→ CommandQueue::push(TapCommand)
        │
        └─→ atomic snapshot of telemetry (level, confidence, state) for /telemetry/level

RunFrame (vrserver) drains:
  └─→ UpdateBooleanComponent(/input/system/click, true)
  └─→ ... 150ms later: ...UpdateBooleanComponent(false)
        └─→ SteamVR ToggleDashboard

[Sidecar IPC, opportunistic, NOT in trigger path]
HTTP thread (in driver_micmap.dll)
  ├─→ GET /state, /telemetry/level, /devices    ←──  client UI polls
  ├─→ PUT /settings → atomic-swap snapshot + config.json write   ←──  client edits
  └─→ POST /training/* → INoiseDetector training mode + persist  ←──  client trains
```

The trigger path collapses from a 7-hop cross-process flow to a 4-hop in-process flow. IPC moves from being a hot path to being a cold path (settings + telemetry only).

---

## Key Abstractions — New

### `DetectionRunner` (new, driver-only)

**Purpose:** owns the detection thread; bridges WASAPI capture to the existing CommandQueue.

**Pattern:** Worker thread with shutdown signal. Constructor takes references to `IAudioCapture`, `INoiseDetector`, `IStateMachine`, `CommandQueue`, `SettingsSnapshot`. Method `start()` launches thread; `stop()` joins. Thread loop: pop sample chunk, analyze, update state machine, repeat.

**Why not just a callback-on-callback chain?** Because the WASAPI callback must return promptly (per WASAPI documentation; AvSetMmThreadCharacteristics MMCSS isn't yet wired). FFT can take 1-3 ms on 2048 samples; on a slow machine that's a lot of audio-cb time. Decoupling via SPSC ring is the canonical fix and was flagged as tech debt in v1.5 (`.planning/codebase/CONCERNS.md:74-78`).

### `SettingsSnapshot` (new, driver-only)

**Purpose:** thread-safe read-mostly access to `AppConfig` for the detection thread.

**Pattern:** `std::atomic<std::shared_ptr<const AppConfig>>`. Read via `std::atomic_load(&snapshot_)`; write via `std::atomic_store(&snapshot_, std::make_shared<const AppConfig>(newCfg))`. Detection thread reads at the top of each iteration.

### `TrainingSession` (new, driver-only)

**Purpose:** single-session training state held outside the detector itself, so HTTP handlers can interrogate progress without locking detector internals.

**Pattern:** owned by `DeviceProvider`; HTTP routes mutate via `std::mutex`-guarded methods. On finalize, calls into `INoiseDetector::finishTraining()` (which stays single-threaded by virtue of being called only from the detection thread or from the HTTP thread when the detection thread is paused). Deletes itself on cancel/finalize.

### `IDriverApi` (renamed from `IDriverClient`, client-only)

**Purpose:** broadened HTTP client surface — settings, state, training, devices.

**Pattern:** same factory pattern (`createDriverApi(host, startPort, endPort)`); same retry/discovery semantics; new method set; client-side JSON serialize/deserialize via nlohmann/json.

---

## Patterns to Follow

### Pattern 1: Single-writer for cross-process state
**What:** Driver writes `config.json` and `training_data.bin`; client never does.
**When:** All persistent state.
**Example:** `PUT /settings` → driver validates → driver atomic-saves file → driver returns OK to client.

### Pattern 2: HTTP thread NEVER touches OpenVR API
**What:** Continuation of v1.5's SVR-05. HTTP handlers may now also touch detection state, audio device list, training session state — but still nothing through `vr::*`.
**When:** Every HTTP route handler.
**Example:** `device_provider.cpp:124` shows the existing pattern — HTTP push, RunFrame consumes. New routes must obey: e.g. `PUT /settings` mutates `settingsSnapshot_` (atomic) and may stop/restart the detector synchronously inside the handler — but **never** calls `vr::VRDriverInput()` or any `vr::*` surface.

### Pattern 3: Atomic shared_ptr for read-mostly cross-thread state
**What:** `std::atomic<std::shared_ptr<const T>>` for settings; reader gets a value snapshot, writer creates a new one.
**When:** Settings, telemetry snapshots.
**Why:** Lock-free reads on the detection hot path; writers are rare (user edits).

### Pattern 4: Bounded SPSC ring with drop-oldest
**What:** Audio sample ring between WASAPI cb thread and detection thread.
**When:** Any producer/consumer where producer must not block.
**Caveat:** The existing CommandQueue uses a mutex+deque with the same drop-oldest policy at depth 8. SPSC ring is for the higher-rate audio path; the existing mutex queue is fine for the low-rate command path.

### Pattern 5: CommandQueue boundary preserved
**What:** Even though detection moves into the driver, the CommandQueue → RunFrame contract stays. Detection thread `push`es; RunFrame `try_pop`s; OpenVR API only called from RunFrame.
**Why:** Don't break what works. The CommandQueue is the v1.5 hard-won correctness boundary.

---

## Anti-Patterns to Avoid

### Anti-Pattern 1: Calling OpenVR API from the detection thread
**What:** Tempting to "just call `UpdateBooleanComponent` directly from `TriggerCallback`" since we're now in-process.
**Why bad:** OpenVR's not-thread-safe-on-non-RunFrame-threads contract is real (validated empirically in bey-closer-t1; SteamVR crashes if violated).
**Instead:** Push `TapCommand{}` into CommandQueue; let RunFrame drain it. Same as v1.5.

### Anti-Pattern 2: Two writers for `config.json`
**What:** Client writes on settings change, driver writes on training finalize.
**Why bad:** Race between concurrent writers; either writer can clobber the other's update.
**Instead:** Driver is sole writer. Client's "save" path is `PUT /settings` (which the driver writes for it).

### Anti-Pattern 3: File-watching the config from the driver
**What:** Use `ReadDirectoryChangesW` to detect client-side edits.
**Why bad:** Re-introduces two-writer problem; spurious notifications during atomic rename writes; complicates testing.
**Instead:** Single-writer + IPC push.

### Anti-Pattern 4: Per-field settings endpoints
**What:** `PUT /settings/sensitivity`, `PUT /settings/cooldown`, etc.
**Why bad:** Multiplies endpoints; makes atomic multi-field changes (e.g. "sensitivity AND threshold change together") impossible without versioning.
**Instead:** Single `PUT /settings` with the full `AppConfig`. Client computes the diff if it cares.

### Anti-Pattern 5: Reusing `micmap::lib` (the existing INTERFACE umbrella)
**What:** Linking `micmap_core_runtime`'s contents into `micmap::lib`.
**Why bad:** `micmap::lib` (`src/CMakeLists.txt:12`) already pulls in `micmap_steamvr` which has the OpenVR + cpp-httplib dependency; using it from the driver re-imports those into the DLL (wasteful) and makes the headless-test invariant murky.
**Instead:** New `micmap_core_runtime` aggregate. Old `micmap::lib` stays for the client EXE.

### Anti-Pattern 6: Removing `IDriverClient::tap()` before Phase 6
**What:** Eager cleanup during Phase 3.
**Why bad:** Loses the rollback path; a Phase 4 IPC bug could brick the trigger flow before driver-side detection is proven.
**Instead:** Carry the dead path until Phase 6's flag flip. Accept the temporary code bloat.

---

## Scalability Considerations

| Concern | Today | Migration impact |
|---------|-------|------------------|
| Audio capture rate | ~96 KB/s @ 48 kHz mono float | Same — unchanged |
| Detection FFT throughput | ~50 FFTs/sec @ 2048 samples | Same — moved, not increased |
| IPC traffic | 1 trigger/sec peak | Drops to near-zero (settings + telemetry polls only) |
| Driver memory | ~20 MB (estimated) | +5 MB (KissFFT plan, training profile, ring buffer) |
| HMD reactivation cycles | 5-cycle stress test PASS in v1.5 | Audio capture survives independently of HMD container reattach |

The migration doesn't introduce new scaling axes. It shifts costs from one process to the other and eliminates IPC on the hot path.

---

## Sources

- `.planning/codebase/ARCHITECTURE.md` — current layered architecture (v1.5 snapshot)
- `.planning/codebase/STRUCTURE.md` — directory layout and CMake targets
- `.planning/codebase/CONCERNS.md` — known v1.5 tech debt (state-machine thread safety, audio buffer accumulation)
- `.planning/codebase/INTEGRATIONS.md` — IPC contract today (cpp-httplib :27015, JSON)
- `.planning/PROJECT.md` (v1.6 milestone definition, sidecar-on-HMD constraints)
- Codebase verification (HIGH confidence — every named symbol/path read on branch `hmd-button` 2026-04-30):
  - `CMakeLists.txt:1-223`
  - `src/CMakeLists.txt:1-20`
  - `src/audio/CMakeLists.txt:1-41` and `src/audio/include/micmap/audio/audio_capture.hpp:1-107`
  - `src/audio/src/audio_capture.cpp:196,233,435,453,521,531` (CoInitializeEx + capture thread)
  - `src/detection/CMakeLists.txt:1-26` and `src/detection/include/micmap/detection/noise_detector.hpp:1-148`
  - `src/core/CMakeLists.txt:1-30`, `src/core/include/micmap/core/state_machine.hpp:1-141`, `src/core/include/micmap/core/config_manager.hpp:1-137`
  - `src/steamvr/CMakeLists.txt:1-105`, `src/steamvr/include/micmap/steamvr/vr_input.hpp:1-215`
  - `driver/CMakeLists.txt:1-165`
  - `driver/src/driver_main.cpp:1-41`
  - `driver/src/device_provider.{hpp,cpp}` — full file
  - `driver/src/command_queue.hpp:1-41`
  - `driver/src/http_server.{hpp,cpp}` — full files
  - `apps/micmap/main.cpp:340-498` (audio cb + shutdown ordering)
  - `apps/mic_test/CMakeLists.txt:1-25` (the headless invariant)
- Sister project: `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — sidecar-on-HMD validation, audio-thread coexistence with driver runtime (referenced via PROJECT.md context)

---

*Architecture analysis: 2026-04-30 — for v1.6 Feature Migration roadmap*

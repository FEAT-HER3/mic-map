# Phase 8: IPC Contract Reshape — Research

**Researched:** 2026-05-05
**Refresh:** 2026-05-05 (force-refresh requested by `/gsd-research-phase`; supersedes commit b0e9a3c)
**Domain:** Localhost HTTP IPC reshape inside an OpenVR driver DLL (cpp-httplib + nlohmann/json + atomic-snapshot config publish + injected logger sinks)
**Confidence:** HIGH (extends shipped v1.5/P5/P6/P7 patterns; no novel primitives)

## Refresh Delta vs. b0e9a3c

| Section | Status | Notes |
|---------|--------|-------|
| User Constraints | unchanged | CONTEXT.md is the canonical source; copied verbatim. |
| Phase Requirements | unchanged | IPC-01..08, LIB-04, HEALTH-01..07 mapping verified against REQUIREMENTS.md. |
| Architectural Responsibility Map | unchanged | All tier ownerships re-verified against current branch (`hmd-button` HEAD `407d8b1`). |
| Standard Stack | unchanged | cpp-httplib v0.14.3 pin confirmed at `external/CMakeLists.txt:62`. |
| Architecture diagram | unchanged | Path through `HttpServer` ctor unchanged; P7 ctor signature already takes `driverDetectionActiveGetter` callback (verified at `http_server.hpp:67`). |
| Patterns | unchanged | P7 D-15 atomic-snapshot pattern still load-bearing; verified `detection_runner.cpp` writes `std::atomic_load_explicit` shape. |
| Don't Hand-Roll | unchanged | All in-tree assets re-located (`config_manager.cpp:280, 322` for `backupAndRotate` / `writeAtomicWindows`). |
| Pitfalls | one new entry — **Pitfall 6 (delta)** | Verified `DriverClient::connect()` at `vr_input.cpp:127-155` uses **Result-truthiness check** (`if (res && res->status == 200)`), NOT the v0.14+ `Result::error()` enum. Rename plan (`08-01`) MUST upgrade this to error-classification or HEALTH-01 indicator will flap on read-timeouts (Pitfall 6). |
| Code Examples | unchanged | All five examples re-verified against shipped sources. |
| State of the Art | unchanged | No upstream cpp-httplib advisories beyond CVE-2025-46728 since 2026-05-05. |
| Assumptions Log | one assumption confirmed — **A6 promoted** | `Result::error()` confirmed available in v0.14.x AND v0.20.1 (in-tree httplib.h read). Note: in-tree code at `vr_input.cpp:144` does NOT use it yet — uses truthiness only. The pitfall mitigation is forward-looking, not retrospective. |
| Validation Architecture | unchanged | Wave 0 gap list (16 scaffolds + 4 lints) still binding. |
| Open Questions | unchanged | All five still open; recommendations stand. |

**Bottom line:** Prior research holds. Refresh re-anchors every in-tree citation against current HEAD and surfaces one execution-time precision (Pitfall 6 mitigation requires a code change, not just a doc update).

## Goal Restatement

Replace v1.5's "trigger-over-HTTP" client→driver IPC with a settings/health/telemetry contract owned by the driver: six new HTTP routes (`GET /state`, `GET /telemetry/level`, `GET /devices`, `GET /settings`, `PUT /settings`, `POST /state/clear-error`), driver becomes the sole writer of `%APPDATA%\MicMap\config.json`, `IDriverClient` is renamed to `IDriverApi`, the client UI gains a driver-health pane (HEALTH-01..07), and LIB-04 ships injected logger sinks (`FileLogSink`, `DriverLogSink`, `MultiSinkLogger`) so the shared lib carries zero `#ifdef MICMAP_DRIVER_BUILD`. `POST /button` survives until P10 as the v1.5 rollback path.

The phase ships the IPC reshape; it does NOT flip the `enable_driver_detection` default — that stays OFF through P8 (P10 owns the cutover). Settings flow through `PUT /settings` regardless of which side runs detection.

**Phase requirements covered:** IPC-01, IPC-02, IPC-03, IPC-04, IPC-05, IPC-07, IPC-08, LIB-04, HEALTH-01..07. (IPC-06 — driver as sole writer of `training_data.bin` — is mapped to P8 in REQUIREMENTS.md but covered here only by inheritance via the same `ReplaceFileW` helper; the actual training-data ownership transfer ships in P9 per CONTEXT.md `<deferred>`.)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### A. JSON dependency in the driver
- **D-01:** `nlohmann/json` is pulled into `driver_micmap.dll` directly (not via `micmap::core_runtime`). Driver TUs (`http_server.cpp`, new `driver/src/config_io.cpp`, new `driver/src/settings_validator.cpp`) include `<nlohmann/json.hpp>`; the shared core lib stays json-free so `mic_test.exe` continues to build without nlohmann symbols.
- **D-02:** CMake lint extends to assert `<nlohmann/json.hpp>` does NOT appear in any TU under `src/audio/`, `src/detection/`, `src/core/`, `src/common/`. Sibling file: `cmake/AssertNoJsonInCore.cmake`. CI runs on every build.
- **D-03:** AppConfig serialization is hand-written `to_json` / `from_json` ADL hooks (nlohmann's `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` is acceptable for plain POD subsections; explicit ADL functions for sections that need version-checking or range-clamping). Schema source-of-truth is `src/core/include/micmap/core/config_manager.hpp` AppConfig.
- **D-04:** Wire format is identical to the v1.5 client-written `config.json` on disk. No envelope, no version bump. Forward-compat: unknown fields ignored on read; missing fields fall back to defaults. PUT /settings rejects only structurally-broken JSON.

#### B. cpp-httplib bump (CVE-2025-46728)
- **D-05:** Bump v0.14.3 → v0.20.1 ships as plan `08-00`. Prereq for every other P8 plan; isolated commit; isolated revert.
- **D-06:** Smoke test = v1.5 trigger path UAT (`POST /button` → CommandQueue → `/input/system/click`) + Phase 6/7 flag-OFF regression. No new endpoints exercised at 08-00.

#### C. Config writer cutover protocol
- **D-07:** Atomic cutover. Driver becomes sole `config.json` writer the moment IPC-04 (`PUT /settings`) lands. Client write path deleted in the same plan that wires `PUT /settings`.
- **D-08:** Client read path stays alive: client reads `config.json` once at startup via `ConfigManager::loadDefault()`. After boot, client never re-reads from disk; live updates flow via `GET /settings` poll OR are pushed by the same PUT call the client itself initiated.
- **D-09:** UI control change semantics: PUT → on 200 optimistically mutate client's in-memory snapshot → on 4xx roll back UI → on ECONNREFUSED refuse the edit. Settings UI gated on driver-loaded indicator (HEALTH-01).
- **D-10:** Driver `Init` reads `config.json` once with 3-attempt retry on `ERROR_SHARING_VIOLATION` (50 ms backoff). On 3 failures, load defaults + log warning; PUT still works.

#### D. Client UI scope (HEALTH-01..07)
- **D-11:** New driver-health pane surfacing `/state` + `/telemetry/level` + `/devices`: driver-loaded indicator, SteamVR-running, detection-state pill, last-trigger relative timestamp, last_error + Clear button, RMS/dBFS level meter (5 Hz visible / 0.5 Hz tray), device-disappeared indicator with "Re-pick" button.
- **D-12:** Existing detection-config widgets (sensitivity slider, threshold, cooldown, min-duration, device picker) stay; only their on-change handlers change. Rewired to `PUT /settings` per D-09.
- **D-13:** Device picker source switches from client-side WASAPI enumeration to `GET /devices`. Polled on UI open; cached for the session unless `audio_device_state=missing`. v1.5 client-side enumeration code stays for `mic_test.exe`.

#### E. Validation + error envelope
- **D-14:** PUT /settings is **all-or-nothing**. Validate full candidate AppConfig → swap atomic snapshot AND persist via `ReplaceFileW` (HTTP 200) OR reject HTTP 400 + `{"field":"first_failed_field","reason":"human-readable"}` with zero state mutation.
- **D-15:** Hand-rolled validator. Per-field validators in `driver/src/settings_validator.cpp` returning `std::optional<ValidationError>`. First-failed-field reporting (no multi-error aggregation).
- **D-16:** `last_error` is monotonic. POST /state/clear-error sets the atomic to null. Concurrent error fires after the clear simply overwrite null. No queue, no error history.

#### F. /devices and /telemetry/level cadence
- **D-17:** `GET /devices` enumerates via the driver's existing WASAPI enumerator. Result cached **1 second**; invalidated immediately on `IMMNotificationClient` device-add/remove.
- **D-18:** `GET /telemetry/level` returns the most-recent RMS/dBFS reading via a `std::atomic<float>` on the AudioWorker (or DetectionRunner) — read-without-lock from the HTTP thread. 5 Hz visible / 0.5 Hz tray; client-side timeout 250 ms.

#### G. LIB-04 logger sinks
- **D-19:** Existing `ILogger`/`setLogger` shape is the foundation. P8 adds `FileLogSink` (`src/common/src/sinks/file_log_sink.cpp`), `DriverLogSink` (`driver/src/sinks/driver_log_sink.cpp` — wraps `vr::VRDriverLog()` with SafeDriverLog Rule-3 guard), `MultiSinkLogger` (`src/common/src/multi_sink_logger.cpp`).
- **D-20:** Driver wires `MultiSinkLogger{DriverLogSink, FileLogSink("%APPDATA%\\MicMap\\micmap-driver.log")}` at `DeviceProvider::Init` BEFORE `httpServer_->Start()`. Client wires `MultiSinkLogger{StdoutLogSink, FileLogSink("%APPDATA%\\MicMap\\micmap.log")}` at `WinMain` startup. `ConsoleLogger` becomes (or gets adapted into) `StdoutLogSink`.
- **D-21:** Zero `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime`. Composition root pattern: each binary's startup constructs the sink list and calls `Logger::setLogger(...)`.

#### H. IDriverClient → IDriverApi rename
- **D-22:** Pure rename in plan `08-01` (after the cpp-httplib bump). Touches `src/steamvr/include/micmap/steamvr/vr_input.hpp`, `src/steamvr/src/vr_input.cpp`, `apps/micmap/main.cpp`, all tests. Mechanical; one commit, no semantic changes.
- **D-23:** New methods (`getState()`, `getSettings()`, `putSettings()`, `getDevices()`, `getTelemetryLevel()`, `clearError()`) added incrementally per their endpoint plan. `tap()` survives until P10 MIG-05. P7 `getHealth().driver_detection_active` untouched.

#### I. HTTP route discipline (IPC-08, v1.5 SVR-05)
- **D-24:** All new routes handle work entirely on the HTTP thread — no `vr::*`, no CommandQueue push. Reads are atomic-snapshot loads; writes mutate atomics directly. SVR-05 boundary survives unchanged.
- **D-25:** All routes bind `127.0.0.1` only.
- **D-26:** Client poll cadence — `/health` 1 Hz; `/state` 2 Hz visible / 0.5 Hz tray; `/telemetry/level` 5 Hz visible / 0.5 Hz tray; `/settings` on UI open + after PUT 200; `/devices` on UI open + on device-disappeared.

#### J. UAT regimen (D-27, D-28)
- 7 mandatory UAT scenarios on Bigscreen Beyond + Win11 Pro: settings round-trip, validation rejection, driver-down UX, last_error clear, netstat localhost-only, logger sinks, cpp-httplib bump regression. 100-PUT stress.

#### K. Plan structure (rough, per D-29)
- **08-00 (Wave 0):** cpp-httplib v0.20.1 bump + smoke test
- **08-01 (Wave 1):** `IDriverClient` → `IDriverApi` rename + LIB-04 sinks + composition wiring
- **08-02 (Wave 2):** nlohmann/json into driver TUs + `AssertNoJsonInCore.cmake` + AppConfig ADL + driver Init reads `config.json`
- **08-03 (Wave 3):** GET endpoints — `/state`, `/settings`, `/devices`, `/telemetry/level`
- **08-04 (Wave 4):** PUT /settings + atomic apply + ReplaceFileW + settings_validator + `POST /state/clear-error`
- **08-05 (Wave 5):** Client UI driver-health pane + UI control rewire
- **08-06 (Wave 6):** UAT D-27(1)..(7) + D-28 stress + post-UAT default-OFF preservation
- **D-30:** P7's `enable_driver_detection` flag stays default OFF. P8 does not flip it.

### Claude's Discretion

- `FileLogSink` flush cadence (per-line / per-N-lines / time-based) — pick whatever doesn't lose data on `vrserver.exe` crash.
- `ConsoleLogger` rename vs adapter — pick whichever produces the smallest diff in `apps/micmap/main.cpp`.
- `settings_validator.cpp` internal layout — flat free functions vs visitor pattern; either is fine for ≤ 30 fields.
- `GET /devices` cache TTL — 1 s starting point; tune by feel. Doc the chosen value in PLAN.
- `MultiSinkLogger::log()` mutex strategy — single mutex or per-sink mutex; standard mutex is fine for the volume.
- Exact poll cadence values within HEALTH-06 tray — 0.5 Hz floor; can drop further if SteamVR isn't running.
- File rename of `vr_input.hpp` → `driver_api.hpp` (recommended for symmetry; keeps grep clean).
- `GET /settings` snapshot vs disk re-read — recommended in-memory snapshot (matches v1.5 SVR-05).
- Validation error path style — JSON-Pointer (`/audio/deviceId`) vs dot-paths (`audio.deviceId`); pick whichever the existing `nlohmann/json::json_pointer` sugar makes natural. Document in PLAN.

### Deferred Ideas (OUT OF SCOPE)

- `POST /button` deletion + `IDriverClient::tap()` removal — **P10 (MIG-05)**
- `enable_driver_detection` default flip ON — **P10 cutover**
- HEALTH-08 tray-icon state glyphs, FAIL-01..05, INST-09, TEST-01..03/05 — **P10**
- Log-file rotation (5 MB cap, 5 generations) — **P10 (TEST-03)**
- Training endpoints + `training_data.bin` ownership transfer — **P9 (TRAIN cluster)**
- `OnDefaultDeviceChanged` follow-the-default + device pinning — **P10+** (P8 wires enumeration only)
- `DeviceNotificationClient` ComPtr migration — **P10+**
- FFT-on-every-frame perf cost — backlog
- Multi-error aggregation on PUT — single-field reporting is enough for v1.6
- Structured `last_error` (severity, code, history) — single-string + clear is the v1.6 shape
- `/debug/snapshot` driver endpoint (TEST-D2) — already deferred per PROJECT.md
- Detection-accuracy work in noisy environments (DET-01/02) — out of v1.6
- Replacing localhost HTTP with named pipes / shared memory (IPC-AF-02) — out of scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| IPC-01 | `GET /state` returns `{driver_loaded, steamvr_running, detection_state, last_trigger_at, last_error, audio_device_id, audio_device_state}` | Atomic-snapshot pattern from P7 D-15 generalizes to a `DriverState` struct populated by AudioWorker (device_state, device_id) + DetectionRunner (state machine pill, last_trigger_at) + DeviceProvider error channel. HTTP handler reads atomics, marshals JSON, returns. |
| IPC-02 | `GET /telemetry/level` returns current RMS/dBFS for 5–10 Hz polling, 250 ms client timeout | `std::atomic<float>` on AudioWorker State (extending the existing `sample_rate` atomic shape at `audio_worker.hpp` published-at-runtime field). Lock-free read from HTTP thread. RMS already computed by audio cb under `MICMAP_DEBUG_RMS_LOG`; lift the computation out of the ifdef and store it. |
| IPC-03 | `GET /devices` returns enumerated WASAPI capture devices (id, friendly name, default flag) | Reuse `IAudioCapture::enumerateDevices()` already invoked by AudioWorker. Wrap result in a 1 s TTL cache (D-17). Invalidate on `IMMNotificationClient` add/remove (Pitfall 13 alive-flag already in place from P6). |
| IPC-04 | `GET /settings` (current AppConfig); `PUT /settings` (full AppConfig, atomic validate → apply → persist; HTTP 400 with `{"field","reason"}` on rejection) | Hand-rolled validator (D-15) with first-failed-field semantics. Validation order: schema → range → commit (Pitfall 7.3). Persist via the existing `writeAtomicWindows` helper at `src/core/src/config_manager.cpp:322` — lifted into a driver-callable `config_io.cpp` (D-14). |
| IPC-05 | Driver is sole writer; reads at Init with 3-attempt SHARING_VIOLATION retry, 50 ms backoff | Direct application of Pitfall 5 mitigation. Implementation lives in `driver/src/config_io.cpp` (new). Existing `ConfigManagerImpl::load` already handles missing-file → defaults; extend with retry loop on Win32 sharing-violation specifically. |
| IPC-06 | Driver is sole writer of `training_data.bin` after `POST /training/finalize` | NOTE: ownership transfer scheduled for P9 per CONTEXT `<deferred>`; P8 ships only the IPC plumbing, not training. Mark IPC-06 as covered by P9. |
| IPC-07 | All endpoints bind `127.0.0.1` only | `HttpServer` ctor at `driver/src/http_server.hpp:64-67` already takes `host="127.0.0.1"` default. Verified by `netstat -an` UAT step (D-27.5). Add lint enforcing the literal. |
| IPC-08 | HTTP-thread → CommandQueue → RunFrame v1.5 SVR-05 boundary survives | New routes never call `vr::*` and never `commandQueue_->push(...)`. Reads = atomic-snapshot loads; writes = atomic mutations + ReplaceFileW. Verifiable by lint similar to `cmake/AssertDetectionRunnerNoVrApi.cmake`. |
| LIB-04 | Logger sinks injected at construction; no `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime` | Existing `Logger::setLogger(std::shared_ptr<ILogger>)` shape unchanged (verified at `src/common/include/micmap/common/logger.hpp:74-80`). New sinks: `FileLogSink`, `DriverLogSink`, `StdoutLogSink`, `MultiSinkLogger`. Composition root pattern (D-21). |
| HEALTH-01 | Driver-loaded indicator (red on ECONNREFUSED, green on success) | Existing `DriverClient::connect()` at `src/steamvr/src/vr_input.cpp:127-155` uses Result-truthiness for failure (`if (res && res->status == 200)`) — does NOT yet distinguish ECONNREFUSED from timeout. **P8 must upgrade to `httplib::Result::error()` enum** (Pitfall 6 mitigation; see Pitfall 6 below). |
| HEALTH-02 | SteamVR-running indicator derived from same poll | Free — `/health` only succeeds when driver is loaded inside vrserver, so HEALTH-02 = HEALTH-01 in P8 (no separate signal needed; SteamVR-running implied by reachable driver). |
| HEALTH-03 | Detection-state pill (idle/training/detecting/triggered/cooldown) from `GET /state` | Sourced from `DetectionRunner` state machine. Already exists in core; expose via atomic snapshot. |
| HEALTH-04 | Last-trigger relative timestamp ("3 s ago", "—") | Add `last_trigger_at` (UTC) to `DriverState` snapshot. Updated by DetectionRunner on rising-edge Triggered (right next to existing trigger-callback fire site in `detection_runner.cpp`). |
| HEALTH-05 | `last_error` display + `POST /state/clear-error` | `std::atomic<std::shared_ptr<const std::string>>` on DeviceProvider; cleared by HTTP handler. D-16 monotonic semantics. |
| HEALTH-06 | RMS/dBFS level meter at 5 Hz visible / 0.5 Hz tray | Implementation from IPC-02. Client polls per visibility state. |
| HEALTH-07 | Device-disappeared indicator on `audio_device_state ∈ {missing, permission_denied}` with "Re-pick" button | Reuse the existing `IMMNotificationClient` callback path (Pitfall 13 alive-flag from P6). On device-removed, set `audio_device_state=missing` in DriverState snapshot. UI surfaces it in the health pane and triggers a `GET /devices` refresh on click. |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **Stack locked this milestone:** C++17, CMake, ImGui+D3D11, WASAPI, KissFFT, **cpp-httplib** (bumped this phase), nlohmann/json, OpenVR SDK. P8 adds nothing new besides the cpp-httplib version bump.
- **Windows-only.** Non-Windows audio stubs remain. Bash via Git Bash; Unix-style paths in shell commands.
- **Driver runs without admin.** Installer is admin; runtime is not.
- **GSD discipline:** Do not skip phase artifacts. Per-task atomic commits; no `--no-verify` unless explicitly requested.
- **Active milestone:** "Seamless SteamVR Integration" / v1.6 Feature Migration; P8 is reshape, not framework swap.
- **Visual validation required** for any HEALTH-* indicator UX (D-27 #3, #6, #7) — type-checking and build-success do not substitute.

## Architecture / Approach

### Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| HTTP route registration + parsing | Driver / `driver/src/http_server.cpp` | — | v1.5 SVR-05 invariant; HTTP thread lives inside `vrserver.exe` as the driver DLL. |
| AppConfig validation | Driver / `driver/src/settings_validator.cpp` | — | Hand-rolled validator must NOT live in shared lib (D-02 lint forbids json there); driver-only. |
| AppConfig schema (struct definitions, field defaults) | Shared lib / `src/core/include/micmap/core/config_manager.hpp` | — | Source-of-truth shared by client + driver. P8 does NOT modify the struct. |
| AppConfig JSON ADL hooks (`to_json`/`from_json`) | Driver / `driver/src/config_io.cpp` | Client / `apps/micmap/src/config_json.cpp` (PUT body construction) | Both binaries serialize/deserialize, but each privately compiles the ADL hooks. mic_test stays json-free. (Open Q5: see below — recommended split-TU strategy.) |
| Atomic config write (`ReplaceFileW`) | Driver / `driver/src/config_io.cpp` | — | Lifted from `src/core/src/config_manager.cpp:322` into driver TU; client write path deleted at P8 cutover. |
| In-memory AppConfig snapshot | Driver / `driver/src/device_provider.{hpp,cpp}` | — | `std::atomic<std::shared_ptr<const AppConfig>>` member; HTTP handlers read/swap; DetectionRunner reads via existing P7 D-15 generalization. |
| WASAPI device enumeration | Driver / `driver/src/audio_worker.cpp` | mic_test (still uses client-side enumerator for headless harness) | Driver exposes via `GET /devices`; v1.5 client enumerator stays for `mic_test.exe`. |
| RMS / dBFS publication | Driver / `driver/src/audio_worker.cpp` (or `detection_runner.cpp`) | — | Audio callback computes once; publishes `std::atomic<float>`. HTTP thread reads. |
| Client UI driver-health pane | Client / `apps/micmap/main.cpp` (or new `apps/micmap/src/driver_health_pane.cpp`) | — | ImGui rendering; polls cadence per D-26. |
| Client driver-loaded indicator | Client / `apps/micmap/main.cpp` (extends existing `drvOk` indicator pattern) | — | ECONNREFUSED is the canonical signal; surface in red/green dot. **Requires upgrading `DriverClient::connect()` from Result-truthiness to `Result::error()` classification** (Pitfall 6). |
| Logger sinks (file/driver-log/stdout/multi) | Shared lib for sink interface + `FileLogSink` + `MultiSinkLogger` + `StdoutLogSink`; driver lib for `DriverLogSink` | — | DriverLogSink depends on `vr::VRDriverLog()` so it CANNOT live in shared lib (LIB-03 lint); other sinks have no driver deps. |
| Logger composition (assemble sink list) | Driver: `DeviceProvider::Init`. Client: `WinMain` startup. | — | Composition-root pattern (D-21). Shared lib never sees the choice. |
| `DriverState` snapshot (state pill, last_trigger_at, last_error, device id, device state) | Driver / `driver/src/device_provider.{hpp,cpp}` (new struct + atomic) | — | Single point where HTTP handlers, AudioWorker (device state changes), and DetectionRunner (state machine, last_trigger_at) write. |

### Recommended Project Structure (delta from current tree)

```
driver/src/
├── config_io.{hpp,cpp}         # NEW — load/save AppConfig with nlohmann + ReplaceFileW + 3-attempt retry
├── settings_validator.{hpp,cpp}# NEW — per-field validators, returns std::optional<ValidationError>
├── driver_state.hpp            # NEW — DriverState struct + atomic-snapshot wrapper (or inline into device_provider.hpp)
├── sinks/
│   └── driver_log_sink.{hpp,cpp} # NEW — wraps vr::VRDriverLog() with SafeDriverLog Rule-3 guard
└── http_server.{hpp,cpp}       # MODIFIED — 6 new routes; ctor gains snapshot getter + mutator + lister + clearer

src/common/src/
├── multi_sink_logger.{cpp}     # NEW — MultiSinkLogger fan-out under a mutex
└── sinks/
    ├── file_log_sink.cpp       # NEW — atomic append; flush per line (default), Claude's discretion
    └── stdout_log_sink.cpp     # NEW (or rename of ConsoleLogger)

src/common/include/micmap/common/
└── log_sink.hpp                # NEW — ILogSink interface (or reuse ILogger)

src/steamvr/include/micmap/steamvr/
└── driver_api.hpp              # RENAMED from vr_input.hpp (recommended; CONTEXT D-22)

src/steamvr/src/
└── driver_api.cpp              # RENAMED from vr_input.cpp + new methods

apps/micmap/src/
├── driver_health_pane.{hpp,cpp}# NEW (optional; can stay inline in main.cpp)
└── config_json.cpp             # NEW — client-side AppConfig ADL hooks for PUT body (Open Q5 option (c))

cmake/
└── AssertNoJsonInCore.cmake    # NEW — sibling of AssertNoOpenVRInCore.cmake
└── AssertHttpServerLocalhostOnly.cmake # NEW — IPC-07 freeze-the-literal lint
└── AssertHttpServerNoVrApi.cmake       # NEW — IPC-08 sibling of AssertDetectionRunnerNoVrApi
└── AssertNoConfigWriteInClient.cmake   # NEW — IPC-05 single-writer grep
```

### System Architecture (data flow post-P8)

```
┌──────────────────────── micmap.exe (client) ──────────────────────┐
│                                                                  │
│  ImGui UI (DriverHealthPane + existing controls)                 │
│        │                                                          │
│        │ on slider/picker change                                  │
│        ▼                                                          │
│  IDriverApi::putSettings(AppConfig)  ───┐                        │
│  IDriverApi::getState()                  │                        │
│  IDriverApi::getTelemetryLevel()         │  cpp-httplib client   │
│  IDriverApi::getDevices()                │  (v0.20.1)            │
│  IDriverApi::getSettings()               │                        │
│  IDriverApi::clearError()                │                        │
│  IDriverApi::tap()  [survives until P10] │                        │
│                                          │                        │
│  Logger: MultiSinkLogger{StdoutLogSink,  │                        │
│           FileLogSink("%APPDATA%/MicMap/ │                        │
│           micmap.log")}                  │                        │
└──────────────────────────────────────────┼────────────────────────┘
                                           │ POST/GET HTTP/1.1
                                           │ 127.0.0.1:27015..27025
┌──────────────────────────────────────────┼────────────────────────┐
│ vrserver.exe (driver_micmap.dll)         ▼                        │
│                                                                   │
│  HttpServer (cpp-httplib v0.20.1)                                │
│    routes:                                                        │
│      POST /button         (rollback path; survives P8)            │
│      GET  /health         (driver_loaded + driver_detection_active)│
│      GET  /port                                                   │
│      GET  /state          NEW (P8)                                │
│      GET  /telemetry/level NEW (P8)                              │
│      GET  /devices        NEW (P8)                                │
│      GET  /settings       NEW (P8)                                │
│      PUT  /settings       NEW (P8) — validate → swap snapshot →   │
│                                       ReplaceFileW → 200/400      │
│      POST /state/clear-error NEW (P8) — atomic null               │
│        │                                                          │
│        │ all read-only handlers: atomic snapshot loads            │
│        │ PUT /settings: validate → atomic.store(new) → persist   │
│        │ NO vr::* calls; NO commandQueue.push() (SVR-05)          │
│        ▼                                                          │
│  std::atomic<std::shared_ptr<const AppConfig>>  configSnapshot_  │
│  std::atomic<std::shared_ptr<const DriverState>> stateSnapshot_  │
│  std::atomic<float>                              rms_            │
│  std::atomic<std::shared_ptr<const std::string>> last_error_     │
│        ▲          ▲                                               │
│        │          │                                               │
│        │ HTTP-thread        DetectionRunner / AudioWorker         │
│        │ writes              writes                                │
│        │                                                          │
│  AudioWorker  ───►  ring  ───►  DetectionRunner                  │
│  (RMS, devs,                    (state pill, last_trigger_at,    │
│   device_state)                  TapCommand → CommandQueue)       │
│                                                                   │
│  CommandQueue ───► RunFrame ───► VRDriverInput()                  │
│                                  ::UpdateBooleanComponent          │
│                                  (only path that calls vr::*)     │
│                                                                   │
│  config.json reads on Init (3-attempt retry; D-10)               │
│  config.json writes on PUT /settings (atomic ReplaceFileW; D-14) │
│                                                                   │
│  Logger: MultiSinkLogger{DriverLogSink (vr::VRDriverLog),         │
│           FileLogSink("%APPDATA%/MicMap/micmap-driver.log")}      │
└───────────────────────────────────────────────────────────────────┘
```

### Pattern 1: Atomic Snapshot Publish/Load (generalized from P7 D-15)

**What:** Single `std::atomic<std::shared_ptr<const T>>` with HTTP-thread writers and many readers (HTTP for GET, detection for live use, audio for device-config reads). Reads are wait-free; writes swap shared_ptr atomically; old snapshot deallocates when last reader releases.

**When to use:** AppConfig (D-09), DriverState (state pill + last_trigger_at + device_id + device_state + last_error). Already proven in P7 for `DetectionConfig` (`detection_runner.hpp`, atomic-snapshot publish/load mechanism per P7 D-15).

**Example:**

```cpp
// driver/src/device_provider.hpp
#include <atomic>
#include <memory>
#include "micmap/core/config_manager.hpp"  // AppConfig

class DeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    // Lock-free getter for HTTP handlers
    std::shared_ptr<const core::AppConfig> getConfigSnapshot() const {
        // C++17 atomic_load free function (matches detection_runner.cpp pattern)
        return std::atomic_load_explicit(&configSnapshot_, std::memory_order_acquire);
    }

    // Validate → swap snapshot → persist. Called from PUT /settings handler.
    // Note (Pitfall 2): persist FIRST, swap second.
    bool applyValidatedConfig(core::AppConfig candidate) {
        if (!saveConfigJson(getConfigPath(), candidate)) return false;  // ReplaceFileW
        auto next = std::make_shared<const core::AppConfig>(std::move(candidate));
        std::atomic_store_explicit(&configSnapshot_, next, std::memory_order_release);
        return true;
    }

private:
    // Hot path: PUT /settings writes; HTTP GET, DetectionRunner, AudioWorker read
    std::shared_ptr<const core::AppConfig> configSnapshot_;
};
```

### Pattern 2: HTTP-thread-only mutation (no CommandQueue push from new routes)

**What:** Every new P8 route mutates atomics (or reads them) directly on the HTTP thread. None of `vr::VRDriverInput()`, `vr::VRProperties()`, `vr::VRServerDriverHost()`, `vr::VRSettings()` is called. CommandQueue stays single-producer (DetectionRunner) + the `POST /button` rollback path until P10.

**When to use:** All six new routes.

**Why it works:** v1.5 SVR-05 says HTTP thread → CommandQueue → RunFrame is the path that touches OpenVR. P8's new routes are data-plane only (settings + telemetry); no OpenVR action originates from them. Verifiable by a sibling lint to `cmake/AssertDetectionRunnerNoVrApi.cmake` scanning `http_server.cpp` for `vr::*`. Note: `http_server.cpp` already passes — the lint just locks it in.

### Pattern 3: Composition-root logger (LIB-04)

**What:** Each binary's startup builds the sink list it wants and calls `Logger::setLogger(std::make_shared<MultiSinkLogger>(sinks))`. Shared lib code only ever calls `Logger::info(...)`.

**Driver composition root:**

```cpp
// driver/src/device_provider.cpp - early in Init, BEFORE httpServer_->Start()
auto sinks = std::vector<std::shared_ptr<ILogSink>>{
    std::make_shared<DriverLogSink>(),       // wraps vr::VRDriverLog with Rule-3 guard
    std::make_shared<FileLogSink>(getAppDataPath() / "micmap-driver.log")
};
auto logger = std::make_shared<MultiSinkLogger>(std::move(sinks));
Logger::setLogger(logger);
```

**Client composition root:**

```cpp
// apps/micmap/main.cpp - WinMain prologue
auto sinks = std::vector<std::shared_ptr<ILogSink>>{
    std::make_shared<StdoutLogSink>(),       // existing ConsoleLogger renamed/adapted
    std::make_shared<FileLogSink>(getAppDataPath() / "micmap.log")
};
Logger::setLogger(std::make_shared<MultiSinkLogger>(std::move(sinks)));
```

**Note on `ILogSink` vs `ILogger`:** Two design options surfaced during research:

(a) Add a new `ILogSink` interface that's distinct from `ILogger`; `MultiSinkLogger` implements `ILogger` and composes `ILogSink`s.
(b) Reuse `ILogger` for sinks too; `MultiSinkLogger` implements `ILogger` and composes `ILogger`s.

Recommend **(a)** for clarity — `setMinLevel`/`getMinLevel` on the logger are about user-facing filtering, not per-sink concerns; sinks shouldn't have to implement them. But (b) is cheaper if minimizing diff matters. Claude's discretion (D-19 references this).

### Anti-Patterns to Avoid

- **File-watching `config.json` from the driver.** Pitfall 5 / IPC-AF-01. Rejected; use HTTP push.
- **Partial AppConfig PUT semantics** (`{"sensitivity": 0.7}`). Adds ambiguity over which fields were "intentionally absent vs. omitted-meaning-default." Pitfall 7.3. P8 D-14 = full AppConfig only.
- **Multi-error aggregation in PUT response.** Increases response shape complexity without UX value. D-15 = first-failed-field only.
- **`#ifdef MICMAP_DRIVER_BUILD` inside shared lib.** LIB-04 explicit. P5's `cmake/lint_no_driver_macro.cmake` already enforces; P8 must not introduce new violations.
- **Health endpoint that grabs mutex held by audio thread.** Pitfall 16. All new GET handlers read `std::atomic` only.
- **`0.0.0.0` bind for any new route.** Pitfall 7.5. Hard-locked by `HttpServer` ctor default; lint should freeze the literal.
- **Mutating client's `ConfigManager` snapshot before PUT response confirms 200.** Would cause UI rollback to fight in-memory state. D-09 ordering: PUT → on 200 swap → on 4xx rollback UI.
- **Treating any HTTP failure as ECONNREFUSED.** Pitfall 6. `nullptr` Result includes timeouts, DNS failures, and connection refused — only `Result::error() == Error::Connection` is "driver down."

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| HTTP server / client | Custom socket + parser | cpp-httplib v0.20.1 (already locked) | Already in tree at `external/CMakeLists.txt:62`; CVE-fixed; same wire format as v1.5. |
| JSON parse / serialize | Hand-rolled | nlohmann/json v3.11.2 (already vendored) | D-01 explicitly chooses nlohmann; symbol bloat constrained by D-02 driver-only lint. |
| Atomic file replace | Custom `MoveFileExW` dance | Existing `writeAtomicWindows` at `src/core/src/config_manager.cpp:322` | v1.5 CFG-04 already battle-tested; lift into `driver/src/config_io.cpp`. [VERIFIED 2026-05-05 grep on current branch] |
| Corruption backup with rotation | Custom timestamp scheme | Existing `backupAndRotate` at `src/core/src/config_manager.cpp:280` | v1.5 CFG-05 already retains 5 generations; reuse verbatim. [VERIFIED 2026-05-05 grep on current branch] |
| WASAPI device enumeration | Re-roll `IMMDeviceEnumerator` walk | Existing `IAudioCapture::enumerateDevices()` already invoked by AudioWorker | Already used by AudioWorker; trivially reused for `GET /devices`. |
| RMS computation | Re-roll | Existing audio cb in `audio_worker.cpp` (under `MICMAP_DEBUG_RMS_LOG`) | Lift the `sumSq → sqrt` block out of the ifdef and store result in a `std::atomic<float>` on State. |
| JSON Schema validation | nlohmann/json-schema or hand-rolled JSON Schema | Hand-rolled per-field validators (D-15) | AppConfig has ≤ 30 fields; per-field validators in 100-200 LoC are simpler than dragging in `nlohmann/json-schema-validator`. CONTEXT explicitly chose hand-rolled. |
| Settings change broadcast (e.g. ReadDirectoryChangesW) | Custom file-watcher thread | HTTP push via `PUT /settings` | Pitfall 5 / IPC-AF-01 — rejected. |
| `vr::VRDriverLog` Rule-3 guard | Re-roll Rule-3 logic | Existing `SafeDriverLog` from P6 (`driver/src/driver_log.hpp`) | Already wraps `vr::VRDriverContext()` null-check; wrap *that* in `DriverLogSink::log(...)`. |
| Atomic snapshot publish | Custom mutex-guarded copy | `std::atomic<std::shared_ptr<const T>>` (P7 D-15 pattern) | Lock-free; already proven for DetectionConfig; generalizes to AppConfig + DriverState. |
| Connection-refused detection | `if (!result)` truthiness | `result.error() == httplib::Error::Connection` | Truthiness collapses connect-refused, timeout, and DNS into one signal — flaps HEALTH-01 indicator under load. Pitfall 6 mandates the precise enum check. |

**Key insight:** Every primitive P8 needs already exists in the tree. P8 is reshape and composition, not invention. The only genuinely new code is: 6 HTTP route handlers (each ~30 LoC), a per-field validator (~150 LoC), three log sinks (~50 LoC each), one CMake lint (~50 LoC), and one client-side ImGui health pane (~150-300 LoC). Everything else is wiring.

## Common Pitfalls

### Pitfall 1: Validation order regression — clamp before schema (Pitfall 7.3)

**What goes wrong:** Validator clamps `sensitivity=-1.0` to `0.0` (silent), commits. Original out-of-range value never logged. UI rolled forward, user thinks the value is what they typed.

**Why it happens:** Easy to reach for `clamp()` first. v1.5 CFG-03 already does clamp/pow2-snap on disk-read defaults, but those clamps are inside `readDetection()`/`readAudio()` *after* type-check.

**How to avoid:** Per-field validator returns `std::optional<ValidationError>` BEFORE any clamp. If the value is out of range → HTTP 400 with `{"field":"detection.sensitivity","reason":"must be in [0.0, 1.0]; got -1.0"}`. No partial mutation. Clamp only runs in the disk-read path (forward-compat for existing on-disk configs).

**Warning signs:** UAT D-27(2) "validation rejection" passes but `GET /settings` returns the clamped value instead of the prior value. Fix: validator runs before clamp.

### Pitfall 2: PUT /settings persist failure after snapshot publish

**What goes wrong:** Validator passes → `configSnapshot_.store(next)` → `ReplaceFileW` fails (disk full, ACL change). In-memory snapshot reflects the new value; disk is stale. Next driver restart reverts.

**Why it happens:** `ReplaceFileW` can fail unpredictably; we publish before persist for low latency.

**How to avoid:** Two viable options — pick one and document:

- **(A) Persist-first, then publish.** Do `saveConfigJson(...)` first; only on success swap the snapshot. HTTP 200 means both done; 500 means neither. *Cost:* `ReplaceFileW` is on the HTTP thread (acceptable per CONTEXT D-24 — "PUT writes mutate atomic + persist via `ReplaceFileW`; the only blocking call; documented as acceptable since PUT is rare").
- **(B) Publish-first, persist after, roll back snapshot on failure.** Lower visible latency; more state to manage on failure.

**Recommendation: (A) persist-first.** PUT /settings is rare (user-initiated UI change, not on-trigger). Keeping snapshot-and-disk in lockstep simplifies failure semantics. HTTP 500 + structured error on persist failure; snapshot unchanged.

**Warning signs:** UAT D-27(1) round-trip succeeds initially, but a force-fail scenario (ACL test, read-only disk) leaves the driver and disk disagreeing. Fix: persist-first ordering.

### Pitfall 3: Logger sink wired AFTER first log line

**What goes wrong:** `Logger::setLogger()` called inside `DeviceProvider::Init` AFTER `httpServer_->Start()`. HTTP server logs its "starting" message via the default `ConsoleLogger` (or whatever `Logger::logger_` defaulted to in `logger.cpp`), bypassing the new file sink.

**Why it happens:** `Logger::logger_` is statically initialized to `std::make_shared<ConsoleLogger>()`. Until `setLogger()` runs, all calls flow there.

**How to avoid:** Wire `Logger::setLogger(MultiSinkLogger{...})` as the FIRST step in driver `Init` and the FIRST step in client `WinMain`, BEFORE any other `MICMAP_LOG_*` call. CONTEXT D-20 says "BEFORE step 4 `httpServer_->Start()`" — strengthen this to "BEFORE any other Init step."

**Warning signs:** Driver log file exists but starts mid-session; first 1-2 messages appear in `vrserver.txt` but not in `micmap-driver.log`. Fix: move `setLogger` earlier.

### Pitfall 4: Driver Init `config.json` read uses non-Win32 retry on linux fallback

**What goes wrong:** `ConfigManagerImpl::load` is platform-agnostic; the SHARING_VIOLATION retry only applies on Windows. If P8 lifts the read function as-is, non-Windows builds (mic_test) might still call it but get an unrelated error. Less concerning given driver is Windows-only, but mic_test invokes the same shared-lib `ConfigManagerImpl::load` on stub paths.

**Why it happens:** `ERROR_SHARING_VIOLATION` is Win32-specific.

**How to avoid:** The retry loop lives in `driver/src/config_io.cpp` (driver-only TU), NOT in shared lib `config_manager.cpp`. The shared lib `load(path)` continues to work as today (single-attempt, sets defaults on missing). Driver wraps with retry: catch sharing-violation specifically by checking `GetLastError()` after a failed `std::ifstream::open`, retry up to 3 times with 50 ms backoff.

**Warning signs:** Race-only failures on slow disk; 1-in-100 driver Init reads to defaults instead of last-saved values. Fix: ensure retry path is exercised by an integration test (open `config.json` for exclusive write in another thread, then call driver Init).

### Pitfall 5: New routes silently bind 0.0.0.0 if a future contributor changes the host literal

**What goes wrong:** Someone adds `--listen-all` for "convenient debugging," ships it. Settings now editable from LAN.

**Why it happens:** No guardrail beyond manual `netstat` UAT.

**How to avoid:** Add a new lint `cmake/AssertHttpServerLocalhostOnly.cmake` that scans `driver/src/http_server.cpp` for any `bind_to_port` / `listen` / `set_default_host` invocation and asserts the host literal is `"127.0.0.1"`. Sibling shape to existing P7 `AssertDetectionRunnerNoVrApi.cmake`. Run on every CTest invocation.

**Warning signs:** UAT D-27(5) `netstat` returns `0.0.0.0:27015`. Fix: lint freezes the literal; PR breaks CI before it merges.

### Pitfall 6: ECONNREFUSED-vs-timeout misclassification (DELTA — code change required, not just doc)

**What goes wrong:** Client polls `/health` and treats *any* failure as "driver down." A 250 ms timeout because the driver is busy serving a 100-PUT storm shows up as red, then green again, flapping the indicator.

**In-tree status:** Verified at `src/steamvr/src/vr_input.cpp:144` — current code uses `if (res && res->status == 200)`, which collapses *all* failure modes (Connection, Read timeout, Write, SSLConnection, Compression) into one "driver down" bucket.

**Why it happens:** cpp-httplib's `httplib::Client::Get()` returns a `Result` that is falsy when ANY of those errors happens; truthiness check is the path of least resistance.

**How to avoid:** ECONNREFUSED is the canonical signal (HEALTH-01). Distinguish via `result.error()` (cpp-httplib v0.14+ exposes a `Error` enum on the Result wrapper). Map only `Error::Connection` → driver-loaded=false. Map `Error::Read` (timeout) → keep prior state, log warning. Document this in the client polling code so a future bump (cpp-httplib v0.20.1) that changes the enum doesn't silently break it.

```cpp
// Recommended replacement at vr_input.cpp:127-155
auto res = client.Get("/health");
if (res && res->status == 200) {
    // … connected
} else if (!res) {
    using E = httplib::Error;
    if (res.error() == E::Connection) {
        // ECONNREFUSED — driver is down. Indicator → red.
    } else if (res.error() == E::Read || res.error() == E::Write) {
        // Timeout / IO. Driver maybe busy. Keep prior state; don't flap indicator.
    } else {
        // Unknown — log and treat as transient.
    }
}
```

**Plan placement:** This code change SHOULD land in plan `08-01` (the rename plan) so the new `IDriverApi` ships with the precise-error pattern from the start; otherwise plan `08-05` (client UI driver-health pane) is forced to either replicate the change or build HEALTH-01 on a flaky signal.

**Warning signs:** Driver-loaded indicator flickers under PUT-storm UAT D-28. Fix: precise error classification.

### Pitfall 7: `last_error` race between cleared-then-fired

**What goes wrong:** User clicks "Clear" → `POST /state/clear-error` → driver atomic stores null. Concurrent device-removed callback fires inside the same window → stores new error. Client's NEXT poll sees the new error and the user thinks Clear didn't work.

**Why it happens:** D-16 explicitly accepts this: "concurrent error fires after the clear simply overwrites null with the new error."

**How to avoid:** This is intentional per CONTEXT D-16. Surface it in the UAT script: after Clear, the next `/state` poll should return null *only* if no new error has fired in the interim. UAT D-27(4) needs to wait long enough between the Clear and the next poll to make this deterministic in a quiet test scenario. Document the semantic in the API contract so client UX doesn't promise "Clear is permanent."

**Warning signs:** UAT D-27(4) is flaky on devices that intermittently report state changes (Bluetooth headsets). Fix: scope the UAT to a stable mic; document the deliberate semantic.

### Pitfall 8: Composition-root logger initialized too late on the client

**What goes wrong:** Mirror of Pitfall 3 on the client side. `WinMain` runs early `MICMAP_LOG_*` lines (e.g., `MicMapApp::initialize()` runs before sink wiring), which flow to the default `ConsoleLogger` and never reach `micmap.log`.

**How to avoid:** Move `Logger::setLogger(MultiSinkLogger{...})` to BEFORE `MicMapApp::initialize()` in `WinMain` — typically right after `WinMain` opens (after `CommandLineToArgvW` / CLI flag parse, but before any other code path).

**Warning signs:** UAT D-27(6) shows partial logs in `micmap.log`. Fix: hoist sink wiring to `WinMain` prologue.

### Pitfall 9: `IDriverApi` rename leaves stale `vr_input.hpp` references

**What goes wrong:** D-22 file rename misses one include path or a comment string. Build still succeeds because the new header is found via include search, but the old name appears in error messages and grep results.

**How to avoid:** Run `grep -rn 'vr_input\|IDriverClient\|driver_client\|DriverClient' src/ apps/ tests/ driver/` after the rename plan and fix every hit. Comment-only references are fine to fix lazily; symbol/name references must update. Note: `tests/` may have references too — `gsd-execute-phase` `08-01` MUST grep all of `src/`, `apps/`, `driver/`, AND `tests/`.

**Warning signs:** New contributor confused by mismatched names. Fix: thorough grep sweep in plan 08-01.

### Pitfall 10: ConsoleLogger constructor signature mismatch when wrapping

**What goes wrong:** P8 either renames `ConsoleLogger` to `StdoutLogSink` or wraps it in an adapter. If the wrapper inherits from `ILogSink` (option (a) in Pattern 3) and `ConsoleLogger` inherits from `ILogger`, the adapter must translate the `setMinLevel` calls — easy to forget.

**How to avoid:** Pick option (a) AND remove `setMinLevel` from `ILogSink` (it's a logger concern). MultiSinkLogger holds a per-logger min-level filter; sinks unconditionally emit anything they receive. Documented as the recommended path; D-19 leaves the choice as Claude's discretion.

**Warning signs:** Build error in adapter; or runtime regression where DEBUG lines appear in production logs. Fix: document min-level ownership clearly in `ILogSink` header.

## Code Examples

### Example 1: AppConfig ADL hooks for nlohmann (D-03, driver-only TU)

```cpp
// driver/src/config_io.cpp
#include "micmap/core/config_manager.hpp"
#include <nlohmann/json.hpp>

namespace micmap::core {

// Schema-source-of-truth = AppConfig in shared header. Hooks defined in
// driver TU only; client also defines hooks privately if it needs to
// serialize for PUT body. mic_test never includes nlohmann.
void to_json(nlohmann::json& j, const AudioConfig& c) {
    j = nlohmann::json{
        {"deviceNamePattern", /* utf8 conversion existing helper */ },
        {"deviceId",          /* utf8 ... */ },
        {"bufferSizeMs",      c.bufferSizeMs}
    };
}

void from_json(const nlohmann::json& j, AudioConfig& c) {
    // Default-on-missing-or-wrong-type matches v1.5 readWString/readInt
    // behavior at src/core/src/config_manager.cpp.
    if (j.contains("bufferSizeMs") && j["bufferSizeMs"].is_number_integer())
        c.bufferSizeMs = j["bufferSizeMs"].get<int>();
    // ... (same shape for deviceNamePattern, deviceId)
}

// Repeat pattern for DetectionConfig, SteamVRConfig, TrainingConfig, AppConfig.
}
```

### Example 2: PUT /settings handler (skeleton — persist-first per Pitfall 2)

```cpp
// driver/src/http_server.cpp - inside SetupRoutes()
server_->Put("/settings", [this](const httplib::Request& req, httplib::Response& res) {
    // 1. Parse JSON
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::exception&) {
        res.status = 400;
        res.set_content(R"({"error":"malformed JSON body"})", "application/json");
        return;
    }

    // 2. Build candidate AppConfig
    core::AppConfig candidate;
    try {
        candidate = body.get<core::AppConfig>();  // from_json ADL
    } catch (const nlohmann::json::exception& e) {
        res.status = 400;
        nlohmann::json err = {{"field", "(structural)"}, {"reason", e.what()}};
        res.set_content(err.dump(), "application/json");
        return;
    }

    // 3. Validate (first-failed-field semantics)
    if (auto v = validateSettings(candidate); v.has_value()) {
        res.status = 400;
        nlohmann::json err = {{"field", v->field}, {"reason", v->reason}};
        res.set_content(err.dump(), "application/json");
        return;
    }

    // 4. Persist FIRST (Pitfall 2 — option A); configMutator_ does
    //    saveConfigJson() FIRST, then atomic_store_explicit on success.
    if (!configMutator_(candidate)) {
        res.status = 500;
        res.set_content(R"({"error":"persist failed"})", "application/json");
        return;
    }

    // 5. configMutator_ already swapped the snapshot inside DeviceProvider
    res.set_content(R"({"status":"ok"})", "application/json");
});
```

### Example 3: GET /telemetry/level (lock-free read)

```cpp
// driver/src/http_server.cpp
server_->Get("/telemetry/level", [this](const httplib::Request&, httplib::Response& res) {
    const float rms = rmsGetter_();   // wraps audioWorker_->state_->rms.load(acquire)
    const float dBFS = (rms > 0.0f)
        ? 20.0f * std::log10(rms)
        : -120.0f;
    nlohmann::json body = {{"rms", rms}, {"dBFS", dBFS}};
    res.set_content(body.dump(), "application/json");
});
```

### Example 4: DriverLogSink with SafeDriverLog Rule-3 guard

```cpp
// driver/src/sinks/driver_log_sink.cpp
#include "driver_log_sink.hpp"
#include "driver_log.hpp"           // existing SafeDriverLog wrapper from P6
#include "micmap/common/logger.hpp"

namespace micmap::driver {

void DriverLogSink::log(LogLevel level, std::string_view message) {
    // SafeDriverLog already guards vr::VRDriverContext() != nullptr (P6 fix).
    // Adding "[level] " prefix here keeps vrserver.txt readable.
    std::string line = std::string("[") + logLevelToString(level) + "] "
                     + std::string(message) + "\n";
    SafeDriverLog("%s", line.c_str());
}

}  // namespace
```

### Example 5: AssertNoJsonInCore.cmake (sibling lint)

```cmake
# cmake/AssertNoJsonInCore.cmake
# P8 D-02: assert no <nlohmann/json.hpp> appears in shared-lib TUs.
# Sibling of cmake/AssertNoOpenVRInCore.cmake. Driver TUs may include json freely;
# shared lib must stay json-free so mic_test.exe builds without nlohmann symbols.

set(_targets_dirs
    "${CMAKE_SOURCE_DIR}/src/audio"
    "${CMAKE_SOURCE_DIR}/src/detection"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/common")

set(_violations "")
foreach(_dir ${_targets_dirs})
    file(GLOB_RECURSE _files
        "${_dir}/*.hpp"
        "${_dir}/*.cpp"
        "${_dir}/*.h")
    foreach(_file ${_files})
        file(READ "${_file}" _content)
        if(_content MATCHES "[<\"]nlohmann/json\\.hpp[>\"]")
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoJsonInCore: ${_vcount} file(s) violate the no-json-in-shared-lib rule (D-02 / Pitfall 15):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertNoJsonInCore: clean")
```

### Example 6: Pitfall 6 mitigation in DriverApi::connect

```cpp
// src/steamvr/src/driver_api.cpp (renamed from vr_input.cpp)
// Pitfall 6 fix: distinguish ECONNREFUSED from timeout

ConnectResult DriverApi::connect() {
    for (int port = startPort_; port <= endPort_; ++port) {
        httplib::Client client(host_, port);
        client.set_connection_timeout(1);
        client.set_read_timeout(1);

        auto res = client.Get("/health");
        if (res && res->status == 200) {
            // … connected (unchanged)
            return ConnectResult::Connected;
        }
        if (!res) {
            using E = httplib::Error;
            switch (res.error()) {
                case E::Connection:
                    // ECONNREFUSED — driver is down on this port. Try next.
                    continue;
                case E::Read:
                case E::Write:
                    // Timeout / IO — driver maybe busy. Keep prior state.
                    return ConnectResult::Timeout;  // caller maps to "keep previous indicator"
                default:
                    return ConnectResult::OtherError;
            }
        }
    }
    return ConnectResult::NotFound;  // every port returned ECONNREFUSED
}
```

## State of the Art

| Old Approach (v1.5) | New Approach (P8) | When Changed | Impact |
|--------------------|--------------------|--------------|--------|
| Trigger over HTTP (`POST /button` is the load-bearing route) | Trigger in-process via CommandQueue (P7) + new IPC = settings/health/telemetry only | P7 ships in-process trigger; P8 ships the new IPC contract; P10 deletes `POST /button` | Latency on trigger drops; HTTP becomes a control plane only. |
| Client writes `config.json` directly via `saveDefault()` | Driver is sole writer; client edits via `PUT /settings` | P8 (atomic cutover D-07) | No two-process race (Pitfall 5). No file-watching. |
| `ConsoleLogger` is the only sink | `MultiSinkLogger` composes `StdoutLogSink`+`FileLogSink`+`DriverLogSink` | P8 (LIB-04) | Driver and client each get their own log files; vrserver.txt still receives driver logs. |
| `IDriverClient` interface | `IDriverApi` interface | P8 plan 08-01 | Naming reflects expanded surface (settings/state/devices, not just trigger). |
| cpp-httplib v0.14.3 | cpp-httplib v0.20.1 (CVE-2025-46728 fixed) [VERIFIED 2026-05-05: github.com/yhirose/cpp-httplib v0.20.1 tag exists] | P8 plan 08-00 | Wire format compatible; documented bump. |
| nlohmann/json only in client | nlohmann/json in driver TUs (driver_micmap.dll) too | P8 plan 08-02 | Driver can serialize/validate AppConfig; mic_test stays json-free via lint. |
| Truthiness-only HTTP error check (`if (res && ...)`) | `Result::error() == Error::Connection` for canonical ECONNREFUSED | P8 plan 08-01 (Pitfall 6 mitigation) | HEALTH-01 indicator stable under PUT-storm; doesn't flap on transient timeouts. |

**Deprecated/outdated (in-tree):**

- `ConsoleLogger` may be renamed to `StdoutLogSink` or wrapped — Claude's discretion (D-19).
- The "HEALTH-D" differentiators (HEALTH-D1..D4) were considered for v1.6 in early research but only HEALTH-D4 (tray-icon glyphs) and HEALTH-D1 (per-component badges) survive — HEALTH-D4 is **deferred to P10** (HEALTH-08); HEALTH-D1 is **deferred to a future GUI revamp milestone** per STATE.md. P8 ships HEALTH-01..07 only.
- cpp-httplib v0.43.3 is the current upstream release [CITED: github.com/yhirose/cpp-httplib/releases page, last verified 2026-05-04]. CONTEXT D-05 explicitly chooses v0.20.1 (the minimum CVE-fixed version) to minimize wire-format / API drift risk. Document the version pin and CVE rationale in PLAN; revisit in a future deps-refresh phase.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | cpp-httplib v0.20.1 has wire-format compatibility with v0.14.3 for the routes used in v1.5 (`POST /button`, `GET /health`, `GET /port`, `GET /status`) | cpp-httplib bump (D-05/D-06) | Smoke test (08-00 UAT) catches breakage. Revert is one commit. Mitigation already designed-in. |
| A2 | The existing audio cb in `audio_worker.cpp` can be lifted out of the `MICMAP_DEBUG_RMS_LOG` ifdef without regressing performance — RMS is already computed today in the debug build, just not stored | IPC-02 telemetry | Verifiable by quick local profile (FFT inside the audio cb is the dominant cost per CONCERNS.md backlog item; another sumSq-and-sqrt is negligible). |
| A3 | `IMMNotificationClient` callbacks fire on a non-audio thread inside WASAPI; safe to use them as cache invalidators for `GET /devices` without contending with audio cb | `GET /devices` D-17 | Pitfall 13 alive-flag already protects against use-after-free; cache invalidation is a single atomic store. |
| A4 | C++17 `std::atomic_load_explicit(&shared_ptr)` is available on the Microsoft toolchain used in this project (it's deprecated in C++20 but present) | Atomic-snapshot pattern | Already in production at `detection_runner.cpp` (P7 D-15). No risk. |
| A5 | The existing `writeAtomicWindows` helper at `src/core/src/config_manager.cpp:322` can be lifted into a driver TU with no shared-lib changes (the helper already has no shared-lib state besides the path argument) | Persist plumbing for PUT /settings | [VERIFIED 2026-05-05] grep confirms it's a free function (anonymous namespace at file scope); copy-paste is safe but should be refactored to a header to avoid maintenance drift. |
| A6 | `cpp-httplib` v0.14.x exposes `Result::error()` returning an `Error` enum with `Connection` / `Read` / `Success` etc. | Pitfall 6 ECONNREFUSED classification | [VERIFIED 2026-05-05] In-tree `httplib.h` (v0.14.3) defines `enum class Error { Success, Unknown, Connection, BindIPAddress, Read, Write, ExceedRedirectCount, Canceled, SSLConnection, SSLLoadingCerts, SSLServerVerification, UnsupportedMultipartBoundaryChars, Compression }`. Surface NOT YET used by current `vr_input.cpp:144` (truthiness only) — Pitfall 6 mitigation is forward-looking, REQUIRES code change in plan 08-01. |
| A7 | All P8 success criteria can be validated without visual UAT *except* the "live driver-loaded indicator" + "level meter" + "device-disappeared indicator" UX checks (D-27 #3, #6, #7) | Validation Architecture | Headless tests cover JSON shapes, validation rejection, single-writer grep, localhost bind, and SVR-05 lint. Visual UAT handles the indicators that depend on ImGui rendering on a live driver/SteamVR. |
| A8 | "ECONNREFUSED IS the canonical 'driver down' signal — no separate liveness ping exists" (CRITERION 3) means the client treats `httplib::Result::error() == Error::Connection` as the red-light condition. The canonical signal mapping: any non-Connection error keeps the prior indicator state (don't flap on transient timeouts) | HEALTH-01 implementation | Aligns with Pitfall 6 mitigation; CONTEXT validates this implicitly via D-09 ("on `ECONNREFUSED` (driver down), client refuses the edit"). |
| A9 | The `apps/micmap` UI panel layout has room for a new driver-health pane without redesign — current `renderUI()` already has separated sections (Status, Audio Device, ...) and a new section is purely additive | Client UI scope (D-11) | Read of `main.cpp` confirms section-by-section layout with `ImGui::Separator()` dividers (`08-UI-SPEC.md` documents the existing v1.5 baseline at `apps/micmap/main.cpp:914`). Adding a "Driver Health" section is mechanical. |
| A10 | The cpp-httplib build flags currently set in driver/CMakeLists.txt remain valid in v0.20.1 (e.g., `CPPHTTPLIB_NO_EXCEPTIONS` per INTEGRATIONS.md) | cpp-httplib bump | Verifiable by reading the v0.20.1 README; if a new define landed (e.g., `CPPHTTPLIB_USE_BROTLI`) it's opt-in. |
| A11 | The `cmake/AssertNoJsonInCore.cmake` lint scope is shared-lib only (`src/audio/`, `src/detection/`, `src/core/`, `src/common/`) — `apps/` and `driver/` are NOT linted because client and driver may use nlohmann | D-02 lint | [CITED: CONTEXT D-02 verbatim] "in any TU under `src/audio/`, `src/detection/`, `src/core/`, `src/common/`" — `apps/micmap/` not in list, so client TUs may still use nlohmann. Open Q5 recommendation (c) keeps the lint simple by splitting ADL hooks across a driver-only TU + a client-only TU. |

## Runtime State Inventory

> P8 is partly a refactor (rename `IDriverClient` → `IDriverApi`, delete client-side `saveDefault` write path) but mostly an additive feature phase. The rename component qualifies for the inventory.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| **Stored data** | `%APPDATA%\MicMap\config.json` already exists; schema unchanged (D-04). Driver Init reads it; no migration needed. `training_data.bin` ownership transfer is **P9, not P8**. | None for `config.json` (read-compatible). For `training_data.bin`: deferred to P9. |
| **Live service config** | None — MicMap doesn't register with external services like n8n / Tailscale / Cloudflare. SteamVR's `vrpathreg`-registered driver dir is unaffected by P8. | None. |
| **OS-registered state** | `app.vrmanifest` registration via `manifest_registrar` (existing). P8 doesn't touch the manifest. The manifest still references `app_key=bigscreen.micmap`. | None. |
| **Secrets and env vars** | None. MicMap has no secrets / API keys. The `enable_driver_audio` and `enable_driver_detection` `vrsettings` keys (P6/P7) are unaffected by the rename. | None. |
| **Build artifacts / installed packages** | Two artifacts — `micmap.exe` and `driver_micmap.dll` — both rebuild from source on every `cmake --build`. The `IDriverClient` → `IDriverApi` rename will regenerate object files cleanly; no stale `.o` / `.obj` survives a full rebuild. The Inno Setup installer (v1.5) does NOT bake in either symbol name; ship-able binaries are version-stamped, not symbol-stamped. | Document a clean-rebuild step in plan 08-01 instructions ("rm -rf build/ before testing"). |

**The canonical question:** *After every file in the repo is updated, what runtime systems still have the old "IDriverClient" / `config.json` write path cached?*

- **Test artifacts**: `tests/test_*.cpp` may reference `IDriverClient` — grep sweep in plan 08-01 covers this.
- **Documentation strings**: comments referring to "DriverClient" are fine to update lazily; symbol references must update.
- **Built binaries on user machines**: irrelevant since the installer ships the new build atomically.
- **`config.json` content schema**: unchanged in P8 (D-04). Old-format config files keep working.

## Environment Availability

> Phase has no new external dependencies. All required tools and SDKs are already in tree or assumed present from prior phases.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| MSVC C++17 | P8 build | ✓ | (existing project setup) | — |
| CMake ≥ 3.20 | Build system | ✓ | (existing) | — |
| OpenVR SDK | DriverLogSink uses `vr::VRDriverLog`; rest of P8 doesn't add OpenVR usage | ✓ | (existing — locked v2.5.1+ per PROJECT.md) | — |
| cpp-httplib v0.20.1 | All HTTP routes (server in driver, client in app) | ✓ on bump | v0.20.1 (tag confirmed) [VERIFIED: github.com/yhirose/cpp-httplib v0.20.1 tag] | Revert to v0.14.3 if smoke fails (D-06) |
| nlohmann/json v3.11.2 | Driver TUs only (D-01) | ✓ | (existing pin in `external/CMakeLists.txt:10`) [VERIFIED 2026-05-05] | — |
| ImGui v1.90.1 + D3D11 | Client UI driver-health pane | ✓ | (existing pin in `external/CMakeLists.txt:81`) [VERIFIED 2026-05-05] | — |
| WASAPI / `IMMDeviceEnumerator` | `GET /devices` enumerator | ✓ | Windows SDK (existing) | — |
| Win32 `SHGetFolderPathW(CSIDL_APPDATA)` / `SHGetKnownFolderPath` | Log file paths | ✓ | (existing usage at `config_manager.cpp` and P7 fix `WR-06` per recent commit `6c99986`) | — |
| Win32 `ReplaceFileW` | PUT /settings persist | ✓ | (existing usage at `config_manager.cpp:322`) | — |
| Bigscreen Beyond + Win11 Pro rig | UAT D-27(1)..(7) and D-28 | ✓ | (existing rig from P5/P6/P7 UATs) | — |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest registering Google-Test-style and CMake-script tests (existing pattern). The project uses CMake `add_test(NAME ... COMMAND ...)` with a mix of:<br/> - C++ test executables linking shared lib (e.g., `tests/test_config_manager.cpp`)<br/> - Driver-headless C++ tests (`tests/driver/audio_worker_lifecycle_headless.cpp`, `detection_settings_propagation_test.cpp`)<br/> - Pure-cmake script lints (`-P AssertNoOpenVRInCore.cmake`) |
| Config file | Root `CMakeLists.txt` + `tests/CMakeLists.txt` (existing) |
| Quick run command | `ctest --test-dir build -R '<filter>' --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| IPC-01 | `GET /state` returns the documented JSON shape with all fields | integration (driver headless) | `ctest -R DriverGetStateShape` | ❌ Wave 0 |
| IPC-02 | `GET /telemetry/level` returns numeric `rms` and `dBFS` fields | unit (HTTP handler with mocked AudioWorker) | `ctest -R DriverGetTelemetryLevel` | ❌ Wave 0 |
| IPC-03 | `GET /devices` returns a JSON array; cache TTL respected | integration | `ctest -R DriverGetDevicesCache` | ❌ Wave 0 |
| IPC-04 | `PUT /settings` happy path: validate → persist → swap; round-trip via `GET /settings` | integration (round-trip) | `ctest -R DriverPutSettingsRoundTrip` | ❌ Wave 0 |
| IPC-04 | `PUT /settings` validation rejection: HTTP 400 + structured envelope; no state mutation | unit (validator) + integration | `ctest -R DriverPutSettingsValidation` | ❌ Wave 0 |
| IPC-05 | Driver is sole writer (grep audit) | script | `ctest -R AssertNoConfigWriteInClient` (NEW grep lint) | ❌ Wave 0 |
| IPC-05 | `Init` retries 3× on `ERROR_SHARING_VIOLATION` | unit (mock filesystem) | `ctest -R DriverInitConfigShareViolation` | ❌ Wave 0 |
| IPC-07 | Localhost-only bind | script | `ctest -R AssertHttpServerLocalhostOnly` (NEW lint) | ❌ Wave 0 |
| IPC-08 | No `vr::*` in HTTP route handlers | script | `ctest -R AssertHttpServerNoVrApi` (NEW lint, sibling of `AssertDetectionRunnerNoVrApi.cmake`) | ❌ Wave 0 |
| LIB-04 | No `#ifdef MICMAP_DRIVER_BUILD` in shared lib | script | `ctest -R AssertNoDriverMacroInCore` | ✅ Already exists (`cmake/lint_no_driver_macro.cmake` per P5) |
| LIB-04 | No `<nlohmann/json.hpp>` in shared lib | script | `ctest -R AssertNoJsonInCore` (NEW lint) | ❌ Wave 0 |
| LIB-04 | Sinks compose without `#ifdef` (composition-root pattern) | unit (instantiate `MultiSinkLogger` in headless test) | `ctest -R MultiSinkLoggerFanOut` | ❌ Wave 0 |
| HEALTH-01 | Client maps `httplib::Error::Connection` → red, `Success` → green; non-Connection errors keep prior state (Pitfall 6) | unit (mock httplib::Result) | `ctest -R ClientDriverLoadedIndicator` | ❌ Wave 0 |
| HEALTH-03..07 | Visual indicators rendered correctly | manual UAT D-27(3)+(4)+(7) | n/a (visual on rig) | n/a |
| HEALTH-06 | Level meter polls 5 Hz visible / 0.5 Hz tray | unit (mock window-visibility flag → measure poll cadence) | `ctest -R ClientLevelMeterCadence` | ❌ Wave 0 |
| (Cross) | cpp-httplib bump regression | integration (existing v1.5 trigger UAT, automated) | `ctest -R V15TriggerPathSmoke` | ❌ Wave 0 (or repurposed existing) |
| (Cross) | 100-PUT stress with no leak | stress integration | `ctest -R DriverPutSettingsStress100` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build -R '^(Driver|Client|Assert).*' --output-on-failure` (excludes long-running stress test) — completes in <30 seconds for the unit + script tier.
- **Per wave merge:** `ctest --test-dir build --output-on-failure` (full suite, including 100-PUT stress).
- **Phase gate:** Full suite green + UAT D-27(1)..(7) + D-28 sign-off before `/gsd-verify-work`.

### Wave 0 Gaps

- [ ] `tests/driver/get_state_shape_test.cpp` — covers IPC-01 JSON shape
- [ ] `tests/driver/get_telemetry_level_test.cpp` — covers IPC-02
- [ ] `tests/driver/get_devices_cache_test.cpp` — covers IPC-03
- [ ] `tests/driver/put_settings_round_trip_test.cpp` — covers IPC-04 happy path
- [ ] `tests/driver/put_settings_validation_test.cpp` — covers IPC-04 rejection (no state mutation)
- [ ] `tests/driver/put_settings_stress100_test.cpp` — covers D-28 stress (100 PUTs, no leak)
- [ ] `tests/driver/init_config_share_violation_test.cpp` — covers IPC-05 retry path
- [ ] `tests/driver/state_clear_error_test.cpp` — covers HEALTH-05 monotonic semantic
- [ ] `tests/test_multi_sink_logger.cpp` — covers LIB-04 fan-out
- [ ] `tests/test_settings_validator.cpp` — covers per-field validator (range, enum, file-path sanity)
- [ ] `tests/test_client_driver_loaded_indicator.cpp` — covers HEALTH-01 ECONNREFUSED classification (Pitfall 6)
- [ ] `tests/test_client_level_meter_cadence.cpp` — covers HEALTH-06 5/0.5 Hz polling
- [ ] `cmake/AssertNoJsonInCore.cmake` — D-02 lint (script test)
- [ ] `cmake/AssertHttpServerLocalhostOnly.cmake` — IPC-07 lint (script test, NEW)
- [ ] `cmake/AssertHttpServerNoVrApi.cmake` — IPC-08 lint, sibling of `AssertDetectionRunnerNoVrApi.cmake` (script test, NEW)
- [ ] `cmake/AssertNoConfigWriteInClient.cmake` — IPC-05 lint, grep `'config.json'` against `apps/micmap/src/` and `src/steamvr/src/` (NEW)
- [ ] `tests/CMakeLists.txt` — register the new tests + lints

*(All Wave 0 gaps must be authored or scaffolded before subsequent waves run; the existing pattern from P7 plan 07-01 is the template.)*

## Open Questions

1. **`POST /state/clear-error` HTTP semantics — should it be idempotent?**
   - What we know: D-16 says "monotonic; cleared by `POST /state/clear-error` sets the atomic to null. Concurrent error fires after the clear simply overwrites null." A client that calls clear twice in succession on the same error is fine.
   - What's unclear: Should clear-error return 200 even when there is no error to clear, or 404? cpp-httplib defaults to 200 for any handler that sets content; explicit choice needed.
   - Recommendation: **Always return 200 with `{"status":"ok"}`.** Simplest semantics; client UX is "the error display is now empty" regardless of prior state.

2. **`GET /telemetry/level` schema — `rms` only, or include `dBFS` and other fields?**
   - What we know: IPC-02 requires "current RMS / dBFS reading." HEALTH-06 says "5 Hz when visible, 0.5 Hz when minimized."
   - What's unclear: Is `dBFS` derived client-side or server-side? Does the response include peak, or only mean RMS?
   - Recommendation: Return both `rms` (linear, [0..1]) and `dBFS` (computed server-side: `20*log10(rms)`, clamped to -120 dB floor). Single source of truth on the server. No peak in v1.6 (HEALTH-D2 trigger sparkline is deferred). UI-SPEC.md confirms the `{rms_normalized, dbfs}` shape — align JSON keys with UI expectations (`rms_normalized` instead of `rms` is acceptable; PLAN should pick one).

3. **Should `GET /devices` populate `is_default` from WASAPI's `eMultimedia` or `eConsole` role?**
   - What we know: Existing enumerator populates `isDefault` per device; CONTEXT references "default flag" generically.
   - What's unclear: WASAPI has multiple "default" roles (eConsole/eCommunications/eMultimedia). Audio capture commonly uses eMultimedia.
   - Recommendation: Use whatever the existing `IAudioCapture::enumerateDevices()` already returns (it's marked `isDefault` based on whichever role the existing implementation queries). Document the role choice in PLAN. Behavior change is P10+ Pitfall 14.

4. **What is the `last_error` envelope shape? Free string or `{code, message}`?**
   - What we know: D-16 says "single string, cleared atomically. No history, no severity, no ID. HEALTH-05's UX is 'show + clear.'"
   - What's unclear: Wire shape: `"last_error": "Audio device not found"` vs `"last_error": {"message":"Audio device not found"}`.
   - Recommendation: **Plain string.** D-16 explicit on simplicity. Future structured-error work belongs in a future observability milestone.

5. **Should the AppConfig `to_json` ADL hooks live in `driver/src/config_io.cpp` AND `apps/micmap/main.cpp`, OR in a shared driver-only TU that both link?**
   - What we know: Client also needs to serialize AppConfig (when constructing a PUT body). D-01 says nlohmann is in driver TUs only — but client also has nlohmann (it always did, from v1.5).
   - What's unclear: Is the `cmake/AssertNoJsonInCore.cmake` lint scope just `src/` (i.e., shared lib), or does it also exclude `apps/`? The lint description in CONTEXT says "in any TU under `src/audio/`, `src/detection/`, `src/core/`, `src/common/`" — `apps/micmap/` is not in the list, so client TUs may still use nlohmann.
   - Recommendation: **Client keeps using nlohmann (it always did).** The lint targets shared lib only. AppConfig ADL hooks can live in:
     - Option (a) a single `src/core/include/micmap/core/config_json.hpp` header that's NOT included by mic_test → mic_test stays clean → both driver and client get hooks via that header.
     - Option (b) duplicated inline in each binary's TU.
     - Option (c) a separate driver-only TU + client-only TU (each defines hooks; mic_test never includes either).
     - Recommend **(c)** to keep the lint simple (no exception list).

6. **Should `IDriverApi::connect()` return a 3-state result (Connected / Timeout / NotFound) per Pitfall 6, or stay boolean?**
   - What we know: Current `IDriverClient::connect()` returns `bool` — true on success, false on any failure mode (vr_input.cpp:154).
   - What's unclear: HEALTH-01 needs to distinguish ECONNREFUSED (driver down → red) from timeout (driver busy → keep prior state). A boolean can't carry that.
   - Recommendation: **Refactor to 3-state enum** (`ConnectResult { Connected, NotFound, Timeout, OtherError }`) in plan `08-01` along with the rename. The cost is small (one enum + caller update); the alternative (calling `client.error()` from outside the connect method) duplicates the logic.

## Sources

### Primary (HIGH confidence — in-tree, verified 2026-05-05)

- `.planning/phases/08-ipc-contract-reshape/08-CONTEXT.md` — locked decisions D-01..D-30, plan rough structure, UAT regimen [VERIFIED]
- `.planning/phases/08-ipc-contract-reshape/08-DISCUSSION-LOG.md` — rationale for the four headline calls [VERIFIED]
- `.planning/phases/08-ipc-contract-reshape/08-UI-SPEC.md` — ImGui visual contract for HEALTH pane [VERIFIED]
- `.planning/REQUIREMENTS.md` — IPC-01..08, HEALTH-01..07, LIB-04 surface specs [VERIFIED]
- `.planning/ROADMAP.md` §"Phase 8" — six exit criteria, dependency graph [VERIFIED]
- `.planning/research/SUMMARY.md` — recommended stack, deltas, IPC reshape semantics [VERIFIED]
- `.planning/research/PITFALLS.md` — Pitfalls 5, 7, 11, 14, 15, 16; mitigations
- `.planning/codebase/ARCHITECTURE.md` — current two-process layout, IPC contract
- `.planning/codebase/STRUCTURE.md` — directory layout (current branch state, dated 2026-04-23)
- `driver/src/http_server.{hpp,cpp}` — existing HTTP server, ctor signature, `/health` + `/port` + `/status` + `POST /button` routes; `driverDetectionActiveGetter` callback pattern (P7 D-09) [VERIFIED 2026-05-05: `http_server.hpp:64-67` ctor signature, `http_server.cpp:26-31` impl]
- `driver/src/device_provider.{hpp,cpp}` — existing Init/Cleanup ordering; AudioWorker + DetectionRunner construction
- `driver/src/audio_worker.{hpp,cpp}` — `state_->rms_logs_emitted` budget pattern; `state_->sample_rate` atomic; WASAPI device enumeration; RMS computation
- `driver/src/detection_runner.{hpp,cpp}` — atomic-snapshot publish/load pattern (P7 D-15); `Pause`/`Resume`/`NotifyOne` lifecycle
- `driver/src/command_queue.hpp` — bounded SPSC drop-oldest queue (unchanged in P8)
- `driver/src/driver_log.hpp` — SafeDriverLog Rule-3 guard from P6; wrapped by DriverLogSink in P8
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` + `src/steamvr/src/vr_input.cpp` — current `IDriverClient` interface; `DriverClient` impl with `connect()` (`vr_input.cpp:127-155`), `tap()` (`vr_input.cpp:169-199`), `getStatus()` (`vr_input.cpp:201-219`), `isDriverDetectionActive()` (`vr_input.cpp:226+` P7 D-10), error handling at `vr_input.cpp:144` (truthiness only — Pitfall 6 forward-looking) [VERIFIED 2026-05-05]
- `src/common/include/micmap/common/logger.hpp` + `src/common/src/logger.cpp` — `ILogger` interface (`logger.hpp:46-53`); `Logger::setLogger()` global access (`logger.hpp:74-80`); default `ConsoleLogger` (`logger.hpp:58-69`) [VERIFIED 2026-05-05]
- `src/core/include/micmap/core/config_manager.hpp` + `src/core/src/config_manager.cpp` — AppConfig schema; `writeAtomicWindows` ReplaceFileW helper (`config_manager.cpp:322`); `backupAndRotate` corruption rotation (`config_manager.cpp:280`); UTF-8 wstring boundary [VERIFIED 2026-05-05 grep]
- `apps/micmap/main.cpp` — `MicMapApp::initialize()`; `loadDefault()`; `saveDefault()` (TO BE DELETED at P8 cutover); `onTrigger()`; `renderUI()`; existing health indicator pattern
- `cmake/AssertDetectionRunnerNoVrApi.cmake` — sibling lint shape; Wave 0 RED-tolerant idiom; three-file explicit list
- `cmake/AssertNoOpenVRInCore.cmake` + `cmake/lint_no_driver_macro.cmake` + `cmake/lint_no_openvr_in_core.cmake` — P5 lint precedent
- `external/CMakeLists.txt` — cpp-httplib v0.14.3 pin (`external/CMakeLists.txt:62`), nlohmann_json v3.11.2 pin (`external/CMakeLists.txt:9-10`), ImGui v1.90.1 (`external/CMakeLists.txt:81`) [VERIFIED 2026-05-05]
- `external/cpp_httplib/.../httplib.h` — `enum class Error { Success, Unknown, Connection, BindIPAddress, Read, Write, ExceedRedirectCount, Canceled, SSLConnection, SSLLoadingCerts, SSLServerVerification, UnsupportedMultipartBoundaryChars, Compression }` [VERIFIED 2026-05-05]

### Primary (HIGH confidence — official docs)

- https://github.com/yhirose/cpp-httplib (releases page, README) — v0.20.1 tag exists [VERIFIED 2026-05-05 via WebFetch]; current upstream is v0.43.3 (CONTEXT D-05 explicitly chooses v0.20.1 to minimize wire-format drift)
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew — atomic file replacement semantics
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/imminotificationclient — device-removed callback contract

### Secondary (MEDIUM confidence — referenced but not re-verified)

- https://github.com/nlohmann/json/blob/v3.11.2/include/nlohmann/json.hpp — `to_json`/`from_json` ADL pattern; `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` macro

### Tertiary (LOW confidence)

- None — every claim in this research is grounded in either in-tree code, in-tree planning artifacts, or the verified cpp-httplib v0.20.1 tag.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — cpp-httplib v0.20.1 tag confirmed; nlohmann v3.11.2, ImGui v1.90.1, KissFFT, OpenVR all unchanged from prior phases. All in-tree pins re-verified at HEAD `407d8b1`.
- Architecture: HIGH — every named symbol verified against current branch (`hmd-button` HEAD `407d8b1`).
- Pitfalls: HIGH — anchored in v1.5/P5/P6/P7 shipped pitfalls + project PITFALLS.md catalog. Pitfall 6 (ECONNREFUSED) verified by direct read of in-tree `vr_input.cpp:144` confirming forward-looking mitigation.
- Validation Architecture: HIGH — extends P7's CTest pattern verbatim; all gaps are scaffolds, not novel infra.

**Research date:** 2026-05-05 (refresh)
**Original research date:** 2026-05-05 (commit b0e9a3c)
**Valid until:** 2026-06-05 (30 days for stable in-tree references; cpp-httplib upstream moves fast — re-verify before any deps-refresh phase)

## RESEARCH COMPLETE

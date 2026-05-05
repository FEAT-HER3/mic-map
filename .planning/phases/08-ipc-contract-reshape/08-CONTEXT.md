# Phase 8: IPC Contract Reshape - Context

**Gathered:** 2026-05-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Reshape driver↔client IPC from "trigger-over-HTTP" to a settings/health/telemetry contract. New endpoints: `GET /state`, `GET /telemetry/level`, `GET /devices`, `GET /settings`, `PUT /settings`, `POST /state/clear-error`. Driver becomes the sole writer of `%APPDATA%\MicMap\config.json` (P5 D-15/D-16, P6 D-02, P7 D-14 deferral chain ends here — `nlohmann/json` lands in `driver_micmap.dll` this phase). `IDriverClient` renamed to `IDriverApi`. Client UI gains driver-health pane (HEALTH-01..07) and rewires its detection-config controls through `PUT /settings`. LIB-04 logger sink injection lands (existing `ILogger`/`setLogger` DI shape extends with `FileLogSink` + `DriverLogSink`).

**In scope:** cpp-httplib v0.14.3 → v0.20.1 bump (CVE-2025-46728); driver as `config.json` reader at Init (3-attempt SHARING_VIOLATION retry) + sole writer (atomic `ReplaceFileW` reused from v1.5 CFG-04); 6 new HTTP routes with localhost-only binding (extends v1.5 `/health`, `/port` routes); JSON layer in driver TUs only (NOT in `micmap::core_runtime`); structured 400 error envelope `{"field":"...","reason":"..."}` on PUT validation failure; in-memory `std::atomic<std::shared_ptr<const AppConfig>>` snapshot (mechanism inherited from P7 D-15); WASAPI device enumeration via `GET /devices` with 1s cache; client UI driver-health pane (HEALTH-01..07: connection indicator, detection-state pill, last-trigger relative timestamp, last-error display + clear, RMS/dBFS level meter at 5 Hz visible / 0.5 Hz tray, device-disappeared indicator); LIB-04 `FileLogSink` + `DriverLogSink` + `MultiSinkLogger` composition in driver and client; `IDriverClient` → `IDriverApi` rename; client UI controls call `PUT /settings` then optimistically apply to client's in-memory `ConfigManager` so client-side detection (still live until P10) sees the change immediately; settings UI gated on driver-loaded indicator (disabled when red); UAT on Bigscreen Beyond + Win11 Pro.

**Out of scope:** training endpoints `POST /training/*` and `training_data.bin` ownership transfer (P9 TRAIN cluster); `POST /button` deletion + `IDriverClient::tap()` removal — survives in parallel as rollback path (P10 MIG-05); `enable_driver_detection` default flip ON (P10 cutover); FAIL-01..05 graceful failure UX (P10); HEALTH-08 tray-icon state glyphs (P10); INST-09 installer co-versioning (P10); TEST-01..03/05 (P10); log-file rotation 5 MB cap + 5 generations (TEST-03 = P10 — P8 ships basic file write, no rotation); `OnDefaultDeviceChanged` follow-the-default + device pinning (Pitfall 14 — wire enumeration only in P8, behavior change P10+); `DeviceNotificationClient` ComPtr migration (P10+); FFT-on-every-frame perf (CONCERNS.md — backlog); detection-accuracy work in noisy environments (out of v1.6).

</domain>

<decisions>
## Implementation Decisions

### A. JSON dependency in the driver

- **D-01:** `nlohmann/json` is pulled into `driver_micmap.dll` directly (not via `micmap::core_runtime`). Driver TUs (`http_server.cpp`, new `driver/src/config_io.cpp`, new `driver/src/settings_validator.cpp`) include `<nlohmann/json.hpp>`; the shared core lib stays json-free so `mic_test.exe` continues to build without nlohmann symbols. Reason: P8 forces complex AppConfig parsing/validation on the HTTP path; hand-rolled parser bugs in a security-relevant codepath (HTTP input → atomic config swap → `ReplaceFileW`) are worse than the symbol-bloat tradeoff (Pitfall 15). The bloat targets driver only; mic_test stays clean — Pitfall 15's "three-binary cost" reduces to one.
- **D-02:** CMake lint extends to assert `<nlohmann/json.hpp>` does NOT appear in any TU under `src/audio/`, `src/detection/`, `src/core/`, `src/common/` (i.e., not in shared lib). Mirrors `cmake/AssertNoOpenVRInCore.cmake` shape. Sibling file: `cmake/AssertNoJsonInCore.cmake`. CI runs on every build.
- **D-03:** AppConfig serialization is hand-written `to_json` / `from_json` ADL hooks (nlohmann's `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` is acceptable for plain POD subsections; explicit ADL functions for sections that need version-checking or range-clamping). Schema source-of-truth is `src/core/include/micmap/core/config_manager.hpp` AppConfig — the same struct the client already serializes — so wire format is identical on both sides (no parser drift).
- **D-04:** Wire format is identical to the v1.5 client-written `config.json` on disk. No envelope, no version bump. Forward-compat: unknown fields are ignored on read; missing fields fall back to defaults (existing v1.5 client behavior). PUT /settings rejects only structurally-broken JSON (HTTP 400, see D-12).

### B. cpp-httplib bump (CVE-2025-46728)

- **D-05:** Bump v0.14.3 → v0.20.1 ships as the very first plan of P8 (plan `08-00`). Prereq for every other P8 plan; isolated commit; isolated revert. Reason: cpp-httplib lives in BOTH driver `HttpServer` AND client `DriverClient` (post-rename `DriverApi`); wire-format / API regressions could land in either binary. Validating the bump in its own plan means any regression debugging in subsequent plans isn't conflated with "did the bump break X."
- **D-06:** Smoke test for the bump = run the existing v1.5 trigger path UAT (`POST /button` → CommandQueue → `/input/system/click`) + Phase 6/7 flag-OFF regression. No new endpoints exercised yet at 08-00. If smoke is green, IPC plans build on the new version. If smoke fails, revert is a single commit.

### C. Config writer cutover protocol

- **D-07:** **Atomic cutover in P8.** Driver becomes sole `config.json` writer the moment IPC-04 (`PUT /settings`) lands. Client write path to `config.json` is deleted in the same plan that wires `PUT /settings` from the UI. IPC-05 explicitly forbids dual-writer; staged dual-writer mode is two-test-matrix drift waiting to happen. Single-writer rule from P8 onward.
- **D-08:** Client read path stays alive: client reads `config.json` once at startup via existing `ConfigManager::loadDefault()`. This is necessary because the client may start before the driver is loaded (SteamVR not yet running). After boot, client never re-reads `config.json` from disk; live updates flow via `GET /settings` poll OR are pushed by the same PUT /settings call the client itself initiated (client knows what it just sent).
- **D-09:** UI control change semantics — when the user moves a slider or picks a device:
  1. Client calls `PUT /settings` with the full `AppConfig`.
  2. On HTTP 200, client optimistically mutates its own in-memory `ConfigManager` snapshot so client-side detection (live until P10) picks up the new value within one detection cycle (no client-side disk write).
  3. On HTTP 4xx (validation failure), client rolls back the UI control to the prior value and surfaces the structured `{"field":"...","reason":"..."}` error.
  4. On `ECONNREFUSED` (driver down), client refuses the edit. Settings UI is gated on the driver-loaded indicator (HEALTH-01) — controls render disabled with tooltip "Driver not loaded — settings cannot be changed" when red.
- **D-10:** Driver `Init` reads `config.json` once with 3-attempt retry on `ERROR_SHARING_VIOLATION` (50 ms backoff between attempts) per IPC-05. Mitigates the install-time race where the client may have just written the file. After successful read, driver's `std::atomic<std::shared_ptr<const AppConfig>>` snapshot is authoritative for the rest of the session. If all 3 attempts fail, driver loads defaults and logs a warning; PUT /settings still works to overwrite.

### D. Client UI scope (HEALTH-01..07)

- **D-11:** New driver-health pane added to client UI surfacing `GET /state` + `GET /telemetry/level` + `GET /devices` data:
  - Driver-loaded indicator (red on `ECONNREFUSED`, green otherwise) — HEALTH-01
  - SteamVR-running indicator (derived from same `/health` poll) — HEALTH-02
  - Detection-state pill (idle/training/detecting/triggered/cooldown) — HEALTH-03
  - Last-trigger relative timestamp ("3 s ago", "—" if never) — HEALTH-04
  - `last_error` display + one-click "Clear" button calling `POST /state/clear-error` — HEALTH-05
  - RMS/dBFS level meter polling `GET /telemetry/level` at 5 Hz visible / 0.5 Hz minimized to tray — HEALTH-06
  - "Device disappeared" indicator on `audio_device_state ∈ {missing, permission_denied}` with one-click "Re-pick device" — HEALTH-07
- **D-12:** Existing client detection-config controls (sensitivity slider, threshold, cooldown, min-duration, device picker) are NOT replaced — they're rewired to call `PUT /settings` per D-09. The control widgets stay as-is; only their on-change handlers change. Reason: keeps the client UI diff small and avoids re-litigating widget design that's not in P8 scope.
- **D-13:** Device picker source switches from client-side WASAPI enumeration to `GET /devices`. Client trusts the driver's view of WASAPI devices (different processes can see different defaults; the driver's view is the one that matters for detection). Polled on UI open; cached for the session unless `audio_device_state=missing` triggers a re-fetch. v1.5 client-side enumeration code remains for headless `mic_test.exe` use; not deleted.

### E. Validation + error envelope

- **D-14:** PUT /settings validation is **all-or-nothing**. Driver constructs a candidate `AppConfig` from the JSON, runs full validation (range clamps, enum membership, file-path sanity, training-data-not-modified), and either swaps the atomic snapshot AND persists via `ReplaceFileW` (HTTP 200) OR rejects with HTTP 400 + `{"field":"first_failed_field","reason":"human-readable"}` and zero state mutation. No partial apply.
- **D-15:** Validation library is hand-rolled (no JSON Schema lib pulled in). AppConfig is small (≤ 30 fields); per-field validators live in a new `driver/src/settings_validator.cpp` that returns `std::optional<ValidationError>`. First-failed-field reporting (not multi-error aggregation) — keeps response shape simple and matches IPC-04's spec.
- **D-16:** `last_error` race: cleared by `POST /state/clear-error` is monotonic — sets the atomic to null. Concurrent error fires after the clear simply overwrites null with the new error. No queue, no error history. HEALTH-05 surfaces the most recent error only; this is intentional simplicity.

### F. /devices and /telemetry/level cadence

- **D-17:** `GET /devices` enumerates via the driver's existing WASAPI enumerator (already in `driver/src/audio_worker.cpp` from P6). Result is cached for **1 second** to absorb client-poll storms (HEALTH-07's "Re-pick device" can hammer it). Cache invalidated immediately on `IMMNotificationClient` device-add/remove callback. Client polls on UI events (picker open, error-state change) — not on a timer.
- **D-18:** `GET /telemetry/level` returns the driver's most-recent RMS/dBFS reading (already computed by the AudioWorker → DetectionRunner pipeline from P6/P7 — no extra capture work). Reading is the rolling RMS over the last detection window. Designed for 5 Hz visible / 0.5 Hz tray polling per HEALTH-06; client-side timeout 250 ms (per IPC-02 spec). Value source is a `std::atomic<float>` on the AudioWorker (or DetectionRunner) — read-without-lock from the HTTP thread.

### G. LIB-04 logger sinks

- **D-19:** Existing `src/common/logger.hpp` `ILogger`/`setLogger` DI shape is the foundation. P8 adds:
  - `FileLogSink` (in `src/common/src/sinks/file_log_sink.cpp`) — atomic append to a path passed at construction; no rotation in P8 (TEST-03 owns rotation in P10).
  - `DriverLogSink` (in `driver/src/sinks/driver_log_sink.cpp`) — wraps `vr::VRDriverLog()` with the SafeDriverLog Rule-3 guard from P6 (`vr::VRDriverContext()` null-check before call). Lives in `driver/` because it depends on `vr::*`; CANNOT be in `micmap::core_runtime` (LIB-03 lint enforces).
  - `MultiSinkLogger` (in `src/common/src/multi_sink_logger.cpp`) — composes 1+ sinks; thread-safe `log()` fan-out.
- **D-20:** Driver binary wires `MultiSinkLogger{DriverLogSink, FileLogSink("%APPDATA%\\MicMap\\micmap-driver.log")}` at `DeviceProvider::Init` (BEFORE step 4 `httpServer_->Start()` so HTTP server logs go through the file sink too). Client wires `MultiSinkLogger{StdoutLogSink, FileLogSink("%APPDATA%\\MicMap\\micmap.log")}` at `WinMain` startup. Existing `ConsoleLogger` becomes `StdoutLogSink` (rename + adapt to sink interface).
- **D-21:** **Zero `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime`** (LIB-04 explicit). Achieved by composition root: each binary's main/Init constructs the sink list it wants and calls `Logger::setLogger(std::make_shared<MultiSinkLogger>(sinks))`. Shared lib code only ever calls `Logger::info(...)` — sink choice is invisible.

### H. IDriverClient → IDriverApi rename

- **D-22:** Pure rename in a single early P8 plan (likely `08-01` after the cpp-httplib bump). Touches: `src/steamvr/include/micmap/steamvr/vr_input.hpp`, `src/steamvr/src/vr_input.cpp`, `apps/micmap/main.cpp`, all tests. No interface surface change in this plan — that comes when the new endpoints get added (`GET /state`, `PUT /settings`, etc.). Rename is mechanical; test diff is grep-and-replace; one commit, no semantic changes.
- **D-23:** New methods (e.g., `getState()`, `getSettings()`, `putSettings()`, `getDevices()`, `getTelemetryLevel()`, `clearError()`) are added incrementally per their corresponding endpoint plan. `tap()` survives until P10 MIG-05. Wire `getHealth().driver_detection_active` from P7 D-09 untouched.

### I. HTTP route discipline (IPC-08, v1.5 SVR-05)

- **D-24:** All new routes (`GET /state`, `GET /settings`, `GET /devices`, `GET /telemetry/level`, `POST /state/clear-error`) handle their work entirely on the HTTP thread — no `vr::*` calls, no CommandQueue push. Reads are atomic-snapshot loads; writes (`PUT /settings`, `POST /state/clear-error`) mutate atomics directly. v1.5 SVR-05 boundary survives unchanged: the only producer to `CommandQueue` after P8 is the P7 `DetectionRunner` (and the v1.5 `POST /button` rollback path until P10).
- **D-25:** All routes bind to `127.0.0.1` only (IPC-07). Existing `HttpServer` ctor already takes `host` defaulting to `"127.0.0.1"` — no change. Verified by `netstat -an` UAT step.
- **D-26:** Client poll cadence:
  - `GET /health` — 1 Hz (existing v1.5; unchanged)
  - `GET /state` — 2 Hz when window visible, 0.5 Hz when minimized (HEALTH-03/04/05)
  - `GET /telemetry/level` — 5 Hz visible / 0.5 Hz tray (HEALTH-06, IPC-02)
  - `GET /settings` — on UI open + after PUT /settings 200 (no timer poll — settings don't drift on their own)
  - `GET /devices` — on UI open + on device-disappeared error (no timer poll)

### J. UAT regimen on Bigscreen Beyond + Win11 Pro

- **D-27:** Mandatory before phase-complete:
  1. **Settings round-trip.** Move sensitivity slider in client → PUT /settings → driver persists → close + reopen client → slider position reflects driver-persisted value (read from disk via client startup `loadDefault()` after driver wrote on prior session).
  2. **Validation rejection.** Send PUT /settings with out-of-range field (e.g., `sensitivity: -1.0`) via curl. Assert HTTP 400 + structured envelope. Assert `GET /settings` returns unchanged prior value (no partial mutation).
  3. **Driver-down UX.** Stop SteamVR while client is running. Client driver-loaded indicator turns red within 1 health-poll cycle. Settings controls disable. Restart SteamVR. Indicator goes green; settings re-enable.
  4. **`last_error` clear.** Force a driver-side error (e.g., yank the audio device mid-session — FAIL-05's surface). HEALTH-05 displays the error string. Click "Clear" → `POST /state/clear-error` → HTTP 200 → next `/state` poll shows null.
  5. **netstat localhost-only.** `netstat -an | findstr :27015` returns only `127.0.0.1:27015` — never `0.0.0.0`.
  6. **Logger sinks.** Verify `%APPDATA%\MicMap\micmap-driver.log` and `%APPDATA%\MicMap\micmap.log` both gain entries during a session. Verify `vrserver.txt` still receives driver log lines (DriverLogSink active).
  7. **cpp-httplib bump regression.** Pre-bump v1.5 trigger path UAT (`POST /button` → dashboard toggle) still passes after 08-00 lands and after all P8 plans land.
- **D-28:** Stress: 100 PUT /settings calls in a tight loop (curl script) — no driver crash, no leaked file handles via Process Explorer, `config.json` integrity preserved (every read after the storm parses cleanly).

### K. Plan structure (rough — planner refines)

- **D-29:** Approximate wave layout for the planner:
  - **08-00 (Wave 0):** cpp-httplib v0.20.1 bump + smoke test
  - **08-01 (Wave 1):** `IDriverClient` → `IDriverApi` rename + LIB-04 logger sinks (`FileLogSink`, `DriverLogSink`, `MultiSinkLogger`) + composition wiring in driver and client
  - **08-02 (Wave 2):** nlohmann/json into driver TUs + `cmake/AssertNoJsonInCore.cmake` lint + AppConfig `to_json`/`from_json` ADL + driver Init reads `config.json` (3-attempt SHARING_VIOLATION retry)
  - **08-03 (Wave 3):** GET endpoints — `/state`, `/settings`, `/devices`, `/telemetry/level` (read-only, no mutation surface yet)
  - **08-04 (Wave 4):** PUT /settings + atomic apply + `ReplaceFileW` persist + `settings_validator.cpp` + HTTP 400 envelope + `POST /state/clear-error`
  - **08-05 (Wave 5):** Client UI driver-health pane (HEALTH-01..07) + UI control rewire (`PUT /settings` + optimistic in-memory apply + driver-loaded gate)
  - **08-06 (Wave 6):** UAT D-27(1)..(7) + D-28 stress + post-UAT default-OFF preservation + `enable_driver_detection` flag still default OFF (P10 owns the flip)
- **D-30:** P7's `enable_driver_detection` flag stays **default OFF** through P8. P8 does NOT flip it. Reason: P10 cutover is the single point of flag flips; P8 ships the IPC surface but leaves the trigger path on v1.5's `POST /button` until P10. Settings still flow through `PUT /settings` regardless of the detection flag — P8 is endpoint reshape, not trigger reshape.

### Claude's Discretion

- Exact `FileLogSink` flush cadence (per-line / per-N-lines / time-based) — pick whatever doesn't lose data on `vrserver.exe` crash.
- Whether `ConsoleLogger` is renamed to `StdoutLogSink` and wrapped, or kept as-is and a new `StdoutLogSink` adapter is added; pick whichever produces the smallest diff in `apps/micmap/main.cpp`.
- Internal layout of `settings_validator.cpp` — flat free functions vs visitor pattern; either is fine for ≤ 30 fields.
- Cache TTL for `GET /devices` — 1 s is a starting point; tune by feel. Doc the chosen value in PLAN.
- Whether `MultiSinkLogger::log()` fans out under a single mutex or per-sink mutex; standard mutex is fine for the volume.
- Exact poll cadence values within HEALTH-06 (tray polling — 0.5 Hz floor; can drop further if SteamVR isn't running).
- Whether `IDriverApi` rename touches just `IDriverClient` → `IDriverApi` or also renames the file (`driver_client.hpp` → `driver_api.hpp`). Recommend file-rename for symmetry; keeps grep clean.
- Whether `GET /settings` returns the snapshot directly or always re-reads from disk on request. Recommend in-memory snapshot (matches v1.5 SVR-05 discipline — HTTP thread doesn't touch disk).
- Whether validation errors return field paths in JSON-Pointer style (`/audio/deviceId`) or dot-paths (`audio.deviceId`); pick whichever the existing `nlohmann/json::json_pointer` sugar makes natural. Document the choice in PLAN.

### Folded Todos

None — `cross_reference_todos` returned 0 matches for Phase 8.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 8: IPC Contract Reshape" — goal, dependency on Phase 5 (driver-side endpoints buildable before P7 lands), 6 success criteria (PUT /settings round-trip, sole-writer grep, client UI live indicators, netstat localhost-only, SVR-05 boundary, separate driver/client log files via injected sinks), STANDARD research flag.
- `.planning/REQUIREMENTS.md` §"IPC Reshape (IPC)" + §"Driver-Health UI (HEALTH)" + §"Shared Library (LIB)" — 17 requirements: IPC-01..08, HEALTH-01..07, LIB-04. Endpoint payloads spec'd; cadences spec'd; binding rules spec'd.
- `.planning/PROJECT.md` §"Current Milestone" + §"Constraints" — locked stack (cpp-httplib + nlohmann/json + ImGui + D3D11), localhost HTTP IPC retained (not replacing IPC, just reshaping), driver runs without admin.
- `.planning/STATE.md` §"Blockers/Concerns" — cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728) deferred to standalone deps-refresh plan inside P8 prereq.

### Pitfall mitigations Phase 8 owns
- `.planning/research/PITFALLS.md` §"Pitfall 5: File-watching `config.json` from the driver" — REJECTED. PUT /settings is the only path. P8 D-07/D-09/D-10 enforce single-writer rule.
- `.planning/research/PITFALLS.md` §"Pitfall 7: HTTP server binding" — `127.0.0.1` only, never `0.0.0.0`. P8 D-25 + D-27(5) verify.
- `.planning/research/PITFALLS.md` §"Pitfall 14: OnDefaultDeviceChanged" — P8 wires GET /devices enumeration only; behavior change (follow-the-default + device pinning) deferred P10+.
- `.planning/research/PITFALLS.md` §"Pitfall 15: Symbol bloat from `nlohmann/json` in three binaries" — **D-01/D-02 LIFT THE DEFERRAL.** json into driver TUs (NOT shared lib); `cmake/AssertNoJsonInCore.cmake` lint enforces. mic_test stays clean.
- `.planning/research/PITFALLS.md` §"Pitfall 11: v1.5 priors" — atomic config write (`ReplaceFileW`) helper from v1.5 CFG-04. P8 D-14 + D-10 reuse.

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 4: IPC Contract Reshape" (research-numbered = roadmap Phase 8) — research-derived rationale for the endpoint shape and driver-as-sole-writer.
- `.planning/research/ARCHITECTURE.md` — driver vs client thread model post-migration; HTTP thread → atomic snapshot mutation discipline.
- `.planning/codebase/ARCHITECTURE.md` §"IDriverClient" + §"Configuration Persistence" — current v1.5 IPC shape that P8 reshapes.
- `.planning/codebase/STRUCTURE.md` — `driver/src/`, `src/steamvr/`, `src/common/`, `apps/micmap/` interface layouts.
- `.planning/codebase/STACK.md` — locked stack; P8 adds nothing new besides the cpp-httplib version bump (already in stack).
- `.planning/codebase/CONCERNS.md` — `DeviceNotificationClient` ComPtr migration flagged; explicitly **deferred to P10**.

### Phase boundary inheritance
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` — D-15/D-16 deferred json + LIB-04 to P8. **P8 lifts both.**
- `.planning/phases/06-driver-side-audio-capture-spike/06-CONTEXT.md` — D-01 VRSettings flag pattern + AudioWorker WASAPI enumeration (P8 D-17 reuses for GET /devices), SafeDriverLog Rule-3 guard (P8 D-19 wraps in DriverLogSink).
- `.planning/phases/07-driver-side-detection-thread/07-CONTEXT.md` — D-09 `/health.driver_detection_active` field (untouched in P8), D-15 atomic-snapshot publish/load mechanism for DetectionConfig (**P8 generalizes to AppConfig snapshot**), D-27 flag-OFF default discipline (P8 preserves; P10 flips).

### v1.5 invariants carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. **P8 adds new HTTP routes but none push to CommandQueue or call `vr::*`.** Boundary unchanged.
- `.planning/milestones/v1.5-ROADMAP.md` CFG-04 — atomic `ReplaceFileW` save with corruption-backup retention. **P8 PUT /settings persists via the same helper.**
- `.planning/milestones/v1.5-ROADMAP.md` CFG-01..05 — defensive nlohmann/json read-back (UTF-8 wstring boundary, clamp/pow2-snap, 5-file corruption backup retention). **P8 driver Init reuses this pattern verbatim.**

### In-tree code touched by P8
- `driver/src/http_server.{hpp,cpp}` — extend with 6 new routes; ctor gains additional callbacks/refs for snapshot reads (Claude's discretion on wiring shape — match the P7 D-09 `driverDetectionActiveGetter` pattern).
- `driver/src/device_provider.{hpp,cpp}` — Init gains `config.json` read step + AppConfig snapshot member + logger sink wiring; Cleanup unchanged shape (snapshot member is just a `std::atomic<std::shared_ptr<>>`, no thread to join).
- `driver/src/config_io.cpp` (NEW) — driver-side `loadConfigJson(path)` + `saveConfigJson(path, config)` using nlohmann + `ReplaceFileW`.
- `driver/src/settings_validator.cpp` (NEW) — per-field validators returning `std::optional<ValidationError>`.
- `driver/src/sinks/driver_log_sink.cpp` (NEW) — wraps `vr::VRDriverLog()` with SafeDriverLog Rule-3 guard.
- `src/common/src/sinks/file_log_sink.cpp` (NEW) — atomic append to a path.
- `src/common/src/multi_sink_logger.cpp` (NEW) — fan-out logger composing 1+ sinks.
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` — `IDriverClient` → `IDriverApi` rename + new method declarations (`getState`, `getSettings`, `putSettings`, `getDevices`, `getTelemetryLevel`, `clearError`).
- `src/steamvr/src/vr_input.cpp` — implementation of new methods using cpp-httplib client.
- `apps/micmap/main.cpp` — UI control on-change handlers rewired to call `PUT /settings`; new health pane wiring; existing `loadDefault()` startup read kept; existing `saveDefault()` write path **deleted**.
- `apps/micmap/src/` (UI panels) — new `DriverHealthPane` (or extension of an existing panel) for HEALTH-01..07; settings controls gated on driver-loaded indicator.
- `cmake/AssertNoJsonInCore.cmake` (NEW) — lint asserting no nlohmann/json in `src/audio/`, `src/detection/`, `src/core/`, `src/common/`. Sibling to P5's `AssertNoOpenVRInCore.cmake`.
- `vendor/cpp-httplib/` (or wherever it's vendored) — bump v0.14.3 → v0.20.1.
- `tests/driver/` — new headless tests for IPC-04 round-trip, IPC-05 sole-writer assertion (grep test), validation rejection, ReplaceFileW atomicity under PUT-storm.

### Reusable assets (already in the tree)
- `src/common/include/micmap/common/logger.hpp` — `ILogger`/`Logger::setLogger()` DI shape. **P8 D-19 extends with new sinks; no change to the public API.**
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig` schema + existing `nlohmann::json` adapters in `src/core/src/config_manager.cpp` (v1.5 client-side). **P8 D-03 reuses the same struct + ADL hooks; lifts them into a driver-shareable form so wire format is identical.**
- `driver/src/audio_worker.cpp` (P6) — `IMMNotificationClient` callback (P6 D-15/D-16 alive-flag) + WASAPI device enumeration. **P8 D-17 hooks GET /devices into the existing enumerator.**
- `driver/src/command_queue.hpp` — unchanged in P8. HTTP routes don't push.
- v1.5 `ReplaceFileW` helper — wherever the v1.5 client-side `saveDefault()` lives. **P8 D-14 reuses for driver-side persistence.**

### Sister-project reference
- None applicable. Sister project's HMD button stub work is orthogonal to IPC reshape.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/common/include/micmap/common/logger.hpp:46-53` — `ILogger` interface with `log(level, msg)`, `setMinLevel`, `getMinLevel`. **P8 sinks (`FileLogSink`, `DriverLogSink`, `StdoutLogSink`) implement this. `MultiSinkLogger` also implements it and fans out.** Public API unchanged.
- `src/common/include/micmap/common/logger.hpp:74-120` — `Logger::setLogger(std::shared_ptr<ILogger>)` global access point. **Composition root in driver/client calls this with a `MultiSinkLogger` instance; no shared-lib code changes.**
- `src/core/src/config_manager.cpp` (v1.5) — existing nlohmann/json `AppConfig` adapters + `ReplaceFileW` atomic save + UTF-8 wstring boundary handling + corruption backup with 5-file retention. **P8 D-10/D-14 lift these into a driver-callable form; client code path stays identical for the read-only side.**
- `driver/src/http_server.{hpp,cpp}` — existing v1.5 `HttpServer` with cpp-httplib. **P8 extends `SetupRoutes()` with 6 new handlers. Ctor signature evolves (already evolved in P7 D-09 with `driverDetectionActiveGetter`).**
- `driver/src/audio_worker.cpp:241-263` (post-P7) — audio callback already feeds DetectionRunner ring + RMS to `std::atomic<float>` (Claude's discretion on exact placement). **P8 D-18 GET /telemetry/level reads that atomic.**
- `driver/src/audio_worker.cpp` — existing WASAPI enumerator (used by AudioWorker to pick the configured device). **P8 D-17 GET /devices reuses the enumeration loop, caches result for 1 s.**
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` — `IDriverClient` interface. **P8 D-22 renames to `IDriverApi`; D-23 adds new methods.**

### Established Patterns
- **Atomic config write via `ReplaceFileW`** — v1.5 CFG-04. Reused for driver-side persistence (D-14).
- **Defensive nlohmann/json read-back with corruption backup** — v1.5 CFG-01..05. Reused for driver Init (D-10).
- **`std::atomic<std::shared_ptr<const T>>` snapshot for live config** — P7 D-15 introduced for `DetectionConfig`. **P8 generalizes the pattern to full `AppConfig`.** Single-mutator (HTTP thread) / multi-reader (HTTP thread for GET, detection thread for live use, audio thread for device-config reads).
- **Composition-root logger setup** — existing `Logger::setLogger()` pattern. P8 wires `MultiSinkLogger` at each binary's startup (DriverProvider::Init for driver; WinMain for client). LIB-04 explicit: no `#ifdef MICMAP_DRIVER_BUILD` inside `micmap_core_runtime`.
- **CMake lint as structural guardrail** — P5 `AssertNoOpenVRInCore.cmake`, P6 `AssertAudioWorkerNoVrApi.cmake`, P7 `AssertDetectionRunnerNoVrApi.cmake`. **P8 adds `AssertNoJsonInCore.cmake`.** CI runs on every build.
- **HTTP route handler discipline (SVR-05)** — v1.5 invariant. HTTP thread never touches `vr::*`, never blocks on disk on the read path. P8 D-24 preserves: GET routes return atomic-snapshot loads; PUT writes mutate atomic + persist via `ReplaceFileW` (the only blocking call; documented as acceptable since PUT is rare).
- **Driver-only files under `driver/src/`** — P5/P6/P7 precedent. P8 adds `config_io.cpp`, `settings_validator.cpp`, `sinks/driver_log_sink.cpp` under `driver/src/`. nlohmann/json appears only in driver TUs — never in shared lib.

### Integration Points
- `driver/src/device_provider.hpp` — add `std::atomic<std::shared_ptr<const AppConfig>> config_` member + `loadConfigOnInit()` private helper + `applyValidatedConfig(AppConfig)` mutator called by PUT handler.
- `driver/src/http_server.hpp` — ctor gains additional getter/setter callbacks: `std::function<std::shared_ptr<const AppConfig>()> configSnapshotGetter`, `std::function<HttpResult(const AppConfig&)> configMutator`, `std::function<DriverState()> stateGetter`, `std::function<void()> errorClearer`, `std::function<float()> rmsGetter`, `std::function<std::vector<DeviceInfo>()> deviceLister`. Match P7 D-09's getter-callback pattern; one callback per route.
- `apps/micmap/main.cpp` — startup-time `Logger::setLogger(MultiSinkLogger{...})`; UI control on-change handlers call `IDriverApi::putSettings(...)`; new `DriverHealthPane` polls `/state`, `/telemetry/level` at HEALTH-06 cadence.
- `cmake/CMakeLists.txt` (root) — include `AssertNoJsonInCore.cmake`; expose `nlohmann_json::nlohmann_json` as a PRIVATE driver dep (not transitive into core_runtime).

</code_context>

<specifics>
## Specific Ideas

- **"Driver is the sole writer."** This is the single load-bearing sentence for P8. Every other decision flows from it. No file-watching (Pitfall 5). No staged dual-write. Atomic cutover in the same plan that wires PUT /settings.
- **Lift the json deferral, but only into the driver.** Pitfall 15's three-binary cost reduces to one when nlohmann lives in driver TUs only. mic_test stays clean. CMake lint enforces; CI catches drift.
- **Optimistic in-memory client apply.** PUT /settings 200 → client mutates its own ConfigManager snapshot in the same call site so client-side detection (live until P10) sees the change immediately. On 4xx, rollback. On ECONNREFUSED, refuse. UX is atomic; internal is dual-update with disk owned by the driver.
- **Settings UI gated on driver-loaded indicator.** When the driver is red (ECONNREFUSED), settings controls are disabled with a clear tooltip. Not a "queue and replay" model — too easy to get wrong; not a worthwhile P8 affordance.
- **Validation is all-or-nothing, single-field reporting.** First failed field → HTTP 400 + `{"field":"...","reason":"..."}`. Snapshot unchanged on rejection. Multi-error aggregation explicitly NOT in P8 — adds shape complexity without user value.
- **`last_error` is monotonic and unstructured.** A single string, cleared atomically. No history, no severity, no ID. HEALTH-05's UX is "show + clear." Anything richer belongs in a future observability milestone.
- **cpp-httplib bump goes first, isolated.** It's a deferred-twice CVE patch + library API drift. One plan, one commit, one revert path. Don't conflate with new-endpoint debugging.
- **`enable_driver_detection` stays default OFF through P8.** P8 ships the IPC reshape; P10 owns the cutover flag flip. Settings flow through PUT /settings regardless of which side runs detection.

</specifics>

<deferred>
## Deferred Ideas

- **`POST /button` deletion + `IDriverClient::tap()` removal** — **P10 (MIG-05)**. Survives in parallel as v1.5 rollback path through P8.
- **`enable_driver_detection` default flip ON** — **P10 cutover**.
- **Tray-icon state glyphs (HEALTH-08), FAIL-01..05 graceful failure UX, INST-09 installer co-versioning, TEST-01..03/05** — **P10**.
- **Log-file rotation (5 MB cap, 5 retained generations)** — **P10 (TEST-03)**. P8 ships basic file-append only.
- **Training endpoints (`POST /training/start`, `/progress`, `/finalize`, `/cancel`, `/recompute`) + `training_data.bin` ownership transfer** — **P9 (TRAIN cluster)**.
- **`OnDefaultDeviceChanged` follow-the-default behavior + device pinning via config** — Pitfall 14 mitigation. P8 wires GET /devices enumeration only; behavior change deferred **P10+**.
- **`DeviceNotificationClient` ComPtr migration** — **P10+**. CONCERNS.md flagged manual `InterlockedIncrement`/`Decrement`; alive-flag pattern (P6 D-15/D-16) carries through P8.
- **FFT-on-every-frame perf cost** — CONCERNS.md Performance Bottleneck #2. **NOT addressed in P8.** Targeted perf phase or backlog.
- **Multi-error aggregation on PUT /settings validation** — single-field reporting is enough for v1.6 UX. Defer to a future observability/UX milestone if real users hit it.
- **Structured `last_error`** (severity, code, history) — same. Single-string + clear is the v1.6 shape.
- **`/debug/snapshot` driver endpoint** (TEST-D2) — already deferred per PROJECT.md.
- **Detection-accuracy work in noisy environments (DET-01/02)** — out of v1.6 scope per PROJECT.md.
- **Replacing localhost HTTP with named pipes / shared memory** (IPC-AF-02 anti-feature) — out of scope; trigger goes in-process so latency benefit disappears.

</deferred>

---

*Phase: 08-ipc-contract-reshape*
*Context gathered: 2026-05-05*

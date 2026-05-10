# Phase 10: Cutover & Cleanup - Context

**Gathered:** 2026-05-10
**Status:** Ready for planning

<domain>
## Phase Boundary

Atomic cutover from dual-runtime (driver-side detection behind flag + v1.5 client-side detection alive) to single driver-resident runtime. Flip `enable_driver_audio` and `enable_driver_detection` defaults to `true` in `driver/resources/settings/default.vrsettings`. Delete `POST /button` route, `IDriverApi::tap()` method, and the entire client-side WASAPI/FFT/state-machine body in `apps/micmap/main.cpp` (`audioCapture`, `detector`, `stateMachine` instances + their plumbing) — ~500 LoC reduction; KissFFT no longer linked into client EXE. Land FAIL cluster (graceful UX for the 5 failure modes), tray-icon state glyphs (HEALTH-08), `--debug-trigger` CLI flag (TEST-02) + `POST /debug/trigger` debug-build-gated endpoint, log rotation in `FileLogSink` (TEST-03), and INST-09 installer co-versioning bake.

**In scope:** flag flip default ON in same atomic plan that deletes server `/button` + client tap call sites + client-side audio/detection body (single-plan cutover protocol — mirrors P8 D-07 / P9 D-23); three new CMake lints `cmake/AssertNoClientDetection.cmake` + `cmake/AssertNoButtonRoute.cmake` + `cmake/AssertCoVersioning.cmake` (RED-tolerant scaffolds Wave 0; go-live in cutover wave); HEALTH-08 tray-icon state glyphs (3 distinct `.ico` resources — armed-green / triggered / error-red — swapped via `Shell_NotifyIconW(NIM_MODIFY, NIF_ICON)` driven by existing `/health` + `/state` poll cadence; pulse = brief 300ms triggered-icon flash auto-reverting via cooldown); FAIL-01..05 inline-pill UX in existing driver-health pane (priority-stacked, one-click actions, mic-permission deep-link via `ShellExecuteW("ms-settings:privacy-microphone")`); FAIL-04 named-mutex single-instance check at `WinMain` entry with `FindWindow + SetForegroundWindow` foregrounding existing instance; `--debug-trigger` CLI flag = `WinMain` short-circuit (mirrors mic_test pattern from P9-04 `tryRunReplayCli`) calling new `IDriverApi::debugTrigger()` against new `POST /debug/trigger` endpoint gated by `MICMAP_DEBUG_BUILD` define (registered only in Debug builds); log rotation in shared-lib `FileLogSink` — synchronous size-check on each write, 5MB cap, 5 retained generations (`micmap-driver.log.{1..5}`), `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` for atomicity (applies to both `micmap-driver.log` and `micmap.log`); INST-09 single version source-of-truth in `cmake/version.cmake` exposing `MICMAP_VERSION` semver, embedded in both binaries via `VS_VERSION_INFO` resource AND compile-time `#define MICMAP_VERSION_STRING`, generated `installer/version.iss` consumed by Inno Setup `#include`; `GET /health` JSON gains `driver_version` field (mirrors P7 D-09 / P9 D-07 getter-callback pattern); client startup compares `health.driver_version` vs `MICMAP_VERSION_STRING` after first successful poll, mismatch logs WARNING + surfaces inline pill in driver-health pane (warn-only, never blocks detection); `hmd_button_test.exe` preserved (TEST-05 — orthogonal to `--debug-trigger`; tests OpenVR input layer end-to-end while `--debug-trigger` tests the CommandQueue/IPC path); UAT regimen on Bigscreen Beyond + Win11 Pro covering all 6 success criteria from ROADMAP.md.

**Out of scope:** docs (DOC-01/DOC-02 — Phase 11); `OnDefaultDeviceChanged` follow-the-default + device pinning (Pitfall 14 — deferred indefinitely; FAIL-05 surfaces device-removed via existing `IMMNotificationClient` rebind callback from P6/P7, no behavior change); `DeviceNotificationClient` ComPtr migration (CONCERNS.md — backlog); FFT-on-every-frame perf cost (CONCERNS.md Performance Bottleneck #2 — backlog or targeted perf phase); detection-accuracy work in noisy environments (DET-01/02 — out of v1.6); multi-error aggregation in FAIL pills (single topmost-priority pill is the v1.6 shape); structured `last_error` (severity / code / history — single string + clear is the v1.6 shape per P8 D-16); blocking version-mismatch dialog (warn-only — hard-block bricks users mid-upgrade race); installer launch-checkbox / silent-install docs / non-default-Steam-path support (DIST-01/02/03 — backlog); v1.5 dashboard-overlay stubs in `dashboard_manager.cpp` retirement (UX-02 — out of v1.6); HEALTH-D1 per-component health badges; trigger-history sparkline (HEALTH-D2); A/B threshold preview (TRAIN-D2); `/debug/snapshot` endpoint (TEST-D2); cpp-httplib v0.20.1 → next bump (no new CVEs flagged).

</domain>

<decisions>
## Implementation Decisions

### A. Cutover sequencing — single atomic plan

- **D-01:** **Atomic single-plan cutover.** One late-phase plan deletes (a) `POST /button` route from `driver/src/http_server.cpp`, (b) `IDriverApi::tap()` declaration from `src/steamvr/include/micmap/steamvr/driver_api.hpp` + impl from `src/steamvr/src/driver_api.cpp`, (c) client-side audio/FFT/state-machine body from `apps/micmap/main.cpp` (the `audioCapture` / `detector` / `stateMachine` instances + their plumbing including the audio callback + the local FFT path + the local trigger callback wiring), (d) all `detector->loadTrainingData(...)` call sites at `apps/micmap/main.cpp:326`, `:647`, `:970`, AND (e) flips both `enable_driver_audio=true` + `enable_driver_detection=true` in `driver/resources/settings/default.vrsettings`. All five in one commit. Three new lints (`AssertNoClientDetection`, `AssertNoButtonRoute`, `AssertCoVersioning`) flip from skip-on-not-found to enforcing in the same plan. Reason: dual-runtime mode is two-test-matrix drift waiting to happen — same logic that drove P8 D-07 single-writer cutover and P9 D-23 single-trainer cutover. Staged cutover would leave the codebase in an unshippable interim state for the duration of intermediate plans.
- **D-02:** **No rollback path inside v1.6 default settings.** Once the flag flip lands, the v1.5 `POST /button` rollback path is gone. If a rig fails post-cutover, rollback is a binary install — not a runtime flag. The `enable_driver_*` keys persist in `default.vrsettings` for emergency override (a user can edit them to `false` to disable detection/audio entirely), but with the client-side body deleted there is no longer a parallel runtime to fall back to. Document this in the cutover plan summary and the `10-UAT.md` exit checklist.
- **D-03:** **Client EXE binary-size regression check** is part of cutover-plan acceptance. Before the cutover plan: capture `micmap.exe` size from the prior commit. After: assert size dropped noticeably (KissFFT + WASAPI capture no longer linked); document the delta in the plan summary. Soft check — the lints are the hard structural gate.

### B. Tray-icon state glyphs (HEALTH-08)

- **D-04:** **Three distinct `.ico` resources**, not overlay compositing. Files: `apps/micmap/resources/tray_armed.ico` (green), `apps/micmap/resources/tray_triggered.ico` (pulse-ish glyph), `apps/micmap/resources/tray_error.ico` (red). All loaded once at `WinMain` startup via `LoadImageW(LR_LOADFROMFILE | LR_DEFAULTSIZE)`. State swap = `Shell_NotifyIconW(NIM_MODIFY, &nid)` with `nid.hIcon` set to the desired handle and `nid.uFlags = NIF_ICON`. Reason: Windows Shell tray API has no clean overlay primitive; per-icon-per-state is the simplest correct approach and reuses the existing tray-icon lifecycle (P3 / P9-04 `first_launch_balloon` already established the `Shell_NotifyIconW` pattern at `apps/micmap/main.cpp:264`).
- **D-05:** **State derivation rules** (no new poll — reuse the existing 1Hz `/health` + 2Hz `/state` poll wiring from P8 D-26):
  - **error (red)** — `/health` returns ECONNREFUSED (driver down — FAIL-02 surface); OR `/state.last_error` is non-null AND non-cleared; OR `/state.audio_device_state ∈ {missing, permission_denied}` (FAIL-01 / FAIL-05 surfaces).
  - **triggered (pulse)** — `/state.detection_state == "triggered"` observed on a poll tick. Sticks for 300ms (one tick at 2Hz), then auto-reverts to **armed** even if subsequent polls still report `triggered` (state machine cooldown is short enough that the next poll typically shows `cooldown` or `idle` anyway). Reason: a pulse is a brief flash, not a held-color — held-red on triggered would conflate with the error state. Kept simple by a tiny `lastTriggeredAt` timestamp on the client; the tray-update tick checks `now - lastTriggeredAt < 300ms`.
  - **armed (green)** — driver loaded (`/health` 200 OK), no error, not currently in the 300ms triggered window. Default state when the system is healthy.
- **D-06:** **Driver state observability** lives entirely on the existing `/state` JSON envelope from P8 D-11 (`detection_state`, `last_error`, `audio_device_state`). No new endpoint, no new field. Tray glyph is a pure rendering of existing state.

### C. FAIL UX surface (FAIL-01..05) — inline pills in driver-health pane

- **D-07:** **Pills in the existing driver-health pane** (P8 D-11 surface), not modals or balloons. Reason: the pane is already the canonical "system status at a glance" surface; modals interrupt and balloons get suppressed by Focus Assist. The pane is visible whenever the client window is open; tray-icon state (D-04..D-06) is the at-a-glance surface for users with the window minimized.
- **D-08:** **Priority stacking — show topmost only.** Conflict order (highest-priority first):
  1. **FAIL-02 driver-not-loaded** (`/health` ECONNREFUSED) — pill: "Driver not installed — run installer or enable in SteamVR" + button "Open SteamVR" (`ShellExecuteW("steam://rungameid/250820")` to launch SteamVR if installed).
  2. **FAIL-03 SteamVR-not-running** (driver loaded would mean SteamVR up; this is also signalled by `/health` ECONNREFUSED but with a longer cumulative duration heuristic — Claude's discretion in PLAN; the simplest policy is "ECONNREFUSED for >5s with SteamVR process not running per `tasklist`" → FAIL-03; else → FAIL-02). Pill: "SteamVR not running — start SteamVR to enable detection". Polling stays at 1Hz (existing P8 D-26 cadence — no retry storm).
  3. **FAIL-01 mic-permission-blocked** (`/state.audio_device_state == "permission_denied"`) — pill: "Mic access blocked" + button "Open Windows mic settings" (`ShellExecuteW("ms-settings:privacy-microphone")`).
  4. **FAIL-05 device-removed** (`/state.audio_device_state == "missing"`) — pill: "Microphone disconnected — reconnect to resume detection" + auto-clear when device reappears (driver-side `IMMNotificationClient` rebind from P6/P7 already handles the recovery; pane just reflects state). HEALTH-07 "Re-pick device" button stays in the pane regardless (P8 surface).
  5. **FAIL-04 double-instance** — handled at `WinMain` entry, never reaches the pane. See D-09.
- **D-09:** **FAIL-04 mechanism**: at `WinMain` very early (before window creation), `CreateMutexW(nullptr, TRUE, L"Local\\MicMapClient_SingleInstance")` + `GetLastError() == ERROR_ALREADY_EXISTS` → call `FindWindowW(L"MicMapMainWindow", nullptr)` (the registered window class), then `ShowWindow(hwnd, SW_RESTORE) + SetForegroundWindow(hwnd)`, then `ExitProcess(0)`. Mutex held for the lifetime of the running instance. No UI thread for the second instance — exits silently per FAIL-04 spec.
- **D-10:** **`last_error` clear** uses existing P8 `POST /state/clear-error`. Pills surface a "Dismiss" button on FAIL-01 / FAIL-05 that calls clear-error; FAIL-02 / FAIL-03 cannot be dismissed (the underlying condition would re-fire on the next poll). Document this UX nuance in the FAIL plan summary.

### D. `--debug-trigger` (TEST-02) + `hmd_button_test.exe` (TEST-05)

- **D-11:** **`POST /debug/trigger` endpoint registered only in debug builds**, gated by a new `MICMAP_DEBUG_BUILD` CMake-driven preprocessor define (set to `1` in Debug, `0` in Release; defaults to `0` if unset for safety). Endpoint pushes a single `TapCommand{}` to the existing `CommandQueue` from the HTTP thread — no audio path, no detection path, exercises the SVR-05 boundary directly. In Release builds the route is not registered; `--debug-trigger` would receive HTTP 404. Document the build-mode dependency in the plan and in user-facing dev docs.
- **D-12:** **`--debug-trigger` CLI implementation** = `WinMain` short-circuit (mirrors P9-04 `tryRunReplayCli` pattern from `apps/mic_test/main.cpp`). Parse `argv` for `--debug-trigger` BEFORE creating window or initializing UI; on match, instantiate a minimal `IDriverApi` client, call new `IDriverApi::debugTrigger()` (cpp-httplib `Post("/debug/trigger", "", "application/json")`), print result to stdout, `ExitProcess(0)`. Exit codes: `0` = HTTP 200, `1` = HTTP non-200 (with stderr message), `2` = ECONNREFUSED. Like mic_test, `micmap.exe` stays a Win32 GUI binary — the short-circuit returns before the GUI initializes. New `IDriverApi::debugTrigger()` method declared and implemented in debug builds only (`#if MICMAP_DEBUG_BUILD`); in Release builds the method is not present and the `--debug-trigger` short-circuit is `#if`'d out so the flag is a no-op.
- **D-13:** **`hmd_button_test.exe` is preserved unchanged.** TEST-05 retains it as a developer tool. The STATE.md "open question about overlap with --debug-trigger" resolves to **no overlap**: `hmd_button_test.exe` exercises the OpenVR input layer end-to-end (creates its own `vr::IVRInput` context, dispatches button events directly into SteamVR) without going through the driver IPC; `--debug-trigger` exercises the IPC + CommandQueue + driver-side trigger pipeline. They test orthogonal layers — keep both. Document in PLAN and in user-facing dev docs that `hmd_button_test` is for OpenVR-input regression and `--debug-trigger` is for driver-IPC regression.

### E. Log rotation (TEST-03)

- **D-14:** **Synchronous size-check on each `FileLogSink::write()` call.** When current file size ≥ 5MB after the write, rotate: `MoveFileExW(L"micmap-driver.log.4", L"micmap-driver.log.5", MOVEFILE_REPLACE_EXISTING)`, `MoveFileExW(L"micmap-driver.log.3", L"micmap-driver.log.4", ...)`, ... `MoveFileExW(L"micmap-driver.log", L"micmap-driver.log.1", ...)`, then continue logging to a fresh `micmap-driver.log`. Five generations retained, `.5` is dropped on rotation. Reason: synchronous keeps `FileLogSink` stateless — no rotation thread, no synchronization with concurrent writers (logger-level mutex from P8 `MultiSinkLogger` already serializes writes). 5MB is small; rotation is rare (<1/day for typical use); the brief stall on rotation is acceptable. Atomic `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` means partial-write is impossible.
- **D-15:** **Same rotation policy applies to BOTH** `%APPDATA%\MicMap\micmap-driver.log` AND `%APPDATA%\MicMap\micmap.log`. The `FileLogSink` constructor takes the path; rotation logic is path-agnostic. P8 `FileLogSink` is the implementation site (extend the existing class — no new file).
- **D-16:** **Size-check cadence**: check on every `write()`, not on a timer. Reads `GetFileAttributesEx(GetFileAttributesExW)` for the current size — cheap on Windows (no open/close cycle). If the cumulative-size-since-last-check optimization becomes warranted, add a counter inside `FileLogSink` — but ship the simpler version first.
- **D-17:** **Failure modes**: if `MoveFileExW` returns FALSE (file locked / permission denied), log a single warning to the SafeDriverLog / Stdout sink (NOT to the file sink — would recurse) and continue logging to the now-oversize file. The bound is soft; we'd rather lose rotation than lose log lines.

### F. INST-09 installer co-versioning

- **D-18:** **Single source-of-truth: `cmake/version.cmake`** exposing `set(MICMAP_VERSION "X.Y.Z")` (semver). All version consumers read from here:
  - `driver_micmap.dll` and `micmap.exe` embed via `VS_VERSION_INFO` resource (CMake `target_sources(... PRIVATE ...rc)` with `configure_file()` substituting `MICMAP_VERSION`).
  - Both binaries also gain a compile-time `#define MICMAP_VERSION_STRING "X.Y.Z"` via `target_compile_definitions(... PRIVATE MICMAP_VERSION_STRING="${MICMAP_VERSION}")`.
  - `installer/MicMap.iss` consumes a generated `installer/version.iss` via Inno Setup `#include "version.iss"` directive. `version.iss` is generated by `configure_file()` from `installer/version.iss.in` at CMake configure time. Inno Setup `[Setup]` section reads `AppVersion={#MICMAP_VERSION}` and the output filename is `MicMap-Setup-v{#MICMAP_VERSION}.exe`.
- **D-19:** **`GET /health` gains `driver_version` field** (string, semver). Pattern mirrors P7 D-09 `driver_detection_active` and P9 D-07 `driver_training_active` — getter-callback wired into `HttpServer` ctor; getter returns the driver's compile-time `MICMAP_VERSION_STRING`. No new endpoint.
- **D-20:** **Client startup version-mismatch check**: after first successful `/health` poll, compare `health.driver_version` vs client's `MICMAP_VERSION_STRING`. On mismatch: (a) `Logger::warning("driver version X.Y.Z does not match client X.Y.Z'")`, (b) surface inline pill in driver-health pane: "Version mismatch — reinstall recommended" (low priority — below all FAIL pills; coexists with armed-tray-icon since detection still works), (c) does NOT block detection or any feature. Hard-block was rejected (would brick users during in-progress upgrade where one binary updates seconds before the other). Warning + pill is the v1.6 shape.
- **D-21:** **Lint `cmake/AssertCoVersioning.cmake`** (RED-tolerant Wave 0; enforcing in cutover wave). Asserts: (a) `cmake/version.cmake` exists and defines `MICMAP_VERSION`, (b) `installer/version.iss.in` exists, (c) at configure time, the resolved `MICMAP_VERSION` value is identical across the driver target, the client target, and the generated `installer/version.iss`. Build fails if any of these drift.
- **D-22:** **Installer atomicity**: Inno Setup's `[Files]` section is atomic by default (all-or-nothing). The single installer places `driver_micmap.dll` + `micmap.exe` + `app.vrmanifest` + shared-lib artifacts (if any — `micmap_core_runtime` is INTERFACE so there are none beyond the static-lib outputs already linked into the binaries) + 3 new `.ico` tray glyphs in lock-step. Upgrade installs the matched set; uninstall cleans the matched set. Verify in UAT on a clean Win11 VM.

### G. Lint discipline

- **D-23:** **Three new sibling lints**, all following the P5/P6/P7/P8/P9 pattern (skip-on-not-found in Wave 0; flip to enforcing in cutover wave):
  - `cmake/AssertNoClientDetection.cmake` — `apps/micmap/` (excluding `apps/mic_test/`) cannot reference `IAudioCapture::start|setAudioCallback`, `INoiseDetector::analyze`, `IStateMachine::update`, `detector->loadTrainingData`. Same regex narrowing strategy as P9-03 `AssertNoClientTraining` (qualifier-prefixed regex to allowlist the headless `mic_test` consumer of the same shared-lib API).
  - `cmake/AssertNoButtonRoute.cmake` — `driver/src/http_server.cpp` cannot register `POST /button` (regex matches `Post\("/button"` and `srv\.Post\("/button"`); `src/steamvr/` cannot reference `IDriverApi::tap` (regex matches `\.tap\(\)|->tap\(\)|::tap\b`).
  - `cmake/AssertCoVersioning.cmake` — see D-21.
- **D-24:** **All three lints registered as CTest entries**, fail-the-build behavior consistent with prior phases. Run on every build; CI catches drift.

### H. UAT regimen on Bigscreen Beyond + Win11 Pro

- **D-25:** Mandatory before phase-complete; mirrors P7/P8/P9 regimen shape. Approximate item list (planner refines with sub-step granularity):
  1. **Cutover smoke.** Install fresh build, launch SteamVR, cover mic, dashboard toggles. Driver-side detection is the trigger path (verify zero HTTP `POST /button` traffic via Wireshark or a synthetic `/button` 404 check).
  2. **Tray glyph state transitions.** Observe armed-green at idle → triggered-pulse on cover-mic → back to armed-green within 300ms + cooldown. Inject error: kill SteamVR → tray turns red within 1 health-poll cycle (≤1s).
  3. **FAIL-01 mic-permission.** Revoke mic permission in Win11 Privacy settings while client is running. Pill surfaces "Mic access blocked"; click button → Windows Settings opens at `ms-settings:privacy-microphone`. Re-grant → pill clears within one poll.
  4. **FAIL-02 driver-missing.** Stop SteamVR, rename `driver_micmap.dll`, restart SteamVR. Client pill: "Driver not installed". Tray red.
  5. **FAIL-03 SteamVR-not-running.** Stop SteamVR while client is running. Pill: "SteamVR not running"; 1Hz poll cadence (verify via netstat / packet capture — no retry storm). Restart SteamVR → pill clears, tray green.
  6. **FAIL-04 double-instance.** Launch `micmap.exe` twice. Second instance foregrounds the first and exits silently (verify via Task Manager — only one process). Window is restored from minimized if applicable.
  7. **FAIL-05 device-removed.** Unplug USB mic mid-session (or use Device Manager to disable). Pill: "Microphone disconnected"; tray red. Reconnect → driver `IMMNotificationClient` callback rebinds → pill auto-clears, tray green.
  8. **`--debug-trigger` end-to-end (Debug build).** `micmap.exe --debug-trigger` exits 0; HMD dashboard toggles. Release build: same command exits non-zero (route 404).
  9. **Log rotation.** Synthesize 6MB of log writes (e.g., temporary debug-build with verbose logging). Verify rotation: `micmap-driver.log` < 5MB after rotation; `micmap-driver.log.1..5` exist; `.6` does not.
  10. **Version mismatch warning.** Install matched set (driver + client both `vX.Y.Z`). Replace one binary with `vX.Y.Z-prev` (manually). Restart client. WARNING in `micmap.log`; pill "Version mismatch" surfaces; detection still works.
  11. **Installer round-trip on clean Win11 VM.** Install vX.Y.Z → upgrade to vX.Y.Z+1 → uninstall. After uninstall, `vrpathreg show` does not list MicMap; `%PROGRAMFILES(X86)%\Steam\steamapps\common\SteamVR\drivers\micmap` removed; `%APPDATA%\MicMap\` left in place per CFG-04 user-data preservation.
  12. **Binary size regression.** `micmap.exe` size dropped noticeably vs pre-cutover commit. Document delta.
  13. **SVR-05 grep audit.** `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost' driver/src/` returns hits ONLY in `device_provider.cpp` and `manifest_registrar.cpp`. No other driver TU touches `vr::*`.
  14. **HMD sleep/wake stress.** 50 cycles of HMD wake/sleep with detection running. No leaked handles in Process Explorer; vrserver.txt clean.
  15. **Cutover lint go-live verification.** All three new lints fire on a synthetic regression (deliberately reintroduce a `POST /button` registration / a `detector->analyze()` in `apps/micmap/`); build fails. Revert; build passes.

### I. Plan structure (rough — planner refines)

- **D-26:** Approximate seven-wave layout (mirrors P8/P9 wave-shape conventions):
  - **10-00 (Wave 0):** RED-tolerant scaffolds — three new CMake lints (`AssertNoClientDetection`, `AssertNoButtonRoute`, `AssertCoVersioning`) in skip-on-not-found mode; headless test scaffolds (`tests/test_tray_glyph_state_machine.cpp`, `tests/test_fail_pill_priority.cpp`, `tests/test_log_rotation.cpp`, `tests/test_version_mismatch.cpp`); EXISTS-gated ctest registrations. Sibling pattern to P5–P9 Wave 0.
  - **10-01 (Wave 1):** Log rotation (`FileLogSink` extension, TEST-03). Single-version source (`cmake/version.cmake` + `installer/version.iss.in` + `target_compile_definitions(MICMAP_VERSION_STRING)` on driver + client targets + `VS_VERSION_INFO` resource scaffolding). No behavior change yet — this wave is pure plumbing.
  - **10-02 (Wave 2):** Tray-icon state glyphs (HEALTH-08) — three `.ico` resources, `Shell_NotifyIconW(NIM_MODIFY, NIF_ICON)` swap, state-derivation logic (D-05) on existing `/health` + `/state` poll callback. Tray pulse window logic.
  - **10-03 (Wave 3):** FAIL UX pills in driver-health pane — priority-stacked rendering (D-08), one-click action buttons (D-08, D-09 deep-link, D-10 dismiss), FAIL-04 named-mutex at `WinMain` (D-09). `GET /health` gains `driver_version` field via getter-callback (D-19).
  - **10-04 (Wave 4):** `POST /debug/trigger` debug-build-gated endpoint + `IDriverApi::debugTrigger()` method (debug-only) + `--debug-trigger` CLI short-circuit at `WinMain` (D-11, D-12). Documented in dev docs alongside `hmd_button_test.exe`.
  - **10-05 (Wave 5) — CUTOVER:** `default.vrsettings` flag flip to `enable_driver_audio=true` + `enable_driver_detection=true`; delete `POST /button` route; delete `IDriverApi::tap()` decl + impl + all client call sites; delete client-side audio/FFT/state-machine body + `detector->loadTrainingData` calls in `apps/micmap/main.cpp` (~500 LoC); flip three lints to enforcing mode. Single atomic plan, single commit. Includes binary-size measurement + delta documentation (D-03).
  - **10-06 (Wave 6):** INST-09 installer co-versioning — `installer/MicMap.iss` consumes generated `version.iss`; output filename uses `{#MICMAP_VERSION}`; client startup version-mismatch warning + pill (D-20). Installer round-trip validation on clean Win11 VM.
  - **10-07 (Wave 7):** UAT regimen — `10-UAT.md` scaffold + manual D-25(1)..(15) sign-off on Bigscreen Beyond + Win11 Pro. NO post-UAT default-OFF restore (P10 OWNS the flip; the on-rig install becomes the new shipped default). Update CLAUDE.md "Hardware rig" section to reflect post-cutover defaults.
- **D-27:** **Wave dependencies**: 10-00 → 10-01 → (10-02 || 10-03 || 10-04 in parallel) → 10-05 (CUTOVER — depends on all of 10-02/03/04 because the cutover wave assumes the FAIL-pills surface exists for FAIL-02/FAIL-03 to use post-flag-flip; the tray glyph rendering reflects post-cutover state cleanly; and `--debug-trigger` provides a non-audio regression test for the post-cutover trigger pipeline) → 10-06 → 10-07. Planner can reshape the parallelization within reason; the cutover wave dependency on 02/03/04 is firm.

### Claude's Discretion

- Exact policy distinguishing FAIL-02 (driver missing) vs FAIL-03 (SteamVR not running) when both surface as ECONNREFUSED. Recommend the simplest heuristic: ECONNREFUSED with SteamVR process not in `tasklist` → FAIL-03; ECONNREFUSED with SteamVR process running → FAIL-02. Planner verifies the `tasklist` cost is acceptable at 1Hz; if not, fall back to "always FAIL-02 unless user clicks the pill, which then re-checks `tasklist` once." Document the chosen policy in PLAN.
- Tray-icon `.ico` artwork — reuse the existing `apps/micmap/micmap.ico` as a base; armed/triggered/error variants can be color-tinted versions or new artistic renderings. Planner picks; `.ico` files are checked in under `apps/micmap/resources/` (or wherever the existing `micmap.ico` lives). Document the asset source.
- Whether `IDriverApi::debugTrigger()` is a virtual method on the interface or a non-virtual free function in a debug-build-only TU. Recommend virtual on the interface with `#if MICMAP_DEBUG_BUILD` guards; keeps the call site clean. Planner picks the smallest-diff shape.
- Inno Setup `version.iss.in` template format — Inno Setup's `#define` directive is the natural fit (`#define MICMAP_VERSION "X.Y.Z"` at the top, then `[Setup] AppVersion={#MICMAP_VERSION}`). Planner verifies during 10-01 that Inno Setup accepts `configure_file()` substitution syntax (CMake replaces `@MICMAP_VERSION@` → `X.Y.Z`).
- Whether `MICMAP_DEBUG_BUILD` is a new define or piggy-backs on an existing `_DEBUG` / `NDEBUG` / `CMAKE_BUILD_TYPE` check. Recommend a new define driven by `if(CMAKE_BUILD_TYPE STREQUAL "Debug") add_definitions(-DMICMAP_DEBUG_BUILD=1)`. Cleaner than relying on `_DEBUG` (which behaves differently across MSVC vs Clang vs the multi-config generator).
- Pulse-icon visual — single `tray_triggered.ico` held for 300ms is the simplest. If the planner finds a clean way to do a true frame-by-frame pulse (e.g., a sequence of icons via a short timer), document the trade-off; otherwise ship the single-icon-flash form.
- Whether the version-mismatch pill is a separate pill below the FAIL-pill stack or a low-priority entry in the same priority list. Recommend separate (FAIL pills are about reachability/health; version-mismatch is about install hygiene). Planner picks the smallest UI diff.
- Whether `--debug-trigger` accepts an argument (e.g., `--debug-trigger=cooldown` to test cooldown path) or is a single-purpose flag. Recommend single-purpose for v1.6; future-defer parameterization if needed.
- The `apps/micmap/main.cpp:300` startup `loadTrainingData` call — D-01 says delete with the rest of the client-side body. Confirm in PLAN that the client never reads `training_data.bin` post-cutover (driver owns it per P9 IPC-06). If any UI surface still wants to *display* the trained-profile name or summary, route via a new `GET /training/profile-summary` field on `/state` rather than re-reading the file.

### Folded Todos

None — `gsd-sdk query todo.match-phase 10` returned 0 matches.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 10: Cutover & Cleanup" — goal (flag flip + ~500 LoC client deletion + FAIL cluster + tray glyphs + `--debug-trigger` + INST-09 + log rotation), depends on Phase 9, 6 Success Criteria (zero `/button` in client / tray glyph reflects state / FAIL UX deep-links / installer co-versioning / mic_test + `--debug-trigger` + log-rotation + hmd_button_test preserved / SVR-05 invariant survives), STANDARD research flag (deletion phase per v1.5 SVR-04 rip-out template).
- `.planning/REQUIREMENTS.md` §"Driver-Resident Detection (MIG)" MIG-05 + §"Driver-Health UI (HEALTH)" HEALTH-08 + §"Test Affordances (TEST)" TEST-01..03/05 + §"Failure Modes (FAIL)" FAIL-01..05 + §"Installer Co-Versioning (INST)" INST-09 — 12 requirements owned by Phase 10.
- `.planning/PROJECT.md` — locked stack (no framework changes), single-installer constraint, runtime non-elevated, installer elevated, Windows-only.
- `.planning/STATE.md` §"Blockers/Concerns" — Phase 10 `hmd_button_test.exe` retention vs `--debug-trigger` overlap question (resolved here at D-13: keep both, no overlap — they test orthogonal layers).

### Pitfall mitigations Phase 10 owns or finalizes
- `.planning/research/PITFALLS.md` §"Pitfall 7: HTTP server binding `127.0.0.1` only" — survives across the route-deletion (Wave 5) and route-addition (Wave 4 `/debug/trigger`) operations. UAT D-25(13) verifies via netstat audit.
- `.planning/research/PITFALLS.md` §"Pitfall 10: Migration boundary states — double-trigger and stale-callsite hazards" — RESOLVED in Phase 10 by deleting the dual runtime. Post-cutover, only the driver-side detection path exists; `driver_detection_active` and `driver_training_active` `/health` fields remain in the envelope but become structurally always-true (driver runs detection unconditionally when `enable_driver_audio=true`). Document in plan that these fields are now "is the driver active in mode X" indicators rather than "which side is driving X" indicators. Field deletion is deferred to Phase 11 docs / future cleanup; runtime cost is zero.
- `.planning/research/PITFALLS.md` §"Pitfall 14: OnDefaultDeviceChanged" — explicitly OUT of Phase 10 scope. FAIL-05 surfaces device-removed via the existing `IMMNotificationClient` rebind callback from P6/P7. Behavior change (follow-the-default + device pinning) deferred indefinitely.
- `.planning/research/PITFALLS.md` §"Pitfall 11: v1.5 priors" — atomic file-replace pattern reused for log rotation (D-14: `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`).

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 6: Cutover & Cleanup" (research-numbered = roadmap Phase 10) — research-derived rationale for the rip-out shape and FAIL UX language.
- `.planning/research/ARCHITECTURE.md` — post-migration thread model; HTTP handlers mutate atomic state on HTTP thread, never call `vr::*`, never push to `CommandQueue` (except the new debug-build `/debug/trigger` which DOES push to CommandQueue — same producer pattern as the deleted `/button` route, just gated by build type instead of v1.5 fallback flag).
- `.planning/codebase/ARCHITECTURE.md` §"Detection to State Machine to Action" + §"Application Layer" — current dual-runtime shape that P10 collapses.
- `.planning/codebase/STRUCTURE.md` — `driver/src/`, `apps/micmap/`, `apps/mic_test/`, `apps/hmd_button_test/`, `installer/`, `cmake/`, shared-lib layouts.
- `.planning/codebase/STACK.md` — locked stack; P10 adds nothing new (Inno Setup, cpp-httplib, ImGui, KissFFT all already in stack — KissFFT just stops being linked into client EXE).
- `.planning/codebase/CONCERNS.md` — `DeviceNotificationClient` ComPtr migration deferred (P10+ → backlog); FFT-on-every-frame perf cost (CONCERNS.md Performance Bottleneck #2 — backlog).

### Phase boundary inheritance
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` — D-10/D-11 (driver links `micmap_core_runtime` PRIVATE; client EXE drops detection deps when client-side body is gone). P10 SC1 confirms binary-size delta.
- `.planning/phases/06-driver-side-audio-capture-spike/06-CONTEXT.md` — D-04/D-05 AudioWorker apartment trick + D-13/D-14 reverse-order teardown + D-15/D-16 `IMMNotificationClient` alive-flag (P10 inherits all unchanged; FAIL-05 surfaces existing rebind behavior, no new lifecycle).
- `.planning/phases/07-driver-side-detection-thread/07-CONTEXT.md` — D-09 `/health.driver_detection_active` field pattern (P10 D-19 mirrors for `driver_version`); D-15 atomic-snapshot publish/load mechanism (unchanged); D-25 UAT regimen shape (P10 D-25 mirrors).
- `.planning/phases/08-ipc-contract-reshape/08-CONTEXT.md` — D-07 atomic single-writer cutover protocol (P10 D-01 mirrors verbatim for the multi-target client-side body cutover); D-09 driver-loaded gate (P10 FAIL-pill stack inherits — pills appear in the same pane); D-11 driver-health pane (P10 D-07 surface lives here); D-14 all-or-nothing validation envelope (P10 inherits for all server-side payload validators); D-16 `last_error` simplicity (P10 inherits — no structured error history); D-22/D-23 `IDriverApi` rename + new methods incremental pattern (P10 D-12 adds `debugTrigger()` debug-build-only); D-26 client poll cadence (P10 D-05 reuses for tray glyph state-derivation — no new poll); D-29 wave layout shape (P10 D-26 mirrors with one extra wave for installer co-versioning).
- `.planning/phases/09-training-migration/09-CONTEXT.md` — D-07 `/health.driver_training_active` field pattern (P10 D-19 mirrors for `driver_version`); D-23 atomic single-writer cutover protocol (P10 D-01 mirrors at multi-target scale); D-38 wave layout (P10 D-26 mirrors with one extra wave); D-40 default-flag-OFF discipline through P9 (P10 OWNS the flip per ROADMAP — P10 D-01 flips both `enable_driver_audio` and `enable_driver_detection` to `true`); D-39 UAT regimen shape (P10 D-25 mirrors); P9-04 `tryRunReplayCli` short-circuit pattern at `apps/mic_test/main.cpp` (P10 D-12 mirrors for `--debug-trigger` short-circuit at `apps/micmap/main.cpp` `WinMain`).

### v1.5 invariants carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-04 — the rip-out discipline template (eliminate fallback paths, no dual-mode runtime). P10 cutover IS the v1.6 application of this template at full scale.
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. P10 deletes `/button` (was a producer) and adds `/debug/trigger` (debug-build only producer). Producer count goes from {DetectionRunner, /button-route, /debug/trigger-route} to {DetectionRunner, /debug/trigger-route-debug-only}. UAT D-25(13) verifies via grep audit.
- `.planning/milestones/v1.5-ROADMAP.md` CFG-01..05 — atomic-file-replace + corruption-backup retention. P10 D-14 reuses the `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` primitive for log rotation.

### In-tree code touched by P10
- `driver/resources/settings/default.vrsettings` — flip `enable_driver_audio` and `enable_driver_detection` to `true` (Wave 5 cutover).
- `driver/src/http_server.{hpp,cpp}` — DELETE `POST /button` route registration (Wave 5); ADD `POST /debug/trigger` route registration gated by `#if MICMAP_DEBUG_BUILD` (Wave 4); ADD `driver_version` field to `GET /health` JSON via existing getter-callback ctor pattern (Wave 3, D-19); ctor evolves with new `driverVersionGetter` callback.
- `driver/src/device_provider.{hpp,cpp}` — pass `[]() { return MICMAP_VERSION_STRING; }` to `HttpServer` ctor for the new health field (Wave 3); no other change.
- `src/steamvr/include/micmap/steamvr/driver_api.hpp` — DELETE `tap()` declaration (Wave 5); ADD `debugTrigger()` declaration gated by `#if MICMAP_DEBUG_BUILD` (Wave 4).
- `src/steamvr/src/driver_api.cpp` — DELETE `tap()` impl (Wave 5); ADD `debugTrigger()` impl gated by `#if MICMAP_DEBUG_BUILD` (Wave 4).
- `apps/micmap/main.cpp` — DELETE entire client-side audio/FFT/state-machine body (~500 LoC, Wave 5): `audioCapture` / `detector` / `stateMachine` instances + the audio callback + the local trigger callback wiring + `detector->loadTrainingData` at `:326`, `:647`, `:970`. ADD `--debug-trigger` short-circuit at `WinMain` entry (Wave 4). ADD FAIL-04 single-instance named-mutex at `WinMain` very-early (Wave 3). ADD tray glyph state-swap logic on the existing `/health` + `/state` poll callback (Wave 2). ADD FAIL-pill rendering + version-mismatch pill in driver-health pane (Wave 3, Wave 6).
- `apps/micmap/resources/tray_armed.ico`, `apps/micmap/resources/tray_triggered.ico`, `apps/micmap/resources/tray_error.ico` (NEW) — tray glyph assets (Wave 2). Existing `apps/micmap/micmap.ico` is the executable icon and is unrelated to the tray icon swap (though planner can reuse as a base).
- `src/common/src/sinks/file_log_sink.cpp` — extend with synchronous size-check + 5-generation rotation (Wave 1, D-14..D-17). Single file change; class shape stays the same; constructor still takes a path.
- `cmake/version.cmake` (NEW) — single source-of-truth `MICMAP_VERSION` semver (Wave 1, D-18).
- `installer/version.iss.in` (NEW) — Inno Setup `#define MICMAP_VERSION "X.Y.Z"` template; configured at CMake configure time into `installer/version.iss` (Wave 1, D-18).
- `installer/MicMap.iss` — `#include "version.iss"`; `[Setup] AppVersion={#MICMAP_VERSION}`; output filename `MicMap-Setup-v{#MICMAP_VERSION}.exe` (Wave 6, D-18, D-22).
- `apps/micmap/CMakeLists.txt` + `driver/CMakeLists.txt` — `target_compile_definitions(... PRIVATE MICMAP_VERSION_STRING="${MICMAP_VERSION}")`; `target_sources(... PRIVATE micmap.rc / driver_micmap.rc)` with `configure_file(VS_VERSION_INFO templates)` (Wave 1, D-18).
- `cmake/AssertNoClientDetection.cmake` (NEW) — sibling lint to P9's `AssertNoClientTraining.cmake`. Forbids `IAudioCapture::start|setAudioCallback`, `INoiseDetector::analyze`, `IStateMachine::update`, `detector->loadTrainingData` in `apps/micmap/` (allowlist `apps/mic_test/`). Wave 0 RED-tolerant; Wave 5 enforcing.
- `cmake/AssertNoButtonRoute.cmake` (NEW) — forbids `Post\("/button"` in `driver/src/` and `\.tap\(\)|->tap\(\)|::tap\b` in `src/steamvr/`. Wave 0 RED-tolerant; Wave 5 enforcing.
- `cmake/AssertCoVersioning.cmake` (NEW) — verifies `cmake/version.cmake` exists, defines `MICMAP_VERSION`, and that the resolved value matches across driver target / client target / generated `installer/version.iss`. Wave 0 RED-tolerant; Wave 6 enforcing.
- `tests/test_tray_glyph_state_machine.cpp` (NEW) — headless state-derivation tests (D-05) for armed/triggered/error transitions; pulse-window 300ms boundary.
- `tests/test_fail_pill_priority.cpp` (NEW) — headless tests for D-08 priority stacking under combinations of FAIL conditions.
- `tests/test_log_rotation.cpp` (NEW) — synthesize 5MB+ writes, assert rotation + 5 generations + atomic move (D-14..D-17).
- `tests/test_version_mismatch.cpp` (NEW) — given a client `MICMAP_VERSION_STRING` and a fake `/health.driver_version`, assert warning + pill state (D-20).
- CI pipeline / `tests/CMakeLists.txt` — register the four new test executables; register `cmake/AssertNoClientDetection`, `cmake/AssertNoButtonRoute`, `cmake/AssertCoVersioning` as ctest entries.

### Reusable assets (already in the tree)
- `apps/micmap/main.cpp:264` — existing `Shell_NotifyIconW(NIM_ADD)` call. **Wave 2 reuses the same `NOTIFYICONDATAW` (`g_app.nid`) — only `nid.hIcon` changes per state, swapped via `Shell_NotifyIconW(NIM_MODIFY, &g_app.nid)`.**
- `apps/micmap/first_launch_balloon.{hpp,cpp}` — P3 D-09 / D-10 first-launch tray balloon. **Pattern reference for Shell_NotifyIcon usage (NIM_MODIFY + uFlags handling). P10 doesn't modify this file.**
- `apps/micmap/main.cpp:515-527` — existing 1Hz `/health` + 2Hz `/state` poll wiring (P8 D-26). **Wave 2 hooks tray glyph state-swap into the same callback. Wave 3 hooks FAIL pill rendering into the same poll's UI render. No new poll.**
- `src/common/include/micmap/common/logger.hpp` + `src/common/src/sinks/file_log_sink.cpp` (P8) — `FileLogSink` class. **Wave 1 extends with rotation logic; class shape unchanged.**
- `src/steamvr/include/micmap/steamvr/driver_api.hpp` + `src/steamvr/src/driver_api.cpp` (P8 rename) — `IDriverApi` interface. **Wave 4 adds `debugTrigger()` (debug-build-only). Wave 5 deletes `tap()`.**
- `apps/mic_test/main.cpp` `tryRunReplayCli` (P9-04) — `WinMain` short-circuit pattern. **Wave 4 mirrors for `--debug-trigger` short-circuit in `apps/micmap/main.cpp`.**
- `installer/MicMap.iss` (P4) — existing Inno Setup script. **Wave 6 adds `#include "version.iss"` and changes `AppVersion` + output filename. No structural rewrite.**
- `apps/hmd_button_test/main.cpp` — TEST-05 developer tool. **P10 leaves untouched per D-13.**
- v1.5 `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` helper from CFG-04 — **D-14 reuses for log rotation.**

### Sister-project reference
- None applicable. v1.5 sister-project HMD button stub work is fully integrated and orthogonal to cutover.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `apps/micmap/main.cpp:264` — existing tray-icon `NIM_ADD` registration with `NIF_ICON | NIF_MESSAGE | NIF_TIP` flags. **Wave 2 reuses the same `g_app.nid` for `NIM_MODIFY` glyph swaps.**
- `apps/micmap/main.cpp:515-527` — `IsIconic`-gated 1Hz `/health` + 2Hz `/state` poll loop (P8 D-26). **Wave 2 + Wave 3 hook tray-swap and FAIL-pill rendering into this loop's render-tick. No new poll.**
- `apps/micmap/first_launch_balloon.{hpp,cpp}` — P3 first-launch balloon pattern; reference for `Shell_NotifyIconW` modification calls.
- `src/common/src/sinks/file_log_sink.cpp` (P8) — `FileLogSink` class with atomic append. **Wave 1 extends with rotation logic in-place.**
- `installer/MicMap.iss` (P4) — Inno Setup script; `[Setup]` section with `AppVersion`, `OutputBaseFilename`, `[Files]` block. **Wave 6 wires `#include version.iss` + `{#MICMAP_VERSION}` substitution.**
- `apps/mic_test/main.cpp` `tryRunReplayCli` (P9-04) — `WinMain` early-return pattern for CLI flag short-circuit. **Wave 4 mirrors for `--debug-trigger` in `apps/micmap`.**
- `cmake/AssertNoClientTraining.cmake` (P9-03) — sibling lint shape with qualifier-prefixed regex narrowing. **Wave 0 `AssertNoClientDetection` mirrors verbatim.**
- `cmake/AssertNoConfigWriteInClient.cmake` (P8-05) — single-writer cutover lint pattern. **Wave 0 `AssertNoButtonRoute` mirrors.**
- `driver/src/http_server.cpp` `GET /health` callback chain (P7 D-09 → P8 → P9 D-07) — getter-callback pattern for envelope fields. **Wave 3 D-19 adds `driver_version` getter following the same shape.**
- `apps/micmap/main.cpp:326,:647,:970` — three `detector->loadTrainingData(configManager->getTrainingDataPath())` call sites. **Wave 5 deletes all three (driver owns training data per IPC-06).**

### Established Patterns
- **Atomic single-plan cutover** — P8 D-07 (config.json sole-writer), P9 D-23 (training_data.bin sole-writer). **P10 D-01 mirrors at multi-target scale: server route + interface method + client body + flag flip + lint go-live in one commit.**
- **Skip-on-not-found → enforcing lint go-live** — P5 / P6 / P7 / P8 / P9 Wave 0 → Wave N pattern. **P10 D-23 adds three new lints following identical shape.**
- **Getter-callback for `/health` envelope fields** — P7 D-09 (`driver_detection_active`) + P9 D-07 (`driver_training_active`). **P10 D-19 (`driver_version`) follows the same shape. Composition root in `DeviceProvider::Init` passes a tiny lambda.**
- **`WinMain` short-circuit for CLI flags** — P9-04 `tryRunReplayCli` in `apps/mic_test`. **P10 D-12 `--debug-trigger` mirrors in `apps/micmap`.**
- **`std::atomic<...>` snapshots for cross-thread state** — P7 / P8 / P9. **P10 inherits unchanged; tray-glyph state derivation reads existing snapshot fields.**
- **HTTP route handler discipline (SVR-05)** — v1.5 invariant. HTTP thread never touches `vr::*`. **P10 D-11 `/debug/trigger` mutates `CommandQueue` (the only producer-style HTTP route post-cutover, debug-build only) — same discipline as the deleted `/button` route, gated by build type instead of v1.5 fallback flag.**
- **Atomic `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`** — v1.5 CFG-04 (config.json) + P8 D-14 (config.json) + P9 D-27 (training_data.bin). **P10 D-14 reuses for log rotation.**
- **Composition-root logger setup** — P8 D-19/D-20/D-21. **P10 inherits unchanged; rotation lives inside `FileLogSink`, sink-construction call sites in `DeviceProvider::Init` and `WinMain` are unmodified.**

### Integration Points
- `driver/src/http_server.hpp` — ctor evolves with `std::function<std::string()> driverVersionGetter` callback (Wave 3); `SetupRoutes()` gains `POST /debug/trigger` registration `#if MICMAP_DEBUG_BUILD` (Wave 4); `SetupRoutes()` deletes `POST /button` registration (Wave 5).
- `driver/src/device_provider.cpp` — `Init` passes `[]() -> std::string { return MICMAP_VERSION_STRING; }` to the new ctor parameter (Wave 3). No state member added.
- `apps/micmap/main.cpp` — five distinct integration points across waves: tray-glyph swap (Wave 2), FAIL-pill render + named-mutex single-instance + version-mismatch pill (Wave 3 / Wave 6), `--debug-trigger` short-circuit (Wave 4), client-side body deletion (Wave 5).
- `cmake/CMakeLists.txt` (root) — include three new lint scripts; consume `cmake/version.cmake`; expose `MICMAP_VERSION_STRING` via `target_compile_definitions` on driver and client targets; configure `installer/version.iss.in` → `installer/version.iss` via `configure_file()`.
- `installer/MicMap.iss` — single line addition (`#include "version.iss"`); existing `[Setup]` section updated to use `{#MICMAP_VERSION}`.
- CI pipeline / `tests/CMakeLists.txt` — register four new test executables (Wave 0 RED-tolerant scaffolds, Wave 1..3 fill in real assertions); register three new lint ctest entries.

</code_context>

<specifics>
## Specific Ideas

- **"Cutover is one commit, not a migration."** Single load-bearing sentence for P10 Wave 5. Server route + interface method + client body + flag flip + lint go-live all atomic. Same logic that drove P8/P9 single-writer cutovers, applied at multi-target scale. Staged cutover would leave the codebase in an unshippable interim state.
- **Tray glyph rides existing polls.** No new HTTP poll, no new state thread. State derivation is a pure function of the `/state` envelope already polled at 2Hz. Pulse on triggered = brief 300ms flash to a distinct icon, auto-revert via timestamp check on the next poll tick. Held-red on triggered would conflate with the error state.
- **FAIL pills are pane-resident and priority-stacked.** No modals (interrupt), no balloons (Focus Assist suppression). Single topmost pill at a time; each FAIL has a one-click action (deep-link / open SteamVR / dismiss). Multi-error aggregation explicitly NOT in v1.6 — adds shape complexity without user value (matches P8 D-16 `last_error` simplicity).
- **`--debug-trigger` and `hmd_button_test` test orthogonal layers.** `hmd_button_test` exercises OpenVR input directly (creates its own `vr::IVRInput`); `--debug-trigger` exercises driver IPC + CommandQueue + RunFrame. STATE.md "open question about overlap" resolves to no-overlap. Keep both; document the distinction in dev docs.
- **Synchronous log rotation, no thread.** `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` is atomic; size-check is cheap; rotation is rare. Stateless rotation logic; reuses existing logger-level mutex from P8 `MultiSinkLogger`. Failure-mode is "log warning to non-file sink, continue at oversize" — soft bound, never lose log lines.
- **Single version source-of-truth: `cmake/version.cmake`.** Driver / client / installer all read from one place via `configure_file()`. Drift detection by `cmake/AssertCoVersioning.cmake`. Hard-block on version mismatch was rejected (would brick mid-upgrade users); warn + pill is the v1.6 shape.
- **Default flag flip is part of cutover, not a separate concern.** Without the deletion, the flag flip is meaningless (both runtimes still alive); without the flag flip, the deletion bricks the install (driver detection off + client detection deleted = no detection). Atomic together, not staged.
- **Post-cutover, P10 OWNS the on-rig install state.** No "post-UAT default-OFF restore" in this phase's UAT regimen — the on-rig install becomes the new shipped default. Update CLAUDE.md "Hardware rig" section to reflect.

</specifics>

<deferred>
## Deferred Ideas

- **Removing `driver_detection_active` and `driver_training_active` `/health` fields** — post-cutover these become structurally always-true. **Phase 11** (docs phase or future cleanup); zero runtime cost to leave them.
- **Multi-error aggregation in FAIL pills** — single topmost-priority pill is the v1.6 shape (matches P8 D-16). Future observability/UX milestone if real users hit it.
- **Structured `last_error`** (severity / code / history) — same. Single string + clear stays the v1.6 shape.
- **Hard-block dialog on version mismatch** — rejected (would brick mid-upgrade users). Warn + pill is the v1.6 shape.
- **`OnDefaultDeviceChanged` follow-the-default + device pinning** (Pitfall 14) — deferred indefinitely. FAIL-05 surfaces the existing rebind-on-reappear behavior; full follow-the-default is a separate audio-UX phase.
- **`DeviceNotificationClient` ComPtr migration** — backlog (CONCERNS.md). Alive-flag pattern from P6 D-15/D-16 carries through P10.
- **FFT-on-every-frame perf cost** — CONCERNS.md Performance Bottleneck #2. **Not addressed in P10.** Targeted perf phase or backlog. Replay harness from P9-04 will likely surface this if agents start running large corpora.
- **HEALTH-D1 per-component health badges** — future GUI revamp.
- **HEALTH-D2 trigger-history sparkline** — future GUI revamp.
- **HEALTH-D3 in-VR overlay status pill** (UX-02) — future overlay milestone (overlay stubs in `dashboard_manager.cpp` remain as tracked tech debt).
- **TRAIN-D2 spectral-profile sparkline / A/B threshold preview** — future GUI revamp milestone.
- **TRAIN-D3 configurable target sample count** — future.
- **TEST-D2 `/debug/snapshot` driver endpoint** — already deferred per PROJECT.md.
- **OBS-01 unified driver+client log file** — future observability milestone. P8/P10 ship separate files (driver + client).
- **DIST-01 installer launch checkbox / DIST-02 silent-install docs / DIST-03 non-default-Steam-path support** — out of v1.6 scope.
- **DET-01/02 detection accuracy in noisy environments** — out of v1.6 scope.
- **UX-01 in-app auto-start toggle** — future settings UX milestone.
- **cpp-httplib next bump** — no new CVEs flagged; defer.
- **Inno Setup → MSIX migration / signed installer** — future distribution milestone.
- **Crash-dump / minidump capture for driver crashes** — future observability milestone.
- **Removing the `enable_driver_audio` / `enable_driver_detection` keys entirely** — kept post-cutover for emergency override (user can flip to false to disable). Future cleanup if they're deemed dead weight.
- **Parameterizing `--debug-trigger`** (e.g., `--debug-trigger=cooldown`) — future.

### Reviewed Todos (not folded)
None — `cross_reference_todos` returned 0 matches.

</deferred>

---

*Phase: 10-cutover-cleanup*
*Context gathered: 2026-05-10*

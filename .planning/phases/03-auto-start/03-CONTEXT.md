# Phase 3: Auto-Start - Context

**Gathered:** 2026-04-23
**Status:** Ready for planning

<domain>
## Phase Boundary

SteamVR launches `micmap.exe` automatically when SteamVR starts, MicMap exits cleanly on `VREvent_Quit`, and registration is idempotent — no console window, no focus steal, no respawn loop.

Deliverables this phase:
- `app.vrmanifest` file emitted next to `micmap.exe` (`app_key = "bigscreen.micmap"`, `is_dashboard_overlay = true`, `launch_type = "binary"`, `binary_path_windows = "micmap.exe"`, `arguments = ["--minimized"]`).
- `--register-vrmanifest` / `--unregister-vrmanifest` headless CLI modes invocable by the Phase 4 installer.
- `manifest_registrar` module in `src/steamvr/` wrapping `IVRApplications_008::AddApplicationManifest` → `IsApplicationInstalled` poll → `SetApplicationAutoLaunch`.
- Idempotent re-registration on every GUI startup (fire-and-forget async) plus quiet retry loop when SteamVR is not yet running.
- `VREvent_Quit` handling with `AcknowledgeQuit_Exiting` + ordered subsystem teardown.
- Silent auto-launch UX: hidden window, tray-icon only, first-silent-launch Windows balloon notification.

Out of scope (other phases / future milestones):
- Installer packaging and `[Run] / [UninstallRun]` wiring of the CLI modes → Phase 4.
- Auto-start toggle checkbox in MicMap's own settings UI → deferred to v1.x (noted in PROJECT.md).
- Finished-page "Launch SteamVR" step in the installer → deferred to v1.x.
- Any README / docs reflecting the new auto-start flow → Phase 5.

</domain>

<decisions>
## Implementation Decisions

### CLI surface & headless runtime (AUTO-02, AUTO-03, AUTO-06)

- **D-01:** CLI argument parsing uses `CommandLineToArgvW` once at `WinMain` entry, followed by a `wcscmp` loop to set a flags struct `{bool register_manifest, bool unregister_manifest, bool minimized}`. No new dependency. Matches the "no new deps" milestone guardrail and the current tiny flag surface (3 flags).
- **D-02:** When `--register-vrmanifest` or `--unregister-vrmanifest` is present, `WinMain` **early-exits** before `RegisterClassExW`: do `VR_Init(VRApplication_Utility)` → call `manifest_registrar::register()` or `::unregister()` → `VR_Shutdown()` → `return exit_code`. No D3D, no ImGui, no audio, no detector, no `driverClient`, no tray. Fast (sub-100ms total).
- **D-03:** Exit codes: **`0` on success, `1` on any failure** — matches Inno Setup `[Run]` default `ExitCodesNotExpected=1`. Failure reason goes to the log file (D-04); installer only consumes the pass/fail bit.
- **D-04:** Headless CLI modes log via the existing `common::Logger` sink (`%APPDATA%\MicMap\micmap.log`). No `AllocConsole` / `AttachConsole` — keeps AUTO-06 invariant (no console window ever).

### Silent auto-launch UX (AUTO-06)

- **D-05:** **SteamVR auto-launches pass `--minimized` via `app.vrmanifest`'s `arguments` field** (`"arguments": ["--minimized"]`). User-clicked shortcuts do not. This is the sole signal the app uses to start silent. No parent-process inspection, no environment sniffing, no heuristics.
- **D-06:** Silent-mode window policy: `CreateWindowW` runs as today but **`ShowWindow` is never called**; `minimizedToTray = true` from boot. Tray icon's "Show" menu item restores normally. Matches the current `--minimized` behavior.
- **D-07:** Console-window guardrail: continue relying on `/SUBSYSTEM:WINDOWS` (already in `apps/micmap/CMakeLists.txt`). Add a verification grep gate at phase exit: `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` must return zero results. No runtime `FreeConsole()` call.
- **D-08:** Single-instance mutex: keep existing "focus existing window + exit" path for user-clicked second instances. **When the second instance has `--minimized` in argv, skip the `SetForegroundWindow` / `PostMessage(IDM_SHOW)` calls** and exit silently. Prevents a SteamVR re-launch from stealing focus from the already-running tray app.
- **D-09:** **First-silent-launch tray balloon**: the first time the app is launched with `--minimized` after install, fire a `Shell_NotifyIcon(NIM_MODIFY, ...)` with `NIF_INFO` balloon — title `"MicMap"`, body something like `"Running in the system tray. Click the icon to open."`. Persistence: a new `AppConfig` bool field **`shownTrayNotification`** (default `false`), flipped to `true` after first balloon. Round-trips through `config_manager` (leverages Phase 2 defensive reader with `.value(key, false)` for forward-compat). Reset naturally on uninstall since `%APPDATA%\MicMap\config.json` is removed.
- **D-10:** Notification API: **`Shell_NotifyIcon` `NIM_MODIFY` + `NIF_INFO`** on the existing tray icon. No Win10+ Toast / `ToastNotificationManager` — that would add COM-activator + AppUserModelID surface for the installer, and Phase 3 scope is auto-start, not notification infrastructure.

### Shutdown lifecycle (AUTO-05)

- **D-11:** `vr::VRSystem()->AcknowledgeQuit_Exiting()` is called **synchronously inside `vr_input.cpp`'s `pollEvents()`, immediately upon seeing `VREvent_Quit`, BEFORE dispatching the app callback**. This guarantees Valve's 2-second quit watchdog is satisfied regardless of how long app-side teardown takes. Direct mitigation of research Pitfall 2 / OpenVR issue #1425.
- **D-12:** Teardown runs in an explicit `MicMapApp::shutdown()` method called from the main-loop exit path (after `while(running)` breaks, before ImGui / D3D shutdown). Ordered (reverse of init):
  1. Stop audio capture
  2. Reset detector
  3. `driverClient->disconnect()`
  4. `vrInput->shutdown()`
  5. Tray icon remove (`Shell_NotifyIcon NIM_DELETE`)
  6. ImGui / D3D / window destruction (existing `CleanupDeviceD3D` etc.)
- **D-13:** **No watchdog thread / no `TerminateProcess` fallback.** Because the ack happens ack-first (D-11), Valve's clock is already stopped when teardown begins; teardown may take whatever it needs without risking a force-kill. Simpler correct design.
- **D-14:** All exit paths (VR Quit, Alt-F4, tray Exit, fatal error) converge on the same `running = false` → `shutdown()` sequence. No separate reason enum; logging can infer via surrounding log context if needed.

### Registration flow (AUTO-01, AUTO-02, AUTO-04)

- **D-15:** GUI startup re-registers on **every boot, fire-and-forget via `std::async(std::launch::async, ...)`**. The task: `VR_Init(Utility)` → `IsApplicationInstalled(app_key)` → if `false`, call `AddApplicationManifest` + poll + `SetApplicationAutoLaunch`; if `true`, no-op. Self-heals across SteamVR upgrades and user-initiated manifest removal (AUTO-04).
- **D-16:** When `VR_Init` fails with `VRInitError_Init_HmdNotFound` / `VRInitError_Init_NoServerForBackgroundApp` (SteamVR not running): **silent retry loop, 30s interval, stops after first successful registration**. No log spam for the expected "SteamVR not running" case. Log INFO once on first successful registration; log WARNING once with the enum name if the error is anything else (unexpected). Detached thread; joins on app shutdown.
- **D-17:** `IsApplicationInstalled` poll tuning (OpenVR issue #1378): **100ms ticks, 2000ms ceiling (20 attempts max)**, log `"polling for manifest install"` once on entry and either `"manifest ready after Nms"` or `"timeout after 2000ms"` on exit. No per-tick log output.
- **D-18:** Re-registration task logs at **INFO on state change only** (not-installed → installed). Subsequent boots that find the manifest already installed emit no log output.

### File + module layout

- **D-19:** **`app.vrmanifest` is generated at build time via CMake `configure_file`** from `apps/micmap/app.vrmanifest.in`. Substitutes `@MICMAP_VERSION@` and `@MICMAP_APP_KEY@` (locked to `bigscreen.micmap`). CMake emits the final `app.vrmanifest` alongside `micmap.exe` in the build output dir. Installer (Phase 4) will pick it up from the staged install layout.
- **D-20:** **New module**: `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` + `src/steamvr/src/manifest_registrar.cpp`. Factory + interface matching project CONVENTIONS.md (`createManifestRegistrar()`, `IManifestRegistrar`). Keeps the `IVRApplications_008` surface inside the `steamvr/` library rather than leaking into `apps/micmap/`.
- **D-21:** Absolute path of `app.vrmanifest` for `AddApplicationManifest` is computed at runtime via `GetModuleFileNameW` → strip filename via `PathCchRemoveFileSpec` → append `L"app.vrmanifest"`. UTF-16 native, handles long paths. Exact helper shape is Claude's discretion.

### Claude's Discretion

- Exact interface shape of `IManifestRegistrar` — e.g., `registerApp() / unregisterApp() / ensureRegistered()` vs one method with an enum. Must be invokable from both CLI modes and the GUI async task.
- Argument-parsing struct location — anonymous-namespace helper in `main.cpp` vs a tiny `common::CliArgs` utility. Whatever keeps `WinMain` under the current line budget.
- `app.vrmanifest` optional fields: `strings.en_us.name` ("MicMap"), `strings.en_us.description`, `image_path` (icon shown in SteamVR "Manage Startup Overlay Apps"). Icon source may reuse `apps/micmap/micmap.rc`'s icon or a new PNG sibling. Standard defaults acceptable.
- Exact wording of the first-silent-launch tray balloon body text.
- Log-line wording for registration states / errors beyond the enum-name requirement in D-16.
- Precise placement of the quiet-retry background thread (member of `MicMapApp` vs local to a new `manifest_registrar` free function launched from the app).
- Phase 3 validation harness: manual UAT is acceptable (SteamVR full-restart cycle cannot be automated headlessly). Optional extension of `hmd_button_test` or a new `--smoketest` mode is nice-to-have, not required.
- New `AppConfig` field `shownTrayNotification` added during Phase 3 execution. Config-manager diff is a one-field read/write + `.value` default; no Phase 2 re-open needed.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 3 requirements + roadmap

- `.planning/REQUIREMENTS.md` §"Auto-Launch (AUTO)" — AUTO-01 through AUTO-06, the falsifiable acceptance criteria.
- `.planning/ROADMAP.md` §"Phase 3: Auto-Start" — goal, dependencies, success criteria, research-spike note on OpenVR issue #1547.
- `.planning/PROJECT.md` §"Key Decisions" — "SteamVR-native auto-start (`app.vrmanifest`) — no Windows Run-key / Startup folder" + `app_key` = `"bigscreen.micmap"` stable across versions.

### Research

- `.planning/research/SUMMARY.md` §"Phase 3: Auto-Launch (AUTO-01)" — recommended architecture for `manifest_registrar`, OpenVR SDK 2.15.6 interface notes.
- `.planning/research/SUMMARY.md` §"Critical Pitfalls" — Pitfall 2 (shutdown loop / OpenVR #1425), Pitfall 5 (`SetApplicationAutoLaunch` race / OpenVR #1378), Pitfall 3 manifest analog (`IsApplicationInstalled` idempotency).
- `.planning/research/SUMMARY.md` §"Research flags" — `SetApplicationAutoLaunch` persistence bug (OpenVR issue #1547); requires multi-restart UAT to characterize frequency.

### Prior phase context (decisions that flow into Phase 3)

- `.planning/phases/01-driver-sidecar-migration/01-CONTEXT.md` — Driver now owns `/input/system/click`; app uses `IDriverClient::tap()` only. App-side `vrInput` is a pure lifecycle monitor (Quit / SteamVRConnected / SteamVRDisconnected).
- `.planning/phases/02-config-read-back/02-CONTEXT.md` — Defensive `nlohmann/json` reader already landed with `.value(key, default)` forward-compat. New `shownTrayNotification` field (D-09) slots into the existing pattern.

### Codebase intel

- `.planning/codebase/ARCHITECTURE.md` — current two-process model, layered libraries, main-loop shape.
- `.planning/codebase/STRUCTURE.md` — directory layout for `src/steamvr/`, `apps/micmap/`.
- `.planning/codebase/CONVENTIONS.md` — factory + interface pattern, `unique_ptr` ownership, `MICMAP_LOG_*` macros, no try-catch idiom (use non-throwing APIs).
- `.planning/codebase/TESTING.md` — manual-UAT conventions for SteamVR-integration validation.

### Existing code (integration points)

- `apps/micmap/main.cpp:562` — `WinMain` entry; current `--minimized` parse at `:596`; single-instance mutex at `:563`. Entry point for CLI mode fork (D-02) and for argv re-parse (D-01).
- `apps/micmap/main.cpp:218-227` — current `vrInput->setEventCallback` wiring that flips `running=false` on `VREventType::Quit`. `AcknowledgeQuit_Exiting` call site (D-11) lives inside `vr_input.cpp`'s poll routine, not here.
- `src/steamvr/include/micmap/steamvr/vr_input.hpp:62-122` — `IVRInput` surface. `pollEvents()` is where D-11's synchronous ack lands.
- `src/steamvr/src/vr_input.cpp:285` — `VR_Init(VRApplication_Background)` precedent. `manifest_registrar` uses `VRApplication_Utility` instead (D-02, D-15).
- `apps/micmap/micmap.rc` + `apps/micmap/resource.h` — existing icon resources; candidate source for `app.vrmanifest`'s `image_path` icon.
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig` struct. D-09's `shownTrayNotification` field lands here.

### OpenVR API surface (consult SDK headers directly at planning time)

- `IVRApplications_008::AddApplicationManifest(absolutePath, bTemporary=false)`
- `IVRApplications_008::IsApplicationInstalled(pchAppKey)` — polling target after `AddApplicationManifest`.
- `IVRApplications_008::SetApplicationAutoLaunch(pchAppKey, bAutoLaunch)` — only call after `IsApplicationInstalled` returns true (OpenVR #1378).
- `IVRApplications_008::RemoveApplicationManifest(pchApplicationsManifestFullPath)` — Phase 3 unregister path.
- `IVRSystem::PollNextEvent` (already in use) — source of `VREvent_Quit`.
- `IVRSystem::AcknowledgeQuit_Exiting()` — D-11 call site.
- `VR_Init(VRApplication_Utility)` — for CLI modes and the async re-registration task; does NOT require an HMD.

### External references (not canonical, for context)

- `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — Phase 4 installer reference; lists `app_key` wiring conventions Phase 3 needs to be compatible with.
- OpenVR issues #1378 (SetApplicationAutoLaunch race), #1425 (shutdown loop / ack watchdog), #1547 (auto-launch persistence drift) — as cited in research SUMMARY.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`apps/micmap/main.cpp` `WinMain`** — already `/SUBSYSTEM:WINDOWS`, already has `--minimized` flag parsed via `strstr(lpCmdLine, "--minimized")` (D-01 replaces this with `CommandLineToArgvW` + loop), already has single-instance mutex (D-08 extends this).
- **Tray icon code in `main.cpp`** (`SetupSystemTray`, `Shell_NotifyIcon` calls) — D-09/D-10 balloon reuses the existing `NOTIFYICONDATA` via `NIM_MODIFY + NIF_INFO`.
- **`src/steamvr/src/vr_input.cpp` event pump** — existing `pollEvents()` already iterates `VREvent` via `PollNextEvent`. D-11 adds one `AcknowledgeQuit_Exiting()` call in the `VREvent_Quit` branch, pre-callback.
- **`common::Logger`** (`MICMAP_LOG_INFO / _WARNING / _ERROR`) writing to `%APPDATA%\MicMap\micmap.log` — D-04's log sink for headless CLI modes.
- **`core::IConfigManager`** defensive reader / writer from Phase 2 — D-09's `shownTrayNotification` field slots in with zero new infra.

### Established Patterns

- **Factory + interface pattern** (`createOpenVRInput`, `createDriverClient`, `createStateMachine`, `createConfigManager`) — D-20's `createManifestRegistrar()` + `IManifestRegistrar` follows the same shape.
- **`unique_ptr` ownership, no raw `new`** — all new objects (registrar, retry-thread state) follow the existing convention.
- **No try-catch idiom** (CONVENTIONS.md) — OpenVR C API already returns error enums; no exceptions to handle. nlohmann/json calls go through `allow_exceptions=false` per Phase 2 precedent.
- **Logging separation**: app side uses `common::Logger` (`MICMAP_LOG_*`), driver side uses `DriverLog`. Phase 3 is app-side only; no `DriverLog` involvement.

### Integration Points

- **`WinMain` at `apps/micmap/main.cpp:562`** — the CLI fork (D-02) sits at the very top of `WinMain`, before `RegisterClassExW`. GUI code path otherwise unchanged.
- **`MicMapApp::initialize`** — new call: spawn the re-registration `std::async` task after `vrInput = createOpenVRInput()` is in place (D-15). Task is detached / fire-and-forget.
- **`MicMapApp::shutdown` (new)** — new method called from the main-loop exit path; performs the ordered teardown from D-12. Replaces the current implicit destructor-chain teardown.
- **`vr_input.cpp` `pollEvents()`** — inject `vr::VRSystem()->AcknowledgeQuit_Exiting()` at the top of the `VREvent_Quit` branch, before the callback dispatch (D-11).
- **`apps/micmap/CMakeLists.txt`** — add `configure_file(app.vrmanifest.in app.vrmanifest @ONLY)` into the build dir. Install rules add `app.vrmanifest` as a sibling of `micmap.exe`. Adds `manifest_registrar` to the steamvr library sources at `src/steamvr/CMakeLists.txt`.
- **Argv re-parse** — CLI flags are re-parsed inside `WinMain` via `CommandLineToArgvW(GetCommandLineW(), &argc)` once at entry. The existing `lpCmdLine` `strstr` check for `--minimized` (line 596) is removed and replaced with a read of the parsed flags struct.

</code_context>

<specifics>
## Specific Ideas

- The single most important UX decision is **D-09 (first-silent-launch tray balloon)**. User explicitly added this beyond the recommended option: they want users to discover the tray icon after a SteamVR-triggered auto-launch, rather than wondering where the app went. Persisted via a new `AppConfig.shownTrayNotification` flag so it fires exactly once per install.
- **D-11's "ack-first inside pollEvents, before callback dispatch"** is the load-bearing correctness decision for AUTO-05. It decouples Valve's 2-second watchdog from app-side teardown latency entirely. No watchdog thread is needed because of this ordering.
- **D-16's silent-retry discipline** — "no logging necessary unless the event has unexpected result" — is a user-explicit preference. Expected "SteamVR not running" = zero log output. Successful registration = one INFO line. Unexpected `VRInitError` = one WARNING line with the enum name. Keeps `%APPDATA%\MicMap\micmap.log` clean for operators diagnosing real issues.
- **D-19's `configure_file`** is driven by the need to keep `app.vrmanifest`'s `version` field in sync with the app binary's version without hand-editing. Phase 4 installer will consume whatever `configure_file` emits; no two-source-of-truth drift.

</specifics>

<deferred>
## Deferred Ideas

- **Win10+ `ToastNotificationManager` toast for the first-silent-launch notification** — rejected for Phase 3 (requires COM-activator + `AppUserModelID` registration, which is installer surface belonging to Phase 4 or a v1.x UX polish milestone). `Shell_NotifyIcon` balloon (D-10) is sufficient now.
- **Auto-start toggle checkbox in MicMap's own settings UI (UX-01)** — already deferred per PROJECT.md to v1.x. Phase 3 does not surface a UI for enabling/disabling the `SetApplicationAutoLaunch` state; that lives in SteamVR's "Manage Startup Overlay Apps" list (AUTO-01 success criterion #3).
- **Granular CLI exit codes (0/2/3/4)** — rejected for Phase 3 (D-03 locks 0/1). If Phase 4 installer grows a need for distinct failure dialogs, revisit then.
- **Event-driven SteamVR-running detection** (via `VR_IsRuntimeInstalled` + `openvrpaths.vrpath` mtime) — rejected in favor of D-16's simple 30s `VR_Init` poll. Re-visit if CPU profile ever becomes a concern.
- **`--smoketest` mode on `micmap.exe`** — optional extension for Phase 3 validation harness. Full SteamVR quit/launch cycle requires manual UAT regardless; any smoke-test mode is nice-to-have. If planner wants to include it, fine; otherwise deferred.
- **Windows Run-key / Startup folder fallback** — explicitly rejected milestone-wide per PROJECT.md. Not re-opened.

</deferred>

---

*Phase: 03-auto-start*
*Context gathered: 2026-04-23*

# Roadmap: MicMap — Seamless SteamVR Integration

## Overview

This roadmap delivers the "Seamless SteamVR Integration" milestone: rip out the legacy virtual-controller driver, replace it with a pure HMD-sidecar that injects `/input/system/click` on the HMD property container, fix the stubbed JSON config read-back path, enable SteamVR-native auto-launch via `app.vrmanifest`, ship a single-click Inno Setup installer, and update documentation to match shipped reality. The sidecar-on-HMD technique is already validated in `bey-closer-t1`; this milestone is focused porting + packaging, not exploration.

**Sequencing rationale (load-bearing — do not reorder):**
- **Driver first:** A broken driver combined with auto-start is a regression that is actively worse than no auto-start (users get a laser beam they didn't want AND the app launches itself). The installer also needs the final driver file layout before it can be written.
- **Config read-back in parallel with driver:** Fully independent, mechanical, vendored library — running it alongside the driver work avoids an idle schedule gap. Parallel-safe.
- **Auto-start after driver:** Auto-launching a broken driver makes no sense. Auto-start can be manually tested before the installer exists.
- **Installer after auto-start:** The app binary owns manifest registration (installer invokes `micmap.exe --register-vrmanifest` as a post-install step). The installer packages the outputs of Phases 1–3.
- **Docs always last:** Documentation describes shipped reality, not plans.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Driver Sidecar Migration** — Replace virtual-controller with pure HMD-sidecar injecting `/input/system/click` (amended by 01-06 after spike falsified the bare-sidecar assumption: bindings patcher now ships alongside, routes HMD system click to ToggleDashboard on Bigscreen Beyond / any lighthouse-non-Index HMD)
- [x] **Phase 2: Config Read-Back** — Wire up the stubbed JSON read path so user settings persist (parallel-safe with Phase 1) — CLOSED 2026-04-23, M-1 PASSED live
- [ ] **Phase 3: Auto-Start** — SteamVR-native auto-launch via `app.vrmanifest` with `VREvent_Quit` handling
- [ ] **Phase 4: Installer** — Single-click Inno Setup installer packaging driver + app + auto-start registration
- [ ] **Phase 5: Documentation** — README + architecture docs updated to shipped reality

## Phase Details

### Phase 1: Driver Sidecar Migration
**Goal**: Driver stops registering a virtual controller and instead creates its own `/input/system/click` boolean component on the HMD property container, so detection triggers toggle the SteamVR dashboard with no laser beam and no dashboard-state branching.
**Depends on**: Nothing (first phase)
**Requirements**: SVR-01, SVR-02, SVR-03, SVR-04, SVR-05, SVR-06, SVR-07, SVR-08, SVR-09, SVR-10, SVR-11
**Success Criteria** (what must be TRUE):
  1. Triggering via `hmd_button_test.exe` on a real HMD opens the SteamVR dashboard with no visible laser beam and no virtual controller appearing in Settings > Devices (prevents Pitfall 7, validates SVR-01/04/11).
  2. A second trigger after the HMD has slept and woken still works without restarting SteamVR — component handle survives HMD deactivation/reactivation (prevents Pitfall 1, validates SVR-02/04).
  3. The driver's first `RunFrame` emits an init log line visible in `%APPDATA%\openvr\logs\vrserver.txt`, and every OpenVR error is logged with its enum name (prevents Pitfall 11, validates SVR-10).
  4. HTTP `/trigger` requests are enqueued on the HTTP thread and drained in `RunFrame` — no OpenVR driver API is ever called from the HTTP thread; `RunFrame` stays under 1ms in dev-build timing asserts (prevents Pitfall 12, validates SVR-05/06).
  5. `grep -r` across the driver source for `VirtualController`, `TrackedDeviceAdded`, `dashboard_open`, `isDashboardOpen`, `ControllerDevice`, `open_vs_select` returns zero results; build is clean under `-Werror`/`/WX` (prevents Pitfall 7, validates SVR-04/07/08/09).
**Plans**: 6 plans
  - [x] 01-01-PLAN.md — CommandQueue + VRInputErrorName header-only primitives + unit test (Wave 1)
  - [x] 01-02-PLAN.md — State machine Releasing state + IDriverClient press/release collapse (Wave 1)
  - [x] 01-03-PLAN.md — Driver sidecar rewrite: DeviceProvider + HttpServer + CMake /WX + controller/launcher delete (Wave 2)
  - [x] 01-04-PLAN.md — App rewire: onTrigger(PressEdge), hmd_button_test buttons, dashboard_manager delete, forbidden-string sweep (Wave 2)
  - [x] 01-05-PLAN.md — D-02 manual-VR validation spike on real HMD — falsified the Plan 01-03 assumption, triggered 01-06 amendment (Wave 3)
  - [x] 01-06-SUMMARY.md — **AMENDMENT**: driver-side bindings_patcher.cpp writes PascalCase dashboard+lasermouse bindings into SteamVR's generic_hmd config; press/release collapsed to single tap. Validated on Bigscreen Beyond: dashboard opens on tap, head-locked cursor present, ToggleDashboard native toggle semantics. New INST-08 requirement for Phase 04.
**Research spike**: HMD reactivation lifecycle (Case D in ARCHITECTURE.md) is untested in bey-closer-t1 — budget a half-day validation spike before declaring phase exit. If `VREvent_TrackedDeviceDeactivated` is unreliable, fall back to re-checking `TrackedDeviceToPropertyContainer` each `RunFrame` tick.

### Phase 2: Config Read-Back
**Goal**: User settings written to `%APPDATA%/MicMap/config.json` actually persist across sessions — replace the read-path stub at `src/core/src/config_manager.cpp:142` with a defensive `nlohmann/json` parser that tolerates corruption.
**Depends on**: Nothing — fully independent of Phase 1; may run in parallel (parallel-safe).
**Requirements**: CFG-01, CFG-02, CFG-03, CFG-04, CFG-05
**Success Criteria** (what must be TRUE):
  1. User changes a setting (device, sensitivity, detection duration, SteamVR options), quits the app, restarts, and finds the setting preserved (validates CFG-01/05).
  2. A corrupted `config.json` (e.g. trailing comma, truncated JSON) causes the app to back the file up to `config.json.corrupted.YYYYMMDD-HHMMSS`, fall back to defaults, and log a visible warning — no crash, no data loss (prevents Pitfall 9, validates CFG-02).
  3. Out-of-range numeric fields (sensitivity, detection duration, sample rate) are clamped to valid ranges with a warning log rather than accepted as-is (prevents malformed-input state corruption, validates CFG-03).
  4. A config written by `saveDefault()` and immediately reloaded produces identical in-memory state — round-trip is identity across the full `AppConfig` struct (validates CFG-04).
**Plans**: 3 plans
  - [x] 02-01-PLAN.md — Wave 0 test scaffold: register `test_config_manager` (5 RED scenarios) + link `nlohmann_json` PRIVATE into `micmap_core`
  - [x] 02-02-PLAN.md — Implement defensive nlohmann/json parser, atomic Windows save (ReplaceFile/MoveFileEx), UTF-8 wstring boundary, clamp/pow2-snap, corruption backup-and-rotate — turn RED → GREEN
  - [x] 02-03-PLAN.md — Verification: warnings-clean full build + M-1 manual end-to-end cycle (live UI persist-across-restart) + VALIDATION sign-off *(CLOSED 2026-04-23: automated GREEN; M-1 PASSED live after commit `73681c5` resolved startup-hang + activation + title regressions; see 02-03-SUMMARY.md "M-1 Resolution")*

### Phase 3: Auto-Start
**Goal**: SteamVR launches `micmap.exe` automatically when SteamVR starts, MicMap exits cleanly when SteamVR exits, and registration is idempotent — no console window, no focus steal, no respawn loop.
**Depends on**: Phase 1 (auto-starting a broken driver is a regression, not a feature).
**Requirements**: AUTO-01, AUTO-02, AUTO-03, AUTO-04, AUTO-05, AUTO-06
**Success Criteria** (what must be TRUE):
  1. Full SteamVR restart cycle: launch SteamVR → MicMap auto-launches silently (no console, no foreground focus) → quit SteamVR → MicMap also exits cleanly within 2s → relaunch SteamVR → MicMap auto-launches again. No respawn loop (prevents Pitfall 2, validates AUTO-01/05/06).
  2. `SetApplicationAutoLaunch` is never called immediately after `AddApplicationManifest` — the registrar polls `IsApplicationInstalled("bigscreen.micmap")` for up to 2 seconds first, and registration is idempotent across restarts and SteamVR upgrades (prevents Pitfall 5 / OpenVR issue #1378, validates AUTO-02/04).
  3. MicMap appears in SteamVR's "Manage Startup Overlay Apps" list with `app_key = "bigscreen.micmap"` and `is_dashboard_overlay = true`, and toggling it there takes effect on next SteamVR launch (validates AUTO-01).
  4. `micmap.exe --register-vrmanifest` and `micmap.exe --unregister-vrmanifest` CLI modes are invocable headlessly (installer-ready); unregister fully removes the manifest registration (validates AUTO-02/03).
**Plans**: 7 plans
  - [ ] 03-01-PLAN.md — Wave 0 test scaffold: RED ctest hooks for all AUTO-0x + injection seams (IVRApplicationsSurface, IVRSystem seam, Shell_NotifyIcon fn-ptr, IConfigManager) so Plans 04–07 are unit-testable without booting SteamVR (Wave 0)
  - [ ] 03-02-PLAN.md — `app.vrmanifest.in` template + `configure_file(@ONLY)` + `Pathcch.lib` link + A2 empirical resolution (string-vs-array `arguments` form, live SteamVR observation, loser form deleted from template) (Wave 0)
  - [ ] 03-03-PLAN.md — `AppConfig::shownTrayNotification` top-level bool (default false) + Phase 2 defensive reader + writer round-trip; empirically closes Research Open Q #4 (Wave 0)
  - [ ] 03-04-PLAN.md — `manifest_registrar` module: `IManifestRegistrar` (registerApp / unregisterApp / ensureRegistered / getLastError) via `vr::VRApplications()` accessor; `AddApplicationManifest(path, bTemporary=false)` → 100ms×20 poll → `SetApplicationAutoLaunch`; absolute path via `GetModuleFileNameW` + `PathCchRemoveFileSpec` (D-21). Stub under `!MICMAP_HAS_OPENVR` (Wave 1)
  - [ ] 03-05-PLAN.md — Extract `processVREvent` to testable free function in `vr_input_events.{hpp,cpp}`; inject `vr::VRSystem()->AcknowledgeQuit_Exiting()` BEFORE `notifyEvent(Quit)` — load-bearing fix for AUTO-05 / OpenVR #1425 (Wave 1)
  - [ ] 03-06-PLAN.md — `main.cpp` CLI + silent-boot integration: `CommandLineToArgvW` parse (D-01), WinMain CLI fork early-exit (D-02, D-03, D-04), single-instance mutex `--minimized` skip (D-08), silent-mode `ShowWindow` skip (D-06), first-silent-launch tray balloon via `NIM_MODIFY + NIF_INFO + NIIF_RESPECT_QUIET_TIME` (D-09, D-10) (Wave 2)
  - [ ] 03-07-PLAN.md — Re-registration `std::thread` + `std::atomic<bool>` retry loop (NEVER `std::async` — Pitfall 6 / amended D-15) + `MicMapApp::shutdown()` ordered teardown per D-12 + exit-path convergence (D-14); SteamVR full-restart UAT (Wave 3)
**Research spike**: `SetApplicationAutoLaunch` persistence (OpenVR issue #1547) — run multiple SteamVR restart cycles during UAT to characterize frequency of the setting being forgotten. If it is meaningfully unreliable, file a v1.x "re-register" UI button as the mitigation.

### Phase 4: Installer
**Goal**: A single admin-elevated `MicMap-Setup-vX.Y.Z.exe` installs the driver, the app, and registers everything with SteamVR in one step — and its uninstaller reverses every one of those actions cleanly.
**Depends on**: Phase 1 (final driver layout), Phase 3 (the app binary must expose `--register-vrmanifest`), Phase 2 (config schema stable so installer doesn't touch config.json — it doesn't, but the schema needs to be settled for upgrades to work).
**Requirements**: INST-01, INST-02, INST-03, INST-04, INST-05, INST-06, INST-07
**Success Criteria** (what must be TRUE):
  1. Clean VM → run installer → SteamVR finds the driver → launch SteamVR → MicMap auto-launches → mic-cover triggers dashboard → uninstall → `vrpathreg show` no longer lists MicMap and `openvrpaths.vrpath` is clean (prevents Pitfalls 3, 10, 15; validates INST-01/03/05).
  2. Installer detects running `vrserver.exe` / `vrmonitor.exe` / `vrcompositor.exe` / `vrdashboard.exe` / `vrwebhelper.exe` via WMI and blocks with a clear message naming the specific process(es) found; DLL files use `restartreplace` as defense-in-depth (prevents Pitfall 4, validates INST-02).
  3. Upgrade-in-place from legacy 0.x (virtual-controller) version: no duplicate `vrpathreg` entries (removedriver-before-adddriver runs unconditionally), no ghost controller lingering in SteamVR Settings > Devices after upgrade (prevents Pitfalls 3, 8; validates INST-03/06).
  4. Installer invokes `micmap.exe --register-vrmanifest` as a `[Run]` step post-install and `micmap.exe --unregister-vrmanifest` as an `[UninstallRun]` step — symmetric install/uninstall without the installer itself linking OpenVR (validates INST-04/05).
  5. `cmake --build --target package` produces `MicMap-Setup-vX.Y.Z.exe` via ISCC.exe with correct `/D` defines — one build command yields one shippable installer (validates INST-07).
**Plans**: TBD
**Research spike**: Upgrade-from-0.x path (Pitfall 8, ghost controller bindings under `%LOCALAPPDATA%\openvr\input\`) requires a real test machine or VM with the legacy virtual-controller driver installed. Also budget half a day for Inno Setup 6 Pascal Script gotchas (Pitfall 16: `#N` preprocessor ambiguity, AnsiString/String casts for file I/O) even with the `BeyondProximity.iss` reference in hand.

### Phase 5: Documentation
**Goal**: README and architecture docs describe the shipped reality — crossed-out auto-start sections become current text, install instructions point at the single `.exe` installer, and the sidecar-on-HMD technique is written down so future maintainers don't re-discover the pitfalls.
**Depends on**: Phases 1, 2, 3, 4 (docs describe shipped behavior, not plans).
**Requirements**: DOC-01, DOC-02
**Success Criteria** (what must be TRUE):
  1. README's auto-start "Known Issues" / crossed-out sections are replaced with current instructions referencing the single `.exe` installer — no remaining references to `install_driver.bat` or virtual controller architecture (validates DOC-01).
  2. A new `docs/architecture.md` (or equivalent) documents the sidecar-on-HMD technique, the `CommandQueue` HTTP-thread → RunFrame boundary, and the HMD reactivation lifecycle (`VREvent_TrackedDeviceDeactivated` handling) — enough context that a future maintainer can diagnose Pitfalls 1, 2, and 12 without re-reading the research archive (validates DOC-02).
  3. Install instructions describe the one-click flow (download `.exe`, run as admin, SteamVR closed) and uninstall instructions match (Add/Remove Programs → MicMap → Uninstall, fully reversible) — both paths reflect actual installer behavior on a clean test machine (validates DOC-01).
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5. Phase 2 is parallel-safe with Phase 1 and may be executed concurrently (no dependency either direction).

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Driver Sidecar Migration | 3/5 | In progress | - |
| 2. Config Read-Back | 3/3 | Complete | 2026-04-23 |
| 3. Auto-Start | 0/7 | Planned | - |
| 4. Installer | 0/TBD | Not started | - |
| 5. Documentation | 0/TBD | Not started | - |

## Coverage

All 31 v1 requirements mapped to exactly one phase. No orphans, no duplicates.

| Phase | Requirements | Count |
|-------|--------------|-------|
| 1. Driver Sidecar Migration | SVR-01, SVR-02, SVR-03, SVR-04, SVR-05, SVR-06, SVR-07, SVR-08, SVR-09, SVR-10, SVR-11 | 11 |
| 2. Config Read-Back | CFG-01, CFG-02, CFG-03, CFG-04, CFG-05 | 5 |
| 3. Auto-Start | AUTO-01, AUTO-02, AUTO-03, AUTO-04, AUTO-05, AUTO-06 | 6 |
| 4. Installer | INST-01, INST-02, INST-03, INST-04, INST-05, INST-06, INST-07 | 7 |
| 5. Documentation | DOC-01, DOC-02 | 2 |
| **Total** | | **31 / 31** |

---
*Roadmap created: 2026-04-22*
*Milestone: Seamless SteamVR Integration*
*Granularity: standard*

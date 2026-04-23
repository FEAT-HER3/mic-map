# Project Research Summary

**Project:** MicMap — Seamless SteamVR Integration milestone
**Domain:** Brownfield SteamVR sidecar driver migration + auto-start + installer (Windows)
**Researched:** 2026-04-22
**Confidence:** HIGH

## Executive Summary

MicMap is a Windows SteamVR addon that translates mic-cover gestures into SteamVR system-button presses. The "Seamless SteamVR Integration" milestone is a focused brownfield architecture migration with four concrete outcomes: replace the virtual-controller driver with a pure sidecar that injects `/input/system/click` on the HMD property container (SVR-01 through SVR-04), wire up the stubbed JSON config read-path (CFG-01), enable SteamVR-native auto-launch via `app.vrmanifest` (AUTO-01), and ship a single-click Inno Setup installer (INST-01/02). The technique is already validated in a sibling project (`bey-closer-t1`); this milestone is an exercise in porting and packaging, not exploration.

The recommended approach is sequencing-sensitive. The driver sidecar must land first — auto-starting a broken driver is actively worse than no auto-start, and the installer cannot target the final file layout until the driver is done. Config read-back is independent and can proceed in parallel. Auto-start depends on the driver being clean. The installer packages everything last. All four researchers independently converged on this exact phase order: driver → config (parallel) → auto-start → installer → docs.

The dominant risks live in two lifecycle edge cases and two install-time correctness issues: (1) the HMD component handle going stale after HMD sleep/wake (Pitfall 1), which the `bey-closer-t1` one-shot latch does not handle and which must be extended to subscribe to `VREvent_TrackedDeviceDeactivated`; (2) MicMap failing to exit when SteamVR quits, causing a respawn loop (Pitfall 2), requiring `VREvent_Quit` pumping on the app side; (3) `vrpathreg adddriver` creating duplicate driver entries on reinstall (Pitfall 3), preventable by running `removedriver` first; and (4) `SetApplicationAutoLaunch` returning `VRApplicationError_UnknownApplication` if called before `AddApplicationManifest` propagates (Pitfall 5 / OpenVR issue #1378), requiring an `IsApplicationInstalled` poll before the call.

## Key Findings

### Recommended Stack

The base stack is locked. This milestone introduces no new vendored dependencies. The only version-level change is bumping the OpenVR SDK reference from v2.5.1 (bey-closer-t1 validation baseline) to **v2.15.6** (current stable, 2026-03-27) — all three interface versions MicMap uses (`IVRDriverInput_004`, `IVRProperties_001`, `IVRApplications_008`) have identical signatures across both versions. Inno Setup **6.7.1** (2026-02-17) is the installer toolchain. nlohmann/json stays pinned at **3.11.2** — the CFG-01 read-path patterns are stable across 3.11.x and no bump is warranted.

**Core technologies (new surface area only):**
- **OpenVR SDK 2.15.6** — `IVRDriverInput_004::CreateBooleanComponent` + `UpdateBooleanComponent` (driver side); `IVRApplications_008::AddApplicationManifest` + `SetApplicationAutoLaunch` (app side) — same interfaces as validated 2.5.1 baseline
- **Inno Setup 6.7.1** — single-click Windows installer; patterned on `BeyondProximity.iss` with MicMap-specific adjustments (`Uninstallable=yes`, expanded process-kill list, no nested-driver Pascal procedures)
- **nlohmann/json 3.11.2 (pinned, no bump)** — JSON read-back; already vendored; use `.value(key, default)` + `try/catch(json::parse_error)`
- **`app.vrmanifest` (new file)** — `"is_dashboard_overlay": true`, `"app_key": "bigscreen.micmap"`, `"launch_type": "binary"`, `"binary_path_windows": "micmap.exe"` relative to manifest dir

**Cross-cutting decisions the roadmapper must carry forward:**
- `app_key` = `"bigscreen.micmap"` — stable across all versions; changing it orphans auto-launch preference
- App owns manifest registration via `--register-vrmanifest` / `--unregister-vrmanifest` CLI modes — not the installer; symmetric uninstall teardown without a separate helper binary
- `AddApplicationManifest(path, /*bTemporary=*/false)` — temporary manifests cannot be auto-launched
- `Uninstallable=yes` in the Inno Setup script — MicMap owns its driver directory (unlike bey-closer-t1)
- No Windows Run-key / Startup folder fallback — ever; SteamVR lifecycle is the requirement

### Expected Features

All features for this milestone are P1 table-stakes. No differentiators are in scope. The competitor baseline (OVR Advanced Settings, OVR Toolkit, XSOverlay) sets the "well-behaved SteamVR overlay tool" mental model — MicMap's auto-start, installer, and settings-persistence must match that unremarkably. MicMap's novel surface is the HMD-sidecar input injection.

**Must have (milestone table stakes):**
- No laser beam on trigger (visual validation on Valve Index + Beyond required)
- Single trigger code path regardless of dashboard open/closed state
- Graceful cold-start resilience (no silent driver disable on race)
- HMD sleep/wake resilience (reactivation lifecycle beyond one-shot latch)
- Auto-start on by default; appears in SteamVR Startup Overlay Apps list
- Silent auto-start (no console window, no focus steal)
- MicMap exits when SteamVR exits (`VREvent_Quit` + `AcknowledgeQuit_Exiting`)
- Single-click installer: admin-elevated, vrserver-running gate, upgrade-in-place via stable AppId GUID, real uninstaller that cleans vrpathreg and manifest
- Config read-back working; corrupt config falls back to defaults without crash
- README updated (crossed-out auto-start sections current; .exe installer replaces .bat)

**Defer to v1.x:** Auto-start toggle checkbox in MicMap UI (P2); installer finished-page "Launch SteamVR" (P2); silent-install CLI flags documented (free from Inno, just needs docs)

**Defer to v2+:** Per-mic training profiles, multiple named presets, in-VR settings overlay

**Anti-features to exclude:** Windows Run-key auto-start, virtual-controller fallback, installer writing to `config.json`

### Architecture Approach

The target architecture is a pure sidecar driver: `driver_micmap.dll` never calls `TrackedDeviceAdded`, creates its own `/input/system/click` boolean component on the HMD property container once that container becomes valid (deferred to RunFrame via polling), and updates that component in response to HTTP triggers enqueued by a `CommandQueue`. Three files are deleted entirely (`virtual_controller.{hpp,cpp}`, `process_launcher.{hpp,cpp}`, `micmap_controller_profile.json`). One new driver-internal primitive is added (`command_queue.hpp`). One new app-side subsystem is added (`manifest_registrar`). One new file ships with the app (`app.vrmanifest`).

**Components and milestone-level changes:**
1. **`device_provider.{hpp,cpp}`** — gains HMD-activation state machine (NotReady → Ready → Invalidated); removes `TrackedDeviceAdded`; absorbs scheduled-release queue from deleted VirtualController
2. **`command_queue.hpp` (new)** — mutex + `std::deque<ClickRequest>`, depth-8 bounded; HTTP thread pushes, RunFrame drains; do not call OpenVR driver APIs from HTTP thread
3. **`virtual_controller.{hpp,cpp}` (deleted)** — ~380 LOC; no feature flag
4. **`process_launcher.{hpp,cpp}` (deleted)** — replaced by `app.vrmanifest` auto-launch
5. **`manifest_registrar.cpp` (new)** — `AddApplicationManifest` → poll `IsApplicationInstalled` → `SetApplicationAutoLaunch(true)`; idempotent; runs on app startup
6. **`app.vrmanifest` (new)** — lives next to `micmap.exe`; `binary_path_windows` is a relative filename
7. **`installer/MicMap.iss` (new)** — admin-elevated; kills vrserver + vrmonitor + vrcompositor + vrdashboard + vrwebhelper; `removedriver` then `adddriver`; `Uninstallable=yes`; `[UninstallRun]` calls `removedriver` and `--unregister-vrmanifest`
8. **`config_manager.cpp`** — replace stub at line 142 with `try { in >> j; } catch(json::parse_error&)`; per-field `.value(key, default)`; bounds validation; corrupt-file backup + defaults fallback
9. **`dashboard_manager.cpp`** — deleted or collapsed; "open vs. select" branch logic gone

**Critical constraint:** `UpdateBooleanComponent` must be called from the RunFrame thread. `CommandQueue` is the mandatory handoff — calling driver APIs from the HTTP thread races with RunFrame and corrupts SteamVR internal state.

### Critical Pitfalls

1. **HMD component handle stale after sleep/wake** — extend `bey-closer-t1`'s one-shot latch to a full state machine. Subscribe to `VREvent_TrackedDeviceDeactivated` for device index 0 via `IVRServerDriverHost::PollNextEvent` in RunFrame. On deactivate: reset `m_hSystemClick = k_ulInvalidInputComponentHandle`, clear `m_hmdComponentAttempted`. Next RunFrame re-creates. Validate manually: put headset down 30s, pick up, trigger must work.

2. **Auto-launch shutdown loop** — `VREvent_Quit` handling is mandatory before shipping AUTO-01. Pump `IVRSystem::PollNextEvent` in the app main loop. On `VREvent_Quit`: call `AcknowledgeQuit_Exiting()`, tear down cleanly. Validate full quit-and-reopen cycle.

3. **`SetApplicationAutoLaunch` race** — `AddApplicationManifest` → poll `IsApplicationInstalled(appKey)` up to 2s → then `SetApplicationAutoLaunch`. Never call `SetApplicationAutoLaunch` immediately after `AddApplicationManifest` (OpenVR issue #1378).

4. **`vrpathreg adddriver` double-registration** — run `vrpathreg removedriver <path>` unconditionally before every `adddriver` in the installer `[Run]` section. `removedriver` on an unregistered path is a no-op.

5. **Virtual-controller removal leaves dangling callsites that compile but crash** — create a removal checklist before touching code. Grep targets: `TrackedDeviceAdded`, `VirtualController`, `dashboard_open`, `open_vs_select`, `isDashboardOpen`, `ControllerDevice`, `ITrackedDeviceServerDriver`. Compile with `-Werror`/`/WX`. Validate first trigger under debugger.

## Implications for Roadmap

### Phase 1: Driver Sidecar Rewrite (SVR-01 through SVR-04)

**Rationale:** Must land first. Auto-starting a broken driver is actively worse than no auto-start. The installer cannot target final file layout until driver shape is settled. `hmd_button_test` enables isolated validation with no audio, no UI, no installer.

**Delivers:** Driver creates `/input/system/click` on HMD property container (deferred to RunFrame); routes HTTP triggers through CommandQueue; processes scheduled releases internally; zero virtual-controller code in codebase.

**Features addressed:** SVR-01, SVR-02, SVR-03, SVR-04

**Pitfalls to prevent:** Pitfall 1 (HMD reactivation — extend one-shot latch to full state machine with deactivation event), Pitfall 6 (driver manifest correct from day one), Pitfall 7 (removal checklist before touching code), Pitfall 11 (wire DriverLog, emit init line first), Pitfall 12 (RunFrame non-blocking — dev-build timing assert)

**Exit criterion:** End-to-end trigger via `hmd_button_test`. Dashboard opens. No laser beam. Second trigger after HMD sleep/wake also works.

**Research flag:** HMD reactivation behavior (Case D) is untested in bey-closer-t1. Budget a half-day validation spike before declaring phase exit.

### Phase 2: Config Read-Back (CFG-01) — parallel with Phase 1

**Rationale:** Fully independent. Small. Can be a separate PR running alongside the driver work.

**Delivers:** `config_manager.cpp` read path with try/catch, `.value(key, default)`, bounds validation, corrupt-file backup + defaults fallback. Write side migrated to nlohmann/json for round-trip conformance.

**Features addressed:** CFG-01

**Pitfalls to prevent:** Pitfall 9 (JSON parse crash on legacy format — try/catch + backup in same PR; unit test with legacy fixture and corrupt-file fixture)

**Exit criterion:** Settings persist across restart. Corrupted `config.json` produces defaults + backup, not crash.

**Research flag:** None — mechanical work, vendored library, documented patterns.

### Phase 3: Auto-Launch (AUTO-01)

**Rationale:** Depends on Phase 1 (driver must be clean). Does not depend on Phase 4 or Phase 2. Can be tested manually before the installer exists.

**Delivers:** `app.vrmanifest`; `manifest_registrar` (AddApplicationManifest → poll IsApplicationInstalled → SetApplicationAutoLaunch, idempotent); `VREvent_Quit` handler with `AcknowledgeQuit_Exiting`; `--register-vrmanifest` / `--unregister-vrmanifest` CLI modes.

**Features addressed:** AUTO-01

**Pitfalls to prevent:** Pitfall 2 (shutdown loop — VREvent_Quit is mandatory), Pitfall 5 (manifest paths — use absolute paths at registration time), Pitfall 3's manifest analog (IsApplicationInstalled check before re-registering)

**Exit criterion:** Run micmap.exe once → quit → restart SteamVR → micmap.exe auto-launches silently → quit SteamVR → micmap.exe also quits → relaunch SteamVR → micmap.exe auto-launches again. MicMap appears in Startup Overlay Apps list.

**Research flag:** `SetApplicationAutoLaunch` persistence bug (issue #1547) needs multiple SteamVR restart cycles in UAT. Budget time.

### Phase 4: Installer (INST-01, INST-02)

**Rationale:** Packages output of Phases 1-3. Must be last among functional phases — needs final driver layout, final app binary with `--register-vrmanifest` mode, and final `app.vrmanifest`.

**Delivers:** `installer/MicMap.iss` producing a single admin-elevated `.exe`. Expands process-kill list. Runs removedriver then adddriver. Runs `micmap.exe --register-vrmanifest`. Stable AppId GUID. Real uninstaller (`vrpathreg removedriver` + `--unregister-vrmanifest`). `Uninstallable=yes`.

**Features addressed:** INST-01, INST-02

**Pitfalls to prevent:** Pitfall 3 (removedriver before adddriver), Pitfall 4 (expanded process list + `restartreplace` on DLL), Pitfall 8 (upgrade-path cleanup — test on machine with old driver), Pitfall 10 (FileExists guard on vrpathreg.exe for defensive uninstall), Pitfall 15 (correct driver directory structure; vrpathreg adddriver targets `{app}` not `{app}\bin\win64`), Pitfall 16 (Pascal Script gotchas — copy IsProcessRunning verbatim; avoid #N at line-start; AnsiString cast for file I/O)

**Exit criterion:** Clean VM → install → SteamVR finds driver → micmap auto-launches → mic-cover triggers dashboard → uninstall reverses cleanly. Upgrade from 0.x: ghost controller gone, no duplicate vrpathreg entries.

**Research flag:** Pitfall 8 (ghost controller bindings) needs real test machine with old driver. Pitfall 16 warrants half-day buffer even with the BeyondProximity.iss reference.

### Phase 5: Documentation (DOC-01)

**Rationale:** Docs describe shipped reality. Always last.

**Delivers:** README updated — auto-start Known Issues resolved, install instructions reference `.exe` installer, new architecture described.

**Research flag:** None.

### Phase Ordering Rationale

- Driver first: broken driver + auto-start = regression; installer needs final layout. No exception.
- Config parallel with driver: fully independent, small; avoids schedule gap between driver and auto-start.
- Auto-start before installer: manifest registration logic is in the app binary; installer invokes that binary; installer cannot call AddApplicationManifest itself (does not link OpenVR).
- Installer last among functional work: packages outputs of all prior phases.
- Docs always last: they describe reality, not plans.

### Research Flags

**Needs deeper research / validation spikes:**
- **Phase 1 (HMD reactivation):** `VREvent_TrackedDeviceDeactivated` + re-create path not validated in bey-closer-t1. Spike required before phase exit.
- **Phase 3 (auto-start):** `SetApplicationAutoLaunch` persistence bug (issue #1547) needs multiple-restart UAT cycles.
- **Phase 4 (installer upgrade path):** Pitfall 8 (ghost controller bindings) requires a test machine with the old virtual-controller driver installed.

**Standard patterns (skip research-phase):**
- **Phase 2 (config read-back):** Mechanical. Vendored library. Documented patterns.
- **Phase 5 (documentation):** Writing.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All interface signatures verified against OpenVR master headers 2026-04; version constants confirmed; Inno Setup 6.7.1 release confirmed; nlohmann/json 3.11.2 patterns stable |
| Features | MEDIUM-HIGH | Table-stakes anchored in concrete prior art and existing codebase; "works after HMD sleep/wake" is inferred user expectation not backed by user study |
| Architecture | HIGH | Sidecar pattern validated end-to-end in bey-closer-t1 on SteamVR March 2026 + OpenVR SDK v2.5.1; cross-checked against current codebase |
| Pitfalls | HIGH | Critical pitfalls cross-verified with bey-closer-t1 prior art, OpenVR wiki, and SteamVR issue tracker; Pitfall 1 (reactivation) flagged as unvalidated with mandatory spike |

**Overall confidence:** HIGH

### Gaps to Address

- **HMD reactivation (Case D):** Whether component handles survive HMD sleep/wake is underspecified in OpenVR docs and untested on target hardware (Valve Index, Bigscreen Beyond). Validate in Phase 1 before phase exit. If `VREvent_TrackedDeviceDeactivated` is unreliable, fall back to re-checking `TrackedDeviceToPropertyContainer` on every RunFrame tick. Do not speculatively ship re-creation logic — repeated CreateBooleanComponent calls may leak handles (unverified).

- **`SetApplicationAutoLaunch` persistence:** Issue #1547 documents occasional setting loss across SteamVR restarts. Phase 3 UAT should characterize frequency. A P2 "Re-register" button in the UI is the mitigation for v1.x.

- **Non-default Steam install path:** Installer hardcodes `{autopf}\Steam\...`. Users with Steam on a non-default drive silently skip driver registration. The `FileExists` guard avoids a crash but produces a silent no-op. Full fix (registry query for `HKCU\Software\Valve\Steam\SteamPath`) is out of scope but should be filed as a follow-up issue.

## Sources

### Primary (HIGH confidence)
- `bey-closer-t1/HMD Button Stub.md` — sidecar-on-HMD technique, error codes, timing; validated SteamVR March 2026 + OpenVR SDK v2.5.1
- `bey-closer-t1/installer/BeyondProximity.iss` — Inno Setup reference (admin, WMI process check, vrpathreg, post-install launch)
- `bey-closer-t1/.planning/RETROSPECTIVE.md` — Inno Setup 6 Pascal Script gotchas
- OpenVR SDK `headers/openvr.h` @ master 2026-04 — `IVRApplications_008` signatures
- OpenVR SDK `headers/openvr_driver.h` @ master 2026-04 — `IVRDriverInput_004`, `IVRProperties_001` signatures
- OpenVR SDK v2.15.6 release notes — released 2026-03-27
- Inno Setup 6.7.1 — jrsoftware.org, released 2026-02-17
- OpenVR wiki: Local Driver Registration — vrpathreg contract
- OpenVR Driver API Documentation — RunFrame semantics, TrackedDeviceToPropertyContainer usage

### Secondary (MEDIUM confidence)
- OpenVR issue #1378 — `VRApplicationError_UnknownApplication` from `SetApplicationAutoLaunch` called too early
- OpenVR issue #1425 — SteamVR respawn loop when registered overlay app stays alive after quit
- OpenVR issue #1547 — auto-launch setting occasionally forgotten across SteamVR restarts
- OpenVR issue #1653 — vrpathreg allows duplicate path registration
- dreiekk/OpenVR-Autostarter — `VREvent_Quit` lifecycle pattern
- OpenVR Advanced Settings — behavioral baseline for "well-behaved SteamVR overlay utility"

### Tertiary (LOW confidence)
- Steam Community SteamVR Troubleshooting forums — ghost driver and ghost controller binding reports
- SteamVR developer community thread on non-Steam app auto-start — overlay + manifest + SetApplicationAutoLaunch confirmed as only supported path

---
*Research completed: 2026-04-22*
*Ready for roadmap: yes*

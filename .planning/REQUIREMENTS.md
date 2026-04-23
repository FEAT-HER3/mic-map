# Requirements: MicMap — Seamless SteamVR Integration

**Defined:** 2026-04-22
**Core Value:** Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.

## v1 Requirements

Requirements for the "Seamless SteamVR Integration" milestone. Each maps to one roadmap phase.

### Driver Sidecar Migration (SVR)

Rip out the virtual-controller driver; replace with a pure sidecar that injects `/input/system/click` on the HMD property container.

- [ ] **SVR-01**: Driver starts with zero registered devices — no `TrackedDeviceAdded` call anywhere in `device_provider`
- [ ] **SVR-02**: Driver defers HMD-container component creation until `IVRProperties::TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` returns a valid container — polled each `RunFrame`, not at `Init`
- [ ] **SVR-03**: Once the HMD container is available, driver creates its own `/input/system/click` boolean component via `IVRDriverInput_004::CreateBooleanComponent` on the HMD container and retains the returned handle
- [ ] **SVR-04**: Driver subscribes to `VREvent_TrackedDeviceDeactivated` for device index 0 and invalidates its cached HMD-side input handle; next `RunFrame` re-creates the component against the re-activated HMD container (HMD sleep/wake resilience)
- [ ] **SVR-05**: Driver exposes a thread-safe `CommandQueue` (mutex-guarded bounded deque, depth 8, drop-oldest policy) — HTTP server thread pushes click requests; `RunFrame` drains. No OpenVR driver API is ever called from the HTTP thread.
- [ ] **SVR-06**: Scheduled button-release timing (currently in `virtual_controller`) moves into the driver's `RunFrame` loop so that `UpdateBooleanComponent(true)` → hold → `UpdateBooleanComponent(false)` is serviced without external scheduling
- [ ] **SVR-07**: `src/steamvr/virtual_controller.{hpp,cpp}`, `src/steamvr/process_launcher.{hpp,cpp}`, and `micmap_controller_profile.json` are deleted — no feature flag, no dead branches, `-Werror`/`/WX` clean
- [ ] **SVR-08**: Trigger path is single code path — `dashboard_manager` dashboard-state polling and the "open vs. select" branching are removed; every detection trigger issues the same `/input/system/click` press
- [ ] **SVR-09**: App-side `driver_client` collapses to a single "click" endpoint matching the simplified driver surface
- [ ] **SVR-10**: Driver logs via `DriverLog` — the first `RunFrame` emits an init line (driver version, build timestamp) so misconfiguration is visible in `%APPDATA%\openvr\logs\vrserver.txt`
- [ ] **SVR-11**: End-to-end validation: `hmd_button_test.exe` triggers dashboard open on real HMD with no visible laser beam, and a second trigger after HMD sleep/wake continues to work

### Config Persistence (CFG)

Wire up the JSON config read-path that's currently stubbed; make settings actually persist across sessions.

- [ ] **CFG-01**: `ConfigManager::loadDefault()` parses `%APPDATA%/MicMap/config.json` via nlohmann/json on startup — replaces the stub at `src/core/src/config_manager.cpp:142`
- [ ] **CFG-02**: Malformed or corrupted config triggers a `json::parse_error` catch, backs the file up to `config.json.corrupted.YYYYMMDD-HHMMSS`, and falls back to defaults — no crash, no data loss, visible log line
- [ ] **CFG-03**: Each field is read with `.value(key, default)` or equivalent guarded accessor and then bounds-validated (detection duration, sensitivity, sample rate) with clamp-to-range + warning log on out-of-range values
- [ ] **CFG-04**: Write/read round-trip is identity — a config written by `saveDefault()` and immediately reloaded produces identical in-memory state (write path migrates to nlohmann/json if necessary for parity)
- [ ] **CFG-05**: User settings that persist: selected audio device, detection duration (ms), sensitivity, SteamVR options. Training data path unchanged.

### Auto-start (AUTO)

SteamVR-native auto-launch via `app.vrmanifest`. No Windows Run-key, no startup folder.

- [ ] **AUTO-01**: Ship `app.vrmanifest` alongside `micmap.exe` — `app_key` = `"bigscreen.micmap"`, `is_dashboard_overlay` = `true`, `launch_type` = `"binary"`, `binary_path_windows` = relative filename `"micmap.exe"`
- [ ] **AUTO-02**: App supports `--register-vrmanifest` CLI mode — calls `IVRApplications::AddApplicationManifest(absolutePath, /*bTemporary=*/false)`, polls `IsApplicationInstalled(app_key)` up to 2 seconds, then `SetApplicationAutoLaunch(app_key, true)` — guards against OpenVR issue #1378 race
- [ ] **AUTO-03**: App supports `--unregister-vrmanifest` CLI mode — symmetric teardown invoked by the installer uninstaller
- [ ] **AUTO-04**: On normal startup, app runs idempotent re-registration so manifest state is self-healing across SteamVR upgrades / user-initiated removal
- [ ] **AUTO-05**: App pumps `IVRSystem::PollNextEvent` in its main loop — on `VREvent_Quit`: call `AcknowledgeQuit_Exiting()`, tear down subsystems, exit. Prevents the OpenVR #1425 respawn loop.
- [ ] **AUTO-06**: Auto-launched `micmap.exe` opens silently — no console window allocation, no foreground focus, tray-icon-only behavior on background startup

### Installer (INST)

Single-click Inno Setup installer replacing the batch scripts. MicMap owns its own driver directory.

- [ ] **INST-01**: `installer/MicMap.iss` produces a single admin-elevated Setup `.exe` via Inno Setup 6.7.1 with a stable `AppId` GUID that supports upgrade-in-place across versions
- [ ] **INST-02**: Installer detects running `vrserver.exe` / `vrmonitor.exe` / `vrcompositor.exe` / `vrdashboard.exe` / `vrwebhelper.exe` via WMI and prompts the user to close SteamVR before proceeding; DLLs use `restartreplace` as defense-in-depth
- [ ] **INST-03**: Installer runs `vrpathreg.exe removedriver <driver-dir>` unconditionally before `vrpathreg.exe adddriver <driver-dir>` to prevent duplicate entries (OpenVR issue #1653); `vrpathreg.exe` presence is gated by `FileExists` so missing SteamVR is a clean skip
- [ ] **INST-04**: Installer runs `micmap.exe --register-vrmanifest` as a post-install `[Run]` step to register the vrmanifest and enable auto-launch
- [ ] **INST-05**: Uninstaller `[UninstallRun]` invokes `micmap.exe --unregister-vrmanifest` and `vrpathreg.exe removedriver <driver-dir>` for symmetric teardown; `Uninstallable=yes` in the script (unlike bey-closer-t1's nested install)
- [ ] **INST-06**: Upgrade from legacy 0.x (virtual-controller) version cleans up stale controller bindings under `%LOCALAPPDATA%\openvr\input\` so no ghost controller haunts SteamVR after upgrade
- [ ] **INST-07**: CMake `package` target invokes ISCC.exe with the correct `/D` defines so `cmake --build --target package` produces `MicMap-Setup-vX.Y.Z.exe`

### Documentation (DOC)

- [ ] **DOC-01**: README is updated to match shipped reality — crossed-out auto-start sections become current text; install instructions reference the single `.exe` installer rather than batch scripts; architecture section describes the sidecar HMD-button approach
- [ ] **DOC-02**: A new `docs/architecture.md` (or equivalent) documents the sidecar-on-HMD technique, the CommandQueue thread boundary, and the HMD reactivation lifecycle — so future maintainers don't re-discover the pitfalls

## v2 Requirements

Acknowledged but not in this milestone.

### Settings UX
- **UX-01**: Auto-start toggle checkbox in the MicMap UI (user-facing re-register / disable without uninstalling)
- **UX-02**: In-VR settings overlay (re-enables and implements the overlay stubs currently in `dashboard_manager.cpp`)

### Distribution
- **DIST-01**: Finished-page "Launch SteamVR" checkbox in the installer
- **DIST-02**: Silent-install CLI flags documented for enterprise/scripted deployment
- **DIST-03**: Non-default Steam install path support — lookup via `HKCU\Software\Valve\Steam\SteamPath` instead of hard-coded `{autopf}\Steam\...`

### Detection
- **DET-01**: Loud-environment detection accuracy improvements (secondary classifier, noise-floor adaptation)
- **DET-02**: Multiple named detection presets / per-environment profiles

## Out of Scope

Explicitly excluded this milestone. Anti-features and deferred scope.

| Feature | Reason |
|---------|--------|
| Virtual-controller fallback alongside the sidecar | User decision: worse UX with no benefit. Rip out entirely, no feature flag. |
| Windows Run-key / Startup folder auto-start | Creates a competing source of truth against SteamVR. SteamVR lifecycle is the contract. |
| Installer writing to `config.json` | Config file is user-mode, owned by `micmap.exe`. Double-owner = drift. |
| SteamVR overlay UI (settings in VR) | Four stubbed overlay functions in `dashboard_manager.cpp`. Tracked as tech debt; deferred to a future overlay milestone. |
| Non-Windows platforms (Linux/macOS audio) | MicMap is Windows-only by design of the SteamVR driver story; stubs remain. |
| Loud-environment detection accuracy | Orthogonal to architecture migration; user-stated out-of-scope. Retrain-in-environment stays the workaround. |
| New detection features beyond mic-cover pattern | Architecture-only milestone. |
| In-dashboard "select" as a distinct action | `/input/system/click` natively handles open + in-dashboard select; the old split was a virtual-controller artifact. |

## Traceability

Populated during roadmap creation. Each requirement maps to exactly one phase.

| Requirement | Phase | Status |
|-------------|-------|--------|
| SVR-01 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-02 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-03 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-04 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-05 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-06 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-07 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-08 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-09 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-10 | Phase 1 — Driver Sidecar Migration | Pending |
| SVR-11 | Phase 1 — Driver Sidecar Migration | Pending |
| CFG-01 | Phase 2 — Config Read-Back | Pending |
| CFG-02 | Phase 2 — Config Read-Back | Pending |
| CFG-03 | Phase 2 — Config Read-Back | Pending |
| CFG-04 | Phase 2 — Config Read-Back | Pending |
| CFG-05 | Phase 2 — Config Read-Back | Pending |
| AUTO-01 | Phase 3 — Auto-Start | Pending |
| AUTO-02 | Phase 3 — Auto-Start | Pending |
| AUTO-03 | Phase 3 — Auto-Start | Pending |
| AUTO-04 | Phase 3 — Auto-Start | Pending |
| AUTO-05 | Phase 3 — Auto-Start | Pending |
| AUTO-06 | Phase 3 — Auto-Start | Pending |
| INST-01 | Phase 4 — Installer | Pending |
| INST-02 | Phase 4 — Installer | Pending |
| INST-03 | Phase 4 — Installer | Pending |
| INST-04 | Phase 4 — Installer | Pending |
| INST-05 | Phase 4 — Installer | Pending |
| INST-06 | Phase 4 — Installer | Pending |
| INST-07 | Phase 4 — Installer | Pending |
| DOC-01 | Phase 5 — Documentation | Pending |
| DOC-02 | Phase 5 — Documentation | Pending |

**Coverage:**
- v1 requirements: 31 total
- Mapped to phases: 31 (100% coverage)
- Unmapped: 0

---
*Requirements defined: 2026-04-22*
*Last updated: 2026-04-22 after roadmap creation (phase mapping populated)*

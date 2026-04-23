# Feature Research

**Domain:** SteamVR hands-free input addon + auto-start integration + single-click driver installer (Windows)
**Researched:** 2026-04-22
**Confidence:** MEDIUM-HIGH (anchored in concrete prior art — bey-closer-t1's BeyondProximity.iss, the existing MicMap codebase, and documented SteamVR/OpenVR behavior; LOW on deeper UX survey because no large-n user study for MicMap exists)

This research is scoped to the "Seamless SteamVR Integration" milestone. It does **not** revisit the core value (cover-mic → dashboard trigger, already validated). It answers: what do users expect from (a) an HMD-sidecar input injector, (b) VR app auto-start, (c) a driver installer, (d) settings persistence — and what should this milestone explicitly *not* do.

## Feature Landscape

### Table Stakes (Users Expect These)

Missing any of these would feel like a regression versus what's already shipped, or versus SteamVR tools users already run alongside MicMap (OVR Advanced Settings, OVR Toolkit, XSOverlay, OpenVR-Autostarter).

#### (a) HMD-sidecar input injection UX

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **No visible laser beam on trigger** | The entire motivation for migrating off the virtual controller. Users already noted the beam as a known issue (README "Known Issues" #1). Keeping even a flash of it = regression. | LOW (falls out of the architecture change — if SVR-04 fully removes `TrackedDeviceAdded`, no laser can exist) | Validate visually in-HMD on Valve Index + Beyond at minimum. A regression here breaks the only value prop of the milestone. |
| **No input focus loss during trigger** | `/input/system/click` on the HMD container is *owned by SteamVR system layer*, not by any game's bound controller. User should never see their game lose focus, pause, or switch hands. | LOW | Native behavior — but verify the active game does not receive a phantom controller input. Games that hook all controller events (VRChat, Beat Saber) are the canary. |
| **Works identically whether dashboard is open or closed** | Old driver branched on dashboard-open state (open-vs-select). Users noticed and got confused when it behaved differently. PROJECT.md explicitly calls this out as "eliminated." | LOW | Single code path on driver side. User-visible requirement: one trained gesture → one unambiguous behavior, always. |
| **Trigger works from first moment SteamVR is up** | If the user trains and triggers within the first 5 seconds of SteamVR startup, it must either work or silently do nothing — NOT crash the driver or cause vrserver to disable it. Known SteamVR behavior: plugins that crash on startup get auto-disabled ([SteamVR/Error Codes](https://developer.valvesoftware.com/wiki/SteamVR/Error_Codes)). | MEDIUM | This is exactly what SVR-02 addresses (defer component creation until HMD property container handle is valid). Without it, cold-start race causes silent disable. Pain: user has to manually re-enable driver after every SteamVR restart. |
| **Works after HMD sleep/wake** | Users routinely put HMD down between sessions. On wake, the HMD property container handle may have changed. Trigger must still work without app restart. | MEDIUM | Requires the driver to re-acquire the HMD component handle on wake events, not just at driver start. Directly tied to SVR-02's polling logic — it needs to handle re-acquisition, not just first-acquisition. |
| **App detects driver-disconnected state and shows it in tray** | If the driver fails to register or SteamVR isn't running, the app should not silently pretend to work. Users expect a visual cue (tray icon color, status text) so they don't cover the mic 20 times wondering why nothing happens. | LOW | Existing HTTP bridge already exposes connection state. Surface it in the ImGui tray/UI. This is the "silent failure" antidote. |

#### (b) Auto-start UX

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Auto-start is on by default after install** | The milestone exists because the current state ("not auto-starting") is explicitly called out as bug #3 in README Known Issues. Default-off would make the install worse, not better. | LOW | Installer calls `vr::VRApplications()->SetApplicationAutoLaunch(true, "featherc.micmap")` after `AddApplicationManifest()`. |
| **Appears in SteamVR's "Startup Overlay Apps" list with a user-facing toggle** | This is the one SteamVR-native escape hatch. Users who want to disable auto-start look here first ([Steam Community Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=3035381763)). If MicMap's not in the list, users Google "how to disable MicMap auto-start" and find nothing. | LOW | Manifest must declare the app as `"type": "overlay"` (or `"other"`) so it appears in the Startup/Shutdown → Choose Startup Overlay Apps list. Without this, SteamVR won't list it. |
| **Silent first launch (no console window, no focus steal)** | User is putting on their headset when SteamVR starts — a desktop window stealing focus mid-HMD-on is universally hated. Tray icon only is the expected default. | LOW | Existing MicMap already runs to tray. Ensure the auto-start launch doesn't pop a visible window the way manual launch might. Check `WinMain` command-line handling. |
| **Graceful behavior when SteamVR closes** | When SteamVR shuts down, MicMap should exit cleanly (or return to idle) rather than keep the mic hot forever with no target for its triggers. Users leave SteamVR open/closed many times a day. | LOW | Listen for `VREvent_Quit` on the app side's OpenVR client, exit cleanly. OpenVR-Autostarter uses this pattern ([dreiekk/OpenVR-Autostarter](https://github.com/dreiekk/OpenVR-Autostarter)). |
| **Doesn't prevent SteamVR from relaunching** | Known SteamVR bug: [openvr#1425](https://github.com/ValveSoftware/openvr/issues/1425) — non-Steam background apps left running can prevent SteamVR relaunch. MicMap must exit when SteamVR exits. | LOW | Same mechanism as above. Anti-pattern: daemonizing MicMap past SteamVR exit. |

#### (c) Driver installer UX

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Single executable, double-click-runs** | Current state is a batch script run as admin — already a friction point. Any step the user has to do manually (elevate, open PowerShell, etc.) is a failure. Inno Setup's UAC prompt handles elevation. | LOW | bey-closer-t1's `BeyondProximity.iss` is the direct template: `PrivilegesRequired=admin`, `DisableWelcomePage=yes`, minimal clicks. |
| **Detects and blocks install while SteamVR is running** | SteamVR locks driver DLLs in memory. Overwriting in-use DLLs causes silent failures on next SteamVR launch — user's SteamVR then has a broken driver and no idea why. bey-closer-t1 gates this with `IsProcessRunning('vrserver.exe')` in `PrepareToInstall`. This is table-stakes for any VR driver installer. | LOW | Direct port of bey-closer-t1's WMI-based `IsProcessRunning` check. |
| **Clear, human error message if SteamVR is running** | Not just "can't install" — say "Please close SteamVR before installing MicMap" with the *reason*. bey-closer-t1's message is the pattern. | LOW | Single `MsgBox` via `PrepareToInstall` return string. |
| **Upgrade-in-place (no manual uninstall before reinstall)** | Inno Setup handles this natively via `{AppId}_is1` registry key — an install over an existing install auto-uninstalls the old version first ([w3tutorials Inno upgrade guide](https://www.w3tutorials.net/blog/how-to-automatically-uninstall-previous-installed-version-in-inno-setup/)). Missing this = user has to hunt for uninstaller in Add/Remove Programs before every update. | LOW | Set `AppId={{GUID}}` once and keep it stable across versions. Inno does the rest. |
| **Uninstall is a single-click too** | bey-closer-t1 set `Uninstallable=no`, which is wrong for MicMap: MicMap owns its own driver directory (PROJECT.md constraint: "not nested") and users install/uninstall independently of any OEM driver. A real uninstaller in Add/Remove Programs is expected. | LOW | Set `Uninstallable=yes` (default). Add `[Run]` step on uninstall to call `vrpathreg.exe removedriver "..."` so the SteamVR registration is also cleaned up — otherwise SteamVR logs warnings forever about the missing driver path. |
| **Admin prompt happens once, at the start** | Not mid-install. Inno's `PrivilegesRequired=admin` triggers UAC at launch before any UI is drawn. Users expect the UAC shield on the installer icon itself. | LOW | Free via Inno. |
| **Installs to a predictable path, not hidden in AppData** | Driver DLLs must live under a path `vrpathreg` can reach and the user can inspect. Convention: `%ProgramFiles%\MicMap\` or directly nested under `...\Steam\steamapps\common\SteamVR\drivers\micmap\` (the current script's location, per README). | LOW | Pick one and document. The latter is more SteamVR-native; the former is more Windows-native. Recommend `%ProgramFiles%\MicMap\` as the install root with the driver subfolder registered via `vrpathreg adddriver "{app}\driver"` — separates app binary from driver binary cleanly. |
| **Post-install verification: "Driver registered" confirmation** | User just clicked Install with admin rights; they deserve to know it actually worked. bey-closer-t1 offers a "Launch SteamVR" checkbox on the finished page which implicitly verifies. MicMap should do the same OR show "MicMap driver registered with SteamVR" text on the finished page. | LOW | Inno `DisableFinishedPage=no` + finished-page message. |
| **Uninstall removes the driver from SteamVR's registry** | If `vrpathreg removedriver` isn't run on uninstall, SteamVR keeps trying to load the missing DLL and logs errors, and "micmap" keeps appearing in the driver list in SteamVR Developer settings. Users report ghost drivers as confusing ([SteamVR Troubleshooting forums](https://steamcommunity.com/app/250820/discussions/2/)). | LOW | `[UninstallRun]` section in the .iss calls `vrpathreg removedriver "..."`. |

#### (d) Settings persistence

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Selected microphone persists across sessions** | Currently the README literally says "Selection persists across sessions" — it doesn't, because config read-back is stubbed (CONCERNS.md tech debt #1). The README lies to the user today. This is regression-from-claim, not regression-from-feature. | LOW | CFG-01: wire up nlohmann/json read in `config_manager.cpp:142`. Dependency already vendored. |
| **Detection duration / sensitivity persist** | Same reason. User adjusts the slider, restarts SteamVR (implicitly restarts MicMap via auto-start), slider is back to 300ms default. | LOW | Same config load path. |
| **Training data persists (already working)** | Already shipped; don't break it. Training data is separately stored in `training_data.bin`, not `config.json`, so the config read-back fix doesn't touch it — but the milestone should not accidentally regress training persistence either. | LOW | Regression-test: train, restart SteamVR, trigger should work without retraining. |
| **Auto-start toggle is user-visible in the app, not just a JSON edit** | `config.json` has `"auto_start": true` but there's no UI toggle — users shouldn't have to edit JSON to flip it. ESPECIALLY because the *real* auto-start is now SteamVR-managed, not MicMap-managed (see anti-features below). | LOW-MEDIUM | Add a checkbox in the existing ImGui settings panel that calls `vr::VRApplications()->SetApplicationAutoLaunch(bool)`. This is the correct surface — SteamVR's own "Choose Startup Overlay Apps" UI is the source of truth, MicMap's checkbox is a convenience mirror. |
| **Config file is human-readable and hand-editable** | Power users and support-forum troubleshooters expect to be able to open `%APPDATA%/MicMap/config.json`, see sensible keys, edit, save, restart. JSON + pretty-printed is table stakes. | LOW | Already JSON. Ensure `nlohmann::json::dump(2)` (indented) on write. |
| **Invalid / missing / corrupt config falls back to defaults, not crash** | User deletes AppData, config is corrupted by an interrupted write, MicMap updates and introduces a new required field — all of these must result in "reset to defaults + log a warning," not a refusal to start. CONCERNS.md security #2 calls out missing input validation. | LOW | Try/catch around the parse, validate bounds on each field, log and default-on-failure. |

### Differentiators (Competitive Advantage)

These are valuable but **not scoped to this milestone**. Listed here so they're captured without bloating scope.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Per-microphone training profiles** | Different HMDs (Beyond, Index, Quest via Link) have different mic characteristics. Right now there's one profile. CONCERNS.md flags this as a Medium-priority missing feature. Would significantly improve "it works great on my HMD but not my friend's" complaints. | MEDIUM | Defer. Profile management is its own milestone — touches training flow, UI, and storage. Out of scope here. |
| **In-VR settings overlay** | User adjusts detection duration from inside VR without taking off headset. Overlay stubs already exist in `dashboard_manager.cpp` (PROJECT.md Out-of-Scope, CONCERNS.md tech debt #2). High perceived value but entire separate domain. | HIGH | Explicitly out of scope per PROJECT.md. Don't regress the stubs — leave them stubbed. |
| **Export / import settings** | User reinstalls Windows, wants their tuned sensitivity/duration back. Low-effort if config is already JSON. | LOW | Deferrable. Users can manually copy `config.json` today if they know where it is. "Backup settings" button in UI is a nice v1.x add. |
| **Multiple named presets** (e.g. "quiet room", "noisy room") | Users currently retrain when their environment changes (README workaround). Preset switcher would be better than retraining. | MEDIUM | Defer. Ties into per-device profiles. |
| **Silent-install CLI flags** (`/SILENT`, `/VERYSILENT`) | Power users, IT deployment, enthusiast communities building "one-click VR setup" scripts want unattended installs. Inno Setup supports this natively. | LOW | Essentially free with Inno — no extra work required, it just works. Worth noting in docs once installer ships. |
| **Installer logs to file for support** | When install fails for obscure reasons (antivirus quarantine, Steam in non-default location), a log in `%TEMP%` the user can attach to a bug report is gold. Inno supports `/LOG=` out of the box. | LOW | Document the flag; don't need to do anything to enable it. |
| **"Don't auto-start" checkbox on installer finished page** | Preemptive escape hatch for the small fraction of users who want to install but trigger manually. | LOW | Optional. Arguably anti-feature (see below) since SteamVR's Startup Overlay Apps UI already provides this. |

### Anti-Features (Commonly Requested, Often Problematic)

Explicit non-goals for this milestone. Documenting so they don't creep in.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| **Windows Run-key or Startup folder auto-start** | "Normal" way to auto-start apps on Windows. Superficially simpler. | Starts MicMap when the user logs in, not when SteamVR starts — mic-monitoring runs 24/7 for no reason, adds to boot time, shows up in Task Manager startup impact. Also fights SteamVR: both the Run-key and `app.vrmanifest` auto-launch would race. | Use `app.vrmanifest` + `SetApplicationAutoLaunch` — SteamVR-native, lifecycle tied to VR sessions only. |
| **Keep the virtual-controller driver as a fallback** | "What if the HMD-sidecar technique breaks in a future SteamVR update?" | PROJECT.md explicit decision: rip it out entirely. Two code paths = two sets of bugs. Fallback path has the laser-beam regression, which is the whole reason for migration. | If SteamVR breaks the sidecar pattern later, fix it then. Don't carry dead weight. |
| **In-dashboard "select" as a separate action** | The old architecture had an open-vs-select split, so there's muscle memory that these might be "two things." | PROJECT.md: `/input/system/click` is one native action that handles both natively. The split was an artifact of the virtual-controller architecture, not a user-facing feature. | Single trigger, single code path. If users ask, explain it's now literally the HMD button behavior. |
| **Custom MicMap auto-start manager ("start/stop MicMap" toggle in the app itself for auto-start only)** | "Why would I go to SteamVR Settings to toggle an auto-start for MicMap?" | SteamVR's Startup Overlay Apps list is the one true UI and users already know to look there. A separate MicMap-side toggle that *doesn't* sync with SteamVR's UI creates two sources of truth and confusion. | Provide a convenience checkbox in MicMap's UI that programmatically calls `SetApplicationAutoLaunch` — it flips the same SteamVR-side flag, so both UIs stay in sync. Do NOT maintain a parallel MicMap-private "auto-start" boolean. |
| **Installer writes to `config.json` during install** | "Let's pre-populate the config from installer choices." | Two writers to the same file. Installer runs as admin → file ACLs end up admin-owned → user-mode MicMap then can't write. Users report this kind of issue frequently in driver installer forums. | Installer leaves `config.json` alone. MicMap writes defaults on first run. Installer only writes files under `%ProgramFiles%\MicMap\` and under Steam's driver dir. |
| **Installer bundles SteamVR or Steam** | "One-click install for new users." | Redistribution licensing nightmare. Massive installer size. Steam has its own updater — fighting it breaks things. | Installer checks for SteamVR presence via `vrpathreg.exe` existence at `{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe` (per bey-closer-t1 pattern); if missing, show a clear "Install SteamVR first" message with a link. |
| **"Run on every Windows login, check if SteamVR is running, launch MicMap if so"** | Some users think this is what "auto-start with SteamVR" means. | This is a watchdog daemon. Adds to boot. Fights SteamVR's native auto-launch. Polls for vrserver forever. | `app.vrmanifest` + `auto_launch=true`. SteamVR itself is the watchdog — it launches MicMap as part of its own startup, no polling needed. |
| **First-launch config wizard in VR** | "User just installed, help them configure from the headset." | Overlay UI is out of scope this milestone (PROJECT.md). Training already happens with HMD on anyway (cover the HMD mic). Wizard-in-VR is a differentiator for a future overlay milestone. | Training flow stays desktop-UI only for this milestone. Future overlay milestone can build a wizard. |
| **Post-install "run MicMap now" button that starts MicMap outside of SteamVR** | "Install → immediately try it." | MicMap is useless without SteamVR running (the driver needs a live vrserver to inject into). Starting MicMap standalone gives the illusion of working and then silently does nothing — exactly the failure mode we're trying to fix. | Post-install button says "Launch SteamVR" (bey-closer-t1 pattern) — SteamVR auto-starting brings MicMap up via the manifest we just registered. |
| **Run-once first-launch config dialog when MicMap starts** | "Welcome to MicMap, let me configure device X for you." | Focus-stealing popup the first time the user enters VR is exactly the thing users hate. Detection works with sane defaults + existing training flow. | First-launch discoverability is the tray icon + the README. If the tray icon popup needs a "Training required" hint, fine — but no modal dialog. |

## Feature Dependencies

```
Installer (INST-01)
    ├──installs──> app + driver binaries
    ├──installs──> app.vrmanifest  ──enables──> Auto-start (AUTO-01)
    │                                               │
    │                                               └──requires──> SteamVR running at some point
    │                                                              to read the manifest
    │
    ├──registers──> driver via vrpathreg   ──enables──> HMD sidecar (SVR-01..04)
    │                                                        │
    │                                                        └──requires──> SteamVR running
    │                                                                       HMD connected (SVR-02 polling)
    │
    └──must-check──> vrserver.exe NOT running during install  (installer safety gate)


Settings UI toggle "Auto-start with SteamVR"
    └──calls──> vr::VRApplications()->SetApplicationAutoLaunch()
               │
               └──requires──> manifest already registered (by installer, INST-01/INST-02)


Config read-back (CFG-01)
    ├──enables──> device selection persistence   [table-stakes]
    ├──enables──> detection duration persistence [table-stakes]
    └──enables──> auto-start toggle state display [table-stakes]
                  (reading SteamVR's SetApplicationAutoLaunch is authoritative, but
                   the UI needs to load _something_ on startup before querying SteamVR)


Uninstaller
    ├──runs──> vrpathreg removedriver (table-stakes — prevents ghost drivers)
    ├──runs──> VRApplications()->RemoveApplicationManifest (table-stakes — removes from auto-start list)
    └──deletes──> app files (table-stakes)
    │
    └──does NOT delete──> %APPDATA%\MicMap\  (user settings + training data survive uninstall;
                                               this is Windows convention — offer a checkbox
                                               if opinionated about it, but default keep)
```

### Dependency Notes

- **Auto-start (AUTO-01) depends on installer (INST-01):** The `app.vrmanifest` file must live at a stable path the installer picks. `VRApplications()->AddApplicationManifest()` takes an absolute path; if the user ever moves `micmap.exe`, the manifest becomes invalid. This is why the installer owns the install path and the manifest registration — MicMap at runtime should not be rewriting or moving the manifest.
- **HMD sidecar (SVR-02) depends on HMD presence polling, not just driver startup:** SteamVR loads drivers before an HMD is necessarily connected. The known failure ([openvr#1378](https://github.com/ValveSoftware/openvr/issues/1378)) of trying to operate on `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` too early returns errors that, if uncaught, get the driver flagged as crashy. Polling in `RunFrame` is already the plan (PROJECT.md SVR-02).
- **Config read-back (CFG-01) conflicts with nothing and is the keystone:** Without it, none of the other "persists" features in the table-stakes list are real. Must land before auto-start toggle UI can meaningfully exist (because the checkbox has no persistent state to remember).
- **Installer upgrade path depends on a stable `AppId`:** Inno Setup's automatic upgrade-in-place keys off `AppId`. If the AppId ever changes between versions, users get parallel installs with no auto-uninstall of the old one. Pick the GUID once, never change it, commit it to the `.iss` file.
- **Auto-start UI toggle in MicMap ↔ SteamVR's own toggle:** These must remain in sync. Recommendation: MicMap's checkbox *queries* `GetApplicationAutoLaunch()` on startup and on panel-open to get current state (SteamVR is the source of truth), and *writes* via `SetApplicationAutoLaunch()`. Do NOT store an `"auto_start": bool` in `config.json` as authoritative — that line in the current config.json is misleading and should be removed (or repurposed as a last-known-state cache only).

## MVP Definition

### Launch With (this milestone — v1 of "seamless")

These are the milestone's table-stakes. Miss any and the milestone's promise is broken.

- [ ] HMD sidecar driver creates `/input/system/click` on HMD container after handle becomes valid (SVR-01, SVR-02)
- [ ] Trigger path updates the HMD-side component (SVR-03)
- [ ] Virtual-controller code fully removed, no laser beam possible (SVR-04)
- [ ] `app.vrmanifest` registered by installer; auto-launch enabled by default (AUTO-01, INST-02)
- [ ] MicMap appears in SteamVR Settings → Startup/Shutdown → Choose Startup Overlay Apps
- [ ] Auto-started MicMap runs to tray (no visible window, no focus steal)
- [ ] MicMap exits when SteamVR exits (listen for `VREvent_Quit`)
- [ ] Inno Setup installer: admin-elevated, blocks install if `vrserver.exe` running, human error message, upgrade-in-place via stable `AppId`, real uninstaller that runs `vrpathreg removedriver` and `RemoveApplicationManifest` (INST-01)
- [ ] Config read-back working (CFG-01): selected device, detection duration, sensitivity all persist across restarts
- [ ] Corrupt / missing config falls back to defaults without crashing
- [ ] Driver-disconnected / SteamVR-not-running state visible in app UI (tray icon or status text) — no silent failures
- [ ] README updated: crossed-out auto-start sections become current; install instructions reference the .exe installer, not the .bat script (DOC-01)

### Add After Validation (v1.x — next milestones after this one)

- [ ] Auto-start toggle checkbox in MicMap UI that calls `SetApplicationAutoLaunch`
- [ ] Installer logging surfaced in docs (`/LOG=` flag, where to find the install log for support)
- [ ] Installer-finished-page "Launch SteamVR" checkbox (cosmetic polish, useful when SteamVR isn't running)
- [ ] Silent-install CLI flags documented (`/SILENT`, `/VERYSILENT`) — essentially free from Inno
- [ ] Export / import settings button in UI

### Future Consideration (v2+)

- [ ] Per-microphone training profiles (separate milestone — touches training, UI, storage)
- [ ] In-VR settings overlay (depends on implementing the stubbed overlay functions; separate milestone per PROJECT.md)
- [ ] Multiple named presets ("quiet room" / "noisy room")
- [ ] Undo / revert training (CONCERNS.md missing feature, priority Low)

## Feature Prioritization Matrix

Only features **in scope for this milestone**. Differentiators and future items are outside the matrix — see the earlier section for those.

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| HMD sidecar button (no laser beam) | HIGH | MEDIUM | P1 |
| HMD handle polling / cold-start resilience | HIGH (prevents silent driver disable) | MEDIUM | P1 |
| Virtual-controller code removal (no fallback) | MEDIUM (simpler codebase) | LOW | P1 |
| `app.vrmanifest` auto-start | HIGH | LOW | P1 |
| Exit on `VREvent_Quit` | MEDIUM (prevents ghost processes) | LOW | P1 |
| Inno installer: admin + vrserver-running gate + upgrade-in-place | HIGH | LOW (bey-closer-t1 template) | P1 |
| Uninstaller that cleans `vrpathreg` and manifest | MEDIUM (prevents ghost drivers) | LOW | P1 |
| Config read-back via nlohmann/json | HIGH (existing "persists across sessions" claim becomes true) | LOW | P1 |
| Corrupt-config fallback to defaults | MEDIUM (rare path but catastrophic if missing) | LOW | P1 |
| Driver-disconnected state visible in UI | MEDIUM (eliminates the silent-failure UX) | LOW | P1 |
| README / docs update | MEDIUM | LOW | P1 |
| Auto-start checkbox in MicMap UI | LOW (SteamVR's own UI already exists) | LOW | P2 |
| Installer finished-page "Launch SteamVR" checkbox | LOW | LOW | P2 |
| Post-install verification message | LOW (implicit via "Launch SteamVR" option) | LOW | P2 |

**Priority key:**
- P1: Must have for this milestone to be "done"
- P2: Should have, add if time allows within the milestone
- P3: Not in this milestone — see Differentiators / Future Consideration sections

## Competitor Feature Analysis

| Feature | OVR Advanced Settings | OVR Toolkit / XSOverlay | OpenVR-Autostarter | MicMap (this milestone) |
|---------|---------------------|-------------------------|---------------------|--------------------|
| Ships as SteamVR overlay app (auto-start native) | Yes | Yes (Steam store distribution) | Yes (via its own manifest) | **Yes (AUTO-01)** |
| Listed in "Choose Startup Overlay Apps" | Yes | Yes | Yes | **Yes (INST-02 registers manifest with `type: overlay`)** |
| Single-click installer | Steam-distributed (Steam is the installer) | Steam-distributed | Manual download + manual register | **Yes (Inno Setup, INST-01)** |
| Settings persistence via JSON | Yes (AppData JSON) | Yes (save/load configs) | Yes (config file) | **Yes (CFG-01 fixes read-back)** |
| Exits when SteamVR exits | Yes | Yes | Yes (its whole point) | **Yes (VREvent_Quit handler)** |
| Input injection on HMD container | N/A (not an input tool) | N/A | N/A | **Yes (SVR-01..04, unique in this space)** |
| Laser-beam-on-trigger artifact | N/A | N/A | N/A | **Eliminated (was the motivation)** |
| Per-device / per-profile settings | Yes (supersampling profiles, chaperone profiles) | Yes (saved configs) | No | No (deferred to future milestone) |
| Export / import settings | Limited | Yes | No | No (deferred) |

**Takeaway:** MicMap's auto-start, installer, and settings-persistence behavior should match the table stakes set by OVR Advanced Settings and OVR Toolkit (which VR users already run daily), because those are the mental model of "well-behaved SteamVR overlay tool." MicMap's genuinely novel surface — the one it should protect and make boringly reliable — is the HMD-sidecar input injection. Everything else should feel unremarkable; the point is that it just works.

## Sources

- **Primary internal references (HIGH confidence — read in full):**
  - `D:\Documents\Projects\mic-map\.planning\PROJECT.md` — milestone definition, in-scope / out-of-scope, key decisions
  - `D:\Documents\Projects\mic-map\README.md` — current (claimed vs. actual) feature set, known issues
  - `D:\Documents\Projects\mic-map\.planning\codebase\CONCERNS.md` — tech debt, missing features, fragility areas
  - `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — Inno Setup reference (admin, vrserver process gate, `vrpathreg adddriver`, post-install launch checkbox)
- **External, ecosystem context (MEDIUM confidence — single-source unless noted):**
  - [Autostarting VR Tools on SteamVR Start — Steam Community Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=3035381763) — user-facing UI path: Settings → Startup/Shutdown → Choose Startup Overlay Apps
  - [OpenVR issue #1378 — SetApplicationAutoLaunch race with AddApplicationManifest](https://github.com/ValveSoftware/openvr/issues/1378) — known timing/error case when registering manifests
  - [OpenVR issue #1425 — SteamVR fails to relaunch if non-Steam background app still running](https://github.com/ValveSoftware/openvr/issues/1425) — rationale for exiting on `VREvent_Quit`
  - [OpenVR issue #1537 — GetApplicationAutoLaunch returns false when overlay app configured to autostart](https://github.com/ValveSoftware/openvr/issues/1537) — caveat: state query is sometimes unreliable, be defensive
  - [SteamVR Developers — Add Application to SteamVR Dashboard Automatically](https://steamcommunity.com/app/250820/discussions/7/4633736723121056607/) — confirms `AddApplicationManifest` + `SetApplicationAutoLaunch` is the supported pattern
  - [dreiekk/OpenVR-Autostarter](https://github.com/dreiekk/OpenVR-Autostarter) — reference for "no background task when not in VR, exit cleanly" behavior
  - [K2VR — Auto-start with SteamVR docs](https://k2vr.tech/docs/autostart.html) — user-facing documentation pattern for how apps explain auto-start to end users
  - [OpenVR-AdvancedSettings](https://github.com/OpenVR-Advanced-Settings/OpenVR-AdvancedSettings) — reference for what "a well-behaved SteamVR overlay utility" looks like end-to-end
  - [Inno Setup — automatic uninstall of previous version (w3tutorials)](https://www.w3tutorials.net/blog/how-to-automatically-uninstall-previous-installed-version-in-inno-setup/) — upgrade-in-place pattern via stable `AppId` + `{AppId}_is1` registry key
  - [Inno Setup FAQ](https://jrsoftware.org/isfaq.php) — `PrivilegesRequired=admin`, `PrepareToInstall`, uninstall registry behavior
  - [Valve Developer Community — SteamVR Error Codes](https://developer.valvesoftware.com/wiki/SteamVR/Error_Codes) — driver-disabled-on-crash behavior informs why cold-start resilience matters
  - [OpenVR — Local Driver Registration wiki](https://github.com/ValveSoftware/openvr/wiki/Local-Driver-Registration) — `vrpathreg.exe` contract during testing and end-user install
- **Training-data (LOWER confidence — sanity-checked but not independently verified):** Conventional Windows installer UX expectations (single UAC prompt, install-to-ProgramFiles, Add/Remove Programs listing); conventional Windows AppData conventions (survive uninstall unless user opts in to purge).

---
*Feature research for: Seamless SteamVR Integration milestone (MicMap)*
*Researched: 2026-04-22*

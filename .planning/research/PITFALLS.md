# Pitfalls Research

**Domain:** Brownfield SteamVR sidecar driver migration (virtual-controller -> HMD-sidecar input injection), SteamVR-native auto-start, Inno Setup driver installer, brownfield cleanup
**Researched:** 2026-04-22
**Confidence:** HIGH (domain-specific pitfalls cross-verified with `bey-closer-t1` prior art, official OpenVR wiki, and SteamVR issue tracker)

## Orientation

This milestone has six distinct pitfall territories:
- (a) Sidecar driver lifecycle (driver that never calls `TrackedDeviceAdded`)
- (b) HMD input-injection edge cases beyond what `HMD Button Stub.md` already solved
- (c) `app.vrmanifest` / auto-launch registration
- (d) Inno Setup installer for a driver that owns its own directory (unlike bey-closer-t1's nested pattern)
- (e) Process-coordination across app <-> driver <-> HMD reactivation
- (f) Ripping out the virtual controller code cleanly

`bey-closer-t1` is the closest sibling prior art. Its `HMD Button Stub.md` documents the two known cross-driver input errors (`VRInputError_WrongType` on cross-driver Update, `VRInputError_InvalidParam` on early Create). Its `BeyondProximity.iss` is the installer reference. Its `RETROSPECTIVE.md` calls out two Inno Setup 6 Pascal-script gotchas that bit the last project. We build on those, we do not re-solve them.

---

## Critical Pitfalls

### Pitfall 1: HMD container handle goes stale after HMD deactivation / reactivation

**What goes wrong:**
The driver successfully creates `/input/system/click` on the HMD property container, caches the component handle (`m_hSystemClick`), and for a while all updates succeed. Then the user puts the headset down, lighthouse deactivates the HMD, the user puts it back on, lighthouse reactivates the HMD — and now `UpdateBooleanComponent` on the cached handle silently no-ops, returns `VRInputError_InvalidHandle`, or worst case the HMD property container handle is now a different value and the old component is orphaned.

**Why it happens:**
`HMD Button Stub.md` frames creation as a one-shot (`m_bComponentAttempted = true` and never retry). That is correct for the "HMD comes up after driver Init" case, but it doesn't handle HMD reactivation during a single driver lifetime. Lighthouse emits `VREvent_TrackedDeviceActivated` / `VREvent_TrackedDeviceDeactivated`. Property-container handles and input-component handles are not guaranteed stable across a deactivation/reactivation cycle.

**How to avoid:**
- Do NOT treat `m_bComponentAttempted` as permanent. Replace it with a state enum: `NotReady` -> `Ready` -> `Invalidated` -> `Ready` (re-create).
- Subscribe to `VREvent_TrackedDeviceDeactivated` for device index 0 (HMD) via `IVRServerDriverHost::PollNextEvent` in `RunFrame()`. On deactivate: reset `m_hSystemClick = k_ulInvalidInputComponentHandle` and set state back to `NotReady`.
- On next `RunFrame`, re-poll `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` and re-run the `CreateBooleanComponent` sequence from `HMD Button Stub.md`.
- Guard every `UpdateBooleanComponent` call: if return is anything except `VRInputError_None`, log the error code and flip state back to `NotReady`. Do NOT silently drop the trigger — surface it in a structured log.

**Warning signs:**
- `UpdateBooleanComponent` returns `VRInputError_InvalidHandle` (1) or `VRInputError_WrongType` (2) after the headset has been off.
- First detection trigger after rebooting the headset does nothing; subsequent triggers also do nothing until SteamVR is fully restarted.
- Works in a fresh SteamVR session, fails the second time the user puts the headset on.

**Phase to address:**
Driver refactor phase (SVR-01/02/03). Put the state-machine for component handle lifecycle in the same PR as the initial `RunFrame` polling. This is the single biggest risk beyond what `HMD Button Stub.md` already documented.

---

### Pitfall 2: Auto-launch shutdown loop — SteamVR relaunches itself because MicMap is still running

**What goes wrong:**
User quits SteamVR. SteamVR notices that MicMap (a registered auto-launch overlay app) is still running, decides it must relaunch SteamVR to manage it, and re-spawns vrserver.exe. User quits again — same thing. SteamVR cannot actually exit while MicMap is alive. Either MicMap runs forever after a quit, or the user is in a respawn loop.

**Why it happens:**
OpenVR issue #1425 documents this exact behavior: "SteamVR fails to relaunch if a non-Steam background-app launched with it is still running." If the app doesn't listen for `VREvent_Quit` and gracefully exit, SteamVR's lifecycle model breaks. The manifest `app_type` also matters — "overlay" apps are expected to be managed by SteamVR, so their lifecycle must match SteamVR's.

**How to avoid:**
- In the MicMap app, subscribe to `VREvent_Quit` via the client-side `IVRSystem::PollNextEvent` loop. On receipt: tear down the HTTP client cleanly, stop the audio thread, exit the process. Add a 2-second watchdog so a stuck shutdown still terminates.
- Call `IVRSystem::AcknowledgeQuit_Exiting()` before tearing down, so SteamVR knows the app is exiting voluntarily.
- Register the manifest with `app_type` matching the actual lifecycle. For a hands-free system-button app that needs to outlive dashboard toggles but not SteamVR itself, use `"overlay"` with `auto_launch : true`. Do NOT register as `"scene"` (reserved for the active VR game).
- Manually test the quit-and-reopen-SteamVR cycle before shipping. This is the single most common auto-launch regression.

**Warning signs:**
- vrserver.exe appears in Task Manager shortly after the user closes SteamVR.
- MicMap tray icon stays active after SteamVR quits.
- SteamVR logs show repeated startup sequences without a user action in between.
- On manual quit: status bar reads "Waiting for apps to close" for more than a few seconds.

**Phase to address:**
Auto-start phase (AUTO-01). Must be verified before the installer phase ships, because the installer registers the manifest.

---

### Pitfall 3: `vrpathreg adddriver` double-registration on reinstall

**What goes wrong:**
User installs MicMap v1.0. Installer runs `vrpathreg adddriver "C:\Program Files\MicMap\driver"`. Later they install v1.1. Installer runs `adddriver` again, pointing to the same path. SteamVR now has two entries for the same driver path in `steamvr.vrpaths`. On startup, vrserver tries to load the driver twice, fails/conflicts, or the driver appears to "mostly work" but with duplicate input components contending.

**Why it happens:**
`vrpathreg adddriver` does not dedupe. Per bey-closer-t1 research notes and OpenVR issue #1653, `vrpathreg` will happily register the same path multiple times. Its companion `finddriver` command exists specifically to enable idempotent install flows. The bey-closer-t1 installer skipped this check because it installs nested (inside a Steam-managed path) — MicMap installs to its own path, so this bug is in play.

**How to avoid:**
- In the Inno Setup `[Run]` section, call `vrpathreg finddriver "<path>"` first. Check exit code / stdout. Only call `adddriver` if not already registered.
- Better: in `PrepareToInstall` or `CurStepChanged(ssInstall)`, run `vrpathreg removedriver "<path>"` unconditionally before `adddriver`. `removedriver` on a non-registered path is a no-op. This matches the common "clean register" idiom documented in OpenVR wiki's Local Driver Registration page.
- In the uninstaller, also call `vrpathreg removedriver` — do not rely on `vrpathreg` being present (see Pitfall 10).

**Warning signs:**
- `steamvr.vrpaths` (`%LOCALAPPDATA%\openvr\openvrpaths.vrpath`) shows duplicate path entries for MicMap.
- Driver log (`vrserver.txt`) shows the driver being enumerated twice.
- Input triggers sometimes work, sometimes don't, or fire twice.
- Users who upgraded from an older MicMap version report inconsistent behavior that reinstalling fixes.

**Phase to address:**
Installer phase (INST-01). Treat as a correctness requirement, not a polish item.

---

### Pitfall 4: Inno Setup blocks install when SteamVR is running, but forgets the HMD-connected-but-SteamVR-off case

**What goes wrong:**
User has headset plugged in, SteamVR quit, headset-runtime helper processes (vrmonitor, vrwebhelper, vrdashboard, or third-party overlays) still alive and holding `driver_micmap.dll` in memory. Installer checks only for `vrserver.exe`, sees it's gone, proceeds to overwrite the DLL. Overwrite silently fails (Windows "file in use") or succeeds partially, leaving a corrupt DLL. On next SteamVR start, driver fails to load.

**Why it happens:**
bey-closer-t1's `BeyondProximity.iss` checks only `vrserver.exe`. That is the main offender but not the only process that can hold the DLL — helper processes and overlays loaded by vrserver can outlive a SteamVR "quit" by several seconds, and third-party overlays (OpenVR Advanced Settings, OVR Toolkit) may keep helper processes alive.

**How to avoid:**
- Expand the `IsProcessRunning` check to a process list: `vrserver.exe`, `vrmonitor.exe`, `vrcompositor.exe`, `vrdashboard.exe`, `vrwebhelper.exe`.
- If any are running, block with a message naming the specific process(es) found. Offer retry without re-running the installer.
- In `[Files]` entries for the driver DLL, add `restartreplace` flag as a belt-and-suspenders fallback: if Windows can't replace the file, queue it for replacement on next reboot. Surface that to the user on the finish page.
- Do NOT skip the process check by time-of-day / lock-file heuristics. Use the positive WMI query that bey-closer-t1 already validated.

**Warning signs:**
- Post-install: DLL size/timestamp doesn't match the new version.
- `driver_micmap.dll` is listed in `handle.exe vrserver` or `Process Explorer` after an apparent-clean SteamVR quit.
- User reports "I installed the new version but it's behaving like the old one."

**Phase to address:**
Installer phase (INST-01). Ship the expanded process list in v1 — retrofitting requires a re-release.

---

### Pitfall 5: `app.vrmanifest` with relative paths vs. install-location surprises

**What goes wrong:**
Manifest declares `"binary_path_windows": "micmap.exe"` (relative). Works from the dev build directory. Installer places the app in `C:\Program Files\MicMap\micmap.exe` and registers the manifest from the same directory. SteamVR tries to auto-launch, resolves the relative path against an unexpected CWD, fails with `VRApplicationError_LaunchFailed`, and silently does not auto-start.

Alternatively: manifest uses an absolute path that was correct at install time. User later reinstalls to a different drive, or Steam moves their library. The manifest still points at the old path. Auto-launch either fails silently or spawns a ghost from the old path if it still exists.

**Why it happens:**
OpenVR wiki documents that "action_manifest_path" is resolved relative to the vrmanifest file's own directory, but `binary_path_windows` behavior is less clearly documented and users report it being interpreted relative to SteamVR's working directory, not the manifest. On top of that, `AddApplicationManifest` resolves paths at registration time, not at launch time — so the registration snapshot persists across the user's filesystem changes.

**How to avoid:**
- In the installer, write the manifest at install time with absolute paths expanded from `{app}` (Inno Setup's `ExpandConstant('{app}')`). Do not ship a static `app.vrmanifest` in the source — generate it in `CurStepChanged(ssPostInstall)` by loading a template and substituting `{app}`.
- Call `vr::VRApplications()->RemoveApplicationManifest(oldPath)` before `AddApplicationManifest(newPath)` in both install (pre-install cleanup) and uninstall flows. Handles the "user reinstalled elsewhere" case.
- After `AddApplicationManifest`, also explicitly call `SetApplicationAutoLaunch(appKey, true)`. Per OpenVR issue #1378, `SetApplicationAutoLaunch` can return `VRApplicationError_UnknownApplication` immediately after a successful `AddApplicationManifest` due to a propagation delay — handle that by polling `IsApplicationInstalled` for up to 2 seconds before calling `SetApplicationAutoLaunch`.
- Verify post-install: a smoke-test PowerShell script that reads `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` and `steamvr.vrsettings` to confirm the manifest is registered with the expected absolute path.

**Warning signs:**
- SteamVR starts but MicMap does not auto-launch.
- SteamVR "Startup / Shutdown > Manage Startup Overlay Apps" list shows MicMap but toggling it does nothing.
- `vrserver.txt` logs `Application <key> failed to launch`.

**Phase to address:**
Auto-start phase (AUTO-01) for the manifest-authoring mechanism. Installer phase (INST-02) for the absolute-path generation at install time.

---

### Pitfall 6: `driver.vrdrivermanifest` missing or malformed — driver loads with empty name

**What goes wrong:**
Driver DLL is present, `vrpathreg adddriver` succeeded, `driver.vrdrivermanifest` was forgotten from the Inno Setup `[Files]` section or has a trailing comma / wrong field name. vrserver enumerates the driver directory, fails to parse the manifest, and either: silently drops the driver, or loads it with a null name so the Settings > Startup > Add-ons panel shows a blank row the user cannot toggle.

**Why it happens:**
The manifest is JSON and Inno Setup just copies the file. If your build system only outputs the DLL to `{app}\bin\win64\` and you forget to `Source:` the manifest, there's no build-time error. The bey-closer-t1 ISS script explicitly copies both (`driver.vrdrivermanifest` and the DLL), but a fresh installer author can easily miss the manifest.

**How to avoid:**
- Verification test baked into the installer smoke script: after install, check `{app}\driver.vrdrivermanifest` exists and parses as JSON with `name`, `directory`, `resourceOnly`, `alwaysActivate` fields present.
- Lint the manifest at build time. CMake custom command that runs `python -c "import json; json.load(open('...'))"` on the manifest before the DLL target completes.
- Copy the reference manifest from the OpenVR `samples/drivers` directory as the baseline, not hand-written.

**Warning signs:**
- Settings > Startup > Add-ons shows a row with empty driver name.
- `vrserver.txt` contains "failed to parse driver manifest" or "driver manifest missing required field".
- `vrpathreg show` lists the driver path but SteamVR never enumerates devices/components from it.

**Phase to address:**
Driver refactor phase (SVR-01) — manifest must already be correct. Verified in installer phase (INST-01) by the smoke script.

---

### Pitfall 7: Virtual-controller removal leaves dangling code paths that compile but dereference null

**What goes wrong:**
The rip-out goes mostly clean. `TrackedDeviceAdded` call is gone, `ControllerDevice` class is deleted. But `DeviceProvider::RunFrame` still has `if (m_pController) m_pController->RunFrame();`, or the HTTP endpoint handler still dispatches to a controller method, or dashboard-state polling still runs and writes to a stale flag. Code compiles, ships, and crashes on first detection trigger with an access violation — which as SteamVR issue #1597 documents, manifests as "Custom driver crash when launching any game."

**Why it happens:**
The existing codebase (per `.planning/PROJECT.md`) has:
- Device provider + virtual controller device class
- Dashboard-open-state polling and branching
- An "open vs. select" code split that only exists because of the virtual controller
- HTTP bridge handler that routes button events through the controller

These are coupled across files. Grep-based removal misses callsites. The `bey-closer-t1` Phase 03.1 did exactly this removal (ProximityDevice + TrackedDeviceAdded ripped out) and the CONTEXT.md lists each piece explicitly — we should mirror that discipline.

**How to avoid:**
- Make a removal checklist before touching code, modeled on `bey-closer-t1/.planning/milestones/v1.0-phases/03.1-remove-virtual-tracker/03.1-CONTEXT.md`. Enumerate every file that references the virtual controller, the dashboard-state poller, the "open vs. select" branch, and the `TrackedDeviceAdded` call.
- Remove files entirely rather than feature-flagging. `SVR-04` says this explicitly — honor it.
- Compile with `-Werror` / `/WX` and `-Wunused-function` / `-Wunused-variable` during the rip-out. Warnings about unused code are the signal that the old code path is fully dead.
- After the rip-out, run the driver under a debugger or with crash dumps enabled on first load (`vrserver` crash dumps go to `%LOCALAPPDATA%\Steam\logs\`). First detection trigger is the stress test.
- Search for these strings across the whole repo: `TrackedDeviceAdded`, `VirtualController`, `dashboard_open`, `open_vs_select`, `isDashboardOpen`, `ControllerDevice`, `ITrackedDeviceServerDriver` (only keep if used by a future device; otherwise remove).

**Warning signs:**
- `vrserver.exe` crashes with exception `0xc0000005` (access violation) when MicMap triggers its first button press.
- Compile warnings about unreferenced private members / dead code.
- Grep of the driver source still finds strings matching the virtual-controller vocabulary after the rip-out.

**Phase to address:**
Driver refactor phase (SVR-04). Do the rip-out in the same commit sequence as the new sidecar implementation — not before, not after. Interleaving leaves the repo in a broken intermediate state.

---

### Pitfall 8: SteamVR cached controller bindings reference the deleted virtual controller

**What goes wrong:**
SteamVR's input-system caches the virtual controller's input bindings in per-user files (typically `steamvr.vrsettings` entries under `input/actions_bindings/` and per-user binding files at `%LOCALAPPDATA%\openvr\input\`). After the rip-out, vrserver still has these bindings on disk, warns about unresolved device bindings on startup, or in pathological cases, refuses to load the dashboard bindings until the user manually "forgets" the old controller in Settings > Devices.

**Why it happens:**
SteamVR serializes bindings per user and does not garbage-collect bindings for drivers that no longer register their controller device class. The rip-out removes the source-of-truth but leaves the cached derivatives.

**How to avoid:**
- In the installer's `CurStepChanged(ssPostInstall)` or as a step in the uninstaller for the OLD installer, clean up `%LOCALAPPDATA%\openvr\input\` entries matching the old virtual controller's serial / manufacturer / model. Be surgical — only delete MicMap-owned entries (filter by serial string).
- Document a manual fallback in the README: "If you upgraded from MicMap <=0.x and see a ghost controller, go to Settings > Devices > Manage Vive Trackers (or equivalent) > remove MicMap Controller."
- Before cutting v1.0 of the new architecture, test the upgrade path on a machine that had the old virtual-controller driver installed, not just a fresh install.

**Warning signs:**
- First SteamVR start after upgrade shows a "lost device" notification with the old MicMap controller serial.
- `vrserver.txt` contains "binding not found for device <serial>" warnings.
- Dashboard controller bindings show an un-bindable "MicMap Controller" in SteamVR input rebinding UI.

**Phase to address:**
Installer phase (INST-01) — upgrade path is an installer responsibility. Can be deferred to a 1.x patch if not blocking on first release, but test it before declaring INST-01 done.

---

### Pitfall 9: JSON read-back crashes on first run because old writes were not strictly conformant

**What goes wrong:**
CFG-01 fixes the read-back with `nlohmann/json`. But the existing write path emits JSON that `nlohmann::json::parse` rejects: trailing comma, comments, numeric overflow, missing quotes, or NaN/Infinity doubles that `nlohmann` rejects by default. On first launch after upgrade, MicMap crashes with an unhandled `nlohmann::json::parse_error` while the user's only remedy is to delete `%APPDATA%\MicMap\config.json`.

**Why it happens:**
The config file has been write-only per `CONCERNS.md`. There's been no feedback loop on what the writes actually produce. In C++ codebases, manual `fprintf`-style JSON writing commonly produces non-conformant output (trailing commas being the #1 offender). `nlohmann` is strict by default.

**How to avoid:**
- Migrate writing to `nlohmann::json` too — one library for both read and write, guaranteed round-trippable.
- Wrap the read in `try/catch (const nlohmann::json::exception&)` and on parse error: log the error, back up the corrupt file (`config.json.bak.<timestamp>`), write a fresh default config, continue startup. Do NOT crash.
- Validate all numeric fields against bounds (sensitivity 0..1, duration > 0, etc.) per `CONCERNS.md` security-considerations note. Out-of-bounds => use default for that field, log, continue.
- Unit test the round-trip: write default config, read it back, assert equality. Add a "legacy format" test case with the current (possibly non-conformant) output as input.

**Warning signs:**
- Testers upgrading from 0.x report MicMap fails to start silently or with a console stack trace.
- Fresh install works; upgrade install crashes.
- `config.json` on tester machines fails a command-line `python -m json.tool` check.

**Phase to address:**
Config read-back phase (CFG-01). Same PR as the read implementation — don't separate reading and defensive error handling into different phases.

---

### Pitfall 10: Uninstaller fails because Steam / SteamVR was uninstalled first

**What goes wrong:**
User uninstalls Steam. That removes `steamapps\common\SteamVR\bin\win64\vrpathreg.exe`. Later they uninstall MicMap. The uninstaller tries to run `vrpathreg removedriver` and either errors out (blocking uninstall), or silently continues but leaves a stale entry in `openvrpaths.vrpath` (which Steam's uninstall doesn't clean). If they later reinstall SteamVR, it discovers a driver path that doesn't exist.

**Why it happens:**
bey-closer-t1 sidestepped this: its installer set `Uninstallable=no` (temporary distribution). MicMap needs a real uninstaller. The dependency on `vrpathreg.exe` being present is fragile because Steam/SteamVR are independently manageable.

**How to avoid:**
- In the uninstaller's Pascal Script cleanup:
  1. Use `FileExists` to check for `vrpathreg.exe` at `{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe`. If present, call `removedriver`. If absent, skip — not an error.
  2. Directly edit `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` as a fallback: it's a JSON file, remove the MicMap entry from the `external_drivers` array.
  3. Remove MicMap's `app.vrmanifest` registration via `vr::VRApplications()->RemoveApplicationManifest` — but this requires an OpenVR runtime. If OpenVR isn't available either, just delete the manifest file and let SteamVR discover the stale registration is invalid on next start.
- Also handle "Steam not installed at all": use the PathFromRegistry / `{autopf}` discovery. If Steam is not found by either, log, skip, continue — uninstaller must always complete.

**Warning signs:**
- Uninstall succeeds but leaves entries in `openvrpaths.vrpath`.
- `vrpathreg show` (after a fresh SteamVR reinstall) lists MicMap pointing to a non-existent path.
- Reinstalling MicMap produces duplicate entries because the uninstall didn't fully remove the old one.

**Phase to address:**
Installer phase (INST-01). A real uninstaller story is the biggest delta from the bey-closer-t1 reference — budget time for it.

---

### Pitfall 11: Driver logging invisible because `DriverLog` wasn't wired up

**What goes wrong:**
The sidecar migration is in progress. Driver isn't triggering inputs as expected. Developer goes to check `vrserver.txt` for the driver's own log output and finds... nothing. No initialization messages, no RunFrame traces, no error codes from `CreateBooleanComponent`. Debugging is blind.

**Why it happens:**
In an OpenVR driver, `DriverLog` / `vr::VRDriverLog()->Log` is the standard facility, but it requires `IVRDriverContext` being set up in `HmdDriverFactory`. If the driver initializes the context incorrectly (missing VR_INIT_SERVER_DRIVER_CONTEXT macro), logs silently go nowhere. Alternatively, logs may go to a separate file (e.g., `vrserver.txt` vs `driver_<name>.txt`) depending on driver version and SteamVR settings. bey-closer-t1's retrospective mentions PowerShell verification scripts that parse `vrserver.txt` — implying that's the right log in their environment, but this is worth confirming for MicMap.

**How to avoid:**
- In the driver's main entry (`HmdDriverFactory`), verify `VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext)` is called and its return is checked.
- At the very start of `DeviceProvider::Init`, emit a distinctive log line: `DriverLog("MicMap driver v<git-hash> init\n")`. If that line doesn't appear in `vrserver.txt` (or wherever your logs land) after a clean restart, the context isn't set up.
- Add a structured log format: every meaningful error code gets logged with the OpenVR enum name (`vr::VRInputError_WrongType`), not a raw integer.
- During the refactor, test first trigger with full driver log stream visible. Keep a tail on `%LOCALAPPDATA%\Steam\logs\vrserver.txt` open.

**Warning signs:**
- Code path clearly executed but log file is empty or missing the expected line.
- The driver's init lines appear but later RunFrame logs don't — may indicate the driver crashed silently.
- `vrserver.txt` shows other drivers logging but not MicMap.

**Phase to address:**
Driver refactor phase (SVR-01). Set up logging on day one of the refactor or you will waste cycles blind-debugging.

---

## Moderate Pitfalls

### Pitfall 12: `RunFrame` runs on every vrserver frame — ~90-120Hz is a hot path

**What goes wrong:**
Developer adds blocking work to `RunFrame`: file I/O, HTTP polling, JSON parse of incoming commands. vrserver frame time spikes. Compositor misses, dashboard lags, users report judder.

**Why it happens:**
The driver's `RunFrame()` runs on vrserver's main pump. Any blocking or slow work there directly stalls vrserver. bey-closer-t1's Phase 3 research flagged this explicitly ("RunFrame() must return quickly (~11ms budget). Use PIPE_NOWAIT, never blocking ConnectNamedPipe or ReadFile").

**How to avoid:**
- `RunFrame` does only non-blocking check/update operations: poll event queue, poll HTTP trigger flag (set by a background thread), call `UpdateBooleanComponent`, poll HMD container handle validity. Everything else on a worker thread.
- The HTTP bridge server (driver side) runs on its own thread — do not service HTTP requests from `RunFrame`.
- Measure: in dev builds, time each `RunFrame` iteration. Assert `< 1ms`. Log if exceeded.

**Warning signs:**
- Compositor stats (Ctrl+Shift+G in SteamVR) show frame drops.
- Performance graph shows vrserver CPU time spikes correlated with MicMap triggers.

**Phase to address:**
Driver refactor phase (SVR-03). Carry over the non-blocking discipline from existing code.

---

### Pitfall 13: The duplicate-path trick breaks if SteamVR tightens cross-driver component permissions

**What goes wrong:**
A future SteamVR update adds permission checks to `CreateBooleanComponent` rejecting cross-driver creation on the HMD container. MicMap users on that SteamVR version stop working with no indicator beyond `VRInputError_PermissionDenied`.

**Why it happens:**
`HMD Button Stub.md` explicitly calls this out as the project's core external dependency: "Sidecar pattern requires SteamVR's acceptance of duplicate-path components on the HMD container." This is not officially documented behavior — it's empirically validated on SteamVR March 2026 + OpenVR SDK v2.5.1. Valve owes no backwards compat on undocumented behavior.

**How to avoid:**
- This is a risk to accept and monitor, not to prevent at the architecture level. Document it in the project README under "Known Risks."
- Handle the error gracefully: if `CreateBooleanComponent` returns `VRInputError_PermissionDenied`, emit a user-visible notification ("SteamVR compatibility changed — MicMap needs an update") and run in a degraded mode. Don't crash, don't silently stop working.
- Monitor SteamVR changelogs for input-system changes. Add a version-check against `vr::IVRSystem::GetRuntimeVersion` — if running on a version newer than the last tested version, log a warning.
- In the telemetry-if-you-have-it or log format, include the error code clearly so diagnosis is fast.

**Warning signs:**
- `CreateBooleanComponent` returns an error other than `VRInputError_InvalidParam` (already documented) — specifically `VRInputError_PermissionDenied` or `VRInputError_AlreadyInUse`.
- SteamVR beta branch testers report MicMap stopped working.

**Phase to address:**
Driver refactor phase (SVR-02). Implement the graceful-degradation path in the same place as the creation logic.

---

### Pitfall 14: Multiple sidecars contending on `/input/system/click`

**What goes wrong:**
User has MicMap installed AND another sidecar (a foot-pedal driver, a gesture driver) both trying to inject `/input/system/click` on the HMD. Each driver has its own component on the same path. SteamVR multiplexes them, but debouncing / priority is undefined. Edge cases: one driver holds it true, the other releases it, state stays true; or rapid alternation from both drivers triggers SteamVR to enter/exit the dashboard multiple times.

**Why it happens:**
`HMD Button Stub.md` states "duplicate path names are allowed" — SteamVR accepts multiple components at the same path from different drivers. The propagation rule isn't documented: is it OR, AND, last-write-wins, or something else? Undefined is the documented behavior.

**How to avoid:**
- Low priority for this milestone — MicMap is probably the only sidecar on a given user's machine. But:
- Log the error from `CreateBooleanComponent` clearly if it's something other than `InvalidParam` / `None` — a future SteamVR might reject duplicates outright.
- In the driver's log, note when MicMap transitions `/input/system/click` to true and to false, so that conflict debugging has data if reported.
- Document "only one mic-cover-style sidecar at a time" as a known limitation in README.

**Warning signs:**
- Rare user report of "dashboard flickers" or "dashboard opens and immediately closes."
- User has known conflicting sidecar installed (OpenVR-AdvancedSettings has its own event injection, but at a different level).

**Phase to address:**
Deferred — document as known limitation. Address in a future milestone if real user reports emerge.

---

### Pitfall 15: Installer writes to `{app}` but the driver needs a subdirectory structure

**What goes wrong:**
Inno Setup `DefaultDirName={autopf}\MicMap`, writes `driver_micmap.dll` directly to `{app}\`. But SteamVR expects the structure `<driver-root>\bin\win64\driver_<name>.dll` + `<driver-root>\driver.vrdrivermanifest`. Driver loads to wrong path or vrpathreg'd path doesn't have the manifest, enumeration fails.

**Why it happens:**
SteamVR driver directory structure is a convention, not obvious from the installer's point of view. bey-closer-t1's `[Dirs]` section explicitly creates `bin\BeyondProximity\bin\win64\` to match. MicMap's installer needs the equivalent layout at `{app}\bin\win64\` + `{app}\driver.vrdrivermanifest`.

**How to avoid:**
- `[Dirs]` section: `Name: "{app}\bin\win64"`.
- `[Files]` section: driver DLL -> `{app}\bin\win64\driver_micmap.dll`, manifest -> `{app}\driver.vrdrivermanifest`.
- `[Run]` section: `vrpathreg adddriver "{app}"` (NOT `"{app}\bin\win64"` — the driver root is the path containing the manifest).
- Smoke test: after install, shell out to `vrpathreg show` and grep for MicMap. Confirm the registered path is `{app}`.

**Warning signs:**
- SteamVR > Settings > Startup > Add-ons does not list MicMap after install.
- `vrserver.txt` logs `driver manifest not found at <path>`.
- Installer completes but triggering does nothing.

**Phase to address:**
Installer phase (INST-01). Catch in installer-smoke-test, before release.

---

### Pitfall 16: Inno Setup 6 Pascal Script gotchas that bit bey-closer-t1

**What goes wrong (verbatim from bey-closer-t1 RETROSPECTIVE.md):**
1. "Inno Setup preprocessor runs on raw text including [Code] sections — `#N` character constants at line start are misinterpreted as directives" — e.g., `#9` (tab) at line-start looked like a preprocessor `#9` directive.
2. "Inno Setup 6 Pascal Script uses AnsiString for file I/O functions but String for string manipulation — need explicit casts."

**Why it happens:**
Both are genuinely subtle, both bit bey-closer-t1 during the installer phase. They are NOT documented in top-of-page Inno Setup docs. They are footnotes in the Pascal Scripting help.

**How to avoid:**
- For `#N` character constants inside `[Code]` strings: use concatenation so `#` is not at line-start. Example: `S := #13 + #10;` rather than the literal string-build across lines with bare `#13#10` at line-start.
- For file I/O: `LoadStringFromFile` signature takes `AnsiString` parameter. When doing `Pos` / `StringChangeEx` after, cast: `Content := String(RawContent);` and back: `SaveStringToFile(path, AnsiString(Content), False);`. See `BeyondProximity.iss` lines 143-184 for the reference pattern.
- Copy the `IsProcessRunning`, `NextButtonClick`, `RestoreRootManifest`, `RestoreVrresources` functions from `BeyondProximity.iss` as the starting point and adapt. Don't rewrite from the Inno Setup docs alone.

**Warning signs:**
- ISCC compilation errors like "Unknown directive: 9" or type-mismatch errors on string assignment.
- Pascal Script runtime errors on install about "string conversion" or "invalid variant".

**Phase to address:**
Installer phase (INST-01). Budget half a day for tripping over these even with the bey-closer-t1 reference — the preprocessor issue in particular shows up only when you edit the script.

---

### Pitfall 17: HTTP localhost port collision on non-default port

**What goes wrong:**
MicMap app <-> driver HTTP bridge binds to a default port (e.g., 39500). Another app on the user's machine has already claimed that port. Driver-side server fails to bind, silently; app-side client connects to the wrong process (or fails to connect); detection triggers go nowhere.

**Why it happens:**
`CONCERNS.md` notes "port scanning fallback" exists for the HTTP client, implying port contention is already a known class of bug. Driver side may not have the same fallback.

**How to avoid:**
- Driver side: on `bind()` failure, try next port. Write the chosen port to a file in `%APPDATA%\MicMap\driver.port` (atomic write) so the app can discover it.
- App side: read the port file on startup, fall back to scan if file is missing or stale.
- On bind success, log the port number. On shutdown, clean up the port file.
- Out of scope to REPLACE the HTTP IPC this milestone per PROJECT.md — but port-contention hardening is a 2-hour win.

**Warning signs:**
- Detection triggers silently do nothing — app logs say "HTTP POST succeeded" but driver never receives.
- Logs show `EADDRINUSE` / `WSAEADDRINUSE` on driver init.

**Phase to address:**
Deferred to a polish phase or the driver refactor (SVR-03) if cheap. Not blocking for milestone.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Cache the HMD property container handle once in `Init` instead of polling in `RunFrame` | Less code | Fails on HMD reactivation (Pitfall 1), breaks the entire milestone for users who take the headset off | Never |
| Use `vrpathreg adddriver` without checking for existing registration | Simpler installer script | Duplicate entries on reinstall (Pitfall 3), weird driver behavior, hard to diagnose | Never |
| Ship relative paths in `app.vrmanifest` | Manifest is portable | Auto-launch silently breaks on install-location changes (Pitfall 5) | Never for installed builds; fine for dev-loop |
| Skip `VREvent_Quit` handling in the MicMap app | Faster shipping | Shutdown loop (Pitfall 2), frustrated users | Never once auto-launch is shipped |
| Keep the virtual-controller code behind a feature flag | Reversible migration | Dead code paths, compile-time bloat, regression risk; PROJECT.md explicitly forbids | Never (see PROJECT.md, SVR-04) |
| Use `fprintf`/manual JSON writing and parse with nlohmann | Incremental migration | Trailing comma / NaN / format divergence -> startup crash (Pitfall 9) | Only during an intra-PR transition; not across releases |
| Log errors as raw integers | Slightly faster | Debug sessions wasted mapping error codes to OpenVR enum names | Only in inner loops; init/error paths always use enum names |
| Skip the Inno Setup uninstaller entirely (like bey-closer-t1 did with `Uninstallable=no`) | Simpler installer | Users cannot cleanly uninstall, vrpathreg entries accumulate forever | Acceptable for internal alpha; NOT for public release |

---

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| OpenVR driver context | Forgetting `VR_INIT_SERVER_DRIVER_CONTEXT` in `HmdDriverFactory` | Call the macro and check return; emit a log line at Init to verify (Pitfall 11) |
| `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` | Calling at `Init()` time | Defer to `RunFrame()` and retry until non-invalid (`HMD Button Stub.md` step 2) |
| `CreateBooleanComponent` cross-driver | Attempting `UpdateBooleanComponent` on another driver's handle (fails with `VRInputError_WrongType`) | Always `CreateBooleanComponent` first; update only handles you created (`HMD Button Stub.md`) |
| `AddApplicationManifest` + `SetApplicationAutoLaunch` | Calling both back-to-back — second returns `VRApplicationError_UnknownApplication` | Add manifest, poll `IsApplicationInstalled(appKey)` for up to 2s, then `SetApplicationAutoLaunch` (OpenVR issue #1378) |
| `vrpathreg adddriver` | Running without a prior `removedriver` or `finddriver` check | `removedriver` first (no-op if absent), then `adddriver` (Pitfall 3) |
| Inno Setup file replacement under Windows file-lock | Checking only `vrserver.exe` for the lock | Check all of vrserver, vrmonitor, vrcompositor, vrdashboard, vrwebhelper; also use `restartreplace` flag (Pitfall 4) |
| `IVRSystem::PollNextEvent` on app side | Not calling it, so `VREvent_Quit` is missed | Pump events in the app's main loop; handle Quit explicitly (Pitfall 2) |
| `DriverLog` | Calling before the context is init'd | Guard with a bool; only log after Init succeeds |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Blocking I/O in `RunFrame` | vrserver frame drops, compositor judder | All I/O off `RunFrame`; use lock-free flags updated by worker threads | First user who has slower storage or a noisy network |
| `UpdateBooleanComponent` on every `RunFrame` tick regardless of state change | Log spam, minor CPU, possible SteamVR-side debouncing weirdness | Only update when state changes (track last-written value) | Noticeable in dense trigger sequences (training false positives) |
| Repeated JSON parse of config file | Startup latency | Parse once at init, hold parsed config in memory | User with 100+ device entries in config |
| HTTP client re-resolving `127.0.0.1` on every request | Micro-latency on triggers | Keep-alive connection or cache the resolved address | Never in practice for localhost; flag only if `cpp-httplib` is misconfigured |
| Per-frame string formatting for log output | vrserver frame time creep | `DriverLog` is rate-limited in hot paths; use a level check | High-verbosity debug builds only |

---

## Security Mistakes

(MicMap is a local-only Windows installed app. Domain-specific security concerns beyond OWASP):

| Mistake | Risk | Prevention |
|---------|------|------------|
| HTTP server in the driver binds `0.0.0.0` instead of `127.0.0.1` | Any LAN/WAN host can trigger dashboard events remotely | Explicit `127.0.0.1` bind in the driver's HTTP server; verified with a smoke test that tries connecting from another host's IP |
| No auth on the HTTP bridge | Any local process (malware, other apps) can trigger dashboard events | For this milestone, accept — localhost-only is the mitigation per `CONCERNS.md`. Long-term: per-boot shared secret written to a user-owned file both processes read |
| Installer runs with admin, writes untrusted content to `{app}` | N/A here because the installer only writes files from its own package — but be disciplined about it | Do NOT fetch or process untrusted input during install (no "download latest driver" patterns) |
| JSON config parsing with no bounds checks | Config-file tampering -> bad driver state | Bounds-check every numeric field on read (Pitfall 9); treat `config.json` as untrusted user input even though it's user-owned |
| Installer writes to `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` directly without backup | Corrupting this file breaks all of SteamVR | Always backup before edit; prefer `vrpathreg` when available |

---

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Installer succeeds silently when SteamVR is running but driver isn't replaced | User thinks upgrade happened; old driver still loaded | `PrepareToInstall` blocks with a clear message naming the running processes (Pitfall 4) |
| Auto-launch doesn't fire on first SteamVR start after install | User has to manually launch micmap.exe — defeating the point | Manifest registration + autolaunch flag in the installer; smoke-test post-install by restarting SteamVR (Pitfall 5) |
| Detection trigger does nothing with no user feedback | User thinks app is broken | App-side tray icon / log message "trigger sent", driver-side log "input updated"; app monitors driver ACK and surfaces errors to the tray |
| Upgrading leaves a phantom controller in Devices | User sees "MicMap Controller — lost" forever | Uninstaller/installer clean up old device bindings (Pitfall 8) |
| `driver.vrdrivermanifest` has `"alwaysActivate" : false` | Driver doesn't load without explicit enable in Add-ons UI | Set `alwaysActivate : true` so the driver runs immediately after install; verify in the manifest before shipping |
| No visible "MicMap active" indicator while in VR | User doesn't know if cover-mic is armed | Out of scope per PROJECT.md (overlay stubs deferred); but document this as known UX gap |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces. Run this before declaring the milestone done.

- [ ] **Sidecar driver loads:** `driver.vrdrivermanifest` parses cleanly, `alwaysActivate : true`, driver appears in Settings > Startup > Add-ons with a non-empty name. (Pitfall 6, 15)
- [ ] **HMD component survives deactivation:** Put the headset on, trigger. Take the headset off. Wait 30 seconds. Put it back on. Trigger. Second trigger works. (Pitfall 1)
- [ ] **Auto-launch round-trip:** Fresh install. Launch SteamVR. MicMap auto-launches. Quit SteamVR. MicMap also quits (not just daemonized). Relaunch SteamVR. MicMap auto-launches again. (Pitfall 2, 5)
- [ ] **Install while SteamVR running:** Installer blocks with a clear message. User closes SteamVR, clicks retry (or re-runs installer), install succeeds. (Pitfall 4)
- [ ] **Upgrade from 0.x:** Install old MicMap (virtual-controller version), run it, now install new MicMap over top. Old controller disappears. Auto-launch works. No duplicate vrpathreg entries. (Pitfall 3, 8)
- [ ] **Uninstall + reinstall:** Uninstall MicMap. `vrpathreg show` no longer lists it. `openvrpaths.vrpath` cleaned. Reinstall. Works cleanly. (Pitfall 3, 10)
- [ ] **Uninstall with SteamVR absent:** Uninstall Steam. Uninstall MicMap. Uninstaller completes without errors. (Pitfall 10)
- [ ] **Config round-trip:** Change all settings, quit app, restart app. All settings preserved. Additionally: manually corrupt `config.json` (add a trailing comma), restart app, app starts with defaults and a log warning, original file backed up. (Pitfall 9)
- [ ] **Driver-only logs visible:** Search `vrserver.txt` after a clean SteamVR restart — find MicMap init message, find at least one `RunFrame` periodic log. (Pitfall 11)
- [ ] **No virtual-controller residue:** `grep -ri "VirtualController\|dashboard_open\|TrackedDeviceAdded\|open_vs_select" src/` returns zero results in the driver source. (Pitfall 7)
- [ ] **Driver structure matches SteamVR convention:** `{app}\driver.vrdrivermanifest` exists, `{app}\bin\win64\driver_micmap.dll` exists. (Pitfall 15)
- [ ] **Localhost-only HTTP:** `netstat -an | findstr :<micmap_port>` shows `127.0.0.1:<port>` not `0.0.0.0:<port>`. (Security)
- [ ] **Inno Setup compiles without warnings:** ISCC stdout clean. Pascal Script passes compile-time checks. (Pitfall 16)

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| HMD handle stale after reactivation (Pitfall 1) | LOW if caught in dev; HIGH if shipped | Release a patch with the state-enum lifecycle; tell users "restart SteamVR" as interim workaround |
| Auto-launch shutdown loop (Pitfall 2) | HIGH | Users must disable auto-launch in SteamVR Settings, kill MicMap in Task Manager, uninstall, install patched version that handles `VREvent_Quit` |
| Duplicate vrpathreg entries (Pitfall 3) | LOW | `vrpathreg removedriver <path>` until entries gone, then reinstall |
| Install while helpers still running (Pitfall 4) | LOW-MEDIUM | User fully logs out / reboots, reinstalls. No silent data corruption, just skipped DLL replacement |
| Bad manifest paths (Pitfall 5) | MEDIUM | Uninstall, reinstall; or patch manifest on disk; or run app with `--force-install-manifest`-equivalent to re-register |
| Missing manifest (Pitfall 6) | LOW | Reinstall or manually copy `driver.vrdrivermanifest` into the driver root |
| Virtual-controller crash residue (Pitfall 7) | MEDIUM | Patch release removing the callsite; users in the meantime: crash when first trigger fires |
| Ghost controller bindings (Pitfall 8) | LOW | Manual: Settings > Devices > forget; Automated: installer-side cleanup |
| Config-parse crash (Pitfall 9) | MEDIUM | User deletes `config.json`; we ship patch with try/catch + backup logic |
| Uninstaller breaks with Steam gone (Pitfall 10) | LOW | Manual file deletion; patch the uninstaller to be defensive |
| Logs invisible (Pitfall 11) | LOW | Check the VR_INIT_SERVER_DRIVER_CONTEXT macro; check writeability of the log path |

---

## Pitfall-to-Phase Mapping

Map each pitfall to a roadmap phase that prevents it, with a verification hook.

| # | Pitfall | Prevention Phase | Verification |
|---|---------|------------------|--------------|
| 1 | HMD handle stale on reactivation | SVR-02 (defer + re-init on deactivate event) | Manual UAT: take headset off/on between triggers |
| 2 | Auto-launch shutdown loop | AUTO-01 (VREvent_Quit handling in app) | Manual UAT: quit SteamVR, confirm MicMap exits; relaunch SteamVR, confirm auto-launch |
| 3 | Double vrpathreg adddriver | INST-01 (removedriver-before-adddriver) | Smoke script reads `openvrpaths.vrpath`, asserts single entry |
| 4 | File-locked DLL replace | INST-01 (multi-process check + restartreplace) | Manual UAT: install with SteamVR running; verify block |
| 5 | Manifest absolute/relative paths | AUTO-01 + INST-02 (generate absolute paths at install time) | Post-install assertion on manifest file contents |
| 6 | Missing `driver.vrdrivermanifest` | SVR-01 (manifest is checked-in) + INST-01 (verified copied) | Installer smoke test asserts file exists and parses |
| 7 | Virtual-controller dangling code | SVR-04 (explicit rip-out checklist) | `grep -r` across source for removed vocabulary; crash-test first trigger |
| 8 | Cached controller bindings | INST-01 (upgrade-path cleanup) | UAT on upgrade-from-0.x |
| 9 | JSON read-back crash on legacy format | CFG-01 (try/catch + backup + defaults) | Unit test with legacy-format fixture + corrupt-file fixture |
| 10 | Uninstaller fails with Steam absent | INST-01 (defensive uninstaller) | UAT: uninstall Steam first, then MicMap |
| 11 | DriverLog invisible | SVR-01 (init context + init log line) | First run of new driver: observe log line in vrserver.txt |
| 12 | Blocking RunFrame | SVR-03 (non-blocking trigger path) | Dev-build timing assert `RunFrame < 1ms` |
| 13 | SteamVR tightens cross-driver permissions | SVR-02 (graceful-degradation path) | Log-format includes full error-code name; document risk |
| 14 | Multiple sidecars on same path | Deferred / documented | README known-limitations section |
| 15 | Driver directory layout | INST-01 (correct `[Dirs]` + `[Files]` + vrpathreg target) | Installer smoke script: assert `vrpathreg show` lists `{app}` path |
| 16 | Inno Setup 6 Pascal Script gotchas | INST-01 (copy pattern from bey-closer-t1) | Installer builds clean with zero ISCC warnings |
| 17 | HTTP port collision | Deferred / polish | Only if real user reports; port-write-to-file pattern |

---

## Phase-Ordering Implications

Based on the pitfall dependencies, the recommended phase order is:

1. **Driver refactor (SVR-01 through SVR-04)** — first, because the sidecar + HMD-component work is the riskiest, and downstream work (auto-start, installer) depends on having a driver that loads cleanly. Within this phase, the order is: (a) set up logging (Pitfall 11) and manifest (Pitfall 6), (b) implement deferred component creation (builds on `HMD Button Stub.md`), (c) add reactivation lifecycle (Pitfall 1), (d) wire HTTP trigger -> `UpdateBooleanComponent` (SVR-03), (e) rip out virtual-controller code last (Pitfall 7).

2. **Config read-back (CFG-01)** — independent of driver work. Can be done in parallel. Own PR.

3. **Auto-start (AUTO-01)** — depends on the new driver being live and its loading confirmed via the driver phase. Auto-start cannot be tested in isolation because a vrmanifest-registered app that doesn't do anything is useless.

4. **Installer (INST-01, INST-02)** — last. The installer packages the results of phases 1-3. All pitfalls 3, 4, 8, 10, 15, 16 live here. INST-02 is a small add-on (manifest registration during install), bundled with INST-01.

5. **Documentation (DOC-01)** — after installer, because the README is describing the shipped behavior.

**Phases that should have a research-spike flag in the roadmap:**

- Driver refactor (SVR-02 in particular) — Pitfall 1 (HMD reactivation lifecycle) is a known unknown. bey-closer-t1 did not encounter it because their driver was long-running-proximity, not event-per-trigger. Budget a spike to verify the reactivation event arrives as expected.
- Auto-start (AUTO-01) — Pitfall 2 (shutdown loop) and Pitfall 5 (manifest paths) have non-obvious failure modes. Budget time for manual UAT cycles.

Phases that are standard patterns and should NOT need additional research:

- Config read-back (CFG-01) — nlohmann/json is already vendored; mechanical work.
- Documentation (DOC-01) — writing.

---

## Sources

### Primary (HIGH confidence, prior art)
- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — cross-driver input-injection technique (primary reference for Pitfalls 1, 13, 14)
- `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — Inno Setup reference (primary reference for Pitfalls 3, 4, 10, 15, 16)
- `D:\Documents\Projects\bey-closer-t1\.planning\RETROSPECTIVE.md` — documents Inno Setup 6 Pascal-script gotchas (Pitfall 16)
- `D:\Documents\Projects\bey-closer-t1\.planning\milestones\v1.0-phases\09-installer-for-tester-distribution\09-RESEARCH.md` — WMI process-detection pattern, ISCC integration, manifest lessons
- `D:\Documents\Projects\bey-closer-t1\.planning\milestones\v1.0-phases\03-proximity-component-spike\03-RESEARCH.md` — `RunFrame` budget, non-blocking discipline, event system (Pitfalls 1, 12)
- `D:\Documents\Projects\bey-closer-t1\.planning\milestones\v1.0-phases\03.1-remove-virtual-tracker\03.1-CONTEXT.md` — rip-out-virtual-device checklist (Pitfall 7)
- `D:\Documents\Projects\mic-map\.planning\PROJECT.md` — milestone scope and constraints
- `D:\Documents\Projects\mic-map\.planning\codebase\CONCERNS.md` — existing tech debt, config read-back is stubbed, HTTP bridge security notes

### Primary (HIGH confidence, official)
- [OpenVR wiki: Local Driver Registration](https://github.com/ValveSoftware/openvr/wiki/Local-Driver-Registration) — `vrpathreg` commands, removedriver pattern (Pitfalls 3, 10)
- [OpenVR wiki: Device Sleep States](https://github.com/ValveSoftware/openvr/wiki/Device-sleep-states) — activity-level transitions, proximity component
- OpenVR SDK v2.5.1 header `openvr_driver.h` — `IVRDriverInput`, `IVRServerDriverHost::PollNextEvent`, `VREvent_TrackedDeviceDeactivated`

### Secondary (MEDIUM confidence, issue trackers)
- [OpenVR issue #1378: SetApplicationAutoLaunch fails with VRApplicationError_UnknownApplication after successful AddApplicationManifest](https://github.com/ValveSoftware/openvr/issues/1378) — Pitfall 5
- [OpenVR issue #1425: SteamVR fails to relaunch if a non-Steam background-app launched with it is still running](https://github.com/ValveSoftware/openvr/issues/1425) — Pitfall 2
- [OpenVR issue #1597: Custom driver crash when launching any game](https://github.com/ValveSoftware/openvr/issues/1597) — Pitfall 7 (crash symptoms from dangling driver code)
- [OpenVR issue #1653: vrpathreg allows to register external drivers with null path](https://github.com/ValveSoftware/openvr/issues/1653) — Pitfall 3 (dedupe is caller's job)
- [Steam Community: Autostarting VR Tools on Steam VR Start](https://steamcommunity.com/sharedfiles/filedetails/?id=3035381763) — `--force-install-manifest` pattern
- [VRChat Face Tracking issue #227: auto-launch not firing](https://github.com/benaclejames/VRCFaceTracking/issues/227) — autostart failure modes observed in the wild

### Tertiary (LOW confidence, unverified)
- Steam Community threads on `vrserver.exe` crashes and `0xc0000005` access violations — correlate with driver-side dangling references (Pitfall 7)
- WebSearch-only: claims about app_type "overlay" lifecycle expectations — validated against OpenVR issue #1425 but not against official docs directly

---

*Pitfalls research for: MicMap "Seamless SteamVR Integration" milestone*
*Researched: 2026-04-22*

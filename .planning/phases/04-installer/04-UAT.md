---
status: complete
phase: 04-installer
source: 04-01-SUMMARY.md, 04-02-SUMMARY.md, 04-03-SUMMARY.md, 04-04-SUMMARY.md, 04-05-SUMMARY.md, 04-06-SUMMARY.md, 04-07-SUMMARY.md, 04-08-SUMMARY.md, 04-09-SUMMARY.md
started: 2026-04-24T00:00:00Z
updated: 2026-04-24T06:30:00Z
---

## Current Test

[testing complete]

## Tests

### 1. Installer Artifact Produced
expected: build/installer/MicMap-Setup-v0.1.0.exe exists, ~2.4 MB (>1 MB), produced by `cmake --build build --target package --config Release`
result: pass

### 2. Installer Launches (UAC + Welcome Wizard)
expected: Double-click MicMap-Setup-v0.1.0.exe → Windows UAC elevation prompt → Inno Setup Welcome page appears with MicMap blue-circle "M" icon in titlebar. Cancel at this point leaves system untouched.
result: pass

### 3. SteamVR-Running Gate (INST-02)
expected: Launch SteamVR first, then run installer. Installer shows Retry/Cancel MsgBox body listing running SteamVR processes by name (vrserver.exe, vrmonitor.exe, vrcompositor.exe, vrdashboard.exe, vrwebhelper.exe — whichever are running, comma-separated). Clicking Cancel aborts; no files under {SteamVR}\drivers\micmap\ appear. Clicking Retry after closing SteamVR proceeds to install.
result: pass
retest_notes: "PASS on retest after UAT-GAP-FIX to installer/MicMap.iss (single-quote WQL literals + tasklist fallback + exception logging) and CMakeLists.txt (openvr_api.dll + InstallRequiredSystemLibraries in install() rule). First run: both gate miss and post-install micmap.exe crash observed (see `reported` below). Fixed installer SHA256 988e620471950934ae4461bdafe4640faebf4968c856a6f0a07febb001f9d629, 3,300,589 bytes."
prior_reported: "fail — during Installing phase, two modal dialogs: (1) `micmap.exe - Application Error: The application was unable to start correctly (0xc00004bc). Click OK to close the application.` and (2) aggregator MsgBox `MicMap installed, but some post-install steps failed: micmap.exe --register-vrmanifest (rc=-1073740612); micmap.exe --patch-bindings (rc=-1073740612). MicMap will still work for basic dashboard toggling. If problems persist, see %APPDATA%\\MicMap\\micmap.log and re-run the installer.` The SteamVR-running gate dialog was not observed (install progressed to [Files] copy)."
prior_severity: blocker

### 4. Happy-Path Install (D-01 + D-02 Layout)
expected: With SteamVR closed, run installer through to Finish. Files land at {SteamPath}\steamapps\common\SteamVR\drivers\micmap\ (nested layout): bin\win64\driver_micmap.dll, driver.vrdrivermanifest, resources\, and bin\micmap.exe + bin\app.vrmanifest under the same root. Finished page shows.
result: pass
verified_layout: "{SteamVR}\\drivers\\micmap\\ contains: bin\\micmap.exe + bin\\app.vrmanifest + bin\\openvr_api.dll + bin\\concrt140.dll + bin\\msvcp140{,_1,_2,_atomic_wait,_codecvt_ids}.dll + bin\\vcruntime140{,_1}.dll + bin\\win64\\driver_micmap.dll + driver.vrdrivermanifest + resources\\driver.vrresources + resources\\settings\\default.vrsettings + unins000.exe + unins000.dat"

### 5. Post-Install vrpathreg Registration (INST-03)
expected: After install, running `"{SteamPath}\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" show` lists the MicMap driver path EXACTLY ONCE (no duplicates). Bindings file patched: {SteamVR}\resources\config\... shows MicMap marker; .micmap_backup exists alongside original.
result: pass
verified: "vrpathreg show: single `micmap : c:\\program files (x86)\\steam\\steamapps\\common\\SteamVR\\drivers\\micmap` entry. vrcompositor_bindings_generic_hmd.json.micmap_backup present. lighthouse_hmd_profile.json contains `\"micmap_patched_v2\": true` marker. NOTE: First post-install attempt showed aggregator MsgBox `micmap.exe --patch-bindings (rc=1)` — traced to EnsureControllerTypeFiles returning `anyWritten` (false on idempotent no-op). Fixed in src/bindings/src/bindings_patcher.cpp — now returns success on already-current state. Retested via upgrade-in-place; aggregator no longer fires."

### 6. Auto-Launch + Driver Loads in SteamVR (INST-04)
expected: Start SteamVR fresh. MicMap driver loads without errors (check vrserver.txt for "driver_micmap" load lines, no red errors). Tray icon appears. HMD has /input/system/click component registered (no laser pointer / no virtual controller visible in SteamVR status).
result: pass
retest_notes: "PASS on retest after double-VR_Init fix (commit 3187fbb, `fix(04): eliminate double-VR_Init in manifest retry thread`; debug session `.planning/debug/micmap-startup-segv.md`). SteamVR auto-launches micmap.exe --minimized; tray icon appears and persists across the SteamVR session; driver load clean in vrserver.txt; /input/system/click component registered; no laser beam / no stray virtual controller. Installer rebuilt as build/installer/MicMap-Setup-v0.1.0.exe (SHA256 f2a62d662b833264e588ddb1544a8af3461597ca0c2c766f65dab55917451651)."
prior_reported: "Partial fail — driver side OK, app side broken. (1) Driver-side PASS: vrserver.txt shows clean driver load — `micmap: MicMap driver initializing (sidecar mode)`, `MicMap[patch]: generic_hmd bindings already patched`, `MicMap: /input/system/click created (handle=7)`, `Loaded server driver micmap (IServerTrackedDeviceProvider_004)`. HTTP server bound to 127.0.0.1:27015. No laser beam / no stray virtual controller. (2) App-side FAIL: No tray icon. vrserver.txt shows SteamVR auto-launched micmap.exe --minimized at 05:42:03.419 → process disconnected 974ms later (05:42:04.393). Manual reproduction via bash confirms: `micmap.exe --minimized` crashes (SEGV / exit 139) ~150ms after logging `OpenVR initialized successfully`, so after initialize() returns but before or during SetupSystemTray / packaged_task thread launch / first_launch_balloon / main-loop entry. Crash reproduces identically with no args and on both installed + dev-built micmap.exe. Bisected to 5d59e0b~1 (= bb8879b) — pre-dates the Plan 4 packaged-task hardening. Out of scope for Phase 4 installer UAT; needs /gsd-debug session on micmap.exe startup."
prior_severity: blocker

### 7. Detection Triggers System Button
expected: Cover the mic (or trigger the trained noise pattern). SteamVR dashboard opens/closes as if the HMD system button was pressed. Repeated triggers toggle the dashboard — hands-free, no physical controller needed.
result: pass
retest_notes: "PASS on retest after double-VR_Init fix (commit 3187fbb). Mic cover triggers trained-noise detection; driver client POSTs to 127.0.0.1:27015; driver asserts /input/system/click and SteamVR dashboard toggles hands-free. Repeated covers toggle the dashboard open/close as expected."
prior_blocked_by: "test 6 micmap.exe startup SEGV"

### 8. Upgrade-In-Place (INST-01 / AppId Frozen)
expected: Run MicMap-Setup-v0.1.0.exe a SECOND time on the same machine. Installer detects existing install via frozen AppId GUID, offers upgrade. After second install: Add/Remove Programs shows ONE MicMap entry (not two); `vrpathreg show` still lists MicMap driver EXACTLY ONCE.
result: pass
verified: "Upgrade-in-place exercised when user reran installer to pick up bindings_patcher.cpp fix. Post-upgrade Add/Remove Programs shows single `MicMap version 0.1.0` entry; vrpathreg show lists single `micmap : c:\\program files (x86)\\steam\\steamapps\\common\\SteamVR\\drivers\\micmap` entry. Frozen AppId GUID BC6D91A7-A852-4562-8CBF-58FC4662FEDC working as designed."

### 9. Uninstall + D-13 Data-Retention Prompt (INST-05)
expected: From Add/Remove Programs (or {app}\unins000.exe) start uninstall. MsgBox prompts "Delete user data?" (or similar) with Yes/No — **No is the default** (Enter preserves data). Clicking No keeps %APPDATA%\MicMap\ intact; clicking Yes deletes it. Uninstall teardown runs --unpatch-bindings, --unregister-vrmanifest, vrpathreg removedriver (in that order).
result: pass
retest_notes: "First attempt surfaced blocker: Pascal runtime error `Cannot call \"WizardSilent\" function during Uninstall` (line 459 of PromptAndMaybeRemoveUserData). Fixed by swapping WizardSilent() → UninstallSilent() in installer/MicMap.iss. Pre-fix teardown still completed Steps 1-4 (unpatch-bindings, unregister-vrmanifest, vrpathreg removedriver, SweepLegacyBindings) before halting at Step 5, verified via state probe: vrpathreg had no micmap entries and no micmap markers in bindings JSON post-halt. Post-fix retest via upgrade-in-place (reinstall replaced unins000.exe with patched one) then rerun uninstall: teardown completed end-to-end including D-13 prompt."
prior_reported: "Runtime error (at 46:359): Internal error: Cannot call \"WizardSilent\" function during Uninstall."

### 10. Post-Uninstall Clean State
expected: After uninstall completes, `vrpathreg show` NO LONGER lists MicMap. {SteamVR}\drivers\micmap\ directory removed. SteamVR bindings restored from .micmap_backup (or marker erased). %LOCALAPPDATA%\openvr\input\ contains no micmap_*.json ghost files. Add/Remove Programs no longer shows MicMap.
result: pass
verified: "drivers\\micmap\\ absent; HKLM uninstall regkey absent; vrpathreg show contains no micmap entries; no `micmap_patched` marker in any JSON under SteamVR\\resources\\config (bindings fully restored from backup); no %LOCALAPPDATA%\\openvr\\input\\micmap_*.json ghost files; %APPDATA%\\MicMap\\ empty (user chose Yes on D-13 prompt, exercising the Delete branch). Residual file: .micmap_backup beside vrcompositor_bindings_generic_hmd.json — INTENTIONAL per D-08 write-once + Plan 04-01 D-11 (fs::copy_file, never fs::rename) so reinstall preserves pristine pre-MicMap state."

## Summary

total: 10
passed: 10
issues: 0
pending: 0
skipped: 0
blocked: 0

## Gaps

- truth: "PromptAndMaybeRemoveUserData uses UninstallSilent() during uninstall (not WizardSilent, which is install-only)"
  status: fixed
  reason: "User reported: uninstaller aborted at Step 5 with Pascal runtime error `Runtime error (at 46:359): Internal error: Cannot call \"WizardSilent\" function during Uninstall.`"
  fix_summary: "installer/MicMap.iss PromptAndMaybeRemoveUserData: swapped `WizardSilent()` -> `UninstallSilent()`. WizardSilent is scoped to the install-time wizard state machine and throws at runtime during uninstall; UninstallSilent is the uninstall-time equivalent and returns True for /SILENT or /VERYSILENT, preserving the HR-01 silent-uninstall headless guard. Retested 2026-04-24 via upgrade-in-place (replaces unins000.exe) + rerun uninstall: teardown completed end-to-end including D-13 MsgBox."
  severity: blocker
  test: 9
  artifacts:
    - path: "installer/MicMap.iss"
      issue: "PromptAndMaybeRemoveUserData called WizardSilent() during uninstall"
  missing: []
  debug_session: ""

- truth: "micmap.exe runs to the main message loop when launched by SteamVR auto-launch (or manually with --minimized / no args)"
  status: fixed
  reason: "User reported: no tray icon appeared after SteamVR auto-launched MicMap. Independently confirmed via bash: micmap.exe --minimized (and no-args) exit with SEGV (bash exit 139) within ~150ms of logging `OpenVR initialized successfully`. Reproduces on both installed and dev-built exe. vrserver.txt corroborates: auto-launched process disconnected 974ms after start, with `VR_Init a second time without an intervening VR_Shutdown` logged on exit."
  severity: blocker
  test: 6
  scope: "Out-of-scope for Phase 4 installer. Installer + driver + registration work correctly; bug is in micmap.exe runtime startup code."
  bisect: "Crash reproduces at 5d59e0b~1 (= bb8879b), pre-dates Plan 4."
  suspect_sites:
    - "apps/micmap/main.cpp :: manifestRetryThread body (lines 287-333): root-caused. The WR-01 mitigation thread was calling vr::VR_Init(VRApplication_Utility) while vrInput's VRApplication_Background session was still active."
    - "apps/micmap/main.cpp :: SetupSystemTray(g_app.hwnd) (line 929): ELIMINATED (Shell_NotifyIcon NIM_ADD succeeds; crash is post-OpenVR init, not tray-registration)."
    - "apps/micmap/main.cpp :: packaged_task + initialConnectThread (lines 944-968): ELIMINATED (bisect places crash at 5d59e0b~1, pre-dates Plan 4 packaged-task hardening)."
    - "apps/micmap/main.cpp :: fireBalloonIfFirstSilentLaunch (lines 983-987): ELIMINATED (crash also reproduces with no-args, where flags.minimized=false skips this branch entirely)."
    - "apps/micmap/main.cpp :: main message loop first frame (line 989+): ELIMINATED (crash also reproduces on the minimized-to-tray path which short-circuits ImGui rendering)."
  root_cause: "In-process second VR_Init without an intervening VR_Shutdown. The WR-01 mitigation manifest retry thread (apps/micmap/main.cpp:287-333, introduced at d5e8a49) polls vrInput->isInitialized() on 100ms ticks; the moment vrInput's VR_Init(VRApplication_Background) completes inside the sidecar, the retry thread issues its own VR_Init(VRApplication_Utility) while the Background session is still active. OpenVR rejects the second init, logs 'VR_Init a second time without an intervening VR_Shutdown' in vrserver.txt, and leaves the process in an inconsistent state that SEGVs ~150ms later. The two VR_Init calls are architecturally redundant: vr::VRApplications() is a process-global accessor, so the manifest registrar can AddApplicationManifest / IsApplicationInstalled / SetApplicationAutoLaunch against the already-initialized Background session without any VR_Init of its own (confirmed by manifest_registrar.cpp:325-326 comment)."
  fix_summary: "apps/micmap/main.cpp :: manifestRetryThread body rewritten. Removed the thread's own VR_Init(VRApplication_Utility) and paired VR_Shutdown(). The thread now polls vrInput->isInitialized() on 100ms cancel-responsive ticks for up to 3s and, when Background is ready, calls manifestRegistrar->ensureRegistered() directly (the registrar reuses the in-process Background session via vr::VRApplications()). On any non-Success outcome the thread falls through to the existing 30s cancel-responsive retry sleep. The offline path (SteamVR not running at startup) is unchanged: the main-loop reconnect branch retries vrInput->initialize() on reconnectInterval ticks, and this thread's poll observes the flip whenever that happens. Teardown ordering unchanged (cancel+join at shutdown() step 0, D-14). Rebuild: cmake --build build --config Release --target micmap clean. Headless smoke test: micmap.exe --minimized stays alive past the 2-second observation window (previously died ~150ms into startup)."
  artifacts:
    - path: "apps/micmap/main.cpp"
      issue: "manifestRetryThread body called vr::VR_Init(VRApplication_Utility) while vrInput's VRApplication_Background session was still active"
  missing: []
  verified: "Empirical confirmation on live HMD 2026-04-24: tray icon persists across SteamVR session; mic cover toggles SteamVR dashboard hands-free; repeated covers re-toggle. UAT tests 6 and 7 both PASS."
  debug_session: ".planning/debug/micmap-startup-segv.md"

- truth: "EnsureControllerTypeFiles returns true when target files already carry the current marker (idempotent no-op is success, not failure)"
  status: fixed
  reason: "User reported: post-install aggregator MsgBox shows `micmap.exe --patch-bindings (rc=1)` on a clean install where bindings are objectively patched correctly (vrcompositor_bindings_generic_hmd.json.micmap_backup present; lighthouse_hmd_profile.json contains `micmap_patched_v2: true` marker; generic_hmd.json contains MicMap /actions/lasermouse bindings)."
  fix_summary: "src/bindings/src/bindings_patcher.cpp EnsureControllerTypeFiles return-value semantics corrected. Previous code tracked `anyWritten` and returned it; an already-current file was logged as `already current` but left `anyWritten` false, so a clean idempotent re-run returned false -> PatchGenericHmdBindings returned false -> main.cpp exited 1 -> Exec rc=1 -> aggregator MsgBox. New code tracks `ok` (default true), sets false only on AtomicWriteJson failure or on a non-MicMap file occupying our target filename. Tests test_bindings_patcher + bindings_patcher_idempotent still green. Retested 2026-04-24 via upgrade-in-place; aggregator MsgBox no longer fires."
  severity: major
  test: 5
  artifacts:
    - path: "src/bindings/src/bindings_patcher.cpp"
      issue: "EnsureControllerTypeFiles returned anyWritten (false on idempotent no-op)"
  missing: []
  debug_session: ""

- truth: "WMI gate in PrepareToInstall detects any of the 5 SteamVR processes (vrserver.exe, vrmonitor.exe, vrcompositor.exe, vrdashboard.exe, vrwebhelper.exe) and shows a Retry/Cancel MsgBox that loops until SteamVR is closed"
  status: fixed
  reason: "User reported: SteamVR running detection did not work — installer proceeded past the gate and ran post-install steps even though SteamVR was running. No gate MsgBox was shown."
  fix_summary: "installer/MicMap.iss IsProcessRunning: WQL query now uses single-quoted string literals (was double-quoted, silently returned empty result sets on some Windows builds). Exception path now logs via GetExceptionMessage. Added IsProcessRunningTasklist fallback using cmd /C tasklist /FI \"IMAGENAME eq ...\" /NH so WMI-broken machines still detect running processes. Retested 2026-04-24 — gate fires correctly."
  severity: blocker
  test: 3
  suspect_sites:
    - "installer/MicMap.iss :: PrepareToInstall"
    - "installer/MicMap.iss :: IsProcessRunning (WMI late-bound COM + ProcSet.Count)"
    - "installer/MicMap.iss :: GetRunningSteamVrProcesses (5-name array)"
    - "try/except fail-open default in IsProcessRunning may have swallowed a real exception (per 04-06-SUMMARY.md Pitfall 16 #3)"
  root_cause: "WQL double-quoted string literal Name = \"vrserver.exe\" returned an empty SWbemObjectSet (Count=0) on Windows 11 26200, so IsProcessRunning returned False for every name. WMI did not throw — no exception caught — so the fail-open path was not even exercised; the gate simply saw 'no processes running' and proceeded."
  artifacts:
    - path: "installer/MicMap.iss"
      issue: "IsProcessRunning WQL query used double-quoted string literal"
  missing: []
  debug_session: ""

- truth: "micmap.exe launches cleanly from the installed {app}\\bin\\ location; --register-vrmanifest and --patch-bindings return rc=0 on success"
  status: fixed
  reason: "User reported: `micmap.exe - Application Error: The application was unable to start correctly (0xc00004bc). Click OK to close the application.` followed by post-install aggregator MsgBox reporting `micmap.exe --register-vrmanifest (rc=-1073740612)` and `micmap.exe --patch-bindings (rc=-1073740612)`. 0xc00004bc is STATUS_SXS_IDENTITIES_DIFFERENT / side-by-side resolution failure; rc=-1073740612 (0xC0000142) is STATUS_DLL_INIT_FAILED. Both indicate a missing runtime or transitive dependency DLL in the {app}\\bin\\ stage."
  fix_summary: "CMakeLists.txt root install() rules now ship openvr_api.dll (found via OpenVR::openvr_api IMPORTED_LOCATION) and MSVC runtime DLLs (msvcp140*.dll, vcruntime140*.dll, concrt140.dll) via include(InstallRequiredSystemLibraries) with CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION=bin. installer/MicMap.iss [Files] adds `{#STAGE_DIR}\\bin\\*.dll` wildcard to pack the new DLLs. Retested 2026-04-24 — install completes without crash; post-install aggregator MsgBox not shown (all 3 Exec steps returned rc=0 implied by pass on test 3 retest)."
  severity: blocker
  test: 3
  suspect_sites:
    - "apps/micmap/CMakeLists.txt install() rule ships micmap.exe but NOT openvr_api.dll / VC runtime DLLs"
    - "Plan 03 build/stage/bin/ layout: only micmap.exe + app.vrmanifest (no openvr_api.dll, no msvcp140.dll, no vcruntime140*.dll, no d3dcompiler_47.dll)"
    - "Compare to build/bin/Release/ which DOES have openvr_api.dll copied via src/steamvr/CMakeLists.txt POST_BUILD (04-04-SUMMARY §Issues Encountered #2)"
    - "Possible VC redist not installed on target machine AND not bundled in installer"
  user_guidance: "Sister project bey-closer-t1 should be used as the reference for how OpenVR is wired. Investigation: bey-closer-t1 driver DLL (driver_BeyondProximity.dll) uses standard import-lib linkage and relies on vrserver to pre-load openvr_api.dll; the standalone exe (beyond_spike_monitor.exe) POST_BUILD-copies openvr_api.dll alongside the exe (CMakeLists.txt:148-156). Their Inno script ships only the driver, not the standalone exe — so no installer-time OpenVR shipping concern exists in bey-closer-t1. For mic-map, the standalone micmap.exe IS shipped, so either (a) POST_BUILD-copy openvr_api.dll + add to install(), matching bey-closer-t1's spike-monitor pattern, or (b) /DELAYLOAD:openvr_api.dll + runtime LoadLibrary from {SteamVR}\\bin\\win64. The 0xc00004bc (STATUS_SXS) is distinct from the OpenVR issue and indicates VC runtime / side-by-side assembly — Plan 03's install() also needs to carry msvcp140.dll + vcruntime140.dll + vcruntime140_1.dll (or bundle a VC redist installer invocation), since target machines may not have the matching VC redist installed."
  root_cause: ""
  artifacts: []
  missing: []
  debug_session: ""

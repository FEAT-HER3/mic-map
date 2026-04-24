---
phase: 04-installer
verified: 2026-04-24T12:00:00Z
status: human_needed
score: 9/11 must-haves verified
overrides_applied: 0
gaps:
  - truth: "Uninstaller data-retention prompt does not hang headless/silent uninstall (INST-05 runtime quality)"
    status: failed
    reason: "PromptAndMaybeRemoveUserData calls MsgBox unconditionally — no WizardSilent() guard. During /SILENT or /VERYSILENT uninstall the dialog is NOT suppressed by Inno Setup's silent flags and will block indefinitely. Review finding HR-01 in 04-REVIEW.md identifies the exact fix (add 'if WizardSilent() then begin Log(...); Exit; end' guard). This is a behavioral defect in the committed code, not a wiring gap."
    artifacts:
      - path: "installer/MicMap.iss"
        issue: "PromptAndMaybeRemoveUserData (lines 358-391) calls MsgBox with no WizardSilent() guard — hangs on /SILENT uninstall"
    missing:
      - "Add 'if WizardSilent() then begin Log(''Silent uninstall: keeping user data at '' + AppDataDir); Exit; end' before the MsgBox call in PromptAndMaybeRemoveUserData"
  - truth: "vrpathreg removedriver runs during uninstall (INST-05 symmetric teardown)"
    status: failed
    reason: "GetVrpathreg() returns g_SteamVRDir + '\\bin\\win64\\vrpathreg.exe', but g_SteamVRDir is populated only by InitializeSetup() which does NOT run during uninstall. At uninstall time g_SteamVRDir is the empty string, so GetVrpathreg('') returns '\\bin\\win64\\vrpathreg.exe' (no root). VrpathregExists() calls FileExists on that relative path — returns False on any sane machine. Result: vrpathreg removedriver is silently skipped, leaving a stale driver entry in openvrpaths.vrpath that SteamVR logs errors about on every subsequent startup. Review finding MR-01 in 04-REVIEW.md provides two correct fix approaches."
    artifacts:
      - path: "installer/MicMap.iss"
        issue: "GetVrpathreg (line 227-232) references g_SteamVRDir which is empty at uninstall time — VrpathregExists() always returns False during uninstall, so vrpathreg removedriver is never invoked"
    missing:
      - "Either re-call GetSteamPath() and reconstruct g_SteamVRDir at the top of CurUninstallStepChanged, OR derive SteamVR root from {app} (three ExtractFilePath levels up from AppDir)"
human_verification:
  - test: "Clean VM install then uninstall: vrpathreg show and openvrpaths.vrpath are clean post-uninstall"
    expected: "vrpathreg show lists no MicMap entry; openvrpaths.vrpath contains no reference to drivers/micmap after uninstall completes"
    why_human: "Requires a real or VM SteamVR environment; cannot verify programmatically that vrpathreg removedriver actually executes and persists"
  - test: "Running-SteamVR gate: launch SteamVR then double-click installer; dialog names the running process(es) and loops correctly on Retry"
    expected: "MsgBox appears naming e.g. 'vrserver.exe, vrmonitor.exe'; [Retry] re-checks; [Cancel] aborts with no partial install"
    why_human: "Interactive dialog behavior requires a live SteamVR session; WMI COM path cannot be tested via grep"
  - test: "MicMap auto-launches after clean install: launch SteamVR post-install, confirm MicMap starts silently"
    expected: "MicMap appears in tray; mic-cover triggers dashboard; no visible laser beam; no console window"
    why_human: "End-to-end install-to-use validation requires real HMD + SteamVR; validates INST-01/03/04 at runtime"
  - test: "Upgrade-in-place from same version: run installer twice; vrpathreg show shows exactly one MicMap entry"
    expected: "removedriver-before-adddriver prevents duplicate entries; single entry in openvrpaths.vrpath after re-install"
    why_human: "Requires live vrpathreg output; validates INST-03 removedriver ordering at runtime (ROADMAP SC #3 partial)"
  - test: "D-13 data-retention prompt: uninstall with training_data.bin present; 'No' branch preserves data, 'Yes' branch removes it"
    expected: "Prompt appears with 'No' as default-focused button; Enter key preserves data; clicking 'Yes' removes %APPDATA%\\MicMap\\"
    why_human: "Interactive uninstall dialog — cannot automate without a real install context"
  - test: "cmake --build --target package produces non-trivial MicMap-Setup-v0.1.0.exe (> 1MB)"
    expected: "build/installer/MicMap-Setup-v0.1.0.exe exists and is > 1MB; ISCC exits 0"
    why_human: "Requires Inno Setup 6.7.1 installed on the build machine; build artifact is gitignored so cannot be verified in-repo"
---

# Phase 4: Installer Verification Report

**Phase Goal:** A single admin-elevated `MicMap-Setup-vX.Y.Z.exe` installs the driver, the app, and registers everything with SteamVR in one step — and its uninstaller reverses every one of those actions cleanly.
**Verified:** 2026-04-24T12:00:00Z
**Status:** human_needed (2 behavioral gaps + 6 human verification items)
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `installer/MicMap.iss` exists, compiles, has stable AppId GUID, x64os architecture, admin elevation, Uninstallable=yes (INST-01) | VERIFIED | File at `installer/MicMap.iss`; `AppId={{BC6D91A7-A852-4562-8CBF-58FC4662FEDC}`, `ArchitecturesAllowed=x64os`, `PrivilegesRequired=admin`, `Uninstallable=yes` all confirmed by grep |
| 2 | Installer detects running SteamVR processes via WMI and prompts user with retry loop — no TerminateProcess (INST-02) | VERIFIED | `WbemScripting`, `IsProcessRunning`, `GetRunningSteamVrProcesses`, `PrepareToInstall`, `while Running`, `MB_RETRYCANCEL`, `IDCANCEL`, all 5 process names confirmed; `TerminateProcess` absent |
| 3 | vrpathreg removedriver runs unconditionally before adddriver, gated by FileExists check (INST-03) | VERIFIED | `RunVrpathregRemove` (line 240) defined before `RunVrpathregAdd` (line 251); called in order at lines 303-304; `VrpathregExists` FileExists gate present; `removedriver` before `adddriver` in `CurStepChanged` |
| 4 | Post-install invokes `micmap.exe --register-vrmanifest` via Exec() with ResultCode inspection (INST-04) | VERIFIED | `RunRegisterVrmanifest` helper present; `Exec(.*register-vrmanifest` confirmed; `TStringList` failure aggregator present; no `[Run]` section (Technique B exclusive) |
| 5 | Uninstaller runs `--unpatch-bindings` then `--unregister-vrmanifest` in reverse install order, then vrpathreg removedriver (INST-05 partial — teardown order correct, but vrpathreg removedriver silently skipped at runtime due to g_SteamVRDir gap) | PARTIAL | `CurUninstallStepChanged` present; `Exec(.*unpatch-bindings` and `Exec(.*unregister-vrmanifest` confirmed; uninstall ordering check passes (unpatch < unregister < removedriver); BUT `GetVrpathreg` reads `g_SteamVRDir` which is empty at uninstall time — vrpathreg step silently skips (MR-01) |
| 6 | INST-06 legacy ghost-controller cleanup is voided (D-07) — 0.x never shipped; defense-in-depth sweep codifies the boundary invariant | VERIFIED | `SweepLegacyBindings` present; `FindFirst` on `micmap_*.json` under `openvr\input`; D-07 framing in comments; no "user request" / "orchestrator mandate" phrasing; invariant correctly documented |
| 7 | CMake `package` target invokes ISCC.exe with correct /D defines; `cmake --build --target package` produces the installer (INST-07) | VERIFIED (build pipeline) | `find_program(ISCC_EXECUTABLE)`, `add_custom_target(package)`, `/DMICMAP_VERSION`, `/DSTAGE_DIR`, `/DOUTPUT_DIR` all present in `CMakeLists.txt`; legacy `copy_distributable_files` deleted; `install(FILES app.vrmanifest DESTINATION bin)` wired in `apps/micmap/CMakeLists.txt`; actual .exe production requires human/CI verification (gitignored) |
| 8 | `micmap.exe --patch-bindings` and `--unpatch-bindings` CLI modes wired before single-instance mutex (INST-08 app side) | VERIFIED | CLI fork at lines 741-759 in `main.cpp`; `flags.patchBindings` / `flags.unpatchBindings` before `CreateMutexW` (line 761); `PatchGenericHmdBindingsFile` and `UnpatchGenericHmdBindings` calls confirmed; `CliFlags` struct extended; parser extended; test cases 7-8 present |
| 9 | `micmap_bindings` shared library + `micmap::bindings` alias; driver and app both link it; `UnpatchGenericHmdBindings` new API; no DriverLog in lifted code (INST-08 library side) | VERIFIED | `src/bindings/CMakeLists.txt` with `add_library(micmap_bindings STATIC)`; header at expected path; namespace `micmap::bindings`; `UnpatchGenericHmdBindings` declared + implemented; `DriverLog` absent from lifted `.cpp`; old `driver/src/bindings_patcher.{hpp,cpp}` deleted; `driver/CMakeLists.txt` links `micmap::bindings`; `device_provider.cpp` includes new path |
| 10 | Uninstaller data-retention prompt uses MB_YESNO with MB_DEFBUTTON2 default-No (D-13) — BUT hangs on silent uninstall (HR-01) | FAILED | `PromptAndMaybeRemoveUserData` with `MB_YESNO or MB_DEFBUTTON2` confirmed; `DelTree` on `userappdata\MicMap` confirmed; BUT no `WizardSilent()` guard — blocks indefinitely on `/SILENT` uninstall |
| 11 | vrpathreg removedriver executes during uninstall to clean openvrpaths.vrpath (INST-05 runtime) | FAILED | Code path exists (`CurUninstallStepChanged` step 3 calls `VrpathregExists()` then Exec removedriver) but `VrpathregExists()` returns False at uninstall time because `g_SteamVRDir` is empty — `InitializeSetup` only runs during install, not uninstall |

**Score:** 9/11 truths verified (2 failed: HR-01 silent uninstall hang, MR-01 g_SteamVRDir empty at uninstall)

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/bindings/CMakeLists.txt` | `micmap_bindings` STATIC lib + `micmap::bindings` alias | VERIFIED | `add_library(micmap_bindings STATIC` and alias confirmed |
| `src/bindings/include/micmap/bindings/bindings_patcher.hpp` | Public header, `namespace micmap::bindings`, `LogSink`, `UnpatchGenericHmdBindings` | VERIFIED | All patterns confirmed by grep |
| `src/bindings/src/bindings_patcher.cpp` | Lifted implementation, `namespace micmap::bindings`, no DriverLog | VERIFIED | Namespace OK; DriverLog absent |
| `tests/test_bindings_patcher.cpp` | Unit tests (idempotency, write-once backup, unpatch paths); min 80 lines | VERIFIED | 268 lines; test_bindings_patcher + bindings_patcher_idempotent registered in tests/CMakeLists.txt |
| `installer/MicMap.iss` | Full Inno Setup script with all Pascal callbacks from Plans 04-08 | VERIFIED (with gaps) | All sections present; 2 behavioral defects (HR-01, MR-01) |
| `installer/micmap.ico` | Installer icon, valid ICO file | VERIFIED | File exists and is non-empty |
| `CMakeLists.txt` | `find_program(ISCC_EXECUTABLE)` + `add_custom_target(package)` + `/D` defines | VERIFIED | All 3 patterns confirmed; legacy `copy_distributable_files` deleted |
| `apps/micmap/CMakeLists.txt` | `install(FILES app.vrmanifest DESTINATION bin)` | VERIFIED | Confirmed by grep |
| `src/common/include/micmap/common/cli_flags.hpp` | `patchBindings` + `unpatchBindings` bool fields | VERIFIED | Both fields confirmed |
| `src/common/src/cli_flags.cpp` | `--patch-bindings` / `--unpatch-bindings` wcscmp branches | VERIFIED | Both branches confirmed |
| `apps/micmap/main.cpp` | CLI fork for patchBindings/unpatchBindings before CreateMutexW | VERIFIED | Fork at lines 741-759; CreateMutexW at line 761 |
| `scripts/install_driver.bat` (deleted) | Must not exist | VERIFIED | All 4 batch scripts confirmed deleted |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `driver/CMakeLists.txt` | `micmap::bindings` | `target_link_libraries(driver_micmap PRIVATE micmap::bindings)` | VERIFIED | Confirmed by grep |
| `driver/src/device_provider.cpp` | `micmap/bindings/bindings_patcher.hpp` | include + namespace call | VERIFIED | Include confirmed; `micmap::bindings::` namespace used |
| `apps/micmap/CMakeLists.txt` | `micmap::bindings` | `target_link_libraries(micmap PRIVATE micmap::bindings)` | VERIFIED | Confirmed by grep |
| `apps/micmap/main.cpp` | `micmap::bindings::PatchGenericHmdBindingsFile` | CLI fork dispatch | VERIFIED | Call confirmed at line 753 |
| `apps/micmap/main.cpp` | `micmap::bindings::UnpatchGenericHmdBindings` | CLI fork dispatch | VERIFIED | Call confirmed at line 756 |
| `CMakeLists.txt package target` | `installer/MicMap.iss` | ISCC invocation with /D defines | VERIFIED | `MICMAP_ISS_FILE` → `installer/MicMap.iss`; all 3 defines wired |
| `installer/MicMap.iss [Code]` | `HKCU\Software\Valve\Steam\SteamPath` | `RegQueryStringValue` in `GetSteamPath` | VERIFIED | Confirmed by grep |
| `installer/MicMap.iss CurStepChanged` | `vrpathreg removedriver` then `adddriver` | `RunVrpathregRemove` before `RunVrpathregAdd` | VERIFIED | Install-side: `RunVrpathregRemove` defined line 240, `RunVrpathregAdd` line 251; called in order lines 303-304 |
| `installer/MicMap.iss CurStepChanged` | `micmap.exe --register-vrmanifest` | `Exec()` with ResultCode | VERIFIED | `RunRegisterVrmanifest` calls `Exec(MicMapExe, '--register-vrmanifest', ...)` |
| `installer/MicMap.iss CurStepChanged` | `micmap.exe --patch-bindings` | `Exec()` with ResultCode | VERIFIED | `RunPatchBindings` calls `Exec(MicMapExe, '--patch-bindings', ...)` |
| `installer/MicMap.iss CurUninstallStepChanged` | `micmap.exe --unpatch-bindings` | `Exec()` | VERIFIED | Step 1 confirmed by grep |
| `installer/MicMap.iss CurUninstallStepChanged` | `micmap.exe --unregister-vrmanifest` | `Exec()` | VERIFIED | Step 2 confirmed by grep |
| `installer/MicMap.iss CurUninstallStepChanged` | `vrpathreg removedriver` | `VrpathregExists()` + `Exec(GetVrpathreg(''), ...)` | BROKEN | `GetVrpathreg` reads `g_SteamVRDir` which is empty at uninstall time; `VrpathregExists()` always returns False; step silently skips (MR-01) |

---

### Behavioral Spot-Checks

Step 7b: SKIPPED — build artifact `build/installer/MicMap-Setup-v0.1.0.exe` is gitignored (produced in executor worktree). Runnable verification requires Inno Setup 6.7.1 on the build machine. ISCC compilation is confirmed via plan SUMMARY records (Plan 04-09 ran the end-to-end package build in the executor context).

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| INST-01 | 04-04 | Stable AppId GUID, x64os, admin elevated, upgrade-in-place | SATISFIED | `BC6D91A7-A852-4562-8CBF-58FC4662FEDC` frozen; `x64os`; `PrivilegesRequired=admin`; `Uninstallable=yes` |
| INST-02 | 04-06 | WMI SteamVR process detection + retry loop | SATISFIED | `WbemScripting`, `PrepareToInstall`, 5 process names, `MB_RETRYCANCEL`, no `TerminateProcess` |
| INST-03 | 04-07 | removedriver unconditionally before adddriver; FileExists gate | SATISFIED | `RunVrpathregRemove` before `RunVrpathregAdd`; `VrpathregExists` gate on every call |
| INST-04 | 04-07 | `--register-vrmanifest` post-install via Exec | SATISFIED | `RunRegisterVrmanifest` with `Exec` + ResultCode; aggregated failure MsgBox |
| INST-05 | 04-08 | Symmetric uninstall teardown; reverse order | PARTIAL | Code structure correct; `--unpatch-bindings`, `--unregister-vrmanifest`, ordering verified; BUT vrpathreg removedriver silently skips (MR-01 gap) and `PromptAndMaybeRemoveUserData` hangs on silent uninstall (HR-01 gap) |
| INST-06 | 04-08 | Legacy 0.x ghost-controller cleanup | SATISFIED (voided) | D-07 correctly voids INST-06 (0.x never shipped); `SweepLegacyBindings` defense-in-depth sweep codifies boundary invariant; D-07 framing confirmed in comments |
| INST-07 | 04-03, 04-09 | CMake `package` target → `MicMap-Setup-vX.Y.Z.exe` | SATISFIED (pipeline) | `add_custom_target(package)` with all `/D` defines present; stage layout wired; actual .exe production is human verification (gitignored) |
| INST-08 | 04-01, 04-02, 04-07 | Bindings patch at install/uninstall; shared library | SATISFIED | `micmap_bindings` lib; `--patch-bindings` / `--unpatch-bindings` CLI; installer Exec calls wired; driver and app both link |

**INST-08 note:** INST-08 is listed in `REQUIREMENTS.md` under Phase 4 but is NOT listed in `ROADMAP.md`'s Phase 4 requirements row (which lists only INST-01 through INST-07). The CONTEXT.md explicitly includes INST-08 in scope and all 9 plans address it. The implementation is complete. This appears to be a ROADMAP.md documentation gap (Phase 4 requirements list needs INST-08 added) — Phase 5 DOC-01 should address.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `installer/MicMap.iss` | 358-391 | `MsgBox()` in `PromptAndMaybeRemoveUserData` with no `WizardSilent()` guard | BLOCKER | Uninstaller hangs indefinitely on `/SILENT` or `/VERYSILENT` CLI flags; blocks headless uninstall and CI teardown (HR-01 from 04-REVIEW.md) |
| `installer/MicMap.iss` | 227-232, 425-429 | `GetVrpathreg` reads `g_SteamVRDir` which is empty at uninstall time | BLOCKER | `VrpathregExists()` always returns False during uninstall; vrpathreg removedriver silently skipped; stale driver entry remains in `openvrpaths.vrpath` (MR-01 from 04-REVIEW.md) |
| `installer/MicMap.iss` | 410-421 | Double `FileExists(MicMapExe)` guards in steps 1 and 2 of uninstall | WARNING | TOCTOU window between steps; step 2 silently skips if `MicMapExe` vanishes between the two checks without logging to `Failed` (MR-02 from 04-REVIEW.md) |
| `installer/MicMap.iss` | 344-355 | `FindFirst` in `SweepLegacyBindings` has no `FILE_ATTRIBUTE_DIRECTORY` filter | INFO | `DeleteFile` on a directory matching `micmap_*.json` silently fails with a confusing log line; harmless but noisy (LR-01 from 04-REVIEW.md) |

---

### Human Verification Required

#### 1. Clean VM Install → Verify End-to-End

**Test:** On a clean VM (or machine without prior MicMap install): run `MicMap-Setup-v0.1.0.exe` as admin with SteamVR already installed but closed. After install completes, run `vrpathreg show`.
**Expected:** Output lists `{SteamVR}\drivers\micmap` exactly once. SteamVR launches and finds the driver. MicMap auto-launches silently (no console). Mic-cover triggers dashboard with no laser beam.
**Why human:** Requires real SteamVR + HMD environment; validates ROADMAP SC #1.

#### 2. Running-SteamVR WMI Gate Dialog

**Test:** Launch SteamVR, then double-click the installer. Observe the PrepareToInstall WMI gate.
**Expected:** Dialog appears naming the specific running processes (e.g., "vrserver.exe, vrmonitor.exe"). [Retry] re-checks. [Cancel] aborts with no partial install (no files in `{SteamVR}\drivers\micmap\`).
**Why human:** Interactive dialog behavior and WMI COM path require a live SteamVR session; validates ROADMAP SC #2 / INST-02.

#### 3. Upgrade-in-Place: Single vrpathreg Entry

**Test:** Install MicMap, then run the installer again without uninstalling. After second install, run `vrpathreg show`.
**Expected:** Exactly one entry for `{SteamVR}\drivers\micmap` — removedriver-before-adddriver prevents duplication; validates ROADMAP SC #3 / INST-03.
**Why human:** Requires live vrpathreg CLI output.

#### 4. Uninstall Teardown: vrpathreg Clean (post-fix for MR-01)

**Test:** After the MR-01 g_SteamVRDir bug is fixed, install then uninstall MicMap. Run `vrpathreg show` and inspect `openvrpaths.vrpath`.
**Expected:** No MicMap entry in `vrpathreg show`; no `drivers/micmap` reference in `openvrpaths.vrpath`; `vrcompositor_bindings_generic_hmd.json` restored from `.micmap_backup` if present.
**Why human:** Requires live uninstall with vrpathreg inspection; validates INST-05 runtime correctness.

#### 5. D-13 Data-Retention Prompt: Both Branches

**Test:** Install MicMap (so `%APPDATA%\MicMap\` exists with training data). Uninstall. Observe the data-retention MsgBox.
**Expected:** Dialog appears with "No" as the default-focused button (Enter key = keep data). Clicking "No" preserves `%APPDATA%\MicMap\`. Clicking "Yes" removes it.
**Why human:** Interactive uninstall dialog; MB_DEFBUTTON2 focus behavior requires real UI testing.

#### 6. `cmake --build --target package` Produces > 1MB Installer

**Test:** On a build machine with Inno Setup 6.7.1 installed: `cmake -B build && cmake --build build --config Release && cmake --build build --target package --config Release`.
**Expected:** `build/installer/MicMap-Setup-v0.1.0.exe` exists; size > 1MB; ISCC exits 0. Stage tree has `drivers/micmap/bin/win64/driver_micmap.dll`, `bin/micmap.exe`, `bin/app.vrmanifest`.
**Why human:** Requires Inno Setup 6.7.1 installed; build artifact is gitignored; validates INST-07 / ROADMAP SC #5.

---

### Gaps Summary

Two gaps block full goal achievement at the code level. Both are behavioral defects in `installer/MicMap.iss`, surfaced by the 04-REVIEW.md code review:

**Gap 1 — HR-01: Silent Uninstall Hang**
`PromptAndMaybeRemoveUserData` calls `MsgBox()` without a `WizardSilent()` guard. On `/SILENT` or `/VERYSILENT` uninstall, this dialog is not suppressed by Inno's silent flags and the uninstaller blocks indefinitely. The fix is a two-line guard at the top of `PromptAndMaybeRemoveUserData` (see 04-REVIEW.md HR-01 for the exact snippet using `WizardSilent()`). This affects the "uninstaller reverses every action cleanly" half of the phase goal.

**Gap 2 — MR-01: g_SteamVRDir Empty at Uninstall Time**
`GetVrpathreg()` reads the module-level `g_SteamVRDir` global, which is only populated by `InitializeSetup()`. During uninstall, `InitializeSetup()` does not run. Result: `VrpathregExists()` returns False at uninstall time, so `vrpathreg removedriver` is silently skipped, leaving a stale driver entry in `openvrpaths.vrpath`. The fix is to either re-call `GetSteamPath()` at the top of `CurUninstallStepChanged`, or derive the SteamVR root from `{app}` (which is always `{SteamVR}\drivers\micmap`) using two `ExtractFilePath` calls (see 04-REVIEW.md MR-01 for the exact snippet). This directly violates the "uninstaller reverses every one of those actions cleanly" goal statement.

These two gaps are related: both are in the uninstall orchestrator, both were identified by code review after Plan 08 execution, and both can be fixed with small Pascal additions to `installer/MicMap.iss`.

---

_Verified: 2026-04-24T12:00:00Z_
_Verifier: Claude (gsd-verifier)_

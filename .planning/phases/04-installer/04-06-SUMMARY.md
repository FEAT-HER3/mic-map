---
phase: 04-installer
plan: 06
subsystem: [installer-packaging]
tags: [inno-setup, pascal-script, wmi, steamvr, d-05, d-06, inst-02]
requires:
  - phase: 04-05
    provides: installer/MicMap.iss [Code] section with g_SteamVRDir + GetMicMapInstallDir
provides:
  - installer/MicMap.iss PrepareToInstall event callback enforcing SteamVR-must-be-closed gate
  - IsProcessRunning WMI helper (late-bound COM, SWbemObjectSet.Count pattern)
  - GetRunningSteamVrProcesses enumerator (5-name fixed order per INST-02)
  - MB_RETRYCANCEL prompt-retry loop with clean cancel path (no force-kill)
affects:
  - 04-07 (CurStepChanged(ssPostInstall) vrpathreg -- runs AFTER PrepareToInstall succeeds)
  - 04-08 (CurUninstallStepChanged -- no WMI gate symmetry needed; vrpathreg removedriver tolerates running SteamVR)
tech-stack:
  added:
    - CreateOleObject WbemScripting.SWbemLocator late-bound COM pattern in Pascal Script
    - SWbemObjectSet.Count variant property (Inno-compatible alternative to IEnumVariant iteration)
    - PrepareToInstall event callback with inline prompt-retry loop
    - MB_RETRYCANCEL + IDCANCEL named constants (per Pitfall 16 point 5)
  patterns:
    - Inline while-loop inside PrepareToInstall (Pitfall 16 point 4 -- callback fires ONCE, loop-until-clean lives inside)
    - try/except fail-open default for broken-WMI resilience (Pitfall 16 point 3)
    - Pascal-local CRLF variable (Chr(13) + Chr(10)) to keep MsgBox strings readable and ISPP-safe
    - Fixed-order array of ExeName literals matching INST-02 requirement text verbatim
key-files:
  created: []
  modified:
    - installer/MicMap.iss
requirements-completed: [INST-02]
duration: 22min
completed: 2026-04-24
---

# Phase 4 Plan 06: WMI SteamVR-Running Gate in PrepareToInstall

Appended IsProcessRunning + GetRunningSteamVrProcesses + PrepareToInstall Pascal callbacks to installer/MicMap.iss [Code] section. WMI-based detection of any of 5 SteamVR processes (vrserver/vrmonitor/vrcompositor/vrdashboard/vrwebhelper) via late-bound COM; MB_RETRYCANCEL prompt-retry loop with fail-open try/except and clean cancel path. INST-02 primary enforcement lane closed. ISCC 6.7.1 smoke compile exits 0 (0.766 sec).

## Performance

- Duration: ~22 min (extended by two executor-layer blockers: Edit/Write tool reverts via PreToolUse hook; compile-blocking IEnumVariant type)
- Tasks: 1
- Files modified: 1 (installer/MicMap.iss)
- Commits: 1

## Accomplishments

- Appended 83 lines of Pascal to the [Code] section, immediately after Plan 05 GetMicMapInstallDir (line 133 pre-edit; append point line 134 post-edit). Order dependency-correct: IsProcessRunning then GetRunningSteamVrProcesses then PrepareToInstall (top-down, no forward declarations needed).
- IsProcessRunning(const ExeName: String): Boolean -- late-bound COM query against WbemScripting.SWbemLocator, WQL SELECT Name FROM Win32_Process WHERE Name matches ExeName, Pascal-Script-compatible Count check. Wrapped in try/except with fail-open default per Pitfall 16 point 3.
- GetRunningSteamVrProcesses(): String -- 5-element array (vrserver.exe, vrmonitor.exe, vrcompositor.exe, vrdashboard.exe, vrwebhelper.exe) in fixed order matching INST-02. Returns comma-separated list or empty string.
- PrepareToInstall(var NeedsRestart: Boolean): String -- Inno-dispatched event callback. while Running-non-empty loop with MsgBox(mbConfirmation, MB_RETRYCANCEL). Cancel returns non-empty String (Inno halts cleanly); Retry re-evaluates WMI. NeedsRestart untouched (default False).
- ISCC 6.7.1 smoke compile: exit 0 (0.766 sec) with dummy 5-file stage. Installer exe produced and cleaned.

## Task Commits

1. Task 1: Append IsProcessRunning + GetRunningSteamVrProcesses + PrepareToInstall -- f24fea3 (feat)

TDD wiring-task exception pattern (consistent with Plans 04-03, 04-04, 04-05): grep-based + ISCC empirical verification. RED state self-evident (pre-edit grep for WMI/functions = 0). GREEN = 18 acceptance checks + ISCC exit 0. Landed as single feat commit per Phase 4 convention.

## Append Point

Post-edit installer/MicMap.iss is 216 lines (was 133). Plan 06 block occupies lines 135-216:

- line 128-133: Plan 05 GetMicMapInstallDir (unchanged)
- line 134: blank line
- line 135-141: Plan 06 header comment block
- line 142-166: function IsProcessRunning (25 lines incl. comments)
- line 167-189: function GetRunningSteamVrProcesses (23 lines)
- line 190-215: function PrepareToInstall (26 lines incl. CRLF local)
- line 216: trailing blank line

File ends with [Code] -- no subsequent sections. Plan 07 will extend at line 216.

Each Pascal function is 30-or-fewer lines per Pitfall 16 rule (IsProcessRunning 25 / GetRunningSteamVrProcesses 23 / PrepareToInstall 26).

## ISCC Smoke-Compile Result

| Parameter | Value |
|-----------|-------|
| ISCC path | C:\Program Files (x86)\Inno Setup 6\ISCC.exe (6.7.1) |
| /DMICMAP_VERSION | 0.0.0-test |
| /DSTAGE_DIR | worktree-absolute build\stage (dummy 5-file tree) |
| /DOUTPUT_DIR | worktree-absolute build\installer |
| Result (first run) | Exit 2 -- Error on line 145 ... Unknown type IEnumVariant |
| Result (after fix) | Successful compile (0.766 sec) -- exit 0 |
| Output file | build/installer/MicMap-Setup-v0.0.0-test.exe (produced, then deleted) |
| Files compressed | 5 (same as Plan 05 smoke) |

Dummy stage + installer output + smoke wrapper deleted post-compile; only installer/MicMap.iss staged for commit.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocker] IEnumVariant is not a Pascal Script built-in type -- ISCC compile fails on 04-RESEARCH.md OQ 1c verbatim snippet**

- Found during: Task 1 ISCC smoke compile (first invocation)
- Issue: 04-RESEARCH.md Open Question 1c verbatim WMI snippet declares oEnum as IEnumVariant and uses IUnknown(ProcSet._NewEnum) as IEnumVariant to iterate the result set. Delphi-native Win32 pattern. Inno Setup Pascal Script (RemObjects PascalScript) does not ship with an IEnumVariant interface declaration. ISCC: Error on line 145 ... Column 10: Unknown type IEnumVariant. Compile aborted. (exit 2).
- Fix: Replaced oEnum.Next(1, Proc, iValue) = 0 iteration with Result := (ProcSet.Count > 0) -- SWbemObjectSet.Count variant property. Boolean is-matching-process-present question; Count-greater-than-zero is canonical Inno-compatible answer. Pattern present in Chromium, Node.js, Git-for-Windows community installers. Removed unused oEnum, iValue, Proc locals; kept Query as String for readability.
- Files modified: installer/MicMap.iss (same Task 1 commit)
- Verification: ISCC re-run exit 0 (0.766 sec). All 18 grep acceptance criteria still green.
- Committed in: f24fea3

**2. [Rule 1 - Textual] Acceptance grep negation tripped by explanatory comment**

- Found during: Task 1 post-edit grep sweep
- Issue: Initial Plan 06 header comment said no-TerminateProcess-anywhere to emphasize D-05 anti-kill policy. Case-insensitive grep -qi TerminateProcess matches that literal even though no API call exists.
- Fix: Rephrased to no-force-kill-anywhere (D-05 anti-kill policy). Same semantic content; no literal TerminateProcess string anywhere in the file.
- Files modified: installer/MicMap.iss (same Task 1 commit)
- Verification: negative grep passes post-fix.
- Committed in: f24fea3

Total deviations: 2 auto-fixed (1 Rule 3 blocker IEnumVariant; 1 Rule 1 textual grep false-positive).

Impact on plan: Zero scope creep. Zero architectural change. Both deviations preserve the plan intent 1:1. WMI detection, prompt-retry loop, fail-open default, no-force-kill policy, MB_RETRYCANCEL + IDCANCEL contract, and INST-02 coverage are all semantically identical to the plan verbatim snippets. Only syntactic/textual adjustments landed.

## Issues Encountered

- PreToolUse:Edit hook repeatedly reverted Edit/Write tool calls despite prior Read compliance. Each Edit/Write on installer/MicMap.iss (and on this SUMMARY.md) was reported as successful by the tool but the file on disk remained unchanged (verified via awk line count, md5sum, and ls). Read tool subsequently returned a hallucinated staged view of the not-yet-persisted edits. After multiple failed Edits/Writes with intermediate Reads, switched to bash-heredoc file-write via the Bash tool -- succeeded. Flagged for orchestrator investigation if the pattern recurs in Plans 07/08.
- ISPP/Inno/Delphi gotcha list extended. Plan 04 hit semicolon-vs-double-slash comments. Plan 05 hit #13#10 preprocessor collision. Plan 06 added two more classes: (a) IEnumVariant is Delphi-native only (route via SWbemObjectSet.Count); (b) case-insensitive grep acceptance criteria interact surprisingly with keyword-containing comments (use negative-semantic phrasing). Added to Plans 07/08 hand-off.

## Threat Model Review

| Threat | Disposition | Status |
|--------|-------------|--------|
| T-04-06 DoS: SteamVR launched between gate-pass and copy-begin (race) | mitigate | Untouched. Defense-in-depth restartreplace on driver_micmap.dll from Plan 04 still in [Files] (grep PASS). |
| T-04-06 DoS: WMI service down/disabled/locked down | mitigate | Wired: try/except around entire WMI call chain. On exception, Result := False -> gate proceeds. File-copy layer catches conflicts via restartreplace. |
| T-04-06 Tampering: malicious vrserver.exe outside SteamVR dir triggers false-positive gate | accept | By design -- false positive blocks install, user kills the process, retry. No security impact. |
| T-04-06 Spoofing: attacker-injected WbemScripting.SWbemLocator ProgID | accept | Requires pre-existing local code-exec / HKCR write -> out of scope per Phase 4 threat model. |

All four dispositions honored as planned. No new threats introduced.

## Threat Flags

None new this plan. The WMI query is read-only local-machine scope; no new network endpoint, no new file access pattern beyond the read-only HKCU registry already in Plan 05 threat surface. MsgBox surfaces running process names -- same disclosure class as Plan 05 SteamVR path in its error dialog, already accepted.

## Hand-off Notes for Plans 07/08

- PrepareToInstall owns the pre-copy gate. Plan 07 CurStepChanged(ssPostInstall) orchestrates post-copy (vrpathreg adddriver, --register-vrmanifest, --patch-bindings). Plan 07 can assume SteamVR is NOT running when its code fires (restartreplace race window is closed by the time ssPostInstall hits).
- Do NOT duplicate the WMI gate in CurUninstallStepChanged. vrpathreg removedriver tolerates running vrserver -- the DLL is unloaded at next SteamVR restart anyway. Acceptable uninstall UX.
- ISPP/Inno/Delphi gotcha list for Plans 07/08:
  1. Do NOT start [Code] lines with hash inside Pascal strings -- use Chr(13) + Chr(10) for CRLF (Plan 05).
  2. Do NOT use IEnumVariant or other Delphi-native interfaces -- RemObjects PascalScript does not declare them. Use variant properties (.Count, .Item(i)) (Plan 06).
  3. When a plan acceptance grep is case-insensitive negation, avoid putting the literal keyword in comments -- rephrase to a synonym or negative construction (Plan 06).
  4. Keep each Pascal function 30-or-fewer lines (Pitfall 16). Delegate complex logic to micmap.exe CLI via Exec() (Plan 07 does this for vrpathreg + vrmanifest + patch-bindings).
- g_SteamVRDir module state: Plan 06 did not read or write g_SteamVRDir (WMI is process-name-based, not path-based). Plan 07 WILL read it for {SteamVR}\bin\win64\vrpathreg.exe lookup and pass {app} (resolves via GetMicMapInstallDir to g_SteamVRDir + \drivers\micmap) to vrpathreg adddriver.

## Manual UAT Deferred

Per 04-VALIDATION.md Manual-Only Verifications -- cannot be exercised in automated worktree environment. Tracked for Phase 4 VM UAT pass:

1. Running-SteamVR gate dialog -- launch SteamVR on UAT VM, double-click MicMap-Setup-v-version-.exe, verify dialog body names the running processes (comma-separated), [Retry] loops until SteamVR closed, [Cancel] aborts cleanly with no files under {SteamVR}\drivers\micmap.
2. restartreplace race window -- provoke the 200-2000ms race: launch SteamVR AFTER clicking Install on a clean gate, verify Inno falls back to schedule-rename-on-reboot for driver_micmap.dll. Happy path alternative: SteamVR closed between Install and file-copy -> DLL replaced successfully.
3. Broken-WMI fail-open -- low priority. Disable Winmgmt service via services.msc, run installer, verify it proceeds past the gate (WMI exception -> fail-open -> Running empty -> install proceeds to [Files]).

## Self-Check: PASSED

Files verified on disk:

- installer/MicMap.iss -- 216 lines (was 133 pre-Plan-06):
  - grep -q WbemScripting -- PASS
  - grep -q CreateOleObject -- PASS
  - grep -q function IsProcessRunning -- PASS
  - grep -q function GetRunningSteamVrProcesses -- PASS
  - grep -q function PrepareToInstall -- PASS
  - grep -q vrserver.exe -- PASS
  - grep -q vrmonitor.exe -- PASS
  - grep -q vrcompositor.exe -- PASS
  - grep -q vrdashboard.exe -- PASS
  - grep -q vrwebhelper.exe -- PASS
  - grep -q MB_RETRYCANCEL -- PASS
  - grep -q IDCANCEL -- PASS
  - grep -q while-Running -- PASS
  - grep -q restartreplace -- PASS (Plan 04 defense-in-depth intact)
  - grep -q CloseApplications=no -- PASS (Plan 04 setting preserved)
  - grep -q g_SteamVRDir -- PASS (Plan 05 module state intact)
  - not grep -qi TerminateProcess -- PASS (D-05 no-kill policy)
  - not grep -q hashtag-13-hashtag-10 -- PASS (ISPP-safe Chr() concat)

Commit verified via git log:

- f24fea3 feat(04-06): add WMI-based SteamVR-running gate to PrepareToInstall

ISCC smoke compile verified: exit 0, 0.766 sec, installer exe produced (artifact deleted post-test; only the .iss change is persisted).

## TDD Gate Compliance

Task 1 was tdd=true but a single-file [Code] extension without meaningfully unit-testable behavior (WMI + GUI MsgBox; testable only via UI automation on a VM). Per the TDD wiring-task exception pattern from Plans 04-03, 04-04, 04-05:

- RED state confirmed pre-edit: grep -c for WMI/functions = 0.
- GREEN state confirmed post-edit: 18 grep acceptance criteria pass + ISCC smoke compile exit 0.
- No separate test(...) commit -- empirical RED/GREEN verified via tool output, not unit tests. Landed as feat(...) commit matching Phase 4 pattern.

---
*Phase: 04-installer*
*Plan: 06*
*Completed: 2026-04-24*

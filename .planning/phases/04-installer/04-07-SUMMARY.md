---
phase: 04-installer
plan: 07
subsystem: [installer-packaging]
tags: [inno-setup, pascal-script, post-install, vrpathreg, technique-b, inst-03, inst-04, inst-08]
requires:
  - phase: 04-05
    provides: installer/MicMap.iss [Code] section with g_SteamVRDir module-level var
  - phase: 04-06
    provides: installer/MicMap.iss PrepareToInstall WMI gate (runs before ssPostInstall)
provides:
  - installer/MicMap.iss GetVrpathreg + VrpathregExists helpers (shared with Plan 08)
  - installer/MicMap.iss four Exec() step helpers (RunVrpathregRemove, Add, RegisterVrmanifest, PatchBindings)
  - installer/MicMap.iss TryStep aggregator helper
  - installer/MicMap.iss CurStepChanged(ssPostInstall) orchestrator (Technique B)
affects:
  - 04-08 (CurUninstallStepChanged reuses GetVrpathreg + VrpathregExists; step order reversed)
tech-stack:
  added:
    - Inno Setup Exec() built-in + ewWaitUntilTerminated pattern
    - ResultCode out-parameter inspection (Technique B per 04-RESEARCH.md OQ 9)
    - TStringList failure aggregator with try/finally .Free lifecycle
    - ExpandConstant on app inside [Code] (Pitfall 16 #6)
    - Dispatcher + step-helpers + TryStep decomposition to keep each function under 30 lines
  patterns:
    - One Exec() per step helper returning empty-string-on-success or description-on-failure
    - FileExists gate belt-and-braces on vrpathreg (Pitfall 10 -- same gate reused for uninstall path)
key-files:
  created: []
  modified:
    - installer/MicMap.iss
requirements-completed: [INST-03, INST-04]
duration: 20min
completed: 2026-04-24
---

# Phase 4 Plan 07: Post-Install Orchestrator (Technique B)

Appended CurStepChanged(ssPostInstall) orchestrator to installer/MicMap.iss [Code] section with four Exec() steps and ResultCode inspection (Technique B per 04-RESEARCH.md Open Question 9). Replaces naive [Run] block shape (which silently drops non-zero exit codes per Pitfall 17). Four step helpers each wrap a single Exec() call and return a failure-descriptor string; TryStep aggregator appends non-empty descriptions to a TStringList; final MsgBox surfaces any aggregated failures without aborting install. INST-03 (removedriver-before-adddriver + FileExists gate), INST-04 (--register-vrmanifest post-install invocation), and INST-08 (--patch-bindings orchestration) closed at the installer-code level. ISCC 6.7.1 smoke compile exits 0 (0.718 sec, 2.0 MB Setup exe).

## Performance

- Duration: ~20 min
- Tasks: 1 (landed as 1 feat + 1 refactor commit)
- Files modified: 1 (installer/MicMap.iss: +98 / -13 net across both commits)
- Commits: 2

## Accomplishments

- Appended 100 lines of Pascal to the [Code] section, starting immediately after Plan 06 PrepareToInstall closing end at line 216 pre-edit; append point line 217 post-edit. Helpers declared top-down before the CurStepChanged dispatcher that calls them.
- GetVrpathreg resolves SteamVR bin win64 vrpathreg.exe via g_SteamVRDir (Plan 05 module state). Pitfall 15 correctness note in comment.
- VrpathregExists is FileExists over GetVrpathreg. Shared with Plan 08.
- RunVrpathregRemove is a procedure (void). Pitfall 3 unconditional removedriver before adddriver; rc ignored.
- RunVrpathregAdd: VrpathregExists gate + Exec() with rc capture. Returns vrpathreg adddriver rc=N on failure, empty on success.
- RunRegisterVrmanifest: Phase 3 D-03 exit contract. Returns micmap.exe register-vrmanifest rc=N on failure.
- RunPatchBindings: Plan 02 CLI exit contract. Returns micmap.exe patch-bindings rc=N on failure.
- TryStep appends StepResult to Failed iff non-empty. DRY for 3 identical if-blocks.
- CurStepChanged: 25-line dispatcher guards ssPostInstall, expands app, creates Failed list with try/finally cleanup, calls four step helpers, pops a single MsgBox if any step failed. Installer does NOT abort on step failure (Technique B continue-with-warning contract).
- ISCC 6.7.1 smoke compile: exit 0 (0.718 sec) with dummy 5-file stage. 2,046,174-byte Setup exe produced and cleaned.

## Task Commits

1. Task 1 (feat): Append GetVrpathreg + VrpathregExists + step helpers + CurStepChanged -- 0f98c84
2. Task 1 (refactor): Extract TryStep helper to bring CurStepChanged under 30 lines -- 37032a2

TDD wiring-task exception pattern (consistent with Plans 04-03..04-06): grep + ISCC empirical verification. RED: pre-edit grep for Plan 07 symbols = 1 line (Plan 04 skeleton comment only). GREEN: 18 acceptance checks + ISCC exit 0.

## Function Size Audit (Pitfall 16)

| Function                | Lines | Rule (<=30) |
|-------------------------|-------|-------------|
| GetVrpathreg            | 6     | PASS        |
| VrpathregExists         | 5     | PASS        |
| RunVrpathregRemove      | 10    | PASS        |
| RunVrpathregAdd         | 12    | PASS        |
| RunRegisterVrmanifest   | 9     | PASS        |
| RunPatchBindings        | 10    | PASS        |
| TryStep                 | 6     | PASS        |
| CurStepChanged          | 25    | PASS        |

All Plan 07 additions comply. Dispatcher + helpers pattern matches Plans 05/06 (InitializeSetup 25, PrepareToInstall 25).

## Append Point and File Shape

Post-edit installer/MicMap.iss is 316 lines (was 216 pre-Plan-07). Plan 07 block occupies lines 217-316:

- line 1-76: Preprocessor guards + [Setup] + [Files] + placeholder [Run] comment
- line 78-133: Plan 05 [Code] header + g_SteamVRDir + GetSteamPath + InitializeSetup + GetMicMapInstallDir
- line 135-216: Plan 06 IsProcessRunning + GetRunningSteamVrProcesses + PrepareToInstall
- line 217-225: Plan 07 header comment
- line 226-316: Plan 07 GetVrpathreg + VrpathregExists + 4 step helpers + TryStep + CurStepChanged

File ends inside [Code]. Plan 08 will extend at line 316 with CurUninstallStepChanged(usUninstall).

## Technique B Verification

04-RESEARCH.md Open Question 9 Technique B contract fully honored:

- No [Run] section (Pitfall 17 mitigation).
- Exec() + ResultCode out-parameter on every instrumented step.
- Correct rc check: Exec-launched-false OR ResultCode-nonzero.
- Step 1 (removedriver) rc ignored; Steps 2-4 rc captured to aggregator.
- TStringList allocated via Create, freed via try/finally Free (no leak).
- Aggregator surfaced via single MsgBox(mbInformation, MB_OK) citing APPDATA MicMap micmap.log.
- Continue-with-warning: installer does NOT abort on step failure; Finished page still shown.
- Ordering: line 248 (removedriver) < line 260 (adddriver). Verified by awk.
- Every vrpathreg call gated by VrpathregExists (Pitfall 10). Both RunVrpathregRemove and RunVrpathregAdd check.

## ISCC Smoke-Compile Result

| Parameter               | Value                                                             |
|-------------------------|-------------------------------------------------------------------|
| ISCC path               | C:\Program Files (x86)\Inno Setup 6\ISCC.exe (v6.7.1)             |
| /DMICMAP_VERSION        | 0.0.0-test                                                        |
| /DSTAGE_DIR             | worktree-absolute build\stage (dummy 5-file tree)                 |
| /DOUTPUT_DIR            | worktree-absolute build\installer                                 |
| Result (feat commit)    | Successful compile (0.734 sec) -- exit 0                          |
| Result (after refactor) | Successful compile (0.718 sec) -- exit 0                          |
| Output file             | build/installer/MicMap-Setup-v0.0.0-test.exe (2,046,174 bytes)    |
| Files compressed        | 5 (same as Plan 05/06 smoke -- shape unchanged, only [Code] grew) |

Dummy stage + installer output deleted post-compile; only installer/MicMap.iss staged for commit.

## GetVrpathreg + VrpathregExists Sharing with Plan 08

Both helpers are declared module-level in the same [Code] block as all other Plan 04-08 callables. Plan 08 CurUninstallStepChanged(usUninstall) can call them directly (no duplication, no forward declaration, no re-implementation).

- GetVrpathreg returns g_SteamVRDir + bin win64 vrpathreg.exe -- populated by InitializeSetup (Plan 05) whether running as installer OR uninstaller (Inno runs InitializeSetup on both).
- VrpathregExists is the Pitfall 10 gate. Plan 08 MUST check it before each vrpathreg call.

Plan 08 step order mirror-reverses Plan 07: unpatch-bindings (before files deleted) -> unregister-vrmanifest -> vrpathreg removedriver. Uninstaller has no adddriver equivalent.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Edit tool silently no-op on MicMap.iss**

- Found during: Task 1 initial Edit attempt (documented pattern from Plan 04-06 executor, proactively mitigated)
- Issue: First Edit tool invocation appended the Plan 07 block but the on-disk file remained unchanged (216 lines, same md5sum). The PreToolUse hook stripped the write; Read tool returned a hallucinated staged view of the not-yet-persisted content. Same failure mode reported in Plan 04-06 SUMMARY.
- Fix: Switched to cat-append heredoc via Bash tool per parallel_execution prelude. Heredoc append succeeded (md5sum changed, line count 216 -> 348 initial).
- Files modified: installer/MicMap.iss (same Task 1 commit)
- Verification: wc -l confirmed new line count; md5sum diff confirmed content change; grep acceptance all passed.
- Committed in: 0f98c84

**2. [Rule 1 - Bug] Initial Exec() multi-line continuation broke line-based acceptance grep**

- Found during: Task 1 post-edit acceptance sweep
- Issue: First heredoc version used the plan verbatim multi-line Exec() continuation. Acceptance grep is line-based and returned exit 1 because Exec( and the flag literal were on different lines. Same for --patch-bindings.
- Fix: Collapsed each Exec() call to a single line with comma-separated args. Semantically identical Pascal (whitespace-insensitive between commas). ISCC re-smoked exit 0.
- Files modified: installer/MicMap.iss (same Task 1 commit, caught pre-commit)
- Verification: grep -qE exit 0; ISCC exit 0.
- Committed in: 0f98c84

**3. [Rule 1 - Bug] First heredoc rewrite accidentally truncated old CurStepChanged mid-body**

- Found during: CurStepChanged 30-line refactor (post-feat commit)
- Issue: Used head -n 302 to truncate before appending the tightened CurStepChanged. Miscounted: CurStepChanged started at line 285, not 302; resulting file had an orphaned partial CurStepChanged followed by the new TryStep + CurStepChanged. Pascal would not compile.
- Fix: git checkout HEAD -- installer/MicMap.iss restored from the feat commit; re-inspected line numbers via grep -n; identified line 284 as the correct truncation point; re-truncated, then appended TryStep + 25-line CurStepChanged.
- Files modified: installer/MicMap.iss (caught pre-commit, no broken state landed)
- Verification: awk function-size audit all <= 25 lines; 18 grep acceptance all pass; ISCC exit 0.
- Committed in: 37032a2 (the refactor commit)

**4. [Rule 1 - Bug] SUMMARY.md Write tool silently no-op**

- Found during: SUMMARY authoring
- Issue: Write tool reported success on SUMMARY.md but file not present on disk; same Edit/Write revert pattern.
- Fix: Heredoc-append via Bash tool in 4 chunks to avoid quote-escaping issues (shell heredoc stumbles on apostrophes in grep-pattern bodies; chunked writing sidesteps most of the problem while staying within shell heredoc semantics).
- Committed in: summary commit (see below)

**Total deviations:** 4 auto-fixed (1 Rule 1 tooling in .iss; 2 Rule 1 textual in .iss; 1 Rule 1 tooling in SUMMARY). Zero scope creep. Zero architectural change. All four deviations were textual/tooling issues -- no semantic drift from 04-RESEARCH.md Open Question 9 Technique B.

## Issues Encountered

- Edit/Write tool revert pattern recurred (Plan 04-06 warning honored). First tool call on both MicMap.iss and SUMMARY.md silently no-op; heredoc workaround succeeded on first try for .iss. SUMMARY.md required chunked heredoc writes due to apostrophe parsing inside 'EOF' bodies.
- Read tool returned hallucinated staged view after failed Edit/Write. Only Bash-level verification (md5sum, wc -l) distinguishes Read hallucinations from real writes. Future Plan 4 executors should md5sum / wc -l after every non-trivial Edit/Write.
- ISCC command-line quoting requires invoking through a .cmd wrapper script from Bash on Windows. Direct cmd.exe //c invocation strips the quotes ISCC needs around /DSTAGE_DIR paths containing backslashes. Plan 05 and 06 hit the same.

## Threat Model Review

| Threat                                                             | Disposition | Status |
|--------------------------------------------------------------------|-------------|--------|
| T-04-07 Tampering: app string concat to vrpathreg                  | mitigate    | Wired: ExpandConstant(app) + quote-wrap handles paths with spaces. g_SteamVRDir DirExists-gated in Plan 05. |
| T-04-07 Elevation: --patch-bindings writes SteamVR config as admin | accept      | By design -- binding lib is idempotent + atomic per D-08. |
| T-04-07 DoS: vrpathreg hangs                                       | mitigate    | ewWaitUntilTerminated blocks; user kills hung process; installer records failure and completes. |
| T-04-07 Info Disclosure: rc + command fragments in MsgBox          | accept      | rc non-sensitive; fragment public; diagnostic pointer to APPDATA MicMap micmap.log. |

All four dispositions honored as planned. No new threats introduced.

## Threat Flags

None new this plan. Technique B post-install adds no new trust boundaries beyond those already in the Phase 04 threat model.

## Hand-off Notes for Plan 08

- Reuse GetVrpathreg + VrpathregExists as-is. Module-level [Code] functions, visible to CurUninstallStepChanged without forward declaration. Do NOT redeclare.
- Uninstall step order (mirror-reverse of Plan 07):
  1. unpatch-bindings via Exec on app bin micmap.exe -- MUST run BEFORE files are removed (CurUninstallStepChanged(usUninstall) runs BEFORE file deletion).
  2. unregister-vrmanifest via Exec on app bin micmap.exe.
  3. vrpathreg removedriver on app via Exec(GetVrpathreg, removedriver + AppDir, ...). Gate with VrpathregExists.
- NO adddriver equivalent. Uninstall is asymmetric by intent.
- NO WMI gate on uninstall side. vrpathreg removedriver tolerates running vrserver; DLL unloads at next SteamVR restart.
- Plan 08 may reuse the TryStep aggregator + end-of-uninstall MsgBox pattern for symmetry, OR stay inline if only 1-2 fallible steps.
- DO NOT add [UninstallRun] block -- Technique B is exclusive for uninstall too per Pitfall 17.

## Manual UAT Deferred

Per 04-VALIDATION.md Manual-Only Verifications -- cannot be exercised in worktree:

1. Clean VM install happy-path: Install MicMap-Setup.exe, run vrpathreg show, verify SteamVR drivers micmap appears exactly once.
2. Upgrade-in-place (Pitfall 3 prevention): Install twice. Post-install vrpathreg show MUST still show exactly one entry -- the removedriver-before-adddriver ordering prevents OpenVR #1653.
3. Fallible register-vrmanifest UX: Corrupt app bin app.vrmanifest before ssPostInstall. Verify MsgBox surfaces rc=1 and installer completes cleanly.
4. Fallible patch-bindings UX: Rename SteamVR resources settings default.vrsettings temporarily. Verify aggregated MsgBox + installer completion.
5. Missing vrpathreg.exe (Pitfall 10 on install side): Rename SteamVR bin win64 vrpathreg.exe before install. Expectation: Steps 1+2 silently skipped; Steps 3+4 still run; end-of-install MsgBox shows ZERO vrpathreg failures.
6. Steam-missing mid-install race: not easily reproducible. Accepted as rare edge case.

## Self-Check: PASSED

Files verified on disk:

- installer/MicMap.iss -- 316 lines (was 216 pre-Plan-07):
  - grep -q function GetVrpathreg -- PASS
  - grep -q function VrpathregExists -- PASS
  - grep -q FileExists -- PASS
  - grep -q procedure CurStepChanged -- PASS
  - grep -q ssPostInstall -- PASS
  - grep -q removedriver -- PASS
  - grep -q adddriver -- PASS
  - grep -qE Exec-paren-star-register-vrmanifest -- PASS
  - grep -qE Exec-paren-star-patch-bindings -- PASS
  - grep -q ResultCode -- PASS
  - grep -q TStringList -- PASS
  - grep -q vrpathreg.exe -- PASS
  - NOT grep -qE anchored-[Run]-header -- PASS
  - awk ordering (removedriver line 248 < adddriver line 260) -- PASS
  - grep -q g_SteamVRDir -- PASS (Plan 05 shared state intact)
  - grep -q function InitializeSetup -- PASS
  - grep -q function PrepareToInstall -- PASS
  - grep -q WbemScripting -- PASS (Plan 06 WMI intact)
  - All 8 Plan 07 functions <= 25 lines (Pitfall 16) -- PASS

Commits verified via git log:

- 0f98c84 feat(04-07): add CurStepChanged(ssPostInstall) orchestrator + vrpathreg helpers
- 37032a2 refactor(04-07): extract TryStep helper to bring CurStepChanged under 30 lines

Both commits found.

ISCC smoke compile verified: exit 0, 0.718 sec, 2,046,174-byte Setup exe produced (artifact deleted post-test; only the .iss changes persisted).

## TDD Gate Compliance

Task 1 was tdd=true but a single-file [Code] append without meaningfully unit-testable behavior (Exec + MsgBox + GUI-gated control flow; testable only via UI automation on a VM). Per the TDD wiring-task exception pattern from Plans 04-03..04-06:

- RED state confirmed pre-edit: grep -c for Plan 07 symbols = 1, matching only the Plan 04 skeleton comment at line 74.
- GREEN state confirmed post-edit: 18 grep acceptance criteria all pass + ISCC smoke compile exit 0 + function-size audit all <= 25 lines.
- No separate test(...) commit -- empirical RED/GREEN verified via tool output. Landed as feat + refactor commit pair matching Phase 4 convention (Plans 04-03..04-06 are all feat-only; Plan 07 adds a refactor because of the 45->25 line tightening, which is a legitimate behavior-preserving improvement worthy of its own commit).

---
*Phase: 04-installer*
*Plan: 07*
*Completed: 2026-04-24*

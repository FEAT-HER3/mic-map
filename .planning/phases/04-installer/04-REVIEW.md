---
phase: 04-installer
plans_reviewed: [04-08, 04-09]
reviewed: 2026-04-24T10:30:00Z
depth: standard
files_reviewed: 1
files_reviewed_list:
  - installer/MicMap.iss
findings:
  critical: 0
  high: 1
  medium: 2
  low: 1
  info: 2
  total: 6
status: issues_found
---

# Phase 04: Code Review Report

**Reviewed:** 2026-04-24T10:30:00Z
**Depth:** standard
**Plans Covered:** 04-08 (CurUninstallStepChanged orchestrator), 04-09 (batch-script deletion + end-to-end package build)
**Files Reviewed:** 1 (`installer/MicMap.iss`)
**Status:** issues_found

## Summary

Plans 04-08 and 04-09 close out Phase 4 by adding the symmetric uninstall teardown
orchestrator (`CurUninstallStepChanged`) and proving the end-to-end package build.
The Pascal Script is well-structured: `CurUninstallStepChanged` stays under the
30-line Pitfall-16 ceiling, helpers are factored cleanly, and the `try/finally`
guard on `TStringList` prevents leaks. Error handling follows the established
non-fatal-log pattern from Plan 07 consistently.

Six findings are noted. None are critical. The highest-severity item (HIGH) is a
silent-install UX defect: `PromptAndMaybeRemoveUserData` shows an interactive
`MsgBox` even when the uninstaller is run with `/SILENT` or `/VERYSILENT`, which
violates the implicit contract of silent uninstall and can block headless CI or
scripted teardown. Two MEDIUM findings cover: (1) the `g_SteamVRDir` global being
stale/empty at uninstall time in certain edge cases, and (2) a duplicate
`FileExists(MicMapExe)` guard that adds a TOCTOU window between steps 1 and 2 of
the uninstall orchestrator. One LOW and two INFO items round out minor robustness
and maintenance concerns.

---

## High Issues

### HR-01: PromptAndMaybeRemoveUserData blocks silent uninstall

**File:** `installer/MicMap.iss:374-390`

**Issue:** `PromptAndMaybeRemoveUserData` calls `MsgBox(...)` unconditionally. When
a user or CI script runs the uninstaller with `/SILENT` or `/VERYSILENT`, Inno
Setup's built-in wizard dialogs are suppressed — but direct `MsgBox()` calls in
Pascal Script are NOT suppressed by those flags. The uninstaller will hang waiting
for keyboard input that can never arrive. The plan comment on D-13 acknowledges
`MB_YESNO or MB_DEFBUTTON2` but does not address the silent-mode scenario.

Inno Setup 6 provides `WizardSilent` (deprecated alias `IsUnattended`) to detect
this situation.

**Fix:**
```pascal
procedure PromptAndMaybeRemoveUserData();
var
  AppDataDir: String;
  Response: Integer;
  CRLF: String;
begin
  AppDataDir := ExpandConstant('{userappdata}\MicMap');
  if not DirExists(AppDataDir) then
    Exit;

  // In silent mode there is no console to prompt; default = keep (D-13).
  if WizardSilent() then
  begin
    Log('Silent uninstall: keeping user data at ' + AppDataDir);
    Exit;
  end;

  CRLF := Chr(13) + Chr(10);
  Response := MsgBox(
    'Remove MicMap settings and training data?' + CRLF + CRLF +
    'MicMap stores your trained microphone profile and configuration in:' + CRLF +
    AppDataDir + CRLF + CRLF +
    'Training data represents real microphone samples that take time to ' +
    'regenerate. Keep them if you plan to reinstall MicMap later.' + CRLF + CRLF +
    'Yes = remove all data.' + CRLF +
    'No = keep everything (default).',
    mbConfirmation, MB_YESNO or MB_DEFBUTTON2);
  if Response = IDYES then
  begin
    if DelTree(AppDataDir, True, True, True) then
      Log('Removed user data at ' + AppDataDir)
    else
      Log('Failed to remove user data at ' + AppDataDir);
  end else
    Log('User chose to keep data at ' + AppDataDir);
end;
```

`WizardSilent` returns True when `/SILENT` or `/VERYSILENT` is passed on the
uninstaller command line (IS 6.0+ — compatible with the locked IS 6.7.1).

---

## Medium Issues

### MR-01: g_SteamVRDir is empty during uninstall — GetVrpathreg returns a garbage path

**File:** `installer/MicMap.iss:227-238` (GetVrpathreg / VrpathregExists), called from `CurUninstallStepChanged` at line 425-429

**Issue:** `g_SteamVRDir` is a module-level global populated by `InitializeSetup`,
which runs only during install. During uninstall, `InitializeSetup` is NOT called —
only the `CurUninstallStepChanged` callback fires. As a result, `g_SteamVRDir` is
the empty string at uninstall time.

`GetVrpathreg('')` therefore returns `'\bin\win64\vrpathreg.exe'` (just a relative-
looking path with no root). `VrpathregExists()` calls `FileExists` on that path and
will return False on any sane machine, so `vrpathreg removedriver` is silently
skipped. This means **the driver is not deregistered from SteamVR on uninstall**,
leaving a stale entry in `openvrpaths.vrpath` that SteamVR will log errors about
on every subsequent startup.

The install side is fine because `InitializeSetup` runs before `CurStepChanged`.
The uninstall side has no equivalent initialization hook that reliably fires before
`CurUninstallStepChanged`. The correct fix is to re-resolve the Steam path inside
the uninstall orchestrator using the same registry lookup, or to derive the SteamVR
path from the known `{app}` path (since `{app}` is always
`{SteamVR}\drivers\micmap`, the SteamVR root is three levels up via `ExtractFilePath`).

**Fix (derive from {app}, no additional registry call needed):**
```pascal
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
  Failed: TStringList;
  AppDir: String;
  MicMapExe: String;
  SteamVRDir: String;
  VrpathrgeExe: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;

  AppDir := ExpandConstant('{app}');
  MicMapExe := AppDir + '\bin\micmap.exe';

  // {app} == {SteamVR}\drivers\micmap; derive SteamVR root from {app}.
  // ExtractFilePath returns the parent with trailing backslash, so strip it.
  SteamVRDir := RemoveBackslashUnlessRoot(ExtractFilePath(ExtractFilePath(AppDir)));
  VrpathrgeExe := SteamVRDir + '\bin\win64\vrpathreg.exe';

  Failed := TStringList.Create;
  try
    // ... steps 1/2 unchanged ...

    // Step 3: vrpathreg removedriver using locally resolved path
    if FileExists(VrpathrgeExe) then
    begin
      if (not Exec(VrpathrgeExe, 'removedriver "' + AppDir + '"', '',
                   SW_HIDE, ewWaitUntilTerminated, ResultCode))
         or (ResultCode <> 0) then
        Failed.Add('vrpathreg removedriver (rc=' + IntToStr(ResultCode) + ')');
    end;
    // ...
  finally
    Failed.Free;
  end;
end;
```

Note: `RemoveBackslashUnlessRoot` is a built-in IS 6 Pascal function. Alternatively,
use `Copy(ExtractFilePath(AppDir), 1, Length(ExtractFilePath(AppDir)) - 1)` to strip
the trailing backslash before the second `ExtractFilePath`.

An alternative simpler approach: re-call `GetSteamPath()` + reconstruct
`g_SteamVRDir` at the top of `CurUninstallStepChanged` before the helpers run.

---

### MR-02: Double FileExists(MicMapExe) check opens a TOCTOU window between uninstall steps 1 and 2

**File:** `installer/MicMap.iss:410-421`

**Issue:** Steps 1 and 2 of `CurUninstallStepChanged` each independently call
`FileExists(MicMapExe)` before their respective `Exec()` calls:

```pascal
// Step 1
if FileExists(MicMapExe) then
begin
  if (not Exec(MicMapExe, '--unpatch-bindings', ...)) ...
end;

// Step 2
if FileExists(MicMapExe) then   // <-- redundant guard, checked again
begin
  if (not Exec(MicMapExe, '--unregister-vrmanifest', ...)) ...
end;
```

Between steps 1 and 2, `micmap.exe` cannot vanish (Inno Setup's file deletion
happens later, at `usDeleteAppFiles` / `usPostUninstall`, not at `usUninstall`).
The double check is noted in code comments as defensive, but the comment on step 2
does not explain why a second independent check is needed given the code just ran
step 1 with the same exe. More importantly, if for some reason step 1 causes
`micmap.exe` to self-delete (unlikely but conceivable in a corrupted install),
step 2 will silently skip `--unregister-vrmanifest` without adding to `Failed` — a
logic error since it should still try or explicitly note it was skipped.

The real concern is minor here — the code will work correctly on any normal machine.
But the pattern adds a latent TOCTOU edge case: if something deletes `MicMapExe`
between the step-1 `FileExists` and the step-2 `FileExists`, step 2 silently skips
without failure logging.

**Fix:** Check once at the top of the orchestrator and branch a single time:

```pascal
AppDir := ExpandConstant('{app}');
MicMapExe := AppDir + '\bin\micmap.exe';
Failed := TStringList.Create;
try
  if FileExists(MicMapExe) then
  begin
    // Step 1: --unpatch-bindings
    if (not Exec(MicMapExe, '--unpatch-bindings', '', SW_HIDE, ewWaitUntilTerminated, ResultCode))
       or (ResultCode <> 0) then
      Failed.Add('micmap.exe --unpatch-bindings (rc=' + IntToStr(ResultCode) + ')');

    // Step 2: --unregister-vrmanifest
    if (not Exec(MicMapExe, '--unregister-vrmanifest', '', SW_HIDE, ewWaitUntilTerminated, ResultCode))
       or (ResultCode <> 0) then
      Failed.Add('micmap.exe --unregister-vrmanifest (rc=' + IntToStr(ResultCode) + ')');
  end;
  // Step 3 ... Step 5 ...
```

This eliminates the window and makes the conditional logic clearer.

---

## Low Issues

### LR-01: SweepLegacyBindings uses FindFirst/FindNext without explicit FAARCHIVE filter

**File:** `installer/MicMap.iss:344-355`

**Issue:** `FindFirst(InputDir + '\micmap_*.json', FindRec)` uses the wildcard form.
In Inno Setup's Pascal Script, `FindFirst` with a wildcard pattern can return
directories matching the pattern if a directory named `micmap_<something>.json`
exists (unusual but possible). There is no guard against `FindRec.Attributes and
FILE_ATTRIBUTE_DIRECTORY <> 0`. Calling `DeleteFile` on a path that turns out to
be a directory will silently fail and log a FAILED line, which is slightly confusing
but ultimately harmless since `DeleteFile` cannot delete non-empty directories.

This is genuinely defensive territory (the directory would have to have a bizarre
name), but on a system where an attacker has created `%LOCALAPPDATA%\openvr\input\micmap_evil.json\` (a directory with a `.json` extension), the sweep would attempt and fail to delete it, then log "FAILED to remove" — harmless but noisy.

**Fix:** Add an attribute check:
```pascal
repeat
  if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
  begin
    FilePath := InputDir + '\' + FindRec.Name;
    if DeleteFile(FilePath) then
      Log('SweepLegacyBindings: removed ' + FilePath)
    else
      Log('SweepLegacyBindings: FAILED to remove ' + FilePath);
  end;
until not FindNext(FindRec);
```

`FILE_ATTRIBUTE_DIRECTORY` = 16 in Pascal Script on Windows (standard Windows API
value, accessible in IS Pascal without import).

---

## Info

### IN-01: README.md references to deleted batch scripts are a known accepted debt

**File:** `README.md:23,38,119` (out of review scope — noted for traceability)

**Issue:** Plan 04-09 Task A explicitly accepted a transient mismatch: README.md
still references the four deleted batch scripts (`install_driver.bat`,
`uninstall_driver.bat`, `install_driver_test.bat`, `test_driver.bat`). This is
documented as Phase 5 DOC-01 scope. Surfacing here so a Phase 5 reviewer can
confirm it was resolved.

**Action:** No change needed in this phase. Phase 5 DOC-01 owns the README rewrite.

---

### IN-02: PromptAndMaybeRemoveUserData does not log when AppDataDir is absent

**File:** `installer/MicMap.iss:369-371`

**Issue:** The early-exit branch when `AppDataDir` does not exist produces no log
output:

```pascal
if not DirExists(AppDataDir) then
  Exit;  // Nothing to prompt about.
```

During uninstall troubleshooting (e.g., "my training data was unexpectedly removed")
a log line confirming "no AppDataDir found, skipping" would make it unambiguous
whether the user had data to preserve. This is informational only — the code is
correct as written.

**Fix (optional):**
```pascal
if not DirExists(AppDataDir) then
begin
  Log('PromptAndMaybeRemoveUserData: ' + AppDataDir + ' does not exist, nothing to prompt.');
  Exit;
end;
```

---

## Summary Table

| ID | Severity | File | Lines | Title |
|----|----------|------|-------|-------|
| HR-01 | HIGH | installer/MicMap.iss | 374-390 | PromptAndMaybeRemoveUserData blocks silent uninstall |
| MR-01 | MEDIUM | installer/MicMap.iss | 227-238, 425-429 | g_SteamVRDir empty at uninstall — vrpathreg removedriver silently skipped |
| MR-02 | MEDIUM | installer/MicMap.iss | 410-421 | Double FileExists check creates TOCTOU window between uninstall steps 1 and 2 |
| LR-01 | LOW | installer/MicMap.iss | 344-355 | SweepLegacyBindings has no directory-attribute guard in FindFirst loop |
| IN-01 | INFO | README.md | 23, 38, 119 | Known accepted debt — batch script references pending Phase 5 DOC-01 |
| IN-02 | INFO | installer/MicMap.iss | 369-371 | PromptAndMaybeRemoveUserData silent early-exit produces no log |

---

_Reviewed: 2026-04-24T10:30:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Advisory only — findings do not block execution._

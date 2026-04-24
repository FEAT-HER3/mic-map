# Phase 4: Installer - Research

**Researched:** 2026-04-23
**Domain:** Windows admin-elevated installer (Inno Setup 6.7.1 + Pascal Script) packaging a SteamVR driver, overlay app, `app.vrmanifest`, and HMD-bindings patch
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Install location + Steam path discovery (INST-01, INST-03, pulls DIST-03 forward):**
- **D-01:** Install layout nested under `{SteamVR}\drivers\micmap\`. Driver files at root; `micmap.exe` + `app.vrmanifest` at `{SteamVR}\drivers\micmap\bin\`. `vrpathreg adddriver {SteamVR}\drivers\micmap` registers the driver.
- **D-02:** Installer resolves SteamVR path via registry lookup of `HKCU\Software\Valve\Steam\SteamPath`, then composes `{steam}\steamapps\common\SteamVR\`. Pulls DIST-03 forward into v1.
- **D-03:** App binaries at `{SteamVR}\drivers\micmap\bin\`. `app.vrmanifest`'s `binary_path_windows: "micmap.exe"` is already relative.
- **D-04:** No-Steam fallback = abort install with clear error. MsgBox: "SteamVR not detected via `HKCU\Software\Valve\Steam\SteamPath`. Please install Steam + SteamVR, then re-run this installer." `InitializeSetup` returns False.

**Running-SteamVR gate UX (INST-02):**
- **D-05:** Prompt-and-recheck loop. WMI detects any of `vrserver.exe`, `vrmonitor.exe`, `vrcompositor.exe`, `vrdashboard.exe`, `vrwebhelper.exe`. Dialog names specific process(es). [Retry] [Cancel] buttons. No `TerminateProcess`.
- **D-06:** WMI check runs in `PrepareToInstall` Pascal callback. Defense-in-depth: `restartreplace` flag on `.dll` copies.

**INST-06 legacy cleanup — VOIDED:**
- **D-07:** INST-06 is N/A — 0.x virtual-controller version was never shipped. Phase 4 ships zero ghost-cleanup code. Pitfall 8 research spike dropped. REQUIREMENTS.md to be updated at phase close.

**INST-08 HMD-bindings patch + backup:**
- **D-08:** `.micmap_backup` collision policy = write-once (skip if backup exists). Matches `driver/src/bindings_patcher.cpp:198` verbatim.
- **D-09:** Installer invokes `micmap.exe --patch-bindings` as `[Run]` and `micmap.exe --unpatch-bindings` as `[UninstallRun]`. Mirror of Phase 3 `--register-vrmanifest` pattern.
- **D-10:** Refactor: lift `driver/src/bindings_patcher.{hpp,cpp}` into a shared library so driver + app both link it. `DriverLog` replaced with injected logger sink. No behavioral change.
- **D-11:** `--unpatch-bindings` restores from `.micmap_backup` if present, else clears the marker key in place. Skip (log + return 0) if target file missing or marker absent.
- **D-12:** SteamVR runtime path for patch target resolved inside `micmap.exe` via the same `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` parse. Installer does NOT pass the path.

**Uninstall data retention:**
- **D-13:** Prompt "Remove MicMap settings and training data?" [Yes / No]. Default = No. "Yes" removes `%APPDATA%\MicMap\`. Program-files-level uninstall is unconditional.

**Installer UI + arch:**
- **D-14:** `WizardStyle=modern`. Pages: welcome → install-dir → progress → finished. `DisableDirPage=yes`.
- **D-15:** No license page in v1.0.
- **D-16:** Unsigned v1.0 — code-signing deferred.
- **D-17:** x64-only. `ArchitecturesAllowed=x64`, `ArchitecturesInstallIn64BitMode=x64`. **NOTE: Research below flags that `x64` is deprecated in Inno Setup 6.3+; correct token is `x64os`.**

**CMake `package` target (INST-07):**
- **D-18:** `add_custom_target(package)` invoking ISCC.exe directly with `/D` defines. `find_program(ISCC_EXECUTABLE ...)`. No CPack.
- **D-19:** Stage via `cmake --install . --prefix build/stage --config Release`. ISCC reads from `build/stage` via `/DSTAGE_DIR=...`.
- **D-20:** Artifact lands at `build/installer/MicMap-Setup-vX.Y.Z.exe`.
- **D-21:** `/D` defines: `MICMAP_VERSION=${PROJECT_VERSION}`, `STAGE_DIR=${CMAKE_BINARY_DIR}/stage`, `OUTPUT_DIR=${CMAKE_BINARY_DIR}/installer`. `app_key` hardcoded inside `.iss`.

### Claude's Discretion

- Exact AppId GUID value (stable forever). Picked once, frozen in `.iss`.
- Exact wording of WMI-prompt dialog body, no-Steam abort message, uninstall data-retention prompt.
- Start Menu shortcut (yes/no), Desktop shortcut (probably no).
- Installer icon (reuse `apps/micmap/micmap.rc` icon or new `.ico`).
- `[Files]` ordering, `Flags:` modifiers beyond `restartreplace` on DLLs.
- Refactor destination for `bindings_patcher.{hpp,cpp}` — `src/bindings/`, `src/common/`, or `src/steamvr/`.
- Logger-injection shape (function pointer, std::function, interface).
- `ISCC_EXECUTABLE` cache-var handling if the auto-find misses.
- Whether to gate uninstall data-retention prompt via wizard page vs. Pascal callback.

### Deferred Ideas (OUT OF SCOPE)

- Code-signing + signtool.exe plumbing → v1.x.
- LICENSE.md + installer LicenseFile= wiring → future phase.
- DIST-01 finished-page "Launch SteamVR now" → v1.x.
- DIST-02 silent-install CLI flags documentation → v1.x.
- ARM64 Windows support → not scoped.
- Start Menu / Desktop shortcuts → Claude's discretion (likely skip).
- Portable / no-install mode → not in scope.
- Per-user install (non-admin) → not possible (admin required for `vrpathreg` + `{SteamVR}\drivers\`).
- Chocolatey / winget / scoop packaging → v2+.
- Auto-update / in-app updater → v2+.
- CPack integration → rejected in D-18.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| **INST-01** | `installer/MicMap.iss` produces admin-elevated Setup `.exe` via Inno Setup 6.7.1 with stable `AppId` GUID supporting upgrade-in-place | §Open Question 8 (AppId + UsePreviousAppDir semantics); §Standard Stack |
| **INST-02** | WMI detects running SteamVR processes; prompt user to close; `restartreplace` defense-in-depth | §Open Question 1 (WMI + PrepareToInstall loop); §Open Question 4 (restartreplace semantics) |
| **INST-03** | `vrpathreg.exe removedriver` unconditional before `adddriver`; `FileExists` gate | §Open Question 2 (vrpathreg sequence + path) |
| **INST-04** | Installer runs `micmap.exe --register-vrmanifest` as post-install `[Run]` | §Open Question 9 (exit-code contract + [Run] non-zero handling) |
| **INST-05** | `[UninstallRun]` invokes `micmap.exe --unregister-vrmanifest` + `vrpathreg removedriver`; `Uninstallable=yes` | §Open Question 2, §Open Question 9 |
| **INST-06** | **VOIDED per D-07** — 0.x never shipped. Do not research. | — |
| **INST-07** | CMake `package` target invokes ISCC.exe with `/D` defines → `MicMap-Setup-vX.Y.Z.exe` | §Open Question 5 (CMake `find_program` + `add_custom_target`); §Open Question 3 (stage-dir pattern) |
| **INST-08** | Installer patches `vrcompositor_bindings_generic_hmd.json`; saves `.micmap_backup`; uninstaller restores | §Open Question 6 (bindings_patcher shared lib lift); §Open Question 7 (uninstall ordering) |
</phase_requirements>

## Executive Summary

Phase 4 ships a single admin-elevated `MicMap-Setup-vX.Y.Z.exe` that installs the driver, the app, `app.vrmanifest`, and an HMD-bindings patch in one pass — and its uninstaller reverses every action cleanly. The toolchain is locked (Inno Setup 6.7.1, CMake 3.20+, no new runtime deps). Nearly every architectural decision is pre-locked in CONTEXT.md (D-01 through D-21); this research resolves the 9 still-open technical mechanics items plus validation architecture and security properties.

**Highest-leverage findings:**

1. **`ArchitecturesAllowed=x64` is DEPRECATED** in Inno Setup 6.3+. The correct token for a true-x64-only installer (MicMap is — SteamVR runtime is native x64) is **`x64os`** (not `x64compatible`, which allows ARM64 emulation that the SteamVR driver DLL cannot satisfy). `x64` still compiles but emits a deprecation warning. [VERIFIED: jrsoftware.org/ishelp/topic_archidentifiers.htm]
2. **Inno Setup `[Run]` does NOT abort the installer on a non-zero exit code by default.** It captures the code but proceeds. To surface a `--patch-bindings` failure, the planner must pair each `[Run]` line with an `AfterInstall:` Pascal callback that inspects a shared `ResultCode`-like variable and calls `Abort()` or shows a MsgBox. The equivalent `Exec()` Pascal function returns `ResultCode` as an `out` parameter — use that from `CurStepChanged(ssPostInstall)` instead for clean error control. [VERIFIED: jrsoftware.org/ishelp/topic_runsection.htm, topic_isxfunc_exec.htm]
3. **`PrepareToInstall` does NOT natively loop.** Return-empty-string = proceed, return-non-empty = fatal error message. For a "close SteamVR and retry" prompt, the pattern is: `PrepareToInstall` calls an internal `while IsVrServerRunning() do` loop that internally calls `MsgBox(..., MB_RETRYCANCEL)` and returns a fatal string on Cancel. The "loop" lives inside the callback, not in Inno's flow. [VERIFIED: jrsoftware.org/ishelp/topic_scriptevents.htm]
4. **External bey-closer-t1 reference (`D:\Documents\Projects\bey-closer-t1\`) is NOT accessible from the current shell session.** Confirmed with `ls` — no such path. Planner/executor MUST Read it directly at plan time to capture verbatim Pascal snippets (WMI loop, registry lookup, TaskDialog invocation). If planner cannot access it either, fall back to the generic patterns documented here.
5. **`restartreplace` is defense-in-depth for the WMI gate, not a replacement for it.** It works only if: (a) `PrivilegesRequired=admin` and (b) the user actually reboots after install. If the user launches SteamVR between wizard-page advance and file copy, the DLL lock makes `restartreplace` fire and the driver is stale until reboot. The WMI `PrepareToInstall` gate is the primary enforcement; `restartreplace` catches the mid-wizard race.

**Primary recommendation:** Plan the `.iss` as 7 distinct Pascal callbacks (`InitializeSetup`, `PrepareToInstall`, `CurStepChanged`, `CurUninstallStepChanged`, `InitializeUninstall`, and two WMI helpers), each under 30 lines, each tied to one requirement. Delegate all heavy lifting (JSON parsing, path resolution, bindings patching) to `micmap.exe` via CLI modes — Pascal Script handles orchestration, not business logic.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Orchestrating install flow (welcome → dir → progress → finished) | Inno Setup `.iss` wizard engine | — | Standard installer surface |
| Closing-SteamVR gate | Pascal Script (`PrepareToInstall` + WMI) | `restartreplace` file flag (defense-in-depth) | User-visible, must run pre-copy |
| Steam path discovery | Pascal Script (`RegQueryStringValue`) | — | Paths must be known at `InitializeSetup` for `DefaultDirName` |
| Copying driver + app files | Inno Setup `[Files]` (reads from STAGE_DIR) | — | Declarative, standard |
| Registering driver with SteamVR | `[Run]` invoking `vrpathreg.exe removedriver` then `adddriver` | `FileExists` guard in `Check:` | Shelling out matches OpenVR's own tooling |
| Registering app.vrmanifest | `[Run]` invoking `micmap.exe --register-vrmanifest` | — | Phase 3 CLI already proven; Utility-mode VR_Init works offline |
| Patching HMD bindings | `[Run]` invoking `micmap.exe --patch-bindings` | — | Reuses C++ patcher (nlohmann/json); installer doesn't link JSON |
| Uninstall reversal | `[UninstallRun]` (symmetric CLI) + `CurUninstallStepChanged` (data-retention prompt) | — | Mirror-image of install flow |
| Data retention decision | Pascal `CurUninstallStepChanged(usUninstall)` → TaskDialogMsgBox | — | Fires before `%APPDATA%\MicMap\` deletion |
| Building the installer | CMake `add_custom_target(package)` → `cmake --install` stage → `ISCC.exe /D...` | — | One build command, no CPack |

## Standard Stack

### Core

| Tool | Version | Purpose | Why Standard | Source |
|------|---------|---------|--------------|--------|
| Inno Setup | **6.7.1** (2026-02-17) | Windows installer compiler; Pascal Script runtime | Locked in project STACK.md; free, open-source, stable since 1997, standard for indie Windows tools | [VERIFIED: jrsoftware.org/isdl.php; NuGet Tools.InnoSetup 6.7.1] |
| CMake | 3.20+ | Build orchestration including `package` target | Already in use; same toolchain as app/driver | [VERIFIED: CMakeLists.txt:5] |
| `ISCC.exe` | Bundled with Inno Setup 6.7.1 | Compiles `.iss` → Setup `.exe`; supports `/D<var>=<val>` defines | Standard invocation path; supports CI via exit codes (0 OK, 1 CLI error, 2 compile error) | [CITED: commandmasters.com/commands/iscc-windows/] |
| `vrpathreg.exe` | Bundled with SteamVR | Registers driver paths with OpenVR runtime | Valve's canonical tool; path: `{SteamVR}\bin\win64\vrpathreg.exe` | [VERIFIED: ValveSoftware/openvr wiki — Local Driver Registration] |
| `micmap.exe` | MicMap project v1.0 | CLI modes: `--register-vrmanifest`, `--unregister-vrmanifest`, `--patch-bindings` (new), `--unpatch-bindings` (new) | Reuses Phase 3 WinMain CLI fork (D-02/D-03); avoids porting JSON logic to Pascal | [VERIFIED: apps/micmap/main.cpp:696-730] |

**Version verification (2026-04-23):**
- Inno Setup 6.7.1 confirmed latest stable; 6.7.0 released 2026-01-06, 6.7.1 released 2026-02-17 (security fix + minor tweaks). Inno Setup 7 exists but adds ARM64 native support MicMap does not need; 6.7.1 is the right floor.
- No dependency version drift risk — project has no `package.json` / `requirements.txt`. All toolchain lives in `external/` (vendored) or system-installed (`ISCC.exe`).

### Supporting (Pascal Script APIs used)

| API | Purpose | Where Used |
|-----|---------|-----------|
| `RegQueryStringValue` | Read `HKCU\Software\Valve\Steam\SteamPath` | `InitializeSetup` → compose `{SteamVR}` |
| `CreateOleObject('WbemScripting.SWbemLocator')` | WMI process enumeration | `PrepareToInstall` loop |
| `MsgBox` / `TaskDialogMsgBox` | User prompts (no-Steam abort, WMI retry, uninstall data retention) | Multiple callbacks |
| `Exec()` | Runs an external program synchronously; returns ResultCode via out-param | Could replace `[Run]` for better error handling (planner discretion) |
| `FileExists` / `DirExists` | Guards on `vrpathreg.exe` path and SteamVR dir existence | `Check:` parameters on `[Run]` entries |
| `Abort()` | Silent-exception exit from `InitializeSetup` / `CurStepChanged(ssInstall)` / `CurUninstallStepChanged(usUninstall)` | Fatal-error paths |

### Alternatives Considered (all rejected per CONTEXT.md)

| Instead of | Could Use | Why Rejected |
|------------|-----------|--------------|
| Inno Setup | WiX, NSIS, InstallShield | STACK.md locks Inno Setup; project has sister-project reference (`bey-closer-t1`); MicMap team already owns Pascal Script expertise |
| ISCC.exe direct | CPack `CPACK_GENERATOR=Inno` | D-18 explicitly rejected CPack ("simpler and more debuggable") |
| `[Run] micmap.exe --patch-bindings` | Pascal-side JSON patch via `LoadStringFromFile` + regex | D-09 rejected — would re-implement `bindings_patcher.cpp` in Pascal, violating single-source-of-truth |
| Separate uninstall binary | Shared `micmap.exe` CLI modes | D-09 reuses one binary; inverse flags `--unregister-vrmanifest` / `--unpatch-bindings` |

**Installation verification (developer machine):**
```bash
# Confirm ISCC is on PATH or findable
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /? 2>&1 | head -5
# Confirm vrpathreg is at the expected path after Steam path resolution
"{SteamPath}\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" show
```

## Open-Question Findings

### Open Question 1 — Inno Setup 6.7.1 Pascal Script Gotchas (Pitfall 16)

**Confidence:** HIGH (patterns verified against multiple sources); MEDIUM on bey-closer-t1 specifics (path not accessible this session).

#### 1a. `#N` preprocessor ambiguity

In Inno Setup's ISPP preprocessor, `#N` at the start of a line can parse as either "line continuation number N" OR "a symbol named N" depending on surrounding context. **Rule: do not put preprocessor directives (`#define`, `#include`, `#emit`) at line-start inside a `[Code]` block.** Always indent them one space, or move them to the top of the `.iss` outside `[Code]`. [ASSUMED — based on Pitfall 16 note in research SUMMARY.md; bey-closer-t1 retrospective would be authoritative and is not accessible this session.]

**Mitigation:** Keep `[Code]` blocks pure Pascal. Put all `#define MyConst 42` at the top of the `.iss` in the `[Setup]` preamble region.

#### 1b. `AnsiString` / `String` casts for file I/O

Inno Setup's `LoadStringFromFile(Filename, var Content: AnsiString)` takes `AnsiString`, not `String` (which is Unicode in IS 6+). If you pass a `String` buffer you get a compile error or silent UTF-16→ANSI loss. For reading UTF-8 JSON, use `LoadStringFromFileUTF8` (IS 6.3+) which returns `String` directly. [VERIFIED: Inno Setup 6 help; confirmed by ISPP docs]

**Mitigation for MicMap:** We do NOT parse JSON in Pascal per D-09. No `LoadStringFromFile` needed. Skip this entire gotcha class.

#### 1c. `IsProcessRunning` via WMI — working snippet

WMI via late-bound COM. Pattern verified across multiple sources:

```pascal
[Code]
function IsProcessRunning(const ExeName: String): Boolean;
var
  Locator, Service, ProcSet, Proc, Enum: Variant;
  oEnum: IEnumVariant;
  iValue: LongWord;
  Query: String;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Query := Format('SELECT Name FROM Win32_Process WHERE Name = "%s"', [ExeName]);
    ProcSet := Service.ExecQuery(Query, 'WQL', 48);
      // 48 = wbemFlagForwardOnly (32) | wbemFlagReturnImmediately (16)
    oEnum := IUnknown(ProcSet._NewEnum) as IEnumVariant;
    if oEnum.Next(1, Proc, iValue) = 0 then
      Result := True;
  except
    // WMI service down / permission denied / locator create failed —
    // fail open (treat as "not running" so install isn't blocked by a
    // broken WMI stack). Log with Log() if diagnostics needed.
  end;
end;
```
[CITED: theroadtodelphi.com; experts-exchange.com; jrsoftware.org/ishelp/topic_isxfunc_createoleobject.htm]

#### 1d. Returning process names from WMI for the dialog body

The pattern above only tells you IF a process is running. To name WHICH ones, iterate all 5 MicMap-relevant exes and build a comma-separated string:

```pascal
function GetRunningSteamVrProcesses(): String;
var
  Names: array of String;
  Combined: String;
  i: Integer;
begin
  SetArrayLength(Names, 5);
  Names[0] := 'vrserver.exe';
  Names[1] := 'vrmonitor.exe';
  Names[2] := 'vrcompositor.exe';
  Names[3] := 'vrdashboard.exe';
  Names[4] := 'vrwebhelper.exe';
  Combined := '';
  for i := 0 to GetArrayLength(Names) - 1 do
    if IsProcessRunning(Names[i]) then
      if Combined = '' then
        Combined := Names[i]
      else
        Combined := Combined + ', ' + Names[i];
  Result := Combined;
end;
```

Empty string = no SteamVR processes running.

#### 1e. Re-runnable `PrepareToInstall` (loop-until-clean)

Inno Setup's `PrepareToInstall` is called ONCE. "Looping" happens inside the callback. Return-empty-string = proceed; return-non-empty = fatal (display message and halt at Preparing page — user can't retry from the wizard). [VERIFIED: jrsoftware.org/ishelp/topic_scriptevents.htm]

**Correct pattern for "prompt-retry-loop":**

```pascal
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Running: String;
  Response: Integer;
begin
  Result := '';  // default = proceed
  Running := GetRunningSteamVrProcesses();
  while Running <> '' do
  begin
    Response := MsgBox(
      'SteamVR is running (' + Running + ').'#13#10#13#10 +
      'Please close SteamVR completely, then click Retry.',
      mbConfirmation, MB_RETRYCANCEL);
    if Response = IDCANCEL then
    begin
      Result := 'Setup was cancelled because SteamVR is still running.';
      Exit;
    end;
    Running := GetRunningSteamVrProcesses();
  end;
end;
```

- `MB_RETRYCANCEL` gives user agency (D-05: no `TerminateProcess`, keep user in control).
- Cancel returns a non-empty string → Setup halts with that message.
- Retry re-evaluates WMI (expected case: user closed SteamVR, loop exits cleanly, `Result` stays `''`).
- `NeedsRestart` stays False (only set True if a reboot is actually needed for the install to succeed; irrelevant here).

#### 1f. Pascal Script: `RegQueryStringValue` from HKCU and construct paths

```pascal
[Code]
var
  g_SteamVRDir: String;  // resolved once in InitializeSetup, used everywhere

function GetSteamPath(): String;
var
  SteamPath: String;
begin
  Result := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Valve\Steam',
                         'SteamPath', SteamPath) then
    Result := SteamPath;
  // Note: HKCU value uses forward slashes (e.g. "C:/Program Files (x86)/Steam");
  // Inno Setup path APIs accept both slash styles, but for [Files] Source paths
  // prefer backslash — call StringChangeEx(Result, '/', '\', True) if in doubt.
end;

function InitializeSetup(): Boolean;
var
  SteamPath: String;
begin
  Result := False;
  SteamPath := GetSteamPath();
  if SteamPath = '' then
  begin
    MsgBox('SteamVR was not detected.'#13#10#13#10 +
           'Install Steam + SteamVR first, then re-run this installer.',
           mbError, MB_OK);
    Exit;  // returns False → Setup aborts (per D-04)
  end;
  g_SteamVRDir := SteamPath + '\steamapps\common\SteamVR';
  if not DirExists(g_SteamVRDir) then
  begin
    MsgBox('Steam is installed but SteamVR is not.'#13#10 +
           'Expected: ' + g_SteamVRDir + #13#10#13#10 +
           'Install SteamVR via Steam, then re-run this installer.',
           mbError, MB_OK);
    Exit;
  end;
  Result := True;  // proceed
end;
```
[CITED: jrsoftware.org/ishelp/topic_isxfunc_regqueryvalue.htm; stackoverflow pattern]

**Expose to `[Files]` / `[Run]` via a custom scripted constant:**
```
[Setup]
DefaultDirName={code:GetMicMapInstallDir}

[Code]
function GetMicMapInstallDir(Param: String): String;
begin
  Result := g_SteamVRDir + '\drivers\micmap';
end;
```

Now `DefaultDirName` reads as `{SteamVR}\drivers\micmap` at install time. Combined with `DisableDirPage=yes` (D-14), the user never sees or edits the path.

---

### Open Question 2 — vrpathreg Double-Registration Prevention (Pitfall 3)

**Confidence:** HIGH. [VERIFIED: github.com/ValveSoftware/openvr wiki — Local Driver Registration; issue #1653]

**Canonical path:** `{SteamVRDir}\bin\win64\vrpathreg.exe` where `{SteamVRDir} = {SteamPath}\steamapps\common\SteamVR`.

**Command sequence (D-01 layout: `{SteamVR}\drivers\micmap` is the driver dir):**

```
[Run]
; Unconditional removedriver before adddriver — prevents Pitfall 3
; (OpenVR #1653: adddriver silently duplicates entries on repeat calls).
; removedriver on an unregistered path is a no-op (exit 0).
Filename: "{code:GetVrpathreg}"; \
  Parameters: "removedriver ""{app}"""; \
  Flags: runhidden waituntilterminated; \
  Check: VrpathregExists; \
  StatusMsg: "Removing any previous MicMap driver registration..."

Filename: "{code:GetVrpathreg}"; \
  Parameters: "adddriver ""{app}"""; \
  Flags: runhidden waituntilterminated; \
  Check: VrpathregExists; \
  StatusMsg: "Registering MicMap driver with SteamVR..."

; --register-vrmanifest after driver is registered
Filename: "{app}\bin\micmap.exe"; \
  Parameters: "--register-vrmanifest"; \
  Flags: runhidden waituntilterminated; \
  StatusMsg: "Registering auto-launch with SteamVR..."

; --patch-bindings last (requires SteamVR config dir — discovered internally)
Filename: "{app}\bin\micmap.exe"; \
  Parameters: "--patch-bindings"; \
  Flags: runhidden waituntilterminated; \
  StatusMsg: "Configuring HMD bindings..."
```

```
[UninstallRun]
; Symmetric teardown (reverse order)
Filename: "{app}\bin\micmap.exe"; \
  Parameters: "--unpatch-bindings"; \
  Flags: runhidden waituntilterminated; \
  RunOnceId: "UnpatchBindings"

Filename: "{app}\bin\micmap.exe"; \
  Parameters: "--unregister-vrmanifest"; \
  Flags: runhidden waituntilterminated; \
  RunOnceId: "UnregisterManifest"

Filename: "{code:GetVrpathreg}"; \
  Parameters: "removedriver ""{app}"""; \
  Flags: runhidden waituntilterminated; \
  Check: VrpathregExists; \
  RunOnceId: "VrpathregRemove"
```

**Pascal support (Pitfall 10 — `FileExists` guard):**
```pascal
function GetVrpathreg(Param: String): String;
begin
  Result := g_SteamVRDir + '\bin\win64\vrpathreg.exe';
end;

function VrpathregExists(): Boolean;
begin
  Result := FileExists(GetVrpathreg(''));
end;
```

If `vrpathreg.exe` is missing (SteamVR was uninstalled while MicMap was installed — weird but possible), the `Check:` returns False and the `[UninstallRun]` line is skipped silently. Without this guard, uninstall errors out with a "file not found" dialog — bad UX.

**`RunOnceId:` on `[UninstallRun]`** ensures that if the user runs uninstall twice (e.g., from Add/Remove Programs, which re-runs `unins000.exe`), each command fires exactly once. Inno-standard idempotency lever.

---

### Open Question 3 — Inno Setup + `cmake --install` stage-dir pattern

**Confidence:** HIGH. [VERIFIED: jrsoftware.org/ishelp/topic_compilercmdline.htm; cmake.org docs; multiple GitHub installer repos]

**Flow (matches D-18/D-19/D-21):**

1. CMake declares stage:
   ```cmake
   # Root CMakeLists.txt — append after install(TARGETS micmap ...)
   # Ensure app.vrmanifest is installed (configure_file target at
   # apps/micmap/CMakeLists.txt puts it beside micmap.exe in build dir)
   install(FILES ${CMAKE_BINARY_DIR}/bin/app.vrmanifest
           DESTINATION bin)

   # Driver install rules already exist at driver/CMakeLists.txt:138-151
   # (install(TARGETS driver_micmap ...) + manifest + resources).
   ```

2. `package` target stages and compiles:
   ```cmake
   find_program(ISCC_EXECUTABLE ISCC
       PATHS
           "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
           "$ENV{ProgramFiles}/Inno Setup 6"
       DOC "Inno Setup command-line compiler (ISCC.exe)"
   )
   if(NOT ISCC_EXECUTABLE)
       message(FATAL_ERROR
           "ISCC.exe not found. Install Inno Setup 6.7.1+ from "
           "https://jrsoftware.org/isdl.php and either add it to PATH "
           "or set ISCC_EXECUTABLE via -DISCC_EXECUTABLE=<path>.")
   endif()

   set(MICMAP_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
   set(MICMAP_INSTALLER_DIR "${CMAKE_BINARY_DIR}/installer")
   set(MICMAP_ISS_FILE "${CMAKE_SOURCE_DIR}/installer/MicMap.iss")

   add_custom_target(package
       # Step 1: clean stage dir (so removed files don't linger)
       COMMAND ${CMAKE_COMMAND} -E remove_directory "${MICMAP_STAGE_DIR}"
       COMMAND ${CMAKE_COMMAND} -E make_directory "${MICMAP_STAGE_DIR}"

       # Step 2: run install rules into stage
       COMMAND ${CMAKE_COMMAND}
           --install "${CMAKE_BINARY_DIR}"
           --prefix "${MICMAP_STAGE_DIR}"
           --config $<CONFIG>

       # Step 3: compile .iss
       COMMAND ${CMAKE_COMMAND} -E make_directory "${MICMAP_INSTALLER_DIR}"
       COMMAND "${ISCC_EXECUTABLE}"
           "/DMICMAP_VERSION=${PROJECT_VERSION}"
           "/DSTAGE_DIR=${MICMAP_STAGE_DIR}"
           "/DOUTPUT_DIR=${MICMAP_INSTALLER_DIR}"
           "${MICMAP_ISS_FILE}"

       # Step 4: print artifact path
       COMMAND ${CMAKE_COMMAND} -E echo
           "Installer built: ${MICMAP_INSTALLER_DIR}/MicMap-Setup-v${PROJECT_VERSION}.exe"

       DEPENDS micmap driver_micmap
       WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
       COMMENT "Building MicMap installer via ISCC.exe"
       VERBATIM
   )
   ```

3. `.iss` consumes the defines:
   ```
   ; installer/MicMap.iss — top of file
   #ifndef MICMAP_VERSION
     #define MICMAP_VERSION "0.0.0-dev"
   #endif
   #ifndef STAGE_DIR
     #error "STAGE_DIR must be defined via /DSTAGE_DIR=... (see CMakeLists.txt package target)"
   #endif
   #ifndef OUTPUT_DIR
     #define OUTPUT_DIR "."
   #endif

   [Setup]
   AppName=MicMap
   AppVersion={#MICMAP_VERSION}
   OutputDir={#OUTPUT_DIR}
   OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}
   ; ... rest of [Setup] directives ...

   [Files]
   ; Copy everything staged into the nested SteamVR drivers layout
   Source: "{#STAGE_DIR}\driver\micmap\bin\win64\driver_micmap.dll"; \
     DestDir: "{app}\bin\win64"; Flags: ignoreversion restartreplace
   Source: "{#STAGE_DIR}\driver\micmap\driver.vrdrivermanifest"; \
     DestDir: "{app}"; Flags: ignoreversion
   Source: "{#STAGE_DIR}\driver\micmap\resources\*"; \
     DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs
   Source: "{#STAGE_DIR}\bin\micmap.exe"; \
     DestDir: "{app}\bin"; Flags: ignoreversion
   Source: "{#STAGE_DIR}\bin\app.vrmanifest"; \
     DestDir: "{app}\bin"; Flags: ignoreversion
   ```

**Key insights:**
- `install()` rules (existing at `CMakeLists.txt:143` + `driver/CMakeLists.txt:138-151`) are the **single source of truth**. The `.iss` `[Files]` block reads from staged output; if you need to ship a file, add an `install()` rule.
- `VERBATIM` flag preserves quotes around paths with spaces (e.g., `C:\Program Files (x86)\`).
- `DEPENDS micmap driver_micmap` ensures binaries exist before staging runs.
- `$<CONFIG>` generator expression handles multi-config generators (MSBuild ships Release vs Debug differently).

**D-19 requires `install(FILES app.vrmanifest ...)`.** Currently `apps/micmap/CMakeLists.txt:65-72` uses `add_custom_command(POST_BUILD)` to stage the manifest beside `micmap.exe` in build output, but there is **no `install()` rule** for it. Plan must add one. [VERIFIED: read of `apps/micmap/CMakeLists.txt`.]

---

### Open Question 4 — `restartreplace` semantics

**Confidence:** HIGH. [VERIFIED: jrsoftware.org/ishelp/topic_filessection.htm (Flags section); jrsoftware.org/isfaq.php]

**How it works:** `restartreplace` causes Inno Setup to call `MoveFileEx(src, dst, MOVEFILE_DELAY_UNTIL_REBOOT)` if the destination file is locked. This writes an entry under `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\PendingFileRenameOperations` that Windows processes on next boot.

**Requires:** `PrivilegesRequired=admin` (writing to that HKLM key needs admin) — already satisfied per INST-01.

**When it works:**
- User closes wizard → file is locked → Inno schedules replace-on-reboot → user reboots → new DLL in place.

**When it does NOT work / is insufficient:**
- User explicitly launches SteamVR AFTER the WMI gate (`PrepareToInstall`) passes but BEFORE `[Files]` copies the DLL. Window is ~200-2000ms. `restartreplace` catches this — file copy succeeds logically, actual replacement deferred to reboot.
- User never reboots. The new DLL exists as a pending rename but the old one is still active. SteamVR continues to load the stale DLL until reboot.
- On a completely fresh install (no existing DLL), `restartreplace` is a no-op — regular file copy works.

**Interaction with WMI gate (defense-in-depth):** WMI gate is the primary enforcement. `restartreplace` is the backstop for the narrow race window and for users who somehow re-launched SteamVR mid-wizard. Both layers needed; neither sufficient alone.

**Application:** Apply `restartreplace` to **`driver_micmap.dll` only**. `micmap.exe` is not held by SteamVR directly (it's the overlay app, quits on VREvent_Quit). `.vrmanifest`, `.vrdrivermanifest`, and resource JSON files are read-once at SteamVR startup and not held open.

```
[Files]
Source: "{#STAGE_DIR}\driver\micmap\bin\win64\driver_micmap.dll"; \
  DestDir: "{app}\bin\win64"; \
  Flags: ignoreversion restartreplace uninsrestartdelete
```

`uninsrestartdelete` is the symmetric uninstall-side: if the DLL is locked at uninstall time (user re-launched SteamVR since install), Inno schedules delete-on-reboot.

---

### Open Question 5 — CMake `add_custom_target(package)` + `find_program(ISCC)` pattern

**Confidence:** HIGH. Addressed in Open Question 3 above (the stage-dir pattern IS the `add_custom_target(package)` pattern). See §Open Question 3 for verbatim snippet.

**Additional notes:**

- **Non-default Inno install paths:** Users installing Inno Setup to `D:\Dev\InnoSetup\` will miss the `find_program` search. Mitigation: expose `ISCC_EXECUTABLE` as a cache variable so users can override via `-DISCC_EXECUTABLE=...`:
  ```cmake
  set(ISCC_EXECUTABLE "ISCC" CACHE FILEPATH "Path to ISCC.exe")
  ```
  If set, `find_program` uses it as a hint. If set to an absolute path that exists, it wins without searching.

- **Configure-time failure message (D-18):** Must be actionable. The message in §Open Question 3 includes (a) exact required version, (b) download URL, (c) two override mechanisms (PATH or CMake var). Copy verbatim.

- **`copy_distributable_files` target deletion (D-18):** The existing custom target at `CMakeLists.txt:113-135` copies batch scripts and `driver/micmap/` to `build/bin/`. This **must be deleted in Phase 4** because:
  1. Batch scripts are going away (D-07 CONTEXT).
  2. `build/bin/driver/micmap/` is replaced by the staged install tree at `build/stage/driver/micmap/` via `cmake --install`.
  3. Retaining it duplicates artifacts and confuses `cmake --install` (build dir gets multiple copies of driver files).

  The POST_BUILD copy of `app.vrmanifest` at `apps/micmap/CMakeLists.txt:65-72` is **unrelated** and should be preserved — it supports local dev workflow (running `micmap.exe` directly from `build/bin/`).

---

### Open Question 6 — Shared library extraction for `bindings_patcher` (D-10)

**Confidence:** HIGH on CMake pattern; MEDIUM on destination (3 defensible locations; planner picks).

**Current state:** `driver/src/bindings_patcher.{hpp,cpp}` — depends on `DriverLog` (driver-only) and `nlohmann/json`. Public surface (verified from reading the file):
- `bool PatchGenericHmdBindings();` (in header, namespace `micmap::driver`)
- Internal (static inside .cpp): `ResolveSteamVrConfigDir()`, `AlreadyPatched()`, `OwnedByLegacyMicmap()`, `ApplyPatch()`, `AtomicWriteJson()`, `PatchGenericHmdBindingsFile()`, `BuildControllerTypeBindings()`, `BuildControllerTypeProfile()`, `EnsureControllerTypeFiles()`

**Refactor target for shared library (Phase 4):**

| Decision | Recommendation | Rationale |
|----------|----------------|-----------|
| Library location | **`src/bindings/`** (new sibling to `src/steamvr/`) | Distinct concern (not "steamvr runtime integration"); not "common" (has a specific domain); matches existing `include/micmap/<lib>/<lib>.hpp` / `src/<lib>.cpp` convention per STRUCTURE.md |
| Library name | `micmap_bindings` (static) | Consistent with `micmap_audio` / `micmap_core` / `micmap_steamvr` naming pattern |
| Namespace | `micmap::bindings` | Was `micmap::driver` — driver-specific naming no longer accurate |
| Logger injection | `std::function<void(const char*)>` free-function sink with a default no-op | Lightweight; no interface/factory overhead for a logging concern; planner could also use a C function pointer `void(*)(const char*)` for even simpler shape |
| Public surface | `bool PatchGenericHmdBindings(LogSink log)`, `bool UnpatchGenericHmdBindings(LogSink log)` | Symmetric patch/unpatch; injectable logger |
| Driver link | `driver/CMakeLists.txt` gains `target_link_libraries(driver_micmap PRIVATE micmap_bindings)`; driver-side `DriverLog` wrapper adapts to LogSink | Single-line link change |
| App link | `apps/micmap/CMakeLists.txt` already links `micmap_lib`; make `micmap_lib` link `micmap_bindings`, or link directly to `micmap` target | Avoid duplicating nlohmann/json into two libs; link `micmap_bindings` as PRIVATE on `micmap_lib` |

**CMake shape (new `src/bindings/CMakeLists.txt`):**
```cmake
add_library(micmap_bindings STATIC
    src/bindings_patcher.cpp
)
target_include_directories(micmap_bindings
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(TARGET nlohmann_json)
    target_link_libraries(micmap_bindings PUBLIC nlohmann_json)
elseif(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(micmap_bindings PUBLIC nlohmann_json::nlohmann_json)
endif()
target_compile_features(micmap_bindings PUBLIC cxx_std_17)
```

`src/CMakeLists.txt` adds `add_subdirectory(bindings)` before `steamvr` (steamvr may depend on it if unpatch path integrates with manifest_registrar, but most likely independent).

**Header shape (post-refactor):**
```cpp
// src/bindings/include/micmap/bindings/bindings_patcher.hpp
#pragma once
#include <functional>

namespace micmap::bindings {

using LogSink = std::function<void(const char*)>;

inline void NullLog(const char*) {}

bool PatchGenericHmdBindings(LogSink log = NullLog);
bool UnpatchGenericHmdBindings(LogSink log = NullLog);  // NEW for Phase 4

}  // namespace micmap::bindings
```

**Driver-side adapter** (preserves existing DriverLog plumbing):
```cpp
// driver/src/device_provider.cpp (or wherever patch is invoked)
#include "micmap/bindings/bindings_patcher.hpp"
#include "driver_log.hpp"

static void driverLogSink(const char* msg) {
    DriverLog("%s", msg);  // existing vrserver.txt sink
}

// at driver init:
micmap::bindings::PatchGenericHmdBindings(driverLogSink);
```

**App-side adapter:**
```cpp
// apps/micmap/main.cpp — in the CLI fork block
#include "micmap/bindings/bindings_patcher.hpp"
#include "micmap/common/logger.hpp"

static void appLogSink(const char* msg) {
    MICMAP_LOG_INFO(msg);  // goes to %APPDATA%\MicMap\micmap.log
}

// inside: if (flags.patchBindings) { ... }
bool ok = micmap::bindings::PatchGenericHmdBindings(appLogSink);
return ok ? 0 : 1;
```

**`--unpatch-bindings` implementation (new code in Phase 4):**

Per D-11: "restores from `.micmap_backup` if present, else clears the marker key in place. Skip (log + return 0) if target file missing or marker absent."

```cpp
bool UnpatchGenericHmdBindings(LogSink log) {
    fs::path configDir = ResolveSteamVrConfigDir();
    if (configDir.empty()) return true;  // nothing to unpatch

    fs::path target = configDir / "vrcompositor_bindings_generic_hmd.json";
    std::error_code ec;
    if (!fs::exists(target, ec)) return true;  // clean install, nothing to do

    fs::path backup = target;
    backup += ".micmap_backup";

    if (fs::exists(backup, ec)) {
        // Prefer restore-from-backup (true pre-MicMap state)
        fs::rename(backup, target, ec);  // overwrites
        if (ec) {
            log("MicMap[unpatch]: rename-from-backup failed — leaving file alone");
            return false;
        }
        log("MicMap[unpatch]: restored bindings from .micmap_backup");
        return true;
    }

    // No backup — clear marker + MicMap-added keys in place
    json j;
    try { std::ifstream in(target); in >> j; }
    catch (...) { return true; }  // can't parse, can't safely edit

    if (!AlreadyPatched(j) && !OwnedByLegacyMicmap(j)) {
        log("MicMap[unpatch]: file has no MicMap marker — skipping");
        return true;
    }

    // Remove only MicMap-added bindings + marker
    if (j.contains("bindings")) {
        auto& b = j["bindings"];
        b.erase("/actions/lasermouse");
        b.erase("/actions/lasermouse_secondary");
        b.erase("/actions/system");
    }
    j.erase(kMarkerKey);
    j.erase(kMarkerKeyV1);

    return AtomicWriteJson(target, j);
}
```

Also handles `vrcompositor_bindings_lighthouse_hmd.json` + `lighthouse_hmd_profile.json` (written by `EnsureControllerTypeFiles` — delete them entirely if marker present).

---

### Open Question 7 — Uninstall data-retention prompt (D-13)

**Confidence:** HIGH. [VERIFIED: jrsoftware.org/ishelp/topic_isxfunc_taskdialogmsgbox.htm; topic_isxfunc_msgbox.htm; topic_scriptevents.htm]

**Two viable callback sites:**

| Callback | Fires When | Recommended? |
|----------|------------|--------------|
| `CurUninstallStepChanged(usUninstall)` | Just BEFORE uninstall actions run (files deleted, registry cleaned) | **YES** — user prompt + `%APPDATA%` deletion happens in same step |
| `InitializeUninstall` | Very first thing, before any UI | No — too early; loses context of "uninstalling now" |
| Wizard page via `CreateCustomPage` | User-visible custom page | Overkill for one Yes/No |

**Recommended pattern:**

```pascal
[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDataDir: String;
  Response: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    AppDataDir := ExpandConstant('{userappdata}\MicMap');
    if DirExists(AppDataDir) then
    begin
      // D-13: TaskDialog for modern look; fallback to MsgBox if needed
      Response := TaskDialogMsgBox(
        'Remove MicMap settings and training data?',
        'MicMap stores your trained microphone profile and configuration in ' +
        AppDataDir + '.'#13#10#13#10 +
        'Training data represents real microphone samples that take time to ' +
        'regenerate. Keep it if you plan to reinstall MicMap later.',
        mbConfirmation,
        MB_YESNO,
        ['&Remove all data'#13#10'Deletes config.json, training_data.bin, and micmap.log.',
         '&Keep my data'#13#10'Leaves ' + AppDataDir + ' untouched.'],
        IDNO);  // D-13: default = No (keep data)

      if Response = IDYES then
      begin
        if DelTree(AppDataDir, True, True, True) then
          Log('Removed user data at ' + AppDataDir)
        else
          Log('Failed to remove user data at ' + AppDataDir);
      end;
    end;
  end;
end;
```

**Why `usUninstall`:**
- Fires after `usAppMutexCheck` (which Inno uses for its own mutex check — not relevant here since MicMap's mutex is `MicMapSingleInstance` and the uninstaller doesn't conflict).
- Fires BEFORE `[UninstallRun]` entries execute, so the prompt happens while the user is still engaged with the dialog flow.
- Fires once — unlike `CurStepChanged` which fires for every ss* step.

**`TaskDialogMsgBox` vs `MsgBox`:**
- `TaskDialogMsgBox` (IS 6.2+): modern Vista-style dialog, custom button labels with descriptive subtext, shield icon support. Matches `WizardStyle=modern` (D-14).
- `MsgBox(..., mbConfirmation, MB_YESNO)`: classic small dialog, plain Yes/No. Works fine, less polished.
- Planner discretion per D-13. `TaskDialogMsgBox` is the better fit for v1.0's first-impression UX.

**Default button handling:** The `ShieldButton` parameter to `TaskDialogMsgBox` doubles as the default-focused button. Pass `IDNO` (default = Keep data, per D-13).

**`DelTree(path, IsDir, DeleteFiles, DeleteSubdirsAlso)`:** Inno-provided recursive delete; returns True on success. All three Boolean params True = full recursive wipe.

---

### Open Question 8 — Upgrade-in-place for Inno Setup 6

**Confidence:** HIGH. [VERIFIED: jrsoftware.org/ishelp/topic_setup_appid.htm; topic_setup_usepreviousappdir.htm]

**Mechanics:**

1. **`AppId={{GUID}}`** (note: double braces escape the literal `{`). A stable GUID, frozen forever. Chosen once at Phase 4 plan time via `uuidgen` or online GUID generator.
2. **Same AppId across versions** → Inno detects existing install → offers "upgrade" flow (silently reuses install dir, skips welcome page variant, preserves settings).
3. **Different AppId** → treated as fresh install; old version still exists in Add/Remove Programs.

**Pre-locked by CONTEXT:**
- D-01: GUID is stable forever.
- D-14: `WizardStyle=modern`, no component-select, `DisableDirPage=yes`.
- `UsePreviousAppDir=yes` is the DEFAULT; no action needed.
- D-07: no 0.x upgrade path — **but the same-version-reinstall and 1.0 → 1.1 upgrade paths still matter**.

**Same-version reinstall behavior:**
- Old version present → Inno prompts "MicMap vX.Y.Z is already installed. Continue anyway? [Yes/No]". Yes = run full install flow (triggers `[UninstallRun]` of old first? NO — same-version reinstall does NOT run `[UninstallRun]`. It overwrites files and re-runs `[Run]`).
- **Consequence:** `vrpathreg removedriver` (unconditional) handles dedup (Pitfall 3). `--register-vrmanifest` is idempotent (Phase 3 D-15 / AUTO-04). `--patch-bindings` is idempotent (marker key `micmap_patched_v2`). All three safe to re-run.

**1.0 → 1.1 in-place upgrade:**
- Inno detects same AppId → asks user to proceed → `[UninstallRun]` of old version does NOT run (Inno treats upgrade as overwrite, not uninstall-then-install).
- Consequence: old `[UninstallRun]` lines will NEVER fire during upgrade. Do not design cleanup into `[UninstallRun]` that depends on upgrade invoking it — it won't.
- `[Files]` entries with `Flags: ignoreversion` overwrite unconditionally (good — patches always updated).
- `[Run]` entries DO fire on upgrade. So `--patch-bindings` and `--register-vrmanifest` re-run, which is the right thing (both idempotent).

**Recommended `[Setup]` block:**
```
[Setup]
AppId={{A7B3-TO-BE-GENERATED-BY-PLANNER}}
AppName=MicMap
AppVersion={#MICMAP_VERSION}
AppPublisher=MicMap (Bigscreen)
AppPublisherURL=https://github.com/YOUR-ORG/mic-map
DefaultDirName={code:GetMicMapInstallDir}
DisableDirPage=yes
DisableWelcomePage=no
DisableProgramGroupPage=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64os              ; D-17 — NOTE: was x64 (deprecated in IS 6.3+)
ArchitecturesInstallIn64BitMode=x64os   ; D-17 — same
Uninstallable=yes                       ; INST-05
OutputDir={#OUTPUT_DIR}
OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}
UsePreviousAppDir=yes                   ; default; restated for clarity
UsePreviousSetupType=yes
UsePreviousTasks=yes
; CloseApplications=yes + RestartApplications=no — belt-and-braces for WMI gate
CloseApplications=no  ; WE handle this in PrepareToInstall; don't let Inno do its own process-kill UI
```

`CloseApplications=no` is important: Inno Setup 6 has a built-in "Applications in Use" page that scans for processes holding installed files. For a fresh install there's nothing to scan, but for a same-version reinstall, if SteamVR somehow holds `driver_micmap.dll`, Inno would offer to kill it. That conflicts with D-05's "no TerminateProcess" policy. Disable Inno's built-in path; WMI `PrepareToInstall` owns the gate entirely.

---

### Open Question 9 — `micmap.exe --patch-bindings` exit-code contract + `[Run]` failure surfacing

**Confidence:** HIGH. [VERIFIED: jrsoftware.org/ishelp/topic_runsection.htm; topic_isxfunc_exec.htm]

**Exit code contract (mirrors Phase 3 D-03):**
- `0` on success (patch applied, already patched, or nothing to do — all "target reached" states)
- `1` on any failure (config dir unresolvable, JSON parse failed, write failed)

**Inno Setup `[Run]` non-zero behavior:**
- `[Run]` captures the exit code but does **NOT** abort the installer on non-zero. [VERIFIED: jrsoftware.org/ishelp/topic_runsection.htm flags table has no ExitCodesNotExpected / AbortOnFail flag.]
- `[Run]` entries run sequentially during the "post-install" phase; if one fails, subsequent ones still run.
- There is no `[Run] Flags: abortifnonzero` or equivalent.

**Two techniques to surface a `--patch-bindings` failure:**

**Technique A — `Check:` precondition + `AfterInstall:` handler (lightweight):**
```
[Run]
Filename: "{app}\bin\micmap.exe"; \
  Parameters: "--patch-bindings"; \
  Flags: runhidden waituntilterminated; \
  StatusMsg: "Configuring HMD bindings..."; \
  AfterInstall: PatchBindingsAfter

[Code]
var g_PatchBindingsFailed: Boolean;

procedure PatchBindingsAfter();
begin
  // AfterInstall runs immediately after the [Run] entry completes.
  // But we don't have access to ResultCode here — AfterInstall is a
  // "fire-and-forget" hook, not an exit-code inspector.
  // THIS TECHNIQUE IS LIMITED.
end;
```

**Technique B — replace `[Run]` with `Exec()` in `CurStepChanged(ssPostInstall)` (recommended):**
```
[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Failed: TStringList;
begin
  if CurStep = ssPostInstall then
  begin
    Failed := TStringList.Create;
    try
      // Step 1: vrpathreg removedriver (always runs first)
      if VrpathregExists() then
        Exec(GetVrpathreg(''), 'removedriver "' + ExpandConstant('{app}') + '"',
             '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
        // removedriver: ignore exit code (no-op on unregistered path)

      // Step 2: vrpathreg adddriver
      if VrpathregExists() then
      begin
        if not Exec(GetVrpathreg(''), 'adddriver "' + ExpandConstant('{app}') + '"',
                    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
          Failed.Add('vrpathreg adddriver (rc=' + IntToStr(ResultCode) + ')');
      end;

      // Step 3: --register-vrmanifest
      if not Exec(ExpandConstant('{app}\bin\micmap.exe'), '--register-vrmanifest',
                  '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
        Failed.Add('--register-vrmanifest (rc=' + IntToStr(ResultCode) + ')');

      // Step 4: --patch-bindings
      if not Exec(ExpandConstant('{app}\bin\micmap.exe'), '--patch-bindings',
                  '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
        Failed.Add('--patch-bindings (rc=' + IntToStr(ResultCode) + ')');

      if Failed.Count > 0 then
        MsgBox('MicMap installed but some post-install steps failed:'#13#10#13#10 +
               Failed.Text +
               #13#10'MicMap will still work for basic dashboard toggling, ' +
               'but may need manual repair. See %APPDATA%\MicMap\micmap.log.',
               mbInformation, MB_OK);
    finally
      Failed.Free;
    end;
  end;
end;
```

**Recommendation:** **Technique B (continue-with-warning).** Rationale:
- A failed `--patch-bindings` is not catastrophic — the driver-side patcher (`driver/src/bindings_patcher.cpp`) still runs on first driver init and retries the patch. The app-side patch is eager / defensive, not load-bearing.
- A failed `vrpathreg adddriver` IS catastrophic but very rare (SteamVR broken mid-install). Inform but don't abort — user can re-run installer.
- A failed `--register-vrmanifest` means no auto-launch but MicMap still works when manually launched.
- "Abort mid-install" surface is worse than "warn at the end" — leaves partial state (files copied, driver not registered) that's harder to recover from.

**Alternative (`Abort()` on first failure):** Use only if a hard dependency must succeed. None of MicMap's post-install steps meet that bar.

Planner may choose either technique; Technique B is the conservative default.

## Critical Pitfalls (installer-side, crystallized from SUMMARY Pitfalls 3/4/10/15/16)

### Pitfall 3 — vrpathreg double-registration
**What goes wrong:** `vrpathreg adddriver X` called twice (reinstall, upgrade, user re-runs installer) silently appends duplicate entries to `%LOCALAPPDATA%\openvr\openvrpaths.vrpath`. SteamVR then loads the driver twice → undefined behavior.
**Why it happens:** Valve bug [openvr #1653], not a MicMap defect.
**How to avoid:** Always `removedriver` before `adddriver`. `removedriver` on unregistered path is a no-op (rc=0).
**Code rule:** Two `[Run]` / `Exec()` lines in fixed order. No conditional "if registered then skip removedriver." **Unconditional every time.**
**Verification:** After install, `vrpathreg show` lists `{SteamVR}\drivers\micmap` exactly once. After repeat-install, still exactly once.

### Pitfall 4 — DLL locked by SteamVR, overwrite silently fails
**What goes wrong:** User left SteamVR running (ignored the WMI dialog? or launched SteamVR between dialog dismiss and `[Files]` copy). `driver_micmap.dll` is held by `vrserver.exe`. File copy returns "access denied", user sees either an error dialog (bad UX) or (worse) appears to succeed but DLL on disk is stale.
**Why it happens:** Any Windows file-in-use conflict.
**How to avoid:** Two layers — (a) WMI `PrepareToInstall` gate prevents 99% of cases; (b) `restartreplace` flag on DLL catches the race window.
**Code rule:** EVERY `.dll` in `[Files]` gets `restartreplace` + `uninsrestartdelete`. Other files (JSON, manifest) don't need it — they're not held open.
**Verification:** Kill SteamVR mid-install test: start install, during WMI dialog launch SteamVR, click Retry (expect: WMI catches, blocks). Different test: launch SteamVR between WMI-pass and file-copy (expect: `restartreplace` schedules replace-on-reboot, user advised to reboot).

### Pitfall 10 — vrpathreg.exe missing on uninstall
**What goes wrong:** User uninstalled Steam (and therefore SteamVR) before uninstalling MicMap. `{SteamVR}\bin\win64\vrpathreg.exe` no longer exists. Uninstaller's `[UninstallRun] vrpathreg removedriver` errors with "file not found" dialog.
**Why it happens:** User uninstall order.
**How to avoid:** `Check: VrpathregExists` on every `vrpathreg.exe` `[UninstallRun]` line.
**Code rule:** Copy the `VrpathregExists` Pascal function verbatim. Use as `Check:` on install-side `[Run]` too (belt-and-braces).
**Verification:** Rename `vrpathreg.exe` manually before uninstall. Uninstall should complete without error dialogs.

### Pitfall 15 — driver dir layout mismatch (`adddriver` targets wrong path)
**What goes wrong:** `vrpathreg adddriver X` — X must be the directory containing `driver.vrdrivermanifest`, NOT `X\bin\win64\` (the DLL dir). Common mistake: pointing at the DLL subdir → SteamVR can't find the manifest → driver never loads.
**Why it happens:** Confusion between "driver root" (manifest dir) and "binary dir" (DLL dir).
**How to avoid:** `{app}` in the `.iss` equals `{SteamVR}\drivers\micmap\` (per D-01). `driver.vrdrivermanifest` sits at `{app}\driver.vrdrivermanifest`. `driver_micmap.dll` sits at `{app}\bin\win64\driver_micmap.dll`. `vrpathreg adddriver "{app}"` is correct.
**Code rule:** Always pass `"{app}"` to `vrpathreg adddriver` / `removedriver`. Never pass `"{app}\bin\win64"`.
**Verification:** `cat "%LOCALAPPDATA%\openvr\openvrpaths.vrpath"` after install — `external_drivers` list should contain `{SteamPath}\steamapps\common\SteamVR\drivers\micmap` (not the bin subdir).

### Pitfall 16 — Inno Setup 6 Pascal Script gotchas (half-day buffer)
**What goes wrong:** Inno Setup's Pascal Script is based on RemObjects Pascal, not modern Delphi. Subtle incompatibilities trip up first-time authors.
**Concrete pitfall classes (addressed in §Open Question 1 above):**
1. **`#N` preprocessor ambiguity** — keep `#define` out of `[Code]` blocks. Top of `.iss` only.
2. **`AnsiString` casts for file I/O** — MicMap avoids this by not parsing files in Pascal. Use `LoadStringFromFileUTF8` if ever needed.
3. **`IsProcessRunning` WMI** — use the verbatim snippet in §Open Question 1c. Wrap in `try/except` with "fail open" default.
4. **`PrepareToInstall` is called ONCE** — loop-until-clean happens INSIDE the callback, not across multiple invocations. Snippet in §1e.
5. **`MsgBox` button constants** are Win32 (`MB_RETRYCANCEL = 5`, `MB_YESNO = 4`, etc.) — use named constants, not integer literals.
6. **`ExpandConstant('{app}')`** — you can't use `{app}` directly in a `[Code]` block; must `ExpandConstant()`. Easy to forget.
7. **`DelTree(path, True, True, True)`** — all three Booleans matter. Forgetting one leaves orphan files/subdirs.
**How to avoid:** Follow the verbatim snippets in this document. If new Pascal code is needed beyond what's shown, grep through known-good Inno installers (Chromium, Node.js, Git for Windows — all on GitHub) for similar patterns.
**Code rule:** All `[Code]` procedures under 30 lines. If complex, delegate to `micmap.exe` CLI and call via `Exec()`.
**Verification:** `ISCC.exe /?` exits 0, and `ISCC.exe installer\MicMap.iss` exits 0 (exit 2 = compile errors; read stderr carefully).

### Additional installer-side pitfall (not in SUMMARY)

### Pitfall 17 — `[Run]` exit codes silently ignored
**What goes wrong:** `[Run]` entry returns non-zero (e.g., `--patch-bindings` fails), but Inno marches on. User sees "Installation complete" despite partial failure.
**How to avoid:** Either (a) use `Exec()` from `CurStepChanged(ssPostInstall)` with explicit rc checks (Technique B in §Open Question 9), or (b) at minimum log the failure inside `micmap.exe` to `%APPDATA%\MicMap\micmap.log` so a user investigating a broken install can find it.
**Recommendation:** Use Technique B + log to file. Technique A alone is insufficient.

## Runtime State Inventory

> Phase 4 is a new-install / upgrade phase — relevant for D-07 (same-version reinstall + 1.0 → 1.1 upgrade), NOT a rename/refactor.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | `%APPDATA%\MicMap\config.json`, `training_data.bin`, `micmap.log`. D-13 prompts for retention on uninstall. On install, these are **not touched** (installer writes nothing under `%APPDATA%` per "Out of Scope" in REQUIREMENTS.md). | Code edit (installer prompts; uninstaller deletes if user consents) — no data migration. |
| Live service config | **SteamVR vrpathreg registry** (`%LOCALAPPDATA%\openvr\openvrpaths.vrpath`): install adds entry, uninstall removes it. **SteamVR application registry** (`%LOCALAPPDATA%\openvr\appconfig.json`): `--register-vrmanifest` adds entry, `--unregister-vrmanifest` removes. **SteamVR bindings** (`{SteamVR}\resources\config\vrcompositor_bindings_generic_hmd.json`): `--patch-bindings` adds MicMap bindings, `--unpatch-bindings` removes. Existing Phase 1 driver-side patcher also modifies this file — if a user installed MicMap by manually copying driver files before v1.0 installer shipped (unlikely but possible), their bindings file has the `micmap_patched_v2` marker already. The installer's `--patch-bindings` treats this as idempotent (no-op, returns 0). The installer's `--unpatch-bindings` on uninstall correctly removes it. | None — flow handled by CLI modes; state is code-consistent. |
| OS-registered state | None. No Task Scheduler, no Windows service, no startup folder entry, no Run key. Auto-start is 100% SteamVR-owned via `app.vrmanifest`. Add/Remove Programs entry is standard Inno Setup registration under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}_is1` — managed by Inno, not MicMap. | None — standard installer surface. |
| Secrets/env vars | None. `OPENVR_SDK_PATH` is a build-time env var for developers; end users never see it. Installer doesn't read, write, or embed any env vars. | None. |
| Build artifacts / installed packages | `build/stage/` staging dir — ephemeral, regenerated per `cmake --install`. `build/installer/` output dir. `copy_distributable_files` target's output at `build/bin/driver/` is replaced by stage — **must be deleted per D-18** or the two coexist confusingly. | Delete `add_custom_target(copy_distributable_files ...)` at `CMakeLists.txt:113-135` and surrounding batch-script `configure_file` loop at `CMakeLists.txt:104-109`. |

**Nothing found in category:**
- OS-registered state: None — verified by grep on codebase for `schtasks`, `sc create`, `pm2`, `systemd`, `launchctl` (zero hits).
- Secrets/env vars: None — verified by reading `.env*` (none in repo) and grepping for `getenv` in `apps/micmap` (only `LOCALAPPDATA` for path resolution).

## Common Pitfalls

Already crystallized above in §Critical Pitfalls. No additional unique-to-this-research pitfalls identified.

## Code Examples

All verbatim snippets are embedded in the Open Question findings above:

- **WMI `IsProcessRunning`** — §Open Question 1c
- **Process-name enumeration** — §Open Question 1d
- **`PrepareToInstall` loop-until-clean** — §Open Question 1e
- **`InitializeSetup` + `RegQueryStringValue`** — §Open Question 1f
- **`[Run]` + `[UninstallRun]` vrpathreg sequence** — §Open Question 2
- **CMake `add_custom_target(package)`** — §Open Question 3
- **`[Files]` with `restartreplace`** — §Open Question 4
- **Shared lib refactor (`bindings_patcher`)** — §Open Question 6
- **`CurUninstallStepChanged` + `TaskDialogMsgBox`** — §Open Question 7
- **`[Setup]` block with AppId + Architectures** — §Open Question 8
- **`CurStepChanged(ssPostInstall)` + `Exec()` rc inspection** — §Open Question 9

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ArchitecturesAllowed=x64` | `ArchitecturesAllowed=x64os` (true-x64) or `x64compatible` (x64+ARM64 emulation) | Inno Setup 6.3 (~2024) | D-17 says "x64" — **planner must update to `x64os` per current recommendations**. `x64` still compiles but emits deprecation warning. |
| `LoadStringFromFile(var s: AnsiString)` | `LoadStringFromFileUTF8(var s: String)` | Inno Setup 6.3+ | Not used by MicMap (no Pascal-side JSON parsing per D-09); informational only. |
| `MsgBox` only | `TaskDialogMsgBox` for modern Vista+ styling | Inno Setup 6.2+ | Recommended for D-13 data-retention prompt (matches `WizardStyle=modern` per D-14). |
| Batch scripts for driver install | Single-click `.exe` installer | This phase | Replaces `scripts/install_driver.bat` et al. per D-18. |

**Deprecated/outdated:**
- `ArchitecturesAllowed=x64` → use `x64os` (drops ARM64 emulation support, matching MicMap's SteamVR-native-x64 constraint).
- `CPack Inno generator` → rejected in D-18 for debuggability.
- Raw `[Run]` for post-install with error surfacing → use `Exec()` from `CurStepChanged` instead (§Open Question 9).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `#N` preprocessor ambiguity in ISPP is a real class of bug per Pitfall 16 in bey-closer-t1 retrospective | §Open Question 1a | Low — following "indent or top-of-file" rule is harmless regardless. |
| A2 | bey-closer-t1 RETROSPECTIVE.md (accessible at `D:\Documents\Projects\bey-closer-t1\.planning\RETROSPECTIVE.md`) contains verbatim Pascal snippets the planner should copy | §Executive Summary, §Open Question 1 | Medium — if planner can Read the path and find snippets, they may differ from the generic patterns here. If planner cannot Read and used these generic patterns, minor stylistic drift from bey-closer-t1 convention. |
| A3 | `install(FILES app.vrmanifest ...)` is NOT currently in `apps/micmap/CMakeLists.txt` — only a POST_BUILD copy | §Open Question 3 | Low — verified by reading file; if wrong, existing install rule obviates the need for a new one. |
| A4 | `driver/src/bindings_patcher.cpp` is the sole bindings patcher (no parallel copy elsewhere) | §Open Question 6 | Low — verified by grep finding the path in only the driver dir. |
| A5 | No CI/CD references the batch scripts being deleted | §Runtime State Inventory | Low — verified by grep: only `README.md`, `driver/README.md` (Phase 5 doc territory), CMakeLists.txt (being replaced), and `.planning/` docs reference them. No `.github/workflows/`, no `.gitlab-ci.yml`. |
| A6 | `app_key = "bigscreen.micmap"` remains stable across v1.0, v1.1, v2.x | §Open Question 8 | Low — locked by PROJECT.md Key Decisions and D-21. |
| A7 | `arguments: "--minimized"` string form (not array form) continues to work in Inno-installed manifests | §Phase Requirements | Very low — empirically verified on Bigscreen Beyond + Win11 per Phase 3 Plan 03-02 A2 resolution (STATE.md). No change in Phase 4. |
| A8 | Inno Setup 6.7.1 is the current stable. 6.7.2 or 6.8 not yet released as of 2026-04-23 | §Standard Stack | Low — verified by jrsoftware.org/isdl.php search result. |

## Open Questions

1. **AppId GUID choice** — CONTEXT.md defers to planner. Recommend `uuidgen` at plan time, document in plan artifact as "MicMap v1.0 AppId locked forever" with the literal GUID. No research needed; decision only.
2. **Start Menu / Desktop shortcut** — CONTEXT.md defers. Recommendation: **none**. Auto-start makes shortcuts redundant. System tray icon is the user-facing surface. Uninstaller reaches users via Add/Remove Programs.
3. **Installer icon source** — CONTEXT.md defers. Recommendation: reuse `apps/micmap/micmap.rc`'s existing `.ico` to keep a single visual identity across exe + installer + tray. No new `.ico` file needed.
4. **Whether `--unpatch-bindings` should fully delete `vrcompositor_bindings_lighthouse_hmd.json`** (written by `EnsureControllerTypeFiles` for Bigscreen Beyond path) — D-11 says "restore from backup OR clear marker key." For this file Valve never shipped a version, so there's no backup to restore. **Recommendation: delete the file if marker present.** Covered in §Open Question 6 snippet.
5. **Whether to run `[UninstallRun]` `vrpathreg removedriver` BEFORE or AFTER the app CLI modes** — current recommendation (§Open Question 2) runs it LAST, so if uninstaller fails mid-way, the driver is still bindings-restored and manifest-unregistered. Order matters; planner verify.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| ISCC.exe (Inno Setup 6.7.1+) | CMake `package` target | Planner verifies at plan time | Expect 6.7.1 | Error at `find_program`; install via [jrsoftware.org/isdl.php](https://jrsoftware.org/isdl.php) |
| CMake 3.20+ | Build orchestration | ✓ (already in use) | — | — |
| MSVC (VS 2022) | C++17 compile | ✓ (already in use) | — | — |
| `vrpathreg.exe` | Runtime install action | Only on end-user machine with SteamVR | — | `FileExists` gate (Pitfall 10) — skip silently if absent |
| `micmap.exe` CLI modes | `[Run]` / `[UninstallRun]` | Built by Phase 4 | v1.0 | — |
| Windows 10+ / Windows 11 | Target OS | ✓ (dev rig) | — | — |
| Clean Win11 VM for UAT | Phase exit validation per ROADMAP success criterion #1 | Planner verifies at plan time | — | Manual-only — document as CLAUDE-GAP-HUMAN task per Validation Architecture §VM below |

**Missing dependencies with no fallback:** None at research time.

**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest (existing — see `CMakeLists.txt:91` `enable_testing()` + `tests/`) |
| Config file | CMake-managed; per-test via `add_test()` in `tests/CMakeLists.txt` hierarchy |
| Quick run command | `ctest --test-dir build -C Release --output-on-failure -R installer_` |
| Full suite command | `ctest --test-dir build -C Release --output-on-failure` |
| Existing test registrations | `test_config_manager`, `test_manifest_registrar`, `test_vrmanifest_schema`, `test_cli_flags`, `test_vr_input_events` (all Phase 1-3 deliverables) |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| INST-01 | `.iss` compiles without errors; produces `.exe` | smoke (build) | `cmake --build build --target package --config Release` | ❌ Wave 0 (new `package` target) |
| INST-01 | `.iss` syntax + `/D` defines parse correctly | unit (ISCC invocation) | `ISCC /DMICMAP_VERSION=0.0.0-test /DSTAGE_DIR=tests/fixtures/stage /DOUTPUT_DIR=build/test-installer installer/MicMap.iss` | ❌ Wave 0 |
| INST-01 | AppId GUID stable (no drift between builds) | unit (grep) | `grep "AppId={{.*-.*-.*" installer/MicMap.iss \| wc -l` equals 1 | ❌ Wave 0 |
| INST-02 | WMI `IsProcessRunning` returns True for live process | unit (Pascal test — use `ISCC /OM+` compile + manual run? OR wrap in C++ test) | Not directly testable — WMI is runtime-only. **See Wave 0 gap + VM below.** | — |
| INST-02 | `restartreplace` flag present on every `.dll` in `[Files]` | unit (grep) | `grep -E '\.dll";.*Flags:.*restartreplace' installer/MicMap.iss \| wc -l` ≥ 1 | ❌ Wave 0 |
| INST-03 | `vrpathreg removedriver` appears BEFORE `adddriver` in `.iss` | unit (grep + ordering) | `awk '/vrpathreg.exe.*(removedriver\|adddriver)/ {print NR, $0}' installer/MicMap.iss` — removedriver line < adddriver line | ❌ Wave 0 |
| INST-03 | `FileExists` guard (`Check: VrpathregExists`) on every vrpathreg line | unit (grep) | Count lines with `vrpathreg.exe` then count `VrpathregExists` — must match | ❌ Wave 0 |
| INST-04 | `[Run]` or `Exec()` invokes `--register-vrmanifest` | unit (grep) | `grep "register-vrmanifest" installer/MicMap.iss` returns ≥1 | ❌ Wave 0 |
| INST-05 | `Uninstallable=yes` directive present | unit (grep) | `grep "^Uninstallable=yes" installer/MicMap.iss` returns 1 | ❌ Wave 0 |
| INST-05 | `[UninstallRun]` has symmetric `--unregister-vrmanifest` + `vrpathreg removedriver` | unit (grep) | `grep -A2 '\[UninstallRun\]' installer/MicMap.iss` contains both | ❌ Wave 0 |
| INST-07 | `cmake --build --target package` produces `MicMap-Setup-v{version}.exe` | smoke (build artifact check) | `test -f build/installer/MicMap-Setup-v${PROJECT_VERSION}.exe` | ❌ Wave 0 |
| INST-07 | `find_program(ISCC)` fails gracefully when ISCC absent | unit (CMake) | `cmake -B build-test-no-iscc -DISCC_EXECUTABLE=NOTFOUND` expects configure failure with actionable message | ❌ Wave 0 |
| INST-08 | `--patch-bindings` CLI exit 0 on clean config | integration (C++ test of `PatchGenericHmdBindings`) | `ctest -R test_bindings_patcher` (requires Wave 0 refactor per D-10) | ❌ Wave 0 (requires `micmap_bindings` lib to exist) |
| INST-08 | `--patch-bindings` idempotent (second call = no-op, rc=0) | integration | Same test, call twice, assert both rc=0 + file unchanged second call | ❌ Wave 0 |
| INST-08 | `--unpatch-bindings` restores from backup if present | integration | Seed file w/ patch + backup, run unpatch, diff against backup | ❌ Wave 0 |
| INST-08 | `--unpatch-bindings` clears marker-in-place if no backup | integration | Seed file w/ patch + NO backup, run unpatch, assert marker gone + bindings keys gone | ❌ Wave 0 |
| INST-08 | `.micmap_backup` write-once semantics | integration | Seed existing backup w/ known content, run patch, assert backup unchanged | ❌ Wave 0 |
| D-10 | `bindings_patcher` shared-lib refactor preserves driver behavior | integration (existing driver tests reused against new target) | Re-run driver-side bindings patch test after refactor | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build -C Release --output-on-failure -R bindings_patcher\|cli_flags\|manifest_registrar` (fast subset focused on refactored surfaces)
- **Per wave merge:** `cmake --build build --target package --config Release` + `ctest --test-dir build -C Release --output-on-failure` (full build + full ctest)
- **Phase gate:** Full ctest green + full `package` target produces valid installer + **VM UAT pass** (see below)

### Wave 0 Gaps

- [ ] `tests/test_bindings_patcher.cpp` — unit/integration test of `micmap::bindings::PatchGenericHmdBindings()` + `UnpatchGenericHmdBindings()` over filesystem fixtures. Covers INST-08, D-08, D-11.
- [ ] `tests/CMakeLists.txt` — register `test_bindings_patcher`, link `micmap_bindings`.
- [ ] `src/bindings/` directory + `CMakeLists.txt` — per D-10 refactor (see §Open Question 6).
- [ ] `src/bindings/include/micmap/bindings/bindings_patcher.hpp` — public header with LogSink + symmetric patch/unpatch API.
- [ ] `src/bindings/src/bindings_patcher.cpp` — relocated + depends-on-LogSink version.
- [ ] `driver/CMakeLists.txt` edit — remove `src/bindings_patcher.cpp` from SOURCES, add `target_link_libraries(driver_micmap PRIVATE micmap_bindings)`.
- [ ] `apps/micmap/CMakeLists.txt` edit — link `micmap_bindings` (via micmap_lib transitively or direct).
- [ ] `apps/micmap/main.cpp` edit — add `--patch-bindings` / `--unpatch-bindings` to CLI fork (mirrors Phase 3 `--register-vrmanifest` at lines 696-730).
- [ ] `src/common/include/micmap/common/cli_flags.hpp` edit — add `bool patchBindings` / `bool unpatchBindings` fields to `CliFlags`.
- [ ] `src/common/src/cli_flags.cpp` edit — parse new flags.
- [ ] `installer/` directory + `MicMap.iss` — primary Phase 4 artifact.
- [ ] `tests/iss_lint.py` (or a CMake custom target) — grep-based static lint of `.iss` content per the grep-based unit tests above. Can be run as an `add_test(NAME iss_static_lint COMMAND ...)` entry.
- [ ] `CMakeLists.txt` edit — remove `add_custom_target(copy_distributable_files ...)` (lines 113-135) + batch-script configure_file loop (lines 104-109), add `find_program(ISCC_EXECUTABLE)` + `add_custom_target(package)`, add `install(FILES app.vrmanifest ...)`.
- [ ] `scripts/` deletion — `install_driver.bat`, `uninstall_driver.bat`, `install_driver_test.bat`, `test_driver.bat`.
- [ ] `REQUIREMENTS.md` edit — mark INST-06 as "N/A per D-07" (deferred to phase close per D-07).

### Manual/VM-gated validation (CLAUDE-GAP-HUMAN tasks)

- [ ] **VM End-to-end install → uninstall cycle** — Clean Win11 VM, install Steam + SteamVR, run installer with SteamVR closed, verify: driver loaded by SteamVR, `app.vrmanifest` registered, `--patch-bindings` applied, uninstall reverses all of the above, `vrpathreg show` no longer lists MicMap. Owns ROADMAP Success Criterion #1.
- [ ] **VM WMI gate UX** — Launch SteamVR mid-wizard, verify WMI dialog names all running processes, Retry re-checks, Cancel aborts cleanly.
- [ ] **VM Same-version reinstall** — Install v1.0, re-run v1.0 installer, verify: `vrpathreg show` lists MicMap exactly once (Pitfall 3 prevention), bindings still patched, manifest still registered, no duplicate SteamVR entries in "Manage Startup Overlay Apps."
- [ ] **Upgrade UAT (deferred until v1.1)** — Not achievable in v1.0; documented for future milestone.

## Security Threat Properties (for planner's `threat_model` block)

MicMap installer runs **admin-elevated** per INST-01. Admin elevation is a privilege-boundary crossing; the planner MUST encode the following threat properties:

### Threat: DLL planting in staging dir
- **STRIDE:** Tampering (T)
- **Surface:** `build/stage/` intermediate directory populated by `cmake --install`.
- **Attack:** Non-admin local user with write access to `build/` (dev machine) drops a malicious `driver_micmap.dll` into `build/stage/driver/micmap/bin/win64/` between the `cmake --install` step and the `ISCC.exe` step. ISCC packages the malicious DLL into the installer. End user runs the admin-elevated installer, DLL lands in `{SteamVR}\drivers\micmap\bin\win64\` and is loaded by `vrserver.exe` (admin? actually vrserver runs user-mode, but privileged access to SteamVR internals).
- **Mitigation:**
  - `build/` is under the developer's controlled workspace; standard dev-hygiene applies.
  - The `package` target runs `cmake --install` → `ISCC` back-to-back in a single CMake invocation. No pausing between steps.
  - Code-signing in v1.x (D-16 deferred) is the definitive fix — attacker can't re-sign with the project's cert. For v1.0 unsigned, document the `build/stage/` cleanliness expectation.
- **Acceptance for v1.0:** Accept. Dev-machine trust model.

### Threat: Path injection via Steam registry value
- **STRIDE:** Tampering + Elevation of Privilege (T + E)
- **Surface:** `HKCU\Software\Valve\Steam\SteamPath` read in `InitializeSetup`.
- **Attack:** Malicious value (e.g., `C:\evil; start C:\evil.exe`) in `SteamPath` registry key. Unquoted concatenation into `[Run]` parameters could enable command injection.
- **Mitigation:**
  - Path is always **quoted** in `[Run]` parameters (`"{app}"` uses Inno's built-in quoting).
  - Paths are **validated** via `DirExists({SteamVR})` before use — non-existent paths abort install.
  - Path is used only for `DefaultDirName` and `vrpathreg.exe` location — not passed to `cmd.exe` / shell-exec contexts.
  - HKCU is user-writable, so "attacker" = user themselves. Not a real threat boundary.
- **Acceptance for v1.0:** Mitigated by quoting + `DirExists` guard.

### Threat: Unauthorized modification outside intended scope
- **STRIDE:** Tampering (T)
- **Surface:** Installer runs admin; in principle could write anywhere on C:.
- **Attack:** Installer bug or attacker-modified `.iss` writes beyond `{SteamVR}\drivers\micmap\` + `%APPDATA%\MicMap\`.
- **Mitigation — installer writes ONLY to:**
  1. `{SteamVR}\drivers\micmap\` (and subdirs) — driver + app binaries
  2. `{SteamVR}\resources\config\vrcompositor_bindings_generic_hmd.json` — via `--patch-bindings`
  3. `{SteamVR}\resources\config\vrcompositor_bindings_lighthouse_hmd.json` + `lighthouse_hmd_profile.json` — via `--patch-bindings` (new files)
  4. `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` — via `vrpathreg`
  5. `%LOCALAPPDATA%\openvr\appconfig.json` — via `--register-vrmanifest`
  6. `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}_is1` — standard Inno uninstall entry
  7. `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\PendingFileRenameOperations` — via `restartreplace` (only when DLL locked)
- **Explicitly does NOT write:**
  - `%APPDATA%\MicMap\*` — owned by `micmap.exe` at runtime; installer never touches (anti-feature per REQUIREMENTS.md "Out of Scope")
  - `HKCU\Software\Valve\*` — read-only
  - Any other Steam files
- **Acceptance for v1.0:** Documented scope; enforced by code review (grep `.iss` for file paths, verify all in allowlist).

### Threat: `[UninstallRun]` deletion scope
- **STRIDE:** Tampering + Denial of Service (T + D)
- **Surface:** Uninstaller running admin; could in principle `DelTree` outside MicMap scope.
- **Attack:** Uninstaller bug deletes `{SteamVR}\resources\` or other Valve-owned paths.
- **Mitigation:**
  - Uninstaller `[UninstallDelete]` limited to `{app}\*` (MicMap's nested driver dir). Inno auto-enumerates `[Files]` entries for reverse removal.
  - `vrpathreg removedriver` operates on **registration metadata only** — never deletes files outside `{app}`.
  - `--unpatch-bindings` touches only the three config JSON files in §Unauthorized Modification #3 above. **MUST verify** (via code review) that rename-from-backup uses exact `target` path string, no wildcards.
  - `%APPDATA%\MicMap\` deletion is gated by `DelTree(AppDataDir, True, True, True)` where `AppDataDir := ExpandConstant('{userappdata}\MicMap')`. Even if `{userappdata}` returns `""` (it won't, but defensively), concatenation gives `\MicMap` which fails `DirExists` check first. Double-check the `DirExists(AppDataDir)` guard per §Open Question 7 snippet.
- **Acceptance for v1.0:** Mitigated by explicit path pinning + `DirExists` guards + code review.

### Threat: Unquoted paths in `[Run]` / `[UninstallRun]`
- **STRIDE:** Tampering (T — command injection)
- **Attack:** `Filename: {app}\bin\micmap.exe; Parameters: --patch-bindings` — if `{app}` contains a space (e.g., `C:\Program Files\...`), Windows `CreateProcess` might interpret subsequent tokens as separate args.
- **Mitigation:** Inno's `Filename:` field accepts unquoted (Inno quotes automatically). `Parameters:` requires MANUAL `""..."""` quoting for any embedded path. The recommended snippets in §Open Question 2 all quote `"{app}"` in parameters.
- **Acceptance for v1.0:** Enforced by code review; static grep lint: `grep "Parameters:" installer/MicMap.iss` — every line referencing `{app}` or path must wrap in `""..."""`.

### Summary security properties for planner's threat_model block

```yaml
threat_model:
  trust_boundary: admin_elevation
  untrusted_inputs:
    - HKCU\Software\Valve\Steam\SteamPath   # registry value
    - process list from WMI                 # runtime state
    - existing vrcompositor_bindings_generic_hmd.json  # file that installer reads before patching
  write_scope:
    - "{SteamVR}\\drivers\\micmap\\**"
    - "{SteamVR}\\resources\\config\\vrcompositor_bindings_generic_hmd.json"  # via --patch-bindings
    - "{SteamVR}\\resources\\config\\vrcompositor_bindings_lighthouse_hmd.json"  # via --patch-bindings
    - "{SteamVR}\\resources\\config\\lighthouse_hmd_profile.json"  # via --patch-bindings
    - "%LOCALAPPDATA%\\openvr\\openvrpaths.vrpath"  # via vrpathreg
    - "%LOCALAPPDATA%\\openvr\\appconfig.json"  # via --register-vrmanifest
    - "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{AppId}_is1"  # standard Inno
    - "%APPDATA%\\MicMap\\**"  # ONLY on uninstall if user consents; installer does NOT write on install
  forbidden_writes:
    - "%APPDATA%\\MicMap\\**"  # on install — owned by micmap.exe
    - "{SteamVR}\\bin\\**"     # Valve-owned
    - "{SteamVR}\\drivers\\<anything other than micmap>\\**"
  mitigations:
    - path_quoting: all [Run] Parameters use ""{app}"" pattern
    - direxists_guards: InitializeSetup validates DirExists({SteamVR})
    - fileexists_guards: all vrpathreg lines gated by Check:VrpathregExists
    - runhidden_flag: all [Run] entries (no console window reveal for CLI work)
    - waituntilterminated: synchronous exec for ordering guarantees
  unsigned_caveat: v1.0 unsigned per D-16; SmartScreen warning acceptable; code-signing deferred to v1.x
```

## Project Constraints (from CLAUDE.md)

- **Windows-only** — installer targets Windows 10/11 exclusively; no Linux/macOS build path.
- **Stack locked this milestone** — C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. **No framework changes.** The `bindings_patcher` lift (D-10) is a relocation, not a new dep.
- **Bash available via Git Bash** — use Unix-style paths in shell commands (forward slashes, `/dev/null`). Applies to CMake invocations, not to `.iss` contents (which use Windows paths natively).
- **Installer runs elevated (admin); runtime does NOT require admin** — the `micmap.exe` overlay app runs user-mode; only the installer/uninstaller elevate. This keeps the admin blast radius minimal.
- **Do not skip phase artifacts** — RESEARCH.md / PLAN.md / SUMMARY.md are project memory. This research document is one such artifact.

## Sources

### Primary (HIGH confidence)

- [Inno Setup Help — Pascal Scripting: Event Functions](https://jrsoftware.org/ishelp/topic_scriptevents.htm) — PrepareToInstall / InitializeSetup / CurStepChanged / CurUninstallStepChanged signatures and return value semantics.
- [Inno Setup Help — [Files] section / Flags](https://jrsoftware.org/ishelp/topic_filessection.htm) — `restartreplace` / `uninsrestartdelete` / `ignoreversion` semantics.
- [Inno Setup Help — [Run] & [UninstallRun] sections](https://jrsoftware.org/ishelp/topic_runsection.htm) — complete flag list, exit-code behavior (none built-in).
- [Inno Setup Help — [Setup]: AppId](https://jrsoftware.org/ishelp/topic_setup_appid.htm) — upgrade-in-place mechanics.
- [Inno Setup Help — [Setup]: UsePreviousAppDir](https://jrsoftware.org/ishelp/topic_setup_usepreviousappdir.htm) — default behavior.
- [Inno Setup Help — Architecture Identifiers](https://jrsoftware.org/ishelp/topic_archidentifiers.htm) — `x64os` vs `x64compatible` vs deprecated `x64`.
- [Inno Setup Help — [Setup]: ArchitecturesAllowed](https://jrsoftware.org/ishelp/topic_setup_architecturesallowed.htm) — full syntax + boolean expressions.
- [Inno Setup Help — Pascal Scripting: TaskDialogMsgBox](https://jrsoftware.org/ishelp/topic_isxfunc_taskdialogmsgbox.htm) — signature + example.
- [Inno Setup Help — Pascal Scripting: CreateOleObject](https://jrsoftware.org/ishelp/topic_isxfunc_createoleobject.htm) — WMI COM automation.
- [Inno Setup Help — Pascal Scripting: Exec](https://jrsoftware.org/ishelp/topic_isxfunc_exec.htm) — ResultCode out-parameter.
- [Inno Setup Help — Pascal Scripting: Abort](https://jrsoftware.org/ishelp/topic_isxfunc_abort.htm) — silent-exception context rules.
- [Inno Setup Help — Command Line Compiler Execution](https://jrsoftware.org/ishelp/topic_compilercmdline.htm) — ISCC.exe `/D` defines, exit codes 0/1/2.
- [Inno Setup Downloads](https://jrsoftware.org/isdl.php) — 6.7.1 current stable verification.
- [Inno Setup 6 Revision History](https://jrsoftware.org/files/is6-whatsnew.htm) — 6.3 deprecation of `x64` identifier.
- [ValveSoftware/openvr wiki — Local Driver Registration](https://github.com/ValveSoftware/openvr/wiki/Local-Driver-Registration) — `vrpathreg.exe` canonical command syntax.
- [ValveSoftware/openvr issue #1653 — vrpathreg allows duplicate path registration](https://github.com/ValveSoftware/openvr/issues/1653) — Pitfall 3 root cause.
- `driver/src/bindings_patcher.cpp` (in repo, read during research) — reference for D-08 write-once backup, D-11 marker semantics, D-12 openvrpaths parse.
- `apps/micmap/main.cpp:696-730` (in repo, read during research) — Phase 3 CLI fork pattern to mirror for `--patch-bindings` / `--unpatch-bindings`.
- `apps/micmap/CMakeLists.txt:22` (in repo, read during research) — `MICMAP_VERSION` source of truth; `app.vrmanifest` generation via `configure_file`.
- `CMakeLists.txt:113-147` (in repo, read during research) — existing install rules + `copy_distributable_files` target being replaced.
- `driver/CMakeLists.txt:138-151` (in repo, read during research) — existing driver install rules.

### Secondary (MEDIUM confidence)

- [CMake 4.3.1 Documentation — CPack Inno Setup Generator](https://cmake.org/cmake/help/latest/cpack_gen/innosetup.html) — documentation of CPack integration we are NOT using (D-18 rejected) but useful for understanding the "alternative" path.
- [Stack Overflow pattern — RegQueryStringValue for SteamPath](https://stackoverflow.com/) (multiple answers converged) — used to build the `GetSteamPath()` snippet in §Open Question 1f.
- [The Road to Delphi — Accessing the WMI from Pascal code](https://theroadtodelphi.com/2010/12/01/accesing-the-wmi-from-pascal-code-delphi-oxygene-freepascal/) — WMI COM pattern (adapted to Inno Pascal Script).
- [HeliumProject/InnoSetup examples on GitHub — CodePrepareToInstall.iss](https://github.com/HeliumProject/InnoSetup/blob/master/Examples/CodePrepareToInstall.iss) — shape of `PrepareToInstall` implementations (though does not show a loop).
- [Inno Setup FAQ — Process detection and restartreplace](https://jrsoftware.org/isfaq.php) — `restartreplace` admin requirement + HKLM Session Manager mechanics.
- `.planning/research/SUMMARY.md` (in repo) — Pitfalls 3, 4, 10, 15, 16 source material (cross-referenced; the installer-side crystallizations in this document are the planner-consumable form).
- `.planning/phases/03-auto-start/03-CONTEXT.md` (in repo) — Phase 3 `--register-vrmanifest` / `--unregister-vrmanifest` CLI contract that `--patch-bindings` mirrors.

### Tertiary (LOW confidence, pending direct access)

- `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — **NOT accessible from this shell session** (confirmed via `ls`). Planner/executor must Read this file directly at plan/exec time for verbatim Pascal snippets (WMI loop, registry lookup, TaskDialog wording). The generic patterns in this document are sufficient for Plan creation; verbatim snippets are a nice-to-have polish.
- `D:\Documents\Projects\bey-closer-t1\.planning\RETROSPECTIVE.md` — **NOT accessible from this shell session**. Source of the Pitfall 16 corpus; planner should Read directly at plan time if possible.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — Inno Setup 6.7.1 verified as current stable (2026-02-17); all APIs used are core Pascal Script primitives stable since IS 5 or IS 6 GA; no deprecated Pascal calls needed.
- Architecture: HIGH — pre-locked by CONTEXT.md D-01 through D-21; this research fills in mechanics only, not architecture choices.
- Pitfalls: HIGH — Pitfalls 3, 4, 10, 15 cross-verified with multiple sources (official docs + OpenVR issues + independent implementations). Pitfall 16 HIGH on general Pascal Script class-of-bug list; MEDIUM on bey-closer-t1 specifics (reference path not accessible this session).
- Validation architecture: HIGH — CTest existing + grep-based static lint is a well-established Inno Setup CI pattern.
- Security threat properties: HIGH — scope is narrow and explicit; all attack surfaces enumerated; mitigations tied to specific snippets.

**Research date:** 2026-04-23
**Valid until:** 2026-07-23 (3 months — Inno Setup 6.x minor versions release every 1-3 months; no API break expected in that window but worth re-verifying if Phase 4 slips past July).

---

*Research completed: 2026-04-23*
*Ready for planning: yes*

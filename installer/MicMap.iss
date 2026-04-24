; MicMap Installer -- Inno Setup 6.7.1 script
; Phase 4 INST-01..INST-08 (see .planning/phases/04-installer/)
; AppId frozen 2026-04-23 -- DO NOT regenerate (Inno upgrade-in-place depends on it)

; ---------------------------------------------------------------
; Preprocessor guards (set by CMake `package` target via /D defines)
; ---------------------------------------------------------------
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
AppId={{BC6D91A7-A852-4562-8CBF-58FC4662FEDC}
AppName=MicMap
AppVersion={#MICMAP_VERSION}
AppPublisher=MicMap
AppPublisherURL=https://github.com/mic-map
DefaultDirName={code:GetMicMapInstallDir}
DisableDirPage=yes
DisableWelcomePage=no
DisableProgramGroupPage=yes
WizardStyle=modern
PrivilegesRequired=admin
; D-17 -- x64os (NOT deprecated x64 per IS 6.3+; refuses ARM64 explicitly)
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
Uninstallable=yes
OutputDir={#OUTPUT_DIR}
OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}
UsePreviousAppDir=yes
UsePreviousSetupType=yes
UsePreviousTasks=yes
; WE handle closing-SteamVR in PrepareToInstall (Plan 06) -- disable Inno's built-in UI
CloseApplications=no
SetupIconFile=micmap.ico
UninstallDisplayIcon={app}\bin\micmap.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; D-01 nested layout: {app} == {SteamVR}\drivers\micmap
; Driver DLL at {app}\bin\win64 (matches driver/CMakeLists.txt install RUNTIME DESTINATION)
; Pitfall 4 (D-06 defense-in-depth): restartreplace + uninsrestartdelete on the DLL
Source: "{#STAGE_DIR}\drivers\micmap\bin\win64\driver_micmap.dll"; \
  DestDir: "{app}\bin\win64"; \
  Flags: ignoreversion restartreplace uninsrestartdelete

Source: "{#STAGE_DIR}\drivers\micmap\driver.vrdrivermanifest"; \
  DestDir: "{app}"; \
  Flags: ignoreversion

Source: "{#STAGE_DIR}\drivers\micmap\resources\*"; \
  DestDir: "{app}\resources"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

; App binaries at {app}\bin (D-03)
Source: "{#STAGE_DIR}\bin\micmap.exe"; \
  DestDir: "{app}\bin"; \
  Flags: ignoreversion

Source: "{#STAGE_DIR}\bin\app.vrmanifest"; \
  DestDir: "{app}\bin"; \
  Flags: ignoreversion

; ---------------------------------------------------------------
; [Run] / [UninstallRun] intentionally OMITTED.
; Plan 07 adds orchestration via CurStepChanged(ssPostInstall) + Exec()
; per 04-RESEARCH.md Open Question 9 Technique B (ResultCode inspection).
; ---------------------------------------------------------------

[Code]
// ---------------------------------------------------------------
// Plan 05: SteamVR registry resolution (D-02) + D-04 no-Steam abort
// Module-level g_SteamVRDir is populated by InitializeSetup and
// reused by Plans 06 (WMI gate), 07 (vrpathreg), and 08 (teardown).
// ---------------------------------------------------------------
var
  g_SteamVRDir: String;  // Resolved once in InitializeSetup. Reused by Plans 06/07/08.

function GetSteamPath(): String;
var
  SteamPath: String;
begin
  Result := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Valve\Steam',
                         'SteamPath', SteamPath) then
  begin
    // HKCU SteamPath uses forward slashes (e.g. "C:/Program Files (x86)/Steam").
    // Normalize to backslashes for [Files] Source path composition.
    StringChangeEx(SteamPath, '/', '\', True);
    Result := SteamPath;
  end;
end;

function InitializeSetup(): Boolean;
var
  SteamPath: String;
begin
  Result := False;
  SteamPath := GetSteamPath();
  if SteamPath = '' then
  begin
    MsgBox('SteamVR was not detected via HKCU\Software\Valve\Steam\SteamPath.' +
           Chr(13) + Chr(10) + Chr(13) + Chr(10) +
           'Please install Steam and SteamVR, then re-run this installer.',
           mbError, MB_OK);
    Exit;
  end;
  g_SteamVRDir := SteamPath + '\steamapps\common\SteamVR';
  if not DirExists(g_SteamVRDir) then
  begin
    MsgBox('Steam is installed, but SteamVR was not found at:' + Chr(13) + Chr(10) +
           g_SteamVRDir + Chr(13) + Chr(10) + Chr(13) + Chr(10) +
           'Please install SteamVR via Steam, then re-run this installer.',
           mbError, MB_OK);
    Exit;
  end;
  Result := True;  // proceed -- g_SteamVRDir now available for the rest of the run
end;

function GetMicMapInstallDir(Param: String): String;
begin
  // D-01: nested layout {SteamVR}\drivers\micmap.
  // Relies on g_SteamVRDir being populated by InitializeSetup.
  Result := g_SteamVRDir + '\drivers\micmap';
end;

// ---------------------------------------------------------------
// Plan 06: SteamVR-running WMI gate (INST-02, D-05 + D-06)
// Runs in PrepareToInstall AFTER the wizard's "Ready to Install"
// page and BEFORE [Files] copy begins. Prompt-and-retry loop names
// the exact running processes; no force-kill anywhere (D-05 anti-kill policy).
// Defense-in-depth: `restartreplace` on driver_micmap.dll (Plan 04).
// ---------------------------------------------------------------
function IsProcessRunning(const ExeName: String): Boolean;
var
  Locator, Service, ProcSet: Variant;
  Query: String;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Query := Format('SELECT Name FROM Win32_Process WHERE Name = "%s"', [ExeName]);
    ProcSet := Service.ExecQuery(Query, 'WQL', 48);
      // 48 = wbemFlagForwardOnly (32) | wbemFlagReturnImmediately (16)
    // SWbemObjectSet.Count -- community-proven variant property supported by
    // Inno Setup's Pascal Script (RemObjects PascalScript does NOT declare
    // IEnumVariant, so we cannot use the Delphi-native ._NewEnum iteration
    // from 04-RESEARCH.md verbatim). Semantically identical for the True/False
    // "is a matching process present" question: Count > 0 iff at least one row.
    Result := (ProcSet.Count > 0);
  except
    // Pitfall 16 #3: WMI service down / permission denied / OLE create failed --
    // fail OPEN (treat as "not running"). A broken WMI stack MUST NOT block the
    // installer. A user with SteamVR actually running will hit file-copy
    // conflicts later -- restartreplace on the DLL handles that race.
  end;
end;

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
    begin
      if Combined = '' then
        Combined := Names[i]
      else
        Combined := Combined + ', ' + Names[i];
    end;
  Result := Combined;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Running: String;
  Response: Integer;
  CRLF: String;
begin
  // ISPP-safe CRLF (Chr() concat -- no #-prefix char-literals; see Plan 05 hand-off).
  CRLF := Chr(13) + Chr(10);
  Result := '';  // default = proceed
  Running := GetRunningSteamVrProcesses();
  while Running <> '' do
  begin
    Response := MsgBox(
      'SteamVR is currently running (' + Running + ').' + CRLF + CRLF +
      'Please close SteamVR completely, then click Retry.' + CRLF +
      '(Closing your VR session is required -- MicMap will not force-quit SteamVR.)',
      mbConfirmation, MB_RETRYCANCEL);
    if Response = IDCANCEL then
    begin
      Result := 'Setup was cancelled because SteamVR is still running.';
      Exit;
    end;
    // IN-03: give vrserver a moment to clear on a racing shutdown. WMI can
    // report a phantom "running" state for a few seconds while vrserver is
    // in WAITING_FOR_EXIT. Without this pause a quick Retry loops the user
    // straight back into the same dialog.
    Sleep(500);
    Running := GetRunningSteamVrProcesses();
  end;
end;

// ---------------------------------------------------------------
// Plan 07: Post-install orchestrator (INST-03, INST-04, INST-08)
// Runs in CurStepChanged(ssPostInstall) AFTER [Files] copy completes,
// BEFORE the wizard's Finished page. Four Exec() steps in fixed order
// with ResultCode inspection (Technique B per 04-RESEARCH.md Open
// Question 9) -- [Run] silently ignores non-zero exit codes (Pitfall 17).
// Every vrpathreg.exe call gated by VrpathregExists (Pitfall 10).
// Shared helpers GetVrpathreg + VrpathregExists reused by Plan 08.
// ---------------------------------------------------------------
function GetVrpathreg(Param: String): String;
begin
  // Pitfall 15: vrpathreg.exe lives under {SteamVR}\bin\win64 (NOT {app}\bin\win64).
  // g_SteamVRDir populated by InitializeSetup (Plan 05).
  Result := g_SteamVRDir + '\bin\win64\vrpathreg.exe';
end;

function VrpathregExists(): Boolean;
begin
  // Pitfall 10: Steam uninstalled independently -> vrpathreg missing -> skip cleanly.
  Result := FileExists(GetVrpathreg(''));
end;

// IN-10: defense-in-depth quoting helper. {app} is presently derived from
// GetMicMapInstallDir with DisableDirPage=yes, so it cannot contain '"'
// (Windows file-system paths forbid it). But if a future refactor ever
// lets user input reach {app} (e.g. re-enabling the dir page, or a task
// that influences path resolution), the raw AppDir interpolation below
// becomes a vrpathreg CLI-injection sink. Doubling embedded quotes closes
// the door preemptively; on today's code path this is a no-op.
function QuoteExecArg(Arg: String): String;
begin
  StringChangeEx(Arg, '"', '""', True);
  Result := Arg;
end;

procedure RunVrpathregRemove(AppDir: String);
var
  IgnoredRC: Integer;
begin
  // Pitfall 3: unconditional removedriver BEFORE adddriver prevents OpenVR #1653
  // duplicate-entry bug. rc is INTENTIONALLY ignored -- removedriver on an
  // unregistered path is a no-op (rc=0 or 1, either is fine).
  // IN-04: variable renamed IgnoredRC to make the "don't read this" intent
  // load-bearing in the signature; Exec() requires an out-parameter here.
  if VrpathregExists() then
    Exec(GetVrpathreg(''), 'removedriver "' + QuoteExecArg(AppDir) + '"', '', SW_HIDE, ewWaitUntilTerminated, IgnoredRC);
end;

function RunVrpathregAdd(AppDir: String): String;
var
  ResultCode: Integer;
begin
  // Pitfall 15: pass "{app}" which IS the manifest dir (contains
  // driver.vrdrivermanifest), NOT {app}\bin\win64 which is the DLL dir.
  Result := '';
  if not VrpathregExists() then
    Exit;
  if (not Exec(GetVrpathreg(''), 'adddriver "' + QuoteExecArg(AppDir) + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    Result := 'vrpathreg adddriver (rc=' + IntToStr(ResultCode) + ')';
end;

function RunRegisterVrmanifest(MicMapExe: String): String;
var
  ResultCode: Integer;
begin
  // Phase 3 D-03 exit contract: 0=success, 1=failure.
  Result := '';
  if (not Exec(MicMapExe, '--register-vrmanifest', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    Result := 'micmap.exe --register-vrmanifest (rc=' + IntToStr(ResultCode) + ')';
end;

function RunPatchBindings(MicMapExe: String): String;
var
  ResultCode: Integer;
begin
  // Plan 02 CLI exit contract: 0=success, 1=failure. NOT catastrophic --
  // driver-side patcher re-runs on every driver init as defensive fallback.
  Result := '';
  if (not Exec(MicMapExe, '--patch-bindings', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    Result := 'micmap.exe --patch-bindings (rc=' + IntToStr(ResultCode) + ')';
end;

procedure TryStep(Failed: TStringList; StepResult: String);
begin
  // Append non-empty failure descriptions to the aggregator.
  if StepResult <> '' then
    Failed.Add(StepResult);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Failed: TStringList;
  AppDir, MicMapExe, CRLF: String;
begin
  if CurStep <> ssPostInstall then Exit;
  CRLF := Chr(13) + Chr(10);
  AppDir := ExpandConstant('{app}');
  MicMapExe := AppDir + '\bin\micmap.exe';
  Failed := TStringList.Create;
  try
    RunVrpathregRemove(AppDir);                        // Step 1: rc ignored (Pitfall 3)
    TryStep(Failed, RunVrpathregAdd(AppDir));          // Step 2: INST-03
    TryStep(Failed, RunRegisterVrmanifest(MicMapExe)); // Step 3: INST-04
    TryStep(Failed, RunPatchBindings(MicMapExe));      // Step 4: INST-08
    if Failed.Count > 0 then
      MsgBox('MicMap installed, but some post-install steps failed:' + CRLF + CRLF +
             Failed.Text + CRLF +
             'MicMap will still work for basic dashboard toggling. ' +
             'If problems persist, see %APPDATA%\MicMap\micmap.log and re-run the installer.',
             mbInformation, MB_OK);
  finally
    Failed.Free;
  end;
end;

// ---------------------------------------------------------------
// Plan 08: Uninstall teardown orchestrator (INST-05, INST-06)
// Runs in CurUninstallStepChanged(usUninstall) BEFORE unins000.exe
// deletes files. Teardown order REVERSED from install (D-09 mirror-
// image contract): --unpatch-bindings -> --unregister-vrmanifest ->
// vrpathreg removedriver. No [UninstallRun] block (Pitfall 17 -- same
// silent-rc hazard as [Run]). Reuses Plan 07's GetVrpathreg +
// VrpathregExists helpers verbatim -- MUST NOT redeclare.
// ---------------------------------------------------------------
procedure SweepLegacyBindings();
// Defense-in-depth sweep. D-07 voids INST-06 as a literal ghost-cleanup
// requirement (no 0.x ever shipped), but this scan codifies the invariant
// that %LOCALAPPDATA%\openvr\input\micmap_*.json is never MicMap's
// territory. Safe + idempotent on every real machine: v1.0 does NOT write
// to this directory (the bindings patch targets
// {SteamVR}\resources\config\ via --patch-bindings). If the sweep ever
// finds a match, it logs and removes it -- documenting the boundary for
// future maintainers.
var
  InputDir: String;
  FindRec: TFindRec;
  FilePath: String;
begin
  InputDir := ExpandConstant('{localappdata}\openvr\input');
  if not DirExists(InputDir) then
    Exit;
  if FindFirst(InputDir + '\micmap_*.json', FindRec) then
  try
    repeat
      // LR-01: FindFirst with a wildcard can match directories whose NAME ends in
      // .json (unusual but possible). DeleteFile on a directory silently fails
      // and logs 'FAILED to remove' -- harmless but noisy. Skip dirs explicitly.
      // FILE_ATTRIBUTE_DIRECTORY = 16 (Windows API constant, usable in IS Pascal).
      if (FindRec.Attributes and 16) = 0 then
      begin
        FilePath := InputDir + '\' + FindRec.Name;
        if DeleteFile(FilePath) then
          Log('SweepLegacyBindings: removed ' + FilePath)
        else
          Log('SweepLegacyBindings: FAILED to remove ' + FilePath);
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

procedure PromptAndMaybeRemoveUserData();
// D-13: ask user whether to keep or remove %APPDATA%\MicMap\ (config.json,
// training_data.bin, micmap.log). Resolved path: userappdata/MicMap (Inno
// constant {userappdata} expands at runtime). Default = Keep (training data
// is expensive to regenerate -- ~150 real mic samples). MB_DEFBUTTON2 focuses
// the No button so accidental Enter preserves data.
var
  AppDataDir: String;
  Response: Integer;
  CRLF: String;
begin
  AppDataDir := ExpandConstant('{userappdata}\MicMap');
  if not DirExists(AppDataDir) then
  begin
    // IN-02: log the skip so uninstall troubleshooting can unambiguously confirm
    // "no user data was present" vs. "data was present and removed/kept".
    Log('PromptAndMaybeRemoveUserData: ' + AppDataDir + ' does not exist, nothing to prompt.');
    Exit;
  end;

  // HR-01: /SILENT and /VERYSILENT suppress Inno wizard dialogs but NOT direct
  // MsgBox() calls in Pascal Script. Without this guard the uninstaller would
  // hang waiting for keyboard input that cannot arrive (breaks CI/scripted
  // teardown). Default in silent mode = keep user data (D-13 default).
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

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
  Failed: TStringList;
  AppDir: String;
  MicMapExe: String;
  SteamVRParent: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;

  AppDir := ExpandConstant('{app}');
  MicMapExe := AppDir + '\bin\micmap.exe';

  // MR-01: InitializeSetup only runs during INSTALL -- at uninstall time
  // g_SteamVRDir is empty, so GetVrpathreg('') returns '\bin\win64\vrpathreg.exe'
  // (no root) and VrpathregExists() is silently False, skipping removedriver.
  // Re-derive g_SteamVRDir from {app} (which is {SteamVR}\drivers\micmap per D-01):
  //   ExtractFilePath strips trailing segment + leaves a trailing backslash, so
  //   apply twice and then strip the final backslash. Two parents up from
  //   {SteamVR}\drivers\micmap == {SteamVR}.
  if g_SteamVRDir = '' then
  begin
    SteamVRParent := ExtractFilePath(ExtractFilePath(AppDir));
    g_SteamVRDir := RemoveBackslashUnlessRoot(SteamVRParent);
    Log('Uninstall: resolved g_SteamVRDir from {app} = ' + g_SteamVRDir);
    // IN-09: the two-parents-up derivation assumes {app} == {SteamVR}\drivers\micmap.
    // DisableDirPage=yes + DefaultDirName=GetMicMapInstallDir ensure this for fresh
    // installs, but UsePreviousAppDir=yes could carry over a non-standard {app} from
    // a prior hand-edited install. Log a diagnostic if the derived path doesn't look
    // like a SteamVR layout (missing bin\win64). VrpathregExists() already handles
    // the skip-cleanly path for Pitfall 10; this just improves post-mortem diagnosis.
    if not DirExists(g_SteamVRDir + '\bin\win64') then
      Log('Uninstall: WARNING derived g_SteamVRDir lacks bin\win64 (' + g_SteamVRDir + '); vrpathreg removedriver will be skipped');
  end;
  Failed := TStringList.Create;
  try
    // Steps 1 + 2 (reverse of Plan 07 Steps 4 + 3): micmap.exe-driven teardown.
    // MR-02: single FileExists gate -- Inno Setup does not delete files until
    // usDeleteAppFiles/usPostUninstall (after usUninstall), so micmap.exe cannot
    // vanish between steps. Double-checking opened a latent TOCTOU window where
    // step 2 would silently skip (no Failed.Add) if something deleted the exe
    // mid-teardown.
    if FileExists(MicMapExe) then
    begin
      // Step 1: --unpatch-bindings
      if (not Exec(MicMapExe, '--unpatch-bindings', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
        Failed.Add('micmap.exe --unpatch-bindings (rc=' + IntToStr(ResultCode) + ')');

      // Step 2: --unregister-vrmanifest
      if (not Exec(MicMapExe, '--unregister-vrmanifest', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
        Failed.Add('micmap.exe --unregister-vrmanifest (rc=' + IntToStr(ResultCode) + ')');
    end;

    // Step 3 (reverse of Plan 07 Steps 1/2 combined -- install does remove-then-add,
    // uninstall only removes). Pitfall 10 gate via VrpathregExists (Plan 07 helper).
    if VrpathregExists() then
    begin
      // IN-10: apply the same defense-in-depth quote escaping as RunVrpathregRemove.
      if (not Exec(GetVrpathreg(''), 'removedriver "' + QuoteExecArg(AppDir) + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
        Failed.Add('vrpathreg removedriver (rc=' + IntToStr(ResultCode) + ')');
    end;

    // Step 4: Defense-in-depth legacy-bindings sweep. D-07 voids INST-06 as a
    // literal ghost-cleanup requirement (0.x was never shipped), but the sweep
    // is an idempotent no-op on every real machine and codifies the invariant
    // that %LOCALAPPDATA%\openvr\input\micmap_*.json is never MicMap's territory.
    SweepLegacyBindings();

    // Step 5: D-13 data-retention prompt.
    PromptAndMaybeRemoveUserData();

    if Failed.Count > 0 then
      Log('MicMap uninstall completed with non-fatal issues:' + Chr(13) + Chr(10) + Failed.Text);
  finally
    Failed.Free;
  end;
end;

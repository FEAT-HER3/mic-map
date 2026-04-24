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

procedure RunVrpathregRemove(AppDir: String);
var
  ResultCode: Integer;
begin
  // Pitfall 3: unconditional removedriver BEFORE adddriver prevents OpenVR #1653
  // duplicate-entry bug. rc is INTENTIONALLY ignored -- removedriver on an
  // unregistered path is a no-op (rc=0 or 1, either is fine).
  if VrpathregExists() then
    Exec(GetVrpathreg(''), 'removedriver "' + AppDir + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
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
  if (not Exec(GetVrpathreg(''), 'adddriver "' + AppDir + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
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

procedure CurStepChanged(CurStep: TSetupStep);
var
  Failed: TStringList;
  AppDir: String;
  MicMapExe: String;
  StepResult: String;
  CRLF: String;
begin
  if CurStep <> ssPostInstall then
    Exit;

  CRLF := Chr(13) + Chr(10);
  AppDir := ExpandConstant('{app}');
  MicMapExe := AppDir + '\bin\micmap.exe';
  Failed := TStringList.Create;
  try
    // Step 1: removedriver (unconditional, rc ignored -- Pitfall 3).
    RunVrpathregRemove(AppDir);

    // Step 2: adddriver (rc captured).
    StepResult := RunVrpathregAdd(AppDir);
    if StepResult <> '' then
      Failed.Add(StepResult);

    // Step 3: --register-vrmanifest (rc captured -- INST-04).
    StepResult := RunRegisterVrmanifest(MicMapExe);
    if StepResult <> '' then
      Failed.Add(StepResult);

    // Step 4: --patch-bindings (rc captured -- INST-08).
    StepResult := RunPatchBindings(MicMapExe);
    if StepResult <> '' then
      Failed.Add(StepResult);

    // Technique B continue-with-warning: aggregated MsgBox, installer does NOT abort.
    if Failed.Count > 0 then
      MsgBox('MicMap installed, but some post-install steps failed:' + CRLF + CRLF +
             Failed.Text +
             CRLF + 'MicMap will still work for basic dashboard toggling. ' +
             'If problems persist, see %APPDATA%\MicMap\micmap.log and re-run the installer.',
             mbInformation, MB_OK);
  finally
    Failed.Free;
  end;
end;

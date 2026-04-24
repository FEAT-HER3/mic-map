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

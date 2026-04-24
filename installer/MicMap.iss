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
// Plan 04 stub -- replaced by Plan 05 with HKCU\Software\Valve\Steam
// registry resolution (D-02) and Plan 06 (WMI SteamVR-running gate).
// ---------------------------------------------------------------
function GetMicMapInstallDir(Param: String): String;
begin
  // Placeholder -- Plan 05 overwrites with real implementation.
  Result := ExpandConstant('{autopf}\MicMap');
end;

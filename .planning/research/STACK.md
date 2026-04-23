# Stack Research — Seamless SteamVR Integration Milestone

**Domain:** Windows SteamVR sidecar driver + companion app (auto-start, installer, HMD input injection)
**Researched:** 2026-04-22
**Confidence:** HIGH (all major claims verified against current upstream headers / official release pages as of 2026-04)

## Scope Note

The base stack is locked (C++17, CMake 3.20+, ImGui v1.90.1 + D3D11, WASAPI, KissFFT 131.1.0, cpp-httplib v0.14.3, OpenVR SDK, nlohmann/json). This document researches **only the new surface area** required by the milestone:

1. OpenVR driver-side APIs for the sidecar-on-HMD technique (SVR-01/02/03)
2. OpenVR client-side IVRApplications API + `app.vrmanifest` for auto-start (AUTO-01)
3. Inno Setup 6.x idioms for SteamVR driver installers (INST-01/02)
4. nlohmann/json read-path patterns for an existing write-only config (CFG-01)

## Recommended Stack

### Core Technologies (New / Upgraded for this milestone)

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| OpenVR SDK | **2.15.6** (tag `v2.15.6`, released 2026-03-27) | Driver `IVRDriverInput_004`, `IVRProperties_001` on the driver side; `IVRApplications_008` on the client side | Current stable upstream as of research date. bey-closer-t1 was validated on v2.5.1; bumping to 2.15.6 for this milestone brings in `RegisterSubprocess` (useful if MicMap ever needs subprocess identification) and keeps us on a supported line. The interfaces MicMap needs (`CreateBooleanComponent`, `UpdateBooleanComponent`, `TrackedDeviceToPropertyContainer`) have identical signatures on v2.5.1 and v2.15.6 — the sidecar-on-HMD technique ports unchanged. **HIGH confidence** (verified in `headers/openvr.h` line 2876 and `headers/openvr_driver.h` lines 3869/3879 on master @ 2026-04) |
| Inno Setup | **6.7.1** (released 2026-02-17) | Single-click Windows installer for driver DLL + app.exe + vrmanifest | Current stable from jrsoftware.org. bey-closer-t1's `BeyondProximity.iss` uses Inno Setup 6 idioms (`PrivilegesRequired=admin`, `{autopf}`, `CreateOleObject` WMI query, `postinstall shellexec` for post-install launch). 7.x is preview-only and not for production. **HIGH confidence** |
| nlohmann/json | **3.12.0** (released 2025-04-11) | JSON parse/serialize for `%APPDATA%/MicMap/config.json` read path | Project currently pins 3.11.2 (already vendored per existing STACK.md). 3.12.0 is source-compatible; only bump if another reason compels it. **Recommendation: stay on 3.11.2 for this milestone** — CFG-01 is about fixing the stubbed read path, not library churn. The read-path patterns (`json::parse`, `operator>>`, `.value()` with defaults, `.contains()`) are identical across 3.11.x and 3.12.x. **HIGH confidence** |

### Driver-Side OpenVR Interfaces Used (Exact Signatures)

All signatures below verified against `openvr_driver.h` on master (2026-04). Interface version constants are the current line MicMap should link against.

#### `IVRDriverInput_004` — obtained via `vr::VRDriverInput()`

```cpp
// openvr_driver.h:3846
virtual EVRInputError CreateBooleanComponent(
    PropertyContainerHandle_t ulContainer,
    const char *pchName,
    VRInputComponentHandle_t *pHandle) = 0;

// openvr_driver.h:3849
virtual EVRInputError UpdateBooleanComponent(
    VRInputComponentHandle_t ulComponent,
    bool bNewValue,
    double fTimeOffset) = 0;

// Version constant — openvr_driver.h:3879
static const char * const IVRDriverInput_Version = "IVRDriverInput_004";
```

#### `IVRProperties_001` — obtained via `vr::VRProperties()`

```cpp
// openvr_driver.h:3366
virtual PropertyContainerHandle_t TrackedDeviceToPropertyContainer(
    TrackedDeviceIndex_t nDevice) = 0;

// Constant — openvr_driver.h:386
static const PropertyContainerHandle_t k_ulInvalidPropertyContainer = 0;

// Version constant — openvr_driver.h:3369
static const char * const IVRProperties_Version = "IVRProperties_001";
```

#### Call sequence (the sidecar-on-HMD idiom)

```cpp
// In DeviceProvider header:
vr::VRInputComponentHandle_t m_hSystemClick = vr::k_ulInvalidInputComponentHandle;
bool m_bSystemClickAttempted = false;

// In DeviceProvider::RunFrame() — called by SteamVR at ~90 Hz:
if (!m_bSystemClickAttempted) {
    vr::PropertyContainerHandle_t hmd =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);

    if (hmd != vr::k_ulInvalidPropertyContainer) {
        m_bSystemClickAttempted = true;  // one-shot; never retry regardless of outcome
        vr::EVRInputError err = vr::VRDriverInput()->CreateBooleanComponent(
            hmd, "/input/system/click", &m_hSystemClick);
        if (err != vr::VRInputError_None) {
            m_hSystemClick = vr::k_ulInvalidInputComponentHandle;
            // Log warning and fall through — triggers become no-ops until restart
        }
    }
}

// When HTTP bridge receives trigger:
if (m_hSystemClick != vr::k_ulInvalidInputComponentHandle) {
    vr::VRDriverInput()->UpdateBooleanComponent(m_hSystemClick, true,  0.0);
    // ...later (single shot or release after cooldown)...
    vr::VRDriverInput()->UpdateBooleanComponent(m_hSystemClick, false, 0.0);
}
```

**Key constraints (re-verified from `bey-closer-t1/HMD Button Stub.md`):**
- `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` returns `k_ulInvalidPropertyContainer` until the HMD's owning driver activates it. MUST defer to `RunFrame`.
- `CreateBooleanComponent` on the HMD container returns `VRInputError_InvalidParam` (4) if called before HMD activation.
- Once created, the handle is owned by MicMap's driver and can be updated freely.
- Duplicate path names (`/input/system/click` coexisting with the lighthouse driver's handle of the same path) are allowed — SteamVR propagates updates from *any* driver's component on that path.
- Cross-driver update attempts (updating a handle not owned by your driver) return `VRInputError_WrongType` (2) — do **not** try to reuse the lighthouse handle.

### Client-Side OpenVR Interface Used (app auto-start)

#### `IVRApplications_008` — obtained via `vr::VRApplications()` after `VR_Init()`

Signatures verified against `openvr.h` lines 2744–2876 on master (2026-04).

```cpp
// openvr.h:2752
virtual EVRApplicationError AddApplicationManifest(
    const char *pchApplicationManifestFullPath,
    bool bTemporary = false) = 0;

// openvr.h:2755
virtual EVRApplicationError RemoveApplicationManifest(
    const char *pchApplicationManifestFullPath) = 0;

// openvr.h:2795 — identifies the calling process as a given app_key; required for IPC/launch-linkage
virtual EVRApplicationError IdentifyApplication(
    uint32_t unProcessId,
    const char *pchAppKey) = 0;

// openvr.h:2815 — flips the auto-launch bit for the app_key
virtual EVRApplicationError SetApplicationAutoLaunch(
    const char *pchAppKey,
    bool bAutoLaunch) = 0;

// openvr.h:2818
virtual bool GetApplicationAutoLaunch(const char *pchAppKey) = 0;

// Version constant — openvr.h:2876
static const char * const IVRApplications_Version = "IVRApplications_008";
```

**Critical doc-comment caveat:** The header comment on `SetApplicationAutoLaunch` says "only valid for applications which return true for VRApplicationProperty_IsDashboardOverlay_Bool." In practice (OpenVR issues #1378, #1424, #1547, and every OSS overlay that auto-starts — OpenVR Advanced Settings, OVR Toolkit, XSOverlay, VR Photo Buddy), the way to make a non-Steam app auto-start is: **ship a vrmanifest with `"is_dashboard_overlay": true`, register it with `AddApplicationManifest(path, false /* non-temporary */)`, then call `SetApplicationAutoLaunch(app_key, true)`**. MicMap is a true background/overlay app (it has no VR scene rendering of its own — it is a mic-listener that drives input), so `is_dashboard_overlay: true` is the correct semantic anyway. This is the same pattern OpenVR Advanced Settings uses.

### `app.vrmanifest` Schema (Recommended for MicMap)

Verified against OpenVR-InputEmulator's shipped manifest, OpenVR issue #1547 (VR Photo Buddy), and the Unity-side manifest-generator gist — all current and structurally identical.

```json
{
  "source": "builtin",
  "applications": [
    {
      "app_key": "bigscreen.micmap",
      "launch_type": "binary",
      "binary_path_windows": "micmap.exe",
      "is_dashboard_overlay": true,
      "arguments": "",
      "image_path": "resources\\micmap_icon.png",
      "strings": {
        "en_us": {
          "name": "MicMap",
          "description": "Covers-the-mic gesture → SteamVR system button. Runs in the background."
        }
      }
    }
  ]
}
```

**Field reference** (all optional unless marked required):

| Field | Required | Notes |
|-------|----------|-------|
| `source` | required | Always `"builtin"` for apps shipped via a vrmanifest (distinguishes from Steam/Mime sources). |
| `applications[]` | required | Array; MicMap has exactly one entry. |
| `app_key` | required | Reverse-DNS-style identifier. **Must be unique and stable across versions.** Changing this orphans the user's auto-launch preference. Recommended: `"bigscreen.micmap"` (matches existing C++ namespace and avoids the bey-closer-t1 conflict). |
| `launch_type` | required | `"binary"` for a native executable (MicMap's case). Other values: `"url"`, `"dashboard_overlay"` — not applicable. |
| `binary_path_windows` | required on Windows | **Relative to the vrmanifest file's directory.** If `app.vrmanifest` lives next to `micmap.exe` in `{app}\` the value is just `"micmap.exe"`. |
| `binary_path_linux` / `binary_path_osx` | — | Omit; MicMap is Windows-only. |
| `is_dashboard_overlay` | recommended true | Required for `SetApplicationAutoLaunch` to succeed per current SteamVR behavior. Semantically correct for MicMap (background app, not a scene app). |
| `arguments` | optional | Empty string is fine. |
| `image_path` | optional but recommended | Icon shown in SteamVR's Startup/Shutdown list. Relative to manifest dir. Ship a 256×256 PNG. |
| `action_manifest_path` | **do NOT include** | MicMap does not use client-side OpenVR Input actions — it injects via its own driver. Leaving this out avoids an empty-actions warning. |
| `strings.<locale>.name` | required | Displayed in SteamVR UI. |
| `strings.<locale>.description` | recommended | Shown in SteamVR's app list. |
| `mime_type` | — | Omit. |

**App key gotcha (OpenVR issue #1378):** If MicMap is ever registered without a manifest (e.g. via `VR_Init` before `AddApplicationManifest`), SteamVR synthesizes `"system.generated.micmap.exe"` and `SetApplicationAutoLaunch("bigscreen.micmap", ...)` returns `VRApplicationError_UnknownApplication`. Order matters: always `AddApplicationManifest` *before* any `IdentifyApplication`/`SetApplicationAutoLaunch` call on that key.

### Inno Setup Script (INST-01/02) — Specific Directives

Based on bey-closer-t1's `BeyondProximity.iss` (battle-tested for SteamVR driver installs) with MicMap-specific adjustments. Inno Setup 6.7.1 syntax.

#### Required `[Setup]` directives

```ini
[Setup]
AppName=MicMap
AppVersion={#MyAppVersion}
AppPublisher=Bigscreen
; MicMap owns its own driver dir — NOT nested under another vendor.
; Recommended: install the whole package into a MicMap-owned dir under Steam common.
DefaultDirName={autopf}\Steam\steamapps\common\MicMap
PrivilegesRequired=admin              ; required for vrpathreg + writing into Steam dir
DisableDirPage=auto                   ; user shouldn't retarget this
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename=MicMap-Setup-{#MyAppVersion}
SetupIconFile=resources\micmap_icon.ico
Uninstallable=yes                     ; MicMap should be cleanly uninstallable (unlike bey-closer-t1)
UninstallDisplayName=MicMap
```

**Difference from bey-closer-t1:** `Uninstallable=no` in `BeyondProximity.iss` reflects that project's nesting-inside-another-vendor constraint. MicMap should be `Uninstallable=yes` and implement clean teardown (remove driver, remove vrmanifest, delete files). See uninstall handling below.

#### `[Files]` — what to ship

```ini
[Files]
; Companion app
Source: "{#AppBuildDir}\micmap.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppBuildDir}\app.vrmanifest"; DestDir: "{app}"; Flags: ignoreversion
Source: "resources\micmap_icon.png"; DestDir: "{app}\resources"; Flags: ignoreversion

; Driver (MicMap-owned driver dir, sibling layout to app)
Source: "{#DriverBuildDir}\bin\win64\driver_micmap.dll"; \
  DestDir: "{app}\driver\micmap\bin\win64"; Flags: ignoreversion
Source: "{#DriverBuildDir}\driver.vrdrivermanifest"; \
  DestDir: "{app}\driver\micmap"; Flags: ignoreversion
```

Note: `driver.vrdrivermanifest`'s `"name"` field MUST match the parent directory name (`"micmap"`) per OpenVR driver rules. Confirmed in Context7 OpenVR tutorial and bey-closer-t1 experience.

#### `[Run]` — post-install actions

```ini
[Run]
; 1. Register the SteamVR driver (admin-elevated, silent).
Filename: "{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"; \
  Parameters: "adddriver ""{app}\driver\micmap"""; \
  Flags: runhidden waituntilterminated; \
  StatusMsg: "Registering MicMap driver with SteamVR..."; \
  Check: FileExists(ExpandConstant('{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'))

; 2. Register the app vrmanifest + set auto-launch. Best done by a tiny helper mode of
;    micmap.exe itself (e.g. `micmap.exe --register-vrmanifest`) rather than an
;    external tool — the app already links OpenVR and can call AddApplicationManifest
;    + SetApplicationAutoLaunch in one elevated shot. This also gives the
;    uninstaller the symmetric --unregister-vrmanifest path.
Filename: "{app}\micmap.exe"; \
  Parameters: "--register-vrmanifest"; \
  Flags: runhidden waituntilterminated; \
  StatusMsg: "Registering MicMap auto-start with SteamVR..."

; 3. Offer to launch SteamVR (post-install checkbox, unchecked by default).
Filename: "steam://run/250820"; \
  Description: "Launch SteamVR"; \
  Flags: postinstall shellexec nowait unchecked skipifsilent
```

#### `[UninstallRun]` — symmetric teardown

```ini
[UninstallRun]
; Unregister vrmanifest + clear auto-launch before files are deleted.
Filename: "{app}\micmap.exe"; \
  Parameters: "--unregister-vrmanifest"; \
  Flags: runhidden waituntilterminated; \
  RunOnceId: "UnregisterVRManifest"

; Remove driver registration.
Filename: "{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"; \
  Parameters: "removedriver ""{app}\driver\micmap"""; \
  Flags: runhidden waituntilterminated; \
  RunOnceId: "RemoveSteamVRDriver"
```

#### `[Code]` — Pascal Script snippets

Reuse **verbatim** from bey-closer-t1's `BeyondProximity.iss`:

1. **`IsProcessRunning`** — WMI OLE query against `Win32_Process`. Graceful fallback if WMI is unavailable. (Lines 78–93 of `BeyondProximity.iss`.)

2. **`PrepareToInstall`** — Blocks install if `vrserver.exe` is running. Returns a user-visible message. SteamVR locks driver DLLs in memory; overwriting mid-run causes silent failures. (Lines 100–105.)

3. **Drop `NextButtonClick` dir validation** — bey-closer-t1 validated the user hadn't misaimed at a non-Beyond driver folder. MicMap installs to its own directory, so this check is unnecessary (and `DisableDirPage=auto` prevents the user from retargeting anyway).

4. **Drop `RestoreRootManifest` / `RestoreVrresources` / `CurStepChanged`** — those fix bey-closer-t1's nesting-under-bigscreenbeyond artifact. MicMap is standalone; remove entirely.

Minimum MicMap `[Code]` block:

```pascal
[Code]

function IsProcessRunning(const ProcessName: String): Boolean;
var
  WbemLocator, WbemServices, WbemObjectSet: Variant;
begin
  Result := False;
  try
    WbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    WbemServices := WbemLocator.ConnectServer('', 'root\cimv2');
    WbemObjectSet := WbemServices.ExecQuery(
      'SELECT Name FROM Win32_Process WHERE Name="' + ProcessName + '"');
    Result := (WbemObjectSet.Count > 0);
  except
    Log('WMI query failed; allowing install to proceed');
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsProcessRunning('vrserver.exe') then
    Result := 'SteamVR is currently running. Please close SteamVR before installing MicMap.';
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if IsProcessRunning('vrserver.exe') then begin
    MsgBox('SteamVR is currently running. Please close SteamVR before uninstalling MicMap.', mbError, MB_OK);
    Result := False;
  end;
  if IsProcessRunning('micmap.exe') then begin
    // Best-effort; MicMap should also handle a graceful shutdown on SIGTERM.
    Exec(ExpandConstant('{app}\micmap.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
  end;
end;
```

### Supporting Libraries (Already Vendored — No Change Needed)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| nlohmann/json | 3.11.2 (pinned) | Read `%APPDATA%/MicMap/config.json` | See "Config Read-Path Patterns" below |
| cpp-httplib | 0.14.3 (pinned) | Existing HTTP bridge app ↔ driver | No change — milestone doesn't touch IPC |
| ImGui | 1.90.1 (pinned) | Existing desktop UI | No change |
| KissFFT | 131.1.0 (pinned) | Existing FFT | No change |

## Config Read-Path Patterns (nlohmann/json 3.11.2)

The existing config_manager writes JSON but the read path is stubbed at `src/core/src/config_manager.cpp:142`. The standard 3.11.x idiom for reading a file that may be missing, malformed, or missing fields (all of which happen in the wild) is:

```cpp
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

bool ConfigManager::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        // File doesn't exist — use defaults, not an error.
        return false;
    }

    json j;
    try {
        in >> j;  // or: j = json::parse(in);
    } catch (const json::parse_error& e) {
        logger_->warn("config.json parse error at byte {}: {}", e.byte, e.what());
        return false;  // leave defaults in place
    }

    // Schema-tolerant reads: .value(key, default) returns default if key missing
    // OR if value is wrong type (with allow_exceptions=false — see below).
    config_.audio.device_id     = j.value("audio_device_id", std::string{});
    config_.audio.sample_rate   = j.value("audio_sample_rate", 48000);
    config_.detection.duration_ms = j.value("detection_duration_ms", 250);
    config_.detection.sensitivity = j.value("detection_sensitivity", 0.7f);

    // Nested objects: check existence first for clean fallback.
    if (j.contains("steamvr") && j["steamvr"].is_object()) {
        const auto& sv = j["steamvr"];
        config_.steamvr.auto_start = sv.value("auto_start", true);
    }

    // Version handshake — reject/upgrade old schemas explicitly.
    int schema_version = j.value("schema_version", 1);
    if (schema_version > kCurrentSchemaVersion) {
        logger_->warn("config.json schema newer than app; ignoring");
        return false;
    }

    return true;
}
```

**Key patterns** (all stable from 3.11.x through 3.12.x):
- `j.value(key, default)` — the idiom for "read-or-default" on missing keys. Throws on type mismatch; use `contains()` + explicit type check when types matter.
- `j.contains(key)` — does not throw.
- `json::parse(input, callback, allow_exceptions=false)` — alternative no-exceptions mode if the codebase policy wants error-code style.
- `std::ifstream >> j` — simplest; throws `parse_error` on failure.
- For the write side (already implemented): `std::ofstream out(path); out << std::setw(2) << j;` for pretty-printed output. Keep as-is.

**Do NOT** use `j["key"].get<T>()` directly on optional fields — it throws on missing keys. Use `.value(k, d)` instead.

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| OpenVR SDK 2.15.6 | SteamVR March 2026+ | SteamVR runtime is always newer than the SDK; 2.15.6 headers are forward-compatible with any current SteamVR. |
| OpenVR SDK 2.15.6 | bey-closer-t1's validated technique (v2.5.1) | `IVRDriverInput_004`, `IVRProperties_001`, `IVRApplications_008` are the same interface versions in both — the sidecar-on-HMD technique ports unchanged. |
| Inno Setup 6.7.1 | Windows 10 1809+ / Windows 11 | Installer runs on both. Generated setups target Windows 7+ by default (relevant only if anyone is still on 7 — MicMap is 10+ per existing constraints). |
| nlohmann/json 3.11.2 | C++17 | Header-only. No linkage concerns. Upgrade to 3.12.0 is source-compatible but not required. |
| vrpathreg.exe | Any SteamVR version that ships it | SteamVR has shipped `bin\win64\vrpathreg.exe` since ~2017; path is stable: `{autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe`. |

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| SteamVR-native auto-start (`app.vrmanifest` + `SetApplicationAutoLaunch`) | Windows `Run` registry key or Startup folder | Never, for this product. User explicitly wants SteamVR-lifecycle-tied startup; Windows-session startup would run MicMap even when not in VR, defeating the integration goal. |
| SteamVR-native auto-start | SteamVR "Startup/Shutdown → Startup Overlay Apps" manual toggle | Only as a UX fallback diagnostic. The installer should set the bit programmatically so the user doesn't have to hunt for it. |
| Inno Setup 6.7.1 | NSIS | NSIS is what OpenVR Advanced Settings uses and it works, but bey-closer-t1's Pascal Script idioms (WMI process check, `FileExists` guards, post-install launch) already solve MicMap's problem domain in Inno. No benefit to switching scripting dialects. |
| Inno Setup 6.7.1 | WiX Toolset (MSI) | MSI gives enterprise-grade features (transactional install, Group Policy deploy) MicMap doesn't need. Inno produces a single .exe that matches the "tester-distribution" UX of bey-closer-t1. |
| `is_dashboard_overlay: true` in vrmanifest | `launch_type: "binary"` with no overlay flag | Without `is_dashboard_overlay: true`, SteamVR treats MicMap as a scene app — `SetApplicationAutoLaunch` returns `VRApplicationError_InvalidApplication` per the header comment and OSS experience (OpenVR #1378, #1547). MicMap is not a scene app anyway. |
| Register vrmanifest from `micmap.exe --register-vrmanifest` | Register from a separate helper `.exe` | App already links OpenVR; adding a CLI mode is cheaper than shipping a second binary just for install-time. Also gives symmetric `--unregister` for the uninstaller. |
| nlohmann/json 3.11.2 (keep pinned) | Bump to 3.12.0 | Only if CFG-01 surfaces a specific 3.12.x-only feature (it shouldn't — `value()`, `contains()`, `parse_error` are all 3.11.x). |
| OpenVR SDK 2.15.6 (upgrade from whatever is vendored) | Stay on 2.5.1 per bey-closer-t1's validation | Only if the upgrade surfaces breakage. All three interface versions MicMap uses (`IVRDriverInput_004`, `IVRProperties_001`, `IVRApplications_008`) are identical. The milestone should bump to 2.15.6 to stay on a supported line. |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Virtual controller driver (`TrackedDeviceAdded` + `ITrackedDeviceServerDriver`) | Renders a laser beam on trigger, requires dashboard-state polling to choose open-vs-select, and the whole thing is the architecture being ripped out. | Sidecar-on-HMD `/input/system/click` via `CreateBooleanComponent` on `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` — see SVR-01/02/03. |
| `IVRDriverInput_003` (legacy) | Older interface version; no reason to pin to it. SteamVR exposes `_004` on all current runtimes. | `IVRDriverInput_Version = "IVRDriverInput_004"` (current). |
| Updating another driver's input handle (e.g. lighthouse's native `/input/system/click` handle) | Returns `VRInputError_WrongType` (2). Verified in bey-closer-t1 writeup. Cross-driver updates are permanently blocked. | Create your own component on the same path. SteamVR merges updates. |
| Calling `CreateBooleanComponent` on the HMD container inside `DeviceProvider::Init()` | HMD container returns `k_ulInvalidPropertyContainer` before the HMD's driver activates. `Create` returns `VRInputError_InvalidParam` (4). | Defer to `RunFrame()` with a one-shot `bool` guard. |
| Batch-script installer (`scripts/install_driver.bat`) | No admin-elevation prompt, no process-safety check, no auto-start registration, no uninstall, no UI. | Inno Setup 6.7.1 with the `[Code]` block from bey-closer-t1. |
| `Uninstallable=no` (copied from bey-closer-t1) | bey-closer-t1 chose this because it overlays another vendor's driver dir. MicMap owns its dir → should be cleanly uninstallable. | `Uninstallable=yes` + `[UninstallRun]` block calling `vrpathreg removedriver` and `micmap.exe --unregister-vrmanifest`. |
| Registering vrmanifest as `bTemporary=true` (second arg of `AddApplicationManifest`) | Temporary manifests are not persisted and can't be auto-launched. The Unity-side gist (krzys-h) documents this explicitly. | `AddApplicationManifest(path, /*bTemporary=*/false)`. |
| Action manifest / OpenVR Input action bindings on the app side | MicMap does not consume VR input from bindings — it injects via the driver. An empty or misconfigured action_manifest_path generates warnings. | Omit `action_manifest_path` from `app.vrmanifest` entirely. |
| `{app}\driver\micmap\bin\win64\driver_micmap.dll` path where `driver.vrdrivermanifest` `"name"` ≠ `"micmap"` | OpenVR requires the driver folder name to match `name` in `vrdrivermanifest`. Mismatch = driver silently not loaded. | Keep them aligned: folder `micmap`, manifest `"name": "micmap"`. |
| Calling `SetApplicationAutoLaunch` before `AddApplicationManifest` (or before `IdentifyApplication` if the app is already running) | Returns `VRApplicationError_UnknownApplication` (OpenVR issue #1378). Key hasn't propagated. | Strict order: `AddApplicationManifest` → `IdentifyApplication(GetCurrentProcessId(), app_key)` (if in-process) → `SetApplicationAutoLaunch`. |

## Stack Patterns by Variant

**If MicMap needs to verify auto-launch was set correctly:**
- After `SetApplicationAutoLaunch`, call `GetApplicationAutoLaunch(app_key)` and log result. Known SteamVR bug (OpenVR #1547) occasionally "forgets" the setting across SteamVR restarts. A user-visible "Auto-start with SteamVR: [Enabled/Disabled] [Re-register]" UI toggle that re-runs registration is a reasonable mitigation.

**If the installer needs to support in-place updates:**
- Inno's `AppId=` (a GUID) makes version upgrades replace the previous install cleanly. Add one.
- `[InstallDelete]` section to clear stale binaries before copying new ones — bey-closer-t1 uses this to clear the old flat-deploy artifacts.

**If SteamVR is installed to a non-default Steam library (not `{autopf}\Steam`):**
- bey-closer-t1's `Check: FileExists(...)` guard gracefully skips the `vrpathreg` step if the default path doesn't exist. This is insufficient for a non-default Steam library — the installer would silently skip driver registration.
- Better: query SteamVR's install path from the registry (`HKCU\Software\Valve\Steam\SteamPath`) via Pascal Script `RegQueryStringValue` and fall through to the default path if the query fails.
- **Out of scope this milestone per existing constraints** — but flag it as PITFALLS-grade for later.

## Installation (build-time)

The milestone requires no new vendored dependencies. CMake changes:

```cmake
# Bump OpenVR SDK reference if using FetchContent or git submodule:
# From v2.5.1 to v2.15.6 — same interface versions, no code changes needed.

# nlohmann/json — if vendored via FetchContent:
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
# No bump required for this milestone.
```

Inno Setup is a developer-machine tool, not a build-time dependency of the project. Install via:
- https://jrsoftware.org/isdl.php → `innosetup-6.7.1.exe` (or current)
- Invoke `ISCC.exe /DMyAppVersion=v1.0.0 /DAppBuildDir=build\Release /DDriverBuildDir=build\driver installer\MicMap.iss` (pattern from bey-closer-t1).

## Sources

**Primary (HIGH confidence — authoritative, verified 2026-04):**
- OpenVR SDK `headers/openvr.h` @ `master` — `IVRApplications_008` interface: class declaration at line 2744, `AddApplicationManifest` at 2752, `SetApplicationAutoLaunch` at 2815, `IdentifyApplication` at 2795, `RegisterSubprocess` at 2868, version constant at 2876. Downloaded from `https://raw.githubusercontent.com/ValveSoftware/openvr/master/headers/openvr.h` (6166 lines, 2026-04-22).
- OpenVR SDK `headers/openvr_driver.h` @ `master` — `IVRDriverInput_004`: class at line 3841, `CreateBooleanComponent` at 3846, `UpdateBooleanComponent` at 3849, version constant at 3879. `IVRProperties_001`: class at 3351, `TrackedDeviceToPropertyContainer` at 3366, version at 3369. `k_ulInvalidPropertyContainer` at 386, `k_ulInvalidInputComponentHandle` at 753.
- OpenVR SDK 2.15.6 release notes — `https://github.com/ValveSoftware/openvr/releases` — released 2026-03-27; notes `IVRApplications` adds `RegisterSubprocess`.
- Inno Setup 6.7.1 download page — `https://jrsoftware.org/isdl.php` — released 2026-02-17.
- nlohmann/json 3.12.0 release notes — `https://github.com/nlohmann/json/releases` — released 2025-04-11.
- `bey-closer-t1/HMD Button Stub.md` — sidecar-on-HMD technique, error codes, timing constraints. Validated on SteamVR March 2026.
- `bey-closer-t1/installer/BeyondProximity.iss` — admin-elevated Inno Setup + WMI process check + vrpathreg integration. 199 lines, patterns adopted here.
- Context7 `/valvesoftware/openvr` — driver tutorial (Activate, RunFrame, CreateBooleanComponent signature) — confirms the header signatures.

**Secondary (MEDIUM confidence — OSS examples corroborate upstream headers):**
- OpenVR issue #1378 — `system.generated.*` app_key trap when manifest is registered after the process.
- OpenVR issue #1547 — `app.vrmanifest` concrete example (VR Photo Buddy); known SteamVR bug that "forgets" auto-launch across restarts.
- OpenVR-InputEmulator `client_overlay/bin/win64/manifest.vrmanifest` — confirms minimal manifest structure matches Valve docs.
- Unity manifest-generator gist (krzys-h) — confirms `AddApplicationManifest(path, bTemporary)` semantics and per-field serialization.

**Contextual (reviewed, not load-bearing):**
- OpenVR Advanced Settings README — uses NSIS, similar auto-launch pattern; confirms the general flow.
- Steam Community SteamVR Developers thread on non-Steam app auto-start — confirms "overlay + manifest + SetApplicationAutoLaunch" is the only supported path.

---

*Stack research for: MicMap Seamless SteamVR Integration milestone*
*Researched: 2026-04-22*

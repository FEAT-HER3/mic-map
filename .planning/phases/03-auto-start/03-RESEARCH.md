# Phase 3: Auto-Start — Research

**Researched:** 2026-04-23
**Domain:** SteamVR-native auto-launch (`app.vrmanifest`), silent-boot UX, VREvent_Quit lifecycle, idempotent registration
**Confidence:** HIGH

## Summary

Phase 3 is the packaging layer that makes MicMap's driver work without a user ever clicking `micmap.exe`. Three sub-domains converge: (1) OpenVR's application-manifest registration API (`IVRApplications::AddApplicationManifest` / `SetApplicationAutoLaunch`), (2) Windows silent-app UX primitives (`/SUBSYSTEM:WINDOWS`, `CommandLineToArgvW`, `Shell_NotifyIcon` balloon, single-instance mutex), and (3) SteamVR's quit watchdog (`VREvent_Quit` → `AcknowledgeQuit_Exiting`). All three surfaces are stable, well-documented, and already partially exercised by the existing codebase. The heavy decisions were made in `/gsd-discuss-phase 3` and are locked in CONTEXT.md (D-01…D-21); this research confirms each decision against authoritative sources, surfaces one concrete SDK-version discrepancy the planner must address, and catalogs the hand-rolling pitfalls the plan must explicitly call out.

**Primary recommendation:** Implement `IManifestRegistrar` with three methods (`registerApp()`, `unregisterApp()`, `ensureRegistered()`), drive it from a `vr::VRApplications()` accessor (NOT a hardcoded `"IVRApplications_008"` string — see Assumption A1), compute `app.vrmanifest`'s absolute path at runtime via `GetModuleFileNameW` + `PathCchRemoveFileSpec`, and hold a detached `std::thread` for the fire-and-forget re-registration retry loop (NOT a raw `std::async` future left to go out of scope — its destructor blocks, turning D-16's 30s retry into synchronous blocking). Gate the tray balloon on a new `AppConfig::shownTrayNotification` bool and fire it exactly once per install lifetime. Inside `vr_input.cpp::pollEvents()`, call `vr::VRSystem()->AcknowledgeQuit_Exiting()` synchronously the instant `VREvent_Quit` is observed, BEFORE notifying the app-side callback — this decouples the watchdog from teardown latency.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**CLI surface & headless runtime (AUTO-02, AUTO-03, AUTO-06):**
- **D-01:** CLI argument parsing uses `CommandLineToArgvW` once at `WinMain` entry, followed by a `wcscmp` loop to set a flags struct `{bool register_manifest, bool unregister_manifest, bool minimized}`. No new dependency.
- **D-02:** When `--register-vrmanifest` or `--unregister-vrmanifest` is present, `WinMain` early-exits before `RegisterClassExW`: `VR_Init(VRApplication_Utility)` → `manifest_registrar::register()` or `::unregister()` → `VR_Shutdown()` → `return exit_code`. No D3D, no ImGui, no audio, no detector, no `driverClient`, no tray.
- **D-03:** Exit codes 0 (success) / 1 (failure).
- **D-04:** Headless CLI modes log via `common::Logger` (`%APPDATA%\MicMap\micmap.log`). No `AllocConsole` / `AttachConsole`.

**Silent auto-launch UX (AUTO-06):**
- **D-05:** SteamVR auto-launches pass `--minimized` via `app.vrmanifest`'s `arguments` field (`"arguments": ["--minimized"]`). User-clicked shortcuts do not. Sole signal for silent boot.
- **D-06:** Silent-mode window policy: `CreateWindowW` runs as today but `ShowWindow` never called; `minimizedToTray = true` from boot.
- **D-07:** `/SUBSYSTEM:WINDOWS` guardrail (already in `apps/micmap/CMakeLists.txt` via `add_executable(micmap WIN32 ...)`) + phase-exit grep gate: `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` must return zero results. No runtime `FreeConsole()`.
- **D-08:** Single-instance mutex: when second instance has `--minimized` in argv, skip `SetForegroundWindow` / `PostMessage(IDM_SHOW)` and exit silently.
- **D-09:** First-silent-launch tray balloon: first time the app is launched with `--minimized` after install, fire `Shell_NotifyIcon(NIM_MODIFY, ...)` with `NIF_INFO` (title "MicMap", body something like "Running in the system tray. Click the icon to open."). New `AppConfig` bool `shownTrayNotification` (default false), flipped true after first balloon. Round-trips through Phase 2 defensive reader with `.value(key, false)`.
- **D-10:** Notification API: `Shell_NotifyIcon NIM_MODIFY + NIF_INFO` on the existing tray icon. No Win10+ Toast.

**Shutdown lifecycle (AUTO-05):**
- **D-11:** `vr::VRSystem()->AcknowledgeQuit_Exiting()` called synchronously inside `vr_input.cpp`'s `pollEvents()`, immediately upon seeing `VREvent_Quit`, BEFORE dispatching the app callback.
- **D-12:** Teardown runs in `MicMapApp::shutdown()` in reverse init order: audio capture → detector reset → `driverClient->disconnect()` → `vrInput->shutdown()` → tray `Shell_NotifyIcon NIM_DELETE` → ImGui/D3D/window.
- **D-13:** No watchdog thread, no `TerminateProcess` fallback.
- **D-14:** All exit paths (VR Quit, Alt-F4, tray Exit, fatal error) converge on `running = false` → `shutdown()`.

**Registration flow (AUTO-01, AUTO-02, AUTO-04):**
- **D-15:** GUI startup re-registers on every boot, fire-and-forget via `std::async(std::launch::async, ...)`. Task: `VR_Init(Utility)` → `IsApplicationInstalled(app_key)` → if false, `AddApplicationManifest` + poll + `SetApplicationAutoLaunch`; if true, no-op.
- **D-16:** When `VR_Init` fails with `VRInitError_Init_HmdNotFound` / `VRInitError_Init_NoServerForBackgroundApp` (SteamVR not running): silent 30s retry loop, stops after first success. Log INFO once on success; log WARNING with enum name if unexpected error. Detached thread; joins on app shutdown.
- **D-17:** `IsApplicationInstalled` poll: 100ms ticks, 2000ms ceiling (20 attempts). Log `"polling for manifest install"` on entry, `"manifest ready after Nms"` or `"timeout after 2000ms"` on exit. No per-tick log.
- **D-18:** Re-registration task logs INFO on state change only (not-installed → installed).

**File + module layout:**
- **D-19:** `app.vrmanifest` generated at build time via CMake `configure_file` from `apps/micmap/app.vrmanifest.in`. Substitutes `@MICMAP_VERSION@` and `@MICMAP_APP_KEY@` (locked to `bigscreen.micmap`). Emitted alongside `micmap.exe` in build output.
- **D-20:** New module: `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` + `src/steamvr/src/manifest_registrar.cpp`. Factory + interface (`createManifestRegistrar()`, `IManifestRegistrar`).
- **D-21:** Absolute path of `app.vrmanifest` for `AddApplicationManifest` computed at runtime via `GetModuleFileNameW` → strip filename via `PathCchRemoveFileSpec` → append `L"app.vrmanifest"`. UTF-16 native, long-path safe.

### Claude's Discretion

- Exact interface shape of `IManifestRegistrar` — e.g., `registerApp() / unregisterApp() / ensureRegistered()` vs one method with an enum.
- Argument-parsing struct location — anonymous-namespace helper in `main.cpp` vs a tiny `common::CliArgs` utility.
- `app.vrmanifest` optional fields: `strings.en_us.name` ("MicMap"), `strings.en_us.description`, `image_path` (icon). Icon source may reuse `apps/micmap/micmap.rc`'s icon or a new PNG sibling — note there is currently no `.ico` or `.png` bundled; `micmap.rc` uses runtime `IDI_APPLICATION`. Standard defaults acceptable.
- Exact wording of the first-silent-launch tray balloon body text.
- Log-line wording for registration states / errors beyond the enum-name requirement in D-16.
- Precise placement of the quiet-retry background thread (member of `MicMapApp` vs local to `manifest_registrar` free function).
- Phase 3 validation harness: manual UAT is acceptable. Optional extension of `hmd_button_test` or a new `--smoketest` mode is nice-to-have.
- New `AppConfig` field `shownTrayNotification` added during Phase 3 execution. Config-manager diff is a one-field read/write + `.value` default; no Phase 2 re-open needed.

### Deferred Ideas (OUT OF SCOPE)

- Win10+ `ToastNotificationManager` toast — rejected for Phase 3.
- Auto-start toggle checkbox in MicMap UI (UX-01) — v1.x.
- Granular CLI exit codes (0/2/3/4) — 0/1 locked.
- Event-driven SteamVR-running detection via `openvrpaths.vrpath` mtime — rejected; D-16's VR_Init poll is simpler.
- `--smoketest` mode on `micmap.exe` — optional extension.
- Windows Run-key / Startup folder fallback — rejected milestone-wide.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| AUTO-01 | Ship `app.vrmanifest` alongside `micmap.exe` with `app_key="bigscreen.micmap"`, `is_dashboard_overlay=true`, `launch_type="binary"`, `binary_path_windows="micmap.exe"` | Pattern 3 gives exact JSON shape; D-19 provides build-time generation; Assumption A2 flags the `arguments` string-vs-array empirical test |
| AUTO-02 | `--register-vrmanifest` CLI mode: `AddApplicationManifest` → poll `IsApplicationInstalled` ≤ 2s → `SetApplicationAutoLaunch` | Pattern 1 (exact sequence), Pitfall 1 (#1378 race), Code Example 1 (parseCliArgs), Code Example 3 (WinMain fork) |
| AUTO-03 | `--unregister-vrmanifest` — symmetric teardown for uninstaller | Pattern 1's `registerApp` has the mirror via `RemoveApplicationManifest(manifestPath)` |
| AUTO-04 | Idempotent re-registration on normal startup — self-heal across SteamVR upgrades / user removal | Pattern 4 (detached thread), D-15/D-18 log discipline, Pitfall 3 (#1547 mitigation) |
| AUTO-05 | Pump `IVRSystem::PollNextEvent`; on `VREvent_Quit`: `AcknowledgeQuit_Exiting()` → teardown → exit | Pattern 2 (ack-first idiom), Pitfall 2 (#1425 watchdog), D-11/D-12/D-13 |
| AUTO-06 | Auto-launched `micmap.exe` opens silently — no console, no focus steal, tray-icon-only | Anti-pattern list, Pitfall 11 (balloon mechanics), D-05/D-06/D-07/D-08/D-09 |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| CLI argument parsing (`--register-vrmanifest` / `--unregister-vrmanifest` / `--minimized`) | App — `WinMain` (anon ns or `common::CliArgs`) | — | Entry-point concern; must fork before any GUI init. Locked by D-01/D-02. |
| `IVRApplications` calls (Add / Remove / IsInstalled / SetAutoLaunch) | App — `src/steamvr/` new `manifest_registrar.{hpp,cpp}` | — | Keeps OpenVR surface inside `micmap_steamvr` library (matches existing `vr_input.cpp`). Locked by D-20. |
| `VR_Init(VRApplication_Utility)` for CLI mode | `WinMain` CLI fork owns init+shutdown around registrar | `manifest_registrar` calls into `vr::VRApplications()` only | Utility mode is HMD-independent; one init per CLI invocation. |
| `VR_Init(VRApplication_Background)` for GUI runtime monitoring | App — existing `OpenVRInput` in `vr_input.cpp` | — | Already in place; unchanged by Phase 3. |
| `AcknowledgeQuit_Exiting` call | `src/steamvr/src/vr_input.cpp::pollEvents()` VREvent_Quit branch | — | Must be called on the same thread that pumps events; ack-before-callback guarantees watchdog satisfied regardless of teardown latency. Locked by D-11. |
| `app.vrmanifest` emission | Build — `apps/micmap/app.vrmanifest.in` + CMake `configure_file` | — | Version string stays synced with `project(MicMap VERSION …)`. Locked by D-19. |
| Single-instance mutex + focus policy | App — existing `WinMain` `CreateMutexW` block | — | D-08 extends with `--minimized`-aware silent-skip path. |
| Tray balloon (`Shell_NotifyIcon NIM_MODIFY + NIF_INFO`) | App — `main.cpp` neighbor of `SetupSystemTray` | — | Reuses existing `NOTIFYICONDATAW` handle; only needs extra fields populated. |
| `AppConfig::shownTrayNotification` persistence | `src/core/` — `config_manager.hpp` + Phase 2 defensive reader | App — writer on balloon fire | `.value("shownTrayNotification", false)` pattern. Locked by D-09. |
| Fire-and-forget re-registration retry loop | App — `MicMapApp` member `std::thread` (detached on init, joined in `shutdown()`) | `manifest_registrar` free function for task body | **See Pitfall 6** — `std::async` future destructor blocks; use `std::thread` + atomic cancel. |

## Standard Stack

### Core

| Library / API | Version | Purpose | Why Standard |
|---------------|---------|---------|--------------|
| OpenVR SDK `IVRApplications` | **007** (currently linked via bey-closer-t1 SDK 2.5.1) / 008 (Valve master) — ABI-compatible for the 4 methods MicMap uses | Manifest registration + auto-launch flag | [VERIFIED: `C:/Users/decid/Documents/projects/mic-map/build/CMakeCache.txt` → OpenVR_INCLUDE_DIR points at bey-closer-t1/extern/openvr/headers; grep shows `IVRApplications_Version = "IVRApplications_007"`]. Do not hardcode version string. |
| Windows Shell API (`shellapi.h`) | Built-in (shell32.lib) | Tray icon + balloon (`NIF_INFO`), `CommandLineToArgvW` | [VERIFIED: `main.cpp:22` already `#pragma comment(lib, "shell32.lib")` and `main.cpp:163` already uses `Shell_NotifyIconW`] |
| PathCch API (`pathcch.h`) | Built-in (Pathcch.lib, Vista+) | Safe path manipulation for manifest abs-path resolution | [CITED: learn.microsoft.com/en-us/windows/win32/api/pathcch/nf-pathcch-pathcchremovefilespec] — MS recommends over older `PathRemoveFileSpec` (buffer-overrun vector) |
| CMake `configure_file` | 3.20+ (project requires 3.20) | Emit `app.vrmanifest` with substituted version | [VERIFIED: root CMakeLists.txt line 5 `cmake_minimum_required(VERSION 3.20)`; local `cmake --version` = 4.3.1] |

### Supporting

| Library / API | Version | Purpose | When to Use |
|---------------|---------|---------|-------------|
| `nlohmann/json` | 3.11.2 (vendored, Phase 2) | Read `shownTrayNotification` with `.value(key, default)` | One-field AppConfig addition. |
| `std::thread` + `std::atomic<bool>` | C++17 | Detached retry loop for GUI re-registration | Prefer over `std::async` (see Pitfall 6). |
| `common::Logger` (`MICMAP_LOG_*`) | In-repo | All log output including CLI-mode runs (D-04) | Already the app-wide sink. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff | Decision |
|------------|-----------|----------|----------|
| `IVRApplications` registration | Windows Run-key / `HKCU\...\Run` | OS-lifecycle, not SteamVR-lifecycle; orphan launches when SteamVR isn't running | Rejected milestone-wide per PROJECT.md. |
| `Shell_NotifyIcon NIF_INFO` balloon | Win10+ `ToastNotificationManager` | Richer UI, Action Center persistence | Rejected per D-10 — requires COM activator + AppUserModelID (Phase 4 surface). |
| `std::async(launch::async, ...)` fire-and-forget | `std::thread` detached + atomic flag | async future destructor joins (Pitfall 6) | Use `std::thread`. |
| `boost::program_options` / `getopt` | Hand-rolled `wcscmp` loop | 3-flag surface doesn't justify a dependency | Hand-roll per D-01. |
| `PathRemoveFileSpecW` (shlwapi) | `PathCchRemoveFileSpec` (pathcch) | shlwapi is older, known buffer-overrun vector | Use PathCch. |

**Installation:** No new vendored dependencies. `Pathcch.lib` is part of Windows SDK. Link with `target_link_libraries(micmap PRIVATE Pathcch)`.

**Version verification:** `IVRApplications_Version` is read from whatever `openvr.h` is linked. As of 2026-04-23 the linked SDK (bey-closer-t1/extern/openvr, v2.5.1) exposes `IVRApplications_007`. Valve master exposes `IVRApplications_008`. **Both are byte-compatible for the 4 methods MicMap calls.** Plan MUST use `vr::VRApplications()` accessor (resolved at link time), never a hardcoded version string.

## Architecture Patterns

### System Architecture Diagram

```
                       ┌──────────────────────┐
                       │ WinMain entry        │
                       │ CommandLineToArgvW   │
                       └──────────┬───────────┘
                                  │
            ┌─────────────────────┼─────────────────────┐
            │                     │                     │
            ▼                     ▼                     ▼
    ┌──────────────┐      ┌──────────────┐     ┌──────────────┐
    │ --register   │      │ --unregister │     │ GUI path     │
    │ -vrmanifest  │      │ -vrmanifest  │     │ (normal or   │
    │ (CLI)        │      │ (CLI)        │     │  --minimized)│
    └──────┬───────┘      └──────┬───────┘     └──────┬───────┘
           │                     │                    │
           ▼                     ▼                    ▼
    ┌──────────────────────────────────┐   ┌────────────────────┐
    │ IManifestRegistrar (new module)  │   │ Single-instance    │
    │ VR_Init(Utility)                 │   │ mutex              │
    │ → AddApplicationManifest(path,   │   │ --minimized-aware  │
    │      bTemporary=false)           │   │ focus skip (D-08)  │
    │ → poll IsApplicationInstalled    │   └────────┬───────────┘
    │      (100ms × 20, 2s ceiling)    │            │
    │ → SetApplicationAutoLaunch(true) │            ▼
    │ VR_Shutdown()                    │   ┌────────────────────┐
    │ exit 0 / 1                       │   │ D3D + ImGui +      │
    └──────────────────────────────────┘   │ audio + detector   │
                                           │ + driverClient     │
                                           │ + vrInput + tray   │
                                           └────────┬───────────┘
                                                    │
                    ┌───────────────────────────────┼────────────────────┐
                    │                               │                    │
                    ▼                               ▼                    ▼
          ┌─────────────────┐          ┌──────────────────────┐  ┌──────────────────┐
          │ Detached retry  │          │ Main loop (existing) │  │ If silent-boot:  │
          │ thread: every   │          │ pollEvents() pumps   │  │ NIM_MODIFY with  │
          │ GUI boot →      │          │ VREvent_Quit →       │  │ NIF_INFO balloon │
          │ ensureRegistered│          │ AcknowledgeQuit_     │  │ if config flag   │
          │ 30s retry if VR │          │ Exiting() SYNC,      │  │ shownTrayNotif   │
          │ not running     │          │ THEN notify app via  │  │ == false, then   │
          │                 │          │ VREventType::Quit    │  │ flip and save    │
          └─────────────────┘          └──────┬───────────────┘  └──────────────────┘
                                              │
                                              ▼
                                     ┌────────────────────┐
                                     │ MicMapApp::        │
                                     │ shutdown() (new)   │
                                     │ ordered teardown   │
                                     │ per D-12           │
                                     └────────────────────┘
```

### Recommended Project Structure (new files only)

```
apps/micmap/
├── app.vrmanifest.in        # NEW — CMake-substituted @MICMAP_VERSION@, @MICMAP_APP_KEY@
├── CMakeLists.txt           # MODIFIED — configure_file + link Pathcch
└── main.cpp                 # MODIFIED — CLI fork, retry thread, balloon, shutdown()

src/steamvr/
├── include/micmap/steamvr/
│   └── manifest_registrar.hpp   # NEW — IManifestRegistrar + factory
└── src/
    ├── manifest_registrar.cpp   # NEW — IVRApplications wrapper
    ├── vr_input.cpp             # MODIFIED — AcknowledgeQuit_Exiting in VREvent_Quit branch
└── CMakeLists.txt               # MODIFIED — add manifest_registrar.cpp to sources

src/core/include/micmap/core/
└── config_manager.hpp           # MODIFIED — AppConfig::shownTrayNotification bool
```

### Pattern 1: IVRApplications call sequence (registerApp)

**What:** Avoid the OpenVR #1378 race where `SetApplicationAutoLaunch` is called before the manifest propagates.
**When to use:** Every `registerApp()` call (CLI and GUI async).
**Example:**
```cpp
// Source: verified against openvr.h (bey-closer-t1 extern/openvr v2.5.1, IVRApplications_007)
//         + OpenVR issue #1378 workaround.
// Per D-02, the outer VR_Init(Utility) / VR_Shutdown is the caller's responsibility
// so CLI mode and GUI async share the registrar surface without duplicating init.

enum class RegisterResult { Success, AddFailed, PollTimeout, AutoLaunchFailed };

RegisterResult registerApp(const std::wstring& manifestAbsPath, const std::string& appKey) {
    std::string utf8Path = wide_to_utf8(manifestAbsPath);

    vr::EVRApplicationError err = vr::VRApplications()->AddApplicationManifest(
        utf8Path.c_str(), /*bTemporary=*/false);
    if (err != vr::VRApplicationError_None) {
        MICMAP_LOG_WARNING("AddApplicationManifest failed: ",
                           vr::VRApplications()->GetApplicationsErrorNameFromEnum(err));
        return RegisterResult::AddFailed;
    }

    // Poll per OpenVR #1378 (D-17: 100ms × 20 = 2000ms ceiling)
    MICMAP_LOG_INFO("polling for manifest install");
    constexpr int kPollIntervalMs = 100;
    constexpr int kPollMaxAttempts = 20;
    bool installed = false;
    for (int i = 0; i < kPollMaxAttempts; ++i) {
        if (vr::VRApplications()->IsApplicationInstalled(appKey.c_str())) {
            installed = true;
            MICMAP_LOG_INFO("manifest ready after ", (i + 1) * kPollIntervalMs, "ms");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    if (!installed) {
        MICMAP_LOG_WARNING("timeout after 2000ms");
        return RegisterResult::PollTimeout;
    }

    err = vr::VRApplications()->SetApplicationAutoLaunch(appKey.c_str(), /*bAutoLaunch=*/true);
    if (err != vr::VRApplicationError_None) {
        MICMAP_LOG_WARNING("SetApplicationAutoLaunch failed: ",
                           vr::VRApplications()->GetApplicationsErrorNameFromEnum(err));
        return RegisterResult::AutoLaunchFailed;
    }
    return RegisterResult::Success;
}
```

### Pattern 2: AcknowledgeQuit_Exiting inside pollEvents

**What:** Ack Valve's watchdog IMMEDIATELY, before app callback runs.
**When to use:** Every `VREvent_Quit`, once per SteamVR session.
**Example:**
```cpp
// Source: bey-closer-t1/extern/openvr/headers/openvr.h:2480-2482 (verified)
// "Call this to acknowledge to the system that VREvent_Quit has been received and that
//  the process is exiting. This extends the timeout until the process is killed."
// AcknowledgeQuit_Exiting returns void — does NOT terminate the process; it extends
// the watchdog so teardown can take as long as it needs.

void processVREvent(const vr::VREvent_t& event) {
    switch (event.eventType) {
        case vr::VREvent_Quit:
            MICMAP_LOG_INFO("SteamVR quit event received");
            // D-11: ack BEFORE app callback so watchdog clock is stopped regardless
            // of downstream teardown latency.
            if (vrSystem_) {
                vrSystem_->AcknowledgeQuit_Exiting();
            }
            notifyEvent(VREventType::Quit);  // app sets running=false, posts WM_STEAMVR_QUIT
            break;
        default:
            break;
    }
}
```

### Pattern 3: app.vrmanifest minimal content (D-19)

**What:** Minimum viable `app.vrmanifest` for a dashboard-overlay auto-launch app.
**When to use:** `apps/micmap/app.vrmanifest.in`, consumed by CMake `configure_file(... @ONLY)`.
**Example:**
```json
{
  "source": "builtin",
  "applications": [
    {
      "app_key": "@MICMAP_APP_KEY@",
      "launch_type": "binary",
      "binary_path_windows": "micmap.exe",
      "arguments": "--minimized",
      "is_dashboard_overlay": true,
      "strings": {
        "en_us": {
          "name": "MicMap",
          "description": "Hands-free SteamVR dashboard toggle via microphone pattern."
        }
      }
    }
  ]
}
```

**CRITICAL field notes:**
- `arguments` is a **JSON string** in every live-registration manifest I surveyed. CONTEXT.md D-05 text specifies array form. This is **Assumption A2** — plan Wave 0 should empirically verify with a one-shot register + SteamVR restart cycle before committing. Evidence leans heavily toward string form; Valve wiki and ecosystem examples (OVR Advanced Settings, SteamVR-PhasmoMatrix, etc.) use string.
- `binary_path_windows` is **relative** to the manifest file's directory. CMake `configure_file` emits manifest next to `micmap.exe`, so `"micmap.exe"` is correct.
- `source: "builtin"` is standard for third-party-installed (non-Steam-store) manifests.
- `image_path` intentionally omitted — no `.ico` / `.png` asset exists in repo (micmap.rc uses runtime `IDI_APPLICATION`). SteamVR falls back to a generic overlay-app glyph. Ship an asset later as polish.
- `app_version` / `strings.description` etc. are optional; adding them is Claude's discretion.

### Pattern 4: Detached std::thread for retry loop (NOT std::async)

**What:** Fire-and-forget background re-registration that may run for minutes.
**When to use:** GUI startup re-registration (D-15, D-16).
**Why not `std::async`:** The `std::future` returned by `std::async(launch::async, ...)` **blocks in its destructor** until the task completes. A fire-and-forget `std::async(launch::async, []{ retryForever(); });` expression yields a temporary whose destructor joins — making the call synchronous. [CITED: open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3679.html, cppreference `std::async`, modernescpp.com "Special Futures"]

```cpp
class MicMapApp {
    std::thread manifestRetryThread_;
    std::atomic<bool> manifestRetryCancel_{false};
    // ...
    void initialize() {
        // ... after createManifestRegistrar() and tray setup ...
        manifestRetryThread_ = std::thread([this]() {
            while (!manifestRetryCancel_.load()) {
                // ensureRegistered() does VR_Init(Utility) → check → maybe register → VR_Shutdown
                // Returns true on success, false on "SteamVR not running" or transient failure.
                if (manifestRegistrar_->ensureRegistered()) break;
                // 30s in 1s ticks so cancel is responsive
                for (int i = 0; i < 30 && !manifestRetryCancel_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        });
    }
    void shutdown() {
        manifestRetryCancel_.store(true);
        if (manifestRetryThread_.joinable()) manifestRetryThread_.join();
        // ... rest of D-12 teardown ...
    }
};
```

### Anti-Patterns to Avoid

- **Hardcoding `"IVRApplications_008"` (or any version string) in code.** Use `vr::VRApplications()` accessor — resolved at compile time from the linked SDK header. Makes the code robust to SDK swaps (see Assumption A1).
- **`AllocConsole()` / `AttachConsole()` in CLI mode** — violates AUTO-06. Use `common::Logger`'s file sink (D-04).
- **Calling `SetApplicationAutoLaunch` unconditionally without polling `IsApplicationInstalled` first** — triggers `VRApplicationError_UnknownApplication` per OpenVR #1378. Even when `AddApplicationManifest` returns success, the in-memory index update is async; poll is mandatory.
- **Holding the `std::async` future in a local stack variable** — destructor joins at scope end → silently blocks on first "SteamVR not running" attempt for 30s.
- **Calling `ShowWindow(SW_HIDE)` in silent mode** — existing `CreateWindowW` at `main.cpp:572` does not pass `WS_VISIBLE`, so the window is hidden by default. D-06 is mechanically: skip the existing `ShowWindow(g_app.hwnd, nCmdShow); UpdateWindow(g_app.hwnd)` at `main.cpp:597`.
- **Calling `Shell_NotifyIcon(NIM_ADD, ...)` a second time to show the balloon** — Shell rejects duplicate ID; use `NIM_MODIFY` on the already-added icon (see Pitfall 11).
- **Restarting the retry thread from inside the main loop when `isVRAvailable()` flips false** — retry loop runs in Utility mode, main loop's `vrInput` runs in Background mode; they are independent `VR_Init` handles. One thread, one `VR_Init` per attempt, always `VR_Shutdown` at end.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Command-line tokenization (quote handling, UTF-16) | Custom `wcstok` / `strchr` loop | `CommandLineToArgvW(GetCommandLineW(), &argc)` + `LocalFree(argv)` | Handles quoting, whitespace, UTF-16 natively. 3-line idiom. [CITED: learn.microsoft.com/.../commandlinetoargvw] |
| Strip filename from a full path | Manual `wcsrchr(L'\\')` + null-poke | `PathCchRemoveFileSpec(buf, len)` from `pathcch.h`, link `Pathcch.lib` | MS explicitly recommends over older `shlwapi` version for overrun-safety and long-path prefix support. [CITED: learn.microsoft.com/.../pathcch/nf-pathcch-pathcchremovefilespec] |
| Detect SteamVR running | Enumerate processes (`CreateToolhelp32Snapshot` / `Process32First` for `vrserver.exe`) | `VR_Init(VRApplication_Utility, &err)` — on failure check `VRInitError_Init_HmdNotFound` / `VRInitError_Init_NoServerForBackgroundApp` | Single API call; no tlhelp32 complexity. Utility mode is HMD-independent. [VERIFIED: openvr.h:1601-1603] |
| JSON manifest emission at build time | Hand-edit `app.vrmanifest` with version string | CMake `configure_file(app.vrmanifest.in app.vrmanifest @ONLY)` + `@MICMAP_VERSION@` substitution | Single source of truth; tracks `project(MicMap VERSION 0.1.0)`. |
| Tray balloon timing | `SetTimer` to remove-after-10s | Let Shell manage; `uTimeout` is deprecated since Vista | [CITED: learn.microsoft.com NOTIFYICONDATAA] — "This member is deprecated as of Windows Vista. Notification display times are now based on system accessibility settings." |
| Focus Assist / Quiet Hours detection | Manual `WNF_SHEL_QUIETHOURS_*` query | Set `NIIF_RESPECT_QUIET_TIME` in `dwInfoFlags`; Shell handles it | Graceful degrade built into Win7+. |
| Silent retry with cancel | `std::async` fire-and-forget | `std::thread` + `std::atomic<bool>` cancel flag | async future destructor blocks — see Pitfall 6. |

**Key insight:** Phase 3's complexity is in **ordering** (ack-before-teardown; poll-before-SetAutoLaunch; CLI-fork-before-RegisterClassExW) and **lifetime** (std::async vs std::thread; LocalFree argv; retry thread join in shutdown) — not API volume. Plan should emphasize state-machine correctness over code count.

## Runtime State Inventory

Phase 3 is feature-addition, not rename, but it introduces new external runtime-visible state:

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| **Stored data** | New `AppConfig::shownTrayNotification` bool. Slots into Phase 2 defensive reader (`.value("shownTrayNotification", false)`). Existing installs default false → one balloon on first silent boot after upgrade. | Code edit only; no data migration. One-field round-trip test (Open Q #4). |
| **Live service config** | SteamVR's per-user **`appconfig.json`** / state under `%LOCALAPPDATA%\openvr\` (the "Manage Startup Overlay Apps" store) gets mutated at runtime by `AddApplicationManifest`. NOT in git, NOT in MicMap install dir — SteamVR's own per-user state. | Registrar writes via OpenVR API (correct boundary). Phase 4 uninstaller invokes `--unregister-vrmanifest` → `RemoveApplicationManifest` for symmetric teardown per AUTO-03. |
| **OS-registered state** | None — AUTO explicitly excludes Windows Run-key, Task Scheduler, services, startup folder. | None. |
| **Secrets / env vars** | `OPENVR_SDK_PATH` env var governs CMake SDK resolution (build-time only). No runtime secrets. | None; see Environment Availability. |
| **Build artifacts / installed packages** | NEW: `app.vrmanifest` emitted to `$<TARGET_FILE_DIR:micmap>/`. Phase 4 installer ships it. Manual UAT until Phase 4 lands runs CLI modes against the build-dir manifest. | Clean build dir between tests; no cross-build contamination. |

**Nothing found** in "OS-registered state" and "Secrets/env vars" — verified by grep of existing codebase for TaskScheduler, Run-key, SOPS, pm2, systemd, launchd, Registry writes outside of Shell_NotifyIcon (which uses Shell IPC, not Registry for balloons).

## Common Pitfalls

### Pitfall 1: OpenVR #1378 — SetApplicationAutoLaunch race

**What goes wrong:** `AddApplicationManifest` returns `VRApplicationError_None`, but immediately-following `SetApplicationAutoLaunch` returns `VRApplicationError_UnknownApplication`.
**Why:** Manifest install is asynchronous inside vrserver; the in-memory index updates on a different tick than the call returns.
**How to avoid:** Poll `IsApplicationInstalled(appKey)` 100ms × 20 per D-17 before calling `SetApplicationAutoLaunch`.
**Warning signs:** Intermittent failure in post-install UAT that "works on second run."
**Source:** [CITED: github.com/ValveSoftware/openvr/issues/1378]

### Pitfall 2: OpenVR #1425 — SteamVR respawn / watchdog

**What goes wrong:** App doesn't pump `VREvent_Quit` + ack → SteamVR kills the app after its watchdog timeout → next SteamVR launch sees a zombie process → relaunch fails or first instance is force-killed.
**Why:** Valve's watchdog assumes unresponsive overlay apps are hung.
**How to avoid:** Call `AcknowledgeQuit_Exiting()` synchronously inside the `VREvent_Quit` handler, BEFORE any teardown work (D-11). Ack is void-return and does not terminate — it resets the watchdog clock so teardown has unlimited time.
**Warning signs:** Logs show MicMap "killed" rather than "shutdown"; second SteamVR launch races with still-dying first instance.
**Source:** [CITED: github.com/ValveSoftware/openvr/issues/1425] — WebFetch confirmed scenario. 2-second watchdog interval is community-consistent (dreiekk/OpenVR-Autostarter README) but not a Valve-documented constant — see Assumption A3.

### Pitfall 3: OpenVR #1547 — SetApplicationAutoLaunch persistence drift

**What goes wrong:** Set succeeds, appears in "Manage Startup Overlay Apps," but after a SteamVR restart the app vanishes (and `IsApplicationInstalled` returns false).
**Why:** SteamVR appconfig.json flushing bug; observed on SteamVR ~1.16.x / OpenVR 1.16.8. Modern SteamVR (2024+) seems less frequent but no confirmed fix commit.
**How to avoid:** Re-register on every GUI boot via D-15's `ensureRegistered()` retry loop. Idempotent: `IsApplicationInstalled == true` → no-op; `false` → re-Add + re-SetAutoLaunch.
**Warning signs:** User reports "MicMap stopped auto-launching after SteamVR updated."
**Source:** [CITED: github.com/ValveSoftware/openvr/issues/1547] — no Valve fix documented.

### Pitfall 4: VRApplication_Utility is HMD-independent

**What goes wrong:** Developer assumes `VR_Init(Utility)` needs an HMD because `VR_Init(Background)` warns on no-HMD.
**How to avoid:** CLI mode uses `VR_Init(VRApplication_Utility)`. Per `openvr.h:1601-1603`: *"Init should not try to load any drivers. The application needs access to utility interfaces (like IVRApplications) that can be used without GPU resources."* Works without HMD.
**Source:** [VERIFIED: bey-closer-t1/extern/openvr/headers/openvr.h:1603]

### Pitfall 5: EVRApplicationError_None is the correct success enum

**What goes wrong:** Conflate `vr::EVRApplicationError` with `vr::EVRInitError` on return checks.
**How to avoid:** Use `vr::VRApplicationError_None` for the 4 `IVRApplications` methods; use `vr::VRInitError_None` for `VR_Init`. Error-to-string: `vr::VRApplications()->GetApplicationsErrorNameFromEnum(err)` (not `VR_GetVRInitErrorAsEnglishDescription`).
**Source:** [VERIFIED: bey-closer-t1/extern/openvr/headers/openvr.h:2517-2543, 2652]

### Pitfall 6: std::async future destructor joins

**What goes wrong:**
```cpp
// LOOKS fire-and-forget; actually BLOCKS:
std::async(std::launch::async, []{ retryRegisterForever(); });  // temporary future dtor waits!
```
The returned `future` is a temporary whose destructor invokes `.wait()` for `async`-launched tasks. If the task takes 30s, the call blocks for 30s.
**Why:** Standards-mandated "async future destructor must wait" [CITED: open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3679.html].
**How to avoid:** Two valid options:
  1. **Detach a raw `std::thread`** (recommended — supports cancellation via atomic flag).
  2. Hold the future as a member variable (task runs in parallel; future destructor at app-shutdown blocks, acceptable if join time is bounded).
**Note:** Existing code at `main.cpp:627` holds futures as `static std::future<void>` inside the main loop. That pattern is correct for its 100ms-bounded work (`wait_for(0ms)` check before reissue). Phase 3's retry loop runs minutes → needs the thread primitive.
**Source:** [CITED: en.cppreference.com/w/cpp/thread/async, modernescpp.com/index.php/the-special-futures/]

### Pitfall 7: Tray balloon suppressed by Focus Assist / DND

**What goes wrong:** Win11 Focus Assist suppresses balloons (gaming mode, DND hours). Shell silently drops; no error return.
**How to avoid:**
  1. Set `NIIF_RESPECT_QUIET_TIME` in `dwInfoFlags` — graceful degrade per MS docs.
  2. Do NOT re-fire on subsequent boots just because the persistent flag says "shown once" — the flag is about "we tried," not "user saw it." Acceptable per D-09 tradeoff.
**Primary UX safety net:** Tray icon itself (NIF_ICON always on) is the discoverability surface; balloon is a nicety.
**Source:** [CITED: learn.microsoft.com NOTIFYICONDATAA dwInfoFlags]

### Pitfall 8: CommandLineToArgvW argv lifetime

**What goes wrong:** Store pointers into argv array, `LocalFree(argv)`, then use → use-after-free.
**How to avoid:** Copy flag values into a `CliFlags` struct **before** `LocalFree`:
```cpp
int argc = 0;
LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
CliFlags flags = parseArgs(argc, argv);  // COPIES what it needs
LocalFree(argv);                         // argv invalid after this
// ... use flags (plain bools, safe) ...
```
**Source:** [CITED: learn.microsoft.com/.../commandlinetoargvw] — "The calling application must free this memory using a single call to the LocalFree function."

### Pitfall 9: GetModuleFileNameW truncation

**What goes wrong:** `WCHAR buf[MAX_PATH]` (260) may be too small on Win10+ with long-path support. `GetModuleFileNameW` silently truncates and sets `GetLastError() == ERROR_INSUFFICIENT_BUFFER`.
**How to avoid:** Use `WCHAR buf[32768]` (64KB on stack, always safe for paths up to the kernel ceiling). Or check `GetLastError()` after the call and grow.
**Recommendation:** `WCHAR buf[32768]` for paranoid correctness; path strings do not outlive `WinMain`.

### Pitfall 10: configure_file timing

**What goes wrong:** `configure_file` runs at CMake **configure** time. Changing `@MICMAP_VERSION@` between configures re-runs it; changing the `.in` file between builds does too (CMake tracks it as a dependency). BUT if someone changes the version via `set(MICMAP_VERSION …)` after the `configure_file` call, stale output can leak.
**How to avoid:**
  1. Add explicit `set(MICMAP_VERSION ${PROJECT_VERSION})` BEFORE `configure_file` in `apps/micmap/CMakeLists.txt`.
  2. `configure_file(... @ONLY)` — the `@ONLY` restricts substitution to `@VAR@` (not `${VAR}`), avoiding accidental JSON-syntax collisions.
**Warning signs:** Built `app.vrmanifest` has stale version string after a version bump without re-configure.

### Pitfall 11: Balloon via NIM_MODIFY (not NIM_ADD)

**What goes wrong:** Developer `Shell_NotifyIcon(NIM_ADD, ...)` a second time to re-add icon with `NIF_INFO` → Shell rejects duplicate ID → icon disappears OR balloon doesn't fire.
**How to avoid:** Tray icon is already NIM_ADD'd at `main.cpp:163` via `SetupSystemTray`. Balloon fires `NIM_MODIFY` on the **same `nid` struct** with `NIF_INFO` in `uFlags` + `szInfoTitle` / `szInfo` / `dwInfoFlags` populated. After firing, subsequent NIM_MODIFY calls for tooltip / icon updates must **clear `NIF_INFO` and zero `szInfo`** to avoid re-firing:

```cpp
// Fire
nid.uFlags |= NIF_INFO;
wcscpy_s(nid.szInfoTitle, L"MicMap");
wcscpy_s(nid.szInfo, L"Running in the system tray. Click the icon to open.");
nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
Shell_NotifyIconW(NIM_MODIFY, &nid);

// Later clear (otherwise a future tooltip NIM_MODIFY re-fires the balloon)
nid.uFlags &= ~NIF_INFO;
nid.szInfo[0] = L'\0';
nid.szInfoTitle[0] = L'\0';
```
**Source:** [CITED: learn.microsoft.com NOTIFYICONDATAA] — "To remove the balloon notification from the UI ... set the NIF_INFO flag in uFlags and set szInfo to an empty string."

### Pitfall 12: app.vrmanifest `arguments` field — string vs array ambiguity

**What goes wrong:** CONTEXT.md D-05 says `"arguments": ["--minimized"]` (array). Every live GitHub manifest I inspected uses string form `"arguments": "--minimized"`. If the wrong form is used, SteamVR ignores the field → GUI boots non-silent → AUTO-06 fails validation.
**Evidence:** jacklul/SteamVR-PhasmoMatrix, OVR-Advanced-Settings, Valve wiki examples all use string form.
**Resolution:** **Assumption A2** — plan Wave 0 should hand-register both forms against a live SteamVR to confirm which is accepted. Prior is heavily on string form.

## Code Examples

### 1. CLI argument parsing (D-01) — anonymous-namespace helper

```cpp
namespace {
struct CliFlags {
    bool registerManifest = false;
    bool unregisterManifest = false;
    bool minimized = false;
};

CliFlags parseCliArgs() {
    CliFlags flags{};
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return flags;  // graceful: treat as no flags
    for (int i = 1; i < argc; ++i) {  // argv[0] is exe name
        if (wcscmp(argv[i], L"--register-vrmanifest") == 0)   flags.registerManifest = true;
        else if (wcscmp(argv[i], L"--unregister-vrmanifest") == 0) flags.unregisterManifest = true;
        else if (wcscmp(argv[i], L"--minimized") == 0)        flags.minimized = true;
    }
    LocalFree(argv);
    return flags;
}
}  // namespace
```

### 2. Resolve `app.vrmanifest` absolute path at runtime (D-21)

```cpp
#include <pathcch.h>
#pragma comment(lib, "Pathcch.lib")   // or target_link_libraries(... Pathcch)

std::wstring resolveManifestAbsolutePath() {
    WCHAR buf[32768];  // long-path safe
    DWORD n = GetModuleFileNameW(nullptr, buf, _countof(buf));
    if (n == 0 || n == _countof(buf)) {
        MICMAP_LOG_ERROR("GetModuleFileNameW failed or truncated");
        return {};
    }
    if (FAILED(PathCchRemoveFileSpec(buf, _countof(buf)))) {
        MICMAP_LOG_ERROR("PathCchRemoveFileSpec failed");
        return {};
    }
    std::wstring result = buf;
    result += L"\\app.vrmanifest";
    return result;
}
```

### 3. WinMain CLI fork (D-02, D-08)

```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    CliFlags flags = parseCliArgs();

    // Headless CLI modes — early exit BEFORE any GUI init (D-02)
    if (flags.registerManifest || flags.unregisterManifest) {
        common::Logger::initialize();  // file sink only — no AllocConsole (D-04)
        vr::EVRInitError initErr = vr::VRInitError_None;
        vr::VR_Init(&initErr, vr::VRApplication_Utility);
        if (initErr != vr::VRInitError_None) {
            MICMAP_LOG_ERROR("CLI mode VR_Init(Utility) failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
            return 1;  // D-03
        }
        auto registrar = steamvr::createManifestRegistrar();
        int exitCode = 0;
        if (flags.registerManifest)        exitCode = registrar->registerApp()   ? 0 : 1;
        else /* unregisterManifest */      exitCode = registrar->unregisterApp() ? 0 : 1;
        vr::VR_Shutdown();
        return exitCode;
    }

    // Single-instance mutex with --minimized-aware focus skip (D-08)
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!flags.minimized) {  // user-clicked second instance — bring existing window forward
            HWND w = FindWindowW(L"MicMapMain", nullptr);
            if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
        }
        // --minimized second instance: SteamVR is re-launching us; the first instance
        // is already alive — exit silently, don't steal focus.
        return 0;
    }

    // ... existing GUI flow up through g_app.initialize() and SetupSystemTray ...

    // D-06: never ShowWindow in silent mode
    if (flags.minimized) {
        g_app.minimizedToTray = true;
    } else {
        ShowWindow(g_app.hwnd, nCmdShow);
        UpdateWindow(g_app.hwnd);
    }

    // D-09: first-silent-launch balloon (once per install)
    if (flags.minimized
        && g_app.configManager
        && !g_app.configManager->getConfig().shownTrayNotification) {
        fireFirstLaunchBalloon(g_app.nid);
        g_app.configManager->getConfig().shownTrayNotification = true;
        g_app.configManager->saveDefault();
    }

    // ... rest of main loop unchanged, plus g_app.shutdown() replaces existing teardown at :659 ...
}
```

### 4. First-launch balloon (D-09, Pitfall 11)

```cpp
void fireFirstLaunchBalloon(NOTIFYICONDATAW& nid) {
    UINT prevFlags = nid.uFlags;
    nid.uFlags |= NIF_INFO;
    wcscpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), L"MicMap");
    wcscpy_s(nid.szInfo, _countof(nid.szInfo),
             L"Running in the system tray. Click the icon to open.");
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    // Clear NIF_INFO + szInfo so future NIM_MODIFY (tooltip/icon updates) don't re-fire
    nid.uFlags = prevFlags;
    nid.szInfo[0] = L'\0';
    nid.szInfoTitle[0] = L'\0';
}
```

### 5. AcknowledgeQuit_Exiting injection (D-11)

```cpp
// src/steamvr/src/vr_input.cpp — modify existing processVREvent()
void processVREvent(const vr::VREvent_t& event) {
    switch (event.eventType) {
        case vr::VREvent_Quit:
            MICMAP_LOG_INFO("SteamVR quit event received");
            // D-11 / Pitfall 2: ack IMMEDIATELY, before app callback, before any teardown
            if (vrSystem_) vrSystem_->AcknowledgeQuit_Exiting();
            notifyEvent(VREventType::Quit);
            break;
        default:
            break;
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `strstr(lpCmdLine, "--minimized")` (main.cpp:596) | `CommandLineToArgvW` + wcscmp loop | Phase 3 | Correct quote handling; UTF-16 native; extensible |
| `PathRemoveFileSpec` (shlwapi) | `PathCchRemoveFileSpec` (pathcch, Vista+) | ~2015 MS API guidance | Buffer-overrun-safe; `\\?\` long-path support |
| `NOTIFYICONDATA.uTimeout` | Omit (Shell uses accessibility default) | Vista | MS: "deprecated as of Windows Vista" |
| `std::async(launch::async, ...)` fire-and-forget | Detached `std::thread` + atomic cancel | C++11/14 practice | async future destructor joins (Pitfall 6) |
| Win10 Toast (`ToastNotificationManager`) | Retained `Shell_NotifyIcon NIF_INFO` balloon | Deferred | Toast needs AppUserModelID + COM activator; scope creep |
| Register once at install time only | Re-register every GUI boot (D-15 ensureRegistered) | OpenVR #1547 mitigation | Idempotent; self-heals across SteamVR upgrades |

**Deprecated / outdated:**
- Hardcoded `"IVRApplications_008"` strings — use `vr::VRApplications()` accessor.
- `AllocConsole` for CLI output — violates AUTO-06.
- Per-tick logging in poll loops — see D-17/D-18 state-change-only discipline.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `IVRApplications_007` and `IVRApplications_008` are ABI-compatible for the 4 methods MicMap calls (`AddApplicationManifest`, `IsApplicationInstalled`, `SetApplicationAutoLaunch`, `RemoveApplicationManifest`). | Stack, Pattern 1 | If Valve changed any method signature between 007 and 008, linker / vtable mismatch at runtime. Mitigation: use `vr::VRApplications()` accessor (compile-time resolved) and grep-verify the linked SDK at plan time. Empirical prior: verified signatures in 007 (openvr.h:2603, 2606, 2609, 2666) match every public 008-referencing reference / tutorial I surveyed. |
| A2 | `"arguments"` in `app.vrmanifest` is a JSON string (`"arguments": "--minimized"`), NOT an array. CONTEXT.md D-05 says array. | Pattern 3, Pitfall 12 | If format is wrong → SteamVR ignores flag → GUI boots non-silent → AUTO-06 fails. Mitigation: Wave 0 empirical register + SteamVR restart test of both forms. Prior heavily favors string (jacklul/SteamVR-PhasmoMatrix + multiple GitHub manifests). |
| A3 | SteamVR's watchdog interval is ~2000ms between `VREvent_Quit` and force-kill. | Pitfall 2 | If shorter, ack-before-callback is even more critical (already doing this). If longer, non-critical. Community references consistently cite 2s; no Valve doc quote. Low risk. |
| A4 | Adding `AppConfig::shownTrayNotification` integrates cleanly with the Phase 2 defensive reader via `.value("shownTrayNotification", false)`. | Stored data, Open Q #4 | Low — Phase 2 explicitly designed for forward-compat. Verify via a one-field round-trip test. |
| A5 | `VR_Init(VRApplication_Utility)` works on a machine with SteamVR runtime installed but `vrserver.exe` NOT currently running — needed for installer-invoked `--register-vrmanifest` to succeed before the user launches SteamVR. | Don't Hand-Roll, Open Q #2 | If false, installer post-install `--register-vrmanifest` fails. Graceful degrade: GUI fire-and-forget retry loop (D-15) re-registers on first GUI launch. Phase 4 treats CLI failure as non-fatal. Empirical test at Wave 0. |
| A6 | `configure_file(app.vrmanifest.in app.vrmanifest @ONLY)` correctly substitutes `@MICMAP_VERSION@` from `PROJECT_VERSION`. | Pattern 3, Pitfall 10 | If substitution fails → literal `@MICMAP_VERSION@` in generated manifest → SteamVR may reject or display garbage. Mitigation: explicit `set(MICMAP_VERSION ${PROJECT_VERSION})` before the configure_file call; post-build cat verification. |

## Open Questions

1. **Does linked OpenVR 2.5.1 exhibit `SetApplicationAutoLaunch` #1547 persistence drift?**
   - Known: Method signatures identical to 2.15.6; #1547 filed against OpenVR 1.16.8.
   - Unclear: Whether Valve silently fixed between 2.5.1 and 2.15.6.
   - Recommendation: Treat as UAT-discoverable; re-registration loop handles either case. Optional ROADMAP follow-up to bump SDK if symptoms observed.

2. **Does installer-invoked `micmap.exe --register-vrmanifest` (Phase 4) succeed when SteamVR has never been launched since install (i.e., `vrserver.exe` not running)?**
   - Known: `VR_Init(Utility)` is HMD-independent; works without HMD.
   - Unclear: Whether the registration call requires `vrserver` to be started at least once, or whether the write goes directly to `appconfig.json`.
   - Recommendation: Phase 4 Wave 0 empirical test on clean VM (Steam installed, SteamVR never launched). If fails, installer `[Run]` treats as non-fatal; GUI's D-15 retry closes the loop on first user-launched SteamVR.

3. **Is there a stable `micmap.png` asset for `image_path`?**
   - Known: `apps/micmap/micmap.rc` comments "Icon is loaded from system resources at runtime"; no `.ico` / `.png` in repo.
   - Unclear: Whether to ship placeholder or omit field.
   - Recommendation: Omit `image_path` in Phase 3. SteamVR falls back to generic overlay glyph. Add as polish when a brand asset exists.

4. **Does Phase 2's defensive reader preserve unknown fields on round-trip, so `shownTrayNotification` persists after a save?**
   - Known: `.value(key, default)` pattern is forward-compat for **reads**.
   - Unclear: Whether Phase 2 writer round-trips unknown fields (unlikely — writers usually serialize a known struct).
   - Recommendation: Wave 0 test — write a config with `shownTrayNotification: true`, mutate an unrelated field, save, reload, confirm the new field persists. One-unit-test scope. Side effect: confirms D-09's persistence claim.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build | ✓ | 4.3.1 | — |
| OpenVR SDK (linked) | `manifest_registrar` + existing `vr_input` | ✓ | 2.5.1 (bey-closer-t1/extern/openvr) exposing IVRApplications_007 | — (compatible with what's needed) |
| Windows Pathcch.lib | `manifest_registrar` path resolution | ✓ | Built-in since Vista | — |
| Windows SDK (shell32, shellapi) | Tray + `CommandLineToArgvW` | ✓ | Win11 headers | — |
| SteamVR runtime | UAT (not build) | Assume yes on dev machine | — | Manual UAT; CLI mode returns code 1 gracefully when VR_Init fails |
| Real HMD (Valve Index / Beyond) | AUTO-01 / AUTO-05 / AUTO-06 visual UAT | Assume yes on dev machine | — | Cannot automate full-cycle validation headlessly |
| Inno Setup (ISCC.exe) | Phase 4 (not Phase 3) | ✓ | `C:/Program Files (x86)/Inno Setup 6/ISCC.exe` | Deferred |
| Git | VCS | ✓ | — | — |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest (native); optional GTest via `-DMICMAP_USE_GTEST=ON`. Existing `tests/test_placeholder.cpp` uses plain `main()` + exit codes. |
| Config file | `C:/Users/decid/Documents/projects/mic-map/tests/CMakeLists.txt` |
| Quick run command | `ctest --output-on-failure -R 'manifest\|tray\|cli\|vr_input_quit'` |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| AUTO-01 | `app.vrmanifest` has `app_key="bigscreen.micmap"`, `is_dashboard_overlay=true`, `launch_type="binary"`, `binary_path_windows="micmap.exe"` | unit (JSON schema assert on generated file) | `ctest -R test_vrmanifest_schema` | ❌ Wave 0 |
| AUTO-01 | MicMap appears in SteamVR "Manage Startup Overlay Apps" after `--register-vrmanifest` | manual UAT | Full SteamVR restart cycle on dev HMD | N/A |
| AUTO-02 | `--register-vrmanifest` exits 0 on success; polls IsApplicationInstalled 100ms×20; calls SetApplicationAutoLaunch only after | integration (stub `IVRApplications`, inject error states) | `ctest -R test_manifest_registrar` | ❌ Wave 0 |
| AUTO-02 | #1378 guard: SetApplicationAutoLaunch never called before `IsApplicationInstalled == true` | unit (mock asserts call order) | Same | ❌ Wave 0 |
| AUTO-03 | `--unregister-vrmanifest` calls `RemoveApplicationManifest` + exits 0 | integration (stub) | `ctest -R test_manifest_registrar_unregister` | ❌ Wave 0 |
| AUTO-04 | Idempotent: `IsApplicationInstalled==true` → no `AddApplicationManifest` call | unit (mock asserts call count) | `ctest -R test_manifest_registrar_idempotent` | ❌ Wave 0 |
| AUTO-04 | SteamVR upgrade simulation: flip `IsApplicationInstalled` false → re-register succeeds | integration | Same harness | ❌ Wave 0 |
| AUTO-05 | `VREvent_Quit` → `AcknowledgeQuit_Exiting` called BEFORE callback | unit (mock `IVRSystem`, call-order recorder) | `ctest -R test_vr_input_quit_ordering` | ❌ Wave 0 |
| AUTO-05 | Full SteamVR quit → MicMap exits within 2s | manual UAT | SteamVR restart cycle | N/A |
| AUTO-06 | Built binary is `/SUBSYSTEM:WINDOWS` | grep gate | `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` must return empty | Shell one-liner; no test file |
| AUTO-06 | `--minimized` causes hidden window + no focus steal | manual UAT | Full SteamVR restart cycle; visual | N/A |
| AUTO-06 | Second-instance mutex with `--minimized` exits silently (no `PostMessage(IDM_SHOW)`) | unit | `ctest -R test_cli_flags_parse` + integration mutex probe | ❌ Wave 0 |
| AUTO-06 | First-silent-launch balloon fires once; flag persists; second silent boot does not re-fire | integration (seam on Shell_NotifyIcon + persistent config) | `ctest -R test_tray_balloon_once` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --output-on-failure -R 'manifest\|tray\|cli\|vr_input_quit'` (targeted)
- **Per wave merge:** `ctest --output-on-failure` (full suite)
- **Phase gate:** Full suite green + manual UAT cycle signed off before `/gsd-verify-work`

### Wave 0 Gaps

- [ ] `tests/test_manifest_registrar.cpp` — covers AUTO-02, AUTO-03, AUTO-04. Requires `createManifestRegistrar` factory to accept an injected `vr::IVRApplications*` (or wrapper interface) for stubbing. Add an overload `createManifestRegistrar(IVRApplicationsSurface&)` or pass via constructor.
- [ ] `tests/test_vr_input_quit_ordering.cpp` — covers AUTO-05's ack-first invariant. Requires call-order recorder; may need small refactor inside `vr_input.cpp` to make the `vr::IVRSystem*` pointer injectable (currently internal `vrSystem_` member).
- [ ] `tests/test_cli_flags_parse.cpp` — covers AUTO-06 flag parsing. Pure unit. Extract `parseCliArgs` from anon ns into a namespace-qualified helper for testability (Claude's discretion, D-01 leaves location flexible).
- [ ] `tests/test_tray_balloon_once.cpp` — covers AUTO-06 balloon persistence. Thin seam on `Shell_NotifyIcon` (function pointer or small wrapper) + `IConfigManager` stub → checks post-balloon flag state and that a second boot with flag-true does not call the seam.
- [ ] `tests/test_vrmanifest_schema.cpp` — loads generated `app.vrmanifest` via nlohmann/json, asserts required fields + that `arguments` string-vs-array matches the live-verified form (A2). Requires test runs after `micmap` target builds (`add_dependencies(test_vrmanifest_schema micmap)`).
- [ ] CTest + plain-main test harness pattern (matching `test_placeholder.cpp`) is sufficient — GTest not required. Keeps dependency surface minimal.

## Security Domain

> `security_enforcement` status: absent from `.planning/config.json` → treated as enabled. Phase 3 has low security surface: no network, no auth, no untrusted input parsing beyond CLI args and a single JSON config field.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | yes (CLI args, manifest JSON round-trip) | `wcscmp` exact-match for flags; `nlohmann/json` `allow_exceptions=false` defensive reader (Phase 2 pattern) |
| V6 Cryptography | no | — |
| V10 Malicious Code | yes (installer-invoked CLI runs elevated in Phase 4; Phase 3's CLI surface is what gets elevated) | Phase 3 CLI does no FS writes outside SteamVR appconfig.json (via OpenVR API); no shell-out; no registry writes outside Shell_NotifyIcon |
| V12 Files and Resources | yes (manifest path resolution) | `PathCchRemoveFileSpec` (safe) over `PathRemoveFileSpec` (buffer-overrun-prone); sized buffer (32768 WCHARs) for `GetModuleFileNameW` |

### Known Threat Patterns for Windows C++ Win32 GUI Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Buffer overrun on path manipulation | Tampering / EoP | `PathCchRemoveFileSpec` + sized buffer; verify `GetModuleFileNameW` return (Pitfall 9) |
| argv use-after-free from `CommandLineToArgvW` | Tampering | Copy flags into struct before `LocalFree(argv)` (Pitfall 8) |
| Unsafe JSON parsing on untrusted config | Tampering / DoS | Phase 2 defensive reader inherits; Phase 3 adds one bool |
| Privilege escalation via admin-elevated installer calling untrusted binary | EoP | Phase 4 concern. Phase 3 CLI runs as admin ONLY when invoked by installer; it performs no FS writes beyond OpenVR API calls |
| Shell_NotifyIcon spoofing / balloon-driven phishing | Spoofing | Balloon text is hardcoded; no user-controlled interpolation |
| Side-loaded OpenVR DLL | Tampering | `openvr_api.dll` ships alongside `micmap.exe`; Windows DLL search-order favors sibling dir. Installer (Phase 4) owns install dir |

## Project Constraints (from CLAUDE.md)

- **Windows-only.** WASAPI, OpenVR driver DLL, Inno Setup. Non-Windows stubs remain.
- **Bash via Git Bash.** Unix-style paths (`/dev/null`, forward slashes) in shell commands; PowerShell available.
- **Stack locked this milestone.** C++17, CMake, ImGui+D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. No framework changes.
- **Installer runs elevated (admin); runtime does not require admin.** Phase 3's CLI modes are runtime-level — must NOT require admin. Writing to SteamVR's per-user `appconfig.json` via OpenVR API goes through per-user state (not `Program Files`), so no elevation needed.
- **Phase order is load-bearing.** Do not reorder. Phase 3 requires Phase 1 complete (✓), Phase 2 landed (✓).
- **Never skip phase artifacts.** RESEARCH.md / PLAN.md / SUMMARY.md are memory across context resets.

## Sources

### Primary (HIGH confidence)

- `C:/Users/decid/Documents/projects/bey-closer-t1/extern/openvr/headers/openvr.h` — linked SDK; verified `IVRApplications` method signatures (lines 2595-2721), `IVRSystem::AcknowledgeQuit_Exiting` (line 2482, void return), `VRApplication_Utility` semantics (line 1603), `EVRApplicationError` enum (lines 2517-2543)
- `C:/Users/decid/Documents/projects/mic-map/build/CMakeCache.txt` — confirmed linked SDK path + IVRApplications_007 exposure
- `C:/Users/decid/Documents/projects/mic-map/apps/micmap/main.cpp` — existing WinMain, tray `NOTIFYICONDATAW nid`, mutex pattern, main-loop structure
- `C:/Users/decid/Documents/projects/mic-map/src/steamvr/src/vr_input.cpp` — `VR_Init(Background)` + `PollNextEvent` precedent
- `C:/Users/decid/Documents/projects/bey-closer-t1/installer/BeyondProximity.iss` — Phase 4 reference (NOTE: no `app.vrmanifest` — BeyondProximity is driver-only; reference is for Phase 4 installer patterns, not Phase 3 manifest conventions)
- [learn.microsoft.com/en-us/windows/win32/api/pathcch/nf-pathcch-pathcchremovefilespec](https://learn.microsoft.com/en-us/windows/win32/api/pathcch/nf-pathcch-pathcchremovefilespec) — PathCchRemoveFileSpec authoritative
- [learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataa](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataa) — NOTIFYICONDATA fields, NIF_INFO semantics, NIIF_RESPECT_QUIET_TIME, uTimeout deprecation
- [learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw) — CommandLineToArgvW lifetime + LocalFree
- [cmake.org/cmake/help/latest/prop_tgt/WIN32_EXECUTABLE.html](https://cmake.org/cmake/help/latest/prop_tgt/WIN32_EXECUTABLE.html) — `add_executable(WIN32)` → `/SUBSYSTEM:WINDOWS`

### Secondary (MEDIUM confidence)

- [github.com/ValveSoftware/openvr/issues/1378](https://github.com/ValveSoftware/openvr/issues/1378) — VRApplicationError_UnknownApplication race → poll workaround
- [github.com/ValveSoftware/openvr/issues/1425](https://github.com/ValveSoftware/openvr/issues/1425) — relaunch-while-zombie → VREvent_Quit handling required
- [github.com/ValveSoftware/openvr/issues/1547](https://github.com/ValveSoftware/openvr/issues/1547) — SetApplicationAutoLaunch persistence drift (SteamVR 1.16.10 / OpenVR 1.16.8)
- [github.com/ValveSoftware/openvr/blob/master/headers/openvr_capi.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr_capi.h) — Valve master `IVRApplications_Version = "IVRApplications_008"`
- [open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3679.html](https://open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3679.html) — `std::async` future destructor blocking semantics (standards paper)
- [modernescpp.com/index.php/the-special-futures/](https://www.modernescpp.com/index.php/the-special-futures/) — practical summary of `std::async` pitfalls
- [en.cppreference.com/w/cpp/thread/async](https://en.cppreference.com/w/cpp/thread/async) — cppreference
- [github.com/jacklul/SteamVR-PhasmoMatrix/blob/master/manifest.vrmanifest](https://github.com/jacklul/SteamVR-PhasmoMatrix/blob/master/manifest.vrmanifest) — real vrmanifest example (but `arguments` field not quoted in the webfetched snippet → A2)
- [github.com/dreiekk/OpenVR-Autostarter](https://github.com/dreiekk/OpenVR-Autostarter) — VREvent_Quit lifecycle reference
- [github.com/OpenVR-Advanced-Settings/OpenVR-AdvancedSettings](https://github.com/OpenVR-Advanced-Settings/OpenVR-AdvancedSettings) — behavioral baseline for dashboard-overlay app

### Tertiary (LOW confidence — flagged)

- Assumption A2 (`arguments` JSON string vs array): mixed evidence; empirical Wave 0 test required.
- Assumption A3 (2s watchdog): community-consistent; no Valve doc quote.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries verified against in-repo code + MS docs
- Architecture: HIGH — aligns with locked CONTEXT.md; mechanical composition of documented primitives
- Pitfalls: HIGH on #1378 / #1425 / async-future (directly verified); MEDIUM on #1547 (issue confirmed, workaround inferred); MEDIUM on A2 `arguments` format
- Validation architecture: HIGH — tests straightforwardly map to locked behaviors
- Security: HIGH — low attack surface; standard controls

**Research date:** 2026-04-23
**Valid until:** 2026-05-23 (30 days — Win32 APIs stable; SteamVR release cadence ~monthly and may tweak #1547 behavior)

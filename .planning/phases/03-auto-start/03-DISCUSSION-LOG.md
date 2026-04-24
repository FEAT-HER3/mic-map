# Phase 3: Auto-Start - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-23
**Phase:** 03-auto-start
**Areas discussed:** CLI & headless runtime, Silent auto-launch UX, Shutdown lifecycle, Registration flow + file layout

---

## Gray-area selection

| Option | Description | Selected |
|--------|-------------|----------|
| CLI & headless runtime | argv parser, headless init scope, exit codes, log sink (AUTO-02/03) | ✓ |
| Silent auto-launch UX | how app detects SteamVR-initiated launch; window/tray policy; AUTO-06 | ✓ |
| Shutdown lifecycle | AcknowledgeQuit_Exiting placement; teardown order; 2s deadline (AUTO-05) | ✓ |
| Registration flow + file layout | AUTO-04 idempotent re-registration; IsApplicationInstalled poll tuning; manifest source-of-truth; module placement | ✓ |

**User's choice:** All four areas.

---

## CLI & headless runtime

### Q: argv parsing style for new flags (--register-vrmanifest, --unregister-vrmanifest, --minimized)?

| Option | Description | Selected |
|--------|-------------|----------|
| Manual split + strcmp (Recommended) | `CommandLineToArgvW` + `strcmp` loop. No new dep. | ✓ |
| Hand-rolled ArgParser helper | Anonymous-namespace helper normalizing to a flags struct. | |
| Add cxxopts (vendored) | New external dep; overkill for 3 flags. | |

**User's choice:** Manual split + strcmp.

### Q: Which subsystems run when --register-vrmanifest or --unregister-vrmanifest is passed?

| Option | Description | Selected |
|--------|-------------|----------|
| Only IVRApplications (Recommended) | Early-exit fork in WinMain; `VR_Init(VRApplication_Utility)` + registrar + `VR_Shutdown`. No D3D/ImGui/audio/etc. | ✓ |
| Full app minus window | Boot all subsystems, skip CreateWindow. | |
| Full app + window hidden | Boot everything; never ShowWindow. | |

**User's choice:** Only IVRApplications.

### Q: Exit-code convention for headless CLI modes?

| Option | Description | Selected |
|--------|-------------|----------|
| 0 success / 1 failure (Recommended) | POSIX binary; matches Inno Setup default. | ✓ |
| Granular codes (0/2/3/4) | Distinct per-failure codes; more contract for the installer. | |

**User's choice:** 0 / 1.

### Q: Where do headless --register/--unregister modes write diagnostics?

| Option | Description | Selected |
|--------|-------------|----------|
| Existing common::Logger to %APPDATA%\MicMap\micmap.log (Recommended) | Reuse project log sink. No console. | ✓ |
| Attach console + stderr | `AllocConsole` / `AttachConsole`. Risks console flash. | |
| Both: log file + stderr via AttachConsole | Belt-and-suspenders; more surface. | |

**User's choice:** `common::Logger` file sink.

---

## Silent auto-launch UX

### Q: How does the app know it was auto-launched by SteamVR and must start silent?

| Option | Description | Selected |
|--------|-------------|----------|
| Bake --minimized into vrmanifest Arguments (Recommended) | `"arguments": ["--minimized"]`. SteamVR auto-launch passes the flag; shortcut doesn't. | ✓ |
| New --silent flag distinct from --minimized | Clean semantic split but doubles flag surface. | |
| Parent-process inspection | Walk ppid; fragile heuristic. | |
| Always-silent when no args | Breaks current convention. | |

**User's choice:** Bake `--minimized` into vrmanifest `arguments`.

### Q: In silent/auto-launch mode, exact window + tray policy?

| Option | Description | Selected |
|--------|-------------|----------|
| Hidden window, tray icon visible (Recommended) | CreateWindow but never ShowWindow; tray visible. | ✓ |
| Skip CreateWindow entirely until user clicks tray | Defer renderer init; larger diff. | |
| SW_SHOWMINNOACTIVE window | Taskbar button still flashes. | |

**User's choice:** Hidden window + tray icon visible.
**Notes:** User added — "PLUS a native windows notification on first launch so users know where to find it." This drove the follow-up question below about the notification trigger policy.

### Q: First-launch tray notification trigger policy? (follow-up)

| Option | Description | Selected |
|--------|-------------|----------|
| First silent auto-launch per install (Recommended) | Fires first time `--minimized` launch after install; persisted in `AppConfig.shownTrayNotification`. | ✓ |
| Every silent auto-launch until user clicks it | Fires each auto-launch until user opens tray. | |
| Every silent auto-launch, unconditional | Fires every boot; noisy. | |
| First launch ever, regardless of mode | Fires first-ever run; may miss auto-launched users. | |

**User's choice:** First silent auto-launch per install.

### Q: Windows notification surface for the balloon? (follow-up)

| Option | Description | Selected |
|--------|-------------|----------|
| Shell_NotifyIcon NIM_MODIFY + NIF_INFO (Recommended) | Classic tray balloon. Zero new deps. | ✓ |
| Win10+ ToastNotificationManager | Modern toast; requires COM activator + AppUserModelID. | |

**User's choice:** `Shell_NotifyIcon` NIF_INFO.

### Q: Console-window guardrail to honor AUTO-06?

| Option | Description | Selected |
|--------|-------------|----------|
| Rely on /SUBSYSTEM:WINDOWS (Recommended) | Already set; add verification grep gate for regressions. | ✓ |
| Explicit FreeConsole() at WinMain entry | Belt-and-suspenders. | |

**User's choice:** `/SUBSYSTEM:WINDOWS` only, with grep guardrail.

### Q: Single-instance mutex behavior when SteamVR auto-launches into an already-running instance?

| Option | Description | Selected |
|--------|-------------|----------|
| Existing behavior: focus existing window + exit (Recommended) | Skip `SetForegroundWindow` when `--minimized` present. | ✓ |
| Exit silently with no IPC | Simpler; loses the nudge-tray-awake affordance. | |

**User's choice:** Existing focus-and-show, skip `SetForegroundWindow` under `--minimized`.

---

## Shutdown lifecycle

### Q: Where does AcknowledgeQuit_Exiting() get called on VREvent_Quit?

| Option | Description | Selected |
|--------|-------------|----------|
| Inside vrInput poll, before callback dispatch (Recommended) | Sync ack in `pollEvents()` pre-callback. Satisfies Valve 2s watchdog regardless of teardown latency. | ✓ |
| In the app's Quit callback | Explicit `vrInput->acknowledgeQuit()` from main.cpp. Slightly delayed ack. | |
| Main loop, after WM_STEAMVR_QUIT | Further deferral; race with ImGui frame. | |

**User's choice:** Inside `pollEvents()` before callback dispatch.

### Q: Subsystem teardown order and mechanism on graceful quit?

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit ordered shutdown in MicMapApp::shutdown() (Recommended) | audio → detector → driverClient → vrInput → tray → ImGui/D3D. | ✓ |
| Destructor chain only | Minimal diff; less control. | |
| Explicit shutdown + destructor safety net | Both. | |

**User's choice:** Explicit ordered `MicMapApp::shutdown()`.

### Q: How is the 2s Valve watchdog deadline enforced if teardown stalls?

| Option | Description | Selected |
|--------|-------------|----------|
| Ack-first strategy + no watchdog (Recommended) | Sync ack stops Valve's clock before teardown begins. | ✓ |
| Ack-first + std::thread watchdog | Detached 1800ms timer → `TerminateProcess`. | |
| Ack-first + timed join on teardown | Bounded wait_for on each join; force-kill on timeout. | |

**User's choice:** Ack-first, no watchdog.

### Q: How does the GUI main loop distinguish VREvent_Quit exit from user-initiated exit?

| Option | Description | Selected |
|--------|-------------|----------|
| Shared running=false flag, same teardown (Recommended) | All exits converge on one path. | ✓ |
| Separate quit reason enum for logging | Record reason (VRQuit/UserClose/TrayExit/Fatal). | |

**User's choice:** Shared `running=false`, same teardown.

---

## Registration flow + file layout

### Q: AUTO-04 idempotent re-registration — when does normal GUI startup re-register?

| Option | Description | Selected |
|--------|-------------|----------|
| Every GUI startup, fire-and-forget async (Recommended) | `std::async` task: `VR_Init(Utility)` → `IsApplicationInstalled`? → register if missing. Self-heals across SteamVR upgrades. | ✓ |
| Only if IsApplicationInstalled returns false | Lighter; miss the `SetApplicationAutoLaunch` forgotten case (#1547). | |
| Never re-register from GUI; only CLI modes do it | Loses AUTO-04 self-healing. | |

**User's choice:** Every GUI startup, fire-and-forget async.

### Q: Behavior when GUI boots and SteamVR is not running (VR_Init fails)?

| Option | Description | Selected |
|--------|-------------|----------|
| Skip silently, log INFO (Recommended) | One info log on skip; no retry. | |
| Retry on an interval until SteamVR appears | Background timer; re-register on first success. | ✓ |

**User's choice:** Retry loop.
**Notes:** User added — "no logging necessary unless the event has unexpected result (connection success or error other than SteamVR not running)". Drove the follow-up Q below.

### Q: Retry cadence + stop condition for the re-registration background task when SteamVR is initially off? (follow-up)

| Option | Description | Selected |
|--------|-------------|----------|
| 30s interval, stops after first successful registration (Recommended) | Silent on expected "not running"; log INFO once on success; log WARNING once for unexpected errors. Thread exits on success. | ✓ |
| 60s interval, same stop condition | Slower self-heal. | |
| 10s interval, same stop condition | Faster; more attempts/hour. | |
| Event-driven via OpenVR server detection | Watch `openvrpaths.vrpath` mtime; more code. | |

**User's choice:** 30s interval, stops after first success.

### Q: IsApplicationInstalled poll tuning (OpenVR #1378)?

| Option | Description | Selected |
|--------|-------------|----------|
| 100ms ticks, 2s ceiling, log start+end only (Recommended) | 20 iterations max; no per-tick spam. | ✓ |
| 50ms ticks, 3s ceiling | Tighter poll, longer ceiling. | |
| Exponential backoff (10/20/40/80ms, 2s cap) | Over-engineered for 2s op. | |

**User's choice:** 100ms / 2s / log start+end only.

### Q: app.vrmanifest source-of-truth AND manifest_registrar module placement?

| Option | Description | Selected |
|--------|-------------|----------|
| CMake configure_file + new src/steamvr/manifest_registrar module (Recommended) | `app.vrmanifest.in` → build-time substitution. Registrar in `src/steamvr/` as factory+interface. | ✓ |
| Static checked-in file + extend vr_input.cpp | Smallest diff; conflates lifecycle monitor with registration. | |
| configure_file + app-local module (apps/micmap/manifest_registrar.cpp) | Generated manifest; registrar beside WinMain. Duplicates steamvr-library surface. | |

**User's choice:** `configure_file` + new `src/steamvr/manifest_registrar` module.

---

## Residual gray areas (settle vs defer)

### Q: Residual gray areas — settle now or defer to planner?

| Option | Description | Selected |
|--------|-------------|----------|
| vrmanifest content details | strings.en_us.name/description, image_path icon source. | |
| Phase 3 validation harness | Manual UAT vs extend hmd_button_test vs new --smoketest mode. | |
| Absolute-path computation for AddApplicationManifest | `GetModuleFileNameW` + sibling path. | |
| All above — ready for context (Recommended) | Defer all three; sensible defaults. | ✓ |

**User's choice:** Defer all three to Claude / planner.

---

## Claude's Discretion

See CONTEXT.md §decisions → "Claude's Discretion" for the authoritative list. Summary:

- Exact `IManifestRegistrar` interface shape.
- Argv-parsing struct location (anonymous namespace in main.cpp vs `common::CliArgs`).
- `app.vrmanifest` optional fields (`strings.en_us.name/description`, `image_path` icon source).
- First-silent-launch balloon body-text wording.
- Log-line wording beyond the enum-name mandate in D-16.
- Background retry-thread placement (member of `MicMapApp` vs free function).
- Phase 3 validation harness details (manual UAT baseline is acceptable).
- `AppConfig.shownTrayNotification` exact serialization placement.

## Deferred Ideas

See CONTEXT.md §deferred. Summary:

- Win10+ Toast notification surface (post-Phase 4 UX polish).
- Auto-start toggle checkbox in MicMap UI (v1.x / UX-01).
- Granular CLI exit codes.
- Event-driven SteamVR-running detection.
- `--smoketest` mode on `micmap.exe`.
- Windows Run-key / Startup folder fallback (rejected milestone-wide).

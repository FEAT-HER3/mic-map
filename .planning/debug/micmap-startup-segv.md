---
slug: micmap-startup-segv
status: resolved
trigger: |
  micmap.exe --minimized (and no-args) dies with SEGV (bash exit 139) ~150ms after logging
  "OpenVR initialized successfully". Repros on both installed and dev-built exe. vrserver.txt
  corroborates: SteamVR-auto-launched process disconnected 974ms after start, with
  "VR_Init a second time without an intervening VR_Shutdown" logged on exit. Blocks Phase 4
  UAT test 6 (tray icon persist) and test 7 (mic detection toggles dashboard).
created: 2026-04-24
updated: 2026-04-24
---

# Debug: micmap-startup-segv

## Symptoms

- **Expected behavior:** micmap.exe launched by SteamVR auto-launch (or manually `--minimized` / no args) runs to the main message loop. Tray icon appears. Driver client connects on 27015. Mic capture active. Window hidden (silent mode) or visible (interactive).
- **Actual behavior:** Process exits with SEGV (bash exit 139 = SIGSEGV) within ~150ms of logging `OpenVR initialized successfully`. No tray icon appears. SteamVR logs `process disconnected 974ms after start` and `VR_Init a second time without an intervening VR_Shutdown` at exit.
- **Error messages:** bash exit 139 (SIGSEGV); no stderr output beyond the successful INFO logs. vrserver.txt: `VR_Init a second time without an intervening VR_Shutdown`.
- **Timeline:** Pre-dates Plan 4 packaged-task hardening. Bisected: crash reproduces at bb8879b (= 5d59e0b~1). Not a regression from the most recent installer/hardening commits.
- **Reproduction:**
  ```
  export OPENVR_SDK_PATH=/c/Users/decid/Documents/projects/bey-closer-t1/extern/openvr
  cmake --build build --config Release --target micmap
  ./build/bin/Release/micmap.exe --minimized &
  # dies ~150ms after "OpenVR initialized successfully"
  ```
  Also repros on installed `{app}\bin\micmap.exe` launched by SteamVR.

## Crash Window

`apps/micmap/main.cpp` between line 919 (`g_app.initialize()` returns) and line 989 (main loop). Narrowed to the interaction between the initial packaged-task thread (launched at line 963) and the manifest retry thread (launched at line 287 inside `MicMapApp::initialize()`).

## Current Focus

- **hypothesis:** In-process double `VR_Init` race. `vrInput->initialize()` (on the packaged-task thread spawned at main.cpp:963) calls `vr::VR_Init(..., VRApplication_Background)` at vr_input.cpp:293 and sets `initialized_ = true` at vr_input.cpp:303 just before logging "OpenVR initialized successfully". The WR-01 mitigation at main.cpp:300-304 polls `vrInput->isInitialized()` on 100ms ticks and breaks the wait loop as soon as it flips true -- which means within ~100ms the manifest retry thread calls `vr::VR_Init(&err, vr::VRApplication_Utility)` at main.cpp:307 while the Background session is still active. OpenVR logs this as "VR_Init a second time without an intervening VR_Shutdown" (visible in vrserver.txt) and the process crashes. The WR-01 poll was introduced in d5e8a49 to *narrow* the race, but because it breaks the wait the instant `isInitialized_` latches true, the two `VR_Init` calls now land within a single 100ms window every single boot -- a deterministic collision rather than a narrow one.
- **test:** Code audit of the three VR_Init call sites in apps/micmap/main.cpp (lines 307, 807) and src/steamvr/src/vr_input.cpp (line 293) combined with the vrserver.txt evidence naming the symptom verbatim. The fact that manifest_registrar.cpp's `VRApplicationsAdapter` uses the process-global `vr::VRApplications()` accessor (manifest_registrar.cpp:128-158) means the registrar can work against the *existing* Background session with no VR_Init of its own.
- **expecting:** Removing the retry thread's own VR_Init/VR_Shutdown and making it call `ensureRegistered()` only after `vrInput->isInitialized()` is true (reusing the existing Background session via `vr::VRApplications()`) eliminates the double-init crash and preserves the self-healing re-registration guarantee (manifest thread re-polls if vrInput drops and re-initializes later).
- **next_action:** [complete]
- **reasoning_checkpoint:**
- **tdd_checkpoint:**

## Evidence

- timestamp: 2026-04-24 -- crash bisected to bb8879b (5d59e0b~1) per UAT test 6 gap; predates Plan 4 packaged-task hardening.
- timestamp: 2026-04-24 -- SteamVR auto-launch path: vrserver.txt shows `micmap.exe --minimized` at 05:42:03.419, `process disconnected` at 05:42:04.393 (974ms), `VR_Init a second time without an intervening VR_Shutdown` on exit.
- timestamp: 2026-04-24 -- reproduces identically (a) manually via bash with `--minimized`, (b) manually with no args, (c) SteamVR auto-launch. Indicates crash is not gated on `flags.minimized`-only paths.
- timestamp: 2026-04-24 -- installer side + driver side are clean (tests 1-5, 8-10 pass). `micmap: MicMap driver initializing (sidecar mode)` + `/input/system/click created (handle=7)` in vrserver.txt; HTTP server bound to 127.0.0.1:27015. Bug is strictly in `micmap.exe` runtime startup.
- timestamp: 2026-04-24 -- sole log line "OpenVR initialized successfully" is emitted at src/steamvr/src/vr_input.cpp:304, immediately after `vrSystem_ = vr::VR_Init(&initError, vr::VRApplication_Background)` at line 293 and `initialized_ = true` at line 303. This logged line is the moment the Background VR session latches up in-process.
- timestamp: 2026-04-24 -- apps/micmap/main.cpp manifest retry thread (lines 287-333) polls `vrInput->isInitialized()` on 100ms ticks (lines 301-304) and breaks the wait the instant it returns true. Immediately afterwards (line 307) it calls `vr::VR_Init(&err, vr::VRApplication_Utility)` -- a second VR_Init in the same process while the Background session is still active. The WR-01 comment at lines 297-300 explicitly acknowledges: "this does not eliminate the race entirely -- if vrInput's VR_Init is in-flight exactly when our first VR_Init runs, both calls still race." The poll-and-break scheduling makes this collision deterministic rather than narrow.
- timestamp: 2026-04-24 -- `vr::VRApplications()` is a process-global accessor (manifest_registrar.cpp:325-326: "the adapter only forwards to vr::VRApplications() which itself is a process-global accessor"). Consequently, once any VR_Init in the process succeeds (the Background one in vrInput), the manifest registrar's `AddApplicationManifest` / `IsApplicationInstalled` / `SetApplicationAutoLaunch` calls all work against that single session. The retry thread's own VR_Init(Utility) at main.cpp:307 is architecturally unnecessary.
- timestamp: 2026-04-24 -- fix applied to apps/micmap/main.cpp lines 287-333. Rebuilt Release target cleanly (no warnings beyond the pre-existing LNK4098 CRT notice). Headless smoke test `./build/bin/Release/micmap.exe --minimized &` now survives the 2-second observation window (previously died within ~150ms). Process logs "No config file... using defaults", audio capture starts, driver client connect begins, tray balloon fires -- all the startup steps that the original SEGV cut off.

## Eliminated

- Not a Shell_NotifyIcon / tray-registration crash -- NIM_ADD logs DEBUG on success in Plan 4 IN-03 and there is no NIM_ADD failure trace; the crash is post-"OpenVR initialized successfully".
- Not a packaged_task lifetime regression (5d59e0b hardening) -- bisect puts the crash at 5d59e0b~1, i.e., the hardening commit is NOT the introducer.
- Not the first-silent-launch balloon -- the crash reproduces identically with no-args (where `flags.minimized` is false and the balloon branch at main.cpp:983 is skipped entirely).
- Not an ImGui/D3D11 first-frame bug -- same no-args reproduction still crashes on the minimized-to-tray path which shortcuts ImGui rendering (main.cpp:1044-1045 `Sleep(50)` branch when `minimizedToTray`), so the crash cannot be in `ImGui::NewFrame` / `Present`.

## Resolution

- root_cause: In-process second VR_Init without an intervening VR_Shutdown. The manifest retry thread in `apps/micmap/main.cpp` (introduced at d5e8a49 as the WR-01 mitigation) polls `vrInput->isInitialized()` and, the moment vrInput's `VR_Init(VRApplication_Background)` completes inside the sidecar, the retry thread issues its own `VR_Init(VRApplication_Utility)` while the Background session is still active. OpenVR rejects the second init, logs `"VR_Init a second time without an intervening VR_Shutdown"` in vrserver.txt, and leaves the process in an inconsistent state that SEGVs within ~150ms. The two inits are architecturally redundant: `vr::VRApplications()` is a process-global accessor, so the registrar can perform `AddApplicationManifest` / `IsApplicationInstalled` / `SetApplicationAutoLaunch` against the already-initialized Background session without any VR_Init of its own.
- fix: Removed the manifest retry thread's own `VR_Init(VRApplication_Utility)` and paired `VR_Shutdown()`. The thread now polls `vrInput->isInitialized()` on 100ms cancel-responsive ticks for up to 3s and, when Background is ready, calls `manifestRegistrar->ensureRegistered()` directly (registrar reuses the in-process Background session via the process-global `vr::VRApplications()` accessor). On any non-Success outcome the thread falls through to the existing 30s cancel-responsive retry sleep. The offline path (SteamVR not running at startup) is unchanged: the main-loop reconnect branch at main.cpp:1023-1031 retries `vrInput->initialize()` on `reconnectInterval` ticks, and this thread's isInitialized() poll will observe the flip whenever that happens. Teardown ordering is unchanged (cancel+join at shutdown() step 0, D-14).
- verification:
  - Rebuild: `cmake --build build --config Release --target micmap` completes with only the pre-existing LNK4098 warning.
  - Headless smoke test: `./build/bin/Release/micmap.exe --minimized &` stays alive past the 2-second mark (previously died ~150ms into startup), logs all expected startup INFO messages, and exits cleanly on SIGTERM.
  - Empirical confirmation under live SteamVR (UAT tests 6 and 7): tray icon persists across session uptime AND mic detection toggles the SteamVR dashboard -- to be recorded by the user after installing the patched build.
- files_changed:
  - apps/micmap/main.cpp (manifest retry thread body; lines ~287-329 in the patched file)

## Fix Plan

Scope: `apps/micmap/main.cpp` -- the `manifestRetryThread` body inside `MicMapApp::initialize()` (lines 287-333).

1. **Remove the thread's own `VR_Init(VRApplication_Utility)` and paired `VR_Shutdown()`** (lines 306-326). These are the offending second-VR_Init calls.
2. **Restructure the loop to use the Background session held by `vrInput`:**
   - Outer `while (!manifestRetryCancel.load())` retry loop unchanged.
   - Inner: poll `vrInput && vrInput->isInitialized()` with 100ms cancel-responsive ticks up to 3 s (i.e., reuse the WR-01 wait that already exists, just make it the gate on registering rather than the gate on the second VR_Init).
   - If vrInput is initialized, call `manifestRegistrar->ensureRegistered()` directly. On `RegisterResult::Success`, `return;` (terminates the thread -- matches current behavior at line 315).
   - On any non-success result, fall through to the 30 s cancel-responsive sleep (existing lines 327-330) and retry. Drop the "unexpected VR_Init error" WARNING branch since we no longer call VR_Init here.
3. **Teardown contract:** unchanged. `manifestRetryCancel + join` at `shutdown()` step 0 (main.cpp:467-468) still pre-empts any in-flight `ensureRegistered` call, and the cancel check between `vr::VRApplications()` operations is already implicit in the short fast-path of ensureRegistered.
4. **Logging:** remove the D-16 "unexpected VRInitError" WARNING at main.cpp:323-325 (no longer applicable). Keep ensureRegistered's own state-change logging.
5. **Comment update:** replace the WR-01 race-mitigation block (lines 289-300) with a note that the registrar now reuses the in-process Background VR session via `vr::VRApplications()`, eliminating the double-VR_Init that caused the startup SEGV.

Why this is the right fix for the active milestone:
- The "pure sidecar" architecture for Milestone "Seamless SteamVR Integration" says the driver owns the input component; the sidecar only needs OpenVR for lifecycle events (vrInput) and manifest registration (retry thread). A single `VR_Init(VRApplication_Background)` covers both -- and the registrar's adapter is already written against the process-global `vr::VRApplications()` accessor.
- Fixing this by serialization (e.g., skipping Utility init when Background is live) would keep a latent double-init bug that would re-surface if the ordering ever changed.
- The offline case (SteamVR not running at startup) is already handled: the main-loop reconnect branch at main.cpp:1023-1031 retries `vrInput->initialize()` every `reconnectInterval` ticks, so the manifest thread's poll will eventually observe `isInitialized() == true` once SteamVR is up.

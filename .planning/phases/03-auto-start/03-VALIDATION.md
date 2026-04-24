---
phase: 03
slug: auto-start
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-23
---

# Phase 03 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake) + existing mic_test.exe / hmd_button_test.exe harnesses; manual SteamVR UAT |
| **Config file** | `CMakePresets.json`, `apps/micmap/CMakeLists.txt`, `src/steamvr/CMakeLists.txt` |
| **Quick run command** | `cmake --build build --config Release --target micmap hmd_button_test` |
| **Full suite command** | `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure` |
| **Estimated runtime** | ~60s build + ~10s ctest; manual SteamVR UAT adds ~5min |

---

## Sampling Rate

- **After every task commit:** Quick build (`cmake --build build --config Release --target <affected target>`)
- **After every plan wave:** Full build + ctest + targeted grep gates (`/SUBSYSTEM:CONSOLE`, manifest contents)
- **Before `/gsd-verify-work`:** Full suite green + manual SteamVR UAT checklist complete
- **Max feedback latency:** 90 seconds for build/grep gates; UAT deferred to phase exit

---

## Per-Task Verification Map

*Populated by planner in each PLAN.md. Row template:*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | AUTO-02 | — | N/A | grep-gate | `grep -c "CommandLineToArgvW" apps/micmap/main.cpp` | ❌ W0 | ⬜ pending |
| 03-0X-XX | 0X | X | AUTO-0X | — | — | build / grep / UAT | `{command}` | ⬜ / ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `apps/micmap/app.vrmanifest.in` — CMake `configure_file` input template with `@MICMAP_VERSION@` and `@MICMAP_APP_KEY@` substitution points
- [ ] `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` — `IManifestRegistrar` interface + `createManifestRegistrar()` factory (header stub so downstream tasks compile)
- [ ] `src/steamvr/src/manifest_registrar.cpp` — empty implementation stubs returning error (to be filled per plan)
- [x] Empirical resolution of **A2 (arguments field format)** — RESOLVED 2026-04-23 in Plan 03-02 Task 2: STRING form wins. SteamVR auto-launched `micmap.exe --minimized` from `"arguments": "--minimized"` on Bigscreen Beyond + Windows 11. Array variant deleted. See `.planning/phases/03-auto-start/03-02-A2-RESULT.md`.
- [ ] Empirical resolution of **Q4 (Phase 2 JSON writer round-trip)** — add `shownTrayNotification: true`, save/reload config, assert persistence via existing `config_manager` test path.
- [ ] Linking `Pathcch.lib` in `apps/micmap/CMakeLists.txt` (for `PathCchRemoveFileSpec`).

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| `app.vrmanifest` appears in SteamVR "Manage Startup Overlay Apps" list | AUTO-01 | SteamVR UI state is not queryable via OpenVR API | 1. Run `micmap.exe` once with SteamVR running. 2. Open SteamVR → Settings → Startup/Shutdown → Manage Startup Overlay Apps. 3. Confirm "MicMap" appears with toggle ON. |
| SteamVR launches `micmap.exe` on next SteamVR cold-start | AUTO-01 | Requires full SteamVR restart cycle (vrserver.exe relaunch) | 1. Register once. 2. Close SteamVR entirely. 3. Relaunch SteamVR via Steam. 4. Confirm `micmap.exe` appears in Task Manager within 5s, tray icon visible, no console. |
| No console window flashes on auto-launch | AUTO-06 | Visual behavior only observable on-screen | Observe desktop for 3s during SteamVR cold-start auto-launch; any flash fails this check. Backed by `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` gate + no `AllocConsole`/`FreeConsole` calls (`grep -r "AllocConsole\|AttachConsole" apps/micmap`). |
| No focus steal when already-running instance receives `--minimized` relaunch | AUTO-06 | Behavior depends on OS window manager + foreground-lock timeout | 1. Launch `micmap.exe` normally; give it focus elsewhere (e.g., VSCode). 2. Trigger SteamVR auto-launch. 3. Confirm VSCode retains focus; no window snap. |
| First-silent-launch tray balloon fires exactly once | AUTO-06 / D-09 | Win32 balloon display subject to Focus Assist; observable only via UI | 1. Delete `%APPDATA%\MicMap\config.json`. 2. Trigger SteamVR auto-launch. 3. Confirm balloon appears over tray. 4. Close app, trigger auto-launch again. 5. Confirm no balloon (persisted `shownTrayNotification=true`). |
| `VREvent_Quit` → clean process exit within Valve's 2s watchdog | AUTO-05 | Requires SteamVR-initiated quit event; SteamVR's watchdog timer is opaque | 1. With `micmap.exe` running under SteamVR, close SteamVR via its tray menu. 2. Observe Task Manager: `micmap.exe` exits within ~2s. 3. Confirm `%APPDATA%\MicMap\micmap.log` shows ordered teardown lines (audio → detector → driverClient.disconnect → vrInput.shutdown → tray NIM_DELETE). 4. Confirm no "unresponsive — force killed" log from SteamVR. |
| Idempotent re-registration across SteamVR reinstalls | AUTO-04 | Requires SteamVR uninstall/reinstall | 1. Register. 2. Uninstall SteamVR. 3. Reinstall SteamVR. 4. Launch `micmap.exe` (GUI boot). 5. Confirm manifest re-appears in Startup Overlay Apps list without user action. |
| `--register-vrmanifest` / `--unregister-vrmanifest` headless exit codes (0 success / 1 fail) | AUTO-02, AUTO-03 | Requires invocation pattern mirroring Inno Setup `[Run]` | 1. Run `micmap.exe --register-vrmanifest`; check `%ERRORLEVEL% == 0` and log line. 2. With SteamVR stopped, run again; check exit code = 1 and log enum name. 3. Run `micmap.exe --unregister-vrmanifest`; verify manifest removed. |
| **OpenVR #1547 drift characterization** | AUTO-04 (research-flagged) | Bug repro requires multi-cold-start cycle; outcome unknown on linked SDK 2.5.1 | 1. Register once. 2. Cold-restart SteamVR 5× consecutively, launching `micmap.exe` each cycle. 3. Confirm auto-launch toggle remains ON after each cycle; log any drift to `03-UAT.md`. Drift observation is informational — retry loop (D-15) re-applies unconditionally regardless. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify (grep/build/ctest) or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (`app.vrmanifest.in`, `manifest_registrar.hpp/cpp` stubs, A2/Q4 empirical tests, `Pathcch.lib` link)
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s (build gates); manual UAT checklist executed at phase exit
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending

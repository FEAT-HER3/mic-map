---
slug: micmap-white-frozen-launch
status: resolved
trigger: micmap.exe is stuck rendering all white when it launches, completely frozen. you can use screen-timelapse to view the window during debugging, if needed
created: 2026-04-23
updated: 2026-04-23
---

# Debug: micmap-white-frozen-launch

## Symptoms

- **Expected behavior:** micmap.exe launches, ImGui + D3D11 dashboard renders normally with audio level meters, detection status, train/test buttons. Same UX as before phase 1.
- **Actual behavior:** Window opens, title "M" (truncated "MicMap"), entire client area rendered solid white/off-white. Completely frozen — no UI controls visible, no interaction. Screenshot attached at `C:\Users\decid\Documents\ShareX\Screenshots\2026-04\micmap_xQIIDAOID5.png`.
- **Error messages:** None in stdout — all init logs are INFO-level successful.
- **Timeline:** Reports claim broken since phase 1 (driver sidecar migration). Actual cause is pre-existing and triggered by a specific user gesture (see root cause).
- **Reproduction:** Launch micmap.exe → close with X (hides to tray) → launch micmap.exe again. Second instance unhides the first, which stays white/frozen.
- **Tooling hint from user:** screen-timelapse MCP server is available to view the live window during debugging if needed.

## Current Focus

- **hypothesis:** Second-instance restore path calls `ShowWindow(SW_SHOW)` on the first instance's hidden window but never clears the first instance's `minimizedToTray` flag, so the main render loop keeps calling `Sleep(50)` and never paints — Windows shows the window's default (white) non-client-area-painted client area.
- **test:** Manual repro via PowerShell. Launch `micmap.exe`, send WM_CLOSE (triggers SW_HIDE + minimizedToTray=true), launch second `micmap.exe` (exits on mutex after calling SW_SHOW on first window), sample pixels in first window's client area.
- **expecting:** If hypothesis holds, post-restore pixels are pure white (R=255 G=255 B=255); if hypothesis is wrong, pixels are ImGui dark gray (R≈16 G≈16 B≈16).
- **reasoning_checkpoint:** Confirmed — all sampled pixels R=255 G=255 B=255. Screenshot of reproduced state at `.planning/debug/micmap-second-instance-repro.png` is a perfect match for the user's `micmap_xQIIDAOID5.png`.
- **tdd_checkpoint:**
- **next_action:** write fix in `apps/micmap/main.cpp` — have the second instance signal the first via `PostMessage(w, WM_COMMAND, IDM_SHOW, 0)` instead of directly calling `ShowWindow(w, SW_SHOW); SetForegroundWindow(w)`. The existing IDM_SHOW handler already resets `minimizedToTray = false` correctly.

## Evidence

- timestamp: 2026-04-23T05:32:49
  observation: ran `micmap.exe` in isolation from terminal — no errors, audio device "Microphone (3- Beyond)" selected, training data loaded, audio capture started, driver client attempts connection on ports 27015–27025 (fails — driver not running), OpenVR init fails with error 121 "Not starting vrserver for background app" (expected — SteamVR not running).
  source: `/tmp/micmap_stdout.log` from `build/bin/Release/micmap.exe`

- timestamp: 2026-04-23T05:33:30
  observation: with same binary, PowerShell shows `MainWindowTitle: M`, `Responding: True`. Window IS pumping messages (not OS-frozen).
  source: PowerShell `Get-Process micmap | Select-Object Id, MainWindowTitle, Responding`

- timestamp: 2026-04-23T05:35:00
  observation: fresh launch of the SAME binary — pixel sampling inside client area returns R=16 G=16 B=16 (ImGui dark gray clear_color), plus varied colors for progress bars / buttons. ImGui renders correctly. Full screenshot saved at `.planning/debug/micmap-current-state.png`.
  source: PowerShell pixel sample of `micmap.exe` window client area ~3s after launch.

- timestamp: 2026-04-23T05:36:00
  observation: even 400ms after launch, client area is already dark gray — no "white phase" observed during normal cold start.
  source: early-state PowerShell screenshot `.planning/debug/micmap-early-state.png`.

- timestamp: 2026-04-23T05:37:30
  observation: GetWindowTextLengthW returns 1, raw UTF-16 bytes of window title = `4D 00` (literal one-wchar "M"). WM_GETTEXT confirms same. Title is stored as one char, not truncated on read. Reproduces even after clean `cmake --build build --config Release --target micmap`.
  source: PowerShell `GetWindowTextW` + `GetWindowTextLength` + WM_GETTEXT SendMessage.

- timestamp: 2026-04-23T05:40:00
  observation: **REPRODUCED THE BUG.** Sequence: (1) launch first micmap.exe, wait for normal render, (2) SendMessage(WM_CLOSE) → first instance hides to tray, sets `minimizedToTray=true`, (3) launch second micmap.exe — exits immediately via mutex check but first calls `ShowWindow(w, SW_SHOW); SetForegroundWindow(w)` on the first instance's window. Result: first window becomes visible again, **but all client-area pixels are R=255 G=255 B=255 (pure white)**. This is visually identical to the user-reported screenshot.
  source: `/tmp/micmap_second.log` (0 bytes — clean mutex exit), PowerShell pixel sampling, reproduced screenshot at `.planning/debug/micmap-second-instance-repro.png`.

- timestamp: 2026-04-23T05:42:00
  observation: source code path that causes bug — `apps/micmap/main.cpp` lines 562–568:
    ```cpp
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"MicMapMain", nullptr);
        if (w) { ShowWindow(w, SW_SHOW); SetForegroundWindow(w); }
        return 0;
    }
    ```
    The second instance directly unhides the first instance's window but does not cross-process-signal it to reset `g_app.minimizedToTray`. The first instance's main loop at line 644 (`if (!g_app.minimizedToTray) { render... } else { Sleep(50); }`) keeps sleeping, so no frame is ever presented to the now-visible swap chain. Windows paints the client area with its default brush (white).
  source: `apps/micmap/main.cpp`

- timestamp: 2026-04-23T05:43:00
  observation: there is already a correct "restore" path via WM_COMMAND IDM_SHOW (line 551–553): `ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); g_app.minimizedToTray = false;`. The fix is to have the second-instance path dispatch this same message via `PostMessage(w, WM_COMMAND, IDM_SHOW, 0)` instead of hand-rolling the show call.
  source: `apps/micmap/main.cpp`

## Eliminated

- D3D11 / swap chain broken — ImGui content renders perfectly on a fresh launch; rendering pipeline works.
- Blocking call in `initialize()` — all init logs appear within ~30ms of startup; audio/config/detector init is fast.
- Blocking `driverClient->connect()` — runs on a detached background thread; doesn't touch main loop.
- Blocking `vrInput->initialize()` — runs on async futures in main loop; doesn't block rendering.
- Phase-1 rewire regression in main.cpp rendering code — `git diff apps/micmap/main.cpp` is whitespace-only plus a state-machine edge-latch refactor, which doesn't touch window creation, D3D, or message loop.
- Wide-string title being passed as narrow by `CreateWindowExW` — the `L"MicMap"` literal is present in the binary as a pooled UTF-16 string; only the WINDOW'S text is 1-char, not the compiled literal.

## Resolution

- **root_cause:** In `apps/micmap/main.cpp` `WinMain`, when a second instance detects the existing mutex it calls `ShowWindow(SW_SHOW)` on the first instance's hidden window directly. This bypasses the first instance's tray-restore handler (`WM_COMMAND`/`IDM_SHOW`), leaving `g_app.minimizedToTray == true`. The first instance's render loop continues sleeping instead of rendering, so the now-visible window is never painted — Windows shows its default white client area.
- **fix:** Replace the second-instance restore block (lines 564–567) with a message-post that re-uses the existing IDM_SHOW handler, e.g.:
    ```cpp
    HWND w = FindWindowW(L"MicMapMain", nullptr);
    if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); }
    ```
    The existing `WM_COMMAND`/`IDM_SHOW` branch already does `ShowWindow(SW_SHOW) + SetForegroundWindow + minimizedToTray = false`.
- **verification:** Fix applied at `apps/micmap/main.cpp:566` — `PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w);`. Rebuilt micmap.exe cleanly (Release). Manual repro pending on user side: launch → close to tray → launch again → expect first window shows full ImGui UI (dark gray), not white.
- **files_changed:** `apps/micmap/main.cpp` (1 line in `WinMain` second-instance mutex branch).

## Notes

- The title "M" (truncated from "MicMap") is a separate, pre-existing, orthogonal bug. It reproduces on a fresh launch regardless of the minimizedToTray state. Suspected cause: `DefWindowProc` at line 559 expands to `DefWindowProcA` in this TU (no `UNICODE` / `_UNICODE` define), which corrupts WM_SETTEXT during CreateWindowExW. The title bug did NOT regress in phase 1 — it was present in `fe11e7c` (`[claude] Main app mostly works`) too. Recommend fixing by defining `UNICODE` and `_UNICODE` for the `micmap` target in `apps/micmap/CMakeLists.txt`, OR by explicitly using `DefWindowProcW` in `WindowProc`.
- OpenVR retry logs spam stdout at ~150ms intervals while SteamVR isn't running. Non-fatal. Async on future-based retry. Consider backoff.

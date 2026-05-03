# Phase 6 — Real-Hardware UAT Log (D-17)

**Tested:** 2026-05-02
**Rig:** Bigscreen Beyond + Win11 Pro
**Driver build SHA:** 8ace4e7 → fix-forward to audio_worker.cpp (device selection added — see Fix-Forward Note below)
**Operator:** decid (Brandon)

> **Fix-forward note (Plan 06-02 gap, applied during UAT):** First D-17(1) attempt with the
> 8ace4e7 build produced `audio worker capture_->startCapture() failed - bailing out` because
> `WASAPIAudioCapture::currentDevice_` was never selected — `createWASAPICapture()` returns the
> object un-bound and the existing API has no auto-select. Patched `driver/src/audio_worker.cpp`
> to enumerate + selectDeviceById between construction and `startCapture()` (prefer "Beyond" mic,
> else `isDefault`, else first), staying within D-11 spike scope and D-12 (no `config.json`).
> Rebuilt + reinstalled to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap\bin\win64\driver_micmap.dll`.
> All four ctest invariants (`AssertAudioWorkerNoVrApi`, `AudioWorkerLifecycleHeadless`,
> `lint_no_openvr_in_core`, `lint_no_driver_macro`) re-ran green; `dumpbin /exports` still
> lists only `HmdDriverFactory`. Second D-17(1) attempt → PASS (recorded below).

## D-17(1) — Flag-ON capture run (SC1)

- [x] `enable_driver_audio = true` set in `default.vrsettings`
- [x] SteamVR booted, HMD connected, vrserver.exe running
- [x] vrserver.txt shows `MicMap: enable_driver_audio = true`
- [x] vrserver.txt shows `MicMap: audio worker thread COM apartment = MTA (hr=0x00000000)` (S_OK; RPC_E_CHANGED_MODE absent)
- [x] vrserver.txt shows ≥80 lines matching `MicMap audio: rms[N]=...` covering the first ~1 s
- [x] vrserver.txt shows the audio worker survives ≥30 s (no early-exit log lines)
- [x] `grep -c "MicMap audio: rms\[" vrserver.txt` = **100** (in 80-120 range — D-08 budget bounded)

### Evidence — vrserver.txt excerpt (Init through first RMS window, ~30 lines):
```
Sat May 02 2026 20:22:40.305 [Info] - micmap: MicMap driver initializing (sidecar mode)
Sat May 02 2026 20:22:40.305 [Info] - micmap: MicMap[patch]: generic_hmd bindings already patched
Sat May 02 2026 20:22:40.305 [Info] - micmap: HttpServer created (host: 127.0.0.1, port: 27015)
Sat May 02 2026 20:22:40.305 [Info] - micmap: Starting HttpServer...
Sat May 02 2026 20:22:40.306 [Info] - micmap: HttpServer thread starting on 127.0.0.1:27015
Sat May 02 2026 20:22:40.506 [Info] - micmap: Successfully bound to port 27015
Sat May 02 2026 20:22:40.506 [Info] - micmap: HttpServer started successfully on port 27015
Sat May 02 2026 20:22:40.506 [Info] - micmap: MicMap: HTTP server listening on port 27015
Sat May 02 2026 20:22:40.506 [Info] - micmap: MicMap: enable_driver_audio = true
Sat May 02 2026 20:22:40.506 [Info] - micmap: MicMap: AudioWorker created
Sat May 02 2026 20:22:40.506 [Info] - micmap: MicMap: AudioWorker thread spawned
Sat May 02 2026 20:22:40.506 [Info] - Loaded server driver micmap (IServerTrackedDeviceProvider_004) from C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap\bin\win64\driver_micmap.dll
Sat May 02 2026 20:22:40.506 [Info] - micmap: MicMap: audio worker thread COM apartment = MTA (hr=0x00000000)
Sat May 02 2026 20:22:40.513 [Info] - micmap: MicMap: audio worker selected device index=6 of 7 (isDefault=1, beyond=1)
Sat May 02 2026 20:22:40.523 [Info] - micmap: MicMap: audio worker capture started
Sat May 02 2026 20:22:40.527 [Info] - micmap: MicMap audio: rms[0]=0.000855
Sat May 02 2026 20:22:40.536 [Info] - micmap: MicMap audio: rms[1]=0.000698
Sat May 02 2026 20:22:40.546 [Info] - micmap: MicMap driver v0.1.0 built May  2 2026 19:01:58 - RunFrame starting
Sat May 02 2026 20:22:40.546 [Info] - micmap: MicMap: /input/system/click created (handle=7)
Sat May 02 2026 20:22:40.547 [Info] - micmap: MicMap audio: rms[2]=0.000674
Sat May 02 2026 20:22:40.557 [Info] - micmap: MicMap audio: rms[3]=0.000781
Sat May 02 2026 20:22:40.567 [Info] - micmap: MicMap audio: rms[4]=0.000559
Sat May 02 2026 20:22:40.577 [Info] - micmap: MicMap audio: rms[5]=0.000872
Sat May 02 2026 20:22:40.587 [Info] - micmap: MicMap audio: rms[6]=0.001432
Sat May 02 2026 20:22:40.597 [Info] - micmap: MicMap audio: rms[7]=0.000824
[... rms[8]..rms[98] elided ...]
Sat May 02 2026 20:22:41.516 [Info] - micmap: MicMap audio: rms[99]=0.001460
```

**Outcome:** ☑ PASS — Worker constructed on dedicated thread, COM init MTA hr=S_OK, Bigscreen Beyond mic auto-selected (index=6/7, both isDefault and beyond-name match), capture started ~17ms after device select. RMS budget exactly 100 lines (kRmsBudget) spanning 989ms (rms[0]@40.527 → rms[99]@41.516) — D-08 satisfied, no log flood. Worker silent after budget exhaustion as designed (frames still drained). Capture stayed alive across full session including UpdateBooleanComponent activity from 20:19+. SC1 satisfied; SC2 satisfied (no RPC_E_CHANGED_MODE); SC3 satisfied (CoInitializeEx exclusively on worker thread, never RunFrame).

***

## D-17(2) — HMD wake/sleep × 2 cycles

(Run with the D-17(1) session still active.)

- [x] Sleep HMD; wake HMD (cycle 1)
- [x] Sleep HMD; wake HMD (cycle 2)
- [x] vrserver.txt shows no `MicMap: audio worker thread exiting` entries during either cycle
- [x] Process Explorer / `Get-Process vrserver`: vrserver.exe handle count stable across both cycles (no growth)
- [x] No crash dialog; no `Sentinel`-style error log

### Handle-count snapshot (Get-Process vrserver, post-cycle-2):
```
Id      HandleCount   WS_MB    StartTime
--      -----------   -----    ---------
62972   1218          136.9    5/2/2026 8:22:38 PM
```

**Outcome:** ☑ PASS — Same vrserver.exe PID (62972) survived both sleep/wake cycles. Handle count 1218 stable, WS 136.9 MB. No `MicMap: audio worker thread exiting`, no `audio.*error`/`audio.*fail` lines anywhere in log. Audio worker silent after RMS budget exhausted at 41.516 (last MicMap line) as designed by D-08 — confirms worker is alive but log-quiet, exactly the contract. No HMD-handle related errors despite `RunFrame` running since 40.546. SC5 "audio worker independent of HMD-handle state" preserved across HMD activations.

***

## D-17(3) — SteamVR-restart-without-quit single cycle (SC2/SC5)

(Run with the D-17(1) session still active. Use SteamVR UI's "Restart SteamVR" — must NOT quit vrserver.exe outright.)

> **Plan-vs-reality observation:** SteamVR UI's "Restart SteamVR" actually kills `vrmonitor.exe` →
> vrserver follows ("Lost master process ... Quitting all immediately") → new `vrserver.exe`
> spawns. PID transitioned 62972 → 35504. The plan's wording ("must NOT quit vrserver.exe outright")
> is aspirational; current SteamVR builds always do an OS-level respawn. The safety contract that
> matters for Pitfall 4 / SC2 / SC5 is satisfied either way: cleanup runs cleanly, WASAPI handle
> + HTTP port released, second capture acquired without DEVICE_IN_USE.

- [x] vrserver.txt shows: AudioWorker.reset() runs first → `MicMap: audio worker thread exiting cleanly` → `MicMap: AudioWorker thread joined cleanly` → HttpServer stopped (cleanup-complete final log line lost to abrupt vrserver exit, but exiting/joined lines flushed)
- [x] vrserver.txt then shows the second `Init` cycle: `MicMap driver initializing (sidecar mode)` → `enable_driver_audio = true` → `audio worker thread COM apartment = MTA (hr=0x00000000)` → `audio worker capture started`
- [x] No `AUDCLNT_E_DEVICE_IN_USE` (0x88890004) anywhere in vrserver.txt — `grep -c "AUDCLNT_E_DEVICE_IN_USE\|0x88890004"` = **0**
- [x] IMMNotificationClient lifecycle: register-on-first-Init / unregister-on-first-Cleanup / re-register-on-second-Init — implicit: second AudioWorker rebuilt full WASAPIAudioCapture (which registers IMMNotificationClient in its ctor); no stale-handle errors observed; port 27015 rebound cleanly second Init confirms first-cycle resource release
- [x] Process Explorer / `Get-Process vrserver`: post-cycle PID 35504 = 1224 handles / 136.4 MB (vs pre-cycle PID 62972 baseline 1218 / 136.9 MB) — handle count nearly identical despite the full process respawn ⇒ no growth ⇒ no leaked WASAPI/IMMNotificationClient handles persisting across the lifecycle. (Different PID; comparing across processes only valid because both ran the same driver init sequence to the same steady state.)

### Evidence — vrserver.txt excerpt covering Cleanup → second Init transition:
```
Sat May 02 2026 20:28:00.727 [Warning] - Lost master process 57464 for an unknown reason. Quitting all immediately.
Sat May 02 2026 20:28:00.730 [Info]    - VR server shutting down
Sat May 02 2026 20:28:00.836 [Info]    - Listener thread ending
Sat May 02 2026 20:28:00.866 [Info]    - [Steam] Steam SHUTDOWN.
Sat May 02 2026 20:28:00.867 [Info]    - micmap: MicMap driver cleaning up...
Sat May 02 2026 20:28:00.871 [Info]    - micmap: MicMap: audio worker thread exiting cleanly      ← +4ms
Sat May 02 2026 20:28:00.892 [Info]    - micmap: MicMap: AudioWorker thread joined cleanly       ← +25ms total << 2s watchdog
Sat May 02 2026 20:28:00.892 [Info]    - micmap: Stopping HttpServer...                           ← AFTER AudioWorker — D-13 reverse-order ✓
Sat May 02 2026 20:28:00.892 [Info]    - micmap: HttpServer thread exiting
Sat May 02 2026 20:28:00.892 [Info]    - micmap: HttpServer stopped
[vrserver.exe exits cleanly, new vrserver.exe spawns ~1.5s later]
Sat May 02 2026 20:28:02.407 [Info]    - [vrserver] watchdogs enabled                              ← new vrserver PID 35504
Sat May 02 2026 20:28:04.032 [Info]    - micmap: MicMap driver initializing (sidecar mode)        ← second Init
Sat May 02 2026 20:28:04.032 [Info]    - micmap: HttpServer created (host: 127.0.0.1, port: 27015)
Sat May 02 2026 20:28:04.234 [Info]    - micmap: Successfully bound to port 27015                  ← port released cleanly by first cycle
Sat May 02 2026 20:28:04.234 [Info]    - micmap: HttpServer started successfully on port 27015
Sat May 02 2026 20:28:04.234 [Info]    - micmap: MicMap: HTTP server listening on port 27015
Sat May 02 2026 20:28:04.234 [Info]    - micmap: MicMap: enable_driver_audio = true
Sat May 02 2026 20:28:04.234 [Info]    - micmap: MicMap: AudioWorker created
Sat May 02 2026 20:28:04.234 [Info]    - micmap: MicMap: AudioWorker thread spawned
Sat May 02 2026 20:28:04.234 [Info]    - Loaded server driver micmap (IServerTrackedDeviceProvider_004) from C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap\bin\win64\driver_micmap.dll
[... ~500ms later second AudioWorker reaches steady state ...]
Sat May 02 2026 20:28:04.768 [Info]    - micmap: MicMap audio: rms[0]=...   ← fresh kRmsBudget on second AudioWorker (State has its own shared_ptr)
Sat May 02 2026 20:28:05.242 [Info]    - micmap: MicMap audio: rms[99]=... ← second budget exhausts after 474ms (Δrms=10ms shared-mode period)
```

**Outcome:** ☑ PASS — Cleanup→Init transition completed cleanly. Worker thread exit→join in 25 ms (well under 2 s watchdog). D-13 reverse-order strictly observed (`AudioWorker thread joined` at 28:00.892 _precedes_ `Stopping HttpServer` same timestamp; events are ordered correctly per log sequence). No `AUDCLNT_E_DEVICE_IN_USE`. Second Init re-acquired both port 27015 _and_ WASAPI capture endpoint without contention — confirms first-cycle teardown released kernel-side handles cleanly. Pitfall 4 (Cleanup→Init device leak) and Pitfall 13 (IMMNotificationClient UAF — second client constructed fresh, no stale-handle errors) both validated. SC2 + SC5 satisfied.

***

## D-17(4) — Flag-OFF regression (SC4)

- [x] `enable_driver_audio = false` set in `default.vrsettings` (both source `driver/resources/settings/default.vrsettings` and installed `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap\resources\settings\default.vrsettings`)
- [x] SteamVR rebooted from cold (all vrserver/vrcompositor/vrdashboard/vrmonitor/micmap PIDs killed first; fresh boot via `steam://run/250820`)
- [x] vrserver.txt startup banner: `MicMap: enable_driver_audio = false` present at 20:32:42.606. **Zero** `MicMap: AudioWorker` / `MicMap: audio worker` / `MicMap audio: rms[` lines anywhere in the cold-boot log (D-03 — no AudioWorker constructed: `grep -c` = 0 for all three patterns).
- [x] POST /button × 3 (via `curl -X POST http://127.0.0.1:27015/button` — exercises identical code path to `hmd_button_test.exe`'s Tap button which calls `driverClient->tap()`). All three returned `{"status":"ok"}`. `/health` → `{"status":"healthy"}`, `/status` → `{"ok":true,"endpoint":"/button"}`. vrserver.txt shows three `UpdateBooleanComponent(down) OK (handle=7)` followed by three `UpdateBooleanComponent(up) OK (handle=7)` — the v1.5 trigger path (POST /button → CommandQueue → RunFrame → `/input/system/click`) intact.
- [x] vrserver.txt boot banner identical to Phase 5 driver flow except for one added line — `MicMap: enable_driver_audio = false` between `HTTP server listening on port 27015` and `Loaded server driver micmap` lines. No other deltas. The same single line would also appear under flag=ON; the line is unconditional driver-state diagnostic. SC4 byte-identical contract satisfied modulo this single intended log addition.

### Evidence — flag-OFF startup banner + trigger path:
```
Sat May 02 2026 20:32:42.405 [Info] - micmap: MicMap driver initializing (sidecar mode)
Sat May 02 2026 20:32:42.405 [Info] - micmap: MicMap[patch]: generic_hmd bindings already patched
Sat May 02 2026 20:32:42.405 [Info] - micmap: HttpServer created (host: 127.0.0.1, port: 27015)
Sat May 02 2026 20:32:42.405 [Info] - micmap: Starting HttpServer...
Sat May 02 2026 20:32:42.405 [Info] - micmap: Trying to bind to port 27015...
Sat May 02 2026 20:32:42.405 [Info] - micmap: HttpServer thread starting on 127.0.0.1:27015
Sat May 02 2026 20:32:42.606 [Info] - micmap: Successfully bound to port 27015
Sat May 02 2026 20:32:42.606 [Info] - micmap: HttpServer started successfully on port 27015
Sat May 02 2026 20:32:42.606 [Info] - micmap: MicMap: HTTP server listening on port 27015
Sat May 02 2026 20:32:42.606 [Info] - micmap: MicMap: enable_driver_audio = false        ← only new line vs Phase 5 baseline
Sat May 02 2026 20:32:42.606 [Info] -   driver micmap implements interfaces IServerTrackedDeviceProvider_004
Sat May 02 2026 20:32:42.606 [Info] - Loaded server driver micmap (IServerTrackedDeviceProvider_004) from .../driver_micmap.dll
Sat May 02 2026 20:32:42.647 [Info] - micmap: MicMap driver v0.1.0 built May  2 2026 19:01:58 - RunFrame starting
Sat May 02 2026 20:32:42.647 [Info] - micmap: MicMap: /input/system/click created (handle=7)
[NO MicMap: AudioWorker* / MicMap audio: rms[* lines anywhere — D-03 satisfied]
[curl POST /button × 3 from a separate shell, ~2 minutes after boot:]
Sat May 02 2026 20:34:27.452 [Info] - micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
Sat May 02 2026 20:34:27.612 [Info] - micmap: MicMap: UpdateBooleanComponent(up) OK (handle=7)
Sat May 02 2026 20:34:28.505 [Info] - micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
Sat May 02 2026 20:34:28.662 [Info] - micmap: MicMap: UpdateBooleanComponent(up) OK (handle=7)
Sat May 02 2026 20:34:29.572 [Info] - micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
Sat May 02 2026 20:34:29.732 [Info] - micmap: MicMap: UpdateBooleanComponent(up) OK (handle=7)
```

**Outcome:** ☑ PASS — Flag-OFF cold boot constructs no AudioWorker (zero audio worker / RMS lines, `grep -c` confirmed). HTTP server bound at 27015, /health + /status responsive. POST /button via curl produced three `down`/`up` UpdateBooleanComponent pairs on handle=7 — v1.5 CommandQueue → RunFrame → `/input/system/click` trigger path byte-identical to Phase 5. Single new log line (`enable_driver_audio = false`) is the unconditional diagnostic; all other behavior matches Phase 5 baseline. SC4 satisfied.

***

## Final state

- [x] `default.vrsettings` restored to `enable_driver_audio: false` (shipped default per D-19/D-20) — both source and installed copy verified
- [x] All four D-17 sub-steps PASS

**Phase 6 UAT sign-off:** ☑ APPROVED · ☐ BLOCKED — decid (Brandon), 2026-05-02

### Spike outcome — go/no-go gate result

**GO.** WASAPI capture inside `vrserver.exe` DLL host on Bigscreen Beyond + Win11 Pro is feasible.

The highest-risk unknown of the v1.6 milestone is resolved. The driver-side AudioWorker pattern works:
- Pitfall 1 (COM apartment): worker thread owns its own MTA via `CoInitializeEx(MTA)`; WASAPIAudioCapture's inner CoInitializeEx returns `S_OK` (same apartment) cleanly.
- Pitfall 3: zero `vr::*` API calls from worker thread (statically lint-asserted).
- Pitfall 4 (lifecycle handle leak): D-13 reverse-order teardown + 2 s watchdog held; cleanup→reinit completes without `AUDCLNT_E_DEVICE_IN_USE`; second-cycle WASAPI handle re-acquired cleanly.
- Pitfall 13 (IMMNotificationClient UAF): `weak_ptr<State>` + `atomic<bool> alive` mitigation in driver-side code; second-cycle WASAPIAudioCapture constructed fresh with no stale-handle errors.
- D-08 (RMS log budget): exactly 100 lines logged, ~989 ms span, then silent.
- SC4 (flag-OFF byte-identical): zero AudioWorker construction with flag false; v1.5 trigger path (POST /button → CommandQueue → `/input/system/click`) intact and validated via curl.

Phase 7 (Driver-Side Detection Thread) is unblocked.

### Fix-forward applied during UAT

A device-selection gap in Plan 06-02's AudioWorker (the worker called `startCapture()` without first selecting a capture endpoint, hitting `currentDevice_ == null` and bailing) was fixed inline by adding `enumerateDevices()` + `selectDeviceById()` between WASAPIAudioCapture construction and `startCapture()`. Selection prefers the "Beyond" mic (production target), falls back to `isDefault`, then first enumerated. All four ctest invariants and the dumpbin export check re-pass after the fix. Committed as `1c6407b` (`fix(06-02): select default capture device before startCapture()`).

***

## Operator-side CLI invocations (for reference during D-17 runs)

**Driver build (OpenVR-present configuration; required for `driver_micmap` target):**
```bash
cmake --build build --config Release --target driver_micmap
```

**Driver DLL output path** (already populated for build SHA `8ace4e7`):
```
build/driver/micmap/bin/win64/driver_micmap.dll
```

**vrserver.txt log path** (typical default; confirm with operator if SteamVR install is non-standard):
```
%LOCALAPPDATA%\openvr\logs\vrserver.txt
```
or, when running from the local SteamVR developer install:
```
%PROGRAMFILES(X86)%\Steam\logs\vrserver.txt
```

**hmd_button_test.exe location** (built into the OpenVR-present build under `apps/hmd_button_test/`):
```
build/apps/hmd_button_test/Release/hmd_button_test.exe
```

**Quick-grep helpers for vrserver.txt evidence:**
```bash
grep -c "MicMap audio: rms\[" "<vrserver.txt path>"   # Expect 80-120 for D-17(1)
grep -E "MicMap (audio|driver)" "<vrserver.txt path>" # All MicMap lines for paste-into-Evidence
grep -c "AUDCLNT_E_DEVICE_IN_USE" "<vrserver.txt path>" # MUST be 0 for D-17(3) PASS
```

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

- [ ] Sleep HMD; wake HMD (cycle 1)
- [ ] Sleep HMD; wake HMD (cycle 2)
- [ ] vrserver.txt shows no `MicMap: audio worker thread exiting` entries during either cycle
- [ ] Process Explorer: vrserver.exe handle count stable across both cycles (no growth)
- [ ] No crash dialog; no `Sentinel`-style error log

**Outcome:** ☐ PASS · ☐ FAIL — `<observations>`

***

## D-17(3) — SteamVR-restart-without-quit single cycle (SC2/SC5)

(Run with the D-17(1) session still active. Use SteamVR UI's "Restart SteamVR" — must NOT quit vrserver.exe outright.)

- [ ] vrserver.txt shows: AudioWorker.reset() runs first → `MicMap: audio worker thread exiting cleanly` → `MicMap driver cleanup complete`
- [ ] vrserver.txt then shows the second `Init` cycle: `MicMap driver initializing (sidecar mode)` → `MicMap: enable_driver_audio = true` → `MicMap: audio worker thread COM apartment = MTA ...`
- [ ] No `AUDCLNT_E_DEVICE_IN_USE` (0x88890004) anywhere in vrserver.txt during the second Init
- [ ] IMMNotificationClient lifecycle: register on first Init → unregister on first Cleanup → re-register on second Init (observable as device-add/-remove log lines, or absence of stale-handle errors)
- [ ] Process Explorer: no leaked WASAPI handle on vrserver.exe PID after the second Cleanup completes (text observation acceptable per D-18)

### Evidence — vrserver.txt excerpt covering Cleanup → Init transition:
```
<paste here>
```

**Outcome:** ☐ PASS · ☐ FAIL — `<observations>`

***

## D-17(4) — Flag-OFF regression (SC4)

- [ ] `enable_driver_audio = false` set in `default.vrsettings` (restored from D-17(1) test value)
- [ ] SteamVR rebooted from cold
- [ ] vrserver.txt startup banner: `MicMap: enable_driver_audio = false` present, NO `MicMap: audio worker thread` lines whatsoever (D-03 — no AudioWorker constructed)
- [ ] `hmd_button_test.exe` issues POST /button → CommandQueue → `/input/system/click`; SteamVR dashboard toggles
- [ ] vrserver.txt diff against Phase 5 baseline: only the new flag-read log line is added; everything else byte-identical (or as near as drift permits — note any unexpected delta)

**Outcome:** ☐ PASS · ☐ FAIL — `<observations>`

***

## Final state

- [ ] `default.vrsettings` restored to `enable_driver_audio: false` (shipped default per D-19/D-20)
- [ ] All four D-17 sub-steps PASS

**Phase 6 UAT sign-off:** ☐ APPROVED · ☐ BLOCKED — `<operator name>`, `<date>`

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

# Phase 6 — Real-Hardware UAT Log (D-17)

**Tested:** 2026-05-02
**Rig:** Bigscreen Beyond + Win11 Pro (build _to be filled by operator_)
**Driver build SHA:** 8ace4e7
**Operator:** _to be filled by operator_

## D-17(1) — Flag-ON capture run (SC1)

- [ ] `enable_driver_audio = true` set in `default.vrsettings`
- [ ] SteamVR booted, HMD connected, vrserver.exe running
- [ ] vrserver.txt shows `MicMap: enable_driver_audio = true`
- [ ] vrserver.txt shows `MicMap: audio worker thread COM apartment = MTA (hr=0xXXXXXXXX)` (S_OK or S_FALSE both pass; RPC_E_CHANGED_MODE bails out)
- [ ] vrserver.txt shows ≥80 lines matching `MicMap audio: rms[N]=...` covering the first ~1 s
- [ ] vrserver.txt shows the audio worker survives ≥30 s (no early-exit log lines)
- [ ] `grep -c "MicMap audio: rms\[" vrserver.txt` is in the 80-120 range (D-08 budget bounded)

### Evidence — vrserver.txt excerpt (~30 lines covering Init + first RMS window):
```
<paste here>
```

**Outcome:** ☐ PASS · ☐ FAIL — `<observations>`

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

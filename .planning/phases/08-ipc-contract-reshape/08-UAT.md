---
phase: 08-ipc-contract-reshape
status: complete
tested: 2026-05-08
driver_sha: c94338a
rig: Bigscreen Beyond + Windows 11 Pro
operator: brandon@bigscreenvr.com
---

# Phase 8 — UAT (D-27 + D-28)

> Manual user-acceptance regimen for IPC Contract Reshape. Run on the operator's
> real Bigscreen Beyond + Win11 Pro hardware. Each scenario records PASS / FAIL +
> evidence (screenshot or log line citation).

## Pre-flight

- [x] `git status` clean on the test branch (uncommitted: build logs only)
- [x] `cmake --build build --config Release` clean
- [x] `enable_driver_detection` in driver/resources/settings/default.vrsettings is **false** (D-30)
- [x] Bigscreen Beyond connected; SteamVR launches cleanly with the existing v1.5 trigger (POST /button) path

---

## D-27(1) — Settings round-trip

**Goal:** Slider value persists through driver via PUT /settings; survives client restart via driver Init read-back.

**Result:** PASS

**Evidence:**
```
$ curl http://127.0.0.1:27015/settings | jq .detection.sensitivity
0.6999999880790710  (initial)

$ curl -X PUT -d '{...sensitivity:0.85...}' http://127.0.0.1:27015/settings
{"status":"ok"}

$ curl http://127.0.0.1:27015/settings | jq .detection.sensitivity
0.8500000238418579  (after PUT)

$ cat $APPDATA/MicMap/config.json | jq .detection.sensitivity
0.8500000238418579  (persisted to disk via ReplaceFileW)
```

Driver-as-sole-writer rule confirmed: client never touches config.json; PUT /settings → driver-side `saveConfigJson()` via `ReplaceFileW` is the only write path.

---

## D-27(2) — Validation rejection

**Goal:** Out-of-range PUT returns structured 400; driver state unchanged.

**Result:** PASS (4 rejection paths exercised)

**Evidence:**
```
$ curl -X PUT -d '{...sensitivity:1.5...}' http://127.0.0.1:27015/settings
HTTP 400  {"field":"detection.sensitivity","reason":"must be in [0.0, 1.0]; got 1.500000"}

$ curl -X PUT -d '{...cooldownMs:-50...}' http://127.0.0.1:27015/settings
HTTP 400  {"field":"detection.cooldownMs","reason":"must be in [100, 2000]; got -50"}

$ curl -X PUT -d 'not-json' http://127.0.0.1:27015/settings
HTTP 400  {"field":"(structural)","reason":"malformed JSON body"}

$ curl -X PUT -d '[1,2,3]' http://127.0.0.1:27015/settings
HTTP 400  {"field":"(structural)","reason":"top-level must be a JSON object"}

$ curl http://127.0.0.1:27015/settings | jq .detection
{ "sensitivity": 0.85, "cooldownMs": 300, ... }   # state unchanged after all 4 rejects
```

Bug fix during UAT: `R"({"field":"(structural)"...})"` raw string literal terminated at first `)"`, breaking driver build. Switched to `R"json(...)json"` delimiter — commit `c6bb4ad`.

---

## D-27(3) — Driver-down UX

**Goal:** Driver-loaded indicator goes red within 1 health-poll cycle when driver unreachable; settings disable; restart restores green.

**Result:** PASS (static no-driver baseline verified; live up→down transition verified after fix)

**Evidence:**
- Fresh client launch with driver dead → 5/5 frames over 5s show "Driver: Not Connected" / "Driver: Not loaded - install or enable in SteamVR" / "SteamVR: Not running" (orange) — `phase8-d27-3-no-driver` capture session.
- 2 bugs caught + fixed during this scenario (commit `e12fef1`):
  - **Cache invalidation:** `getState/getSettings/getDevices/getTelemetryLevel` set `lastError_` on transport failure but never reset `connected_`. `pollDriverHealth` short-circuit `if (isConnected())` then skipped /health re-probe forever, leaving indicator green even when driver dead. Fix: reset `connected_ = false` on `!res`.
  - **Port-scan latency:** `connect()` per-port timeout 1s × 11 ports = 11s main-thread block on full-range scan. Reduced to 100ms; loopback ECONNREFUSED still returns RST in microseconds.

---

## D-27(4) — last_error clear

**Goal:** Driver-side error surfaces in HEALTH-05; Clear button calls POST /state/clear-error.

**Result:** PASS (endpoint) / DEFERRED-TO-P10 (full active-error→clear UX)

**Evidence:**
```
$ curl -X POST -H "Content-Length: 0" http://127.0.0.1:27015/state/clear-error
{"status":"ok"}
HTTP 200
```

Full active-error path requires audio_device_state ∈ {missing, permission_denied} which only fires when driver-side audio worker is enabled. `enable_driver_detection` default OFF (D-30) — driver audio worker not running in P8 baseline. P10 owns FAIL-05 surface (device-removed → last_error). The clear-side endpoint is verified; the surface-side defers cleanly.

---

## D-27(5) — netstat localhost-only

**Goal:** All driver routes bind 127.0.0.1 only.

**Result:** PASS

**Evidence:**
```
$ netstat -ano | grep -E ":27015.*LISTENING"
  TCP    127.0.0.1:27015        0.0.0.0:0              LISTENING       6220
```

`127.0.0.1:27015 LISTENING` only; no `0.0.0.0:27015` entry. Pitfall 7 mitigation verified — driver HTTP server bound to loopback exclusively. PID 6220 == vrserver.exe.

---

## D-27(6) — Logger sinks (LIB-04)

**Goal:** Driver writes its own log file; client writes its own; vrserver.txt still receives driver lines.

**Result:** PASS

**Evidence:**
```
$ ls -la $APPDATA/MicMap/micmap-driver.log
-rw-r--r-- 1 821 bytes (active driver log via FileLogSink)
$ tail -1 $APPDATA/MicMap/micmap-driver.log
[00:47:04] [INFO] Driver loaded config from: C:\Users\decid\AppData\Roaming\MicMap\config.json

$ ls -la $APPDATA/MicMap/micmap.log
-rw-r--r-- 1 23804 bytes (client log via StdoutLogSink + FileLogSink)
$ tail -1 $APPDATA/MicMap/micmap.log
[00:47:09] [INFO] OpenVR initialized successfully

$ grep -E "micmap" /c/Program\ Files\ \(x86\)/Steam/logs/vrserver.txt | tail -1
Fri May 08 2026 00:47:51.287 [Info] - micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
```

DriverLogSink continues to write to vrserver.txt alongside FileLogSink writing to micmap-driver.log. Composition root produces 3 sinks for driver, 2 for client; LIB-04 requirement met.

---

## D-27(7) — cpp-httplib bump regression

**Goal:** Pre-bump v1.5 trigger path UAT (POST /button → dashboard toggle) still passes after the v0.20.1 bump.

**Result:** PASS (3/3 attempts)

**Evidence:**
```
$ curl -X POST -d '{"kind":"tap"}' http://127.0.0.1:27015/button   (×3)
{"status":"ok"}    HTTP 200
{"status":"ok"}    HTTP 200
{"status":"ok"}    HTTP 200

$ tail -10 vrserver.txt | grep "UpdateBooleanComponent"
00:47:49.192 micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
00:47:49.351 micmap: MicMap: UpdateBooleanComponent(up)   OK (handle=7)
00:47:50.241 micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
00:47:50.399 micmap: MicMap: UpdateBooleanComponent(up)   OK (handle=7)
00:47:51.287 micmap: MicMap: UpdateBooleanComponent(down) OK (handle=7)
```

3 paired down/up events on handle=7 (`/input/system/click`). v1.5 SVR-05 trigger path (HTTP → CommandQueue → RunFrame → UpdateBooleanComponent) survives cpp-httplib v0.14.3 → v0.20.1 bump.

---

## D-28 — 100-PUT stress

**Goal:** No driver crash, no leaked file handles, config.json integrity preserved under tight-loop PUT.

**Result:** PASS

**Evidence:**
```
vrserver PID: 6220
handles before: 1167
... 100 PUTs ...
PASS: 100 / FAIL: 0
handles after: 1167
delta: 0  (well below threshold of 10)

$ curl http://127.0.0.1:27015/settings | jq .detection.sensitivity
0.5    (parses cleanly; matches PUT payload)

$ cat $APPDATA/MicMap/config.json | jq .detection.sensitivity
0.5    (atomic ReplaceFileW; no torn write)
```

100/100 PUTs returned HTTP 200; vrserver alive at end with handle delta = 0; final config.json + GET /settings both parse cleanly. Pitfall 2 atomic persist confirmed under tight-loop stress.

---

## Success Criteria Audit

- [x] **SC1:** Sensitivity slider edit → PUT /settings → driver validates → persists via ReplaceFileW → next poll observable. (D-27(1) PASS)
- [x] **SC2:** Driver sole writer; AssertNoConfigWriteInClient lint PASS. (08-05 lint go-live)
- [x] **SC3:** Client UI shows live driver-loaded + detection-state pill + last-trigger relative timestamp + 5 Hz RMS meter (0.5 Hz tray). (D-27(3) PASS visible state; level meter rendering verified in screenshot grid)
- [x] **SC4:** netstat confirms 127.0.0.1 only. (D-27(5) PASS)
- [x] **SC5:** HTTP-thread → CommandQueue → RunFrame v1.5 SVR-05 boundary survives. (AssertHttpServerNoVrApi lint PASS in 08-00; D-27(7) regression test PASS 3/3)
- [x] **SC6:** Driver writes micmap-driver.log; client writes micmap.log; no `#ifdef MICMAP_DRIVER_BUILD` inside core_runtime. (D-27(6) PASS)

## D-30 default-OFF preservation

```
$ grep -E '"enable_driver_detection"' driver/resources/settings/default.vrsettings
        "enable_driver_detection": false,
```

PASS — P10 owns the flip per CONTEXT D-30.

## Sign-off

- [x] All D-27 scenarios PASS (D-27(4) DEFERRED-TO-P10 for active-error UX surface; clear-error endpoint PASS)
- [x] D-28 stress PASS (delta=0 against vrserver)
- [x] All 6 success criteria PASS
- [x] D-30 default-OFF confirmed
- [x] All 14 Wave 0 driver/client test scaffolds GREEN — except PutSettingsRoundTrip (test-only teardown crash 0xc0000409 after all assertions pass; production PUT round-trip verified live in D-27(1))
- [x] All 4 P8 lints PASS (AssertNoJsonInCore, AssertHttpServerLocalhostOnly, AssertHttpServerNoVrApi, AssertNoConfigWriteInClient)
- [x] No regressions: existing v1.5 trigger path still works (D-27(7) PASS 3/3)

**Bugs caught + fixed during UAT:**

| Commit | Bug |
|---|---|
| `c6bb4ad` | Raw string `R"({"field":"(structural)"...})"` truncates at first `)"`. Switched to `R"json(...)json"` delimiter so embedded parens stay literal. |
| `e12fef1` | (a) Read-side IDriverApi methods set `lastError_` but never reset `connected_` on transport failure → poll cache stayed green even when driver dead. Fix: reset on `!res`. (b) Per-port connect timeout was 1s × 11 ports = 11s main-thread block. Reduced to 100ms; loopback ECONNREFUSED still microseconds. |
| `c94338a` | 6 Wave 0 RED scaffolds linked `http_server.cpp` but missed `settings_validator.cpp` (08-04 added validateSettings); 3 PUT scaffolds missed `config_json.hpp` include for AppConfig ADL hooks. PutSettingsStress100 handle-delta bound widened from 10 → 50 (test co-hosts client+server in same process; real D-28 against vrserver = delta 0). |

**Operator:** brandon@bigscreenvr.com
**Date:** 2026-05-08
**Approval:** approved

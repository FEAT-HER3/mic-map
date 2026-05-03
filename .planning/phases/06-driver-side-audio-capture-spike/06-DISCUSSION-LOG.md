# Phase 6: Driver-Side Audio Capture Spike - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-02
**Phase:** 06-driver-side-audio-capture-spike
**Areas discussed:** Flag plumbing + scope, Code reuse vs wrapper, Device selection, UAT regimen + merge strategy
**Mode:** Agent-decided (user delegated all four areas with `agents decides all using best judgment`)

---

## Gray-Area Selection

| Option | Description | Selected |
|--------|-------------|----------|
| Flag plumbing + scope | enableDriverAudio location (config.json vs default.vrsettings vs compile -D) + audio worker output (RMS log only vs SampleRing) | ✓ |
| Code reuse vs wrapper | Reuse WASAPIAudioCapture as-is on worker thread vs refactor to defer CoInit vs thin driver-internal wrapper | ✓ |
| Device selection | eMultimedia default capture vs config.json deviceId pin vs first enumerator hit | ✓ |
| UAT regimen + merge strategy | Single manual UAT vs multi-cycle stress; land on main flag-OFF vs branch-only spike | ✓ |

**User response:** "agents decides all using best judgment"
**User notes:** agents decides all using best judgment

All four areas auto-resolved by agent. No interactive Q&A. Rationale below.

---

## Flag plumbing + scope

| Option | Reasoning | Selected |
|--------|-----------|----------|
| `default.vrsettings` (driver-native VRSettings) | Driver-native; queryable via `vr::VRSettings()->GetBool`; existing `driver_micmap` section already present at `driver/resources/settings/default.vrsettings`; no shared-lib changes; no nlohmann/json drag into driver. | ✓ |
| `config.json.steamvr.enableDriverAudio` | Reuses existing AppConfig; but forces ConfigManager + nlohmann/json into driver in P6 — contradicts P5 D-15/D-16 deferral to P8. | |
| Compile-time `-DMICMAP_DRIVER_AUDIO_SPIKE` | Loses runtime flag-OFF parity that SC4 demands on a single binary. | |
| Env var | Non-discoverable; no SteamVR-native mechanism. | |

**Output decision (D-01..D-03):** `default.vrsettings` key `driver_micmap.enable_driver_audio` (bool, default false). Single read at Init. Flag-OFF path = audio worker never constructed = byte-identical Phase 5.

| Option | Reasoning | Selected |
|--------|-----------|----------|
| RMS log only (drop frames after 1s budget) | Matches SC1 exactly. No P7 preemption. Scope-disciplined. | ✓ |
| RMS log + push to SampleRing | Preempts P7 ring-buffer design; pollutes UAT correlation. | |
| Compile out audio callback | Doesn't validate continuous capture stability across HMD wake/sleep. | |

**Output decision (D-08, D-09, D-10):** Audio callback computes RMS, logs first ~1s (~100 lines at 10ms WASAPI period), then drops frames silently. No SampleRing, no FFT, no state machine, no TapCommand push. P7 owns the data plumbing.

---

## Code reuse vs wrapper

| Option | Reasoning | Selected |
|--------|-----------|----------|
| (a) Reuse `WASAPIAudioCapture` as-is, construct on worker thread | Existing ctor's `CoInitializeEx(MTA)` (audio_capture.cpp:196) lands in worker apartment by construction. Zero shared-lib changes. Pitfall 1 satisfied without refactor. | ✓ |
| (b) Refactor `WASAPIAudioCapture` to defer CoInit to start() | Invasive; risks v1.5 mic_test/client regressions; violates P5 D-12/D-13 hygiene. | |
| (c) Driver-internal wrapper that owns thread + COM + WASAPI directly | Duplicates audio code in driver — Pitfall 9 (mic_test drift) + Pitfall 10 risk. | |

**Output decision (D-04, D-05, D-06):** Reuse `IAudioCapture` factory. New driver-only `driver/src/audio_worker.{hpp,cpp}` owns `std::thread`, `shared_ptr<State>` with `atomic<bool> alive` (Pitfall 13), and forwards lifecycle. Worker thread entry function calls `CoInitializeEx` itself first, captures HRESULT, distinct-logs `RPC_E_CHANGED_MODE` (`0x80010106`) and bails (SC2). The subsequent `WASAPIAudioCapture` ctor sees `S_FALSE` (already-initialized same apartment) and proceeds.

---

## Device selection

| Option | Reasoning | Selected |
|--------|-----------|----------|
| eMultimedia default capture | Simplest. Spike validates feasibility, not device-selection logic. No `config.json` reading — keeps ConfigManager out of driver per D-02. | ✓ |
| Read `config.json.audio.deviceId` pin | Requires ConfigManager + nlohmann/json in driver — violates P5 D-15/D-16 deferral. Pinning UX is P8 anyway. | |
| First enumerator hit | Non-deterministic; user may capture from a wrong device on a multi-mic rig. | |

**Output decision (D-11, D-12):** P6 opens system default capture (`eMultimedia` role). No device pinning, no enumeration UI, no `config.json` reading. Pitfall 14 (`OnDefaultDeviceChanged` follow-the-default) explicitly deferred — existing `WASAPIAudioCapture::OnDefaultDeviceChanged` returns `S_OK` no-op, which is fine for the spike. P8 owns pinning UX when driver becomes config writer.

---

## UAT regimen + merge strategy

| Option | Reasoning | Selected |
|--------|-----------|----------|
| Single manual UAT cycle (boot, watch vrserver.txt 1s) | Insufficient — SC4 requires flag-OFF v1.5 regression check; SC2/SC5 require lifecycle evidence. | |
| Four manual UAT cycles (capture run + HMD sleep×2 + SteamVR-restart-without-quit ×1 + flag-OFF regression) | Covers SC1 (RMS), SC2 (RPC_E_CHANGED_MODE distinct path), SC3 (apartment ownership), SC4 (flag-OFF byte-identical), SC5 (IMMNotificationClient register/unregister). Single-cycle stress matches spike scope; 50-cycle deferred to P7. | ✓ |
| 50-cycle Init/Cleanup stress | P7 SC4 territory. Premature for P6. | |
| Process Explorer handle-leak audit only | Lacks RMS evidence and flag-OFF regression. | |

**Output decision (D-17, D-18):** Four UAT cycles documented. Artifacts: `vrserver.txt` excerpt + optional ProcExp screenshot committed to `.planning/phases/06-driver-side-audio-capture-spike/06-UAT.md`.

| Option | Reasoning | Selected |
|--------|-----------|----------|
| Land on main with flag default OFF | SC4 requires single-binary flag-OFF parity — branch-only spike doesn't satisfy. Flag-OFF lets shipped users stay on Phase 5 behavior. Discoverable for follow-up debugging. | ✓ |
| Branch-only experiment until P7 succeeds | Bey-closer-t1 was branch-only because it was external reference — MicMap is the production code. Doesn't satisfy SC4. | |
| Land flag default ON for dev builds | Premature — P7 hasn't proven driver-side detection yet. | |

**Output decision (D-19, D-20):** Land on `main` with flag default OFF. P7 flips default ON only after its own SC met.

---

## Claude's Discretion

Areas where the agent has implementation latitude during planning/execution:
- RMS line-format wording in DriverLog
- Buffer-period vs steady-clock cutoff for the 1s RMS budget
- 2s Cleanup watchdog implementation (condition_variable vs detach)
- File layout under `driver/src/` for AudioWorker (single .hpp/.cpp vs split state struct)
- Whether to log resolved device friendly name + sample rate at Init for UAT evidence

## Deferred Ideas

Captured in CONTEXT.md `<deferred>` section:
- SPSC SampleRing + audio→detection plumbing → P7
- `OnDefaultDeviceChanged` follow-the-default + device pinning UI → P7/P8
- 50-cycle Init/Cleanup stress test → P7 SC4
- `DeviceNotificationClient` ComPtr migration (CONCERNS.md item) → P7
- Driver as `config.json` reader / device id pin → P8
- LIB-04 logger sink injection (`DriverLogSink` + `FileLogSink`) → P8
- `RPC_E_CHANGED_MODE` shared-lib handling refactor → P7+ if needed
- `hmd_button_test.exe` retirement → P10 at earliest (P6 D-17(4) actively uses it)
- cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728) → P8 (per P5 D-15)

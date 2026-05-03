---
phase: 06-driver-side-audio-capture-spike
asvs_level: L1
block_on: any-open
audit_date: 2026-05-02
auditor: gsd-security-auditor
threats_total: 23
threats_closed: 23
threats_open: 0
result: SECURED
---

# Phase 6 Security Audit

**Phase:** 06 — driver-side-audio-capture-spike
**ASVS Level:** L1 (default — not configured)
**Audit Date:** 2026-05-02

## Threat Verification

| Threat ID | Category | Plan | Disposition | Evidence |
|-----------|----------|------|-------------|----------|
| T-06-01-01 | T (false-negative lint) | 01 | mitigate | cmake/AssertAudioWorkerNoVrApi.cmake:53-55 — regex set `[<\"]openvr[a-z_]*\\.h[>\"]`, `[^a-zA-Z0-9_]vr::`, `^vr::` copied byte-for-byte from lint_no_openvr_in_core.cmake |
| T-06-01-02 | D (RED test blocks ctest) | 01 | mitigate | cmake/AssertAudioWorkerNoVrApi.cmake:43-50 — `if(NOT EXISTS "${_file}") continue() endif()` skip branch; tests/CMakeLists.txt:151 — `if(EXISTS .../audio_worker.cpp)` conditional source inclusion |
| T1 | — (no COM in Plan 01) | 01 | accept | Plan 01 runs no COM, no thread; threat surfaces in Plan 02 — accepted per threat register |
| T6 | I (log flood — Plan 01 scope) | 01 | accept | RMS logging budget enforced in Plan 02; Plan 01 only registers the RED test — accepted per threat register |
| T1 | T (COM apartment corruption) | 02 | mitigate | driver/src/audio_worker.cpp:133 — `::CoInitializeEx(nullptr, COINIT_MULTITHREADED)` appears exactly once on the worker thread; three-bucket HRESULT handling: RPC_E_CHANGED_MODE path (lines 134-143), generic FAILED path (lines 144-150), success/S_FALSE path (lines 151-152) |
| T2 | S (UAF in IMMNotificationClient callback) | 02 | mitigate | driver/src/audio_worker.cpp:241 — `std::weak_ptr<State> weak = state_`; lines 244-247 — `auto sp = weak.lock(); if (!sp \|\| !sp->alive.load(std::memory_order_acquire)) return;`; Stop() line 96 — `state_->alive.store(false, ...)` BEFORE thread join/detach |
| T3 | D (Cleanup deadlock) | 02 | mitigate | driver/src/audio_worker.cpp:108-120 — 25 ms poll loop with 2 s `steady_clock` deadline; line 117 — literal log `"MicMap: audio worker did not exit within 2 s watchdog - detaching (T3 mitigation)"` present; line 119 — `thread_.detach()` last-resort |
| T4 | I (WASAPI handle leak) | 02 | mitigate | driver/src/audio_worker.cpp:294-300 — `capture_->stopCapture()` then `capture_.reset()` then `::CoUninitialize()` — all on the worker thread (reverse-order teardown, Pitfall 4 / D-13) |
| T6 | I (log flood) | 02 | mitigate | driver/src/audio_worker.cpp:55 — `constexpr uint32_t kRmsBudget = 100`; lines 257-262 — `if (emitted < kRmsBudget) { DriverLog(...) }` gates all RMS writes |
| T-06-02-01 | T (lint false-positive on DriverLog macro) | 02 | mitigate | driver/src/audio_worker.cpp — zero occurrences of `vr::` literal in source text; DriverLog is a function-style macro from `driver_log.hpp`, token not written as `vr::` in audio_worker.cpp; AssertAudioWorkerNoVrApi scans source text only |
| T5 | I (driver-audio surprise-on) | 02 | mitigate | driver/resources/settings/default.vrsettings:6 — `"enable_driver_audio": false`; confirmed SC4 byte-identical to Phase 5 via D-17(4) UAT |
| T1 carry | T (COM apartment — DeviceProvider) | 03 | mitigate | driver/src/device_provider.cpp — zero `CoInitializeEx` calls; 06-03-SUMMARY.md verification table: `grep -c "CoInitializeEx" driver/src/device_provider.cpp` == 0 PASS; DeviceProvider::Init never touches COM |
| T3 | D (Cleanup blocking shutdown) | 03 | mitigate | driver/src/device_provider.cpp:125-127 — `if (audioWorker_) { audioWorker_.reset(); }` appears BEFORE `httpServer_->Stop()` at line 130; 06-03-SUMMARY.md verification table confirms ordering |
| T5 | I (surprise-on — DeviceProvider) | 03 | mitigate | driver/src/device_provider.cpp:82-95 — three-bucket EVRSettingsError handling (`VRSettingsError_None`, `VRSettingsError_UnsetSettingHasNoDefault`, generic error); all non-success paths force `driverAudioEnabled_ = false`; line 101 — `if (driverAudioEnabled_)` construction gate |
| T-06-03-01 | I (Init failure cascade) | 03 | mitigate | driver/src/device_provider.cpp:103-106 — `if (!audioWorker_->Start()) { DriverLog("MicMap: AudioWorker::Start failed — continuing without audio\n"); audioWorker_.reset(); }` — Init continues and returns `VRInitError_None`; v1.5 trigger path preserved |
| T2 carry | S (UAF — delegated) | 03 | accept (delegated) | Mitigation lives in AudioWorker (Plan 02): weak_ptr alive-flag confirmed at driver/src/audio_worker.cpp:241-247; DeviceProvider::Cleanup only invokes audioWorker_.reset() which triggers the Plan 02 dtor |
| T4 carry | I (handle leak — DeviceProvider) | 03 | mitigate | driver/src/device_provider.cpp:125-127 — `audioWorker_.reset()` as first Cleanup step; Plan 02 reverse-order teardown (stopCapture → reset → CoUninitialize) confirmed above |
| T6 carry | I (log flood — delegated) | 03 | accept (delegated) | Budget enforced inside Plan 02 callback; Init/Cleanup log lines in DeviceProvider are O(1) per phase transition |
| T5 | I (surprise-on post-UAT) | 04 | mitigate | driver/resources/settings/default.vrsettings:6 — `"enable_driver_audio": false` (restored); 06-UAT.md Final state: `[x] default.vrsettings restored to enable_driver_audio: false` |
| T-06-04-01 | T (UAT log fabrication) | 04 | mitigate | 06-UAT.md D-17(1) Evidence block: real vrserver.txt lines including `MicMap audio: rms[0]=0.000855` through `rms[99]=0.001460`; D-17(3) Evidence block: real Cleanup→Init transition logs; sign-off: `decid (Brandon), 2026-05-02`; no `<paste here>` placeholders present |
| T-06-04-02 | D (operator stalls) | 04 | accept | Real-hardware spike intrinsic; binary resume signals (`READY`, `D17-1-2-3 PASS`, `D17-4 PASS`) used; UAT completed and signed off — accepted per threat register |
| T1/T2/T3/T4/T6 observe | — (real-hw verification) | 04 | observe | 06-UAT.md: D-17(1) — MTA CoInit hr=0x00000000, RMS budget exactly 100 lines (T1, T6); D-17(2) — handle count 1218 stable across 2 HMD wake/sleep cycles, no watchdog fire (T3); D-17(3) — `AudioWorker thread joined cleanly` +25 ms << 2 s watchdog, AUDCLNT_E_DEVICE_IN_USE = 0, second WASAPIAudioCapture fresh (T2, T3, T4); D-17(4) — zero AudioWorker / RMS lines with flag false (T5 SC4) |

## Accepted Risks Log

| Threat ID | Accepted By | Rationale |
|-----------|-------------|-----------|
| T1 (Plan 01 scope) | Phase 06 planner | Plan 01 introduces no COM/thread; T1 surfaces are in Plan 02 where they are mitigated |
| T6 (Plan 01 scope) | Phase 06 planner | RMS budget is enforced at the implementation site in Plan 02; Plan 01 has no per-frame logging path |
| T2 carry (Plan 03) | Phase 06 planner | Delegation is by design — Plan 02 owns the mitigation; Plan 03 only invokes the Plan 02 dtor |
| T6 carry (Plan 03) | Phase 06 planner | Delegation is by design — budget enforced in Plan 02 callback; DeviceProvider lifecycle logs are O(1) |
| T-06-04-02 | Phase 06 planner | Manual gates are intrinsic to a real-hardware spike; binary resume signals keep the operator unambiguous |

## Unregistered Flags

None. No `## Threat Flags` sections appeared in any plan SUMMARY.md. The two Rule 3 auto-fixes recorded in 06-02-SUMMARY.md (SafeDriverLog pre-context guard; test target OpenVR gating) are build-correctness issues, not new security threats, and map to no threat ID.

## Audit Trail

| Check | Result |
|-------|--------|
| All required_reading files loaded | PASS |
| Threat register extracted from all four PLAN.md files | PASS (23 threat entries) |
| cmake/AssertAudioWorkerNoVrApi.cmake regex set matches lint_no_openvr_in_core.cmake | PASS |
| NOT EXISTS skip branch present in AssertAudioWorkerNoVrApi.cmake | PASS (line 43) |
| CoInitializeEx appears exactly once in audio_worker.cpp | PASS (line 133 only) |
| RPC_E_CHANGED_MODE three-bucket handling present | PASS (lines 134-150) |
| weak_ptr alive-flag callback present | PASS (lines 241-247) |
| 2 s watchdog poll + detach last-resort present | PASS (lines 108-120) |
| Reverse-order teardown on worker thread | PASS (lines 294-300) |
| kRmsBudget = 100 with log gate | PASS (lines 55, 257-262) |
| zero vr:: literal in audio_worker.cpp | PASS |
| zero CoInitializeEx in device_provider.cpp | PASS |
| audioWorker_.reset() BEFORE httpServer_->Stop() in Cleanup | PASS (lines 125-130) |
| Three-bucket EVRSettingsError in DeviceProvider::Init | PASS (lines 82-95) |
| if (driverAudioEnabled_) construction gate | PASS (line 101) |
| Start() failure does NOT return VRInitError_Driver_Failed | PASS (lines 103-106) |
| default.vrsettings enable_driver_audio == false | PASS (line 6) |
| UAT evidence non-placeholder (real log lines present) | PASS |
| UAT sign-off with name + date | PASS (decid/Brandon, 2026-05-02) |
| D-17(1) RMS count in 80-120 range | PASS (exactly 100) |
| D-17(3) AUDCLNT_E_DEVICE_IN_USE = 0 | PASS |
| D-17(3) AudioWorker joined within 2 s watchdog | PASS (+25 ms) |
| D-17(4) zero AudioWorker lines with flag false | PASS |

---
phase: 6
slug: driver-side-audio-capture-spike
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-02
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake/CTest (existing) + manual real-hardware UAT (Bigscreen Beyond + Win11 Pro) |
| **Config file** | `CMakeLists.txt` (root + `driver/CMakeLists.txt`) |
| **Quick run command** | `cmake --build build-headless --config Release --target driver_micmap` |
| **Full suite command** | `cmake --build build-headless --config Release && ctest --test-dir build-headless --output-on-failure` |
| **Estimated runtime** | ~120 seconds (build + ctest); UAT D-17 sequence ~10 minutes manual |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build-headless --config Release --target driver_micmap` (driver-only target — fastest feedback for driver-only changes)
- **After every plan wave:** Run `cmake --build build-headless --config Release && ctest --test-dir build-headless --output-on-failure` (catches new source-grep lints + dumpbin invariant)
- **Before `/gsd-verify-work`:** Full suite must be green AND D-17 manual UAT artifacts committed to `.planning/phases/06-driver-side-audio-capture-spike/06-UAT.md`
- **Max feedback latency:** 120 seconds (build + ctest); manual UAT is end-of-phase only

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 06-01-01 | 01 | 0 | MIG-01 | — | N/A | static-grep | `ctest --test-dir build-headless -R AssertAudioWorkerNoVrApi --output-on-failure` | ❌ W0 | ⬜ pending |
| 06-01-02 | 01 | 0 | MIG-01 | — | N/A | static-grep | `ctest --test-dir build-headless -R AssertNoOpenVRInCore --output-on-failure` (P5 carryover, must remain green) | ✅ | ⬜ pending |
| 06-01-03 | 01 | 0 | MIG-01 | — | N/A | binary-export | `ctest --test-dir build-headless -R AssertDriverExports --output-on-failure` (P5 carryover; only `HmdDriverFactory` exported) | ✅ | ⬜ pending |
| 06-02-01 | 02 | 1 | MIG-01 | — | N/A | build | `cmake --build build-headless --config Release --target driver_micmap` (compiles `audio_worker.cpp`, links `micmap::core_runtime`) | ❌ W0 | ⬜ pending |
| 06-02-02 | 02 | 1 | MIG-01 | — | N/A | unit | `ctest --test-dir build-headless -R AudioWorkerLifecycleHeadless --output-on-failure` (constructs+destroys `AudioWorker` with capture mocked / device-absent path; verifies `alive=false` set before signal, no leaked threads) | ❌ W0 | ⬜ pending |
| 06-03-01 | 03 | 2 | MIG-01 | — | N/A | build | `cmake --build build-headless --config Release` (full driver + integration; no shared-lib regressions) | ✅ | ⬜ pending |
| 06-03-02 | 03 | 2 | MIG-01 | — | N/A | manual-UAT | D-17(1) flag-ON capture + `vrserver.txt` grep — see Manual-Only table below | N/A | ⬜ pending |
| 06-03-03 | 03 | 2 | MIG-01 | — | N/A | manual-UAT | D-17(2) HMD wake/sleep × 2 | N/A | ⬜ pending |
| 06-03-04 | 03 | 2 | MIG-01 | — | N/A | manual-UAT | D-17(3) SteamVR-restart-without-quit single cycle | N/A | ⬜ pending |
| 06-03-05 | 03 | 2 | MIG-01 | — | N/A | manual-UAT | D-17(4) flag-OFF regression via `hmd_button_test.exe` (v1.5 trigger path byte-identical) | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

*Note: Phase 6 is a real-hardware spike — Success Criteria 1, 2, 5 are observed via `vrserver.txt` log inspection on a Bigscreen Beyond + Win11 Pro rig and cannot be fully automated. SC3 + SC4 have automated static-grep / binary-export / build invariants in CTest, plus a flag-OFF regression run of `hmd_button_test.exe`.*

---

## Wave 0 Requirements

- [ ] `cmake/AssertAudioWorkerNoVrApi.cmake` — source-grep lint asserting `driver/src/audio_worker.{hpp,cpp}` contains no `vr::` API calls (mirrors P5 `AssertNoOpenVRInCore.cmake` shape; satisfies SC3 + Pitfall 3 carry).
- [ ] `tests/driver/audio_worker_lifecycle_headless.cpp` — headless GoogleTest (or equivalent) that constructs `AudioWorker` with the existing `IAudioCapture` factory faked / no-device path, drives Init→Cleanup, asserts `state->alive == false` before shutdown signal, asserts the worker thread joins within 2s. No real WASAPI device required.
- [ ] CTest registration for `AudioWorkerLifecycleHeadless` and `AssertAudioWorkerNoVrApi`.
- [ ] No new framework install — CMake/CTest + GoogleTest already in tree per Phase 5 P5 D-10 link surface.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| SC1 — Flag-ON WASAPI capture proven on real hardware | MIG-01 | Spike outcome only observable on Bigscreen Beyond + Win11 Pro rig with live SteamVR + vrserver.exe DLL host. The whole purpose of the phase. | (1) Set `enable_driver_audio = true` in `default.vrsettings`. (2) Launch SteamVR with HMD connected. (3) `grep` `vrserver.txt` for: "MicMap audio: worker constructed", `CoInitializeEx` outcome line (`S_OK`/`S_FALSE`), "WASAPI device opened", and ≥80 lines of `MicMap audio: rms[N]=` covering ~1s. (4) Confirm capture stays alive ≥30s. (5) Copy log excerpt to `06-UAT.md`. |
| SC2 — `RPC_E_CHANGED_MODE` distinct log path | MIG-01 | Hard to provoke synthetically; surfaces only if the driver's calling apartment is forced into STA. Inspect log line shape via static review + provoked-induction (force STA in a debug build harness if available). | Static review of `audio_worker.cpp`: confirm `if (hr == RPC_E_CHANGED_MODE)` branch logs `"MicMap: audio worker thread already in another COM apartment (RPC_E_CHANGED_MODE) — bailing out"` and exits. Document branch coverage in `06-UAT.md`. |
| SC3 — `CoInitializeEx` only on worker thread, never RunFrame | MIG-01 | Static-grep + runtime invariant — automated lint covers grep; runtime confirmation via D-17(1) `vrserver.txt` (no `CoInitializeEx` log line tagged with RunFrame thread id). | (1) `AssertAudioWorkerNoVrApi` ctest must pass. (2) `vrserver.txt` from D-17(1) must show only one `CoInitializeEx` invocation, on the worker thread. |
| SC4 — Flag-OFF byte-identical to Phase 5 | MIG-01 | Behavioural equivalence to v1.5 baseline only verifiable end-to-end. Existing harness `hmd_button_test.exe` already proves the v1.5 trigger path. | D-17(4): set `enable_driver_audio = false`, restart SteamVR, run `hmd_button_test.exe`. Confirm dashboard toggles via POST /button → CommandQueue → `/input/system/click`. Diff `vrserver.txt` startup banner against Phase 5 baseline (no audio worker construction lines). |
| SC5 — `IMMNotificationClient` register/unregister on worker + alive-flag | MIG-01 | Requires runtime callback observation; SteamVR-restart-without-quit single cycle (D-17(3)) is the safest provocation surface for a spike. Process Explorer handle leak check is operator visual. | D-17(3): with flag-ON, "Restart SteamVR" via UI without quitting `vrserver.exe`. (1) Confirm `vrserver.txt` shows: notification client registered on worker, then unregistered before second Init, then re-registered. (2) Process Explorer: no leaked WASAPI handle on the `vrserver.exe` PID after Cleanup. (3) No `AUDCLNT_E_DEVICE_IN_USE` on second Init. |
| SC2 (companion) — RMS log budget bounded ≈1s | MIG-01 | Log-flooding regression check; visual on `vrserver.txt`. | After D-17(1): `grep -c "MicMap audio: rms\["` `vrserver.txt` ≈80–120 lines (10ms shared-mode period × ~1s). If ≫1000 lines, the budget is broken — D-08 violated. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies (manual-UAT tasks for SC1/SC2/SC5 are flagged manual-only with reason)
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify (each plan wave produces a build/ctest signal)
- [ ] Wave 0 covers all MISSING references (`AssertAudioWorkerNoVrApi`, `audio_worker_lifecycle_headless`)
- [ ] No watch-mode flags
- [ ] Feedback latency < 120s for code-only changes; UAT batch is end-of-phase
- [ ] `nyquist_compliant: true` set in frontmatter once Wave 0 lands

**Approval:** pending

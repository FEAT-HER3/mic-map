---
phase: 7
slug: driver-side-detection-thread
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-03
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Source: 07-RESEARCH.md §"Validation Architecture".

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake-native) — plain-main exit-code convention. P5/P6 precedent. |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build -R 'AssertDetectionRunnerNoVrApi\|test_command_queue\|AudioWorkerLifecycleHeadless\|DetectionSettingsPropagation' --output-on-failure` |
| **Full suite command** | `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure` |
| **Estimated runtime** | ~30 seconds quick / ~3 minutes full (Release reconfigure) |

---

## Sampling Rate

- **After every task commit:** Run quick command (lint + smoke loops, < 5s)
- **After every plan wave:** Run full suite (clean Release reconfigure)
- **Before `/gsd-verify-work`:** Full suite green AND UAT D-25(1)..D-25(6) signed off in `07-UAT.md`
- **Max feedback latency:** 5 seconds (quick) / 180 seconds (full)

---

## Per-Task Verification Map

> Filled by planner once PLAN.md task IDs are assigned. Skeleton populated from research §"Phase Requirements → Test Map".

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | 0 | MIG-02 | — | DetectionRunner pushes TapCommand; no `vr::*` | lint | `ctest -R AssertDetectionRunnerNoVrApi -V` | ❌ W0 (`cmake/AssertDetectionRunnerNoVrApi.cmake`) | ⬜ pending |
| TBD | TBD | TBD | MIG-02 | — | CommandQueue concurrent-push race-free (HTTP + DetectionRunner producers) | unit | `ctest -R test_command_queue` | ✅ existing — extend if needed | ⬜ pending |
| TBD | TBD | TBD | MIG-04 | — | Reverse-order Cleanup; 50-cycle Init/Cleanup zero leaked handles | integration | `ctest -R DeviceProviderLifecycleStress -V` | ❌ W0 (`tests/driver/device_provider_lifecycle_stress_test.cpp`) | ⬜ pending |
| TBD | TBD | TBD | MIG-06 | — | `publish()` snapshot propagates to detection thread < 50 ms | integration | `ctest -R DetectionSettingsPropagation -V` | ❌ W0 (`tests/driver/detection_settings_propagation_test.cpp`) | ⬜ pending |
| TBD | TBD | TBD | SC2 | — | grep audit `vr::*` outside `device_provider.cpp` + `manifest_registrar.cpp` | CI grep | `ctest -R AssertDetectionRunnerNoVrApi` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `cmake/AssertDetectionRunnerNoVrApi.cmake` — sibling lint covering `detection_runner.{hpp,cpp}` + `sample_ring.hpp`. Mirror `AssertAudioWorkerNoVrApi.cmake` shape (RED-tolerant skip-on-NOT-EXISTS).
- [ ] `tests/driver/detection_settings_propagation_test.cpp` — MIG-06 < 50 ms verifier (publish() from worker thread, steady_clock measure observed-swap, assert).
- [ ] `tests/driver/device_provider_lifecycle_stress_test.cpp` — SC4 / MIG-04 50-cycle harness using `GetProcessHandleCount` for handle-leak detection.
- [ ] `tests/CMakeLists.txt` — register new tests + new lint (mirror registration shape at `tests/CMakeLists.txt:173-176`).
- [ ] `tests/test_command_queue.cpp` — verify two-producer concurrent-push coverage; extend if missing (small diff).

Test framework install: not required — CTest is CMake-native and already in use.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Cover-mic toggles dashboard via in-process trigger; zero `POST /button` traffic | SC1 | Requires real Bigscreen Beyond + SteamVR + microphone | D-25(1) — flag-ON UAT; capture vrserver.txt + httpServer access log |
| Detection pause/resume on EnterStandby/LeaveStandby; no leaked WASAPI/COM handles | SC3 / MIG-03 | Requires real HMD sleep/wake cycle | D-25(2) — HMD wake/sleep ×2; Process Explorer handle audit |
| 50-cycle Init/Cleanup real-WASAPI audit | SC4 / MIG-04 | Headless catches handle leaks but real-WASAPI device-handle leaks may need scripted SteamVR-restart smoke | D-25(3) — Process Explorer audit on Bigscreen Beyond after 50 driver-restart cycles |
| Flag-OFF byte-identical regression vs P6 | — | Requires `hmd_button_test.exe` + real driver bring-up | D-25(5) — `enable_driver_audio=0` + `enable_driver_detection=0`, run hmd_button_test.exe |
| Coexistence handshake: `/health.driver_detection_active` + client trigger suppression | Pitfall 10 (D-09..D-12) | Requires both client UI + driver flag-ON | D-25(6) — confirm single-toggle behavior; flip flag mid-session, verify ≤ 1 health-poll resume |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (3 new test files + 1 new CMake lint + tests/CMakeLists.txt registration)
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s quick / < 180s full
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending

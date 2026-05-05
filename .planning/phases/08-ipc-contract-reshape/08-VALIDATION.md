---
phase: 08
slug: ipc-contract-reshape
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-05
---

# Phase 08 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> See `08-RESEARCH.md` § "Validation Architecture" for the full mapping of success criteria to automated/manual verification.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (existing) + ctest source-grep lints (Phase 5/6/7 pattern) + GoogleTest (existing in tests/) |
| **Config file** | `tests/CMakeLists.txt`, `cmake/Assert*.cmake` |
| **Quick run command** | `cmake --build build --target test_micmap_core && ctest --test-dir build -R '08_' --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~30s quick / ~3 min full |

---

## Sampling Rate

- **After every task commit:** Run quick command (Phase 8-scoped tests + lints)
- **After every plan wave:** Run full suite
- **Before `/gsd-verify-work`:** Full suite green + manual UAT signed off
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

> Plans 08-00..08-06 are TBD until gsd-planner runs. Populate this table from PLAN.md task IDs after planning. The per-success-criterion map below is the binding contract.

### Success Criterion → Verification Type

| SC | Description | Type | Mechanism |
|----|-------------|------|-----------|
| SC1 | PUT /settings atomic validate→persist→publish, HTTP 400 with `{field,reason}` on bad input | automated | GoogleTest against in-process http_server with mock CommandQueue + tmpdir for ReplaceFileW |
| SC2 | Driver sole writer of config.json | automated | `AssertNoConfigWriteInClient.cmake` ctest lint (grep `'config.json'` in `apps/micmap/src/` and `src/steamvr/src/`) |
| SC3 | Client UI live indicator (red on ECONNREFUSED, green on success), pill, level meter, cadence (5 Hz visible / 0.5 Hz minimized) | manual UAT | Real Bigscreen Beyond + Win11 rig; observation + screen recording |
| SC4 | All driver routes bind to 127.0.0.1 only | automated + UAT | `AssertHttpServerLocalhostOnly.cmake` lint freezes literal; `netstat -an` UAT confirms at runtime |
| SC5 | HTTP handlers never call `vr::*` (SVR-05 invariant survives) | automated | `AssertHttpServerNoVrApi.cmake` ctest lint (sibling of P7's `AssertDetectionRunnerNoVrApi.cmake`) |
| SC6 | Driver/client log to separate files via injected sinks; no `#ifdef MICMAP_DRIVER_BUILD` in `micmap_core_runtime` | automated + manual | Existing P5 grep lint catches `#ifdef`; manual file-existence check post-UAT for both log files |

---

## Wave 0 Requirements

Wave 0 lands before any production code (RED-tolerant scaffolds — Phase 6/7 pattern):

- [ ] `cmake/AssertHttpServerLocalhostOnly.cmake` — freezes `127.0.0.1` literal in cpp-httplib bind site
- [ ] `cmake/AssertHttpServerNoVrApi.cmake` — sibling of `AssertDetectionRunnerNoVrApi.cmake`, freezes SVR-05 for new routes
- [ ] `cmake/AssertNoConfigWriteInClient.cmake` — single-writer rule grep
- [ ] `cmake/AssertNoJsonInCore.cmake` — keeps nlohmann/json out of `micmap_core_runtime` (Option C ADL placement)
- [ ] `tests/driver/http_settings_atomicity.cpp` — GoogleTest scaffold for PUT /settings validate-persist-publish (RED-tolerant)
- [ ] `tests/driver/http_state_endpoint.cpp` — RED-tolerant scaffold for `/state` and `/state/clear-error`
- [ ] `tests/driver/http_devices_endpoint.cpp` — RED-tolerant scaffold for `/devices`
- [ ] `tests/driver/http_telemetry_level.cpp` — RED-tolerant scaffold for `/telemetry/level` (RMS publish/load)
- [ ] `tests/driver/logger_injection.cpp` — RED-tolerant scaffold for `LIB-04` injected sinks
- [ ] `tests/client/driver_api_econnrefused.cpp` — RED-tolerant scaffold for `Error::Connection` → red, `Error::Read` → keep prior state
- [ ] CTest registrations for all of the above

*All scaffolds compile and run RED until production code lands. Phase 7 precedent: same pattern, no surprises.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| ImGui live driver-loaded indicator color flip on real driver | HEALTH-01 / SC3 | Real ImGui frame rendering can't be unit-tested cleanly without a D3D11 fixture | Launch `micmap.exe` with driver loaded → green; quit driver → red within 1 poll cycle |
| Detection-state pill update | HEALTH-02 / SC3 | Same — visual ImGui validation | Trigger detection in driver, observe pill cycles armed→triggered→armed |
| Level meter cadence (5 Hz visible / 0.5 Hz minimized) | HEALTH-04 / SC3 | Cadence visible only at runtime | Observe meter at ~5 Hz when window open; minimize to tray, confirm poll drops to ~0.5 Hz via driver log timestamps |
| Last-trigger-relative timestamp updates | HEALTH-03 / SC3 | Wall-clock formatting validation | Trigger detection, observe "X s ago" / "X min ago" tick |
| `netstat -an` shows only `127.0.0.1` binds | IPC-08 / SC4 | Runtime port-binding observation | Run driver, run `netstat -an | findstr <port>`, confirm no `0.0.0.0` |
| `%APPDATA%\MicMap\micmap-driver.log` and `micmap.log` written separately | LIB-04 / SC6 | Filesystem state check | Trigger driver activity + client activity, confirm both files exist with non-zero size and distinct content |
| HMD wake/sleep cycle does not strand the IPC server | (regression) | Timing-sensitive; covered by P6/P7 manual UAT pattern | Sleep HMD → wake → `GET /state` still returns within 1s |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies (post-planning)
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify (post-planning)
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter (post-planning audit)

**Approval:** pending — populate per-task table after gsd-planner runs.

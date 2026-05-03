---
phase: 07-driver-side-detection-thread
plan: 02
subsystem: infra
tags: [vrsettings, json, schema, openvr, driver, configuration]

# Dependency graph
requires:
  - phase: 06-driver-side-audio-capture-spike
    provides: "default.vrsettings with 4 driver_micmap keys (enable, http_port, http_host, enable_driver_audio); CMake POST_BUILD copy at driver/CMakeLists.txt:148 propagates the file to build/driver/micmap/resources/settings/"
provides:
  - "5 new VRSettings keys under driver_micmap: enable_driver_detection (bool, false), detection_sensitivity (float, 0.7), detection_threshold (float, 0.6), detection_cooldown_ms (int32, 1000), detection_min_duration_ms (int32, 200) — schema half of D-13"
  - "default-OFF discipline preserved (D-27): enable_driver_detection=false guarantees SC4 byte-identical-to-P6 regression invariant when 07-05 lands DeviceProvider::Init read logic"
affects: [07-05, 07-09, 08-PUT-settings]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "VRSettings single-read schema extension (D-13): append keys to driver_micmap block; values become detection-thread state via vr::VRSettings()->GetBool/GetFloat/GetInt32 in DeviceProvider::Init"
    - "Default-OFF flag discipline (D-27): new feature flags ship as JSON boolean false so a re-install on existing rigs cannot silently flip behavior"

key-files:
  created: []
  modified:
    - "driver/resources/settings/default.vrsettings — appended 5 detection keys (4 P6 keys preserved byte-identical)"

key-decisions:
  - "default values for detection_min_duration_ms (200) and detection_cooldown_ms (1000) intentionally diverge from v1.5 client-side DetectionConfig (300/300) per CONTEXT D-13 LOCKED — divergence will be reconciled in P8 cutover when driver becomes config.json reader"
  - "detection_cooldown_ms and detection_min_duration_ms shipped as JSON integers (no decimal point) so vr::VRSettings()->GetInt32 contract holds — verified via python isinstance(_, int) acceptance check"
  - "enable_driver_detection ships as JSON boolean false (not string 'false') so 07-05 GetBool falls through cleanly and SC4 byte-identical-to-P6 invariant survives"

patterns-established:
  - "Schema-only plan (no code, no tests): JSON edit + python json.load assertion battery is the entire verification surface — pattern replicable for future VRSettings schema bumps"

requirements-completed: [MIG-06]

# Metrics
duration: 2min
completed: 2026-05-03
---

# Phase 07 Plan 02: default.vrsettings detection schema (D-13)

**Appended 5 driver-side detection keys to default.vrsettings (enable_driver_detection=false, detection_sensitivity=0.7, detection_threshold=0.6, detection_cooldown_ms=1000, detection_min_duration_ms=200) — schema half of D-13 ready for 07-05 DeviceProvider::Init single-read.**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-05-03T11:04:40Z
- **Completed:** 2026-05-03T11:06:00Z
- **Tasks:** 1 / 1
- **Files modified:** 1

## Accomplishments

- 5 new VRSettings keys added under `driver_micmap` (enable_driver_detection, detection_sensitivity, detection_threshold, detection_cooldown_ms, detection_min_duration_ms) with the exact types and values from CONTEXT D-13.
- 4 existing P6 keys (enable, http_port, http_host, enable_driver_audio) preserved byte-identical — zero drift from P6 closeout.
- D-27 default-OFF discipline enforced: enable_driver_detection ships as JSON boolean `false`, guaranteeing SC4 byte-identical-to-P6 regression holds when 07-05's DeviceProvider::Init read logic lands.
- vrsettings GetInt32 contract preserved: cooldown_ms and min_duration_ms shipped as JSON integers (no decimal), validated via `isinstance(_, int) and not isinstance(_, bool)` python checks.

## Task Commits

Each task was committed atomically (parallel worktree, --no-verify):

1. **Task 1: Append 5 detection keys to default.vrsettings (D-13)** — `f3b7dfb` (feat)

## Files Created/Modified

- `driver/resources/settings/default.vrsettings` — extended `driver_micmap` block from 4 → 9 keys; existing P6 keys byte-identical; new keys typed and defaulted per D-13.

## Decisions Made

- **Default-mirror divergence documented in commit message, not in JSON.** The two divergences from v1.5 client-side DetectionConfig (`min_duration_ms` 200 vs 300, `cooldown_ms` 1000 vs 300) are LOCKED per CONTEXT D-13. Documented in the task commit body per the PATTERNS.md "Specifics" warning. JSON has no comments; the commit message is the doc anchor.
- **All 5 new keys appended in a single edit.** No interleaved code change — pure schema bump, trivially parallelizable with 07-03 DetectionRunner authoring (Wave 1, no file overlap).

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None. Configure passed (`cmake -B build-headless -S . -DMICMAP_BUILD_DRIVER=OFF` → "Configuring done"). Carryover ctest invariants stay green (Visual Studio multi-config required `-C Debug`):

- `lint_no_openvr_in_core` — Passed
- `AssertAudioWorkerNoVrApi` — Passed
- `AssertNoOpenVRInCore` — clean (configure-time CMake message, "visited 7 targets")

`AssertDetectionRunnerNoVrApi` is not present in this worktree because 07-01 lands it on a parallel branch — expected, not an issue. STATE.md changes shown in `git status` are orchestrator-owned (pre-existing) and were not staged in this plan's commit.

## User Setup Required

None — no external service configuration required.

## Threat Flags

None — no new trust boundary or surface introduced. The four threats in the plan's threat register are mitigated as designed:

- T-07-02-01 (JSON corruption silently disables flag): JSON validates; explicit value/type assertions in acceptance check.
- T-07-02-02 (surprise enablement): `enable_driver_detection is False` asserted; D-27 holds.
- T-07-02-03 (int-vs-float type confusion): `isinstance(_, int)` asserted on both *_ms keys.
- T-07-02-04 (silent default drift v1.5 vs v1.6): documented in commit message per CONTEXT D-13 LOCKED disposition.

## Self-Check: PASSED

- `driver/resources/settings/default.vrsettings` — FOUND (modified, 9 keys, JSON valid, all assertions pass).
- Commit `f3b7dfb` — FOUND in `git log` on branch `hmd-button`.
- `.planning/phases/07-driver-side-detection-thread/07-02-SUMMARY.md` — being created by this Write.

## Next Phase Readiness

- Schema half of D-13 complete; the runtime read half lands in 07-05 (DeviceProvider::Init single-read VRSettings block).
- 07-09 UAT D-25(1) can now toggle `enable_driver_detection` from false → true on the Bigscreen Beyond rig once the 07-05 read code lands.
- 08 PUT /settings cutover will surface the v1.5/v1.6 default delta on `min_duration_ms` and `cooldown_ms` — flagged for that plan's authoring step.
- No blockers.

---
*Phase: 07-driver-side-detection-thread*
*Plan: 02*
*Completed: 2026-05-03*

# Phase 7: Driver-Side Detection Thread - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-03
**Phase:** 07-driver-side-detection-thread
**Areas discussed:** SPSC ring + plumbing, HMD standby behavior, Double-trigger coexistence, Settings source in P7

---

## Gray-Area Selection

| Option | Description | Selected |
|--------|-------------|----------|
| SPSC ring + plumbing | Sizing, overflow policy, wakeup mechanism. Pitfall 12 audio-thread-never-blocks rule. | ✓ (auto) |
| HMD standby behavior | SC3 EnterStandby pause: pause detection only / pause both / drain-and-discard. | ✓ (auto) |
| Double-trigger coexistence | Pitfall 10 mitigation: manual UAT / /health-driven / driver swallows POST /button / accept cooldown squash. | ✓ (auto) |
| Settings source in P7 | VRSettings keys / hardcode / preempt PUT /settings / read config.json. | ✓ (auto) |

**User's choice:** "Other" — "agent decides all using best judgment". All four areas auto-resolved by Claude with recommended defaults; no interactive Q&A. This is functionally equivalent to `--auto` mode for this discuss session.

**Notes:** User explicitly delegated all decisions to agent judgment. Recommended options selected per gray-area description in the AskUserQuestion call. Reasoning captured in CONTEXT.md `<decisions>` D-01..D-28.

---

## SPSC ring + plumbing

| Option | Selected |
|--------|----------|
| Bounded ring sized 4× buffer-period (~16 slots × 480 frames ≈ 160ms backlog), drop-OLDEST, cv-wait wakeup with short timeout | ✓ |
| Drop-NEWEST policy | |
| Spin/busy-wait wakeup | |
| Unbounded queue | |

**Rationale:** Drop-oldest preserves recent acoustic state (the data detection actually needs); audio thread never blocks per Pitfall 12; cv with short timeout bounds shutdown latency without burning CPU. Recorded as D-01..D-05 in CONTEXT.md.

---

## HMD standby behavior

| Option | Selected |
|--------|----------|
| Pause detection only — AudioWorker keeps capturing, ring drops oldest until resume | ✓ |
| Pause both — close WASAPI on EnterStandby, rebind on LeaveStandby | |
| Drain-and-discard — detection thread runs but skips FFT/state-machine | (subset of selected option) |
| No-op — keep both running, ignore standby | |

**Rationale:** Cycling WASAPI device handles on every HMD wake/sleep amplifies Pitfall 4's lifecycle pressure. Detection paused while audio keeps producing means ring drops oldest until resume — no harm, no leaked handles. Detection thread implements drain-and-discard inside its paused branch. Recorded as D-06..D-08, D-21.

---

## Double-trigger coexistence (Pitfall 10)

| Option | Selected |
|--------|----------|
| Driver exposes `driver_detection_active` in /health, client auto-suppresses local trigger when true | ✓ |
| UAT instruction only — manual disable | |
| Driver swallows POST /button while own detection active | |
| Accept noise — state-machine cooldown squashes most | (belt-and-suspenders fallback within selected option) |

**Rationale:** /health is already polled in v1.5 for the connection indicator — adding one boolean field is the minimal change. Client suppresses trigger but keeps detection alive for UI visualization (confidence/RMS displays still work). State-machine cooldown is the secondary defense for the brief window before /health catches up. Entire mechanism deleted in P10 along with POST /button. Recorded as D-09..D-12.

---

## Settings source in P7

| Option | Selected |
|--------|----------|
| VRSettings keys (driver-native, single-read at Init) + atomic snapshot mechanism + headless 50ms-propagation test | ✓ |
| Hardcode v1.5 defaults | |
| Preempt minimal PUT /settings to validate MIG-06 e2e | |
| Read config.json directly | |

**Rationale:** VRSettings is the same single-read pattern P6 D-01 established for `enable_driver_audio` — driver-native, no shared-lib touch, no nlohmann/json bloat (Pitfall 15 deferred per P5 D-15/D-16). MIG-06's < 50ms propagation requirement is satisfied by the **mechanism** (atomic snapshot + publish() API + headless test), not by an HTTP path; P8 wires PUT /settings to call publish() as the production path. Defaults mirror v1.5 client-side config.json so P8 cutover doesn't drift. Recorded as D-13..D-16, D-25(4), D-28.

---

## Claude's Discretion

Items where the user accepted Claude's judgment without further interaction:
- SPSC ring slot count and per-slot frame size
- DetectionRunner cv_.wait_for timeout duration
- SampleRing template-on-T vs hardcoded-float
- C++20 `std::atomic<std::shared_ptr<>>` vs `std::atomic_load`/`store` free functions
- Detection-thread log verbosity and prefix convention
- File layout (single detection_runner.{hpp,cpp} vs split)
- Headless test layout (extend P6 file vs new files)
- HMD-pause event source (EnterStandby vs VREvent_TrackedDeviceDeactivated — recommended EnterStandby)
- Drop-count diagnostic logging cadence
- Client-side `/health.driver_detection_active` poll integration shape

## Deferred Ideas

None new from this discussion — all deferrals are downstream-phase scope as already mapped in ROADMAP.md (P8 IPC reshape, P9 training, P10 cutover/cleanup, P11 docs). Captured in CONTEXT.md `<deferred>`.

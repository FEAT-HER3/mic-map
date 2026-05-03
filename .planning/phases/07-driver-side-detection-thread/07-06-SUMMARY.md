---
status: CHECKPOINT
phase: 07
plan: 06
subsystem: uat
tags: [uat, d-25, d-26, d-27, manual-sign-off, hardware-validation]
dependency-graph:
  requires:
    - 07-01-SUMMARY.md (DetectionConfig + DetectionSettingsPropagation ctest)
    - 07-02-SUMMARY.md (default.vrsettings 5 detection keys)
    - 07-03-SUMMARY.md (DetectionRunner + MicMap detection: TapCommand pushed log line)
    - 07-04-SUMMARY.md (DeviceProvider lifecycle wiring + DeviceProviderLifecycleStress ctest + paused/resumed log pairs)
    - 07-05-SUMMARY.md (/health driver_detection_active + onTrigger suppression log)
  provides:
    - .planning/phases/07-driver-side-detection-thread/07-UAT.md (six D-25 cases scaffolded; sign-off pending)
    - default.vrsettings flag-ON state (transient; reverted by Task 3 post-UAT)
  affects:
    - Phase 7 GO/NO-GO gate (UAT sign-off table)
    - Main-branch shipped flag defaults (post-UAT D-27 closeout, NOT yet applied)
tech-stack:
  added: []
  patterns: [manual-sign-off-table, evidence-file-naming-convention]
key-files:
  created:
    - .planning/phases/07-driver-side-detection-thread/07-UAT.md
  modified:
    - driver/resources/settings/default.vrsettings (flags ON for UAT runs; transient)
decisions:
  - "Task 1 only — UAT scaffold authored + flags toggled ON. Tasks 2 (sign-off) and 3 (D-27 flag-OFF restore) explicitly deferred for interactive operator-driven execution by the orchestrator on the Bigscreen Beyond + Win11 Pro rig."
  - "Sign-off table left empty for operator fill-in; agent does not pre-mark cases as passing or signed."
  - "default.vrsettings flag-ON commit lives on worktree branch only; the post-UAT D-27 closeout commit will revert both flags to false before main-branch merge."
metrics:
  duration: ~4 minutes
  completed: 2026-05-03
  tasks_executed: 1
  tasks_pending: 2
---

# Phase 7 Plan 6: UAT Scaffold (CHECKPOINT — Tasks 2 + 3 pending)

**One-liner:** Scaffolded `07-UAT.md` with six D-25 manual sign-off cases (mirrors 06-UAT.md shape) and toggled both driver flags ON in `default.vrsettings` for the live UAT runs; D-25 sign-off and D-27 flag-OFF restore are explicitly handed back to the orchestrator for interactive operator-driven execution.

## Status

**CHECKPOINT** — Task 1 complete; Task 2 (six D-25 case sign-offs on Bigscreen Beyond + Win11 Pro) and Task 3 (post-UAT default.vrsettings flag-OFF restore per D-27) pending — handed back to orchestrator for interactive UAT.

## What was built (Task 1 only)

### 07-UAT.md scaffold (`.planning/phases/07-driver-side-detection-thread/07-UAT.md`)

279-line UAT artifact mirroring the 06-UAT.md shape. Six D-25 cases each with:

- **Setup** — exact reproduction steps (which `default.vrsettings` flag values, install path, SteamVR boot conditions).
- **Test** — numbered operator action list.
- **Pass criteria** — verifiable via log-grep, file-read, ctest, or visual observation.
- **Evidence** — artifact filenames namespaced under `.planning/phases/07-driver-side-detection-thread/uat-evidence/` per D-26.
- **Sign-off** — `⬜ pending → ✅ pass / ❌ fail (operator initials + date)` placeholder, **left empty** for operator fill-in.

The six cases cover:

| # | Case | SC | Pass criterion (load-bearing) |
|---|------|-----|-------------------------------|
| 1 | Flag-ON in-process trigger | SC1 | `MicMap detection: TapCommand pushed (n=N)` ≥ 3 lines AND zero `POST /button` traffic |
| 2 | HMD wake/sleep ×2 | SC3 / MIG-03 | 4 paired `MicMap detection: paused` / `MicMap detection: resumed` lines AND Process Explorer handle delta ≤ 10 |
| 3 | 50-cycle Init→Cleanup stress | SC4 / MIG-04 | Headless `DeviceProviderLifecycleStress` ctest delta ≤ 5 + 5-cycle real-rig SteamVR-restart audit (delta ≤ 20) |
| 4 | Settings propagation < 50 ms | SC5 / MIG-06 | Headless `DetectionSettingsPropagation` ctest `elapsed_ms < 50` |
| 5 | Flag-OFF regression | byte-identical to P6 closeout | `hmd_button_test.exe` exits 0 + zero `MicMap detection:` AND zero `MicMap audio:` lines |
| 6 | Coexistence handshake | Pitfall 10 / D-09..D-12 | Single dashboard toggle per cover + `curl /health` `driver_detection_active` toggles + mid-session flip resumes client trigger path |

Final sign-off table + Phase 7 GO/NO-GO line + D-27 closeout section (instructs operator to revert both flags to false post-UAT and commit with `docs(07): UAT GO; restore main-branch flag defaults to OFF (D-27)`).

### default.vrsettings flag toggle (`driver/resources/settings/default.vrsettings`)

Both `enable_driver_audio` and `enable_driver_detection` flipped from `false` → `true` so the UAT runs exercise the in-process detection path (D-25(1), D-25(2), D-25(3) real-rig, D-25(6)). D-25(5) requires the operator to flip back to `false` mid-UAT per the scaffold's setup steps.

This commit lives on the worktree branch only; **Task 3 (NOT executed by this agent)** will revert both flags to `false` after all six cases sign off ✅, restoring the shipped-main default-OFF discipline (mirrors P6 D-19/D-20 closeout).

## What is pending (Tasks 2 + 3)

### Task 2 — Six D-25 sign-offs on Bigscreen Beyond + Win11 Pro (BLOCKING checkpoint)

The orchestrator runs the UAT interactively with the operator. Each case requires real-hardware observation (cover-mic on Bigscreen Beyond, HMD sleep/wake proximity-sensor cycles, Process Explorer handle audits, SteamVR restart cycles, mid-session flag-flip + curl `/health`). The agent CANNOT automate this; SC1 + SC3 + D-25(6) coexistence handshake are explicitly real-hardware verifications per D-26.

Operator fills in sign-off table with initials + date as each case lands, captures evidence files into `.planning/phases/07-driver-side-detection-thread/uat-evidence/`.

### Task 3 — D-27 flag-OFF restore (post-UAT closeout)

After all six cases ✅, operator (or orchestrator) reverts both flags in `driver/resources/settings/default.vrsettings` back to `false` and commits with `docs(07): UAT GO; restore main-branch flag defaults to OFF (D-27)`. P10's cutover plan is the single point where defaults flip back to `true` for shipped v1.6.

## Deviations from Plan

None — Task 1 executed exactly as specified by the `<interfaces>` byte-template. Per the executor's prompt instructions, Tasks 2 and 3 were intentionally deferred (handed back to orchestrator for interactive UAT) rather than executed; this is the requested partial-execution shape, not a deviation.

## Self-Check: PASSED

**Files exist:**
- `.planning/phases/07-driver-side-detection-thread/07-UAT.md` — FOUND (15870 bytes)
- `driver/resources/settings/default.vrsettings` — FOUND (modified; both flags now `true`)

**Commits exist:**
- `bff959c` `docs(07-06): scaffold 07-UAT.md with six D-25 cases` — FOUND
- `db697e7` `chore(07-06): toggle enable_driver_audio + enable_driver_detection ON for UAT runs` — FOUND

**Acceptance grep checks (all met or exceeded vs PLAN's specified minima):**

| Pattern | Required | Actual |
|---------|----------|--------|
| `# Phase 7` | ≥ 1 | 1 |
| `Bigscreen Beyond + Win11 Pro` | ≥ 1 | 1 |
| `D-25(1)` | ≥ 1 | 7 |
| `D-25(2)` | ≥ 1 | 3 |
| `D-25(3)` | ≥ 1 | 4 |
| `D-25(4)` | ≥ 1 | 3 |
| `D-25(5)` | ≥ 1 | 5 |
| `D-25(6)` | ≥ 1 | 4 |
| `MicMap detection: TapCommand pushed` | ≥ 1 | 3 |
| `MicMap detection: paused` | ≥ 1 | 1 |
| `MicMap detection: resumed` | ≥ 1 | 1 |
| `DeviceProviderLifecycleStress` | ≥ 1 | 3 |
| `DetectionSettingsPropagation` | ≥ 1 | 3 |
| `hmd_button_test` | ≥ 1 | 6 |
| `driver_detection_active` | ≥ 2 | 7 |
| `onTrigger: driver_detection_active=true, suppressing` | ≥ 1 | 2 |
| `Sign-off`/`Sign-Off` | ≥ 6 | 10 |
| `GO/NO-GO` | ≥ 1 | 1 |
| `D-27` | ≥ 1 | 3 |

All Task 1 acceptance criteria from `07-06-PLAN.md` lines 347-367 satisfied.

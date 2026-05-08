---
gsd_state_version: 1.0
milestone: v1.6
milestone_name: Feature Migration
status: executing
stopped_at: Phase 9 context gathered
last_updated: "2026-05-08T08:44:32.116Z"
last_activity: 2026-05-06 -- Phase 08 execution started
progress:
  total_phases: 7
  completed_phases: 4
  total_plans: 20
  completed_plans: 20
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-30 with v1.6 Feature Migration milestone)

**Core value:** Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.
**Current focus:** Phase 08 — ipc-contract-reshape

## Current Position

Phase: 08 (ipc-contract-reshape) — EXECUTING
Next: `/gsd-discuss-phase 7` (no CONTEXT.md yet)
Plan: 1 of 7
Status: Executing Phase 08
Last activity: 2026-05-06 -- Phase 08 execution started

## Roadmap Summary

7 phases for v1.6 (continues numbering from v1.5 which ended at Phase 4):

| Phase | Name | Requirements | Research Flag |
|-------|------|--------------|---------------|
| 5 | Shared Library Extraction | LIB-01, LIB-02, LIB-03 | STANDARD |
| 6 | Driver-Side Audio Capture Spike | MIG-01 | NEEDS VALIDATION (WASAPI in DLL host) |
| 7 | Driver-Side Detection Thread | MIG-02, MIG-03, MIG-04, MIG-06 | STANDARD threading; NEEDS VALIDATION HMD sleep/wake |
| 8 | IPC Contract Reshape | IPC-01..08, HEALTH-01..07, LIB-04 | STANDARD |
| 9 | Training Migration | TRAIN-01..06, TEST-04 | NEEDS VALIDATION (training UX commit/discard) |
| 10 | Cutover & Cleanup | MIG-05, FAIL-01..05, HEALTH-08, TEST-01, TEST-02, TEST-03, TEST-05, INST-09 | STANDARD |
| 11 | Documentation | DOC-01, DOC-02 | STANDARD |

Coverage: 45/45 v1.6 requirements mapped. No orphans, no duplicates.

## Accumulated Context

### Decisions

Full log lives in PROJECT.md Key Decisions table.

Decisions affecting v1.6 roadmap:

- **Phase numbering continues from v1.5.** v1.5 ended at Phase 4 (Installer); v1.6 starts at Phase 5 — no reset.
- **Phase 5 must be first.** Shared lib is the keystone refactor; without it every later phase duplicates code or breaks the `mic_test.exe` headless invariant.
- **Phase 6 isolates the highest-risk unknown.** WASAPI inside vrserver DLL host is validated once in sister project `bey-closer-t1` but not in this driver — real-hardware spike on Bigscreen Beyond + Win11 Pro is mandatory before Phase 7. Behind `enableDriverAudio` flag, default OFF.
- **Driver-as-sole-writer for `config.json` and `training_data.bin`.** File-watching from the driver rejected (Pitfall 5 — torn reads, no rejection path). Settings flow exclusively through `PUT /settings`.
- **`POST /button` and `IDriverClient::tap()` survive until Phase 10.** Provides rollback path during phased migration; deletion is the cutover.
- **v1.5 SVR-05 invariant preserved.** HTTP-thread → CommandQueue → RunFrame is still the only path that touches OpenVR API. Detection thread becomes a new producer for the same CommandQueue; the boundary is unchanged.
- **Phase 5 (Documentation) carryover from v1.5 rolled into v1.6 as Phase 11** — DOC-01/DOC-02 re-scoped against the post-migration architecture.
- 06-01: Wave 0 RED scaffold uses skip-on-NOT-EXISTS lint branch + EXISTS-gated test source list so cmake configure stays clean while build-time missing-include diagnostic remains the Nyquist gate
- 06-02: AudioWorker class — Pitfall 1 apartment-trick (D-04) by constructing WASAPIAudioCapture on the worker thread; weak_ptr<State> alive-flag callback (Pitfall 13/D-15/D-16); 2 s watchdog Stop() (D-13); RPC_E_CHANGED_MODE distinct log line (D-06/SC2)
- 06-02: SafeDriverLog Rule-3 fix — guard on vr::VRDriverContext() before vr::VRDriverLog() so headless tests do not crash before VR_INIT_SERVER_DRIVER_CONTEXT
- 06-03: forward-decl class AudioWorker in device_provider.hpp rather than including audio_worker.hpp — mirrors HttpServer/CommandQueue pattern; complete type only needed in device_provider.cpp where ~DeviceProvider is defined
- 06-03: AudioWorker construction LAST in Init (after httpServer_->Start) and audioWorker_.reset() FIRST in Cleanup (before httpServer_->Stop) — D-13 reverse-order teardown enforced by explicit Cleanup() sequence not by member declaration order
- 06-03: D-14 fail-soft semantics — AudioWorker::Start() failure resets the unique_ptr but Init still returns VRInitError_None so the v1.5 HTTP/CommandQueue/HMD trigger path stays alive even when audio capture cannot start
- 06-04 Task 1: scaffold .planning/phases/06-driver-side-audio-capture-spike/06-UAT.md from PLAN interfaces template (Tested 2026-05-02, Driver SHA 8ace4e7, Rig/Operator placeholders); toggle default.vrsettings.driver_micmap.enable_driver_audio false → true for D-17(1)-(3) live runs (Task 3 restores to false per D-19/D-20 — shipped default OFF on main)

### Pending Todos

None — Phase 5 ready to plan.

### Blockers/Concerns

None blocking. Open questions to address during phase planning:

- **Phase 6 spike outcome:** if WASAPI fails inside the vrserver DLL host, escalate before Phase 7 begins. The whole architecture migration is predicated on this working.
- **Phase 9 training UX:** commit/discard pattern designed from first principles (no v1.5 prior art); validate with a real training session on hardware.
- **Phase 10 hmd_button_test.exe decision:** TEST-05 keeps it as a developer tool; confirm before cutover that this is still the right call given TEST-02 `--debug-trigger` overlap.
- **cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728):** deferred to a standalone deps-refresh plan inside Phase 5; risk to wire format is low but non-zero.

## Deferred Items

Items carried forward from v1.5 that are NOT in v1.6 scope (see PROJECT.md and REQUIREMENTS.md "Future Requirements"):

| Category | Item | Status | Reason |
|----------|------|--------|--------|
| Backlog | OBS-01 — unified rotating log file (one file shared across driver+client) | candidate | Partially addressed by LIB-04 + TEST-03 (separate files); unification deferred |
| Backlog | HEALTH-D1 — per-component health badges with hover tooltips | deferred | Future GUI revamp milestone |
| Backlog | HEALTH-D2 — trigger-history sparkline | deferred | Future milestone |
| Backlog | TRAIN-D2 — A/B threshold preview | deferred | Future milestone |
| Backlog | TEST-D2 — `/debug/snapshot` driver endpoint | deferred | Future milestone |
| Backlog | UX-01 (in-app auto-start toggle), UX-02 (in-VR settings overlay) | deferred | Out of v1.6 scope — settings UX work belongs to a later milestone |
| Backlog | DIST-01/02/03 (installer launch checkbox, silent-install docs, non-default Steam path) | deferred | Out of v1.6 scope |
| Backlog | DET-01/02 (detection accuracy in noisy environments) | deferred | Out of v1.6 scope — orthogonal to migration |
| Backlog | cpp-httplib v0.14.3 → v0.20.1 bump (CVE-2025-46728) | candidate | Deferred to standalone deps-refresh plan |

## Session Continuity

Last session: 2026-05-08T08:44:32.108Z
Stopped at: Phase 9 context gathered
Resume file: .planning/phases/09-training-migration/09-CONTEXT.md

**Next action:** `/gsd-discuss-phase 7` — gather context for Driver-Side Detection Thread (MIG-02, MIG-03, MIG-04, MIG-06).

**Phase 6 spike outcome:** GO. WASAPI capture inside `vrserver.exe` DLL host on Bigscreen Beyond + Win11 Pro is feasible. Phase 7 unblocked.

**Planned Phase:** 07 (Driver-Side Detection Thread) — 6 plans — 2026-05-03T07:53:18.904Z

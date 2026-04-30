---
gsd_state_version: 1.0
milestone: v1.6
milestone_name: Feature Migration
status: defining_requirements
stopped_at: v1.6 milestone opened 2026-04-30 — defining requirements
last_updated: "2026-04-30T00:00:00.000Z"
last_activity: 2026-04-30 -- v1.6 milestone opened (relocate non-UI features from client to SteamVR driver; shared audio/detection lib; roll in v1.5 Phase 5 docs)
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-30 with v1.6 Feature Migration milestone)

**Core value:** Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.
**Current focus:** v1.6 Feature Migration — relocate audio/detection/state-machine/config/trigger from `micmap.exe` into `driver_micmap.dll`; extract shared audio+detection library so the same code is buildable into driver, client, and headless test harness; client becomes settings + driver-health UI; roll in v1.5 deferred docs (DOC-01/02) updated for post-migration architecture.

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-04-30 — v1.6 milestone opened; running domain research before requirements

## Accumulated Context

### Decisions

Full log lives in PROJECT.md Key Decisions table.

Decisions affecting v1.6 (carried from v1.5):

- Phase 5 (Documentation) carryover from v1.5 rolled into v1.6 — DOC-01/DOC-02 will be re-scoped against the post-migration architecture, not the pre-migration shipped reality.
- Audio + detection code must live in a shared library so the same source compiles into the driver DLL, the client EXE, and the existing `mic_test.exe` harness — no SteamVR runtime dependency in the shared layer.
- File-sink logger remains a backlog candidate; not yet committed to v1.6 scope.

### Pending Todos

None.

### Blockers/Concerns

None blocking. Open architectural questions to surface in research:

- WASAPI capture from inside a SteamVR driver process (threading, COM apartment, audio device permissions when SteamVR runs under a different user context).
- Driver lifecycle for long-running detection thread (start/stop alignment with `IServerTrackedDeviceProvider::Init`/`Cleanup`; HMD reactivation cycles; CommandQueue boundary).
- New IPC contract: settings push, training-sample push, health pull. Whether localhost HTTP stays or is replaced.
- Config file ownership when both processes can read it (driver reads at boot, client writes on save) — file locking, atomic update propagation.

## Deferred Items

Items carried forward from v1.5 that are NOT in v1.6 scope:

| Category | Item | Status | Reason |
|----------|------|--------|--------|
| Backlog | File-sink logger (`%APPDATA%\MicMap\micmap.log`) | candidate | Not committed to v1.6; may be folded in if the driver-side logging story demands it |
| Backlog | UX-01 (in-app auto-start toggle), UX-02 (in-VR settings overlay) | deferred | Out of v1.6 scope — settings UX work belongs to a later milestone |
| Backlog | DIST-01/02/03 (installer launch checkbox, silent-install docs, non-default Steam path) | deferred | Out of v1.6 scope — installer touched in v1.5; further polish deferred |
| Backlog | DET-01/02 (detection accuracy in noisy environments) | deferred | Out of v1.6 scope — orthogonal to the migration |

## Session Continuity

Last session: 2026-04-30 — v1.6 opened
Stopped at: PROJECT.md + STATE.md updated; phase dirs to be cleared; running research before requirements.

**Next action:** Continue `/gsd-new-milestone` workflow — research → requirements → roadmap.

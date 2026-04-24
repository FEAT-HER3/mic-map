---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: unknown
stopped_at: Phase 03 Plan 06 complete — CLI fork + silent-boot policy + first-launch balloon integrated into WinMain; AUTO-02/AUTO-03/AUTO-06 closed at integration level; 8/8 ctest GREEN
last_updated: "2026-04-24T04:05:38.675Z"
last_activity: 2026-04-24
progress:
  total_phases: 5
  completed_phases: 2
  total_plans: 15
  completed_plans: 14
  percent: 93
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-22)

**Core value:** Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.
**Current focus:** Phase 03 — auto-start

## Current Position

Phase: 03 (auto-start) — EXECUTING
Plan: 7 of 7
Next: Plan 03-05 (Wave 1) — extract processVREvent to vr_input_events.{hpp,cpp}; inject AcknowledgeQuit_Exiting() BEFORE notifyEvent(Quit) for AUTO-05 / OpenVR #1425. Plan 03-06 (Wave 2) and 03-07 (Wave 3) follow.
Last activity: 2026-04-24

Progress: [█████████░] 93%

## Performance Metrics

**Velocity:**

- Total plans completed: 0
- Average duration: n/a
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

**Recent Trend:**

- Last 5 plans: n/a (no plans completed)
- Trend: n/a

*Updated after each plan completion*
| Phase 01-driver-sidecar-migration P03 | 25 | 3 tasks | 8 files |
| Phase 01-driver-sidecar-migration P04 | 2580 | 3 tasks | 7 files |
| Phase 03-auto-start P01 | 600 | 2 tasks | 7 files |
| Phase 03-auto-start P02 | 480 | 3 tasks | 8 files |
| Phase 03-auto-start P03 | 34 | 1 tasks | 3 files |
| Phase 03-auto-start P04 | 1200 | 2 tasks | 3 files |
| Phase 03-auto-start P05 | 133 | 2 tasks | 5 files |
| Phase 03-auto-start P06 | 900 | 3 tasks | 7 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Rip out virtual-controller driver entirely — no fallback, no feature flag (SVR-04).
- Adopt sidecar-on-HMD technique validated in bey-closer-t1 (`HMD Button Stub.md`).
- Use SteamVR-native auto-start (`app.vrmanifest`) — no Windows Run-key / Startup folder.
- Inno Setup installer patterned on `BeyondProximity.iss`, but MicMap owns its own driver directory (not nested).
- Fix stubbed JSON config read-back with already-vendored nlohmann/json.
- Phase 2 (Config Read-Back) is parallel-safe with Phase 1 (Driver Sidecar).
- Sidecar Init: HTTP start is fail-closed (VRInitError_Driver_Failed); HMD component creation is deferred to RunFrame to satisfy SVR-02
- HttpServer /status endpoint retained (not deleted) because app-side DriverClient::getStatus() probes /status rather than /health
- T-03-02 (browser Origin-check) deferred to a future driver-observability phase; default bind is loopback-only so in-scope browser CSRF is low-risk
- Plan 01-04 stripped DashboardState/HMDButtonAction enums and the four IVRInput dashboard methods (getDashboardState/sendHMDButtonEvent/sendDashboardSelect/performDashboardAction) per Plan 01-02 SUMMARY hand-off and 01-04 Task 3 Part D; VREventType enum kept (apps consume Quit + SteamVRConnected/Disconnected)
- hmd_button_test.exe harness now exposes Send Press / Send Release / Tap (press+150ms+release) buttons that drive IDriverClient::press()/release() directly; Open Dashboard/Send Click/Auto/Send A/Send System buttons and handlers removed (SVR-07 gate: zero forbidden-string hits across driver/src src/ apps/)
- Plan 03-01 publishes parseCliArgs's CliFlags struct at src/common/include/micmap/common/cli_flags.hpp (D-01 left location to discretion; chose micmap::common to avoid leaking apps/micmap private code into tests)
- Plan 03-01 vrmanifest schema test uses ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/app.vrmanifest path (multi-config-correct under MSBuild) and links against bare `nlohmann_json` target (project's external/CMakeLists.txt declares interface lib without `::` alias)
- Plan 03-01 vr_input quit-ordering test follows OPTION 1 (free function processVREvent in vr_input_events.{hpp,cpp}) — Plan 03-05 must extract to free fn rather than refactor OpenVRInput class internals
- Plan 03-02 — A2 LOCKED to STRING form: 'arguments': '--minimized'. SteamVR auto-launched 'micmap.exe --minimized' on Bigscreen Beyond + Win11; array-form variant deleted; test_vrmanifest_schema strict-asserts string form. Forward-slash manifest path is a SILENT KILLER (vrserver treats it as working dir, skips manifest, returns no error) — surfaced as <critical_pitfall> in Plan 03-04 with mandatory runtime guard.
- Plan 03-03 closes Q4 GREEN: Phase 2 writer requires explicit per-field wiring (j["key"] = c.field) — no struct introspection in nlohmann::json. Future AppConfig fields require 3 edits: declaration, read line, write line.
- Plan 03-04 — IVRApplicationsSurface seam uses vr::EVRApplicationError return types (plan proposed uint32_t; test contract from Plan 03-01 required native enum). GetApplicationsErrorNameFromEnum non-pure (has default impl) so the test stub that overrides only 4 methods can be instantiated. OpenVR linkage on micmap_steamvr elevated from PRIVATE to PUBLIC; Pathcch linked PRIVATE on WIN32.
- Plan 03-05 — free function named processVREventImpl (not processVREvent) to keep OpenVRInput::processVREvent member's delegation call-site unambiguous; Plan 01 RED test renamed in same commit (62fe4a5)
- Plan 03-05 — OpenVRInput uses nested private adapter classes (VRSystemAdapter + EventSinkAdapter) to bridge production vr::IVRSystem + member notifyEvent to the OpenVR-free seam in vr_input_events.hpp; AUTO-05 / D-11 ack-before-notify invariant locked at unit level
- Plan 03-06 — namespace aligned to Plan 01 RED test (micmap::apps plural, not micmap::app singular as plan prose suggested). Plan 01's working tree had an uncommitted src/common/src/cli_flags.cpp; committed under 03-06 Task 1 tag so per-task commit chain stays clean. Grep-gate-aware comments: forbidden tokens (AllocConsole, strstr(lpCmdLine) paraphrased so regex gates stay clean.

### Pending Todos

None yet.

### Blockers/Concerns

Research-flagged validation spikes (budget by phase planners):

- **Phase 1:** HMD reactivation lifecycle (Case D) untested in bey-closer-t1 — validation spike required before phase exit.
- **Phase 3:** `SetApplicationAutoLaunch` persistence bug (OpenVR issue #1547) — multiple-restart UAT cycles needed.
- **Phase 4:** Upgrade-from-0.x ghost-binding cleanup (Pitfall 8) — requires real test machine with legacy driver installed. Inno Setup 6 Pascal Script gotchas (Pitfall 16) warrant half-day buffer.

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## Session Continuity

Last session: 2026-04-24T04:05:38.670Z
Stopped at: Phase 03 Plan 06 complete — CLI fork + silent-boot policy + first-launch balloon integrated into WinMain; AUTO-02/AUTO-03/AUTO-06 closed at integration level; 8/8 ctest GREEN
Resume file: None

**Planned Phase:** 03 (auto-start) — 7 plans — 2026-04-24T02:46:02.744Z

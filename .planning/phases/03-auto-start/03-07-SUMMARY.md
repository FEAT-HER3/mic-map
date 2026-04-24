---
phase: 03-auto-start
plan: 07
subsystem: apps/micmap
tags: [winmain, retry-thread, ordered-shutdown, auto-04, auto-05, manifest-registrar, d-12, d-14, d-15, d-16, d-18, pitfall-6, uat]

# Dependency graph
requires:
  - phase: 03-auto-start
    plan: 04
    provides: "micmap::steamvr::createManifestRegistrar() + IManifestRegistrar::ensureRegistered() — the callable invoked by the retry thread body"
  - phase: 03-auto-start
    plan: 05
    provides: "processVREventImpl ack-first invariant (AcknowledgeQuit_Exiting() BEFORE notifyEvent(Quit)) — stops the Valve watchdog clock immediately so ordered teardown is not under time pressure"
  - phase: 03-auto-start
    plan: 06
    provides: "WinMain CLI fork + silent-boot + single-instance mutex — the foundation this plan lands MicMapApp::shutdown and the retry thread on top of, without touching WinMain pre-loop"
provides:
  - "MicMapApp::manifestRetryThread (std::thread) + MicMapApp::manifestRetryCancel (std::atomic<bool>) — fire-and-forget re-registration on every GUI boot; Pitfall 6-safe (never std::async)"
  - "MicMapApp::shutdown() body — D-12 ordered teardown: retry-thread-join → audio stop → detector reset → driverClient disconnect → vrInput shutdown → tray NIM_DELETE"
  - "D-14 exit-path convergence: WM_STEAMVR_QUIT / IDM_EXIT / WM_QUIT all drive running=false → g_app.shutdown()"
  - "AUTO-04 closed at live-UAT level (Procedure D — 5 cycles, 0 drift; retry thread is present as defense-in-depth)"
  - "AUTO-05 closed at live-UAT level (Procedure C — under-watchdog exit, no zombie) + existing Plan 05 unit-level ack-first invariant"
  - "Phase 3 exit criterion met: `.planning/phases/03-auto-start/03-07-UAT.md` — all Procedures A–E PASS"
affects:
  - "Phase 4 installer — micmap.exe's shutdown is now ordered + watchdog-safe; installer's [Run] / [UninstallRun] register/unregister calls can rely on 0/1 exit codes even when SteamVR is offline (A1 deviation: VRApplication_Utility works offline)"
  - "Phase 5 documentation — DOC-02 can reference the D-12 teardown order + Pitfall 6 std::async-vs-std::thread rationale as canonical architecture facts"
  - "Future observability micro-plan — file log sink at %APPDATA%\\MicMap\\micmap.log flagged as follow-up (see 03-07-UAT.md Procedure C deviation)"

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Retry-thread lifecycle: spun up in MicMapApp::initialize() AFTER configManager/vrInput exist; joined FIRST in shutdown() before any other subsystem teardown (prevents VR_Init/VR_Shutdown race)"
    - "Pitfall 6 avoidance: std::thread + std::atomic<bool> cancel-flag, 1s sleep ticks in 30-tick loop for cancel-responsiveness. NEVER std::async (whose future destructor blocks and defeats fire-and-forget semantics)"
    - "D-16 logging discipline: silent retry on HmdNotFound / NoServerForBackgroundApp (expected state), single WARNING with VRInitError enum name on other errors, no log spam"
    - "Shutdown idempotency via function-local static atomic guard — second call is a no-op even if called from multiple exit paths"
    - "UAT deviation recording discipline: plan-assumption-vs-reality discrepancies recorded in the UAT artifact with assessment + follow-up, not treated as gate failures when correctness is preserved"

key-files:
  created:
    - .planning/phases/03-auto-start/03-07-UAT.md
    - .planning/phases/03-auto-start/03-07-SUMMARY.md
  modified:
    - apps/micmap/main.cpp (MicMapApp member additions + initialize() retry-thread spin-up + MicMapApp::shutdown body)

key-decisions:
  - "A1 plan expectation (rc=1 offline) was wrong — VRApplication_Utility init mode works offline (talks to vrpathreg + appconfig.json on disk). Actual rc=0 offline is more robust and is exactly what Phase 4's installer [Run] step needs. No code change; UAT deviation documented."
  - "AUTO-05 ordered-teardown evidence gathered from Task Manager + Plan 05 unit tests (ack-first invariant) rather than from a file log — MicMap's logger is stdout-only under the WINDOWS subsystem and the plan's assumed %APPDATA%\\MicMap\\micmap.log does not exist. Non-blocking; file log sink recommended as a Phase 5 / micro-plan follow-up."
  - "Retry thread spun up unconditionally on every GUI boot (not guarded by a 'first run' flag). Cost is one VR_Init/VR_Shutdown round trip every 30s while SteamVR is running; on success the thread returns immediately. Net cost when everything is healthy: one VR_Init/VR_Shutdown per GUI session. Net benefit: self-heal on OpenVR #1547 drift with zero user action."
  - "Shutdown() idempotency via function-local static atomic rather than a member field — no added struct state; no observable from outside the function; zero chance of cross-instance interference (there is only one MicMapApp anyway, but future-proofing is free here)."
  - "`5-cycle 0-drift` observation on Bigscreen Beyond is recorded as informational, not proof of #1547 extinction. The retry-thread defense-in-depth remains. If field reports surface drift, escalate to a v1.x 'Re-register' UI button (original Phase 3 research spike disposition)."

patterns-established:
  - "Retry thread must be joined BEFORE vrInput->shutdown() — otherwise vrInput's VR_Shutdown can race the retry thread's VR_Init. Enforced by code comment + acceptance_criteria grep on shutdown() ordering."
  - "Function-local static atomic for idempotency guards — use when the guard state is purely internal and has no external observer. Avoids polluting struct state with bookkeeping flags."
  - "UAT artifact structure: per-procedure table (Step | Expected | Observed | Verdict) + separate Deviations Summary + Follow-up Items section. Makes the deliverable reviewable without chasing commit hashes."

requirements-completed: [AUTO-01, AUTO-04, AUTO-05]
# AUTO-01 closed at live-UAT level (A3: MicMap row in Startup Overlay Apps
#   with auto-launch toggle ON + B3-B4: silent auto-launch on SteamVR cold
#   start). Already integration-complete per Plans 02 / 06; this plan
#   confirms it live on Bigscreen Beyond.
# AUTO-04 closed at live-UAT level (D: 5 cycles, 0 drift + retry thread
#   present as defense-in-depth). Plan 04 closed AUTO-04 at unit level;
#   this plan integrates the thread into WinMain and runs live UAT.
# AUTO-05 closed at live-UAT level (C: under-watchdog exit, no zombie,
#   no "killed" dialog). Plan 05 closed AUTO-05 at unit level with the
#   ack-first invariant; this plan wires the ordered D-12 teardown body
#   and confirms live.

# Metrics
duration: ~10min (Task 1 implementation + build + ctest + UAT recording; live UAT procedures ran separately on the rig)
completed: 2026-04-23
tasks_completed: 2
files_changed: 1 (apps/micmap/main.cpp) + 2 artifacts
commits: 1 feat (a6d2372) + 2 docs (this summary + UAT)
---

# Phase 03 Plan 07: Retry Thread + Ordered Shutdown + Phase 3 UAT Summary

**MicMapApp now spins a cancel-responsive `std::thread` on every GUI boot that retries manifest registration until SteamVR is running, and tears its subsystems down in D-12 reverse-init order (retry-thread → audio → detector → driverClient → vrInput → tray) so SteamVR's 2s watchdog never fires. Live UAT on Bigscreen Beyond: all 5 procedures PASS; AUTO-01 / AUTO-04 / AUTO-05 closed. Phase 3 complete.**

## Performance

- **Duration:** ~10min (Task 1 implementation window; live UAT ran separately on the rig)
- **Started:** 2026-04-23 (Task 1 commit `a6d2372` at 2026-04-23 21:09:23 -0700)
- **Completed:** 2026-04-23 (UAT + this SUMMARY)
- **Tasks:** 2 (1 auto + 1 human-verify checkpoint)
- **Files modified:** 1 (`apps/micmap/main.cpp`)
- **Artifacts created:** 2 (`03-07-UAT.md`, `03-07-SUMMARY.md`)

## Accomplishments

- **Fire-and-forget retry thread** — `MicMapApp::manifestRetryThread` + `manifestRetryCancel` atomic. Runs `VR_Init(Utility) → ensureRegistered() → VR_Shutdown` every 30s (in 1s cancel-responsive ticks) until success, then exits. Pitfall 6-safe (never `std::async`).
- **D-12 ordered shutdown** — `MicMapApp::shutdown()` body wired: retry-thread join FIRST, then audio stop → detector reset → driverClient disconnect → vrInput shutdown → tray `NIM_DELETE`. Idempotent via function-local static atomic.
- **D-14 exit-path convergence** — `WM_STEAMVR_QUIT`, `IDM_EXIT`, and the main-loop `WM_QUIT` path all drive `running=false` → `g_app.shutdown()`.
- **Live UAT on Bigscreen Beyond** — all 5 procedures (A/B/C/D/E) PASS. AUTO-01 / AUTO-04 / AUTO-05 closed at live-HMD level.
- **Phase 3 complete** — Ready for `/gsd-code-review 3` and `/gsd-verify-work 3`.

## Task Commits

1. **Task 1: Retry-thread members + `initialize()` spin-up + `MicMapApp::shutdown` body** — `a6d2372` (feat)
2. **Task 2: SteamVR full-restart UAT** — no code commit (live-rig verification). Evidence captured in `.planning/phases/03-auto-start/03-07-UAT.md`.

**Plan metadata:** forthcoming (this SUMMARY + UAT + STATE/REQUIREMENTS/ROADMAP updates will be committed atomically after this file is written).

## Files Created/Modified

- `apps/micmap/main.cpp` — Added `manifestRetryThread` (`std::thread`) + `manifestRetryCancel` (`std::atomic<bool>`) members to `MicMapApp`; spun up retry thread at end of `initialize()`; implemented `MicMapApp::shutdown()` body with D-12 ordered teardown; verified exit-path convergence.
- `.planning/phases/03-auto-start/03-07-UAT.md` — Live UAT record (all 5 procedures PASS; 2 documented plan deviations; follow-ups listed).
- `.planning/phases/03-auto-start/03-07-SUMMARY.md` — This file.

## Decisions Made

See `key-decisions` in frontmatter above. Highlights:

- **A1 rc=0 offline is correct behavior, not a bug** — `VRApplication_Utility` init mode does not need vrserver. The plan's rc=1 expectation was too pessimistic; actual behavior is more robust and is exactly what Phase 4's installer needs.
- **AUTO-05 evidence via Task Manager + Plan 05 unit tests** — because the logger is stdout-only (no file sink yet), and the plan assumed a file log path that doesn't exist. Non-blocking; follow-up logged.
- **Retry thread unconditional on GUI boot** — cost is trivial (one VR_Init round trip in the healthy path); benefit is zero-touch recovery on OpenVR #1547 drift.
- **Shutdown idempotency via function-local static atomic** — zero struct-state cost, no external observer needed.

## Deviations from Plan

### UAT Deviations (non-blocking)

**1. [UAT Deviation] A1 rc expectation was wrong**
- **Found during:** UAT Procedure A step 1
- **Issue:** Plan expected `rc=1` (`VRInitError_Init_HmdNotFound`) when registering with SteamVR offline; actual result was `rc=0` (successful registration on disk).
- **Root cause:** `VR_Init(&err, vr::VRApplication_Utility)` mode works offline — utility APIs talk directly to `vrpathreg` / `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` / `appconfig.json` on disk; no running vrserver required.
- **Fix:** No code change required — actual behavior is more robust than plan assumed, and is exactly what Phase 4's installer `[Run]` step needs (register works with SteamVR closed).
- **Files modified:** none
- **Verification:** `appconfig.json` inspected post-A1: MicMap entry present, auto-launch flagged ON. A3 SteamVR UI confirmed the same state.
- **Follow-up:** Update `.planning/phases/03-auto-start/03-07-PLAN.md` Procedure A wording the next time the plan file is touched (cosmetic, deferred).

**2. [UAT Deviation / Plan Gap] Procedure C assumed a file log at `%APPDATA%\MicMap\micmap.log` that does not exist**
- **Found during:** UAT Procedure C step 3
- **Issue:** Plan instructed "Open `%APPDATA%\MicMap\micmap.log` and scroll to the tail. Confirm teardown order lines appear in sequence." — but MicMap logs to stdout via `ConsoleLogger` (the WINDOWS subsystem discards stdout). No file log sink has been wired. `%APPDATA%\MicMap\micmap.log` does not and never did exist.
- **Root cause:** Observability gap; no file log sink in the logging module. Plan 06 SUMMARY noted this explicitly ("Log files under %APPDATA%\\MicMap\\ would be a Plan 07 concern; not needed for AUTO-02/03 acceptance.") but Plan 07 did not actually add one — it was out of scope for the retry-thread + shutdown work.
- **Alternative evidence used:**
  - Task Manager — `micmap.exe` exits within the Valve 2s watchdog ceiling (implies ack-first fired; otherwise SteamVR would have posted a "killed/unresponsive" dialog — it did not).
  - No zombie process — confirms `shutdown()` completed.
  - Plan 03-05 unit tests (`test_vr_input_events_quit_ordering`) lock the ack-before-notify invariant at source level; commits `62fe4a5`, `6b32b42`.
  - Full ctest suite still 8/8 GREEN post Task 1 commit `a6d2372`.
- **Files modified:** none
- **Verification:** Live Task Manager observation + existing unit tests + full ctest GREEN.
- **Follow-up:** Recommend wiring a `FileLogger` at `%APPDATA%\MicMap\micmap.log` as a dedicated observability micro-plan OR folded into Phase 5 DOC-02. Non-blocking.

---

**Total deviations:** 2 UAT deviations (both documentation/observability gaps in the PLAN.md itself, not implementation bugs).
**Impact on plan:** No implementation changes required. Both deviations are documented with alternative evidence paths and follow-up suggestions. Phase 3 exit criterion fully met.

## Issues Encountered

None during Task 1 implementation. Build clean, `ctest --test-dir build -C Release --output-on-failure` 8/8 GREEN. Grep gates:

- `grep -c "std::async" apps/micmap/main.cpp` (inside retry-thread block): 0 (Pitfall 6 respected).
- `grep -c "manifestRetryThread" apps/micmap/main.cpp`: ≥2 (member + initialize spin-up + shutdown join).
- `grep -A30 "manifestRetryThread\s*=\s*std::thread" apps/micmap/main.cpp | grep -c "ensureRegistered"`: ≥1.
- `grep -n "void MicMapApp::shutdown" apps/micmap/main.cpp`: ≥1 (body defined, not just declared).

## User Setup Required

None — no external service configuration required. The installer (Phase 4) will handle first-time manifest registration via `[Run] micmap.exe --register-vrmanifest` post-install.

## Phase 3 Closure

**All Phase 3 requirements closed:**

| ID | Description | Closure level | Evidence |
|----|-------------|---------------|----------|
| AUTO-01 | Ship `app.vrmanifest` + appear in SteamVR Startup Overlay Apps | live-UAT | 03-07-UAT Procedure A3 |
| AUTO-02 | `--register-vrmanifest` CLI mode | integration (Plan 06) + live-UAT | 03-06-SUMMARY + 03-07-UAT Procedure A |
| AUTO-03 | `--unregister-vrmanifest` CLI mode | integration (Plan 06) + live-UAT | 03-06-SUMMARY + 03-07-UAT Procedure A4 |
| AUTO-04 | Idempotent re-registration + self-heal | unit (Plan 04) + integration (this plan) + live-UAT (Procedure D, 5 cycles 0 drift) | 03-04-SUMMARY + this SUMMARY + 03-07-UAT Procedure D |
| AUTO-05 | `AcknowledgeQuit_Exiting` + ordered teardown ≤2s | unit (Plan 05) + integration (this plan) + live-UAT (Procedure C) | 03-05-SUMMARY + this SUMMARY + 03-07-UAT Procedure C |
| AUTO-06 | Silent boot, no focus steal, balloon once | integration (Plan 06) + live-UAT (Procedure B, E) | 03-06-SUMMARY + 03-07-UAT Procedures B, E |

**Phase 3 exit criterion:** MET. Ready for `/gsd-code-review 3` and `/gsd-verify-work 3`, then `/gsd-transition` to Phase 4 (Installer).

## Next Phase Readiness

- Phase 4 (Installer) unblocked. `micmap.exe --register-vrmanifest` / `--unregister-vrmanifest` are headlessly invocable with correct 0/1 exit codes even when SteamVR is offline (A1 UAT deviation confirmed this is the better behavior).
- Final driver file layout (Phase 1) + stable config schema (Phase 2) + live-validated auto-start (Phase 3) = all Phase 4 prerequisites satisfied.
- Phase 5 (Documentation) carries two informational follow-ups from this plan:
  1. File log sink at `%APPDATA%\MicMap\micmap.log` (observability).
  2. Update `03-07-PLAN.md` Procedure A text to reflect rc=0 offline behavior (cosmetic).

## Self-Check: PASSED

- `apps/micmap/main.cpp` modified: FOUND (`a6d2372`).
- `.planning/phases/03-auto-start/03-07-UAT.md`: FOUND (created in this pass).
- `.planning/phases/03-auto-start/03-07-SUMMARY.md`: FOUND (this file).
- Task 1 commit `a6d2372`: FOUND (`git log --oneline`).
- Full ctest suite: 8/8 GREEN (reported by Task 1 commit; no regression introduced by docs-only additions).

---
*Phase: 03-auto-start*
*Completed: 2026-04-23*
*UAT rig: Bigscreen Beyond + Win11 Pro (dev machine)*

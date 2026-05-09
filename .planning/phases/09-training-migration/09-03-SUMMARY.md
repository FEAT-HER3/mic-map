---
phase: 09-training-migration
plan: 03
subsystem: client-ui
tags: [phase-9, training-migration, wave-3, single-writer-cutover, client-rewire, ui]
requires:
  - 09-00 (AssertNoClientTraining lint script)
  - 09-01 (driver-side TrainingSession lifecycle)
  - 09-02 (5 training HTTP routes + IDriverApi extension + HealthView.driver_audio_enabled)
provides:
  - "endpoint-driven Training pane in apps/micmap (Train/Cancel/Recompute/Confirm/Discard buttons)"
  - "5 Hz GET /training/progress poll wired in pollDriverHealth (visible-window only)"
  - "GET /health full-envelope poll populating driverAudioEnabled + driverTrainingActive atomics"
  - "AssertNoClientTraining ctest registered (single-writer cutover go-live)"
affects:
  - apps/micmap/main.cpp (deleted v1.5 training body; inserted endpoint-driven pane; extended pollDriverHealth)
  - cmake/AssertNoClientTraining.cmake (Rule-3 lint narrowing — qualifier-prefixed regex)
  - tests/CMakeLists.txt (registered AssertNoClientTraining as ctest)
tech-stack:
  added: []
  patterns:
    - "5-state ImGui state machine driven by /training/progress.state (idle/collecting/computing/ready/cancelled/finalized)"
    - "next-poll path for finalize success (canonical UX, ≤200 ms latency); direct response only for ValidationFailed/Conflict"
    - "proactive UI gate sourced from /health.driver_audio_enabled — 503 audio_disabled response is now defense-in-depth fallback"
    - "destructive-confirmation modal pattern (Discard Profile) with WR-03/WR-07 audioMutex serialization"
key-files:
  created: []
  modified:
    - apps/micmap/main.cpp
    - cmake/AssertNoClientTraining.cmake
    - tests/CMakeLists.txt
decisions:
  - "Deleted lines 953-1034 of the v1.5 main.cpp training UI body (range was planner-estimated as 962-1027 + status block 1029-1034 — block expanded to include the Training section header at line 953-955 since Task 2 re-emits its own header in the new pane)."
  - "Preserved line 869 detector->loadTrainingData (now line 970): it is part of the device-switch handler that re-binds WASAPI to a new device after a /settings change — orthogonal to training UI lifecycle, kept as-is. Task 1G evaluation outcome."
  - "Narrowed AssertNoClientTraining lint regex from `[^a-zA-Z0-9_]X` to `detector->X` for all four PatternTrainer/INoiseDetector entry points (Rule-3 deviation). Required because the new IDriverApi::startTraining method shares a name with the old PatternTrainer::startTraining; the previous bare-token regex would FATAL on `driverClient->startTraining` (the very call form 09-03 ships). The narrowed regex preserves lint intent (forbid client TUs from calling INoiseDetector training surface) while permitting the IDriverApi calls."
  - "Wired healthView_.driver_audio_enabled via a new IDriverApi::getHealth() call piggy-backed on the existing 1 Hz health poll cycle. Two new atomics (driverAudioEnabled / driverTrainingActive) cache the boolean across polls; UI reads via .load(). The proactive Train Pattern gate (BeginDisabled when !audio_on) is the canonical contract; the 503 audio_disabled response from POST /training/start is now defense-in-depth only (Task 2 Step 5 confirmation)."
  - "Finalize success-path UX uses the next-poll canonical flow per UI-SPEC §'Confirm flow'. The Confirm & Save click calls finalizeTraining(true) and immediately returns; the success transition (state==finalized) is observed by the next 5 Hz progress poll (≤200 ms latency = 1 × 5Hz interval) which triggers detector->loadTrainingData() (D-24 optimistic apply, serialized with audioMutex) + 'Profile saved' toast. Direct-response handling is reserved for ValidationFailed / Conflict envelopes which DO need synchronous feedback so the UI can re-enable."
metrics:
  duration: "~1 hour single-session"
  completed: "2026-05-08"
  tasks: 3
  files-modified: 3
  commits: 3
---

# Phase 9 Plan 03: Single-Writer Cutover Summary

Single-writer cutover for `training_data.bin`: the v1.5 client-side training body in `apps/micmap/main.cpp` is deleted, replaced with an endpoint-driven Training pane that consumes the IDriverApi training methods shipped by 09-02; the AssertNoClientTraining lint goes live as a ctest, structurally enforcing the IPC-06 sole-writer rule from this commit forward.

## Tasks Executed

| Task | Name                                                                        | Commit  | Files                                                          |
|------|-----------------------------------------------------------------------------|---------|----------------------------------------------------------------|
| 1    | Delete v1.5 client-side training body + saveTrainingData call sites         | 0a9f8ca | apps/micmap/main.cpp                                           |
| 2    | Insert endpoint-driven training pane + proactive audio_enabled gate         | 27aa883 | apps/micmap/main.cpp, cmake/AssertNoClientTraining.cmake       |
| 3    | Register AssertNoClientTraining ctest — single-writer cutover go-live       | 531d874 | tests/CMakeLists.txt                                           |

## Exact Line Ranges Touched

Planner's line-number estimates shifted slightly because the Wave 0/1/2 commits (09-00 through 09-04) had not modified `apps/micmap/main.cpp` between 09-CONTEXT capture and 09-03 execution; the lines lined up almost exactly. As-executed:

**Deleted in `apps/micmap/main.cpp`:**

| Original Line(s) | What was removed                                                                                                       |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------- |
| 91-92            | `std::atomic<bool> isTraining{false};` + `std::atomic<int> trainingSampleCount{0};` members                            |
| 403-405          | Audio-callback `if (isTraining) { detector->addTrainingSample(...); trainingSampleCount++; }` block                    |
| 617-618          | Shutdown-time `if (detector && detector->hasTrainingData() && configManager) detector->saveTrainingData(...);`         |
| 953-1034         | Entire v1.5 Training UI body — section header + auto-stop check + Stop Training button + Train Pattern button + Clear button + Cover-mic coaching copy + "Status: Profile trained / No profile loaded" status block |

**Preserved in `apps/micmap/main.cpp`** (per plan instruction):

| Original Line | What was kept (now at the line listed)                                                                       | Notes                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| 300 → 326     | Startup `detector->loadTrainingData(configManager->getTrainingDataPath());`                                 | Comment now documents D-24 optimistic-apply hookup added by Task 2's poll handler              |
| 869 → 970     | Device-switch handler `detector->loadTrainingData(...)` after WASAPI re-bind                                 | Task 1G evaluation: confirmed orthogonal to training UI lifecycle (re-binds profile after a /settings device change). Kept verbatim. |

**Inserted in `apps/micmap/main.cpp`:**

| New location  | What was added                                                                                              |
| ------------- | ----------------------------------------------------------------------------------------------------------- |
| ~165-186      | `driverAudioEnabled` + `driverTrainingActive` atomics + `TrainingUiState` struct + `trainingUi_` member + `lastTrainingPoll` timer |
| ~537-551      | GET /health full-envelope fetch on every 1 Hz health-poll tick (when `driver_loaded`)                       |
| ~595-647      | 5 Hz GET /training/progress poll loop with stop conditions + canonical finalize-success path (D-24)         |
| ~649-663      | Orphan-recovery branch (synchronous /training/progress fetch when /health.driver_training_active goes true) |
| ~960-1264     | New endpoint-driven Training section: Idle / Collecting / Computing / Ready states, Discard Profile destructive modal, toast renderer, last_error renderer |

## Verification Results

All seven plan-level verification gates green:

1. `grep -nE "\bisTraining\b|\baddTrainingSample\b|\bfinishTraining\b|\bsaveTrainingData\b" apps/micmap/main.cpp` — **zero hits** (`startTraining` survives only as the new `driverClient->startTraining()` call site at line 1074 + a single comment reference at line 175, both expected).
2. `grep -n "loadTrainingData" apps/micmap/main.cpp` — three preserved hits (startup at line 326, finalize-success optimistic-apply at line 647, device-switch handler at line 970).
3. `cmake --build build --target micmap` — clean (only the pre-existing LIBCMT LNK4098 warning).
4. `ctest --test-dir build -R AssertNoClientTraining` — **PASS** (0.02 s).
5. `cmake -DCLIENT_ROOTS=apps/micmap -P cmake/AssertNoClientTraining.cmake` — `clean (5 files scanned across 1 roots)`.
6. Manual visual smoke deferred to 09-05 UAT per plan boundary.
7. ImGui colors used in the new pane: only the UI-SPEC palette — green `ImVec4(0,1,0,1)`, orange `ImVec4(1,0.5f,0,1)`, destructive `ImVec4(0.86f,0.20f,0.20f,1)`, plus auto-applied `ImGuiCol_TextDisabled` via `BeginDisabled`. No new color values introduced.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Lint regex collision between `IDriverApi::startTraining` and `PatternTrainer::startTraining`**

- **Found during:** Task 2 verification step (post-insert lint check)
- **Issue:** The plan explicitly mandates new client call sites of the form `driverClient->startTraining()` (the new endpoint-driven pane is the very thing 09-03 ships). The AssertNoClientTraining lint, as written by 09-00 Task 1, used a bare-token regex `[^a-zA-Z0-9_]startTraining` that matches **both** the v1.5 PatternTrainer call form (`detector->startTraining`) and the new IDriverApi call form (`driverClient->startTraining`). After Task 2's insertions, the lint FATALed even though the v1.5 code was correctly deleted. This was a planner-side gap: the planner knew both call surfaces shared the name (interfaces block at line 102 of the plan calls out `IDriverApi::startTraining` explicitly) but the lint script ships without an allowlist for the new call surface.
- **Fix:** Tightened the lint regex to require the `detector->` qualifier prefix for all four PatternTrainer/INoiseDetector entry points (`addTrainingSample`, `finishTraining`, `startTraining`, `saveTrainingData`). The narrowing preserves the lint's stated intent — preventing client TUs from calling into the v1.5 PatternTrainer / INoiseDetector training surface — while permitting the IDriverApi endpoint-driven calls. CMake regex has no negative lookbehind, so qualifier-based narrowing is the simplest robust differentiator. A future TU that aliased INoiseDetector to a different name would bypass the new regex; that trade-off is acceptable because the alias-based bypass requires deliberate refactor (caught at code review). Lint header updated with a "09-03 Rule-3 narrowing" subsection documenting the rationale.
- **Sanity check:** Synthetic input file containing `void f() { detector->startTraining(); }` still FATALs the lint (verified during execution). The `apps/mic_test/` directory-match allowlist still works (verified by passing both `apps/micmap` and `apps/mic_test` as CLIENT_ROOTS — lint reports "clean (5 files scanned across 2 roots)" because mic_test files are skipped).
- **Files modified:** `cmake/AssertNoClientTraining.cmake`
- **Commit:** 27aa883 (bundled with the Task 2 insertions for atomic logical change)

### Architectural Changes

None. No Rule-4 escalations.

## Plan-Level Truths Confirmed

| Truth (from plan must_haves)                                                                              | Confirmed |
| --------------------------------------------------------------------------------------------------------- | --------- |
| `apps/micmap/main.cpp:962-1027` v1.5 training body deleted                                                | Yes (block was 953-1034 as-executed; planner estimate within ~5 lines) |
| `:618` shutdown saveTrainingData deleted                                                                  | Yes (now line 619 after re-numbering; saveTrainingData call gone)      |
| `:974 + :991` saveTrainingData deleted (auto-stop / Stop Training)                                        | Yes (deleted with the 953-1034 block — those lines no longer exist)    |
| `:300` startup loadTrainingData PRESERVED                                                                 | Yes (now line 326; explanatory comment added per plan instruction)     |
| `:404` audio-callback addTrainingSample DELETED                                                           | Yes (replaced with comment + the surrounding `if (isTraining)` guard removed via natural deletion when the member was removed at line 91) |
| Endpoint-driven training pane: Train Pattern → IDriverApi::startTraining                                  | Yes (line 1074)                                                        |
| Endpoint-driven training pane: Cancel Training → cancelTraining                                            | Yes (line 1192)                                                        |
| Endpoint-driven training pane: getTrainingProgress 5 Hz poll                                              | Yes (line 612, in pollDriverHealth)                                    |
| Endpoint-driven training pane: Recompute Thresholds → recomputeTraining                                   | Yes (line 1218)                                                        |
| Endpoint-driven training pane: Confirm & Save → finalizeTraining(true)                                    | Yes (line 1242)                                                        |
| Proactive audio_enabled gate from healthView_.driver_audio_enabled (warning fix)                          | Yes (line 1058 const bool audio_on = driverAudioEnabled.load(); BeginDisabled at line 1063) |
| 503 audio_disabled now defense-in-depth fallback                                                          | Yes (handled in startTraining error switch at line 1086 — matches errorField "audio_disabled") |
| Finalize 200 OK → next-poll path canonical (≤200 ms latency)                                              | Yes (poll handler at line 619 triggers detector->loadTrainingData + "Profile saved" toast on state=="finalized") |
| AssertNoClientTraining ctest registration goes LIVE                                                        | Yes (tests/CMakeLists.txt line 935-948)                                |
| D-08: client-side detection unaffected — startup loadTrainingData preserved, audio-callback analyze preserved | Yes (analyze branch at line 414 unchanged; only the training-side branch removed) |
| D-25: client read of training_data.bin not wired in P9 — startup load only                                | Yes (no live-reload watcher introduced; reload-on-finalize hook reuses the existing loadTrainingData call) |
| D-06: AssertNoClientTraining ctest references the lint script with the apps/mic_test/ allowlist           | Yes (allowlist still active; lint sanity-checked against both roots)   |

## Threat Surface Scan

No new security-relevant surface introduced beyond the existing 09-02 HTTP IPC. The new training UI pane:

- Reads from /health and /training/progress (HTTP GET on 127.0.0.1; bound by IPC-07).
- Writes via POST /training/{start,finalize,cancel,recompute} (HTTP POST on 127.0.0.1; same trust boundary).
- All inputs (sensitivity slider, click events) are validated server-side per 09-02.
- Discard Profile modal modifies only the in-memory client-side detector — no on-disk side effects.
- The new `detector->loadTrainingData()` optimistic-apply call after finalize success reads the on-disk profile that the driver just wrote; the read is identical in nature to the existing startup load and inherits its trust assumptions.

No threat flags raised.

## Self-Check: PASSED

**Files referenced in commits exist:**
- `apps/micmap/main.cpp` — FOUND (1469 lines after edits; original 1468)
- `cmake/AssertNoClientTraining.cmake` — FOUND
- `tests/CMakeLists.txt` — FOUND

**Commits exist on hmd-button:**
- `0a9f8ca` — FOUND (`refactor(09-03): delete v1.5 client-side training body in main.cpp`)
- `27aa883` — FOUND (`feat(09-03): insert endpoint-driven training pane in micmap UI`)
- `531d874` — FOUND (`test(09-03): register AssertNoClientTraining ctest — single-writer cutover go-live`)

**Verification gates:**
- ctest AssertNoClientTraining — PASS
- cmake build micmap — clean
- cmake build driver_micmap — clean
- lint script direct invocation — clean (5 files scanned)
- Forbidden-token grep — zero hits

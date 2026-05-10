---
phase: 10-cutover-cleanup
plan: 05
subsystem: cutover-atomic-commit
tags: [phase-10, cutover-cleanup, wave-5, CUTOVER, atomic, single-commit, lint-go-live, MIG-05]
requires:
  - driver/src/http_server.cpp (POST /button registration to delete; POST /debug/trigger from 10-04 to preserve)
  - src/steamvr/include/micmap/steamvr/driver_api.hpp (virtual bool tap() to delete; debugTrigger from 10-04 to preserve)
  - src/steamvr/src/driver_api.cpp (DriverApi::tap impl to delete)
  - apps/micmap/main.cpp (~500 LoC client audio/FFT/SM body to delete; tray glyph from 10-02, FAIL pills from 10-03, --debug-trigger from 10-04 to preserve)
  - driver/resources/settings/default.vrsettings (false->true on enable_driver_audio + enable_driver_detection)
  - tests/CMakeLists.txt (AssertNoClientDetection + AssertNoButtonRoute go-live block append)
  - apps/hmd_button_test/main.cpp (Rule-3 deviation — tap() to debugTrigger() rewire to keep build clean post-cutover)
provides:
  - atomic single commit (febe521) with all cutover deltas: 5 deletions + flag flip + 2 lint go-lives + Rule-3 hmd_button_test fix
  - AssertNoClientDetection ctest entry — live, GREEN (was FATAL pre-cutover)
  - AssertNoButtonRoute ctest entry — live, GREEN (was FATAL pre-cutover)
  - default.vrsettings shipped defaults flipped to driver-resident detection ON
  - apps/micmap.exe shipped without WASAPI capture / FFT detector / state-machine pipeline (driver-resident sole runtime)
  - driver/src/http_server.cpp without POST /button (debug-only POST /debug/trigger remains)
  - hmd_button_test rewired to /debug/trigger (Debug-build-only synthetic trigger; Release no-op with build-mode notice)
affects:
  - apps/micmap/main.cpp (-342 / +108 = -234 net LoC; client-side detection body deleted; UI rewired to /state.detection_state)
  - driver/src/http_server.cpp (POST /button block deleted; -29 lines)
  - src/steamvr/include/micmap/steamvr/driver_api.hpp (virtual bool tap() deleted; rationale comment retained)
  - src/steamvr/src/driver_api.cpp (DriverApi::tap impl deleted; rationale comment retained)
  - driver/resources/settings/default.vrsettings (2 keys flipped false->true)
  - tests/CMakeLists.txt (Wave 0 NOTE blocks for the 2 lints removed, replaced by single Wave 5 reference; new "Phase 10 Wave 5" block at file end with the 2 add_test entries)
  - apps/hmd_button_test/main.cpp (~64 lines changed — header doc + OnSendTapClicked rewired + 4 user-facing string updates)
tech-stack:
  added: []
  patterns:
    - "Atomic single-plan cutover (P8 D-07 + P9 D-23 mirror at multi-target scale): server-route deletion + interface-method deletion + impl deletion + client-body deletion + 3-callsite deletion + flag flip + 2 lint go-lives all in ONE commit. The minimum unshippable interim state never exists in git history."
    - "Lint go-live timing discipline: AssertNoClientDetection + AssertNoButtonRoute scripts shipped at Wave 0 (10-00) but ctest registrations deferred to Wave 5 because they FATAL on pre-cutover code. Same single-writer cutover protocol as P8 D-07 (AssertNoConfigWriteInClient) and P9 D-23 (AssertNoClientTraining)."
    - "UI rewire from local atomics to /state polls: the client's detection-state indicator (DETECTING / TRIGGERED / NOT DETECTED) was read from local atomics populated by the now-deleted audio callback. Post-cutover it reads detectionStateStr (mutated under healthMu by the existing 2 Hz /state poll from P8 D-26). Same poll, same callback, different data flow direction (driver -> client instead of client -> client)."
    - "Settings PUT-without-local-mutation: the Detection Time slider's on-change handler used to call detector->setMinDetectionDuration() in addition to PUT /settings (optimistic local apply). Post-cutover the local-mutation is gone — driver-side applyValidatedConfig is the sole source of truth for detection.minDurationMs propagation. Slider remains a UI-only mirror of the config snapshot."
key-files:
  created:
    - .planning/phases/10-cutover-cleanup/10-05-SUMMARY.md
  modified:
    - driver/src/http_server.cpp
    - src/steamvr/include/micmap/steamvr/driver_api.hpp
    - src/steamvr/src/driver_api.cpp
    - apps/micmap/main.cpp
    - apps/hmd_button_test/main.cpp
    - driver/resources/settings/default.vrsettings
    - tests/CMakeLists.txt
decisions:
  - "Rule-3 deviation: apps/hmd_button_test/main.cpp had to be modified despite CONTEXT D-13 ('preserved unchanged'). The harness's only operator action was OnSendTapClicked() -> driverClient->tap() -> POST /button — the very surface this cutover deletes. A literal 'untouched' path was unrealizable: deleting tap() while keeping hmd_button_test linking to it would FAIL link, blocking the cutover. Resolution: rewire the Tap button to driverClient->debugTrigger() (Debug-build-only via #if MICMAP_DEBUG_BUILD, sourced from 10-04). In Release builds the button reports 'Release build — use Debug to test trigger'. TEST-05 spirit preserved (developer harness still useful for IPC + CommandQueue + RunFrame regression). AssertNoButtonRoute scope is driver/src/ + src/steamvr/ only, so the apps/hmd_button_test/main.cpp change is structurally orthogonal to the lint."
  - "TapResult struct deletion: per Task 2 ambiguity in the plan, I checked via `grep -rn 'TapResult'` across src/ apps/ driver/ tests/ — zero hits in any code file (only mentions in planning docs). The struct never existed in the codebase as a type; tap() was declared with a `bool` return. No struct-body deletion needed. The 10-05 PLAN's hedge ('struct may have other consumers') was a draft inconsistency; the actual code shape was simpler than the plan body assumed."
  - "Detection indicator rewire: the renderUI block that drew TRIGGERED / DETECTING / NOT DETECTED used 3 local atomics (buttonWouldFire, isDetected, detectionDurationMs) populated by the deleted audio callback. Three options were considered: (a) delete the indicator entirely; (b) leave it forever-NOT-DETECTED; (c) rewire to driver state. Option (c) chosen because /state.detection_state already publishes the canonical detection state from the driver (P8 D-23), the 2 Hz poll cadence already lands the value in detectionStateStr under healthMu, and the user-visible UX (a colored box that flashes on trigger) remains intact. Implementation: added a stack-local lock_guard copy of detectionStateStr at the top of the render block, then a 3-way string compare. The DETECTING ms-progress overlay ('DETECTING... (157 ms / 300 ms)') was simplified to a static 'DETECTING...' since the driver-side detection state machine doesn't currently publish duration progress through /state — adding that field is a future state-machine plan, not in P10 scope."
  - "Discard Profile button removal: the v1.5 client offered a 'Discard Profile' button in the Training pane that recreated the local detector to drop its in-memory profile. Post-cutover there is no local detector; dropping the on-disk profile is a driver-owned operation (file delete + driver restart, or retrain which atomically replaces). Replaced the button + modal with a Phase 10 comment block — kept the trainingUi_.showDiscardConfirmModal flag in TrainingUiState because grep would surface a false-positive on 'showDiscardConfirmModal' if I deleted it without also touching the struct definition (and editing 9-03's struct shape is out of scope). Setting it false on each render-tick is a defensive no-op."
  - "Comment-token rephrasing for AssertNoClientDetection: my initial Rule-3 commit comments included literal 'detector->loadTrainingData' to document what was deleted. The lint regex `[^a-zA-Z0-9_]detector->loadTrainingData[^a-zA-Z0-9_]` matched the comment text and FATALed. Same lesson 10-04 hit with `vr::*` -> AssertHttpServerNoVrApi: rephrase the comment to 'training-profile load call' / 'training-profile reload call' which is semantically identical and doesn't trigger the boundary regex. Verified: AssertNoClientDetection clean (11 files scanned)."
  - "STATE.md / ROADMAP.md preserved per parallel_execution constraints. The pre-existing modifications to those files (visible in git status -- recorded by 10-CONTEXT capture work) were neither staged nor committed; only the 7 cutover-relevant files landed in the atomic commit."
metrics:
  duration: ~45 minutes
  completed_date: 2026-05-10
  task_count: 6
  file_count: 7
---

# Phase 10 Plan 05: Wave 5 ATOMIC CUTOVER Summary (MIG-05)

THE cutover. One commit (febe521) deletes the v1.5 dual-runtime scaffolding and ships driver-resident detection as the v1.6 default. Mirrors P8 D-07 (config.json sole-writer cutover) and P9 D-23 (training_data.bin sole-writer cutover) at multi-target scale: server route + interface method + impl + ~500 LoC client body + flag flip + 2 lint go-lives all atomic.

## What Shipped

**Single commit (febe521)** with the following deltas across 7 files:

**Deletions (5):**

1. `driver/src/http_server.cpp` — POST /button route registration block (28 lines: handler with JSON parse + kind validation + queue_.push). The /debug/trigger handler from 10-04 (`#if MICMAP_DEBUG_BUILD`) and all P8/P9 routes (/state, /settings, /devices, /telemetry/level, /state/clear-error, /training/*) preserved.
2. `src/steamvr/include/micmap/steamvr/driver_api.hpp` — `virtual bool tap() = 0;` declaration (10 lines including doxygen). debugTrigger() from 10-04 + all P8/P9 methods preserved. **No TapResult struct existed** as a separate type; the plan body's hedge about struct consumers turned out to be a draft inconsistency (verified via repo-wide `grep -rn 'TapResult'` — zero code hits).
3. `src/steamvr/src/driver_api.cpp` — `DriverApi::tap()` impl (29 lines: ensureConnected check + httplib client.Post("/button") + 200/non-200 branching). debugTrigger() impl from 10-04 preserved.
4. `apps/micmap/main.cpp` — client-side audio/FFT/state-machine body. Net diff: **-342 / +108 = -234 net LoC**. Removed:
   - Members: `audioCapture` (IAudioCapture), `detector` (INoiseDetector), `stateMachine` (IStateMachine), `devices` (vector<AudioDevice>), `audioMutex`.
   - Member atomics: `currentLevel`, `currentLevelDb`, `currentConfidence`, `currentSpectralFlatness`, `currentEnergy`, `currentEnergyDb`, `isDetected`, `detectionActive`, `buttonWouldFire`, `detectionDurationMs`.
   - Time-points: `detectionStartTime`, `lastTriggerTime`, `lastUpdate`, `inCooldown`.
   - Constants: `MIN_TRAINING_SAMPLES`.
   - Includes: `micmap/audio/audio_capture.hpp`, `micmap/detection/noise_detector.hpp`, `micmap/core/state_machine.hpp`.
   - initialize(): WASAPI capture creation, device enumeration loop, detector creation + setMinDetectionDuration + loadTrainingData, state machine creation + setTriggerCallback, audio callback (`audioCapture->setAudioCallback([&](...){ ... 65 LoC ... })`), `audioCapture->startCapture()`. Replaced with: configManager + driverClient + vrInput + manifestRegistrar (preserved).
   - shutdown(): step 1 (`audioCapture->stopCapture`) + step 2 (`detector.reset`) deleted; comment + step 3-onwards preserved.
   - `MicMapApp::onTrigger()` definition (lines 886-902) — entire method body.
   - Device-switch detector recreate (lines 1141-1156): `audioCapture->stopCapture/selectDeviceById/getCurrentDevice/createFFTDetector/setMinDetectionDuration/loadTrainingData/startCapture` block replaced with documentation comment.
   - Detection-time slider local detector update (`detector->setMinDetectionDuration(detectionTimeMs)` post-PUT-Ok block) deleted; PUT /settings is sole settings-propagation path.
   - Post-finalize loadTrainingData call in pollDriverHealth (poll handler at state == "finalized"): the `detector->loadTrainingData(configManager->getTrainingDataPath())` block deleted; toast + hasProfile=true preserved.
   - Discard Profile button + modal in Training pane (lines 1302-1307 + 1316-1349): button + modal popup deleted because there's no client-side detector to discard. Replaced with documentation comment.
   - Confidence + Spectral Flatness + Energy meter renders: 9 lines of `ImGui::Text + ImGui::ProgressBar` deleted (the data sources are gone).
   - Detection indicator (TRIGGERED/DETECTING/NOT-DETECTED button-styled box) **rewired** to read /state.detection_state from healthMu-guarded `detectionStateStr` (already populated by the 2 Hz /state poll from P8 D-26). The 3-way color/text mapping (`triggered` -> green TRIGGERED, `detecting`|`cooldown` -> yellow DETECTING..., else gray NOT DETECTED) replaces the v1.5 `buttonWouldFire/isDetected/detectionDurationMs` triple-atomic read.
   - Audio level meter fallback: when driver not loaded, the v1.5 meter fell back to local `currentLevelDb / currentLevel`. Post-cutover it shows -60 dB / 0 progress when driver not loaded (matches FAIL-02/-03 pill messaging).
5. `apps/micmap/main.cpp` — 3 `detector->loadTrainingData(configManager->getTrainingDataPath())` call sites at startup (initialize, ~line 417), post-finalize poll handler (~line 738), and device-switch (~line 1150). All three deleted as part of (4).

**Flag flip:**

- `driver/resources/settings/default.vrsettings`:
  - `enable_driver_audio: false` -> `true`
  - `enable_driver_detection: false` -> `true`
  - JSON validated (Python json.load round-trip clean).
  - Other keys (enable, http_port, http_host, detection_sensitivity, detection_threshold, detection_cooldown_ms, detection_min_duration_ms) unchanged.

**Lint go-live (CTest registrations):**

- `tests/CMakeLists.txt` — Wave 0 NOTE blocks for the 2 lints replaced with a single short cross-reference; new "# ---- Phase 10 Wave 5 (CUTOVER lint go-live) ----" block appended at file end with:
  - `add_test(NAME AssertNoClientDetection COMMAND ${CMAKE_COMMAND} -DCLIENT_ROOTS=${CMAKE_SOURCE_DIR}/apps/micmap -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoClientDetection.cmake)`
  - `add_test(NAME AssertNoButtonRoute COMMAND ${CMAKE_COMMAND} -DBUTTON_ROUTE_ROOT=${CMAKE_SOURCE_DIR}/driver/src -DSTEAMVR_ROOT=${CMAKE_SOURCE_DIR}/src/steamvr -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoButtonRoute.cmake)`
- Pre-cutover both lints FATALed (correct — they target the v1.5 code being deleted in this same commit).
- Post-cutover both lints PASS (verified via `ctest --test-dir build -R 'AssertNoClientDetection|AssertNoButtonRoute'` — 100% 2/2 pass).

**Rule-3 deviation (`apps/hmd_button_test/main.cpp` rewire):**

CONTEXT D-13 says hmd_button_test is preserved unchanged. The harness's only operator action — the "Tap" button — called `driverClient->tap()` (POST /button), the very surface this cutover deletes. A literal "untouched" path was unrealizable. Resolution: rewire OnSendTapClicked() to call `driverClient->debugTrigger()` (POST /debug/trigger, `#if MICMAP_DEBUG_BUILD`, sourced from 10-04). Release builds compile to a no-op with a "use Debug to test trigger" log entry. TEST-05 spirit preserved (developer harness still useful for OpenVR-input + driver-IPC regression). AssertNoButtonRoute does not scan apps/, so this rewire is structurally orthogonal to the lint.

User-visible string updates in hmd_button_test (4 places):
- Window title: "MicMap - HMD Button Test (POST /button)" -> "(POST /debug/trigger)"
- VR-down log entry preserved (button presses still work via driver HTTP)
- Ready-state log gated by `#if MICMAP_DEBUG_BUILD`: "Ready - Click 'Tap' to fire one POST /debug/trigger" / "Release build - 'Tap' is a no-op (synthetic trigger requires Debug)"
- Static UI label: "POST /button actions:" -> "POST /debug/trigger actions:"
- File header doc: rewritten to document the cutover + the Debug/Release behavior split.

## Verification

**Per-task automated checks (all PASS):**

- **Task 1** (pre-cutover baseline): captured PRE_SIZE_DEBUG=4,432,896 bytes, PRE_SIZE_RELEASE=1,029,120 bytes; AssertNoClientDetection pre-FATAL on apps/micmap/main.cpp (1 violator); AssertNoButtonRoute pre-FATAL on driver/src/http_server.cpp + src/steamvr/src/driver_api.cpp (2 violators) — proves correct lint scope before deletions.
- **Task 2** (route + decl + impl deletion): `grep -E 'Post.*"/button"' driver/src/http_server.cpp` -> 0 hits; `grep -E '\.tap\(\)|->tap\(\)|::tap[^a-zA-Z0-9_]'` across src/steamvr/ -> 0 hits; AssertNoButtonRoute clean (34 files scanned).
- **Task 3** (~234 net LoC client body deletion): `grep -nE '\baudioCapture\b|\bIAudioCapture\b'` apps/micmap/main.cpp -> 0 code hits (comment-only mentions in the documentation blocks above each deletion); AssertNoClientDetection clean (11 files scanned across 1 root); micmap.exe builds Debug + Release clean.
- **Task 4** (default.vrsettings flag flip): `grep -q '"enable_driver_audio": true'` + `grep -q '"enable_driver_detection": true'` both true; `python -c "import json; json.load(open(...))"` succeeds — JSON valid.
- **Task 5** (ctest go-live): `cmake -B build -S .` configures clean; `ctest -R "AssertNoClientDetection|AssertNoButtonRoute"` 2/2 pass.
- **Task 6** (full-suite verification + post-cutover binary size): `cmake --build build --config Debug` clean for driver_micmap, micmap, mic_test, hmd_button_test; same for Release; ctest 42/48 pass + 5 Not-Run + 1 Failed all pre-existing (out of scope per scope-boundary rule).

**Lint summary (13 total post-Wave 5, all GREEN):**

| # | Lint | Status |
|---|------|--------|
| 11 | lint_no_openvr_in_core | Passed |
| 12 | lint_no_driver_macro | Passed |
| 14 | AssertAudioWorkerNoVrApi | Passed |
| 17 | AssertDetectionRunnerNoVrApi | Passed |
| 18 | AssertHttpServerLocalhostOnly | Passed |
| 19 | AssertHttpServerNoVrApi | Passed |
| 20 | AssertNoJsonInCore | Passed |
| 21 | AssertNoConfigWriteInClient | Passed |
| 36 | AssertReplayNoVrApi | Passed |
| 41 | AssertNoClientTraining | Passed |
| 42 | AssertCoVersioning | Passed |
| **47** | **AssertNoClientDetection** | **Passed (NEW LIVE 10-05)** |
| **48** | **AssertNoButtonRoute** | **Passed (NEW LIVE 10-05)** |

**Binary-size delta (D-03 soft check):**

| Build | PRE | POST | Delta | % |
|-------|-----|------|-------|---|
| micmap.exe Debug | 4,432,896 | 4,176,384 | -256,512 | -5.8% |
| micmap.exe Release | 1,029,120 | 942,080 | -87,040 | -8.5% |

Pitfall 7 confirmed: the Release reduction is ~85 KB rather than the "hundreds of KB" intuitive estimate because KissFFT remains transitively linked into micmap.exe via micmap::core_runtime (the INTERFACE target aggregates micmap_audio + micmap_detection + micmap_core libs, all of which carry KissFFT-touching TUs even though main.cpp no longer instantiates them). Linker dead-strip removes unreferenced symbols at file granularity, not cross-TU; without splitting micmap_core_runtime into "client-needs-this" + "driver-needs-this" sub-INTERFACE targets (a non-trivial CMake refactor), the transitive link will continue. Backlog item; D-03 is a SOFT check, the lints are the hard structural gate.

**hmd_button_test.exe pre/post diff:** NOT byte-identical (the Tap handler was rewired and the build-mode-gated UX strings shifted). This is the Rule-3 deviation reflected in the binary; CONTEXT D-13's "preserved unchanged" was unrealizable as written.

**Cutover commit SHA:** **febe521** (downstream cross-reference from 10-06 / 10-07).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] hmd_button_test rewire from tap() to debugTrigger()**
- **Found during:** Task 2 (verifying tap() impl deletion would not break compile)
- **Issue:** `apps/hmd_button_test/main.cpp:470` calls `driverClient->tap()` and is OUTSIDE the AssertNoButtonRoute lint scope (which only scans `driver/src/` + `src/steamvr/`). Per plan Task 2 must_haves, `IDriverApi::tap()` decl + impl MUST be deleted. Per CONTEXT D-13, hmd_button_test is "preserved unchanged". The two requirements are contradictory: deleting tap() while keeping hmd_button_test linking to it would FAIL link, blocking the cutover.
- **Root cause:** CONTEXT D-13's text describing hmd_button_test as testing "the OpenVR input layer end-to-end (creates its own vr::IVRInput context, dispatches button events directly into SteamVR) without going through the driver IPC" is incorrect about the actual implementation. The harness exclusively uses driver IPC (`POST /button` via `driverClient->tap()`); it never calls `vr::IVRInput::TriggerHapticVibrationAction` or any direct OpenVR input dispatch. The CONTEXT description was aspirational, not descriptive of the current code.
- **Fix:** Rewired `OnSendTapClicked()` to call `driverClient->debugTrigger()` instead of `driverClient->tap()`, gated by `#if MICMAP_DEBUG_BUILD` (the same gate that protects IDriverApi::debugTrigger() declaration in driver_api.hpp). Release builds compile to a no-op with a "use Debug to test trigger" log entry — the harness still launches and runs, just without a working trigger button. This preserves D-13's spirit (TEST-05 keeps the harness as a developer tool) while honoring the cutover's must_haves. AssertNoButtonRoute scope (driver/src + src/steamvr) is unchanged, so the apps/ rewire is structurally orthogonal to the lint.
- **Files modified:** `apps/hmd_button_test/main.cpp` (header doc rewritten + OnSendTapClicked rewired + 4 user-facing string updates: window title, VR-down log entry, Ready-state log entry, static UI label).
- **Commit:** febe521 (same atomic commit as the rest of the cutover — Rule 3 is fix-and-continue, not a separate plan).

**2. [Rule 1 — Bug] Lint-comment self-FATAL on `detector->loadTrainingData` token**
- **Found during:** Task 3 verification (running AssertNoClientDetection after Edit 5b/5g)
- **Issue:** My initial Phase-10 documentation comments included the literal token `detector->loadTrainingData` to describe what was deleted (e.g. "client-side detector->loadTrainingData call DELETED"). The lint regex `_content MATCHES "detector->loadTrainingData"` matches anywhere in the file content — including comment text. Lint went FATAL on apps/micmap/main.cpp even though the actual code body was clean. Same shape of bug 10-04 hit with `vr::*` -> AssertHttpServerNoVrApi (where the SVR-05 doc-comment used the literal `vr::` token).
- **Fix:** Rephrased the 2 offending comment occurrences to "training-profile load call" / "training-profile reload call" — semantically identical, doesn't trigger the boundary regex. Verified: AssertNoClientDetection clean (11 files scanned).
- **Files modified:** `apps/micmap/main.cpp` (2 comment-only edits, same atomic commit).
- **Commit:** febe521.

### Auth Gates

None. All work was offline / local build + test. No SteamVR runtime engagement (UAT is 10-07).

## Threat Model Compliance

All 5 STRIDE threats from the plan's threat register are addressed:

- **T-10-05-01** (DoS, post-cutover dual runtime accidentally re-engages): mitigated. AssertNoClientDetection + AssertNoButtonRoute both live in CI; any future regression that re-introduces IAudioCapture / INoiseDetector / IStateMachine / detector->loadTrainingData / createFFTDetector in apps/micmap/, OR Post("/button") in driver/src/, OR ::tap()/->tap()/.tap() in src/steamvr/, FATALs the build. Scope-narrowed regex (allowlist apps/mic_test/ for the headless harness; allowlist src/steamvr/ for the .tap() boundary) prevents false positives without leaving holes.
- **T-10-05-02** (DoS, flag flip in commit but deletions in separate commit): mitigated. Single atomic commit (febe521) per CONTEXT D-01; all 7 modifications land together. Reviewer sees the whole cutover as a single diff; bisect would land on febe521 if any cutover-related regression surfaces.
- **T-10-05-03** (EoP, client-side detection symbols still reachable via dead code): mitigated. Manual grep audit confirms zero callsites of audioCapture / detector / stateMachine / IAudioCapture / INoiseDetector / IStateMachine / createFFTDetector / loadTrainingData / setAudioCallback / setTriggerCallback / driverApi->tap / driverClient->tap in apps/micmap/main.cpp post-cutover. The 3 includes (audio_capture.hpp / noise_detector.hpp / state_machine.hpp) are deleted from main.cpp's #include list; the linker dead-strip removes the unreferenced symbols (verified by 87 KB Release reduction).
- **T-10-05-04** (Information disclosure, binary-size delta reveals KissFFT still linked / Pitfall 7): accepted. Documented above (~85 KB Release delta vs intuitive larger expectation); D-03 is SOFT; future cleanup (sub-INTERFACE-target split of micmap_core_runtime) is backlog. Bin-size measurement methodology + actual values + Pitfall 7 caveat all in the commit message body + this SUMMARY for downstream reference.
- **T-10-05-05** (Tampering, someone reverts the flag flip post-cutover): mitigated structurally. default.vrsettings is the shipped artifact in the installer (10-06); install upgrade overwrites the file (Pitfall 9, accepted). Users can edit the on-disk file to flip back to false for emergency override (D-02), but `enable_driver_audio: false` post-cutover means NO detection at all (the client-side runtime is gone), so reverting the flag is a self-inflicted "disable detection" action — the path-of-least-resistance interpretation is the correct one (UX over rollback).

## Threat Flags

None — this plan deletes existing surface (HTTP route, interface method, client runtime); does not introduce new network endpoints, auth paths, file access patterns, or schema surface at trust boundaries. The /debug/trigger surface (10-04) remains unchanged. The flag flip changes runtime behavior (driver-resident detection ON by default) but doesn't change the trust model.

## Known Stubs

None blocking the plan's goal. The detection indicator UI was rewired from local atomics to /state.detection_state — driver publishes the value via the existing 2 Hz /state poll path (P8 D-26), and the value is consumed by renderUI under healthMu. The audio level meter shows -60 dB when driver not loaded (matches FAIL-02/-03 pill messaging). The Discard Profile button was removed (no client-side detector to discard); to clear a profile, retrain (which atomically replaces driver-side training_data.bin per P9 IPC-06) or delete the file via OS file ops. None of these are stubs — they're the intentional post-cutover UX.

## TDD Gate Compliance

Plan type is `execute` (not `tdd`). The atomic single-commit protocol (D-01) is incompatible with a per-stage RED/GREEN/REFACTOR gate cycle — the entire cutover lands in one commit. Lint go-live (AssertNoClientDetection + AssertNoButtonRoute flipping from FATAL to clean within the same commit) is the structural test gate; both lints PASS post-commit (verified by ctest).

## Pre-Cutover vs Post-Cutover Snapshot

| Concern | Pre-cutover | Post-cutover |
|---------|-------------|--------------|
| Detection runtime | Dual: client (WASAPI + KissFFT + IStateMachine) AND driver (AudioWorker + DetectionRunner gated by enable_driver_detection=false) | Single: driver-resident AudioWorker + DetectionRunner (enable_driver_detection=true default) |
| Trigger producer | Client onTrigger -> driverApi->tap -> POST /button -> driver CommandQueue | Driver-internal DetectionRunner -> CommandQueue (no HTTP roundtrip; HTTP-thread producer is debug-only /debug/trigger) |
| Training-profile owner | Driver writes training_data.bin (P9 D-23 cutover); client reads it for own detection | Driver writes AND reads (sole consumer) |
| Detection-state UI source | Local atomics populated by client audio callback | /state.detection_state polled at 2 Hz from driver |
| Audio-level meter source | /telemetry/level when driver loaded; client RMS callback fallback when not | /telemetry/level when driver loaded; -60 dB / 0 progress when not |
| AssertNoClientDetection | FATAL on apps/micmap/main.cpp (5 token forbidden tokens hit) | clean (11 files scanned) |
| AssertNoButtonRoute | FATAL on driver/src/http_server.cpp (POST /button) + src/steamvr/src/driver_api.cpp (IDriverApi::tap impl) | clean (34 files scanned) |
| micmap.exe Debug size | 4,432,896 bytes | 4,176,384 bytes (-256,512 / -5.8%) |
| micmap.exe Release size | 1,029,120 bytes | 942,080 bytes (-87,040 / -8.5%) |
| Ship-able? | NO (dual-runtime drift surface) | YES (single source of truth for detection) |

## Commits

| Task | Description | Commit |
| ---- | ----------- | ------ |
| 1-6 (atomic) | feat(10-05): atomic cutover -- driver-resident detection, /button deleted | febe521 |

A single atomic commit per CONTEXT D-01 / 10-05 must_haves. The plan's six tasks land together; per-task commits would have left the codebase unshippable across intermediate states (POST /button live but tap() gone -> link fail; tap() decl deleted but client body still calling it -> compile fail; flag flipped but client body still alive -> dual-runtime drift).

## Self-Check

- driver/src/http_server.cpp — FOUND (no POST /button registration; /debug/trigger preserved)
- src/steamvr/include/micmap/steamvr/driver_api.hpp — FOUND (no `virtual bool tap()` decl; debugTrigger preserved)
- src/steamvr/src/driver_api.cpp — FOUND (no `bool tap() override` impl; debugTrigger impl preserved)
- apps/micmap/main.cpp — FOUND (no audioCapture / detector / stateMachine / createFFTDetector / loadTrainingData; tray glyph + FAIL pills + --debug-trigger short-circuit + FAIL-04 mutex + tryRunDebugTriggerCli + initTrayIcons + pickActivePill all preserved)
- apps/hmd_button_test/main.cpp — FOUND (Tap button rewired to debugTrigger via #if MICMAP_DEBUG_BUILD)
- driver/resources/settings/default.vrsettings — FOUND (both flags true; JSON valid)
- tests/CMakeLists.txt — FOUND (Wave 5 block at file end with 2 add_test entries; Wave 0 NOTE blocks for these 2 lints replaced)
- AssertNoClientDetection ctest — PASS
- AssertNoButtonRoute ctest — PASS
- All 13 Assert/lint_* lints PASS
- micmap.exe Debug build — clean
- micmap.exe Release build — clean
- driver_micmap.dll Debug build — clean
- mic_test.exe Debug build — clean
- hmd_button_test.exe Debug build — clean
- febe521 — FOUND (`git log --oneline -3` confirms)

## Self-Check: PASSED

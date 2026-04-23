---
slug: micmap-activation-no-hmd-tap
status: resolved
trigger: hmd_button_test.exe works when it sends the tap command, but activation in the main app (micmap.exe) doesn't seem to do anything. likely not wired for the changes from phase 1
created: 2026-04-23
updated: 2026-04-23
---

# Debug: micmap-activation-no-hmd-tap

## Symptoms

- **Expected behavior:** Covering mic in micmap.exe (trained noise pattern detected) triggers SteamVR dashboard toggle via HMD `/input/system/click` — same result as running hmd_button_test.exe's tap command.
- **Actual behavior:** Audio activation in micmap.exe appears to do nothing. Dashboard does not toggle.
- **Error messages:** None observed (logs not yet inspected).
- **Timeline:** Never worked post-phase-1 (driver sidecar migration). hmd_button_test.exe harness works — confirms OpenVR sidecar + bindings route is functional.
- **Reproduction:** Launch micmap.exe with SteamVR running, cover mic (trained pattern), observe no dashboard toggle.
- **User hypothesis:** micmap.exe main app not wired to new phase-1 sidecar activation path (legacy dashboard_manager was removed; likely the detection→activation call site still points at dead code or was not reconnected).

## Current Focus

- **hypothesis:** Wiring is correct to `driverClient->tap()`, but state machine never reaches Triggered because it consumes raw `result.confidence` (instantaneous) instead of the detector's already temporally-gated `result.isWhiteNoise`. Instantaneous confidence drops below the 0.7 threshold between callbacks even when a cover is sustained, so `updateDetecting()` resets to Idle before `minDetectionDuration` (300 ms) elapses, and `triggerCallback_` never fires.
- **test:** code-read comparison against hmd_button_test (which does NOT go through state machine — it invokes `driverClient->tap()` directly on a button click).
- **expecting:** confirm state machine uses `result.confidence` while UI uses `result.isWhiteNoise`; confirm these two values can disagree because of the detector's internal temporal gate.
- **next_action:** apply fix — drive the state machine from `result.isWhiteNoise` (as 1.0/0.0) and collapse the state machine's `minDetectionDuration` to 0 so it becomes a pure latch+cooldown over the detector's already-gated boolean.
- **reasoning_checkpoint:**
- **tdd_checkpoint:**

## Evidence

- timestamp: 2026-04-23 — `apps/micmap/main.cpp:303` calls `stateMachine->update(result.confidence, delta)` with the instantaneous confidence value from `NoiseDetector::analyze()`.
- timestamp: 2026-04-23 — `apps/micmap/main.cpp:232` sets `smConfig.detectionThreshold = config.detection.sensitivity` (default 0.7) and `smConfig.minDetectionDuration = 300 ms`.
- timestamp: 2026-04-23 — `src/core/src/state_machine.cpp:120-143` requires `confidence >= threshold` every update for `minDetectionDuration` straight; any single update with `confidence < threshold` during `Detecting` transitions back to `Idle` (line 127-130).
- timestamp: 2026-04-23 — `src/detection/src/noise_detector.cpp:285-287` computes `result.confidence = 0.35*energyRatio + 0.35*energyConsistency + 0.30*correlation` as an **instantaneous** per-frame score.
- timestamp: 2026-04-23 — `src/detection/src/noise_detector.cpp:290,322` uses an *internal* temporal gate + spike gate to derive `result.isWhiteNoise`; "high" hits use threshold 0.60, not 0.70, and the gate keeps the boolean stable across instantaneous dips.
- timestamp: 2026-04-23 — `apps/micmap/main.cpp:264-297` — all UI feedback (`isDetected`, `detectionActive`, `detectionDurationMs`, `buttonWouldFire`) is driven by `result.isWhiteNoise`, so the operator sees the yellow "DETECTING..." / green "TRIGGERED" states even though the state machine never fires.
- timestamp: 2026-04-23 — `apps/hmd_button_test/main.cpp:446-478` bypasses the state machine entirely and calls `driverClient->tap()` directly on a button click — which is why the harness works.
- timestamp: 2026-04-23 — the detector already owns the temporal gate (`detector->setMinDetectionDuration(config.detection.minDurationMs)` at `apps/micmap/main.cpp:211`). Passing raw `confidence` to the state machine also applies `minDetectionDuration` a second time, double-gating the trigger path.
- timestamp: 2026-04-23 — mic_test.exe does not actually call any driver; its `buttonWouldFire` is a UI-only latch. So mic_test working visually is not evidence that the state-machine → tap path works.

## Eliminated

- driverClient wiring differences between micmap.exe and hmd_button_test.exe: both use `createDriverClient()` with identical defaults (host=127.0.0.1, ports 27015-27025) → same behavior.
- `onTrigger` call site pointing at removed `dashboard_manager`: verified `onTrigger()` at `apps/micmap/main.cpp:335-343` calls `driverClient->tap()` correctly.
- Missing driver build: hmd_button_test.exe hitting the same driver proves the driver is live.
- Port mismatch / bindings patch not applied: hmd_button_test.exe's success rules this out.

## Resolution

- **root_cause:** `apps/micmap/main.cpp:303` feeds the instantaneous `DetectionResult::confidence` into `IStateMachine::update()`, but the state machine requires the value to stay `>= detectionThreshold` (0.7) for the entire `minDetectionDuration` (300 ms) without a single dip. The detector's real stable detection signal is `DetectionResult::isWhiteNoise`, which is produced by the detector's own temporal+spike gate (using a looser 0.60 "high" threshold and frequency-of-high-hits logic). Because `confidence` drops below 0.7 between audio callbacks, `updateDetecting()` keeps bouncing back to `Idle` and the `triggerCallback` that calls `driverClient->tap()` is never invoked. The UI still shows detection/trigger feedback because it consumes `isWhiteNoise`, which masked the state-machine bug.
- **fix:** In `apps/micmap/main.cpp` — (1) pass `result.isWhiteNoise ? 1.0f : 0.0f` (not `result.confidence`) to `stateMachine->update(...)`, and (2) set `smConfig.minDetectionDuration = 0 ms` so the state machine acts as a pure edge-latch + cooldown over the detector's already-gated boolean. Detection duration remains configured on the detector via `setMinDetectionDuration(config.detection.minDurationMs)` (single source of truth). Cooldown stays on the state machine.
- **verification:** manual hardware test on Bigscreen Beyond — launch micmap.exe with SteamVR running, cover mic; expect green TRIGGERED UI state + dashboard toggle. Log line "Tap fired after 0ms" from `state_machine.cpp:137` should print on each successful activation. `DriverClient::tap() successful` (`vr_input.cpp:188`) should follow.
- **files_changed:**
  - apps/micmap/main.cpp (state-machine config + update call-site)

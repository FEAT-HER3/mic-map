# Phase 9: Training Migration - Discussion Log

**Discussed:** 2026-05-08
**Mode:** default (no flags)

## Gray Areas Presented

The following four phase-specific gray areas were identified through analysis of P6/P7/P8 carry-forwards + REQUIREMENTS.md TRAIN-01..06 + TEST-04 + IPC-06 + the existing in-tree training code surface (`PatternTrainer` shared lib + client-side training body at `apps/micmap/main.cpp:962-1027` + driver's P7 read-only profile load at `driver/src/detection_runner.cpp:108-172`):

1. **Detection ↔ training arbitration** — While driver is training, what happens to (a) the driver's DetectionRunner thread, (b) the client-side detection still alive until P10, and (c) how is anti-feature TRAIN-AF-01 (client must NOT open WASAPI) structurally enforced?
2. **Recompute semantics (TRAIN-06)** — Where does the "most-recent stored sample set" live? Payload shape — `{sensitivity}` only or full TrainingConfig? Recompute returns preview-with-explicit-finalize or commits immediately?
3. **Session lifecycle + orphan recovery** — Idle-timeout for orphaned sessions? Concurrent-start collision behavior? Driver crash mid-session — discard or attempt resume?
4. **WAV replay harness shape (TEST-04)** — Where in the build (mic_test client-side per TEST-01)? Sample-rate mismatch policy? Output verification shape (trigger count, JSON, exit codes)? Mono/stereo/bit-depth support?

## User Selection + Direction

**User answered:** "agent decides all using best judgment. note that the replay harness will be used for future improvements to the detection algorithm. it will be used to enable automated+agent-driven QA given a pool of user-generated wav files. so optimize for that use case."

**Interpretation:** Claude makes all four decisions with best judgment. The replay harness gray area gets specific direction — optimize for batch / agent-driven QA workflows over user-generated WAV corpora, not for one-off developer use. This reframes TEST-04 design.

## Decisions Recorded

All four gray areas resolved with full decision detail in `09-CONTEXT.md`. Summary:

### A. Detection ↔ training arbitration
- **Mode-switch architecture**, not pause-detection. `std::atomic<DriverMode>` member (`Detecting | Training`) on `DeviceProvider` (HTTP handlers flip; DetectionRunner reads at top of every loop iteration). Same SPSC ring, same WASAPI thread, same UAF guards — only the per-iteration consumer branch changes (`detector_->analyze` ↔ `trainingSession_->addSample`).
- AudioWorker is unchanged; capture keeps running; ring keeps filling.
- TRAIN-AF-01 enforcement is **structural**: client-side training body (`apps/micmap/main.cpp:962-1027`) is **deleted** in P9 (Wave 3). New CMake lint `cmake/AssertNoClientTraining.cmake` greps `apps/micmap/` for `addTrainingSample\|finishTraining\|startTraining\|saveTrainingData` — must return zero. `apps/mic_test/` is allowlisted (it's the headless training tool — TEST-01 invariant).
- `GET /health` JSON gains `driver_training_active: bool` (mirrors P7 D-09 `driver_detection_active` callback pattern). Client uses it to gate the Train UI.
- Client-side detection (still alive until P10) is **not affected by training mode** — its mic-cover-during-training feeds nothing relevant because the client's `addTrainingSample` path is deleted.

### B. Recompute (TRAIN-06)
- **RAM-only sample storage** in `TrainingSession`. Lost on driver restart. Reasoning: privacy (raw mic audio ≈ user voice/room) + simplicity + matches REQ wording ("most-recent stored sample set" with no permanence promise).
- Payload: `{"sensitivity": float}` **only**, range-validated 0.0..1.0. HTTP 400 envelope on out-of-range.
- Behavior: **preview-then-confirm**. Recompute re-runs `PatternTrainer::finishTraining()` with new sensitivity, returns new `thresholds_preview` in HTTP 200, does NOT persist. Client must call `POST /training/finalize {"confirm":true}` to commit. Mirrors P8's preview UX and TRAIN-03 wording.
- Recompute is only valid when state is `ready`. Returns HTTP 409 outside `ready`. Idempotent within `ready` — multiple recomputes replace the preview.

### C. Session lifecycle + orphan recovery
- **Single-session-at-a-time** via mutex on driver. Concurrent `POST /training/start` returns HTTP 409 with no state mutation.
- States: `collecting → computing → ready → finalized`; `cancelled` is terminal sibling reachable from any non-finalized state.
- **30 s no-new-sample timeout** auto-cancels with `last_error="training_timed_out_no_samples"`. Resets on every accepted sample (per-session, not wall-clock since start). Prevents permanent mic-hold from orphaned client without breaking interactive UX.
- `POST /training/cancel` is **idempotent** — returns 200 OK whether or not a session is active.
- Driver crash / SteamVR restart mid-session: existing `training_data.bin` untouched; in-memory session gone; **no resume attempted**. Client sees `driver_training_active=false` next health poll.
- `POST /training/finalize` requires session in `ready` state; returns 409 if `collecting` with insufficient samples. Payload `{"confirm": true}` (preferred) or `{"sensitivity": float, "threshold": float}`.

### D. WAV replay harness (TEST-04) — agent-driven QA optimization
- **Lives in `apps/mic_test/`** — TEST-01 invariant preserved (no SteamVR, no driver, no `vr::*`). CI corpus regression runs without SteamVR.
- **CLI surface** designed for batch / scriptable / agent-driven use:
  - `--replay <wav>` single-file
  - `--replay-dir <dir>` batch mode (recursive)
  - `--expect-triggers <N>` and `--expect-triggers-tolerance <±N>` exit-code assertions
  - `--expect-triggers-from <manifest.json>` per-file expectations
  - `--profile <path>` and `--config <path>` for reproducibility across hosts
  - `--json-output <path>` machine-readable per-file results (schema documented in CONTEXT.md D-30)
  - `--max-duration <s>` bounds CI runtime (default 600 s)
- **Exit codes** well-defined: 0 = pass, 1 = expectation failed, 2 = file unreadable / format unsupported.
- **WAV format policy** lenient (real-world agent corpora are heterogeneous):
  - Stereo → mono downmix (average L+R)
  - Sample-rate mismatch → linear-interpolation resample (deterministic, not pristine; documented trade-off)
  - 16-bit PCM and 32-bit float supported; 8/24-bit return exit 2
  - Long files (> max-duration) rejected with exit 2
- **Replay pacing**: flat-out, no realtime. State-machine `dt` computed from WAV sample count, not wall-clock. 10 s WAV processes in tens of ms; 100-WAV corpus completes in seconds.
- **Determinism**: byte-identical output across runs. PLAN verifies no `steady_clock` calls remain on the deterministic replay path.
- **Corpus seed**: `tests/corpus/replay/positive_001.wav`, `negative_silence_001.wav`, `negative_speech_001.wav`, `manifest.json`, `README.md`. CI invokes `mic_test --replay-dir tests/corpus/replay --expect-triggers-from manifest.json --json-output replay_results.json` on every build; failure = build failure; `replay_results.json` captured as CI artifact.
- **`AssertReplayNoVrApi.cmake`** lint sibling locks down the new replay TUs.

## Plan Wave Layout (rough — planner refines)

Six waves mirroring P8's shape:
- **09-00 (Wave 0):** RED-tolerant scaffolds + lint files in skip-on-not-found mode + EXISTS-gated CTest registrations.
- **09-01 (Wave 1):** `TrainingSession` + `training_io` + DetectionRunner mode-branch + DeviceProvider session ownership.
- **09-02 (Wave 2):** 5 new HTTP routes + `IDriverApi` 5 new methods + `GET /health` `driver_training_active` field.
- **09-03 (Wave 3):** Client UI training section rewire + client-side training body deletion + `AssertNoClientTraining.cmake` enforcing-mode go-live (single-writer cutover).
- **09-04 (Wave 4):** WAV replay harness + WAV decoder vendor + corpus seed + CI invocation + `AssertReplayNoVrApi.cmake` go-live.
- **09-05 (Wave 5):** UAT D-39(1)..(10) on Bigscreen Beyond + Win11 Pro.

## UAT Regimen Summary

10 mandatory UAT steps before phase-complete (full detail in CONTEXT.md D-39):
1. Training round-trip on real hardware (start → cover → finalize → re-init → loads)
2. Cancel mid-session (samples discarded, file untouched)
3. Recompute → preview → finalize (preview replaces, finalize persists)
4. Validation rejection on bad payloads (400 envelope, no state mutation)
5. Orphan timeout (30 s no-sample auto-cancels)
6. Driver-down during training UX (ECONNREFUSED handling)
7. `AssertNoClientTraining` lint go-live (grep returns zero in `apps/micmap/`)
8. CI corpus replay (mic_test --replay-dir exits 0; manifest assertions pass)
9. Replay determinism (3 back-to-back runs byte-identical)
10. Stress: 50 rapid start/cancel cycles (no leaked handles, file integrity)

## Deferred Ideas Captured

All deferred items recorded in `09-CONTEXT.md` `<deferred>` section. Notable:
- TRAIN-D2 (spectral-profile sparkline / A/B preview) → future GUI revamp
- TRAIN-D3 (configurable target sample count) → future
- Persistent training-sample storage → rejected (privacy + cost)
- Realtime-paced replay → rejected (defeats agent-loop optimization)
- High-quality resampling → YAGNI
- 8/24-bit WAV → rejected (>95% of real WAVs are 16-bit/32-bit float)

## Next Steps

`/clear` then `/gsd-plan-phase 9` to decompose into waves with task-level breakdown + verification loop.

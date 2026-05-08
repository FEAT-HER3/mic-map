# Phase 9: Training Migration - Context

**Gathered:** 2026-05-08
**Status:** Ready for planning

<domain>
## Phase Boundary

Driver becomes sole owner of mic + training pipeline end-to-end. Five new endpoints (`POST /training/start`, `GET /training/progress`, `POST /training/finalize`, `POST /training/cancel`, `POST /training/recompute`) on the driver's `cpp-httplib` server. `training_data.bin` writer transfers from client to driver (atomic single-writer cutover, mirrors P8 D-07 for `config.json`). Client demoted to observer: progress polling at 5–10 Hz + preview confirm + Train/Cancel buttons rewired to new endpoints. Client-side training body (`detector->startTraining/addTrainingSample/finishTraining/saveTrainingData` at `apps/micmap/main.cpp:962-1027`) is **deleted** in P9 — not deferred — and a CMake lint (`cmake/AssertNoClientTraining.cmake`, sibling to P8 `AssertNoConfigWriteInClient.cmake`) enforces it. `mic_test.exe --replay` lands optimized for automated agent-driven QA over a user-generated WAV corpus (per user direction): batch directory mode, JSON output, expectation-based exit codes, no realtime pacing, deterministic, no SteamVR/driver dependency (TEST-01 invariant preserved).

**In scope:** new files `driver/src/training_session.{hpp,cpp}` (`TrainingSession` class — `PatternTrainer` host, sample accumulator, recompute preview cache, single-instance mutex), `driver/src/training_io.{hpp,cpp}` (`saveTrainingFile` via `ReplaceFileW` with v1.5 corruption-backup pattern reused), 5 new HTTP routes localhost-only with structured 400 envelope on validation failure, `IDriverApi` 5 new methods (`startTraining`, `getTrainingProgress`, `finalizeTraining`, `cancelTraining`, `recomputeTraining`), `GET /health` JSON gains `driver_training_active: bool`, `DetectionRunner` adds `std::atomic<DriverMode>` member with `Detecting | Training` values that branches the per-iteration consumer (same SPSC ring, same WASAPI thread, same `weak_ptr<State>` UAF guard), bounded 30 s collect-window timeout for orphaned sessions, `DriverMode` propagation via the same atomic-snapshot mechanism (P7 D-15 generalization), client UI training section fully rewired (delete client-side training body, replace buttons with endpoint calls), `mic_test.exe` extended with `--replay <wav>`, `--replay-dir <dir>`, `--expect-triggers <N>`, `--expect-triggers-tolerance <±N>`, `--profile <path>`, `--config <path>`, `--json-output <path>`, `--max-duration <s>` flags, vendored single-header WAV decoder (`dr_wav.h` recommended; planner picks license-clean equivalent), stereo→mono downmix, linear-interpolation resample, initial corpus seed in `tests/corpus/replay/` + `manifest.json`, CI invocation of corpus replay on every build, UAT on Bigscreen Beyond + Win11 Pro.

**Out of scope:** `enable_driver_detection` default flip ON (P10 cutover); `POST /button` deletion + `IDriverClient::tap()` removal (P10 MIG-05); FAIL-01..05 graceful failure UX (P10); HEALTH-08 tray-icon state glyphs (P10); INST-09 installer co-versioning (P10); TEST-01..03/05 (P10) — TEST-04 only owned here; log-file rotation (P10 TEST-03); training-sample raw audio persistence (privacy + disk cost — RAM-only stored sample set, lost on driver restart; explicit decision); resume training across SteamVR restart (too complex, low value); idle-timeout abort beyond the 30 s no-new-sample window (interactive UX, no broader timeout); multi-session concurrent training (single-instance mutex, second start returns 409); high-quality resampling in replay harness (linear interp is enough for regression; perf-grade sinc resampler is YAGNI); detection-accuracy work in noisy environments (DET-01/02 — out of v1.6); `OnDefaultDeviceChanged` device pinning (P10+); `DeviceNotificationClient` ComPtr migration (P10+).

</domain>

<decisions>
## Implementation Decisions

### A. Detection ↔ training arbitration

- **D-01:** New `enum class DriverMode { Detecting, Training }` member on `DetectionRunner` (or `DeviceProvider` — Claude's discretion on ownership; recommend `DeviceProvider` since the HTTP handlers need to flip it and they already have a getter pattern from P7 D-09 / P8 D-23). Single `std::atomic<DriverMode>` member, swap via `store(release)` from HTTP thread, `load(acquire)` at top of every detection loop iteration. Reuses the P7 D-15 atomic-snapshot mechanism shape (same memory-order discipline; matches MIG-06 50 ms propagation guarantee).
- **D-02:** `DetectionRunner` loop branches once per iteration on `DriverMode`:
  - `Detecting` (default): drains ring → `detector_->analyze(block)` → `stateMachine_->update(confidence, dt)` → on rising-edge Triggered, push `TapCommand` to `CommandQueue` (P7 unchanged).
  - `Training`: drains ring → `trainingSession_->addSample(block)` (which delegates to `PatternTrainer::addSample()` from `src/detection/include/micmap/detection/pattern_trainer.hpp`). State machine and `CommandQueue` are NOT touched in Training mode (no triggers fire while training).
- **D-03:** Mode transitions are **HTTP-thread-only**. `POST /training/start` flips `Detecting → Training` (mutex-protected; rejects if already in Training mode with HTTP 409). `POST /training/finalize` and `POST /training/cancel` flip `Training → Detecting`. `POST /training/recompute` does NOT change mode (only valid in `Training` mode with state `ready`). Mode flip is atomic; the next detection-loop iteration observes the new mode within one ring drain cycle (≪ 50 ms in practice).
- **D-04:** **`AudioWorker` is unchanged.** Capture keeps running; ring keeps filling. Mode switch only changes the consumer's branch — single producer, single consumer, no plumbing change. WASAPI lifecycle, `IMMNotificationClient`, `weak_ptr<State>` UAF guard, COM init, all P6/P7 invariants survive verbatim. 50-cycle Init/Cleanup stress test (P7 SC4) extended with mode-flip iterations to verify no regression.
- **D-05:** Anti-feature **TRAIN-AF-01** is enforced **structurally, not behaviorally**: the client's local training body (`apps/micmap/main.cpp:962-1027`) is **deleted** in P9 (the entire "auto-stop / Stop Training / Train Pattern / Clear" block that calls `detector->startTraining()` + `addTrainingSample()` + `finishTraining()` + `saveTrainingData()`). New CMake lint `cmake/AssertNoClientTraining.cmake` greps `apps/micmap/src/` and `apps/micmap/main.cpp` for `\baddTrainingSample\b\|\bfinishTraining\b\|\bstartTraining\b\|\bsaveTrainingData\b` — **must return zero**. Sibling pattern to P8's `AssertNoConfigWriteInClient.cmake`. CI runs on every build. The client never *can* open WASAPI for training because the path is gone, not because it's gated.
- **D-06:** `mic_test.exe` keeps `startTraining` / `addTrainingSample` / `finishTraining` / `saveTrainingData` calls (it's the headless training tool — TEST-01 invariant + dev workflow). The lint scope is **client-side only** (`apps/micmap/`); `apps/mic_test/` is allowlisted. Document in `AssertNoClientTraining.cmake`'s header comment.
- **D-07:** `GET /health` JSON gains a `"driver_training_active": bool` field — `true` iff the driver is in `Training` mode. Pattern mirrors P7 D-09's `driver_detection_active` field exactly (same getter-callback wiring on the HTTP server ctor). Client polls `/health` (already does at 1 Hz from v1.5); when `driver_training_active=true`, the client's "Train Pattern" button is replaced by a "Training in progress…" disabled state with a Cancel button that calls `POST /training/cancel`. When false, normal Train button is offered.
- **D-08:** Client-side detection (still alive until P10) is **not affected by training mode**. The client's local `detector->analyze` path runs from its own `audioCapture` instance — different WASAPI handle than the driver's. The two capture paths coexist in P9 just as they coexist in P7 (Pitfall 10 double-trigger handshake via `driver_detection_active` survives). The training mutex is mic-owner conflict only at the user-experience level (driver's mic-cover during training feeds the trainer; client's mic-cover during training feeds nothing relevant because the client's `addTrainingSample` path is deleted). No additional gating needed.

### B. TrainingSession lifecycle + orphan recovery

- **D-09:** `TrainingSession` is a stateful object owned by `DeviceProvider`. Construction is **lazy** — created on first `POST /training/start`, destroyed on finalize/cancel/timeout. Single-instance mutex enforced by checking the owning `std::unique_ptr<TrainingSession>` is null before construction; concurrent `POST /training/start` while one exists returns HTTP 409 `{"error":"training_in_progress","reason":"another session is active"}` with no state mutation.
- **D-10:** `TrainingSession` states (single linear progression): `collecting → computing → ready → finalized`. `cancelled` is a terminal sibling reachable from any non-finalized state. Observable via `GET /training/progress` `{"state": "..."}` field per REQUIREMENTS.md TRAIN-02.
  - `collecting`: `PatternTrainer::addSample` accepting samples; `samples_collected < target`.
  - `computing`: `PatternTrainer::finishTraining()` running (synchronous; brief; held under session lock).
  - `ready`: thresholds computed, `thresholds_preview` populated; awaiting `POST /training/finalize`.
  - `finalized`: `training_data.bin` persisted; mode flipped back to `Detecting`; session destroyed.
  - `cancelled`: samples discarded; mode flipped back to `Detecting`; session destroyed.
- **D-11:** `target` sample count is `PatternTrainer`'s `TrainingConfig::maxSamples` (default 100 per `pattern_trainer.hpp:28`). Document the value in PLAN.md so client UI's progress bar denominator matches. **NOT** configurable via `POST /training/start` payload in P9 — keeps the endpoint signature minimal. Future-deferred to a `TRAIN-D3` differentiator if real users hit the ceiling.
- **D-12:** **Bounded collect-window timeout — 30 s** of no-new-accepted-sample auto-transitions session from `collecting` to `cancelled` with `last_error="training_timed_out_no_samples"`. Reason: prevent permanent mic-hold from orphaned client (client crashed without sending cancel). 30 s is Claude's discretion — long enough for user thinking-pause, short enough for orphan recovery; planner can tune. Timeout is per-session (resets on every accepted sample), not wall-clock since start. The `samples_accepted == 0` early window has no special-case — same 30 s rule.
- **D-13:** `POST /training/cancel` is **idempotent** — returns HTTP 200 OK whether or not a session is active. Reason: client retry-on-timeout shouldn't 404. Body: `{"cancelled": true|false}` indicating whether an actual cancellation happened.
- **D-14:** Driver crash / SteamVR restart mid-session: existing `training_data.bin` is **untouched** (single-writer `ReplaceFileW` only fires on finalize). On next `DeviceProvider::Init`, P7's read-only profile load (at `driver/src/detection_runner.cpp:160`) loads the prior committed profile. In-memory session state is gone. **No resume attempted** — collected samples lost; client UI sees `driver_training_active=false` next health poll and re-enables Train button. Document in PLAN: agents should not rely on training surviving SteamVR restart.
- **D-15:** `POST /training/finalize` requires the session to be in `ready` state (not `collecting`). If called during `collecting` with `confirm: true` and `samples_collected >= MIN_TRAINING_SAMPLES` (per existing v1.5 client constant `MIN_TRAINING_SAMPLES * 3` at `apps/micmap/main.cpp:962`), driver internally transitions `collecting → computing → ready` first then finalizes. Otherwise returns HTTP 409 `{"error":"insufficient_samples","reason":"need at least N samples"}`. Validation envelope per P8 D-14.
- **D-16:** `POST /training/finalize` payload: `{"confirm": true}` accepts the preview thresholds (preferred path). Alternatively `{"sensitivity": float, "threshold": float}` accepts explicit thresholds (matches REQUIREMENTS.md TRAIN-03 wording). All-or-nothing validation per P8 D-14. Hand-rolled validators in `driver/src/settings_validator.cpp` (extend the file, not new file). On success, calls `INoiseDetector::saveTrainingData(training_data_path)` via `training_io.cpp` wrapper (which adds `ReplaceFileW` atomicity around the existing `noise_detector.cpp:329` save).

### C. Recompute (TRAIN-06) semantics

- **D-17:** Stored sample set is **RAM-only** on the driver — `std::vector<TrainingSample>` member of `TrainingSession` (or `PatternTrainer`'s internal accumulator, depending on whether `PatternTrainer` exposes the raw samples; if not, `TrainingSession` mirrors them as it forwards to `addSample`). **Not persisted to disk** between sessions. Reason: (1) raw mic-cover audio ≈ user voice/room — privacy concern; (2) ~150 samples × FFT bin storage is non-trivial; (3) recompute UX is a within-session affordance — TRAIN-06 says "over the most-recent stored sample set" with no permanence promise. Document in PLAN: recompute lifetime = session lifetime.
- **D-18:** `POST /training/recompute` payload: `{"sensitivity": float}` **only**. Per REQUIREMENTS.md TRAIN-06 verbatim. Range-validated (0.0..1.0); HTTP 400 envelope on out-of-range. Threshold/cooldown/min-duration changes go through P8 `PUT /settings` — not reshapable here.
- **D-19:** Recompute is only valid when session state is `ready`. Returns HTTP 409 if called outside `ready`. Reason: simpler state diagram; preserves the "collect → compute → ready → finalize" linear flow.
- **D-20:** Recompute behavior: re-runs `PatternTrainer::finishTraining()` internals against the cached sample set with the new sensitivity, replaces `thresholds_preview` in-place, returns the new preview in the HTTP 200 response body. Does **NOT** persist `training_data.bin`. Client must call `POST /training/finalize {"confirm": true}` to commit the recomputed preview. This mirrors P8's "preview-then-confirm" UX and satisfies TRAIN-06's "preview the client can confirm or discard" wording.
- **D-21:** Recompute is idempotent within `ready` state — multiple recomputes with different sensitivities just keep replacing the preview. The accepted preview is whichever recompute was most recent before finalize. No history kept (matches P8 D-16 `last_error` simplicity).
- **D-22:** `GET /training/progress` response shape (per REQUIREMENTS.md TRAIN-02 + P9 extensions):
  ```json
  {
    "samples_collected": 42,
    "target": 100,
    "thresholds_preview": {
      "sensitivity": 0.7,
      "energy_threshold": 0.012,
      "spectral_profile_summary": { "mean": 0.03, "stddev": 0.008, "size": 256 }
    } | null,
    "state": "collecting" | "computing" | "ready" | "cancelled",
    "last_error": null | "training_timed_out_no_samples" | "..."
  }
  ```
  `thresholds_preview` is `null` until state is `ready`. `spectral_profile_summary` keeps the wire payload bounded (UI doesn't need the full FFT-bin vector; rendering it is a future-deferred TRAIN-D2 sparkline differentiator).

### D. training_data.bin sole-writer cutover

- **D-23:** **Atomic cutover in P9** — driver becomes sole `training_data.bin` writer the moment `POST /training/finalize` lands. Client write path (`detector->saveTrainingData(configManager->getTrainingDataPath())` at `apps/micmap/main.cpp:618`, `:974`, `:991`) is deleted in the same plan that wires the finalize endpoint. `cmake/AssertNoClientTraining.cmake` lint goes live in the same plan (mirror of P8 single-writer cutover protocol — D-07).
- **D-24:** Driver `Init` is unchanged — already reads `training_data.bin` via the P7 fail-soft path at `detection_runner.cpp:108-172`. Driver re-loads the file after every successful `POST /training/finalize` (or just calls `detector_->loadTrainingData(path)` directly with the just-written file — Claude's discretion; recommend the direct in-memory swap because the file just got written, no need to round-trip through disk).
- **D-25:** Client read of `training_data.bin` was **never wired** in the v1.5 client — the client only writes; the in-memory `INoiseDetector` instance carries the trained profile until process exit. So client read path doesn't need cutover. Verify with `grep -rn 'loadTrainingData' apps/micmap/` — should show only the v1.5-era startup load at `main.cpp:300` (`detector->loadTrainingData(...)` for client-side detection while client-side detection still runs). That path stays alive until P10 deletes client-side detection entirely. P9 does not touch it.
- **D-26:** File path: `%APPDATA%\MicMap\training_data.bin` — unchanged from v1.5. Driver resolves via `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` per the existing P7 path at `detection_runner.cpp:127`. Reuse the helper (extract to a small `appdata_paths.{hpp,cpp}` if it grows; Claude's discretion).
- **D-27:** Persist via `ReplaceFileW` atomicity — wrap `INoiseDetector::saveTrainingData` (which does a plain `std::ofstream` in `noise_detector.cpp:329`) with the v1.5 corruption-backup retention pattern (5-file backup ring). New file `driver/src/training_io.{hpp,cpp}` mirrors `driver/src/config_json.{hpp,cpp}` from P8 in shape — `saveTrainingFile(path, detector*)`, `std::optional<TrainingPersistError>` return, ReplaceFileW + corruption-backup. Reuses the same helper functions where possible.

### E. WAV replay harness (TEST-04) — agent-driven QA optimization

User direction: "the replay harness will be used for future improvements to the detection algorithm. it will be used to enable automated+agent-driven QA given a pool of user-generated wav files. so optimize for that use case." Decisions below flow from this.

- **D-28:** **Lives in `apps/mic_test/`** (TEST-01 invariant — no SteamVR, no driver, no `vr::*`). `mic_test.exe` links `micmap_core_runtime` directly, instantiates `INoiseDetector` + `IStateMachine` from the shared lib factories. Driver is uninvolved. CI runs the corpus regression on every build without needing SteamVR — agent-driven QA loops never touch the driver.
- **D-29:** CLI surface optimized for batch / scriptable / agent-driven use:
  - `mic_test.exe --replay <wav>` — single-file mode; prints trigger events to stdout.
  - `mic_test.exe --replay-dir <dir>` — batch mode over every `.wav` in `dir` (recursive).
  - `mic_test.exe --expect-triggers <N>` — exit code 0 if observed trigger count matches `N`, exit 1 otherwise. CI-friendly assertion.
  - `mic_test.exe --expect-triggers-tolerance <±N>` — allow ±N drift for noisy-corpus regression robustness.
  - `mic_test.exe --expect-triggers-from <manifest.json>` — per-file expectation manifest (recommended for `--replay-dir`); agents read/write the manifest as their corpus evolves.
  - `mic_test.exe --profile <path>` — load specific `training_data.bin` (default: `%APPDATA%\MicMap\training_data.bin`).
  - `mic_test.exe --config <path-to-AppConfig.json>` — load detection settings from JSON file (sensitivity/threshold/cooldown/min-duration), bypass VRSettings + `%APPDATA%` config. Reproducibility across hosts.
  - `mic_test.exe --json-output <path>` — emit machine-readable per-file results (see D-30 for shape). Agents read this back.
  - `mic_test.exe --max-duration <s>` — reject WAVs longer than `s` seconds (default 600). Bounds CI runtime.
  - Combinable: `--replay-dir <d> --expect-triggers-from <m> --json-output <out> --config <c> --profile <p>` is the canonical agent-loop invocation.
- **D-30:** `--json-output` shape (machine-readable; stable for future agent consumption):
  ```json
  {
    "config_path": "...",
    "profile_path": "...",
    "files": [
      {
        "wav": "tests/corpus/replay/positive_001.wav",
        "duration_s": 1.234,
        "sample_rate": 48000,
        "channels": 1,
        "expected_triggers": 1,
        "observed_triggers": 1,
        "tolerance": 0,
        "pass": true,
        "triggers": [
          { "t_s": 0.42, "confidence": 0.81, "state": "Triggered" }
        ]
      }
    ],
    "summary": { "total": 12, "passed": 11, "failed": 1 }
  }
  ```
  Schema is the agent contract. Document in PLAN.md.
- **D-31:** Exit codes — `0` = all pass (or no expectations set; pure dump mode), `1` = at least one expectation failed, `2` = file not found / unreadable / format unsupported. Well-defined for shell-script agent loops.
- **D-32:** WAV format policy (designed for noisy real-world agent inputs, not pristine audio):
  - **Stereo → mono downmix** by averaging L+R. Most consumer mic recorders default to stereo; reject-on-stereo would frustrate agents collecting corpus. Document in PLAN.
  - **Sample-rate mismatch → linear-interpolation resample** to detector's expected rate. Not high-quality (not sinc/polyphase) — purpose is regression-testing the detection pipeline, not preserving audio fidelity. Agents care about repeatability across the corpus; the resample is deterministic. Document the trade-off in PLAN.
  - **Bit depth**: 16-bit PCM and 32-bit float supported (covers >95% of agent-generated WAVs). 24-bit and 8-bit return exit code 2 with a clear stderr message. Document in PLAN.
  - **Duration cap** default 600 s (10 min); configurable via `--max-duration`. Long files = slow agent loops. Reject with exit code 2.
  - WAV decoder: vendor a single-header library (recommended `dr_wav.h`, public-domain / MIT-0; planner picks if licensing changes). Adds zero new build complexity.
- **D-33:** **Replay pacing — flat-out, no realtime.** Process audio at maximum CPU speed; drive `IStateMachine::update(confidence, dt)` with `dt = frame_count / sample_rate` (computed from the WAV, not wall-clock). A 10 s WAV processes in tens of ms. Agent loops over 100-WAV corpus complete in seconds. Realtime pacing would defeat the QA-loop optimization.
- **D-34:** **Determinism.** Same WAV + same profile + same config = same trigger output, byte-identical, every run. State-machine cooldown timers must use injected `dt` (not `steady_clock`) — verify `IStateMachine::update(confidence, dt)` signature in PLAN; if `IStateMachine` internally calls `steady_clock` anywhere in the cooldown logic, that path must be made dt-pure. Document any non-deterministic call sites found during planning.
- **D-35:** **Corpus seed.** P9 ships an initial corpus under `tests/corpus/replay/` with at minimum:
  - `positive_001.wav` — short (~2 s) mic-cover-like white-noise burst, expected 1 trigger.
  - `negative_silence_001.wav` — 5 s silence, expected 0 triggers.
  - `negative_speech_001.wav` — 5 s of speech audio (royalty-free or self-recorded), expected 0 triggers.
  - `manifest.json` — per-file expected triggers + tolerance + notes.
  Initial corpus is a smoke seed; user/agents grow it over time. Document the manifest schema in `tests/corpus/replay/README.md`.
- **D-36:** **CI invocation.** Existing CTest registration adds `mic_test_replay_corpus` test that invokes `mic_test --replay-dir tests/corpus/replay --expect-triggers-from tests/corpus/replay/manifest.json --json-output ${CMAKE_BINARY_DIR}/replay_results.json`. Failure = build failure. The JSON output is captured as a CI artifact for trend analysis.
- **D-37:** No driver dependency anywhere in the replay pipeline. `cmake/AssertReplayNoVrApi.cmake` lint sibling enforces `apps/mic_test/src/wav_replay.{hpp,cpp}` and any new `apps/mic_test/main.cpp` replay sections do not include `<openvr.h>` / `<openvr_driver.h>`. Mirrors P5's `AssertNoOpenVRInCore.cmake` shape. (mic_test as a whole already has this property; the lint just locks it down for the new TUs.)

### F. Plan structure (rough — planner refines)

- **D-38:** Approximate wave layout for the planner (six waves; mirrors P8's six-wave shape):
  - **09-00 (Wave 0):** RED-tolerant scaffolds — `cmake/AssertNoClientTraining.cmake` (initial skip-on-not-found mode), `cmake/AssertReplayNoVrApi.cmake`, headless test scaffolds (`tests/driver/training_session_test.cpp`, `tests/driver/training_endpoint_validation_test.cpp`, `tests/mic_test/wav_replay_test.cpp`), CTest registrations EXISTS-gated. Sibling pattern to P5–P8 Wave 0 conventions.
  - **09-01 (Wave 1):** `driver/src/training_session.{hpp,cpp}` (TrainingSession class — `PatternTrainer` host, sample accumulator, recompute preview cache, single-instance mutex, 30 s timeout watcher) + `driver/src/training_io.{hpp,cpp}` (`saveTrainingFile` via ReplaceFileW + corruption-backup retention); DetectionRunner adds `DriverMode` atomic + per-iteration branch (in-place modification of P7 detection loop); DeviceProvider wires session ownership.
  - **09-02 (Wave 2):** 5 new HTTP routes (`POST /training/start`, `GET /training/progress`, `POST /training/finalize`, `POST /training/cancel`, `POST /training/recompute`) on `driver/src/http_server.cpp`; `IDriverApi` 5 new method declarations + cpp-httplib client implementations in `src/steamvr/`; `GET /health` JSON gains `driver_training_active` field via P7 D-09 callback pattern.
  - **09-03 (Wave 3):** Client UI training section rewire — delete `apps/micmap/main.cpp:962-1027` client-side training body; replace with `IDriverApi::startTraining()` / `getTrainingProgress()` (5 Hz poll) / `finalizeTraining({"confirm":true})` / `cancelTraining()` / `recomputeTraining({"sensitivity":x})`; client-side `detector->saveTrainingData()` call sites at `:618`, `:974`, `:991` deleted; `cmake/AssertNoClientTraining.cmake` switches to enforcing mode (single-writer cutover go-live). Driver-loaded gate (P8 D-09) extended to Train button.
  - **09-04 (Wave 4):** WAV replay harness — vendor WAV decoder, new `apps/mic_test/src/wav_replay.{hpp,cpp}`, extend `apps/mic_test/main.cpp` with all CLI flags from D-29, JSON output schema D-30, exit codes D-31, format policies D-32, deterministic pacing D-33–D-34, corpus seed D-35, CI invocation D-36, `cmake/AssertReplayNoVrApi.cmake` go-live.
  - **09-05 (Wave 5):** UAT on Bigscreen Beyond + Win11 Pro — D-39 regimen below.
- **D-39:** UAT regimen (mandatory before phase-complete; mirrors P7/P8 regimen shape):
  1. **Training round-trip on real hardware.** Click Train in client → driver enters Training mode → cover mic → `samples_collected` advances at 5 Hz client poll → finalize accepts preview → driver writes `training_data.bin` via ReplaceFileW → client closes/reopens → file loads on driver Init → cover mic again → dashboard toggles. End-to-end on Bigscreen Beyond.
  2. **Cancel mid-session.** Click Train → driver enters Training mode → cover mic for 2 s → click Cancel → driver discards samples, returns to Detecting → `training_data.bin` unmodified (compare hash before/after).
  3. **Recompute → preview → finalize.** Train to `ready` → call `POST /training/recompute {"sensitivity":0.5}` via curl → assert response includes new `thresholds_preview` → call `POST /training/finalize {"confirm":true}` → assert persisted profile reflects the recomputed thresholds.
  4. **Validation rejection.** `POST /training/start` with extra fields, `POST /training/finalize` without `confirm`, `POST /training/recompute` with out-of-range sensitivity. Each returns HTTP 400 with structured envelope; driver state unchanged on rejection.
  5. **Orphan timeout.** Start training, wait 30 s without covering mic → assert session auto-cancels with `last_error="training_timed_out_no_samples"`; mode flips back to Detecting.
  6. **Driver-down during training UX.** Start training → kill SteamVR → client detects ECONNREFUSED within 1 s → Train UI re-enables on restart; in-memory session lost; existing `training_data.bin` untouched.
  7. **`AssertNoClientTraining` lint go-live verification.** `grep -rn 'addTrainingSample\|finishTraining\|startTraining\|saveTrainingData' apps/micmap/` returns zero hits in `apps/micmap/`. CI build fails if a future commit reintroduces them.
  8. **CI corpus replay.** `mic_test --replay-dir tests/corpus/replay --expect-triggers-from tests/corpus/replay/manifest.json --json-output replay.json` exits 0; `replay.json` schema validates against D-30; per-file `pass: true` for every seed file.
  9. **Replay determinism.** Run (8) three times back-to-back; trigger timestamps and JSON output byte-identical.
  10. **Stress.** 50 rapid `POST /training/start` → `POST /training/cancel` cycles; Process Explorer shows no leaked file handles, no leaked WASAPI handles; `training_data.bin` integrity preserved (existing v1.5 profile loadable after stress).

### G. Default flag discipline carry-forward

- **D-40:** P9 does **not** flip any default flag. `enable_driver_audio` and `enable_driver_detection` stay default OFF in `default.vrsettings`. P10 (cutover) owns the single point of flag flips. P9's training endpoints are reachable only when `enable_driver_audio=1` (Training mode requires audio capture); when audio is off, `POST /training/start` returns HTTP 503 `{"error":"audio_disabled","reason":"enable_driver_audio is false"}`. Document.

### Claude's Discretion

- Whether `DriverMode` atomic lives on `DeviceProvider` or `DetectionRunner`. Recommend `DeviceProvider` (HTTP handlers already touch it via P7 D-09 / P8 D-23 callback pattern); `DetectionRunner` reads via getter or shared atomic. Pick whichever produces the smallest diff against P7/P8 wiring.
- Whether `TrainingSession` mirrors `PatternTrainer`'s sample accumulator or just delegates to `PatternTrainer::addSample` and trusts its internal storage. Inspect `pattern_trainer.cpp` in PLAN; if `PatternTrainer` already retains the accepted samples for finishTraining(), no mirror needed; if not, `TrainingSession` keeps its own `std::vector<std::vector<float>>`.
- WAV decoder choice — `dr_wav.h` recommended (public-domain, single-header, ~3 KLOC, no deps). Alternatives: `libsndfile` (LGPL — license ladder), `tinywav`, `audiofile`. Planner picks license-clean fit; document choice in PLAN.
- Whether `--expect-triggers-from <manifest>` and `--expect-triggers <N>` are mutually exclusive or stack (later wins) — pick the simpler shape; recommend mutually exclusive with stderr error on collision.
- Resample target sample rate — pick the detector's expected rate; recommend reading from `INoiseDetector::getSampleRate()` if exposed, otherwise hardcode the project's standard 48 kHz with documentation.
- 30 s collect-window timeout value (D-12) — tune empirically during UAT; PLAN documents the chosen value.
- `target` sample count exposure to client — D-22 ships it in `GET /training/progress` for progress bar denominator; recommend NOT making it configurable via `POST /training/start` payload (D-11). Planner can revisit if UAT surfaces the need.
- Recompute preview wire-shape size — `spectral_profile_summary` summary fields (mean / stddev / size) keep the payload bounded vs returning the full spectral-profile vector. If the client UI later needs the full vector for sparkline/visualization (TRAIN-D2), expose it then. P9 ships the summary form.
- WAV-replay output verbosity — `--quiet` for CI-only summary line, `--verbose` for per-trigger stdout; pick the smaller surface and document.

### Folded Todos

None — `gsd-sdk query todo.match-phase 9` returned 0 matches.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 9: Training Migration" — goal (driver sole mic owner during training, client = observer, training_data.bin transfers to driver, mic_test --replay), depends on Phase 7 (driver owns audio) + Phase 8 (IPC surface), 5 Success Criteria (training round-trip, anti-feature TRAIN-AF-01 enforced, cancel/recompute, mic_test --replay corpus, sole-writer training_data.bin), NEEDS VALIDATION research flag (training UX commit/discard pattern designed first principles).
- `.planning/REQUIREMENTS.md` §"Training Migration (TRAIN)" — TRAIN-01..06 (5 endpoints + anti-feature TRAIN-AF-01 single-owner WASAPI + TRAIN-06 recompute differentiator) + §"Test Affordances (TEST)" TEST-04 (mic_test --replay regression harness) + §"IPC Reshape (IPC)" IPC-06 (driver sole writer of training_data.bin).
- `.planning/PROJECT.md` §"Current Milestone" + §"Constraints" — locked stack, training UX commit/discard validation must happen on real hardware, single-installer co-version constraint.
- `.planning/STATE.md` §"Blockers/Concerns" — Phase 9 training UX commit/discard pattern designed from first principles (no v1.5 prior art). P9 is the validate-with-real-training-session phase.

### Pitfall mitigations Phase 9 owns
- `.planning/research/PITFALLS.md` §"Pitfall 5: File-watching `config.json` from the driver" — same hard rule applies to `training_data.bin`. Driver writes only on `POST /training/finalize`. No file-watching. Single-writer rule from P9 onward. **D-23 enforces.**
- `.planning/research/PITFALLS.md` §"Pitfall 7: HTTP server binding `127.0.0.1` only" — P9's 5 new routes inherit P8 D-25 + IPC-07.
- `.planning/research/PITFALLS.md` §"Pitfall 10: Migration boundary states — double-trigger and stale-callsite hazards" — P9 adds `driver_training_active` field to `/health` (mirrors P7 D-09 `driver_detection_active`). Client suppresses local Train button when true. Same migration-handshake pattern. **D-07 enforces.**
- `.planning/research/PITFALLS.md` §"Pitfall 12: WASAPI capture buffer pacing" — P9 reuses P7's SPSC ring + drop-OLDEST policy unchanged; mode switch only changes consumer branch, not producer.
- `.planning/research/PITFALLS.md` §"Pitfall 13: IMMNotificationClient UAF" — P9 inherits P6 D-15/D-16 alive-flag unchanged; no new notifier surface introduced.
- `.planning/research/PITFALLS.md` §"Pitfall 15: Symbol bloat from `nlohmann/json` in three binaries" — P9 adds JSON in `driver/src/http_server.cpp` extension only. `AssertNoJsonInCore.cmake` lint from P8 D-02 still enforces. mic_test's WAV replay does NOT add JSON to the shared lib (JSON output writing in mic_test is acceptable since mic_test is a binary, not the shared lib; but if `--config <path>` reads JSON, that goes in `apps/mic_test/src/` not in `src/core/`).
- `.planning/research/PITFALLS.md` §"Pitfall 11: v1.5 priors" — atomic `ReplaceFileW` + corruption-backup retention pattern (CFG-01..05) reused for `training_data.bin` persistence. **D-27 reuses.**

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 5: Training Migration" (research-numbered = roadmap Phase 9) — research-derived rationale for endpoint shape and driver-as-sole-trainer.
- `.planning/research/ARCHITECTURE.md` — driver vs client thread model post-migration; HTTP handlers mutate atomic state on HTTP thread, never call `vr::*`, never push to `CommandQueue`.
- `.planning/codebase/ARCHITECTURE.md` §"Detection to State Machine to Action" + §"Configuration Persistence" — current v1.5 client-side training flow that P9 reshapes.
- `.planning/codebase/STRUCTURE.md` — `driver/src/`, `apps/micmap/`, `apps/mic_test/`, `src/detection/`, `src/core/` interface layouts.
- `.planning/codebase/STACK.md` — locked stack; P9 adds one new vendored dep (single-header WAV decoder — `dr_wav.h` recommended).
- `.planning/codebase/CONCERNS.md` — Performance Bottleneck #2 (FFT-on-every-frame) flagged but **not addressed in P9** (preserves P7 default; targeted perf phase or backlog).

### Phase boundary inheritance
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` — D-10/D-11 (driver links `micmap_core_runtime` PRIVATE), D-15/D-16 (json + LIB-04 deferred → P8 lifted). P9 reuses; mic_test continues to link `micmap_core_runtime` for the replay path.
- `.planning/phases/06-driver-side-audio-capture-spike/06-CONTEXT.md` — D-04/D-05 AudioWorker apartment trick + D-13/D-14 reverse-order teardown + D-15/D-16 IMMNotificationClient alive-flag (P9 inherits all unchanged; mode switch doesn't perturb audio lifecycle).
- `.planning/phases/07-driver-side-detection-thread/07-CONTEXT.md` — D-09 `/health.driver_detection_active` field pattern (**P9 D-07 mirrors for `driver_training_active`**), D-15 atomic-snapshot publish/load mechanism (**P9 D-01 reuses for DriverMode**), D-17/D-18 DetectionRunner loop shape (**P9 D-02 extends with mode branch**), D-19/D-20 Init/Cleanup ordering (**P9 inherits unchanged; TrainingSession is lazy-constructed within RunFrame, not Init**), D-25 UAT regimen shape (**P9 D-39 mirrors**).
- `.planning/phases/08-ipc-contract-reshape/08-CONTEXT.md` — D-01/D-02 (nlohmann/json driver-only + AssertNoJsonInCore lint — **P9 inherits both**), D-07 atomic single-writer cutover protocol (**P9 D-23 mirrors for training_data.bin**), D-09 optimistic in-memory client apply (**P9 client UI inherits the pattern for finalize-then-update-local-state**), D-14 all-or-nothing validation + structured 400 envelope (**P9 endpoint validation inherits**), D-15 hand-rolled validators in `settings_validator.cpp` (**P9 extends the file**), D-22/D-23 IDriverApi rename + new methods incremental pattern (**P9 D-38 Wave 2 adds 5 new methods**), D-29 wave layout shape (**P9 D-38 mirrors six-wave layout**).

### v1.5 invariants carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. **P9 adds 5 HTTP routes; none push to CommandQueue or call `vr::*`. Boundary unchanged.**
- `.planning/milestones/v1.5-ROADMAP.md` CFG-04 — atomic `ReplaceFileW` save with corruption-backup retention. **P9 D-27 reuses for `training_data.bin`.**

### In-tree code touched by P9
- `driver/src/training_session.{hpp,cpp}` (NEW) — class shape per D-09–D-22; PatternTrainer host; sample accumulator; recompute preview cache; single-instance mutex; 30 s timeout watcher.
- `driver/src/training_io.{hpp,cpp}` (NEW) — `saveTrainingFile(path, detector*)` with ReplaceFileW + corruption-backup retention; mirror shape of `driver/src/config_json.{hpp,cpp}` from P8.
- `driver/src/detection_runner.{hpp,cpp}` — add `DriverMode` atomic read + per-iteration branch on mode; in `Training` mode, drain ring → `trainingSession_->addSample(block)` instead of `detector_->analyze`. Existing P7 detection branch unchanged. Existing P7 `loadTrainingData` at `:160` unchanged.
- `driver/src/device_provider.{hpp,cpp}` — add `std::unique_ptr<TrainingSession>` lazy member; add `std::atomic<DriverMode> mode_`; add 5 HTTP route callbacks; wire single-writer cutover to `training_io.cpp`. Init/Cleanup unchanged in shape (TrainingSession lifecycle is HTTP-driven, not Init/Cleanup-driven).
- `driver/src/http_server.{hpp,cpp}` — extend `SetupRoutes()` with 5 new handlers (`POST /training/start`, `GET /training/progress`, `POST /training/finalize`, `POST /training/cancel`, `POST /training/recompute`); ctor evolves to add 5 new callbacks; `GET /health` JSON gains `driver_training_active` field (extend the v1.5 v.7 D-09 getter pattern).
- `driver/src/settings_validator.cpp` — extend with training payload validators (sensitivity range, confirm-required, etc.).
- `src/steamvr/include/micmap/steamvr/driver_api.hpp` — `IDriverApi` 5 new method declarations (`startTraining`, `getTrainingProgress`, `finalizeTraining`, `cancelTraining`, `recomputeTraining`).
- `src/steamvr/src/driver_api.cpp` — implementations using cpp-httplib client.
- `apps/micmap/main.cpp` — **DELETE** training section at `:962-1027` (the auto-stop check, Stop Training button, Train Pattern button, Clear button, isTraining state, trainingSampleCount counter, all `detector->startTraining/addTrainingSample/finishTraining/saveTrainingData` calls). Replace with new endpoint-driven training pane (Train button → `POST /training/start`, Cancel button → `POST /training/cancel`, progress bar from `GET /training/progress` 5 Hz poll, "Recompute with sensitivity" slider → `POST /training/recompute` → preview → confirm → `POST /training/finalize`). Also delete `detector->saveTrainingData(...)` call sites at `:618` and `:991` (the local-detector save paths on quit / on training-stop).
- `apps/micmap/main.cpp:300` — leave the `detector->loadTrainingData(...)` startup load alone; client-side detection is still alive until P10. Document in PLAN.
- `apps/mic_test/main.cpp` — extend with `--replay`, `--replay-dir`, `--expect-triggers`, `--expect-triggers-tolerance`, `--expect-triggers-from`, `--profile`, `--config`, `--json-output`, `--max-duration` flags per D-29.
- `apps/mic_test/src/wav_replay.{hpp,cpp}` (NEW) — WAV decoder + downmix + linear resample + corpus runner + JSON output writer.
- `vendor/dr_wav/dr_wav.h` (NEW) — single-header WAV decoder; planner verifies license (public-domain / MIT-0).
- `tests/corpus/replay/positive_001.wav`, `negative_silence_001.wav`, `negative_speech_001.wav`, `manifest.json`, `README.md` (NEW) — initial corpus seed.
- `cmake/AssertNoClientTraining.cmake` (NEW) — lint asserting no `addTrainingSample\|finishTraining\|startTraining\|saveTrainingData` in `apps/micmap/` (allowlist `apps/mic_test/`). Sibling to P8's `AssertNoConfigWriteInClient.cmake`.
- `cmake/AssertReplayNoVrApi.cmake` (NEW) — lint asserting `apps/mic_test/src/wav_replay.{hpp,cpp}` does not include `<openvr.h>` / `<openvr_driver.h>`. Sibling to P5's `AssertNoOpenVRInCore.cmake`.
- `tests/driver/training_session_test.cpp` (NEW) — headless lifecycle tests (single-instance mutex, 30 s timeout, recompute preview + finalize confirm, finalize-without-confirm rejection, cancel idempotency).
- `tests/driver/training_endpoint_validation_test.cpp` (NEW) — payload validation envelope tests for all 5 endpoints.
- `tests/mic_test/wav_replay_test.cpp` (NEW) — WAV decoder + resample + downmix + JSON output schema unit tests.

### Reusable shared-lib factories (already in `micmap_core_runtime` from P5)
- `src/detection/include/micmap/detection/pattern_trainer.hpp` — `PatternTrainer` class with `startTraining`, `addSample`, `finishTraining`, `cancelTraining`, `isTraining`, `isComplete`, `getSpectralProfile`, `getEnergyThreshold`, `getStats`, `setProgressCallback`, `getConfig`, `setConfig`. **P9 D-09 hosts an instance inside `TrainingSession`.** The full lifecycle is already implemented in shared lib; P9 just wraps it in HTTP/state-machine plumbing.
- `src/detection/include/micmap/detection/noise_detector.hpp:89,96` — `INoiseDetector::saveTrainingData(path)` + `loadTrainingData(path)`. **P9 D-27 wraps `saveTrainingData` with ReplaceFileW atomicity in `training_io.cpp`.** The serialization itself (`noise_detector.cpp:329`) is reused.
- `src/audio/include/micmap/audio/audio_capture.hpp` — `IAudioCapture` interface unchanged. P9 reuses the AudioWorker→SampleRing→DetectionRunner plumbing from P6/P7 verbatim.
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig` schema (already nlohmann/json-aware in P8). P9 reads sensitivity/threshold/cooldown for the recomputed preview if needed.

### Sister-project reference
- None applicable. Sister project's HMD button stub work is orthogonal to training migration.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/detection/include/micmap/detection/pattern_trainer.hpp` — full `PatternTrainer` class with `startTraining/addSample/finishTraining/cancelTraining/isTraining/isComplete/getSpectralProfile/getEnergyThreshold/getStats/setProgressCallback`. **TrainingSession hosts a `std::unique_ptr<PatternTrainer>` and forwards `addSample` calls from the detection-loop's Training-mode branch.**
- `src/detection/src/noise_detector.cpp:329` — `saveTrainingData(path)` writes the trained profile via `std::ofstream`. **D-27 wraps with ReplaceFileW + corruption-backup retention in `driver/src/training_io.cpp`** — the actual serialization stays in `noise_detector.cpp`.
- `src/detection/src/noise_detector.cpp:376` — `loadTrainingData(path)` reads + validates the profile. **P7 already calls this at `driver/src/detection_runner.cpp:160`. P9 calls it again from `POST /training/finalize` after writing, to swap the in-memory profile (or skips the round-trip and uses `INoiseDetector::setSpectralProfile` if exposed; Claude's discretion).**
- `apps/micmap/main.cpp:962-1027` — entire client-side training section. **DELETED in P9 D-05 (Wave 3).** Becomes endpoint-driven training pane.
- `apps/micmap/main.cpp:300` — `detector->loadTrainingData(configManager->getTrainingDataPath())` startup load for client-side detection. **Survives P9 unchanged**; P10 deletes when client-side detection is removed.
- `apps/micmap/main.cpp:618`, `:974`, `:991` — `detector->saveTrainingData(...)` client-side save call sites. **DELETED in P9 D-05 (Wave 3); single-writer cutover go-live.**
- `driver/src/detection_runner.cpp:108-172` — P7's fail-soft profile load at Init. **P9 unchanged; same path also re-runs after every successful finalize (or in-memory swap; Claude's discretion).**
- `driver/src/detection_runner.cpp:127` — `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` for `%APPDATA%` resolution. **P9 D-26 reuses; consider extracting to `driver/src/appdata_paths.{hpp,cpp}` if it grows beyond two call sites.**
- `driver/src/http_server.{hpp,cpp}` — existing v1.5/P8 cpp-httplib server. **P9 adds 5 new routes via `SetupRoutes()` extension; ctor evolves with 5 new callbacks (training start/progress/finalize/cancel/recompute).**
- `driver/src/settings_validator.cpp` (from P8) — hand-rolled per-field validators returning `std::optional<ValidationError>`. **P9 extends with training payload validators (sensitivity range, confirm-required, etc.).**
- `apps/mic_test/main.cpp:783, :808` — existing `saveTrainingData` / `loadTrainingData` call sites in mic_test for headless training tool. **Preserved unchanged; mic_test is allowlisted by `AssertNoClientTraining.cmake`.**
- `src/steamvr/include/micmap/steamvr/driver_api.hpp` — `IDriverApi` (post-P8 rename). **P9 adds 5 new method declarations.**
- `src/steamvr/src/driver_api.cpp` — `IDriverApi` cpp-httplib client implementations. **P9 adds 5 new method implementations following the P8 pattern.**

### Established Patterns
- **`std::atomic<std::shared_ptr<const T>>` snapshot for live config** — P7 D-15 introduced for `DetectionConfig`; P8 generalized to `AppConfig`. **P9 introduces `std::atomic<DriverMode>` (simpler — atomic-of-enum, no shared_ptr indirection) following the same release/acquire memory-order discipline.**
- **Atomic ReplaceFileW + corruption-backup retention** — v1.5 CFG-04. P8 D-14 reused for `config.json`. **P9 D-27 reuses for `training_data.bin`** via new `driver/src/training_io.{hpp,cpp}`.
- **All-or-nothing validation + structured 400 envelope** — P8 D-14. **P9 inherits for all 5 training endpoints; extends `settings_validator.cpp` with training-specific validators.**
- **Composition-root logger setup** — P8 D-19/D-20/D-21. **P9 inherits unchanged; new TUs use `Logger::info(...)` etc. via the shared lib API; sink choice invisible.**
- **CMake lint as structural guardrail** — P5 `AssertNoOpenVRInCore.cmake`, P6 `AssertAudioWorkerNoVrApi.cmake`, P7 `AssertDetectionRunnerNoVrApi.cmake`, P8 `AssertNoJsonInCore.cmake` + `AssertHttpServerLocalhostOnly.cmake` + `AssertHttpServerNoVrApi.cmake` + `AssertNoConfigWriteInClient.cmake`. **P9 adds `AssertNoClientTraining.cmake` + `AssertReplayNoVrApi.cmake`.** CI runs on every build.
- **HTTP route handler discipline (SVR-05)** — v1.5 invariant; P8 D-24 preserved. **P9 D-03: training endpoints mutate atomic state on HTTP thread; never push to CommandQueue; never call `vr::*`. Boundary unchanged.**
- **Driver-only files under `driver/src/`** — P5/P6/P7/P8 precedent. **P9 adds `training_session.{hpp,cpp}` and `training_io.{hpp,cpp}` under `driver/src/`. nlohmann/json appears only in driver TUs; lint enforces.**
- **Single-writer cutover protocol** — P8 D-07 atomic cutover (driver becomes sole `config.json` writer in same plan that wires PUT /settings; client write path deleted; lint goes live). **P9 D-23 mirrors verbatim for `training_data.bin` (Wave 3).**
- **Migration handshake `/health` field** — P7 D-09 introduced `driver_detection_active`. **P9 D-07 adds `driver_training_active` using the same getter-callback pattern.** P10 deletes both.
- **Optimistic in-memory client apply** — P8 D-09. **P9 client UI's finalize handler optimistically updates the local detector's profile in memory after a 200 OK from `POST /training/finalize` (so client-side detection still alive until P10 picks up the new thresholds within one detection cycle).**

### Integration Points
- `driver/src/device_provider.hpp` — add `std::unique_ptr<TrainingSession> trainingSession_` (lazy member; constructed on first `POST /training/start`, destroyed on finalize/cancel/timeout) + `std::atomic<DriverMode> mode_` + 5 new HTTP route callbacks.
- `driver/src/http_server.hpp` — ctor evolves with 5 new callbacks: `std::function<HttpResult()> trainingStart`, `std::function<TrainingProgress()> trainingProgressGetter`, `std::function<HttpResult(const FinalizePayload&)> trainingFinalize`, `std::function<HttpResult()> trainingCancel`, `std::function<HttpResult(float sensitivity)> trainingRecompute`. `GET /health` callback gets a 5th element for `driver_training_active`.
- `driver/src/detection_runner.hpp` — add `getMode()` accessor + accept a `TrainingSession*` ref (or a `std::function<void(const float*, size_t)>` callback) to forward Training-mode samples. Claude's discretion on the wiring shape.
- `apps/micmap/main.cpp` — Train UI section deletion + replacement; `IDriverApi` calls; 5 Hz progress poll; preview/confirm dialog.
- `apps/mic_test/main.cpp` — argument parsing extension; new `wav_replay.cpp` integration; JSON output writer.
- `apps/mic_test/CMakeLists.txt` — register `wav_replay.cpp` source; vendor `dr_wav.h` (or chosen WAV decoder) include path.
- `cmake/CMakeLists.txt` (root) — include `AssertNoClientTraining.cmake` and `AssertReplayNoVrApi.cmake` after Wave 0 lands (initial skip-on-not-found mode); Wave 3 / Wave 4 toggle them to enforcing mode.
- CI pipeline — extend with `mic_test_replay_corpus` CTest registration that runs `mic_test --replay-dir tests/corpus/replay --expect-triggers-from tests/corpus/replay/manifest.json --json-output ${CMAKE_BINARY_DIR}/replay_results.json`. Capture `replay_results.json` as CI artifact.

</code_context>

<specifics>
## Specific Ideas

- **"Driver becomes the sole training pipeline. Client is the observer."** Single load-bearing sentence for P9. Every other decision flows from it. Client-side training body deleted, lint enforces, `mic_test` allowlisted as the headless tool.
- **Mode-switch architecture, not pause-detection-thread architecture.** DetectionRunner stays alive; same SPSC ring; same WASAPI thread; same UAF guards. Only the per-iteration consumer branch changes (`detector_->analyze` ↔ `trainingSession_->addSample`). Inherits P6/P7 lifecycle correctness for free; no new pause/resume gymnastics.
- **Replay harness is for agent-driven QA, not a developer toy.** Designed for batch directory mode + JSON output + expectation manifests + deterministic flat-out pacing + CI corpus invocation. The user's clarification reframes the whole TEST-04 design: not "replay one WAV manually," but "let agents iterate over corpora and assert detection-algo regression." Optimize accordingly.
- **WAV format leniency over strictness.** Stereo→mono downmix + linear-interp resample + 16/32-bit accept. Real-world agent corpora come from heterogeneous recorders. Reject-on-mismatch frustrates the QA loop. Document the trade-offs (linear resample isn't pristine; that's fine for regression).
- **Recompute is a within-session affordance, not persistent.** RAM-only sample storage. Privacy + simplicity + matches REQUIREMENTS wording ("most-recent stored sample set" with no permanence promise). Document so future improvements don't accidentally persist samples.
- **30 s collect-window timeout, not session-wide timeout.** Resets on every accepted sample. Long thinking pauses are fine; no-input-for-30 s is the orphan signal. Prevents permanent mic-hold without breaking interactive UX.
- **Validation on the all-or-nothing pattern from P8.** Single first-failed-field reporting; no partial state mutation; structured 400 envelope. P9 doesn't relitigate P8's validation shape decisions.
- **`enable_driver_audio` gate on training start.** P10 owns all flag flips; P9 stays default OFF. When `enable_driver_audio=0`, `POST /training/start` returns HTTP 503 — clear stderr message. Document.
- **Determinism is a P9 feature, not nice-to-have.** Replay must be byte-identical across runs. State machine `dt` is computed from WAV sample count, not wall-clock. Verify in PLAN that no `steady_clock` calls remain on the deterministic replay path.

</specifics>

<deferred>
## Deferred Ideas

- **`POST /button` deletion + `IDriverClient::tap()` removal** — **Phase 10 (MIG-05)**. Survives in parallel as v1.5 rollback path through P9.
- **`enable_driver_detection` default flip ON** — **Phase 10 cutover commit.** P9 ships flag default OFF (D-40).
- **Client-side detection deletion** — **Phase 10**. P9 keeps client-side detection alive (with health-poll trigger suppression from P7 D-10) so client UI's confidence/RMS visualization keeps working until P10.
- **Tray-icon state glyphs (HEALTH-08), FAIL-01..05 graceful failure UX, INST-09 installer co-versioning, TEST-01..03/05** — **Phase 10**.
- **Log-file rotation (5 MB cap, 5 retained generations)** — **Phase 10 (TEST-03)**.
- **TRAIN-D2 spectral-profile sparkline / A/B threshold preview** — **future GUI revamp milestone**. P9 ships summary fields only (mean/stddev/size).
- **TRAIN-D3 configurable target sample count via `POST /training/start` payload** — **future**. P9 hardcodes via `PatternTrainer::TrainingConfig` defaults.
- **Persistent training-sample storage for cross-session recompute** — **rejected (privacy + cost)**. RAM-only is the v1.6 shape.
- **Resume training across SteamVR restart** — **rejected (complexity / value)**. Mid-session crash discards collected samples; existing profile preserved.
- **High-quality (sinc / polyphase) WAV resampling in replay harness** — **YAGNI**. Linear interp is enough for regression testing. Document the trade-off.
- **Realtime-paced replay** — **rejected (defeats agent-loop optimization)**. Flat-out only. If a future use case needs realtime (live-mic-substitute development tool), add a `--realtime` flag then.
- **8-bit / 24-bit WAV support** — **rejected (>95% of real WAVs are 16-bit or 32-bit float)**. Exit code 2 with clear stderr message.
- **`/debug/snapshot` driver endpoint (TEST-D2)** — **already deferred per PROJECT.md**.
- **OBS-01 unified driver+client log file** — **future observability milestone**. P8 already split into separate files (`micmap-driver.log` + `micmap.log`).
- **Multi-error aggregation on PUT /training/* validation** — **future**. P9 inherits P8's single-field reporting.
- **Detection-accuracy work in noisy environments (DET-01/02)** — **out of v1.6 scope**.
- **`OnDefaultDeviceChanged` follow-the-default + device pinning** — **Phase 10+**. Pitfall 14 mitigation; not P9 scope.
- **`DeviceNotificationClient` ComPtr migration** — **Phase 10+**. CONCERNS.md flagged.
- **FFT-on-every-frame perf cost** — CONCERNS.md Performance Bottleneck #2. **Not addressed in P9.** Targeted perf phase or backlog. Replay harness will likely surface this if agents start running large corpora.

</deferred>

---

*Phase: 09-training-migration*
*Context gathered: 2026-05-08*

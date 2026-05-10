---
phase: 09-training-migration
verified: 2026-05-08T00:00:00Z
status: passed
score: 9/9 must-haves verified
verdict: PASS
re_verification:
  previous_status: none
  previous_score: n/a
  gaps_closed: []
  gaps_remaining: []
  regressions: []
---

# Phase 9: Training Migration — Verification Report

**Phase Goal:** Driver becomes sole owner of `training_data.bin`; client demoted to observer; 5 new HTTP training endpoints; CI replay corpus harness lands; v1.5 client-side training body deleted (single-writer cutover per IPC-06 / D-05 / D-23).

**Verified:** 2026-05-08
**Status:** PASS
**Re-verification:** No — initial verification.

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | IPC-06 single-writer holds — zero `saveTrainingData` callers under `apps/micmap/` | VERIFIED | Grep across `apps/micmap/`: only two hits in `main.cpp` — line 175 (TRAIN-AF-01 anti-feature comment, no token) and line 1074 (`driverClient->startTraining()` — IPC call to driver, qualifier-prefixed). Zero `saveTrainingData` calls. `cmake/AssertNoClientTraining.cmake` enforces this structurally and runs as ctest. |
| 2 | Driver is sole writer of `training_data.bin` (atomic, ReplaceFileW) | VERIFIED | `driver/src/training_io.cpp:191` defines `saveTrainingFile`; `:120` calls `detector.saveTrainingData(tmp)` then `:132` `ReplaceFileW` for atomic swap on NTFS, with `MoveFileExW` fallback for fresh writes (`:152`) and ACL-preserving replace. Only call site is `driver/src/device_provider.cpp:397`, invoked from the `/training/finalize` route handler. |
| 3 | All 5 HTTP training endpoints exist with validation envelopes | VERIFIED | `driver/src/http_server.cpp:451` (`POST /training/start`), `:475` (`GET /training/progress`), `:514` (`POST /training/finalize`), `:546` (`POST /training/cancel`), `:569` (`POST /training/recompute`). `validateFinalizePayload` (`settings_validator.cpp:102`) + `validateRecomputePayload` (`:160`) emit `{field, reason}` envelopes per P8 D-14. UAT D-39(4) verified all 5 rejection envelopes (4a–4e) on real driver. |
| 4 | `/health` exposes `driver_training_active` + `driver_audio_enabled` | VERIFIED | `driver/src/http_server.cpp:209` (`driver_training_active`) and `:215` (`driver_audio_enabled`). Both populated via getter callbacks following the P7 D-09 pattern. |
| 5 | `mic_test --replay-dir` works + `mic_test_replay_corpus` ctest registered | VERIFIED | Built `mic_test.exe` and ran `mic_test --replay-dir tests/corpus/replay --json-output …` → exit 0, 3/3 PASS, JSON output matches D-30 schema (config_path / files[] / summary present, per-file `wav / duration_s / sample_rate / channels / observed_triggers / pass / triggers`). `ctest -R mic_test_replay_corpus -C Debug` → Passed (0.10 s). Registered in `tests/CMakeLists.txt:923`. |
| 6 | `AssertNoClientTraining` + `AssertReplayNoVrApi` lints registered as ctest and clean | VERIFIED | `ctest -R AssertReplayNoVrApi` → Passed; `ctest -R AssertNoClientTraining` → Passed. Manual `cmake -P …AssertNoClientTraining.cmake -DCLIENT_ROOTS=apps/micmap` → "clean (5 files scanned)"; `…AssertReplayNoVrApi.cmake -DREPLAY_DIR=apps/mic_test/src` → "clean (2 files scanned)". Registrations in `tests/CMakeLists.txt:793` and `:946`. UAT D-39(7) verified the lint catches injected regressions. |
| 7 | Auto-transition Collecting → Ready works (UI-SPEC unblocker) | VERIFIED | `driver/src/training_session.cpp:183` `TrainingSession::maybeAutoCompute()` checks state == Collecting and `samples_collected_ >= target_`, then drives `compute()`. Called from `driver/src/detection_runner.cpp:550` once per detection-loop iteration after `addSample`. Commit `0f27466`. UAT D-39(1) confirmed end-to-end: progress went `samples=2 collecting` → `samples=100 ready` with preview populated within ~3 s. |
| 8 | TrainingSession lifecycle states observable + correct | VERIFIED | `driver/src/training_session.hpp` declares states Collecting / Computing / Ready / Finalized / Cancelled. `device_provider.cpp:334-338` maps each `SessionState::*` → wire string ("collecting" / "computing" / "ready" / "finalized" / "cancelled"); `:322` emits "idle" when no session. UAT D-39(1)..(3) observed all five states transitioning correctly across the full lifecycle on real hardware. `markFinalized` (`:206`), `cancel` (`:196`), and timeout transition (`:175-180`) also wired. |
| 9 | D-40 vrsettings flags restored to false post-UAT | VERIFIED | `driver/resources/settings/default.vrsettings:6` `enable_driver_audio: false`; `:7` `enable_driver_detection: false`. Confirmed by UAT sign-off Sign-Off section (`09-UAT.md:300`) and matches D-40 (P10 owns flag flips). |

**Score:** 9 / 9 truths verified.

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `driver/src/training_session.{hpp,cpp}` | TrainingSession class with full lifecycle | VERIFIED | Lifecycle states + `addSample` / `compute` / `recompute` / `cancel` / `markFinalized` / `tickTimeout` / `maybeAutoCompute` / `snapshot` all present. |
| `driver/src/training_io.{hpp,cpp}` | Atomic `ReplaceFileW` writer for `training_data.bin` | VERIFIED | `saveTrainingFile` at `:191`; `getDriverTrainingDataPath` at `:187`. Used only by `device_provider.cpp:397`. |
| `driver/src/detection_runner.cpp` | DriverMode atomic + per-iteration mode branch | VERIFIED | Acquire-load on `mode_` per loop iteration; Training-mode branch drains ring → `session->addSample(...)` then `session->maybeAutoCompute()`. AssertDetectionRunnerNoVrApi remains green. |
| `driver/src/device_provider.{hpp,cpp}` | Lazy `unique_ptr<TrainingSession>` + 7 callbacks | VERIFIED | `tryStartTrainingSession`, `resetTrainingSession`, training callbacks wired into ctor; D-40 audio_disabled gate (`:286`); D-09 single-instance 409 (`:313`). |
| `driver/src/http_server.{hpp,cpp}` | 5 new routes + /health field | VERIFIED | Routes at `:451 / :475 / :514 / :546 / :569`. `TrainingProgressView` declared in `http_server.hpp:73`. |
| `driver/src/settings_validator.{cpp,hpp}` | Training payload validators | VERIFIED | `validateFinalizePayload` + `validateRecomputePayload`; `{field, reason}` envelope shape preserved. |
| `src/steamvr/include/micmap/steamvr/driver_api.hpp` | 5 new IDriverApi methods | VERIFIED | `startTraining` / `getTrainingProgress` / `finalizeTraining` / `cancelTraining` / `recomputeTraining` all declared at lines 447–473. |
| `apps/micmap/main.cpp` | v1.5 training body deleted; endpoint-driven UI | VERIFIED | Zero `saveTrainingData` / `addTrainingSample` / `finishTraining` callers. New UI uses `driverClient->startTraining/cancelTraining/finalizeTraining/recomputeTraining/getTrainingProgress` (lines 635, 667, 1074, 1184, 1211, 1228, 1244). |
| `apps/mic_test/src/wav_replay.{hpp,cpp}` + `apps/mic_test/main.cpp` | 9 CLI flags + WAV decode | VERIFIED | Built and executed; `--replay-dir` produced D-30-conformant JSON. AssertReplayNoVrApi clean. |
| `vendor/dr_wav/dr_wav.h` | Single-header WAV decoder | VERIFIED | Present at `vendor/dr_wav/dr_wav.h`. |
| `tests/corpus/replay/` | 3 seed WAVs + manifest.json + README.md | VERIFIED | `positive_001.wav`, `negative_silence_001.wav`, `negative_speech_001.wav`, `manifest.json`, `README.md` all present. Manifest schema valid. |
| `cmake/AssertNoClientTraining.cmake` | Source-grep lint, qualifier-prefixed regex | VERIFIED | Lint script + ctest registration at `tests/CMakeLists.txt:946`. UAT D-39(7) demonstrated regression-catch. |
| `cmake/AssertReplayNoVrApi.cmake` | No `<openvr*>` in replay TUs | VERIFIED | Lint script + ctest registration at `tests/CMakeLists.txt:793`. |

---

### Key Link Verification

| From | To | Via | Status |
|------|----|-----|--------|
| `apps/micmap/main.cpp` Train button | `IDriverApi::startTraining` | `driverClient->startTraining()` at `:1074` | WIRED |
| `apps/micmap/main.cpp` progress poll | `GET /training/progress` | `driverClient->getTrainingProgress()` at `:635, :667` | WIRED |
| `POST /training/finalize` handler | `training_io::saveTrainingFile` | `device_provider.cpp:397` | WIRED |
| `DetectionRunner` Training-mode branch | `TrainingSession::addSample` | `detection_runner.cpp:543` | WIRED |
| `DetectionRunner` per-iteration tick | `TrainingSession::maybeAutoCompute` | `detection_runner.cpp:550` | WIRED |
| `/health` route | `driver_training_active` getter | `http_server.cpp:209` | WIRED |

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| AssertNoClientTraining lint clean | `cmake -DCLIENT_ROOTS=apps/micmap -P cmake/AssertNoClientTraining.cmake` | "clean (5 files scanned across 1 roots)" | PASS |
| AssertReplayNoVrApi lint clean | `cmake -DREPLAY_DIR=apps/mic_test/src -P cmake/AssertReplayNoVrApi.cmake` | "clean (2 files scanned)" | PASS |
| Phase 9 ctests pass | `ctest --test-dir build -C Debug -R "AssertNoClientTraining|AssertReplayNoVrApi|mic_test_replay_corpus"` | 3/3 Passed | PASS |
| `mic_test --replay-dir` executes | `mic_test.exe --replay-dir tests/corpus/replay --json-output …` | exit 0; 3/3 PASS; JSON D-30 schema valid | PASS |
| `mic_test` builds | `cmake --build build --config Debug --target mic_test` | `mic_test.exe` produced (LIBCMT warning only, non-fatal) | PASS |

---

### Requirements Coverage

| Requirement | Description | Status | Evidence |
|-------------|-------------|--------|----------|
| TRAIN-01 | `POST /training/start` enters training mode | SATISFIED | `http_server.cpp:451`; UAT D-39(1) PASS |
| TRAIN-02 | `GET /training/progress` returns wire shape | SATISFIED | `http_server.cpp:475`; D-22 shape; UAT D-39(1)..(3) observed all transitions |
| TRAIN-03 | `POST /training/finalize` persists `training_data.bin` | SATISFIED | `http_server.cpp:514`; UAT D-39(1) hash flip + dashboard toggle |
| TRAIN-04 | `POST /training/cancel` discards samples, no file write | SATISFIED | `http_server.cpp:546`; UAT D-39(2) hash unchanged |
| TRAIN-05 / TRAIN-AF-01 | Client never opens its own WASAPI for training (single-owner) | SATISFIED | Client training body deleted; `AssertNoClientTraining` ctest enforces; UAT D-39(7) regression catch |
| TRAIN-06 | `POST /training/recompute` previews thresholds | SATISFIED | `http_server.cpp:569`; UAT D-39(3) sensitivity=0.30 → correlation=0.65 persisted |
| TEST-04 | `mic_test --replay` regression harness + corpus | SATISFIED | `wav_replay.{hpp,cpp}`, 9 CLI flags, 3-WAV seed corpus, ctest registered, D-39(8/9) determinism PASS |
| IPC-06 | Driver sole writer of `training_data.bin` | SATISFIED | `training_io.cpp:191` only writer; lint enforces; UAT hash invariants on D-39(2)/(6)/(10) |

All declared requirements for Phase 9 are SATISFIED.

---

### Anti-Patterns Found

None. Spot-checked the modified files:
- `apps/micmap/main.cpp` — no TODO / FIXME / placeholder pertaining to Phase 9 endpoints; new training pane uses real `driverClient->*` calls.
- `driver/src/training_session.cpp` — full implementation; no stubs.
- `driver/src/training_io.cpp` — full atomic-write implementation including 5-gen corruption-backup ring.

---

### Human Verification Required

None. UAT (D-39 ten cases) was already executed by operator Reavo on Bigscreen Beyond + Win11 Pro 2026-05-09 (`09-UAT.md`); 9 PASS + 1 N/A; zero FAIL.

---

### Deferred Items (out-of-scope per phase boundary; do NOT block)

These items were explicitly carried forward to Phase 10 and acknowledged in `deferred-items.md`. They are NOT Phase 9 regressions.

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Dual correlation-threshold formula in `noise_detector.cpp` (lines 167 vs 450) | Phase 10 | Predates Phase 9 (v1.5 carryover); UAT D-39(3) flagged as latent. |
| 2 | Orphan-timeout user-idle gate (capture-death gate is correct as implemented) | Phase 10 | UAT D-39(5) marked N/A; deferred. |
| 3 | Client paired-life with SteamVR (overlay-app design; survive-restart UX) | Phase 10 | UAT D-39(6) marked PASS w/revised semantics. |
| 4 | `PutSettingsRoundTrip` ctest exit 3 | P8 follow-up | Pre-existing P8 race; not Phase 9 surface. |

---

## Verdict: PASS

Phase 9 has actually delivered what it promised:

- **IPC-06 single-writer cutover is real.** Zero `saveTrainingData` callers remain in `apps/micmap/`; the only writer is `training_io::saveTrainingFile` in the driver, invoked exclusively by the `/training/finalize` handler.
- **All 5 HTTP endpoints are present and validated** with the P8-style structured-400 envelope; UAT exercised every rejection path on real driver runtime.
- **`/health` exposes both `driver_training_active` and `driver_audio_enabled`** matching the P7 D-09 pattern.
- **`mic_test --replay-dir` works end-to-end:** built locally, ran 3-WAV seed corpus, JSON output schema-valid per D-30, exit code semantics correct.
- **Two new lints (`AssertNoClientTraining`, `AssertReplayNoVrApi`) are ctest-registered and clean;** the former even caught the UAT-injected regression at D-39(7).
- **The Collecting → Ready auto-transition** (the UI-SPEC unblocker fix in commit `0f27466`) is wired in `detection_runner.cpp:550` and observable in `training_session.cpp:183`.
- **TrainingSession lifecycle states are correctly mapped to the wire** — five distinct states (Collecting / Computing / Ready / Finalized / Cancelled) with the "idle" sentinel, observed transitioning correctly during UAT D-39(1)..(3).
- **D-40 default-flag discipline preserved** — both `enable_driver_audio` and `enable_driver_detection` restored to `false` post-UAT in `default.vrsettings`.

The Phase 9 hardware UAT (10 D-39 cases on Bigscreen Beyond + Win11 Pro, signed off by Reavo on 2026-05-09) closes the NEEDS-VALIDATION loop with 9 PASS + 1 N/A and zero FAIL dispositions. The single N/A (D-39(5) orphan timeout user-idle gate) is correctly classified as a P10 carryover, not a Phase 9 regression — the implementation guards the *capture-pipeline-death* failure mode (a real and useful invariant), and the *user-walked-away* gate is a separate UX concern owned by P10.

**Recommendation:** Phase 9 is complete. ROADMAP marker for Phase 9 (`.planning/ROADMAP.md:118` currently `[ ]`) can be flipped to `[x]` with a 2026-05-09 completion footnote, and execution can proceed to Phase 10 (Cutover & Cleanup).

---

_Verified: 2026-05-08_
_Verifier: Claude (gsd-verifier, owl perch mica-w1)_

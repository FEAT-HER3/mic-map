---
phase: 09-training-migration
plan: 02
subsystem: api
tags: [phase-9, training-migration, wave-2, http-endpoints, idriverapi, validation, ipc]

# Dependency graph
requires:
  - phase: 09-00
    provides: TrainingEndpointValidation RED scaffold + AssertNoClientTraining lint + AssertReplayNoVrApi lint
  - phase: 09-01
    provides: TrainingSession (driver-side training core) + training_io::saveTrainingFile + DriverMode atomic + DeviceProvider trainingSession_ lifecycle
provides:
  - 5 new driver HTTP routes (POST /training/start, GET /training/progress, POST /training/finalize, POST /training/cancel, POST /training/recompute)
  - GET /health driver_training_active (D-07) AND driver_audio_enabled (warning-fix proactive disable contract)
  - settings_validator FinalizePayload struct + 3 free-function validators (validateFinalizePayload, validateRecomputePayload, validateEmptyOrEmptyObjectBody)
  - IDriverApi 6 new methods (startTraining, getTrainingProgress, finalizeTraining, cancelTraining, recomputeTraining, getHealth) and 4 new types (TrainingProgressView, ThresholdsPreviewView, TrainingResult, HealthView)
  - DetectionRunner::reloadTrainingDataAsync — async on-disk reload path used by D-24 in-memory profile swap (added under deviation Rule 2)
affects: [09-03 (client UI rewire — consumes IDriverApi training methods + HealthView), 09-05 (UAT)]

tech-stack:
  added: []
  patterns:
    - "All-or-nothing JSON validation envelope ({field, reason}) — extended from P8 D-14 to training payloads"
    - "5-state TrainingResult enum (Ok / ValidationFailed / ConnectionFailed / OtherError / Conflict) — Conflict is the new P9 status distinguishing 409 outcomes from generic OtherError"
    - "HttpResult inline-status return type for HTTP-thread DeviceProvider callbacks (avoids vr::* slip; preserves SVR-05)"
    - "Async detector-reload via cv-notified pending-path slot on DetectionRunner — preserves Pitfall 4 thread-affinity for INoiseDetector::loadTrainingData"

key-files:
  created:
    - .planning/phases/09-training-migration/09-02-SUMMARY.md
    - .planning/phases/09-training-migration/deferred-items.md
  modified:
    - driver/src/settings_validator.hpp (FinalizePayload + 3 validator decls + nlohmann json include)
    - driver/src/settings_validator.cpp (3 validator impls; <array> include; reuses fmtFloat)
    - driver/src/http_server.hpp (HttpResult, ThresholdsPreviewView, TrainingProgressView types; FinalizePayload fwd-decl; ctor expanded with 7 new callbacks at end)
    - driver/src/http_server.cpp (5 new route handlers; /health gains driver_training_active + driver_audio_enabled)
    - driver/src/device_provider.hpp (no public surface changes; private helpers reused)
    - driver/src/device_provider.cpp (7 callbacks wired into HttpServer ctor; nlohmann/json.hpp pulled in for body builders)
    - driver/src/detection_runner.hpp + .cpp (reloadTrainingDataAsync — Rule 2 minimal addition for D-24)
    - src/steamvr/include/micmap/steamvr/driver_api.hpp (4 new types: ThresholdsPreviewView, TrainingProgressView, TrainingResult, HealthView; 6 new IDriverApi methods)
    - src/steamvr/src/driver_api.cpp (DriverApi impl of 6 new methods using cpp-httplib client, mirroring putSettings shape)
    - tests/driver/training_endpoint_validation_test.cpp (Wave 0 RED scaffold ctor signature aligned to plan-canonical "append at end" pattern)

key-decisions:
  - "Append 7 new callbacks at the END of the HttpServer ctor parameter list (default nullptr) — preserves byte-compatibility with all existing P7/P8 ctor callsites and tests; the Wave 0 RED scaffold from 09-00 was patched to match this canonical signature"
  - "503 audio_disabled response classified as TrainingResult::OtherError (not Conflict) — Conflict is reserved for 409 outcomes; the UI gates the Train button upstream via HealthView.driver_audio_enabled, so 503 is a defensive belt-and-suspenders"
  - "finalizeTraining read timeout bumped to 1000ms to absorb the driver-side saveTrainingFile + in-memory swap; other training methods stay at 500ms"
  - "D-24 in-memory swap implemented via DetectionRunner::reloadTrainingDataAsync (HTTP thread stores pending path under mu_, notifies cv; detection thread consumes at top of run loop) — preserves Pitfall 4 thread-affinity for INoiseDetector::loadTrainingData"
  - "trainingStart sources sampleRate from AudioWorker::sample_rate() (live WASAPI rate, not cached) and fftSize from getConfigSnapshot()->detection.fftSize so PUT /settings changes propagate to the next training session without driver restart"

patterns-established:
  - "Training HTTP route handler shape: (1) parse body or empty-body validate; (2) validate payload via settings_validator; (3) check callback non-null (503 if not); (4) invoke callback returning HttpResult; (5) copy status+body verbatim onto httplib::Response. SVR-05 invariant preserved across all 5 routes."
  - "Client-side training write method shape (mirrors putSettings): ensureConnected → httplib::Client with set_*_timeout → Post → distinct handling for 200/400/409/503 — 400 and 409 parse {field|error, reason} envelope into result.errorField/errorReason for UI surfacing"

requirements-completed: [TRAIN-01, TRAIN-02, TRAIN-03, TRAIN-04, TRAIN-06]

# Metrics
duration: ~3h (cumulative, plan was partially pre-executed across earlier sessions; final task 4 + summary done in current session)
completed: 2026-05-09
---

# Phase 9 Plan 02: Training HTTP Endpoints + IDriverApi Extension Summary

**Five training HTTP routes land on the driver with all-or-nothing JSON validation envelopes; IDriverApi adds the matching 5 client methods plus HealthView for the migration handshake.**

## Performance

- **Duration:** ~3h cumulative (partial pre-execution across earlier sessions; current session executed Task 4 + verification + SUMMARY)
- **Started:** 2026-05-09T00:08Z (Task 1 commit)
- **Completed:** 2026-05-09T07:34Z
- **Tasks:** 4/4
- **Files modified:** 10 source files + 1 test scaffold patched

## Accomplishments

- 5 new training HTTP routes (start, progress, finalize, cancel, recompute) with structured 400 / 409 / 503 envelopes wired through DeviceProvider callbacks. SVR-05 invariant preserved (no vr::* on HTTP thread).
- /health JSON gains both new fields (driver_training_active per D-07; driver_audio_enabled per the proactive-disable contract).
- IDriverApi adds 5 training methods + getHealth, with the new 5-state TrainingResult enum surfacing Conflict (409) distinctly from OtherError.
- D-24 in-memory profile swap implemented via a thread-affine async detector-reload path on DetectionRunner, so finalize causes the next detection cycle to use the just-trained profile without disk round-trip.
- TrainingEndpointValidation ctest is GREEN (transitioned from RED-build at 09-00) — 6/6 cases including audio_disabled 503, training_in_progress 409, validation envelopes for extra-fields/missing-confirm/out-of-range-sensitivity, and no-state-mutation post-rejection.

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend settings_validator with training payload validators** — `b30817d` (feat)
2. **Task 2: Add 5 training HTTP routes + extend /health** — `da0d0e9` (feat)
3. **Task 3: Wire HttpServer training callbacks in DeviceProvider + D-24 in-memory swap** — `5edc48b` (feat)
4. **Task 4: Extend IDriverApi with 5 training methods + HealthView** — `9144f7f` (feat)

## Files Created/Modified

### Driver-side (driver/src/)

- `settings_validator.{hpp,cpp}` — FinalizePayload struct; 3 free-function validators with first-failed-field {field, reason} envelope. Strict-shape: unknown top-level fields rejected (T-09-02-10 mitigation).
- `http_server.{hpp,cpp}` — Wire types HttpResult/ThresholdsPreviewView/TrainingProgressView; ctor extended with 7 new optional callbacks at end (defaults nullptr, preserving all existing callsites). 5 new route handlers + GET /health extension.
- `device_provider.{hpp,cpp}` — 7 inline lambdas wired into HttpServer ctor: trainingStart (D-09 single-instance + D-40 audio-disabled gate), trainingProgressGetter (D-12 timeout tick + state mapping + auto-reset on terminal), trainingFinalize (D-15 collecting→ready compute + D-16 explicit overrides + saveTrainingFile + Pitfall 2 reset ordering + D-24 in-memory swap), trainingCancel (D-13 idempotent {cancelled:bool}), trainingRecompute (D-19 ready-only + preview body), driverTrainingActiveGetter, driverAudioEnabledGetter.
- `detection_runner.{hpp,cpp}` — reloadTrainingDataAsync (Rule 2 minimal API: HTTP thread stores pending path under mu_; detection thread consumes at top of run loop and dereferences detector_->loadTrainingData on the detection thread per Pitfall 4 thread-affinity).

### Client-side (src/steamvr/)

- `include/micmap/steamvr/driver_api.hpp` — 4 new types (ThresholdsPreviewView, TrainingProgressView, TrainingResult, HealthView); 6 new pure-virtual methods on IDriverApi (startTraining, getTrainingProgress, finalizeTraining, cancelTraining, recomputeTraining, getHealth).
- `src/driver_api.cpp` — DriverApi impl of all 6 new methods. Mirror of putSettings shape: ensureConnected → httplib::Client → Post/Get → distinct status handling. 400 and 409 parse the {field, reason} or {error, reason} envelope into result.errorField/result.errorReason for UI surfacing. 503 audio_disabled body parsed too. finalizeTraining uses 1000ms read timeout to absorb saveTrainingFile + in-memory swap.

### Test scaffold

- `tests/driver/training_endpoint_validation_test.cpp` — patched in Task 2 to align the Wave 0 RED scaffold's HttpServer ctor invocation with the plan-canonical "append at end" signature. The scaffold from 09-00 placed driverAudioEnabledGetter at position 7 (clashing with stateGetter); Task 2 moved it to the end.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 — Bug] Wave 0 RED scaffold ctor mismatch in tests/driver/training_endpoint_validation_test.cpp**

- **Found during:** Task 2.
- **Issue:** The 09-00 test scaffold passed driverAudioEnabledGetter at ctor position 7 (which collides with the existing stateGetter parameter type std::shared_ptr<const DriverState>()). Plan 09-02 explicitly mandates "append 7 new callbacks at the END" of the existing parameter list to preserve all P7/P8 callsites.
- **Fix:** Re-ordered the test's ctor invocation to use the plan-canonical signature (driverAudioEnabledGetter, trainingActiveGetter, trainingStartHandler at the end after deviceLister). Adjusted the trainingStartHandler signature from std::function<std::optional<ValidationError>()> to std::function<HttpResult()> to match the canonical wire shape.
- **Files modified:** tests/driver/training_endpoint_validation_test.cpp.
- **Commit:** da0d0e9.

**2. [Rule 2 — Critical functionality] D-24 in-memory profile swap requires a thread-safe detector reload path**

- **Found during:** Task 3.
- **Issue:** Plan describes that on POST /training/finalize success, the driver should perform an in-memory swap so the next detection cycle picks up the fresh profile without disk round-trip (D-24). But INoiseDetector::loadTrainingData is not thread-safe; calling it from the HTTP thread while the detection thread is reading the detector races (Pitfall 4 — detector is constructed and used on the detection thread).
- **Fix:** Added DetectionRunner::reloadTrainingDataAsync(path) — HTTP thread stores the pending path under mu_ + notifies the detection cv; detection thread consumes the slot at the top of its run loop and calls detector_->loadTrainingData on the detection thread. Pure additive minimal API extension; defaults preserve all existing callers.
- **Files modified:** driver/src/detection_runner.{hpp,cpp}.
- **Commit:** 5edc48b.

**3. [Rule 1 — Bug] Documentation reference to namespaces — none encountered in this plan**

The plan-context note about prior 09-04 deviation #3 (namespace `micmap::core::IStateMachine`, State::Triggered, getCurrentState()) did not affect 09-02 — the only state-machine-related code 09-02 touches is the SessionState transitions inside TrainingSession (delivered by 09-01). No 09-02 file references the detection state machine directly. Recorded for completeness only; no action taken.

### Out-of-Scope Discoveries

**Pre-existing PutSettingsRoundTrip ctest failure** — recorded in `deferred-items.md`. The test exits with code 3 (uncaught exception, likely nlohmann::json::parse on a not-yet-flushed config file). Reproduces identically on HEAD before Task 4 edits. No 09-02 file is on the failing path. Belongs in a P8 follow-up plan.

## Verification

- `cmake --build build --target driver_micmap` — succeeds (driver DLL builds with all 5 new HTTP routes wired).
- `cmake --build build --target test_training_endpoint_validation` — succeeds.
- `ctest --test-dir build -C Debug -R TrainingEndpointValidation` — **PASSES** (1/1; transitions the Wave 0 RED scaffold from 09-00 to GREEN).
- `cmake -DHTTP_SERVER_DIR=driver/src -P cmake/AssertHttpServerNoVrApi.cmake` — clean (2 files scanned).
- `cmake -DHTTP_SERVER_DIR=driver/src -P cmake/AssertHttpServerLocalhostOnly.cmake` — clean (2 files scanned).
- `cmake -DSRC_ROOTS="src/audio;src/detection;src/common" -P cmake/AssertNoJsonInCore.cmake` — clean (21 files scanned).
- `cmake --build build --target micmap` — succeeds (client target builds with the new IDriverApi methods + HealthView).

## Threat Surface Scan

All threats from the plan's `<threat_model>` STRIDE register are addressed by the implementation:

- T-09-02-01 (Tampering on /training/finalize body) — validateFinalizePayload returns first-failed-field envelope; nlohmann::json::parse exception caught → 400 (structural).
- T-09-02-02 (Tampering on /training/recompute sensitivity) — validateRecomputePayload range-checks 0.0..1.0 + std::isfinite.
- T-09-02-03 (Spoofing) — IPC-07 inheritance via AssertHttpServerLocalhostOnly lint (clean above).
- T-09-02-04 (DoS via concurrent /training/start) — tryStartTrainingSession returns false on existing session → 409 envelope.
- T-09-02-05 (DoS via malformed JSON) — every parse wrapped in try/catch with structural envelope returned.
- T-09-02-06 (EoP via vr::* slip on HTTP thread) — AssertHttpServerNoVrApi lint clean above.
- T-09-02-07 (EoP via concurrent finalize + detection) — DeviceProvider::resetTrainingSession release-stores mode_=Detecting BEFORE resetting unique_ptr.
- T-09-02-08 (Information disclosure on thresholds_preview) — accepted; spectral_profile_summary returns only mean/stddev/size.
- T-09-02-09 (Repudiation on finalize without disk write) — saveTrainingFile false → HttpResult{500, "{...persist_failed...}"}; session stays in Ready, allowing retry.
- T-09-02-10 (Tampering via extra fields on no-body endpoints) — validateEmptyOrEmptyObjectBody requires "" or "{}" exactly.

No NEW security-relevant surface introduced beyond the threat register.

## Self-Check: PASSED

### File existence verification

- driver/src/settings_validator.hpp — FOUND
- driver/src/settings_validator.cpp — FOUND
- driver/src/http_server.hpp — FOUND
- driver/src/http_server.cpp — FOUND
- driver/src/device_provider.hpp — FOUND
- driver/src/device_provider.cpp — FOUND
- driver/src/detection_runner.hpp — FOUND
- driver/src/detection_runner.cpp — FOUND
- src/steamvr/include/micmap/steamvr/driver_api.hpp — FOUND
- src/steamvr/src/driver_api.cpp — FOUND

### Commit verification

- b30817d (Task 1: settings_validator) — FOUND
- da0d0e9 (Task 2: http_server routes) — FOUND
- 5edc48b (Task 3: device_provider wiring) — FOUND
- 9144f7f (Task 4: IDriverApi extension) — FOUND

### Test verification

- TrainingEndpointValidation ctest — PASSES (1/1, 0.40s).

### Lint verification

- AssertHttpServerNoVrApi — clean.
- AssertHttpServerLocalhostOnly — clean.
- AssertNoJsonInCore — clean.

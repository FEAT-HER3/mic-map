---
phase: 09-training-migration
plan: 00
subsystem: build / lints / test-scaffolds
tags: [phase-9, training-migration, wave-0, lints, red-tolerant]
dependency_graph:
  requires:
    - "tests/CMakeLists.txt P5/P6/P7/P8 EXISTS-gated source-list pattern (lines 197-580)"
    - "cmake/AssertNoConfigWriteInClient.cmake (P8 D-07) — verbatim shape for AssertNoClientTraining (GLOB_RECURSE form)"
    - "cmake/AssertDetectionRunnerNoVrApi.cmake (P7 D-22) — verbatim shape for AssertReplayNoVrApi (explicit-target form)"
    - "tests/driver/detection_settings_propagation_test.cpp + tests/test_settings_validator.cpp — plain-main + MM_CHECK convention"
    - "tests/driver/put_settings_validation_test.cpp — HTTP scaffold pattern (configGetter / configMutator stubs, port-isolation, httplib::Client)"
  provides:
    - "2 new CMake source-grep lints under cmake/ (AssertNoClientTraining, AssertReplayNoVrApi)"
    - "3 new RED-tolerant test scaffolds (training_session_test, training_endpoint_validation_test, wav_replay_test)"
    - "4 new ctest registrations: AssertReplayNoVrApi + 3 EXISTS-gated test exes (TrainingSessionLifecycle, TrainingEndpointValidation, WavReplayHarness)"
    - "Deferred AssertNoClientTraining ctest registration (go-live in 09-03 alongside client-body deletion per D-23 single-writer cutover)"
  affects:
    - "tests/CMakeLists.txt — appended ~120 lines as Phase 9 Wave 0 RED scaffold block"
tech_stack:
  added: []
  patterns:
    - "Wave 0 RED-tolerant lints (skip-on-NOT-EXISTS — AssertReplayNoVrApi)"
    - "Wave 0 RED-tolerant lints (GLOB_RECURSE with allowlist-guard — AssertNoClientTraining)"
    - "Wave 0 RED-tolerant test scaffolds (EXISTS-gated source list — mirrors P7/P8)"
    - "Single-writer cutover protocol (deferred ctest registration until 09-03 — mirrors P8 D-07 timing)"
key_files:
  created:
    - "cmake/AssertNoClientTraining.cmake"
    - "cmake/AssertReplayNoVrApi.cmake"
    - "tests/driver/training_session_test.cpp"
    - "tests/driver/training_endpoint_validation_test.cpp"
    - "tests/mic_test/wav_replay_test.cpp"
  modified:
    - "tests/CMakeLists.txt"
decisions:
  - "AssertNoClientTraining uses [^a-zA-Z0-9_]TOKEN | ^TOKEN regex form (Rule 1 fix during Task 1 verification): CMake regex (POSIX-like) does not support \\b word-boundary, so the plan's literal `\\baddTrainingSample\\b` form silently failed to match anything. Switched to the same idiom cmake/lint_no_openvr_in_core.cmake uses for `vr::` — non-identifier prefix OR start-of-file. Lint now correctly FATALs on apps/micmap/main.cpp:404,618,962-1027 (verified)."
  - "AssertNoClientTraining ctest registration deferred to 09-03 per D-23 single-writer cutover (mirrors P8 D-07 timing for AssertNoConfigWriteInClient). Lint script lives in the repo at Wave 0; ctest line gets uncommented after the v1.5 client training body is deleted."
  - "wav_replay_test scaffold uses inline RIFF/WAVE writer (44-byte canonical header + raw byte payload via std::ofstream binary). No external WAV fixtures required — fixtures generated at test run-time in std::filesystem::temp_directory_path() / 'micmap_p9_replay_test'."
  - "TrainingEndpointValidation source-list keyed on EXISTS(training_session.cpp) AND EXISTS(http_server.cpp) — pulls in all the P7/P8 device_provider transitive TUs (audio_worker, detection_runner, config_io, config_json, settings_validator, driver_log_sink) so once 09-01/09-02 land, the test exe links cleanly."
  - "WavReplayHarness has NO if(OpenVR_FOUND) wrapper — the replay harness is headless per TEST-01 invariant. EXISTS-gate on wav_replay.cpp keeps the executable buildable at Wave 0 (the missing #include on wav_replay.hpp is the build-time RED gate per D-37)."
metrics:
  duration_minutes: 12
  completed_date: "2026-05-08"
  commits: 3
  files_created: 5
  files_modified: 1
  tasks: 3
---

# Phase 9 Plan 00: Wave 0 RED Scaffold Summary

Two new structural-guardrail lints (single-trainer rule, no-OpenVR-in-replay-harness) and 3 plain-main test scaffolds landed; Phase 9 Wave 0 ctest block wired into tests/CMakeLists.txt. cmake configure stays green via EXISTS-gated source lists even though the production impl files (training_session.{hpp,cpp}, http_server training routes, wav_replay.{hpp,cpp}) do not yet exist — those build-time compile failures are the Nyquist gate for the rest of Phase 9.

## What was delivered

### 2 CMake source-grep lints (Task 1)

| Script | Form | Scope | Wave 0 Status |
|--------|------|-------|---------------|
| `AssertNoClientTraining.cmake` | GLOB_RECURSE / CLIENT_ROOTS + allowlist guard | apps/micmap (canonical); apps/mic_test exempt per D-06 | FATALs on apps/micmap/main.cpp (expected — ctest deferred to 09-03) |
| `AssertReplayNoVrApi.cmake` | explicit-target / REPLAY_DIR + skip-on-NOT-EXISTS | apps/mic_test/src/wav_replay.{hpp,cpp} | clean (0 files scanned) — ctest GREEN at Wave 0 |

`AssertNoClientTraining` mirrors the verbatim shape of `cmake/AssertNoConfigWriteInClient.cmake` (P8 D-07). 4-condition forbidden-pattern disjunction over `addTrainingSample`, `finishTraining`, `startTraining`, `saveTrainingData`. Allowlist guard (`if(_dir MATCHES "apps/mic_test") continue()`) keeps the headless training tool exempt under TEST-01 even if the canonical CLIENT_ROOTS scope is widened by mistake.

`AssertReplayNoVrApi` mirrors the verbatim shape of `cmake/AssertDetectionRunnerNoVrApi.cmake` (P7 D-22). Same 3 OpenVR forbidden-pattern regexes (`[<\"]openvr[a-z_]*\\.h[>\"]`, `[^a-zA-Z0-9_]vr::`, `^vr::`). Two-file explicit target list pinned per CONTEXT D-37.

### 3 RED-tolerant test scaffolds (Task 2)

All scaffolds follow the plain-main + MM_CHECK convention from `tests/driver/detection_settings_propagation_test.cpp`. Each `#include`s a not-yet-existing header — the build-time compile failure is the Nyquist gate per CONTEXT 09-VALIDATION.md.

| File | Requirement | RED until | Awaits impl |
|------|-------------|-----------|-------------|
| `tests/driver/training_session_test.cpp` | TRAIN-01..04, TRAIN-06 | 09-01 | `driver/src/training_session.{hpp,cpp}` (lifecycle FSM, recompute, tickTimeout, validateFinalizePayload) |
| `tests/driver/training_endpoint_validation_test.cpp` | IPC-06 / D-09 / D-14 / D-15 / D-18 / D-40 | 09-02 | `driver/src/http_server.{hpp,cpp}` extension (5 training routes) + settings_validator extension |
| `tests/mic_test/wav_replay_test.cpp` | TEST-04 / D-30 / D-32 / D-34 | 09-04 | `apps/mic_test/src/wav_replay.{hpp,cpp}` + `vendor/dr_wav/dr_wav.h` |

**training_session_test.cpp** — 5 cases (16 MM_CHECK assertions): construction state, addSample → compute Ready transition, recompute(0.5f) preview refresh, 30 s timeout watchdog (`tickTimeout`) firing `training_timed_out_no_samples`, validateFinalizePayload(confirm=false) rejection.

**training_endpoint_validation_test.cpp** — 6 cases (24 MM_CHECK assertions): extra-fields → 400, missing confirm → 400 (field=="confirm"), out-of-range sensitivity → 400 (field=="sensitivity"), audio_disabled → 503, training_in_progress → 409, no-state-mutation post-rejection. Uses port 27140 to avoid collision with P8 (27120).

**wav_replay_test.cpp** — 7 cases (24 MM_CHECK assertions): 1 s mono 48k 16-bit happy path; stereo 32-bit float downmix to mono; 44.1k → 48k linear-resample frame count; 8-bit reject (exit_code 2 per D-32); truncated 1-hour header + max_duration_s 600 reject; determinism across 3 replays (D-34); JSON output schema (config_path / profile_path / files / summary per D-30). Inline RIFF/WAVE writer generates fixtures in `temp_directory_path()/micmap_p9_replay_test`.

### ctest wiring (Task 3)

Appended `# ---- Phase 9 Wave 0 (RED scaffold) ----` block (~120 lines) to `tests/CMakeLists.txt`. Registers:
- 1 lint ctest: `AssertReplayNoVrApi`
- 3 test-exe registrations under EXISTS-gated source lists:
  - `TrainingSessionLifecycle` — gated on `OpenVR_FOUND`; sources expand once `driver/src/training_session.cpp` and `driver/src/training_io.cpp` exist
  - `TrainingEndpointValidation` — gated on `OpenVR_FOUND`; sources expand once `driver/src/training_session.cpp` AND `driver/src/http_server.cpp` exist (full P7/P8 transitive driver-TU set)
  - `WavReplayHarness` — NO `if(OpenVR_FOUND)` wrapper (headless per TEST-01); sources expand once `apps/mic_test/src/wav_replay.cpp` exists

`AssertNoClientTraining` ctest registration deliberately deferred per D-23 (would FATAL on existing v1.5 main.cpp). Comment block in tests/CMakeLists.txt documents the deferral and points to 09-03 as the go-live plan.

## Commits

| # | Hash | Message |
|---|------|---------|
| 1 | `b7858c5` | feat(09-00): add AssertNoClientTraining + AssertReplayNoVrApi lint scripts |
| 2 | `355c919` | test(09-00): add 3 RED-tolerant test scaffolds (training session, training endpoints, WAV replay) |
| 3 | `447cd8e` | build(09-00): wire AssertReplayNoVrApi + 3 P9 RED scaffolds into ctest |

## Verification

- `cmake -DREPLAY_DIR=apps/mic_test/src -P cmake/AssertReplayNoVrApi.cmake` → `-- AssertReplayNoVrApi: clean (0 files scanned)` (Wave 0 — wav_replay.{hpp,cpp} not yet authored).
- `cmake -DCLIENT_ROOTS=apps/micmap -P cmake/AssertNoClientTraining.cmake` → FATAL (1 file: apps/micmap/main.cpp). Expected — this is precisely why the ctest registration is deferred to 09-03 per D-23.
- `cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug` configures cleanly (no `CMake Error` / `FATAL`).
- `ctest -N --test-dir build | grep -E "AssertReplayNoVrApi|TrainingSessionLifecycle|TrainingEndpointValidation|WavReplayHarness"` → all 4 entries listed (test #36–#39).
- `AssertNoClientTraining` correctly absent from the ctest list at Wave 0.
- 3 scaffold files include not-yet-existing headers → build-time RED gate intact (`training_session.hpp`, `http_server.hpp` extension, `wav_replay.hpp`).
- Each scaffold has the `Phase 9 Wave 0 RED scaffold` banner string and ≥5 MM_CHECK assertions (training_session: 16; training_endpoint_validation: 24; wav_replay: 24).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] AssertNoClientTraining regex used `\b` word-boundary (rejected by CMake regex engine)**

- **Found during:** Task 1 cmake -P verification (initial Wave-0 smoke against `apps/micmap`).
- **Issue:** The plan's spec text specified the verbatim regex set `_content MATCHES "\\baddTrainingSample\\b"` (etc.). CMake regex (POSIX-like) does not support `\b` word-boundary — it parses `\b` as an "Invalid character escape." The lint silently scanned 5 files but matched zero violations, falsely reporting "clean" against `apps/micmap` even though `main.cpp:404` clearly has `detector->addTrainingSample(...)`.
- **Fix:** Switched to `[^a-zA-Z0-9_]TOKEN | ^TOKEN` form — same idiom `cmake/lint_no_openvr_in_core.cmake` uses for `vr::`. 8-condition disjunction (4 tokens × 2 prefix variants). Now correctly FATALs on apps/micmap/main.cpp:404, 618, 962-1027.
- **Files modified:** `cmake/AssertNoClientTraining.cmake`
- **Commit:** `b7858c5` (incorporated into Task 1's atomic commit since the bug was discovered and fixed during initial verification of that task's deliverable, before the commit landed).

### Deferred Items (out-of-scope, intentional)

None — Wave 0 scope was fully achievable with no deferred items. The `AssertNoClientTraining` ctest deferral is **not** a deferred item but rather an intentional plan-encoded deferral per D-23 (single-writer cutover protocol; ctest go-live happens in 09-03 alongside client-body deletion).

## Self-Check

**Files claimed created:**
- cmake/AssertNoClientTraining.cmake — FOUND
- cmake/AssertReplayNoVrApi.cmake — FOUND
- tests/driver/training_session_test.cpp — FOUND
- tests/driver/training_endpoint_validation_test.cpp — FOUND
- tests/mic_test/wav_replay_test.cpp — FOUND

**Files claimed modified:**
- tests/CMakeLists.txt — FOUND (appended Phase 9 Wave 0 block, ~120 lines)

**Commits claimed:**
- b7858c5 — FOUND
- 355c919 — FOUND
- 447cd8e — FOUND

## Self-Check: PASSED

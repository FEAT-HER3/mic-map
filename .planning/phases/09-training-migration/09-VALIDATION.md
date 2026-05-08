---
phase: 9
slug: training-migration
status: approved
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-08
revised: 2026-05-08
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake) + ad-hoc PowerShell harness; mic_test.exe --replay for regression corpus |
| **Config file** | `CMakeLists.txt` (test targets), `tests/replay-corpus/` (WAV seed corpus) |
| **Quick run command** | `cmake --build build --target test_unit && ctest --test-dir build -L unit --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure && build/bin/mic_test.exe --replay tests/replay-corpus/*.wav` |
| **Estimated runtime** | ~30 seconds (unit) + ~10 seconds (replay corpus) |

---

## Sampling Rate

- **After every task commit:** Run quick command (unit tests)
- **After every plan wave:** Run full suite (unit + replay regression)
- **Before `/gsd-verify-work`:** Full suite must be green + manual hardware-loop sign-off (Bigscreen Beyond + Win11)
- **Max feedback latency:** 40 seconds

---

## Per-Task Verification Map

> Populated 2026-05-08 — every automated task across 09-00..09-04 mapped to its requirement, threat ref, secure behavior, test type, and exact verify command. Manual checkpoint task 09-05 lives under "Manual-Only Verifications" below.

| Task ID    | Plan  | Wave | Requirement                          | Threat Ref                                                                | Secure Behavior                                                                          | Test Type                | Automated Command                                                                                                                                                                                                                       | File Exists | Status     |
|------------|-------|------|--------------------------------------|---------------------------------------------------------------------------|------------------------------------------------------------------------------------------|--------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------|------------|
| 09-00 T1   | 09-00 | 0    | TRAIN-AF-01, TEST-01                 | T-09-00-01, T-09-00-02                                                    | CMake source-grep lints exist with allowlist + skip-on-NOT-EXISTS RED-tolerance          | unit (cmake -P)          | `cmake -P cmake/AssertReplayNoVrApi.cmake -DREPLAY_DIR=apps/mic_test/src 2>&1 \| grep -E "FATAL\|clean"`                                                                                                                                | ❌ W0       | ⬜ pending |
| 09-00 T2   | 09-00 | 0    | TRAIN-01..04, TRAIN-06, TEST-04      | T-09-00-04, T-09-00-05                                                    | Plain-main MM_CHECK RED scaffold; build-time include of not-yet-existing header is gate | unit (file existence)    | `test -f tests/driver/training_session_test.cpp && test -f tests/driver/training_endpoint_validation_test.cpp && test -f tests/mic_test/wav_replay_test.cpp && grep -q "MM_CHECK" tests/driver/training_session_test.cpp`              | ❌ W0       | ⬜ pending |
| 09-00 T3   | 09-00 | 0    | TEST-01, IPC-06                      | T-09-00-03                                                                | EXISTS-gated source lists keep cmake configure GREEN at Wave 0                          | integration (cmake -B)   | `cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug 2>&1 \| grep -E "AssertReplayNoVrApi\|TrainingSessionLifecycle\|TrainingEndpointValidation\|WavReplayHarness\|FATAL"`                                                                       | ❌ W0       | ⬜ pending |
| 09-01 T1   | 09-01 | 1    | IPC-06, TRAIN-05                     | T-09-01-01, T-09-01-04                                                    | Atomic ReplaceFileW + 5-gen corruption-backup rotation; binary format byte-for-byte     | unit (grep + build)      | `test -f driver/src/training_io.hpp && grep -q "saveTrainingFile" driver/src/training_io.hpp && grep -q "ReplaceFileW" driver/src/training_io.cpp && grep -q "training_data.bin.corrupted" driver/src/training_io.cpp && ! grep -q "nlohmann/json" driver/src/training_io.hpp` | ❌ W0       | ⬜ pending |
| 09-01 T2   | 09-01 | 1    | TRAIN-01..04, TRAIN-06               | T-09-01-02, T-09-01-03, T-09-01-05                                        | Single-instance via DeviceProvider null-check; 30s timeout; ZERO OpenVR symbols          | unit (grep + lint)       | `test -f driver/src/training_session.hpp && grep -q "class TrainingSession" driver/src/training_session.hpp && grep -q "training_timed_out_no_samples" driver/src/training_session.cpp && ! grep -qE "<openvr.h>\|<openvr_driver.h>\|vr::" driver/src/training_session.cpp` | ❌ W0       | ⬜ pending |
| 09-01 T3   | 09-01 | 1    | TRAIN-01, TRAIN-04, IPC-06           | T-09-01-05, T-09-01-06                                                    | Mode-branch via release-acquire atomic; AssertDetectionRunnerNoVrApi stays GREEN         | unit (grep + lint)       | `test -f driver/src/driver_mode.hpp && grep -q "DriverMode::Training" driver/src/detection_runner.cpp && grep -q "memory_order_acquire" driver/src/detection_runner.cpp && cmake -P cmake/AssertDetectionRunnerNoVrApi.cmake -DDETECTION_RUNNER_DIR=driver/src 2>&1 \| grep -q "clean"` | ❌ W0       | ⬜ pending |
| 09-01 T4   | 09-01 | 1    | TRAIN-01..04, IPC-06                 | T-09-01-05                                                                | Lazy unique_ptr<TrainingSession> guarded by trainingMutex_; release-store on mode flip   | integration (cmake build)| `grep -q "tryStartTrainingSession" driver/src/device_provider.cpp && grep -q "resetTrainingSession" driver/src/device_provider.cpp && grep -q "training_session.cpp" driver/CMakeLists.txt && cmake -B build -S . && cmake --build build --target driver_micmap 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true` | ❌ W0       | ⬜ pending |
| 09-02 T1   | 09-02 | 2    | TRAIN-01, TRAIN-02, TRAIN-06         | T-09-02-01, T-09-02-02, T-09-02-10                                        | First-failed-field {field, reason} envelope; strict shape rejection                      | unit (grep + build)      | `grep -q "validateFinalizePayload" driver/src/settings_validator.hpp && grep -q "FinalizePayload" driver/src/settings_validator.hpp && grep -q 'must be in \[0.0, 1.0\]' driver/src/settings_validator.cpp && cmake --build build --target driver_micmap 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true` | ❌ W0       | ⬜ pending |
| 09-02 T2   | 09-02 | 2    | TRAIN-01..06, IPC-06                 | T-09-02-03, T-09-02-05, T-09-02-06                                        | 5 new HTTP routes on 127.0.0.1; AssertHttpServerNoVrApi + LocalhostOnly enforce           | unit (grep + lint)       | `grep -q "/training/start" driver/src/http_server.cpp && grep -q "/training/finalize" driver/src/http_server.cpp && grep -q "driver_training_active" driver/src/http_server.cpp && cmake -P cmake/AssertHttpServerNoVrApi.cmake -DHTTP_SERVER_DIR=driver/src 2>&1 \| grep -q "clean" && cmake -P cmake/AssertHttpServerLocalhostOnly.cmake -DHTTP_SERVER_DIR=driver/src 2>&1 \| grep -q "clean"` | ❌ W0       | ⬜ pending |
| 09-02 T3   | 09-02 | 2    | TRAIN-01..06, IPC-06                 | T-09-02-04, T-09-02-07, T-09-02-09                                        | 6 callbacks wired; D-40 audio_disabled gate; D-09 single-instance 409 envelope            | integration (ctest)      | `grep -q "trainingStart" driver/src/device_provider.cpp && grep -q "audio_disabled" driver/src/device_provider.cpp && grep -q "training_in_progress" driver/src/device_provider.cpp && cmake --build build --target test_training_endpoint_validation 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true; ctest --test-dir build -R "TrainingEndpointValidation" --output-on-failure` | ❌ W0       | ⬜ pending |
| 09-02 T4   | 09-02 | 2    | TRAIN-01..06                         | T-09-02-01, T-09-02-02                                                    | IDriverApi 5 new methods + TrainingResult enum (Ok/ValidationFailed/Connection/Conflict) | integration (build)      | `grep -q "startTraining" src/steamvr/include/micmap/steamvr/driver_api.hpp && grep -q "TrainingProgressView" src/steamvr/include/micmap/steamvr/driver_api.hpp && grep -q "TrainingResult" src/steamvr/include/micmap/steamvr/driver_api.hpp && cmake --build build --target micmap 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true` | ❌ W0       | ⬜ pending |
| 09-03 T1   | 09-03 | 3    | TRAIN-AF-01, IPC-06                  | T-09-03-01                                                                | Zero hits for 5 forbidden tokens in apps/micmap/main.cpp; preserves :300 startup load   | unit (grep absence)      | `! grep -nE '\\bisTraining\\b\|\\baddTrainingSample\\b\|\\bfinishTraining\\b\|\\bstartTraining\\b\|\\bsaveTrainingData\\b' apps/micmap/main.cpp 2>/dev/null && cmake --build build --target micmap 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true`                                                                | ❌ W0       | ⬜ pending |
| 09-03 T2   | 09-03 | 3    | TRAIN-05                             | T-09-03-02, T-09-03-04, T-09-03-05, T-09-03-06                            | UI-SPEC button labels + colors verbatim; 5Hz progress poll; orphan-recovery via /health  | unit (grep presence)     | `grep -q "Train Pattern" apps/micmap/main.cpp && grep -q "Cancel Training" apps/micmap/main.cpp && grep -q "Recompute Thresholds" apps/micmap/main.cpp && grep -q "Confirm & Save" apps/micmap/main.cpp && grep -q "driverApi_->startTraining" apps/micmap/main.cpp && cmake --build build --target micmap 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true` | ❌ W0       | ⬜ pending |
| 09-03 T3   | 09-03 | 3    | TRAIN-AF-01, IPC-06                  | T-09-03-01                                                                | AssertNoClientTraining ctest go-live; CI fails on regression                              | integration (ctest)      | `grep -q "Phase 9 Wave 3" tests/CMakeLists.txt && grep -q "add_test(NAME AssertNoClientTraining" tests/CMakeLists.txt && cmake -B build -S . && ctest --test-dir build -R "AssertNoClientTraining" --output-on-failure 2>&1 \| grep -qE "Passed\|clean"` | ❌ W0       | ⬜ pending |
| 09-04 T1   | 09-04 | 1    | TEST-04                              | T-09-04-06, T-09-04-07                                                    | Public-domain / MIT-0 single-header vendoring; size-guard catches truncation             | unit (grep + size)       | `test -f vendor/dr_wav/dr_wav.h && grep -qE "Public [Dd]omain\|MIT-0\|Unlicense" vendor/dr_wav/dr_wav.h && grep -q "drwav_init_file_w" vendor/dr_wav/dr_wav.h && wc -l vendor/dr_wav/dr_wav.h \| awk '{ if ($1 < 1000) exit 1 }'`        | ❌ W0       | ⬜ pending |
| 09-04 T2   | 09-04 | 1    | TEST-04, TEST-01                     | T-09-04-01, T-09-04-03, T-09-04-08, T-09-04-09                            | dt computed from frame_count/sample_rate (not steady_clock); ZERO OpenVR symbols         | unit (grep + lint)       | `test -f apps/mic_test/src/wav_replay.hpp && grep -q "replayWav" apps/mic_test/src/wav_replay.hpp && grep -q "DR_WAV_IMPLEMENTATION" apps/mic_test/src/wav_replay.cpp && ! grep -qE "<openvr.h>\|<openvr_driver.h>\|vr::" apps/mic_test/src/wav_replay.cpp && cmake -P cmake/AssertReplayNoVrApi.cmake -DREPLAY_DIR=apps/mic_test/src 2>&1 \| grep -q "clean"` | ❌ W0       | ⬜ pending |
| 09-04 T3   | 09-04 | 1    | TEST-04                              | T-09-04-04                                                                | Manifest schema validates; corpus seed with positive + 2 negatives                        | unit (json validate)     | `test -f tests/corpus/replay/positive_001.wav && test -f tests/corpus/replay/negative_silence_001.wav && test -f tests/corpus/replay/negative_speech_001.wav && test -f tests/corpus/replay/manifest.json && python -c "import json; json.load(open('tests/corpus/replay/manifest.json'))"` | ❌ W0       | ⬜ pending |
| 09-04 T4   | 09-04 | 1    | TEST-04                              | T-09-04-01, T-09-04-09                                                    | mic_test_replay_corpus ctest exits 0; replay_results.json schema valid                    | integration (ctest)      | `cmake --build build --target mic_test 2>&1 \| grep -vE '^#' \| grep -qE 'error C[0-9]+:' && exit 1 \|\| true; ctest --test-dir build -R "mic_test_replay_corpus" --output-on-failure 2>&1 \| grep -qE "Passed"`                          | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/driver/training_session_test.cpp` — TrainingSession state-machine stubs (RED tests for TRAIN-01..03)
- [ ] `tests/driver/training_endpoint_validation_test.cpp` — HTTP endpoint envelope tests (RED for TRAIN-01..06)
- [ ] `tests/mic_test/wav_replay_test.cpp` — dr_wav decode stubs (RED for TEST-04)
- [ ] `tests/corpus/replay/` directory + 3 seed WAVs + manifest.json + README.md
- [ ] `cmake/AssertNoClientTraining.cmake` (lint script; ctest registration deferred to 09-03)
- [ ] `cmake/AssertReplayNoVrApi.cmake` (lint script + ctest registration at 09-00)
- [ ] `vendor/dr_wav/dr_wav.h` — single-header WAV decoder (committed direct, see RESEARCH §A4)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| End-to-end live training session on real hardware | TRAIN-01..05 | NEEDS VALIDATION flag in roadmap — UX commit/discard pattern is novel; hardware loop required | 1) Launch SteamVR + driver + client. 2) Click "Train" — observe progress bar at 5–10 Hz polling. 3) Confirm finalize writes new `training_data.bin` atomically. 4) Trigger detection with trained pattern — verify HMD button click. (D-39(1) — full regimen in 09-UAT.md.) |
| Cancel mid-training discards state cleanly | TRAIN-04 | State-transition correctness on real audio is empirical | Start training, click Cancel after ~50 samples. Verify driver returns to detection mode and `training_data.bin` is unchanged (mtime+hash unchanged). (D-39(2)) |
| Recompute preview confirm/discard | TRAIN-06 | Threshold-derivation UX is first-principles; needs human judgement | After collecting samples, POST `/training/recompute {sensitivity:0.7}`, observe preview thresholds, confirm or discard. Verify discard does not modify file. (D-39(3)) |
| TRAIN-AF-01 single-owner WASAPI invariant | TRAIN-AF-01 | Anti-feature lint check at code-review time | `grep -rn 'IAudioCapture\|createAudioCapture' apps/micmap/src/` — confirm no `.start()` calls during training mode. Lint AssertNoClientTraining (09-03) enforces structurally. (D-39(7)) |
| Driver-down during training UX | TRAIN-05, IPC-06 | SteamVR kill / restart cycle requires real driver runtime | Per 09-UAT.md case D-39(6): kill SteamVR mid-session; observe ECONNREFUSED detected within 1s; restart; verify training_data.bin unchanged. |
| 50-cycle stress (handle leak detection) | IPC-06, TRAIN-AF-01 | Process Explorer handle counts require running real driver | Per 09-UAT.md case D-39(10): 50 rapid POST /training/start → cancel cycles; verify no monotonic handle growth + training_data.bin integrity preserved. |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies (Per-Task Map populated above; every 09-00..09-04 task has an `<automated>` block)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify (every row has a verify command)
- [x] Wave 0 covers all MISSING references (dr_wav vendor in 09-04 T1; replay corpus in 09-04 T3; RED test stubs in 09-00 T2)
- [x] No watch-mode flags (every command is one-shot — `cmake --build`, `ctest --test-dir`, `cmake -P`; no `--watch` / `--server-mode`)
- [x] Feedback latency < 40s (cmake configure ~10s + per-target build ~15s + per-test run ~5s; all under the 40s budget per Sampling Rate row above)
- [x] Replay corpus has ≥1 known-positive + ≥1 known-negative WAV (TEST-04) — corpus seed in 09-04 Task 3 has positive_001.wav + negative_silence_001.wav + negative_speech_001.wav
- [ ] Hardware sign-off recorded in PR/UAT (NEEDS VALIDATION) — pending 09-05 manual checkpoint
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved (Wave 0..4 automated verification map populated; hardware sign-off pending 09-05)

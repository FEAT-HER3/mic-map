---
phase: 9
slug: training-migration
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-08
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

> Filled by gsd-planner during plan generation. Each task gets a row mapping to its plan wave, requirement IDs (TRAIN-01..06, TEST-04, IPC-06), test type (unit / integration / replay / manual), and automated command.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD     | TBD  | TBD  | TBD         | TBD        | TBD             | TBD       | TBD               | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/training/test_session_lifecycle.cpp` — TrainingSession state-machine stubs (RED tests for TRAIN-01..03)
- [ ] `tests/training/test_recompute.cpp` — recomputeProfile stubs (RED for TRAIN-06)
- [ ] `tests/replay/test_replay_decoder.cpp` — dr_wav decode stubs (RED for TEST-04)
- [ ] `tests/replay-corpus/` directory + Python seed-generator script (`tools/gen-replay-corpus.py`)
- [ ] `tests/conftest.cmake` (or equivalent CTest fixture wiring) — shared CTest labels (`unit`, `replay`)
- [ ] `vendor/dr_wav/dr_wav.h` — single-header WAV decoder (committed direct, see RESEARCH §A4)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| End-to-end live training session on real hardware | TRAIN-01..05 | NEEDS VALIDATION flag in roadmap — UX commit/discard pattern is novel; hardware loop required | 1) Launch SteamVR + driver + client. 2) Click "Train" — observe progress bar at 5–10 Hz polling. 3) Confirm finalize writes new `training_data.bin` atomically. 4) Trigger detection with trained pattern — verify HMD button click. |
| Cancel mid-training discards state cleanly | TRAIN-04 | State-transition correctness on real audio is empirical | Start training, click Cancel after ~50 samples. Verify driver returns to detection mode and `training_data.bin` is unchanged (mtime unchanged). |
| Recompute preview confirm/discard | TRAIN-06 | Threshold-derivation UX is first-principles; needs human judgement | After collecting samples, POST `/training/recompute {sensitivity:0.7}`, observe preview thresholds, confirm or discard. Verify discard does not modify file. |
| TRAIN-AF-01 single-owner WASAPI invariant | TRAIN-AF-01 | Anti-feature lint check at code-review time | `grep -rn 'IAudioCapture\|createAudioCapture' apps/micmap/src/` — confirm no `.start()` calls during training mode. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (dr_wav vendor, replay corpus, RED test stubs)
- [ ] No watch-mode flags
- [ ] Feedback latency < 40s
- [ ] Replay corpus has ≥1 known-positive + ≥1 known-negative WAV (TEST-04)
- [ ] Hardware sign-off recorded in PR/UAT (NEEDS VALIDATION)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending

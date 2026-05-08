---
phase: 09-training-migration
plan: 04
subsystem: testing
tags: [phase-9, training-migration, wave-1, wav-replay, mic-test, ci-corpus, dr-wav, agent-qa]

# Dependency graph
requires:
  - phase: 09-training-migration / 09-00
    provides: AssertReplayNoVrApi lint script (RED-tolerant skip-on-NOT-EXISTS) and tests/mic_test/wav_replay_test.cpp Wave 0 RED scaffold
provides:
  - vendor/dr_wav/dr_wav.h (single-header WAV decoder, dr_wav v0.14.6, public-domain / MIT-0)
  - apps/mic_test/src/wav_replay.{hpp,cpp} (decode + downmix + linear-resample + JSON output, headless)
  - 9 new replay CLI flags on mic_test (--replay, --replay-dir, --expect-triggers[-tolerance|-from], --profile, --config, --json-output, --max-duration)
  - tests/corpus/replay/{positive_001,negative_silence_001,negative_speech_001}.wav + manifest.json + README.md (deterministic seed corpus)
  - tools/gen-replay-corpus.py (deterministic regen script, fixed RNG seeds)
  - tests/CMakeLists.txt mic_test_replay_corpus ctest registration (Wave 1 / 09-04)
  - T-09-04-01 declared-duration DoS gate (peekWavHeader pre-scan in wav_replay.cpp)
affects: [phase-10, future-detection-tuning, agent-qa-loops, ci-regression-trend]

# Tech tracking
tech-stack:
  added:
    - "dr_wav v0.14.6 (mackron/dr_libs @ 243e26ffa) — single-header WAV decoder, public-domain / MIT-0"
  patterns:
    - "WinMain CLI dispatch pattern: tryRunReplayCli() short-circuits before any GUI/audio init when --replay or --replay-dir is present; returns -1 to fall through to GUI mode otherwise"
    - "Pre-scan WAV RIFF header (peekWavHeader) before drwav_init to recover *declared* data-chunk size (dr_wav silently clamps to file size, defeats max-duration DoS gate)"
    - "kBlockFrames = 480 — reuse the driver SampleRing block size in the replay harness so detector/state-machine see identical block boundaries to the live audio path"
    - "dt = block_count * 1000 / sample_rate (D-34 determinism); never std::chrono::steady_clock"

key-files:
  created:
    - vendor/dr_wav/dr_wav.h
    - apps/mic_test/src/wav_replay.hpp
    - apps/mic_test/src/wav_replay.cpp
    - tests/corpus/replay/positive_001.wav
    - tests/corpus/replay/negative_silence_001.wav
    - tests/corpus/replay/negative_speech_001.wav
    - tests/corpus/replay/manifest.json
    - tests/corpus/replay/README.md
    - tools/gen-replay-corpus.py
  modified:
    - apps/mic_test/main.cpp
    - apps/mic_test/CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "Acquired dr_wav.h via curl from upstream (raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h @ 243e26ffa); WebFetch tool not available in this executor environment, sister-project bey-closer-t1 had no dr_wav.h vendored, so curl-from-upstream-with-pinned-SHA was the only verifiable acquisition path."
  - "mic_test is a Win32 GUI binary (WinMain entry, not wmain) — added a tryRunReplayCli() dispatcher that runs at the very top of WinMain via GetCommandLineW + CommandLineToArgvW. CLI mode and GUI mode coexist in one binary; switching to a console subsystem would have rippled into the existing GUI path and broken live-mic testing."
  - "T-09-04-01 mitigation requires reading the *declared* WAV data-chunk size (not the file-clamped value dr_wav exposes via totalPCMFrameCount). Hand-rolled peekWavHeader() walks RIFF/WAVE chunks before drwav_init_file_w to surface declared duration; declared-duration > max_duration_s returns exit code 2."
  - "namespace divergence from plan: actual state machine lives in micmap::core::IStateMachine with State::Triggered (src/core/include/micmap/core/state_machine.hpp), NOT micmap::detection::IStateMachine. Used the canonical namespace; plan reference was stale."
  - "Profile policy: positive_001.wav cannot fire its 1-trigger expectation profileless (detector is uninitialised → confidence stays 0). Per plan Task 4 Step 4 fallback, mic_test_replay_corpus ctest does NOT pass --expect-triggers-from initially — it validates decode/downmix/resample/JSON pipeline instead. manifest.json stays in repo as the contract for a future plan that ships seed_profile.bin."
  - "writeJsonOutput shipped as 3 overloads: vector<ReplayResult> (used by 09-00 RED scaffold tests/mic_test/wav_replay_test.cpp case 7), DirReplayResult+cfg (--replay-dir), single ReplayResult+cfg (--replay). All three call sites in the codebase resolve cleanly."
  - "Mutual exclusion --expect-triggers vs --expect-triggers-from enforced at CLI parse, returns exit 2 with stderr message; matches CONTEXT line 168."

patterns-established:
  - "WAV-replay determinism contract: dt computed from frame-count + sample-rate, recursive-directory listing sorted lexicographically before walking, JSON output written via deterministic dump(2). Three consecutive runs verified byte-identical sha256 of replay_results.json."
  - "Two-overload public surface for the replay harness: replayWav(path, cfg) is decode-only / shape-validation (used by 09-00 RED scaffold), replayWav(path, cfg, detector, sm, expected, tolerance) is full detect-and-trigger (used by mic_test --replay). The decode-only form covers format-policy assertions in isolation; the detect-and-trigger form drives end-to-end agent QA loops."

requirements-completed: [TEST-04]

# Metrics
duration: ~50min
completed: 2026-05-08
---

# Phase 9 Plan 04: WAV Replay Harness Summary

**Headless WAV replay harness for mic_test.exe — decode + downmix + linear-resample + detector pipeline + JSON output, with a 3-WAV deterministic seed corpus and CI ctest registration. Wave 0 RED scaffold (tests/mic_test/wav_replay_test.cpp, 7 cases) turns GREEN.**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-05-08
- **Completed:** 2026-05-08
- **Tasks:** 5 + 1 deviation fix (6 commits)
- **Files modified:** 12 (3 modified, 9 created)

## Accomplishments

- **Vendored dr_wav v0.14.6** (mackron/dr_libs @ 243e26ffa, dual-licensed public-domain / MIT-0) under `vendor/dr_wav/dr_wav.h`. License + API surface verified at commit time.
- **Implemented `wav_replay.{hpp,cpp}`** (~600 LOC) with two replay APIs: a decode-only form for shape/format testing and a detect-and-trigger form for end-to-end replay. Bit-depth gate (D-32: 16-bit PCM + 32-bit float only), stereo-and-N-channel-to-mono downmix, linear-interp resample, kBlockFrames=480 chunked detector feed, dt-pure determinism (D-34), 3 JSON-output overloads.
- **Closed T-09-04-01 DoS gap** discovered during integration testing (`peekWavHeader` pre-scan recovers declared chunk size before dr_wav clamps it to file size).
- **Added 9 CLI flags to mic_test** with a `tryRunReplayCli()` dispatch path that short-circuits before any GUI/audio init. Live-mic GUI mode preserved unchanged.
- **Landed 3-WAV deterministic seed corpus** under `tests/corpus/replay/` plus `manifest.json` + `README.md` + `tools/gen-replay-corpus.py` (numpy fixed-RNG, byte-stable across hosts).
- **Registered `mic_test_replay_corpus` ctest** in `tests/CMakeLists.txt` Wave 1 (09-04) block. Passing locally; replay_results.json validates against D-30 schema; 3 consecutive runs produce byte-identical sha256.
- **AssertReplayNoVrApi lint** transitioned from RED-tolerant skip-on-NOT-EXISTS to actively scanning 2 files (`wav_replay.{hpp,cpp}`) and reports clean.
- **Wave 0 RED scaffold (tests/mic_test/wav_replay_test.cpp)** turns from build-RED to all-7-cases-pass.

## Task Commits

Each task was committed atomically (one fix commit also broken out for the T-09-04-01 mitigation that surfaced during Task 3):

1. **Task 1: Vendor dr_wav.h** — `529be26` (chore)
2. **Task 2: Implement wav_replay.{hpp,cpp}** — `c8838d4` (feat)
3. **Deviation fix: T-09-04-01 declared-duration DoS gate** — `5347175` (fix; Rule 2 — missing critical mitigation surfaced during Task 3 integration)
4. **Task 3: 9 CLI flags + dispatch in mic_test** — `955f0e4` (feat)
5. **Task 4: Seed corpus + manifest + regen script** — `b93eece` (chore)
6. **Task 5: mic_test_replay_corpus ctest** — `e80d592` (build)

## Files Created/Modified

**Created (9):**
- `vendor/dr_wav/dr_wav.h` — single-header WAV decoder, dr_wav v0.14.6 @ 243e26ffa
- `apps/mic_test/src/wav_replay.hpp` — public surface (ReplayConfig / ReplayResult / TriggerEvent / DirReplayResult; replayWav x2 overloads; replayWavDir; writeJsonOutput x3 overloads)
- `apps/mic_test/src/wav_replay.cpp` — decode + downmix + resample + JSON impl; T-09-04-01 peekWavHeader DoS gate
- `tests/corpus/replay/positive_001.wav` — ~2 s band-limited white-noise burst (mic-cover signature)
- `tests/corpus/replay/negative_silence_001.wav` — 5 s silence baseline
- `tests/corpus/replay/negative_speech_001.wav` — 5 s synthetic speech-like signal (multi-formant + 3 Hz syllable AM)
- `tests/corpus/replay/manifest.json` — per-file expected_triggers + tolerance + notes
- `tests/corpus/replay/README.md` — schema docs + regen recipe + determinism contract
- `tools/gen-replay-corpus.py` — deterministic numpy-based regen script

**Modified (3):**
- `apps/mic_test/main.cpp` — `tryRunReplayCli()` dispatcher + 9-flag wide-argv parser at top of WinMain; live-mic GUI mode preserved unchanged
- `apps/mic_test/CMakeLists.txt` — added wav_replay.cpp to source list; nlohmann_json link; vendor/dr_wav + apps/mic_test/src include paths
- `tests/CMakeLists.txt` — appended `# ---- Phase 9 Wave 1 (09-04) ----` block after Wave 0 terminator with `mic_test_replay_corpus` ctest registration

## Decisions Made

See frontmatter `key-decisions` for the full list. Most consequential:

- **WinMain CLI-dispatch pattern** (Rule 3 inline-fix): mic_test is a Win32 GUI binary, not a console wmain entry. Adding a console-mode replay path via `GetCommandLineW`+`CommandLineToArgvW` short-circuit at the top of WinMain preserved the live-mic GUI path while enabling agent QA loops to drive the binary headlessly.
- **T-09-04-01 mitigation via header pre-scan** (Rule 2 missing-critical): dr_wav silently clamps the WAV data-chunk size to file size, defeating the `--max-duration` DoS guard. A hand-rolled `peekWavHeader()` recovers declared duration before `drwav_init` for the security check.
- **Profile-deferred CI corpus invocation**: positive_001.wav cannot fire its expected trigger profileless (no training data → confidence stays at 0). Plan-spec fallback was either to ship `seed_profile.bin` (requires training-from-WAV tooling not yet built) or to register the ctest without `--expect-triggers-from`. Chose the latter — the manifest stays in repo as the contract for a future plan that adds train-from-WAV + seed_profile.bin.
- **dr_wav acquisition path**: WebFetch tool not exposed in this executor; `bey-closer-t1` did not have dr_wav.h vendored (only hidapi + openvr + mdns + oscpp). Used `curl -fsSL` against the canonical raw URL with the upstream master commit SHA pinned in the commit message — equivalent to the WebFetch path described in the plan, with verifiable upstream provenance.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 — Missing Critical Mitigation] T-09-04-01 declared-duration DoS gate**
- **Found during:** Task 3 integration testing (running test_wav_replay against the 09-00 scaffold)
- **Issue:** Test case 5 (truncated 1-hour header with `cfg.max_duration_s = 600` expects exit_code 2) failed because dr_wav silently clamps `dataChunkSize` to actual file size (dr_wav.h ~3763), so `totalPCMFrameCount` reports the truncated 1-second duration and our `--max-duration` check did not fire. This defeats the threat-register T-09-04-01 mitigation as written ("--max-duration default 600 s; reject longer files with exit code 2").
- **Fix:** Added `peekWavHeader()` in `apps/mic_test/src/wav_replay.cpp` — a 100-line hand-rolled RIFF/WAVE chunk walker that reads the declared data-chunk byte count before `drwav_init_file_w`. `decodeWav()` now checks `declared_duration_s > cfg.max_duration_s` and returns exit code 2 with a `"declared duration ... exceeds --max-duration ..."` stderr message. peekWavHeader failure (file unreadable / not RIFF/WAVE) is non-fatal — falls through to dr_wav's own diagnostics.
- **Files modified:** apps/mic_test/src/wav_replay.cpp (+117 LOC)
- **Verification:** test_wav_replay all 7 cases pass; case 5 specifically reports `error: ...case5_one_hour_truncated.wav: declared duration 3600s exceeds --max-duration 600s`
- **Committed in:** `5347175` (broken out as a standalone fix commit between Task 2 and Task 3)

**2. [Rule 3 — Blocking, fixed inline] mic_test entry shape mismatch**
- **Found during:** Task 3 (extending `mic_test/main.cpp`)
- **Issue:** Plan referenced an existing `wmain(int argc, wchar_t* argv[])` entry; in reality `mic_test` is a Win32 GUI binary with `WinMain(HINSTANCE, ...)`. Naively adding wide-string `argc/argv` parsing to a `WinMain` would not compile — `wargv` is not a global symbol there.
- **Fix:** Used `GetCommandLineW()` + `CommandLineToArgvW()` (shellapi.h, shell32.lib) to recover wide argv at the top of `WinMain`. Wrapped the parse + dispatch into `tryRunReplayCli()` which returns `-1` when neither `--replay` nor `--replay-dir` is set; `WinMain` falls through to the existing live-mic GUI path on `-1`. Returned exit codes (0 / 1 / 2) follow CONTEXT D-31 verbatim when in replay mode.
- **Files modified:** apps/mic_test/main.cpp, apps/mic_test/CMakeLists.txt (no link change needed — `#pragma comment(lib, "shell32.lib")` covers the new symbols)
- **Verification:** mic_test builds cleanly; `mic_test --replay-dir tests/corpus/replay --json-output build/r.json` exits 0; running mic_test with no args (or any non-replay args) still launches the live-mic GUI window unchanged.
- **Committed in:** `955f0e4` (Task 3 commit)

**3. [Rule 1 — Bug, fixed inline] Plan namespace references stale**
- **Found during:** Task 2 (writing wav_replay.cpp)
- **Issue:** Plan `<interfaces>` block referenced `micmap::detection::IStateMachine` with `DetectionState::Triggered` enum and `getState()` method. Actual interface lives at `micmap::core::IStateMachine` with `State::Triggered` and `getCurrentState()` (`src/core/include/micmap/core/state_machine.hpp`).
- **Fix:** Used canonical `micmap::core::` namespace + `State::Triggered` + `getCurrentState()` throughout wav_replay.cpp. Header forward-declares both `micmap::detection::INoiseDetector` and `micmap::core::IStateMachine` to keep dependency surface light.
- **Files modified:** apps/mic_test/src/wav_replay.{hpp,cpp}
- **Verification:** wav_replay.cpp compiles; `mic_test` binary links; AssertReplayNoVrApi clean.
- **Committed in:** `c8838d4` (Task 2 commit)

**4. [Rule 2 — Missing Critical] Three writeJsonOutput overloads required (test scaffold compatibility)**
- **Found during:** Task 2 (writing wav_replay.hpp public surface)
- **Issue:** Plan specified two `writeJsonOutput` overloads (DirReplayResult + single ReplayResult, both taking ReplayConfig). The 09-00 RED scaffold (`tests/mic_test/wav_replay_test.cpp` line 181) calls `writeJsonOutput(out_path, results)` with `vector<ReplayResult>` and no ReplayConfig. Without a third overload the Wave 0 success criterion ("scaffold turns from RED-build to GREEN") would not be reachable.
- **Fix:** Added a third overload `bool writeJsonOutput(const std::filesystem::path&, const std::vector<ReplayResult>&)` that emits the same D-30 JSON shape with empty `config_path` / `profile_path` strings. Aggregates pass/fail counts from the vector for the `summary` block.
- **Files modified:** apps/mic_test/src/wav_replay.{hpp,cpp}
- **Verification:** test_wav_replay case 7 (JSON schema) passes — emitted JSON has all four required keys (`config_path`, `profile_path`, `files`, `summary`).
- **Committed in:** `c8838d4` (Task 2 commit)

**5. [Rule 4 → resolved as Rule 3 inline] Profile-deferred CI corpus registration**
- **Found during:** Task 4 Step 4 (verifying corpus passes its own expectations)
- **Issue:** positive_001.wav's expected 1 trigger requires a trained detector profile. Without `--profile`, detector runs uninitialised → confidence stays 0 → no triggers ever fire → corpus FAILs its own manifest.
- **Decision path:** Plan Task 4 Step 4 explicitly anticipated this with two fallbacks: (a) ship `tests/corpus/replay/seed_profile.bin` as a fourth file, or (b) defer and register without `--expect-triggers-from`. Both are acceptable per the plan; chose (b) because shipping (a) requires a "train from WAV" mode in mic_test that is out of scope for 09-04.
- **Fix:** `mic_test_replay_corpus` ctest registration omits `--expect-triggers-from`. The harness still validates decode + downmix + resample + JSON-output across all 3 corpus files. manifest.json stays in repo as the contract for a future plan that ships seed_profile.bin and re-enables `--expect-triggers-from`.
- **Files modified:** tests/CMakeLists.txt (registration block); tests/corpus/replay/manifest.json (unchanged, kept as contract)
- **Verification:** `ctest -R mic_test_replay_corpus` passes; replay_results.json shape OK; 3-run sha256 byte-identical (D-34 determinism).
- **Committed in:** `e80d592` (Task 5 commit)

---

**Total deviations:** 5 auto-fixed (2 Rule 1/2 missing-critical, 2 Rule 3 blocking, 1 Rule 4-bracket policy choice with explicit plan blessing)
**Impact on plan:** All deviations were either security-mitigation completions (T-09-04-01), entry-shape adaptation to the existing codebase (Win32 GUI binary), test-scaffold compatibility (3rd writeJsonOutput overload), or an explicitly anticipated plan-fallback. No scope creep; all five plan tasks completed; success criteria all met (modulo profile-deferral noted above).

## Issues Encountered

**Pre-existing build failures (out-of-scope):**
- `test_training_endpoint_validation` — RED scaffold awaiting plan 09-02 (HttpServer ctor signature mismatch, expected at this wave).
- `test_training_session` — RED scaffold awaiting a future plan (`validateFinalizePayload` not yet implemented in `driver/src/training_session.hpp`).

Verified these failed BEFORE my changes (`git stash` + rebuild reproduced both). Per executor SCOPE BOUNDARY rule, out-of-scope: 09-04 owns `apps/mic_test/`, `vendor/`, `tests/corpus/`, `tools/`, and the new `tests/CMakeLists.txt` 09-04 block. The driver-side training scaffolds belong to 09-01 / 09-02 / 09-03.

## User Setup Required

None — this plan is fully headless and adds no external services or runtime dependencies. dr_wav.h is vendored; numpy is the only host requirement for `tools/gen-replay-corpus.py` (regeneration is opt-in; CI does not regenerate).

## Next Phase Readiness

**Ready for downstream consumers:**
- Wave 0 RED scaffold (`tests/mic_test/wav_replay_test.cpp`) — all 7 cases now pass.
- AssertReplayNoVrApi — actively scanning, clean.
- mic_test_replay_corpus — passing, byte-stable.
- mic_test --replay / --replay-dir CLI surface ready for agent QA loops.

**Future plan hooks:**
- A "train from WAV" mode (likely a future plan in Phase 9 or a follow-on phase) can produce `tests/corpus/replay/seed_profile.bin`; updating the ctest registration to add `--profile <path> --expect-triggers-from <manifest>` then activates the full positive/negative expectation matching.
- `replay_results.json` schema is stable per CONTEXT D-30; CI can start uploading this as an artifact for trend analysis.

## Self-Check: PASSED

Files verified to exist (pre-final-commit):
- vendor/dr_wav/dr_wav.h (FOUND, 9105 lines)
- apps/mic_test/src/wav_replay.hpp (FOUND)
- apps/mic_test/src/wav_replay.cpp (FOUND)
- apps/mic_test/main.cpp (FOUND, modified)
- apps/mic_test/CMakeLists.txt (FOUND, modified)
- tests/corpus/replay/positive_001.wav (FOUND)
- tests/corpus/replay/negative_silence_001.wav (FOUND)
- tests/corpus/replay/negative_speech_001.wav (FOUND)
- tests/corpus/replay/manifest.json (FOUND)
- tests/corpus/replay/README.md (FOUND)
- tools/gen-replay-corpus.py (FOUND)
- tests/CMakeLists.txt (FOUND, modified — Wave 1 (09-04) block appended)

Commits verified to exist:
- 529be26 (Task 1) — FOUND
- c8838d4 (Task 2) — FOUND
- 5347175 (Deviation fix) — FOUND
- 955f0e4 (Task 3) — FOUND
- b93eece (Task 4) — FOUND
- e80d592 (Task 5) — FOUND

Test verification:
- ctest mic_test_replay_corpus — PASSED
- ctest WavReplayHarness — PASSED
- ctest AssertReplayNoVrApi — PASSED
- 3-run determinism: replay_results.json sha256 byte-identical across runs
- replay_results.json validates against D-30 schema (config_path / profile_path / files / summary)

---
*Phase: 09-training-migration*
*Completed: 2026-05-08*

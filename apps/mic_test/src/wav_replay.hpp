/**
 * @file wav_replay.hpp
 * @brief Phase 9 / TEST-04 / D-28..D-37 — WAV replay harness for mic_test.
 *
 * Decode a WAV file (or directory of WAVs), optionally feed samples through
 * INoiseDetector + IStateMachine deterministically, emit per-file results in
 * JSON shape per CONTEXT D-30.
 *
 *   - Bit-depth policy per D-32: 16-bit PCM and 32-bit float supported;
 *     8-bit / 24-bit return exit code 2.
 *   - Format policy per D-32: stereo->mono averaged downmix; linear-interp
 *     resample to ReplayConfig::target_sample_rate.
 *   - Pacing flat-out per D-33: no realtime — process at max CPU speed.
 *   - Determinism per D-34: dt = block_count * 1000 / sample_rate (NOT
 *     std::chrono::steady_clock).
 *
 * TEST-01 invariant (D-28 / D-37): ZERO OpenVR symbols. AssertReplayNoVrApi
 * lint enforces this on every CI run.
 *
 * Two replay APIs are exposed:
 *
 *   1. Decode-only / shape: `replayWav(path, cfg)` validates a WAV file and
 *      returns frames-out / duration / channels / exit_code without running
 *      any detector. Used by the Wave 0 RED scaffold (tests/mic_test/wav_replay_test.cpp)
 *      to assert the format-policy gate from D-32 in isolation.
 *
 *   2. Detect-and-trigger: `replayWav(path, cfg, detector, sm, expected, tol)`
 *      runs the decoded samples through the supplied INoiseDetector +
 *      IStateMachine, observes rising-edge Triggered transitions, and
 *      compares the trigger count against `expected_triggers` (within
 *      `tolerance`). Used by `mic_test --replay` / `--replay-dir`.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Forward-declare the detector + state machine interfaces — keep this header
// dependency-light (detector .cpp pulls the full hpp).
namespace micmap::detection { class INoiseDetector; }
namespace micmap::core { class IStateMachine; }

namespace micmap::mic_test {

/**
 * @brief Replay configuration shared by single-file and batch replay.
 */
struct ReplayConfig {
    std::filesystem::path config_path;        ///< For JSON output reporting (--config).
    std::filesystem::path profile_path;       ///< For JSON output reporting (--profile).
    uint32_t target_sample_rate{48000};       ///< Linear-resample target rate (D-32).
    std::size_t fft_size{2048};               ///< Detector FFT size (matches micmap default).
    int max_duration_s{600};                  ///< D-32: reject longer WAVs with exit code 2.
    bool quiet{false};                        ///< Suppress per-file PASS/FAIL stdout summary lines.
    bool verbose{false};                      ///< Emit per-trigger stdout lines.
};

/**
 * @brief A single rising-edge trigger event observed during replay.
 *
 * Only emitted by the detect-and-trigger overload; the decode-only overload
 * leaves the trigger list empty.
 */
struct TriggerEvent {
    double t_s{0.0};                          ///< Seconds from start of WAV at the rising edge.
    float confidence{0.0f};                   ///< Detector confidence at the rising edge.
    std::string state{"Triggered"};           ///< State name — always "Triggered" today.

    bool operator==(const TriggerEvent& o) const noexcept {
        return t_s == o.t_s && confidence == o.confidence && state == o.state;
    }
    bool operator!=(const TriggerEvent& o) const noexcept { return !(*this == o); }
};

/**
 * @brief Per-file replay result.
 *
 * `exit_code` is the mic_test process exit code semantics per D-31:
 *   0 = success / no expectation set,
 *   1 = expectation failure (observed_triggers vs expected_triggers diff > tolerance),
 *   2 = format / IO error (file not found, unsupported bit depth, oversize).
 */
struct ReplayResult {
    int exit_code{0};
    std::filesystem::path wav;
    double duration_s{0.0};                   ///< Source-file duration (before resample).
    uint32_t sample_rate{0};                  ///< AFTER downmix+resample == target_sample_rate.
    uint16_t channels{1};                     ///< AFTER downmix == 1 (mono).
    std::size_t frames_out{0};                ///< Frame count after resample (test-scaffold field).
    std::optional<int> expected_triggers;
    int observed_triggers{0};
    int tolerance{0};
    bool pass{true};
    std::vector<TriggerEvent> triggers;       ///< Rising-edge events (detect-and-trigger overload).
    std::vector<TriggerEvent> trigger_output; ///< Test-scaffold determinism field — same content as `triggers`.
    std::optional<std::string> error_message;
};

/**
 * @brief Aggregate result for a directory replay.
 */
struct DirReplayResult {
    std::vector<ReplayResult> files;
    int total{0};
    int passed{0};
    int failed{0};
};

/**
 * @brief Decode-only replay: validate format, downmix+resample, return shape.
 *
 * Used by tests/mic_test/wav_replay_test.cpp to exercise the D-32 bit-depth
 * gate, downmix path, resample path, and max-duration guard without needing
 * a trained detector. Does not invoke any detector or state machine.
 */
ReplayResult replayWav(const std::filesystem::path& wav, const ReplayConfig& cfg);

/**
 * @brief Detect-and-trigger replay: full pipeline.
 *
 * Caller is responsible for pre-loading the detector training profile via
 * INoiseDetector::loadTrainingData before calling. dt is computed from
 * block frame count + sample rate (D-34) — never from std::chrono::steady_clock.
 */
ReplayResult replayWav(const std::filesystem::path& wav,
                       const ReplayConfig& cfg,
                       micmap::detection::INoiseDetector& detector,
                       micmap::core::IStateMachine& sm,
                       std::optional<int> expected_triggers,
                       int tolerance);

/**
 * @brief Replay every *.wav under `dir` (recursive, sorted for determinism).
 *
 * If `expectations_path` is non-empty, it is parsed as the manifest.json
 * schema (CONTEXT D-30 + D-35); per-file expected_triggers + tolerance are
 * looked up by basename or relative path.
 */
DirReplayResult replayWavDir(const std::filesystem::path& dir,
                             const ReplayConfig& cfg,
                             micmap::detection::INoiseDetector& detector,
                             micmap::core::IStateMachine& sm,
                             const std::filesystem::path& expectations_path = {});

/**
 * @brief Write a vector of ReplayResult to JSON per the CONTEXT D-30 schema.
 *
 * Schema:
 *   {
 *     "config_path": "...",
 *     "profile_path": "...",
 *     "files": [ { wav, duration_s, sample_rate, channels, expected_triggers,
 *                  observed_triggers, tolerance, pass, triggers: [...] } ],
 *     "summary": { total, passed, failed }
 *   }
 *
 * Three overloads provided to match all observed call sites:
 *   - vector<ReplayResult>      — used by tests/mic_test/wav_replay_test.cpp
 *   - DirReplayResult + cfg     — used by `mic_test --replay-dir`
 *   - single ReplayResult + cfg — used by `mic_test --replay`
 */
bool writeJsonOutput(const std::filesystem::path& out_path,
                     const std::vector<ReplayResult>& results);

bool writeJsonOutput(const std::filesystem::path& out_path,
                     const ReplayConfig& cfg,
                     const DirReplayResult& result);

bool writeJsonOutput(const std::filesystem::path& out_path,
                     const ReplayConfig& cfg,
                     const ReplayResult& single);

} // namespace micmap::mic_test

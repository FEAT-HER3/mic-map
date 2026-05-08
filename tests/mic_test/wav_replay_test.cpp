/**
 * @file wav_replay_test.cpp
 * @brief Phase 9 Wave 0 RED scaffold for WAV decode + downmix + linear-resample
 *        + JSON output schema + determinism (TEST-04, D-30, D-32, D-34).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_settings_validator.cpp / tests/driver/detection_settings_propagation_test.cpp.
 *
 * RED until Plan 09-04 lands apps/mic_test/src/wav_replay.{hpp,cpp} +
 * vendor/dr_wav/dr_wav.h. This translation unit is intentionally fail-to-build
 * at Wave 0 — the compile failure IS the Nyquist gate per
 * .planning/phases/09-training-migration/09-VALIDATION.md.
 *
 * Test cases:
 *   1. 1 s mono 48 kHz 16-bit PCM => exit_code == 0; duration_s ~= 1.0.
 *   2. Stereo 48 kHz 32-bit float => channels reported == 1 (downmix to mono).
 *   3. Mono 44.1 kHz => detector receives 48 kHz frames after linear resample (D-32).
 *   4. 8-bit WAV => exit_code == 2 (unsupported bit depth per D-32).
 *   5. Truncated 1-hour header with cfg.max_duration_s = 600 => exit_code == 2.
 *   6. Determinism: same WAV replayed 3 times produces byte-identical trigger output (D-34).
 *   7. JSON output schema: writeJsonOutput emits {config_path, profile_path, files, summary} per D-30.
 */

#include "wav_replay.hpp"                   // RED hook: lands in Plan 09-04

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mt = micmap::mic_test;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

// ----- Minimal RIFF/WAVE writer (44-byte canonical header + raw sample bytes).
// audio_format: 1 = PCM (16/8), 3 = IEEE float (32).
static bool writeWav(const fs::path& path,
                     uint16_t audio_format,
                     uint16_t channels,
                     uint32_t sample_rate,
                     uint16_t bits_per_sample,
                     const std::vector<uint8_t>& sample_bytes,
                     uint32_t override_data_size = 0) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const uint32_t data_size = override_data_size ? override_data_size
                                                  : static_cast<uint32_t>(sample_bytes.size());
    const uint32_t fmt_chunk_size = 16;
    const uint32_t riff_size = 4 + (8 + fmt_chunk_size) + (8 + data_size);
    const uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8u);
    const uint16_t block_align = static_cast<uint16_t>(channels * (bits_per_sample / 8u));

    auto w_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto w_u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("RIFF", 4);
    w_u32(riff_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    w_u32(fmt_chunk_size);
    w_u16(audio_format);
    w_u16(channels);
    w_u32(sample_rate);
    w_u32(byte_rate);
    w_u16(block_align);
    w_u16(bits_per_sample);
    out.write("data", 4);
    w_u32(data_size);
    out.write(reinterpret_cast<const char*>(sample_bytes.data()),
              static_cast<std::streamsize>(sample_bytes.size()));
    return out.good();
}

int main() {
    const fs::path tmp_root = fs::temp_directory_path() / "micmap_p9_replay_test";
    std::error_code ec;
    fs::remove_all(tmp_root, ec);
    fs::create_directories(tmp_root, ec);

    // ----- Case 1: 1 s mono 48 kHz 16-bit PCM.
    {
        const uint32_t rate = 48000;
        std::vector<uint8_t> bytes(rate * 2u, 0);   // 1 s of silence (sample-bytes pattern)
        auto wav = tmp_root / "case1_mono_48k_16.wav";
        MM_CHECK(writeWav(wav, /*pcm=*/1, /*ch=*/1, rate, /*bps=*/16, bytes));

        mt::ReplayConfig cfg{};
        mt::ReplayResult result = mt::replayWav(wav, cfg);
        MM_CHECK(result.exit_code == 0);
        MM_CHECK(result.duration_s > 0.99 && result.duration_s < 1.01);
    }

    // ----- Case 2: stereo 48 kHz 32-bit float => downmix to mono.
    {
        const uint32_t rate = 48000;
        const size_t frames = rate;        // 1 s
        std::vector<uint8_t> bytes(frames * 2u * 4u, 0);   // ch=2, bps=32 => 8 B/frame
        auto wav = tmp_root / "case2_stereo_48k_f32.wav";
        MM_CHECK(writeWav(wav, /*float=*/3, /*ch=*/2, rate, /*bps=*/32, bytes));

        mt::ReplayConfig cfg{};
        mt::ReplayResult result = mt::replayWav(wav, cfg);
        MM_CHECK(result.exit_code == 0);
        MM_CHECK(result.channels == 1);
    }

    // ----- Case 3: 44.1 kHz mono => linear-resampled to detector's expected 48 kHz.
    {
        const uint32_t rate = 44100;
        std::vector<uint8_t> bytes(rate * 2u, 0);          // 1 s of silence at source rate
        auto wav = tmp_root / "case3_mono_44k1_16.wav";
        MM_CHECK(writeWav(wav, /*pcm=*/1, /*ch=*/1, rate, /*bps=*/16, bytes));

        mt::ReplayConfig cfg{};
        cfg.target_sample_rate = 48000;
        mt::ReplayResult result = mt::replayWav(wav, cfg);
        MM_CHECK(result.exit_code == 0);
        // Linear resample: 44100 -> 48000 => ~48000 frames out, tolerance ±2.
        MM_CHECK(result.frames_out + 2 >= 48000);
        MM_CHECK(result.frames_out <= 48000 + 2);
    }

    // ----- Case 4: 8-bit PCM => exit_code 2 (unsupported per D-32).
    {
        const uint32_t rate = 48000;
        std::vector<uint8_t> bytes(rate, 0x80);   // 1 s, 1 B/sample
        auto wav = tmp_root / "case4_mono_48k_8.wav";
        MM_CHECK(writeWav(wav, /*pcm=*/1, /*ch=*/1, rate, /*bps=*/8, bytes));

        mt::ReplayConfig cfg{};
        mt::ReplayResult result = mt::replayWav(wav, cfg);
        MM_CHECK(result.exit_code == 2);
    }

    // ----- Case 5: truncated 1-hour header + cfg.max_duration_s = 600 => exit_code 2.
    {
        const uint32_t rate = 48000;
        // Declared data size: 3600 s of mono 16-bit = 3600 * rate * 2 bytes.
        const uint32_t declared = 3600u * rate * 2u;
        std::vector<uint8_t> bytes(rate * 2u, 0);   // only 1 s on disk (truncated)
        auto wav = tmp_root / "case5_one_hour_truncated.wav";
        MM_CHECK(writeWav(wav, /*pcm=*/1, /*ch=*/1, rate, /*bps=*/16, bytes,
                          /*override_data_size=*/declared));

        mt::ReplayConfig cfg{};
        cfg.max_duration_s = 600;
        mt::ReplayResult result = mt::replayWav(wav, cfg);
        MM_CHECK(result.exit_code == 2);
    }

    // ----- Case 6: determinism — replay same WAV 3x, byte-identical trigger output.
    {
        const uint32_t rate = 48000;
        std::vector<uint8_t> bytes(rate * 2u, 0);
        auto wav = tmp_root / "case6_determinism.wav";
        MM_CHECK(writeWav(wav, /*pcm=*/1, /*ch=*/1, rate, /*bps=*/16, bytes));

        mt::ReplayConfig cfg{};
        mt::ReplayResult r1 = mt::replayWav(wav, cfg);
        mt::ReplayResult r2 = mt::replayWav(wav, cfg);
        mt::ReplayResult r3 = mt::replayWav(wav, cfg);
        MM_CHECK(r1.trigger_output == r2.trigger_output);
        MM_CHECK(r2.trigger_output == r3.trigger_output);
    }

    // ----- Case 7: JSON output schema (D-30).
    {
        std::vector<mt::ReplayResult> results;
        results.emplace_back();   // single empty result is sufficient for shape
        auto out_path = tmp_root / "case7_out.json";
        MM_CHECK(mt::writeJsonOutput(out_path, results));
        std::ifstream in(out_path);
        MM_CHECK(in.good());
        std::stringstream ss;
        ss << in.rdbuf();
        auto parsed = nlohmann::json::parse(ss.str());
        MM_CHECK(parsed.contains("config_path"));
        MM_CHECK(parsed.contains("profile_path"));
        MM_CHECK(parsed.contains("files"));
        MM_CHECK(parsed.contains("summary"));
    }

    fs::remove_all(tmp_root, ec);
    std::cout << "all tests passed\n";
    return 0;
}

/**
 * @file wav_replay.cpp
 * @brief Implementation of the WAV replay harness. See wav_replay.hpp for API.
 *
 * Policy reminders (Phase 9 / TEST-04):
 *   - D-32: stereo->mono averaged downmix; linear-interp resample to
 *           ReplayConfig::target_sample_rate; 16-bit PCM and 32-bit float
 *           supported; reject 8 / 24 / 64-bit with exit code 2.
 *   - D-33: flat-out pacing — no realtime sleeps / steady_clock waits.
 *   - D-34: dt = block_count * 1000 / sample_rate. Determinism: identical
 *           input + identical detector state == identical TriggerEvent stream.
 *   - D-37 / TEST-01: ZERO OpenVR symbols. Lint enforces.
 *
 * Implementation notes:
 *   - dr_wav is included exactly once with DR_WAV_IMPLEMENTATION here.
 *   - The header forward-declares INoiseDetector / IStateMachine; this TU
 *     pulls the full headers so compiles are local to this file.
 *   - JSON I/O uses nlohmann/json directly — mic_test is a binary, not part
 *     of the shared core that AssertNoJsonInCore protects.
 */
#include "wav_replay.hpp"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "micmap/detection/noise_detector.hpp"
#include "micmap/core/state_machine.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace micmap::mic_test {

namespace {

// Matches driver SampleRing<16, 480> block size (P7 D-04 / 09-PATTERNS.md).
// Keeps replay-side block boundaries identical to the live audio path so the
// detector and state machine see the same frame-count rhythm.
constexpr std::size_t kBlockFrames = 480;

// Convert interleaved L+R stereo to mono by averaging.
void downmixStereoToMono(const float* in_lr,
                         std::size_t frame_count,
                         std::vector<float>& out_mono) {
    out_mono.resize(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        out_mono[i] = 0.5f * (in_lr[2 * i] + in_lr[2 * i + 1]);
    }
}

// Generic N-channel -> mono fold (averages all channels per frame).
void downmixNChannelsToMono(const float* in,
                            std::size_t frame_count,
                            std::uint16_t channels,
                            std::vector<float>& out_mono) {
    out_mono.resize(frame_count);
    const float inv = 1.0f / static_cast<float>(channels);
    for (std::size_t i = 0; i < frame_count; ++i) {
        float sum = 0.0f;
        for (std::uint16_t c = 0; c < channels; ++c) {
            sum += in[i * channels + c];
        }
        out_mono[i] = sum * inv;
    }
}

// Linear-interp resample mono buffer from src_rate to dst_rate.
// D-32: deterministic, low-quality (regression testing only — not for audio fidelity).
std::vector<float> linearResample(const std::vector<float>& in,
                                  std::uint32_t src_rate,
                                  std::uint32_t dst_rate) {
    if (src_rate == dst_rate || in.empty()) return in;
    const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const std::size_t out_count =
        static_cast<std::size_t>(static_cast<double>(in.size()) * ratio + 0.5);
    std::vector<float> out(out_count);
    const std::size_t last = in.size() - 1;
    for (std::size_t i = 0; i < out_count; ++i) {
        const double src_pos = static_cast<double>(i) / ratio;
        const std::size_t lo = static_cast<std::size_t>(src_pos);
        const std::size_t hi = (lo + 1 <= last) ? lo + 1 : last;
        const float frac = static_cast<float>(src_pos - static_cast<double>(lo));
        const std::size_t loc = (lo <= last) ? lo : last;
        out[i] = in[loc] * (1.0f - frac) + in[hi] * frac;
    }
    return out;
}

// Pre-scan the RIFF header to recover the *declared* data-chunk byte count
// before dr_wav silently clamps it to the on-disk file size (dr_wav.h line
// ~3763). This is the T-09-04-01 DoS mitigation surface: a malicious WAV that
// advertises a 1-hour data chunk while shipping only 1 s of bytes would
// otherwise sail past the max-duration guard because dr_wav reports the
// clamped duration as totalPCMFrameCount/sampleRate.
//
// Returns true on success and writes (declared_data_bytes, channels,
// sample_rate, bits_per_sample) into the out-params. Returns false if the
// file is unreadable or not a recognisable RIFF/WAVE container — in that
// case the caller falls back to dr_wav's own diagnostics.
bool peekWavHeader(const std::filesystem::path& wav,
                   std::uint64_t& out_declared_data_bytes,
                   std::uint16_t& out_channels,
                   std::uint32_t& out_sample_rate,
                   std::uint16_t& out_bits_per_sample) {
    std::ifstream f(wav, std::ios::binary);
    if (!f) return false;

    auto rd_u32 = [&](std::uint32_t& v) -> bool {
        unsigned char b[4];
        if (!f.read(reinterpret_cast<char*>(b), 4)) return false;
        v = static_cast<std::uint32_t>(b[0])
          | (static_cast<std::uint32_t>(b[1]) << 8)
          | (static_cast<std::uint32_t>(b[2]) << 16)
          | (static_cast<std::uint32_t>(b[3]) << 24);
        return true;
    };
    auto rd_u16 = [&](std::uint16_t& v) -> bool {
        unsigned char b[2];
        if (!f.read(reinterpret_cast<char*>(b), 2)) return false;
        v = static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[0])
          | (static_cast<std::uint16_t>(b[1]) << 8));
        return true;
    };
    auto rd_id = [&](char id[4]) -> bool { return static_cast<bool>(f.read(id, 4)); };

    char riff[4]; if (!rd_id(riff) || std::memcmp(riff, "RIFF", 4) != 0) return false;
    std::uint32_t riff_size; if (!rd_u32(riff_size)) return false;
    char wave[4]; if (!rd_id(wave) || std::memcmp(wave, "WAVE", 4) != 0) return false;

    // Walk chunks until "fmt " and "data" are both found.
    bool have_fmt = false, have_data = false;
    while (f && (!have_fmt || !have_data)) {
        char id[4];
        if (!rd_id(id)) break;
        std::uint32_t chunk_size;
        if (!rd_u32(chunk_size)) break;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            std::uint16_t fmt_tag;
            std::uint16_t channels;
            std::uint32_t sample_rate;
            std::uint32_t byte_rate_unused;
            std::uint16_t block_align_unused;
            std::uint16_t bps;
            if (!rd_u16(fmt_tag) || !rd_u16(channels) || !rd_u32(sample_rate)
                || !rd_u32(byte_rate_unused) || !rd_u16(block_align_unused)
                || !rd_u16(bps)) break;
            out_channels = channels;
            out_sample_rate = sample_rate;
            out_bits_per_sample = bps;
            have_fmt = true;
            // Skip any extra fmt bytes (e.g. WAVE_FORMAT_EXTENSIBLE).
            if (chunk_size > 16) {
                f.seekg(chunk_size - 16, std::ios::cur);
            }
            // Pad byte alignment.
            if (chunk_size & 1u) f.seekg(1, std::ios::cur);
        } else if (std::memcmp(id, "data", 4) == 0) {
            out_declared_data_bytes = static_cast<std::uint64_t>(chunk_size);
            have_data = true;
            // Don't read the data — we've got what we need.
            break;
        } else {
            // Skip unknown chunk + pad byte.
            f.seekg(chunk_size + (chunk_size & 1u), std::ios::cur);
        }
    }
    return have_fmt && have_data;
}

ReplayResult makeError(const std::filesystem::path& wav, int code, const std::string& msg) {
    ReplayResult r;
    r.wav = wav;
    r.exit_code = code;
    r.error_message = msg;
    r.pass = false;
    std::cerr << "error: " << wav.string() << ": " << msg << "\n";
    return r;
}

// Decode-and-validate path shared by both replayWav overloads.
//
// On success, populates `r.duration_s`, `r.sample_rate` (target rate post-resample),
// `r.channels` (== 1 mono after downmix), `r.frames_out`, and `out_samples`
// with the resampled mono buffer.
//
// On failure, returns false; caller propagates the populated `r.exit_code` /
// `r.error_message` to the user. (`r` is initialised by makeError on the
// failure paths.)
bool decodeWav(const std::filesystem::path& wav,
               const ReplayConfig& cfg,
               ReplayResult& r,
               std::vector<float>& out_samples) {
    if (!std::filesystem::exists(wav)) {
        r = makeError(wav, 2, "file not found");
        return false;
    }

    // T-09-04-01 DoS gate: verify the *declared* WAV duration before letting
    // dr_wav silently clamp dataChunkSize down to the on-disk file size
    // (dr_wav.h ~3763). A malicious or accidentally truncated file that
    // claims hours of audio while shipping seconds must fail here.
    {
        std::uint64_t declared_data_bytes = 0;
        std::uint16_t pre_channels = 0;
        std::uint32_t pre_sample_rate = 0;
        std::uint16_t pre_bps = 0;
        if (peekWavHeader(wav, declared_data_bytes, pre_channels,
                          pre_sample_rate, pre_bps)
                && pre_channels > 0 && pre_sample_rate > 0 && pre_bps > 0) {
            const std::uint64_t bytes_per_frame =
                static_cast<std::uint64_t>(pre_channels) *
                static_cast<std::uint64_t>(pre_bps / 8u);
            if (bytes_per_frame > 0) {
                const std::uint64_t declared_frames =
                    declared_data_bytes / bytes_per_frame;
                const double declared_duration_s =
                    static_cast<double>(declared_frames) /
                    static_cast<double>(pre_sample_rate);
                if (declared_duration_s > static_cast<double>(cfg.max_duration_s)) {
                    std::ostringstream msg;
                    msg << "declared duration " << declared_duration_s
                        << "s exceeds --max-duration "
                        << cfg.max_duration_s << "s";
                    r = makeError(wav, 2, msg.str());
                    return false;
                }
            }
        }
        // peekWavHeader failure is non-fatal — dr_wav's own diagnostics
        // surface the malformed-RIFF case below.
    }

#ifdef _WIN32
    // wstring path on Windows — drwav_init_file_w respects native UTF-16 paths.
    drwav w;
    if (!drwav_init_file_w(&w, wav.wstring().c_str(), nullptr)) {
        r = makeError(wav, 2, "WAV open failed (drwav_init_file_w)");
        return false;
    }
#else
    drwav w;
    if (!drwav_init_file(&w, wav.string().c_str(), nullptr)) {
        r = makeError(wav, 2, "WAV open failed (drwav_init_file)");
        return false;
    }
#endif

    // Capture metadata BEFORE drwav_uninit (which zeroes the struct).
    const std::uint32_t src_rate    = w.sampleRate;
    const std::uint16_t src_channels = w.channels;
    const std::uint32_t bps         = w.bitsPerSample;
    const std::uint16_t fmt_tag     = w.translatedFormatTag;
    const drwav_uint64 total_frames = w.totalPCMFrameCount;

    // D-32: bit-depth gate.
    const bool is_16bit_pcm   = (bps == 16);
    const bool is_32bit_float = (bps == 32 && fmt_tag == DR_WAVE_FORMAT_IEEE_FLOAT);
    if (!is_16bit_pcm && !is_32bit_float) {
        std::ostringstream msg;
        msg << "unsupported bit depth " << bps
            << " (only 16-bit PCM and 32-bit float supported)";
        drwav_uninit(&w);
        r = makeError(wav, 2, msg.str());
        return false;
    }

    // D-32: max-duration guard — blocks oversize WAVs (also catches the
    // case where the RIFF data chunk advertises a multi-hour size).
    const double duration_s =
        (src_rate > 0)
            ? static_cast<double>(total_frames) / static_cast<double>(src_rate)
            : 0.0;
    if (duration_s > static_cast<double>(cfg.max_duration_s)) {
        std::ostringstream msg;
        msg << "duration " << duration_s << "s exceeds --max-duration "
            << cfg.max_duration_s << "s";
        drwav_uninit(&w);
        r = makeError(wav, 2, msg.str());
        return false;
    }

    // Read entire file as f32 (interleaved if multi-channel).
    std::vector<float> raw(static_cast<std::size_t>(total_frames) * src_channels);
    drwav_uint64 frames_read = 0;
    if (total_frames > 0) {
        frames_read = drwav_read_pcm_frames_f32(&w, total_frames, raw.data());
    }
    drwav_uninit(&w);
    if (frames_read != total_frames) {
        r = makeError(wav, 2, "drwav_read_pcm_frames_f32 short read");
        return false;
    }

    // Downmix to mono.
    std::vector<float> mono;
    if (src_channels == 1) {
        mono = std::move(raw);
    } else if (src_channels == 2) {
        downmixStereoToMono(raw.data(), static_cast<std::size_t>(total_frames), mono);
    } else if (src_channels > 0) {
        downmixNChannelsToMono(raw.data(),
                               static_cast<std::size_t>(total_frames),
                               src_channels, mono);
    } else {
        r = makeError(wav, 2, "WAV reports 0 channels");
        return false;
    }

    // Resample to the configured target rate. linearResample is a no-op when
    // src_rate == cfg.target_sample_rate; that's the common 48 kHz path.
    out_samples = linearResample(mono, src_rate, cfg.target_sample_rate);

    r.wav = wav;
    r.duration_s = duration_s;
    r.sample_rate = cfg.target_sample_rate;   // post-resample
    r.channels = 1;                           // post-downmix
    r.frames_out = out_samples.size();
    r.exit_code = 0;
    r.pass = true;
    return true;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// Public API — decode-only overload (test-scaffold form).

ReplayResult replayWav(const std::filesystem::path& wav, const ReplayConfig& cfg) {
    ReplayResult r;
    std::vector<float> samples;
    if (!decodeWav(wav, cfg, r, samples)) {
        return r;
    }
    // No detector; observed_triggers stays 0, no expectation set, pass=true.
    return r;
}

// -----------------------------------------------------------------------------
// Public API — detect-and-trigger overload.

ReplayResult replayWav(const std::filesystem::path& wav,
                       const ReplayConfig& cfg,
                       micmap::detection::INoiseDetector& detector,
                       micmap::core::IStateMachine& sm,
                       std::optional<int> expected_triggers,
                       int tolerance) {
    ReplayResult r;
    std::vector<float> samples;
    if (!decodeWav(wav, cfg, r, samples)) {
        return r;
    }

    r.expected_triggers = expected_triggers;
    r.tolerance = tolerance;

    // D-33 flat-out / D-34 dt-pure: walk the resampled mono buffer in
    // kBlockFrames-sized slices, ask the detector + state machine to advance,
    // and record rising-edge Triggered transitions.
    const std::uint32_t rate = cfg.target_sample_rate ? cfg.target_sample_rate : 1;
    micmap::core::State last_state = micmap::core::State::Idle;

    std::size_t processed = 0;
    while (processed < samples.size()) {
        const std::size_t this_block =
            std::min<std::size_t>(kBlockFrames, samples.size() - processed);

        const auto result = detector.analyze(samples.data() + processed, this_block);

        // D-34: dt strictly from frame count and sample rate. Never steady_clock.
        const auto dt = std::chrono::milliseconds(
            static_cast<long long>(this_block) * 1000 / static_cast<long long>(rate));
        sm.update(result.confidence, dt);

        const auto state = sm.getCurrentState();
        if (state == micmap::core::State::Triggered
                && last_state != micmap::core::State::Triggered) {
            // Rising edge — record one trigger.
            TriggerEvent ev;
            ev.t_s = static_cast<double>(processed) / static_cast<double>(rate);
            ev.confidence = result.confidence;
            ev.state = "Triggered";
            r.triggers.push_back(ev);
            r.trigger_output.push_back(ev);   // determinism-test mirror
            r.observed_triggers++;
            if (cfg.verbose) {
                std::cout << "TRIGGER  " << wav.string()
                          << "  t=" << ev.t_s
                          << "s  confidence=" << ev.confidence << "\n";
            }
        }
        last_state = state;
        processed += this_block;
    }

    // Evaluate against expectation.
    if (r.expected_triggers.has_value()) {
        const int observed = r.observed_triggers;
        const int expected = *r.expected_triggers;
        const int diff = std::abs(observed - expected);
        r.pass = diff <= tolerance;
        r.exit_code = r.pass ? 0 : 1;
    } else {
        r.pass = true;
        r.exit_code = 0;
    }

    if (!cfg.quiet) {
        std::cout << (r.pass ? "PASS" : "FAIL") << "  " << wav.string()
                  << "  observed=" << r.observed_triggers
                  << " expected=" << (r.expected_triggers.has_value()
                        ? std::to_string(*r.expected_triggers) : std::string("n/a"))
                  << " (tolerance +/-" << r.tolerance << ")\n";
    }
    return r;
}

// -----------------------------------------------------------------------------
// Directory replay.

DirReplayResult replayWavDir(const std::filesystem::path& dir,
                             const ReplayConfig& cfg,
                             micmap::detection::INoiseDetector& detector,
                             micmap::core::IStateMachine& sm,
                             const std::filesystem::path& expectations_path) {
    DirReplayResult result;

    // Load manifest if provided. Unrecognised / missing entries simply leave
    // the file with no expectation (pass = true on any observed_triggers).
    std::map<std::string, std::pair<int, int>> expectations;   // wav-name -> (expected, tolerance)
    if (!expectations_path.empty() && std::filesystem::exists(expectations_path)) {
        try {
            std::ifstream f(expectations_path);
            nlohmann::json m = nlohmann::json::parse(f);
            const auto files_array = m.is_array()
                ? m
                : m.value("files", nlohmann::json::array());
            for (const auto& entry : files_array) {
                const auto name = entry.value("wav", std::string{});
                const int exp = entry.value("expected_triggers", 0);
                const int tol = entry.value("tolerance", 0);
                if (!name.empty()) {
                    expectations[name] = std::make_pair(exp, tol);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "manifest parse failed: " << e.what() << "\n";
        }
    }

    // Collect WAV files (recursive, sorted for determinism per D-34).
    std::vector<std::filesystem::path> wavs;
    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                wavs.push_back(entry.path());
            }
        }
    }
    std::sort(wavs.begin(), wavs.end());

    for (const auto& wav : wavs) {
        const auto rel = std::filesystem::relative(wav, dir).string();
        const auto basename = wav.filename().string();
        std::optional<int> expected;
        int tolerance = 0;
        auto it = expectations.find(rel);
        if (it == expectations.end()) {
            it = expectations.find(basename);
        }
        if (it != expectations.end()) {
            expected = it->second.first;
            tolerance = it->second.second;
        }

        auto r = replayWav(wav, cfg, detector, sm, expected, tolerance);
        if (r.pass) result.passed++;
        else        result.failed++;
        result.total++;
        result.files.push_back(std::move(r));
    }

    if (!cfg.quiet) {
        std::cout << "Replay corpus: " << result.passed << "/" << result.total
                  << " passed\n";
    }
    return result;
}

// -----------------------------------------------------------------------------
// JSON output — three overloads matching every observed call site.

namespace {

nlohmann::json fileToJson(const ReplayResult& r) {
    nlohmann::json f;
    f["wav"] = r.wav.string();
    f["duration_s"] = r.duration_s;
    f["sample_rate"] = r.sample_rate;
    f["channels"] = r.channels;
    f["expected_triggers"] = r.expected_triggers.has_value()
        ? nlohmann::json(*r.expected_triggers)
        : nlohmann::json(nullptr);
    f["observed_triggers"] = r.observed_triggers;
    f["tolerance"] = r.tolerance;
    f["pass"] = r.pass;
    f["frames_out"] = r.frames_out;
    f["triggers"] = nlohmann::json::array();
    for (const auto& t : r.triggers) {
        nlohmann::json tev;
        tev["t_s"] = t.t_s;
        tev["confidence"] = t.confidence;
        tev["state"] = t.state;
        f["triggers"].push_back(std::move(tev));
    }
    if (r.error_message.has_value()) {
        f["error"] = *r.error_message;
    }
    return f;
}

bool writeDocToFile(const std::filesystem::path& out_path, const nlohmann::json& doc) {
    std::ofstream out(out_path);
    if (!out.is_open()) return false;
    out << doc.dump(2);
    return out.good();
}

} // anonymous namespace

bool writeJsonOutput(const std::filesystem::path& out_path,
                     const std::vector<ReplayResult>& results) {
    nlohmann::json doc;
    doc["config_path"] = std::string{};
    doc["profile_path"] = std::string{};
    doc["files"] = nlohmann::json::array();
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        doc["files"].push_back(fileToJson(r));
        if (r.pass) ++passed; else ++failed;
    }
    nlohmann::json summary;
    summary["total"] = static_cast<int>(results.size());
    summary["passed"] = passed;
    summary["failed"] = failed;
    doc["summary"] = std::move(summary);
    return writeDocToFile(out_path, doc);
}

bool writeJsonOutput(const std::filesystem::path& out_path,
                     const ReplayConfig& cfg,
                     const DirReplayResult& result) {
    nlohmann::json doc;
    doc["config_path"] = cfg.config_path.string();
    doc["profile_path"] = cfg.profile_path.string();
    doc["files"] = nlohmann::json::array();
    for (const auto& r : result.files) {
        doc["files"].push_back(fileToJson(r));
    }
    nlohmann::json summary;
    summary["total"] = result.total;
    summary["passed"] = result.passed;
    summary["failed"] = result.failed;
    doc["summary"] = std::move(summary);
    return writeDocToFile(out_path, doc);
}

bool writeJsonOutput(const std::filesystem::path& out_path,
                     const ReplayConfig& cfg,
                     const ReplayResult& single) {
    DirReplayResult wrap;
    wrap.files.push_back(single);
    wrap.total = 1;
    wrap.passed = single.pass ? 1 : 0;
    wrap.failed = single.pass ? 0 : 1;
    return writeJsonOutput(out_path, cfg, wrap);
}

} // namespace micmap::mic_test

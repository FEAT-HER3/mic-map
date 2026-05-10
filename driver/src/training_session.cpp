/**
 * @file training_session.cpp
 * @brief Implementation of TrainingSession (P9 D-09..D-22, IPC-06).
 *
 * Verbatim port of the v1.5 client training idiom — see 09-PATTERNS.md
 * "v1.5 detector-as-trainer idiom" section. The driver TU consumes the
 * INoiseDetector interface only; no PatternTrainer host alongside the
 * detector (that would double-instantiate state). No save -> load
 * round-trip on recompute (recompute = setSensitivity in-place per
 * D-20).
 *
 * Pitfalls honored:
 *   1. Construction race: DeviceProvider release-stores mode_ AFTER
 *      constructing the session (mitigated in DeviceProvider — this
 *      TU just guarantees the ctor runs to completion before returning
 *      the object).
 *   2. Destruction race: DeviceProvider holds trainingMutex_ during
 *      reset; addSample re-locks mu_ on entry — serialization protects
 *      against in-flight addSample at the moment of reset (the actual
 *      window is closed by the mode_ flip + acquire-load discipline in
 *      DetectionRunner; mu_ is the belt-and-suspenders).
 *   3. Timeout watchdog vs finalize/cancel: tickTimeout, cancel, and
 *      markFinalized all take mu_; serialization is the protection.
 *   4. v1.5 detector internal rate-limiter: samples_collected_ counter
 *      increments per addSample call (matches v1.5 client convention at
 *      apps/micmap/main.cpp:404 — unconditional increment per audio
 *      callback frame).
 */

#include "training_session.hpp"
#include "micmap/common/logger.hpp"
#include "micmap/detection/noise_detector.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace micmap::driver {

TrainingSession::TrainingSession(uint32_t sampleRate, size_t fftSize)
    : sampleRate_(sampleRate)
    , fftSize_(fftSize)
    , detector_(micmap::detection::createFFTDetector(sampleRate, fftSize))
    , lastAcceptedSample_(std::chrono::steady_clock::now()) {
    if (!detector_) {
        // createFFTDetector should never fail (returns make_unique<FFTNoiseDetector>
        // unconditionally per src/detection/src/noise_detector.cpp:745-746) but
        // defend anyway — a null detector here means the session can never make
        // progress; mark it Cancelled so the HTTP thread will return 500 on
        // start instead of silently hanging.
        MICMAP_LOG_ERROR("TrainingSession: createFFTDetector returned nullptr");
        state_ = SessionState::Cancelled;
        last_error_ = std::string("detector_construction_failed");
        return;
    }
    // Verbatim v1.5 idiom (apps/micmap/main.cpp:1006). Must run BEFORE the
    // detection thread observes mode_ == Training (DeviceProvider guarantees
    // this with release-store-after-construct discipline).
    detector_->startTraining();
    MICMAP_LOG_INFO("TrainingSession: started (sampleRate=", sampleRate,
                    ", fftSize=", fftSize, ", target=", target_, ")");
}

TrainingSession::~TrainingSession() {
    // The unique_ptr<INoiseDetector> destructor releases all internal sample
    // buffers (RAM-only per D-17) — never persisted. Single member; teardown
    // order trivially correct.
    MICMAP_LOG_INFO("TrainingSession: torn down "
                    "(samples_collected=", samples_collected_.load(std::memory_order_acquire),
                    ", state=", static_cast<int>(state_), ")");
}

bool TrainingSession::addSample(const float* samples, size_t count) {
    if (samples == nullptr || count == 0) return false;
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ != SessionState::Collecting) return false;
    if (!detector_) return false;
    // Verbatim v1.5 idiom (apps/micmap/main.cpp:404). v1.5 INoiseDetector::
    // addTrainingSample returns void; the v1.5 client convention increments
    // a sample-count regardless of detector-internal acceptance (which is
    // gated by isTraining() + an internal rate-limiter). We mirror that
    // convention so the user-visible progress matches the v1.5 UX exactly.
    detector_->addTrainingSample(samples, count);
    samples_collected_.fetch_add(1, std::memory_order_release);
    lastAcceptedSample_ = std::chrono::steady_clock::now();
    return true;
}

std::optional<std::string> TrainingSession::compute() {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ != SessionState::Collecting) {
        return std::string("compute: not in Collecting state");
    }
    if (!detector_) {
        state_ = SessionState::Cancelled;
        last_error_ = std::string("compute: detector unavailable");
        return *last_error_;
    }
    state_ = SessionState::Computing;
    // Verbatim v1.5 idiom — finishTraining computes the FFT spectral profile
    // + correlation/energy thresholds from the accumulated samples
    // (src/detection/src/noise_detector.cpp).
    if (!detector_->finishTraining()) {
        state_ = SessionState::Cancelled;
        last_error_ = std::string("compute: insufficient_samples_or_invalid_data");
        MICMAP_LOG_WARNING("TrainingSession: finishTraining returned false");
        return *last_error_;
    }

    // Build preview from the freshly-computed TrainingData. Mean / stddev of
    // the spectralProfile FFT bins gives the client a compact human-readable
    // summary without round-tripping the full bin vector through JSON
    // (D-22).
    const auto& td = detector_->getTrainingData();
    ThresholdsPreview p;
    p.sensitivity      = detector_->getSensitivity();
    p.energy_threshold = td.energyThreshold;
    const auto& bins   = td.spectralProfile;
    p.spectral_profile_summary.size = bins.size();
    if (!bins.empty()) {
        const double sum = std::accumulate(bins.begin(), bins.end(), 0.0);
        const double mean = sum / static_cast<double>(bins.size());
        double sq_diff = 0.0;
        for (float v : bins) {
            const double d = static_cast<double>(v) - mean;
            sq_diff += d * d;
        }
        p.spectral_profile_summary.mean   = static_cast<float>(mean);
        p.spectral_profile_summary.stddev =
            static_cast<float>(std::sqrt(sq_diff / static_cast<double>(bins.size())));
    }
    preview_ = p;
    state_ = SessionState::Ready;
    MICMAP_LOG_INFO("TrainingSession: compute complete "
                    "(sensitivity=", p.sensitivity,
                    ", energy_threshold=", p.energy_threshold,
                    ", profile_bins=", p.spectral_profile_summary.size, ")");
    return std::nullopt;
}

bool TrainingSession::recompute(float sensitivity) {
    if (!std::isfinite(sensitivity) || sensitivity < 0.0f || sensitivity > 1.0f) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ != SessionState::Ready) return false;
    if (!detector_) return false;
    // Verified canonical path per D-20: setSensitivity in-place, refresh
    // preview_->sensitivity from detector_. energy_threshold and
    // spectral_profile_summary are sample-derived (computed in finishTraining)
    // and therefore unchanged across sensitivity tweaks — no re-derivation
    // needed; no save -> load round-trip.
    detector_->setSensitivity(sensitivity);
    if (preview_.has_value()) {
        preview_->sensitivity = detector_->getSensitivity();
    }
    return true;
}

ProgressSnapshot TrainingSession::snapshot() const {
    ProgressSnapshot s;
    s.samples_collected = samples_collected_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(mu_);
    s.target             = target_;
    s.state              = state_;
    s.thresholds_preview = preview_;
    s.last_error         = last_error_;
    return s;
}

bool TrainingSession::tickTimeout(std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ != SessionState::Collecting) return false;
    if ((now - lastAcceptedSample_) < std::chrono::seconds(30)) return false;
    state_ = SessionState::Cancelled;
    last_error_ = std::string("training_timed_out_no_samples");
    MICMAP_LOG_INFO("TrainingSession: timed out (no accepted sample in 30 s); "
                    "transitioned to Cancelled");
    return true;
}

bool TrainingSession::maybeAutoCompute() {
    // Probe state + sample count under mu_, then release before compute()
    // (compute() re-acquires mu_; we cannot nest). The window between unlock
    // and compute() can race with cancel() / finalize() — compute() handles
    // the not-in-Collecting case via its own early-return so the race is safe.
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (state_ != SessionState::Collecting) return false;
        if (samples_collected_.load(std::memory_order_acquire) < target_) return false;
    }
    return !compute().has_value();
}

void TrainingSession::cancel() {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ == SessionState::Finalized) return;   // idempotent (D-13)
    state_ = SessionState::Cancelled;
    preview_ = std::nullopt;
    // Samples remain inside detector_'s internal buffers (RAM-only per D-17)
    // until DeviceProvider drops the unique_ptr<TrainingSession> in
    // resetTrainingSession() — the entire memory footprint goes away then.
}

void TrainingSession::markFinalized() {
    std::lock_guard<std::mutex> lock(mu_);
    state_ = SessionState::Finalized;
    MICMAP_LOG_INFO("TrainingSession: finalized");
}

micmap::detection::INoiseDetector* TrainingSession::detector() {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ == SessionState::Cancelled) return nullptr;
    return detector_.get();
}

SessionState TrainingSession::state() const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_;
}

size_t TrainingSession::samples_collected() const {
    return samples_collected_.load(std::memory_order_acquire);
}

size_t TrainingSession::target() const {
    std::lock_guard<std::mutex> lock(mu_);
    return target_;
}

std::optional<ThresholdsPreview> TrainingSession::preview() const {
    std::lock_guard<std::mutex> lock(mu_);
    return preview_;
}

std::optional<std::string> TrainingSession::last_error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return last_error_;
}

} // namespace micmap::driver

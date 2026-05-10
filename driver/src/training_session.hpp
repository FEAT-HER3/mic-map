/**
 * @file training_session.hpp
 * @brief Phase 9 / TRAIN-01..06 / D-09..D-22 / IPC-06: driver-side
 *        training-session state owner.
 *
 * Hosted from DeviceProvider as a lazy std::unique_ptr<TrainingSession>
 * (HTTP-thread-only construction/destruction per D-03). Hosts a single
 * std::unique_ptr<INoiseDetector> created via createFFTDetector — verbatim
 * port of the v1.5 client training idiom (apps/micmap/main.cpp:298 +
 * :1006 + :404 + :974 — deleted in 09-03 but preserved in git history).
 *
 * State machine: Collecting -> Computing -> Ready -> Finalized, with
 * Cancelled as a terminal sibling reachable from any non-Finalized state
 * (D-10). 30 s no-new-accepted-sample timeout transitions Collecting ->
 * Cancelled with last_error="training_timed_out_no_samples" (D-12).
 *
 * Threading discipline:
 *   - addSample is the ONLY hot-path method; called from DetectionRunner
 *     thread when DriverMode == Training. Pitfall 1 mitigation:
 *     DeviceProvider release-stores mode_ AFTER constructing this session
 *     so the detection thread's acquire-load observes a fully-constructed
 *     session.
 *   - Pitfall 2 mitigation: DeviceProvider holds trainingMutex_ during
 *     reset of the unique_ptr<TrainingSession>; DetectionRunner reads
 *     mode_ first, then dereferences trainingSession() — a transient
 *     null read is safe (drop the block).
 *   - All other methods (compute, recompute, snapshot, tickTimeout,
 *     cancel, markFinalized, detector accessors) run on the HTTP thread
 *     and serialize through the internal mu_.
 *
 * ZERO OpenVR API surface in this TU. ZERO header includes from OpenVR.
 * Lint AssertDetectionRunnerNoVrApi covers detection_runner.cpp; this
 * TU is held to the same standard manually until / unless extended.
 */

#pragma once

// settings_validator.hpp is included so the Wave 0 test scaffold's
// `md::validateFinalizePayload` reference resolves through the
// transitive include surface once 09-02 lands the validator declarations
// alongside the existing validateSettings entry-point (see 09-00 PLAN
// test case 5 commentary — "the include lives in training_session.hpp's
// transitive surface"). Until 09-02 the symbol is unresolved at LINK
// time; that is the documented partial-RED progression for Wave 0
// per 09-01-PLAN.md verification step #7.
#include "settings_validator.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace micmap::detection { class INoiseDetector; }   // forward decl; full include in .cpp

namespace micmap::driver {

/// @brief Lifecycle state of a training session.
///        Collecting -> Computing -> Ready -> Finalized linear progression
///        with Cancelled as a terminal sibling reachable from any
///        non-Finalized state (D-10).
enum class SessionState : uint8_t {
    Collecting,
    Computing,
    Ready,
    Finalized,
    Cancelled
};

/// @brief Compact summary of the trained spectral profile FFT bins.
///        Surfaced through GET /training/progress when state == Ready
///        (D-22). Mean / stddev computed from INoiseDetector::getTrainingData()
///        ::spectralProfile.
struct SpectralProfileSummary {
    float  mean{0.0f};
    float  stddev{0.0f};
    size_t size{0};
};

/// @brief Read-only preview of the trained thresholds. Populated when
///        compute() succeeds; refreshed on recompute(). Sample-derived
///        fields (energy_threshold, spectral_profile_summary) stay
///        constant across recompute calls — sensitivity gates the
///        correlation threshold inside INoiseDetector::analyze, NOT the
///        energy floor or spectral profile (verified at
///        src/detection/src/noise_detector.cpp:329 v1.5 algorithm).
struct ThresholdsPreview {
    float                  sensitivity{0.0f};
    float                  energy_threshold{0.0f};
    SpectralProfileSummary spectral_profile_summary{};
};

/// @brief Snapshot returned to the GET /training/progress handler
///        (D-22). target denominator is surfaced through this snapshot
///        so the client UI matches the driver-side count without
///        endpoint payload coupling (D-11).
struct ProgressSnapshot {
    size_t                           samples_collected{0};
    size_t                           target{100};   // PatternTrainer minSamples * 3 default per v1.5; D-11/D-22
    std::optional<ThresholdsPreview> thresholds_preview;
    SessionState                     state{SessionState::Collecting};
    std::optional<std::string>       last_error;
};

/// @brief Driver-side training session.
class TrainingSession {
public:
    /// @brief Constructed from DeviceProvider on first POST /training/start.
    ///        sampleRate / fftSize forward to createFFTDetector(); should
    ///        match AudioWorker capture rate (sample rate consistency is
    ///        the caller's responsibility — DeviceProvider reads the
    ///        WASAPI-negotiated rate from AudioWorker before constructing
    ///        this session).
    TrainingSession(uint32_t sampleRate, size_t fftSize);
    ~TrainingSession();

    TrainingSession(const TrainingSession&)            = delete;
    TrainingSession& operator=(const TrainingSession&) = delete;
    TrainingSession(TrainingSession&&)                 = delete;
    TrainingSession& operator=(TrainingSession&&)      = delete;

    /// @brief Hot path. Called from DetectionRunner thread when
    ///        DriverMode == Training. Returns true iff the sample was
    ///        accepted (state == Collecting). On accept: increments
    ///        samples_collected_ and stamps lastAcceptedSample_ to
    ///        re-arm the 30 s timeout watchdog. v1.5 client convention
    ///        (apps/micmap/main.cpp:404) increments unconditionally per
    ///        addTrainingSample call.
    bool addSample(const float* samples, size_t count);

    /// @brief HTTP-thread. Transitions Collecting -> Computing -> Ready
    ///        by calling detector_->finishTraining(). Returns nullopt on
    ///        success; an error string on insufficient samples / wrong
    ///        state / detector failure (state -> Cancelled in that case).
    std::optional<std::string> compute();

    /// @brief HTTP-thread. Only valid in Ready (D-20). Calls
    ///        detector_->setSensitivity(s) directly (verified canonical
    ///        path); refreshes preview_->sensitivity from
    ///        detector_->getSensitivity(). Returns true if recomputed;
    ///        false on out-of-range, non-finite, or wrong state.
    bool recompute(float sensitivity);

    /// @brief HTTP-thread. Returns the current snapshot for
    ///        GET /training/progress responses (D-22). Lock-free for
    ///        samples_collected (atomic acquire-load); mu_-locked for
    ///        the rest.
    ProgressSnapshot snapshot() const;

    /// @brief HTTP-thread. Called once per /training/progress poll
    ///        (cheap; same thread). If state == Collecting and
    ///        (now - lastAcceptedSample_) >= 30s, transitions to
    ///        Cancelled with last_error="training_timed_out_no_samples".
    ///        Returns true iff a state transition occurred this call.
    bool tickTimeout(std::chrono::steady_clock::time_point now);

    /// @brief Detection-thread. Called once per detection-runner ring
    ///        drain after addSample(). If state == Collecting and
    ///        samples_collected >= target, runs compute() inline to
    ///        transition Collecting -> Ready (or Cancelled on
    ///        insufficient_samples_or_invalid_data). Required because
    ///        the UI-SPEC's "Cover mic -> Ready preview" flow assumes
    ///        the driver auto-transitions when samples reach the
    ///        configured target — neither addSample nor finalize alone
    ///        provide that path. UAT D-39(1)/(3) gap fix.
    ///        Returns true iff compute() succeeded (state moved to Ready).
    bool maybeAutoCompute();

    /// @brief HTTP-thread. Explicit cancel (POST /training/cancel
    ///        handler). Idempotent — safe to call from any non-Finalized
    ///        state (D-13). On Finalized: no-op.
    void cancel();

    /// @brief HTTP-thread. Called by the finalize handler (09-02) AFTER
    ///        a successful disk write to mark the session terminal.
    ///        After this returns, DeviceProvider should reset its
    ///        unique_ptr<TrainingSession>.
    void markFinalized();

    /// @brief Accessor for the finalize handler — gives it the trained
    ///        INoiseDetector to pass to training_io::saveTrainingFile.
    ///        detector_ is created in the ctor and survives the entire
    ///        session lifecycle (single instance — v1.5 idiom).
    ///        Returns nullptr only if state == Cancelled (defensive —
    ///        callers should not persist a cancelled session).
    micmap::detection::INoiseDetector* detector();

    SessionState                     state() const;
    size_t                           samples_collected() const;   // lock-free; atomic acquire-load
    size_t                           target() const;
    std::optional<ThresholdsPreview> preview() const;
    std::optional<std::string>       last_error() const;

private:
    const uint32_t sampleRate_;
    const size_t   fftSize_;

    // Single detector for the entire session lifecycle: training,
    // recompute, persistence. Constructed in ctor via createFFTDetector
    // (verbatim port of the v1.5 client idiom at
    // apps/micmap/main.cpp:298 + :1006).
    std::unique_ptr<micmap::detection::INoiseDetector> detector_;

    // Lock-free progress counter (read by HTTP thread on every
    // /training/progress poll; written by detection thread on every
    // accepted addSample call). mirrors the discipline at
    // detection_runner.cpp:101 for activeConfig_ atomic-load.
    std::atomic<size_t>                     samples_collected_{0};

    SessionState                            state_{SessionState::Collecting};
    std::optional<ThresholdsPreview>        preview_;
    std::optional<std::string>              last_error_;
    std::chrono::steady_clock::time_point   lastAcceptedSample_;
    size_t                                  target_{100};   // v1.5 default; D-11
    mutable std::mutex                      mu_;
};

} // namespace micmap::driver

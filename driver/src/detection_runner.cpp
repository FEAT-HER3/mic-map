// driver/src/detection_runner.cpp
//
// Phase 7 / D-17 / D-18 / D-20 / D-22.
// Pitfall 3, 4, 12 mitigations live here.
//
// The detection thread drains the SPSC SampleRing, runs the FFT noise
// detector + the rising-edge state machine, and pushes TapCommand into
// the existing v1.5 CommandQueue on rising-edge Triggered. The
// HTTP-thread -> CommandQueue -> RunFrame v1.5 SVR-05 boundary is
// preserved verbatim (DetectionRunner is a NEW PRODUCER of an existing
// primitive; nothing about RunFrame changes).
//
// Pitfall 12 / D-18: state-machine cooldown is the natural rate-limiter
// on commandQueue_.push -- at most one TapCommand per cooldown window,
// regardless of FFT cadence. No extra throttle.
//
// Pitfall 4 / Shared Pattern 4: detector_ + stateMachine_ are reset on
// the detection thread (inside RunLoop, after the loop body exits) so
// their destructors run on the same thread that constructed them.
//
// Pitfall 13 / D-23: NO COM device-notifier registration here. The
// audio-device-removed surface stays in WASAPIAudioCapture (registered
// inside AudioWorker per P6 D-15).

#include "detection_runner.hpp"
#include "driver_log.hpp"

// P6 lifted the P5 link-only restriction -- driver TUs may now #include
// shared-lib headers (driver/CMakeLists.txt:79 comment). The include
// paths are exported via INTERFACE on micmap::core_runtime (P5 D-10).
#include <micmap/detection/noise_detector.hpp>
#include <micmap/core/state_machine.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace micmap::driver {

namespace {

// 2 s shutdown watchdog (D-13 / D-20) matching P6 AudioWorker pattern
// (audio_worker.cpp:55-59). On overrun: log + thread_.detach() (T3
// mitigation -- never block vrserver.exe shutdown).
constexpr auto kShutdownWatchdog = std::chrono::seconds(2);
constexpr auto kWatchdogPoll     = std::chrono::milliseconds(25);

// Per-iteration cv_.wait_for timeout. Claude's discretion confirmed at
// 50 ms (RESEARCH.md Standard Stack alternatives -- balances Cleanup
// latency vs idle CPU). Worst-case MIG-06 propagation = one wakeup
// interval = 50 ms; well inside SC5 < 50 ms gate. publish() also
// notifies cv_ so propagation latency is bounded by store->notify->ack
// rather than by the timeout when the ring is empty.
constexpr auto kWakeTimeout      = std::chrono::milliseconds(50);

} // namespace

DetectionRunner::DetectionRunner(SampleRing<16, 480>& ring,
                                 CommandQueue& commandQueue,
                                 uint32_t sampleRate,
                                 DetectionConfig initial)
    : ring_(ring)
    , commandQueue_(commandQueue)
    , sampleRate_(sampleRate)
{
    // Seed activeConfig_ with the initial snapshot so Start can construct
    // detector + state machine without a publish() race. lastObserved_ is
    // populated on the first loop iteration when the detection thread
    // observes activeConfig_ (pointer-identity differs from null cache).
    auto seed = std::make_shared<const DetectionConfig>(initial);
    std::atomic_store_explicit(&activeConfig_, seed, std::memory_order_release);
}

DetectionRunner::~DetectionRunner() { Stop(); }

bool DetectionRunner::Start() {
    if (running_.load(std::memory_order_acquire)) {
        DriverLog("MicMap detection: Start called while already running\n");
        return thread_.joinable();
    }

    // Construct detector + state machine on the calling thread (cheap
    // factory calls; the threading discipline lives in the loop body,
    // not in setup).
    auto cfg = std::atomic_load_explicit(&activeConfig_, std::memory_order_acquire);
    detector_ = micmap::detection::createFFTDetector(sampleRate_, /*fftSize=*/2048);
    if (!detector_) {
        DriverLog("MicMap detection: createFFTDetector returned null - Start failed\n");
        return false;
    }

    // P7 D-25(1) gap closure: load existing v1.5 training profile so detection
    // can fire on real hardware. P7 is read-only consumer; P9 (Training
    // Migration) makes the driver the sole writer of training_data.bin.
    // Fail-soft: missing/corrupt profile leaves detector untrained, analyze()
    // returns near-zero confidence, state machine never fires; driver stays
    // alive so the v1.5 client POST /button fallback remains usable.
    {
        std::filesystem::path profile_path;
#ifdef _WIN32
        wchar_t path_buf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path_buf))) {
            profile_path = std::filesystem::path(path_buf) / L"MicMap" / L"training_data.bin";
        }
#endif
        if (!profile_path.empty() && std::filesystem::exists(profile_path)) {
            if (detector_->loadTrainingData(profile_path)) {
                DriverLog("MicMap detection: loaded training profile from %s\n",
                          profile_path.string().c_str());
            } else {
                DriverLog("MicMap detection: loadTrainingData failed for %s - detection inert until valid profile\n",
                          profile_path.string().c_str());
            }
        } else {
            DriverLog("MicMap detection: no training profile at %s - detection inert until valid profile\n",
                      profile_path.string().c_str());
        }
    }

    // StateMachineConfig field names per src/core/include/micmap/core/state_machine.hpp:17-21:
    //   detectionThreshold (float)
    //   minDetectionDuration (std::chrono::milliseconds)
    //   cooldownDuration (std::chrono::milliseconds)
    micmap::core::StateMachineConfig smCfg;
    smCfg.detectionThreshold   = cfg->threshold;
    smCfg.minDetectionDuration = std::chrono::milliseconds(cfg->min_duration_ms);
    smCfg.cooldownDuration     = std::chrono::milliseconds(cfg->cooldown_ms);
    stateMachine_ = micmap::core::createStateMachine(smCfg);
    if (!stateMachine_) {
        DriverLog("MicMap detection: createStateMachine returned null - Start failed\n");
        detector_.reset();
        return false;
    }
    applyConfig(*cfg);   // pushes sensitivity into detector if API exposes it

    shutdown_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    thread_finished_.store(false, std::memory_order_release);
    triggers_.store(0, std::memory_order_release);

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&DetectionRunner::ThreadEntry, this);
    DriverLog("MicMap detection: thread spawned (sampleRate=%u, fftSize=2048)\n",
              sampleRate_);
    return thread_.joinable();
}

void DetectionRunner::Stop() {
    if (!thread_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_.store(true, std::memory_order_release);
    }
    cv_.notify_all();

    // 2 s watchdog matching P6 AudioWorker (audio_worker.cpp:104-120 / D-13 / D-20).
    // Poll thread_finished_ every 25 ms; on overrun log + detach (T3 mitigation
    // -- never block vrserver.exe shutdown).
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + kShutdownWatchdog;
    while (!thread_finished_.load(std::memory_order_acquire) &&
           clock::now() < deadline) {
        std::this_thread::sleep_for(kWatchdogPoll);
    }
    if (thread_finished_.load(std::memory_order_acquire)) {
        thread_.join();
        DriverLog("MicMap detection: thread joined cleanly\n");
    } else {
        // P7 REVIEW WR-01: do NOT detach. The detection thread holds a raw
        // reference to AudioWorker's SampleRing (member ring_) and to its own
        // detector_/stateMachine_/this. Detaching here lets DeviceProvider::
        // Cleanup proceed to audioWorker_.reset() while the detection thread is
        // still calling ring_.has_data() / try_pop -- a use-after-free on the
        // ring AND on `this` (commandQueue_, triggers_, cv_, mu_) once
        // ~DetectionRunner returns. The original T3 "never block vrserver.exe"
        // rationale is valid for AudioWorker (whose teardown crosses COM /
        // WASAPI), but DetectionRunner's only blocking surface is
        // cv_.wait_for(50ms); shutdown_ has already been set + notified, so the
        // worst additional wait is ~one timeout period after we get here. A
        // watchdog overrun means something is genuinely wrong -- prefer the
        // diagnosable hang over silent UAF.
        DriverLog("MicMap detection: WARNING - did not exit within 2 s watchdog; "
                  "joining anyway to avoid UAF on AudioWorker ring teardown\n");
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

void DetectionRunner::Pause() {
    // Idempotent -- exchange returns the previous value; if already paused, no-op.
    // RESEARCH Open Question 5: RunFrame may receive duplicate EnterStandby
    // events; the early-return prevents log spam + wakeup storms.
    if (paused_.exchange(true, std::memory_order_acq_rel)) return;
    cv_.notify_one();
    DriverLog("MicMap detection: paused\n");
}

void DetectionRunner::Resume() {
    if (!paused_.exchange(false, std::memory_order_acq_rel)) return;
    cv_.notify_one();
    DriverLog("MicMap detection: resumed\n");
}

void DetectionRunner::publish(std::shared_ptr<const DetectionConfig> next) {
    // MIG-06 atomic-shared_ptr swap. C++17 free-function form. Detection
    // thread observes the new pointer at its next acquire-load (top of
    // RunLoop iteration). cv_.notify_one() so wait_for unblocks promptly
    // -- matters for the SC5 < 50 ms test when the ring has no pending data.
    std::atomic_store_explicit(&activeConfig_, std::move(next),
                               std::memory_order_release);
    cv_.notify_one();
}

void DetectionRunner::NotifyOne() {
    cv_.notify_one();
}

std::shared_ptr<const DetectionConfig> DetectionRunner::active_config_for_test() const {
    return std::atomic_load_explicit(&activeConfig_, std::memory_order_acquire);
}

void DetectionRunner::applyConfig(const DetectionConfig& cfg) {
    // Push runtime parameters into the detector + state machine where the
    // shared-lib API exposes live setters.
    //
    // INoiseDetector (src/detection/include/micmap/detection/noise_detector.hpp):
    //   - setSensitivity(float)         -- exposed (line 110)
    //   - setMinDetectionDuration(int)  -- exposed (line 126); state machine
    //                                      already enforces min-duration via
    //                                      its own clock, so we leave this to
    //                                      the state machine and skip pushing
    //                                      it into the detector to avoid
    //                                      double-counting the hold window.
    //
    // IStateMachine (src/core/include/micmap/core/state_machine.hpp):
    //   - configure(const StateMachineConfig&) -- exposed (line 74); rebuilds
    //                                             the entire config in one shot.
    if (detector_) {
        detector_->setSensitivity(cfg.sensitivity);
    }
    if (stateMachine_) {
        micmap::core::StateMachineConfig smCfg;
        smCfg.detectionThreshold   = cfg.threshold;
        smCfg.minDetectionDuration = std::chrono::milliseconds(cfg.min_duration_ms);
        smCfg.cooldownDuration     = std::chrono::milliseconds(cfg.cooldown_ms);
        stateMachine_->configure(smCfg);
    }
}

void DetectionRunner::ThreadEntry(DetectionRunner* self) {
    self->RunLoop();
}

void DetectionRunner::RunLoop() {
    using clock = std::chrono::steady_clock;
    auto last_tick = clock::now();

    // Trigger callback -- fires from inside stateMachine_->update on rising
    // edge (Triggered transition). State machine cooldown is the natural
    // CommandQueue rate-limiter (Pitfall 12 / D-18 -- at most one TapCommand
    // per cooldown window, regardless of FFT cadence). Set BEFORE the loop
    // body so the very first ring drain can fire it.
    stateMachine_->setTriggerCallback([this]() {
        commandQueue_.push(TapCommand{});
        const uint32_t n = triggers_.fetch_add(1, std::memory_order_relaxed) + 1;
        DriverLog("MicMap detection: TapCommand pushed (n=%u)\n", n);
    });

    std::array<float, 480> block{};
    size_t block_count = 0;

    while (true) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, kWakeTimeout, [this] {
                return shutdown_.load(std::memory_order_acquire)
                    || paused_.load(std::memory_order_acquire)
                    || ring_.has_data();
            });
        }
        if (shutdown_.load(std::memory_order_acquire)) break;

        // MIG-06: reload settings snapshot, apply if changed. Pointer-identity
        // compare suffices because publish() always allocates a fresh
        // shared_ptr (see ctor seed + publish step).
        auto cfg = std::atomic_load_explicit(&activeConfig_,
                                             std::memory_order_acquire);
        if (cfg.get() != lastObserved_.get()) {
            applyConfig(*cfg);
            lastObserved_ = cfg;
        }

        if (paused_.load(std::memory_order_acquire)) {
            // Drain-discard: keeps the ring from filling, prevents drop-storm
            // logspam on resume. State machine NOT updated (D-07 -- cooldown
            // timers keep ticking; resume continues from same logical state).
            while (ring_.try_pop(block, block_count)) { /* discard */ }
            continue;
        }

        // Active path: drain -> analyze -> update -> trigger fires inside update.
        while (ring_.try_pop(block, block_count)) {
            auto result = detector_->analyze(block.data(), block_count);
            const auto now = clock::now();
            const auto dt  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - last_tick);
            last_tick = now;
            stateMachine_->update(result.confidence, dt);
        }
    }

    // Reverse-construction-order teardown ON THIS THREAD (Pitfall 4 /
    // Shared Pattern 4). detector_ + stateMachine_ are reset here so the
    // destructors run on the detection thread, not on whatever thread
    // happens to call ~DetectionRunner.
    if (stateMachine_) stateMachine_.reset();
    if (detector_)     detector_.reset();
    DriverLog("MicMap detection: thread exiting cleanly (triggers=%u)\n",
              triggers_.load(std::memory_order_relaxed));
    thread_finished_.store(true, std::memory_order_release);
}

} // namespace micmap::driver

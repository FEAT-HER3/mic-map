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
#include "device_provider.hpp"   // P9 D-01: full type for deviceProvider_->mode() / trainingSession() deref
#include "driver_log.hpp"
#include "training_session.hpp"  // P9 D-01: TrainingSession::addSample target in Training-mode branch

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
#  include <shlobj.h>         // SHGetKnownFolderPath; pulls in knownfolders.h
#  include <objbase.h>        // CoTaskMemFree (P7 REVIEW WR-06)
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

// P7 REVIEW IN-05: detection FFT bin size. Matches the v1.5 GUI default
// (apps/micmap/main.cpp via ConfigManager.detection.fftSize). P8 (config
// read-back) threads this through DetectionConfig so the driver and the
// GUI app share a single source of truth; until then this constant is
// the authoritative driver-side value.
constexpr int kDetectionFftSize  = 2048;

} // namespace

DetectionRunner::DetectionRunner(SampleRing<16, 480>& ring,
                                 CommandQueue& commandQueue,
                                 uint32_t sampleRate,
                                 DetectionConfig initial,
                                 DriverStatePublisher statePublisher,
                                 DeviceProvider* deviceProvider)
    : ring_(ring)
    , commandQueue_(commandQueue)
    , sampleRate_(sampleRate)
    , statePublisher_(std::move(statePublisher))
    , deviceProvider_(deviceProvider)
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
    detector_ = micmap::detection::createFFTDetector(sampleRate_, kDetectionFftSize);
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
        bool appdata_resolved = false;
#ifdef _WIN32
        // P7 REVIEW WR-06: prefer SHGetKnownFolderPath (long-path-aware,
        // not deprecated) over SHGetFolderPathW (deprecated; silently
        // truncates on AppData paths exceeding MAX_PATH via symlinks).
        // SHGetKnownFolderPath requires COM to be initialized; the
        // vrserver Init thread initializes its own apartment before
        // calling our Init, so we run inside that apartment. Verified
        // against the SteamVR driver-host conventions (vrcompositor
        // initializes MTA on the same thread it dispatches Init from).
        PWSTR appdata = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData,
                                                /*flags=*/0,
                                                /*hToken=*/nullptr,
                                                &appdata);
        if (SUCCEEDED(hr) && appdata != nullptr && appdata[0] != L'\0') {
            profile_path = std::filesystem::path(appdata) / L"MicMap" / L"training_data.bin";
            appdata_resolved = true;
        }
        if (appdata != nullptr) {
            // CoTaskMemFree must be called even on SHGetKnownFolderPath
            // failure paths if the pointer was set; documented behavior.
            CoTaskMemFree(appdata);
        }
        if (!appdata_resolved) {
            DriverLog("MicMap detection: SHGetKnownFolderPath(FOLDERID_RoamingAppData) "
                      "failed (hr=0x%08X) - no profile load attempted (detection inert)\n",
                      static_cast<unsigned>(hr));
        }
#else
        // P7 REVIEW IN-02: driver is Windows-only per CLAUDE.md, but keep
        // non-Windows builds (test stubs) explicit -- log that we deliberately
        // skip profile-path resolution rather than silently leaving
        // profile_path empty.
        DriverLog("MicMap detection: non-Windows build, skipping profile load "
                  "(detection inert)\n");
#endif
        // P7 REVIEW WR-06: differentiate the three log shapes so the
        // diagnostic in vrserver.txt is actionable:
        //   1. appdata path resolution failed (logged above; no further log here)
        //   2. profile file does not exist at <path>
        //   3. profile file exists but failed to load (corrupt / wrong format)
        if (appdata_resolved) {
            if (std::filesystem::exists(profile_path)) {
                if (detector_->loadTrainingData(profile_path)) {
                    DriverLog("MicMap detection: loaded training profile from %s\n",
                              profile_path.string().c_str());
                } else {
                    DriverLog("MicMap detection: loadTrainingData failed for %s - detection inert until valid profile\n",
                              profile_path.string().c_str());
                }
            } else {
                DriverLog("MicMap detection: profile file does not exist at %s - detection inert until valid profile\n",
                          profile_path.string().c_str());
            }
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
    // P7 REVIEW IN-06: seed lastObserved_ here so the first RunLoop iteration
    // does not redundantly re-apply the same config. applyConfig is
    // idempotent so the prior behavior was harmless, but skipping the
    // redundant call removes a trivially confusing log/work pattern at
    // start-of-loop. publish() races are still observed correctly because
    // RunLoop's pointer-identity compare uses cfg.get() != lastObserved_.get()
    // -- a fresh shared_ptr from publish() differs from this seeded value.
    lastObserved_ = cfg;

    shutdown_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    thread_finished_.store(false, std::memory_order_release);
    triggers_.store(0, std::memory_order_release);

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&DetectionRunner::ThreadEntry, this);
    DriverLog("MicMap detection: thread spawned (sampleRate=%u, fftSize=%d)\n",
              sampleRate_, kDetectionFftSize);
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
    // P7 REVIEW WR-02: Pause/Resume only act on a running thread. If the
    // runner has not been Start()ed (or has already been Stop()ped), there
    // is no thread to pause and no cv_ to notify -- the operation is a
    // no-op. Without this guard, Pause-before-Start would set paused_ to
    // true, and the unconditional `paused_.store(false)` in Start() would
    // silently wipe the intent, leaving callers unable to express
    // "construct in paused state".
    if (!running_.load(std::memory_order_acquire)) return;
    // Idempotent -- exchange returns the previous value; if already paused, no-op.
    // RESEARCH Open Question 5: RunFrame may receive duplicate EnterStandby
    // events; the early-return prevents log spam + wakeup storms.
    if (paused_.exchange(true, std::memory_order_acq_rel)) return;
    cv_.notify_one();
    DriverLog("MicMap detection: paused\n");
}

void DetectionRunner::Resume() {
    // P7 REVIEW WR-02: same guard as Pause -- Resume on a non-running
    // runner is a no-op. The thread does not exist; nothing to wake.
    if (!running_.load(std::memory_order_acquire)) return;
    // P7 REVIEW WR-03: always notify on a state change AND on an
    // already-resumed Resume. The cv predicate handles spurious wakeup
    // safely; a redundant notify is cheap (one futex syscall) and
    // matches caller expectations -- after Resume returns, the thread is
    // unblocked promptly rather than potentially sleeping the full 50 ms
    // wait_for window. The previous early-return-when-already-resumed
    // shape made `Pause(); Resume();` toggles inside one frame yield the
    // same wakeup latency as no-op, defeating the stated purpose of
    // Resume notifying.
    const bool was_paused = paused_.exchange(false, std::memory_order_acq_rel);
    cv_.notify_one();
    if (was_paused) {
        DriverLog("MicMap detection: resumed\n");
    }
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

void DetectionRunner::reloadTrainingDataAsync(std::filesystem::path path) {
    // P9 09-02 D-24: thread-safe deferred reload. Store the path under mu_
    // (so RunLoop's read at the top of its iteration sees a fully-published
    // value) and notify the cv_.wait_for to wake promptly. The detection
    // thread observes the pending path on its next loop iteration and calls
    // detector_->loadTrainingData on its own thread — preserving the
    // thread-affinity discipline that detector_/stateMachine_ writes happen
    // on the detection thread only (Pitfall 4 / Shared Pattern 4).
    {
        std::lock_guard<std::mutex> lk(mu_);
        pendingReloadPath_ = std::move(path);
    }
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
        // LOAD-BEARING: IStateMachine::configure() may clear internal
        // callbacks; re-install required.
        //
        // P7 REVIEW WR-04: the IStateMachine contract is ambiguous about
        // whether configure() preserves the trigger callback across a
        // reconfigure. There are now THREE install sites by design:
        //   1. Start()         -- via applyConfig(*cfg) for the seeded config
        //   2. RunLoop entry   -- guards against any prior state lost across
        //                         the thread boundary (stateMachine_ was
        //                         constructed on the calling thread, callback
        //                         install also happens there in step 1, but
        //                         the cb is captured by value into the state
        //                         machine; a defensive re-install is cheap).
        //   3. applyConfig()   -- THIS site. Re-installed after configure()
        //                         in case the impl clears callbacks.
        // The behavior is functionally identical at all three sites (same
        // lambda capturing `this`); the cost is one std::function move per
        // publish, which happens at most once per MIG-06 settings change
        // (rare). Future maintainer changing IStateMachine's callback
        // retention contract: audit all three sites; pick one definitive
        // home + a unit test that publishes a config mid-loop and asserts
        // the next trigger still fires.
        stateMachine_->setTriggerCallback([this]() {
            commandQueue_.push(TapCommand{});
            const uint32_t n = triggers_.fetch_add(1, std::memory_order_relaxed) + 1;
            DriverLog("MicMap detection: TapCommand pushed (n=%u)\n", n);
            // P8 D-23: rising-edge trigger -> stamp last_trigger_at on the
            // DriverState snapshot. The detection_state field is updated by
            // RunLoop's polling logic (it sees Triggered on the next iter
            // and publishes "triggered" along with the trigger timestamp).
            if (statePublisher_) {
                statePublisher_(std::string{"triggered"},
                                std::chrono::system_clock::now());
            }
        });
    }
}

void DetectionRunner::ThreadEntry(DetectionRunner* self) {
    self->RunLoop();
}

void DetectionRunner::RunLoop() {
    // P7 REVIEW WR-05: no `clock` alias here -- dt is computed from
    // sample count rather than wall-clock now()-last_tick (see the
    // analyze loop below). Removes scheduler jitter from state machine
    // timing entirely.

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

        // P9 09-02 D-24: if an HTTP-thread finalize requested an in-memory
        // reload of training_data.bin, consume it here before the analyze
        // loop. detector_->loadTrainingData runs on the detection thread to
        // preserve thread-affinity (Pitfall 4 / Shared Pattern 4).
        std::optional<std::filesystem::path> reloadPath;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (pendingReloadPath_.has_value()) {
                reloadPath = std::move(pendingReloadPath_);
                pendingReloadPath_.reset();
            }
        }
        if (reloadPath.has_value() && detector_) {
            if (detector_->loadTrainingData(*reloadPath)) {
                DriverLog("MicMap detection: reloaded training profile from %s "
                          "(D-24 in-memory swap)\n",
                          reloadPath->string().c_str());
            } else {
                DriverLog("MicMap detection: WARNING - reloadTrainingDataAsync "
                          "loadTrainingData failed for %s; detector retains prior profile\n",
                          reloadPath->string().c_str());
            }
        }

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

        // P8 D-23: publish current detection_state to DeviceProvider's
        // DriverState snapshot if it changed since last iter. The trigger
        // callback above (in applyConfig / RunLoop install sites) ALSO
        // publishes "triggered" with last_trigger_at -- which lands first
        // because it fires from inside stateMachine_->update during the
        // analyze loop. The post-update publish here may overwrite the
        // detection_state field with "Cooldown" on the next iter, but the
        // last_trigger_at timestamp is preserved through DeviceProvider's
        // COW lambda (it copies the current snapshot before mutating).
        if (statePublisher_ && stateMachine_) {
            const auto cur = stateMachine_->getCurrentState();
            const char* name = micmap::core::stateToString(cur);
            // Lowercase mapping per CONTEXT IPC-01 detection_state vocab.
            std::string ds;
            switch (cur) {
                case micmap::core::State::Idle:      ds = "idle";      break;
                case micmap::core::State::Training:  ds = "training";  break;
                case micmap::core::State::Detecting: ds = "detecting"; break;
                case micmap::core::State::Triggered: ds = "triggered"; break;
                case micmap::core::State::Cooldown:  ds = "cooldown";  break;
                default:                             ds = name;        break;
            }
            if (ds != lastPublishedState_) {
                statePublisher_(ds, std::nullopt);
                lastPublishedState_ = ds;
            }
        }

        // Active path: drain -> (analyze + state-machine update) OR
        // (forward to TrainingSession), depending on DriverMode.
        // P7 REVIEW WR-05: compute dt from sample count rather than wall
        // clock. The previous wall-clock dt fed the entire idle duration
        // (cv_.wait_for timeout, or the full Pause->Resume gap) into the
        // state machine on the first post-wait update, prematurely
        // satisfying cooldown timers and erasing scheduler-jitter
        // determinism. Sample-count dt is what an offline analyzer would
        // compute, matches v1.5 GUI semantics, and removes scheduler
        // jitter from the timing entirely. sampleRate_ is the live
        // WASAPI rate (WR-01); fallback rate of 1 prevents division by
        // zero in the (impossible) sampleRate_=0 path -- if that ever
        // fires, the dt collapses to block_count ms which is still a
        // sane upper bound for a 480-frame block.
        //
        // P9 D-01 / D-02 / Pitfall 1+2: read DriverMode once per
        // ring-drain cycle via acquire-load. Pairs with DeviceProvider's
        // release-store on mode_ AFTER constructing TrainingSession
        // (publish discipline mirrors activeConfig_ at line 101). A
        // stale-read-while-flipping is safe in either direction:
        //   - read Detecting after flip-to-Training: this iteration
        //     analyzes against a still-valid trained profile (no UAF;
        //     detector_ outlives the session); next iter sees Training.
        //   - read Training after flip-back-to-Detecting: the Training
        //     branch null-checks trainingSession() and drops the block
        //     if reset has already run.
        const auto mode = (deviceProvider_ != nullptr)
            ? deviceProvider_->mode().load(std::memory_order_acquire)
            : DriverMode::Detecting;
        const uint32_t rate_for_dt = sampleRate_ ? sampleRate_ : 1;
        while (ring_.try_pop(block, block_count)) {
            if (mode == DriverMode::Detecting) {
                auto result = detector_->analyze(block.data(), block_count);
                const auto dt = std::chrono::milliseconds(
                    static_cast<long long>(block_count) * 1000 / rate_for_dt);
                stateMachine_->update(result.confidence, dt);
            } else {
                // DriverMode::Training — forward block to TrainingSession.
                // Pitfall 2 mitigation: trainingSession() is null-checked
                // because the mode flip and the unique_ptr<TrainingSession>
                // reset are not atomic across two members. A transient null
                // read is safe — drop the block. DeviceProvider also
                // release-stores mode_ = Detecting BEFORE the reset, so the
                // window of mode==Training && session==nullptr is bounded
                // by one detection-loop iteration at most.
                if (deviceProvider_ != nullptr) {
                    if (auto* session = deviceProvider_->trainingSession()) {
                        session->addSample(block.data(), block_count);
                    }
                }
            }
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

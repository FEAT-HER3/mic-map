// driver/src/detection_runner.hpp
//
// Phase 7 / D-17 / D-18 / D-22 / SVR-05: driver-side detection thread.
//
// DetectionRunner drains a SampleRing fed by AudioWorker's WASAPI capture
// callback, runs INoiseDetector::analyze + IStateMachine::update, and on
// rising-edge Triggered pushes TapCommand into the existing v1.5
// CommandQueue. The HTTP-thread -> CommandQueue -> RunFrame v1.5 SVR-05
// boundary is preserved verbatim -- DetectionRunner is a NEW PRODUCER of
// an existing primitive, nothing about RunFrame changes.
//
// SVR-05 / D-22 enforcement: ZERO OpenVR API surface in this translation
// unit. ZERO OpenVR header includes. The DriverLog macro from
// "driver_log.hpp" (the documented thread-safe driver-host service
// interface, Shared Pattern 1) is the only OpenVR-shaped surface allowed
// from the detection thread, and that include lives in the .cpp.
//
// Pitfall 13 / D-23: DetectionRunner does NOT register IMMNotificationClient
// or any COM notifier. The audio-device-removed surface is owned by
// WASAPIAudioCapture (registered inside AudioWorker per P6 D-15).
// DetectionRunner only ever touches: SampleRing (read), DetectionConfig
// snapshot (atomic load), state machine (own instance), noise detector
// (own instance), CommandQueue (push).

#pragma once

#include "command_queue.hpp"
#include "sample_ring.hpp"

#include <atomic>
#include <chrono>          // P8 D-23: DriverStatePublisher last_trigger_at type
#include <condition_variable>
#include <cstdint>
#include <functional>      // P8 D-23: DriverStatePublisher callback type
#include <memory>
#include <mutex>
#include <optional>        // P8 D-23: DriverStatePublisher last_trigger_at type
#include <string>          // P8 D-23: DriverStatePublisher detection_state type
#include <thread>

// Forward declarations to avoid pulling shared-lib headers into this header
// (mirrors audio_worker.hpp's `namespace micmap::audio { class IAudioCapture; }`
// precedent at audio_worker.hpp:25). The .cpp includes
// <micmap/detection/noise_detector.hpp> + <micmap/core/state_machine.hpp>.
namespace micmap::detection { class INoiseDetector; }
namespace micmap::core { class IStateMachine; struct StateMachineConfig; }

namespace micmap::driver {

/// MIG-06 settings snapshot (lock-free atomic-shared_ptr publish/load).
/// Defaults mirror driver/resources/settings/default.vrsettings (07-02 / D-13).
struct DetectionConfig {
    float sensitivity{0.7f};
    float threshold{0.6f};
    int   cooldown_ms{1000};
    int   min_duration_ms{200};
};

/// @brief P8 D-23: callback signature DetectionRunner uses to push state-
///        machine transitions (idle/detecting/triggered/cooldown) and trigger
///        timestamps into DeviceProvider's DriverState atomic snapshot.
///        last_trigger_at is set ONLY at the rising edge into Triggered;
///        nullopt at every other transition (the snapshot's prior value
///        is preserved through DeviceProvider's COW publish lambda).
using DriverStatePublisher = std::function<void(
    std::string detection_state,
    std::optional<std::chrono::system_clock::time_point> last_trigger_at)>;

class DetectionRunner {
public:
    DetectionRunner(SampleRing<16, 480>& ring,
                    CommandQueue& commandQueue,
                    uint32_t sampleRate,
                    DetectionConfig initial,
                    /// P8 D-23: optional DriverState publisher. When non-null,
                    /// the run loop calls this on every state-machine transition
                    /// so the GET /state response reflects live state. Default
                    /// nullptr keeps existing test ctors (4 args) compiling
                    /// unchanged.
                    DriverStatePublisher statePublisher = nullptr);
    ~DetectionRunner();

    DetectionRunner(const DetectionRunner&) = delete;
    DetectionRunner& operator=(const DetectionRunner&) = delete;
    DetectionRunner(DetectionRunner&&) = delete;
    DetectionRunner& operator=(DetectionRunner&&) = delete;

    /// Spawns the detection thread. Returns true if joinable.
    bool Start();

    /// Idempotent shutdown; called by destructor. 2 s watchdog (D-20)
    /// matching P6 AudioWorker pattern (audio_worker.cpp:90-122).
    void Stop();

    /// HMD standby pause (RunFrame-thread-safe, called from EnterStandby in
    /// 07-05). Idempotent -- repeated calls are no-ops (RESEARCH Open
    /// Question 5: RunFrame may receive duplicate EnterStandby events).
    void Pause();

    /// HMD standby resume (RunFrame-thread-safe, called from LeaveStandby).
    /// Idempotent -- repeated calls are no-ops.
    void Resume();

    /// MIG-06: publish a new DetectionConfig snapshot. Lock-free atomic
    /// shared_ptr swap; detection thread observes it within one
    /// cv_.wait_for cycle (<= 50 ms). Called by HTTP/PUT /settings handler
    /// in P8 (P7 ships the mechanism only).
    void publish(std::shared_ptr<const DetectionConfig> next);

    /// Audio-thread wakeup hook (called from AudioWorker's setAudioCallback
    /// after each ring push by 07-05). Wakes the cv_.wait_for so the
    /// detection thread services freshly-pushed audio without waiting out
    /// the 50 ms timeout.
    void NotifyOne();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    uint32_t TriggersEmitted() const { return triggers_.load(std::memory_order_relaxed); }

    /// Test-only accessor for the active DetectionConfig snapshot. Used by
    /// tests/driver/detection_settings_propagation_test.cpp (07-01) to
    /// measure publish() -> observed-swap latency. Mirrors P6's
    /// `state_for_test()` precedent (audio_worker.hpp:88-92). Does NOT
    /// widen the production API surface; may be deleted in P10 if unused.
    std::shared_ptr<const DetectionConfig> active_config_for_test() const;

private:
    static void ThreadEntry(DetectionRunner* self);
    void RunLoop();
    void applyConfig(const DetectionConfig& cfg);

    SampleRing<16, 480>&                                ring_;
    CommandQueue&                                       commandQueue_;
    uint32_t                                            sampleRate_;

    // MIG-06 atomic snapshot. C++17 atomic_load/store free-function form
    // (project compiles as C++17 per driver/CMakeLists.txt:18; deprecated
    // in C++20 but present; bump to native std::atomic<std::shared_ptr>
    // when the project advances). Hot path = detection thread acquire-load
    // once per loop iteration; rare writes = HTTP / publish() callers.
    std::shared_ptr<const DetectionConfig>              activeConfig_;
    std::shared_ptr<const DetectionConfig>              lastObserved_;   // local cache (detection thread only)

    std::unique_ptr<micmap::detection::INoiseDetector>  detector_;       // constructed in Start, reset on detection thread at exit
    std::unique_ptr<micmap::core::IStateMachine>        stateMachine_;   // ditto

    std::thread                                         thread_;
    std::mutex                                          mu_;
    std::condition_variable                             cv_;
    std::atomic<bool>                                   shutdown_{false};
    std::atomic<bool>                                   paused_{false};
    std::atomic<bool>                                   running_{false};
    std::atomic<bool>                                   thread_finished_{false};
    std::atomic<uint32_t>                               triggers_{0};

    /// P8 D-23: optional state publisher. nullptr in the existing 4-arg ctor
    /// path used by tests/driver/detection_settings_propagation_test.cpp;
    /// DeviceProvider supplies a non-null lambda in production.
    DriverStatePublisher                                statePublisher_;
    /// Last published detection_state string (run-loop-thread local) so we
    /// only publish when the state changes -- avoids COW spam on every iter.
    std::string                                         lastPublishedState_;
};

} // namespace micmap::driver

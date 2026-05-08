/**
 * @file device_provider.hpp
 * @brief OpenVR device provider for MicMap HMD sidecar driver
 *
 * Implements IServerTrackedDeviceProvider but does NOT register any tracked
 * device. Instead, it creates its own /input/system/click boolean component
 * on the HMD property container and drains TapCommands from a CommandQueue
 * populated by the HTTP thread. Each tap command fires DOWN immediately
 * then UP after a short hold so SteamVR's complex_button binding sees a
 * single-click. See Phase 1 plan 01-03 (amended).
 */

#pragma once

#include <openvr_driver.h>

#include "detection_runner.hpp"   // P7 D-13/D-19: DetectionConfig is stored
                                  // BY VALUE as detectionDefaults_; including
                                  // the full header here is cheaper than the
                                  // unique_ptr<DetectionConfig> alternative.
                                  // detection_runner.hpp itself includes only
                                  // command_queue.hpp + driver_mode.hpp +
                                  // sample_ring.hpp + std headers — no
                                  // shared-lib pull-in.

#include "driver_mode.hpp"        // P9 D-01: DriverMode enum for atomic mode_

#include "driver_state.hpp"       // P8 D-23: DriverState POD for GET /state
                                  // atomic snapshot. Header has no JSON or
                                  // OpenVR includes -- safe to pull in here.

#include "micmap/core/config_manager.hpp"   // P8 D-15: AppConfig schema for
                                            // atomic snapshot (header is
                                            // JSON-free so AssertNoJsonInCore
                                            // is unaffected).

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>          // P9 D-09: trainingMutex_ guards trainingSession_ lifecycle
#include <optional>

namespace micmap::driver {

// Forward declarations
class HttpServer;
class CommandQueue;
class AudioWorker;
class DetectionRunner;   // P7 D-19: full type only needed in device_provider.cpp
                         // (where ~DeviceProvider is defined). DetectionConfig
                         // (used as a by-value member) comes from the include
                         // above.
class TrainingSession;   // P9 D-09: lazy unique_ptr<TrainingSession>; full type
                         // only needed in device_provider.cpp where the
                         // unique_ptr is constructed/destroyed and the addSample
                         // / snapshot accessors are dereferenced.

/**
 * @brief Lifecycle state of the HMD-side /input/system/click component.
 *
 * - NotReady:   cold start, no HMD container seen yet.
 * - Ready:      hSystemClick_ is valid; UpdateBooleanComponent may succeed.
 * - Invalidated: HMD deactivation observed or Update returned an error; the
 *                next RunFrame will attempt CreateBooleanComponent again.
 */
enum class HmdComponentState {
    NotReady,
    Ready,
    Invalidated
};

/**
 * @brief Device provider that runs the MicMap sidecar inside vrserver.
 *
 * RunFrame order (load-bearing):
 *   0. first-frame init log (SVR-10)
 *   1. drain OpenVR events (observe TrackedDeviceDeactivated for HMD)
 *   2. create-or-recreate /input/system/click if not Ready
 *   3. drain CommandQueue (DOWN stamps pressTimestamp_; UP applies or defers
 *      until the min-hold floor is met)
 *   4. tick any pending deferred release
 *   5. max-hold watchdog (SVR-06) forces UP if the button has been held > 5s
 */
class DeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    DeviceProvider();
    ~DeviceProvider();

    // IServerTrackedDeviceProvider interface
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    // Central write helper: routes UpdateBooleanComponent + error handling
    // through a single choke point (Pitfall 12 — avoid redundant writes,
    // Pitfall 1 — flip to Invalidated on any error return).
    void writeValue(bool v);

    std::unique_ptr<CommandQueue> commandQueue_;
    std::unique_ptr<HttpServer> httpServer_;
    std::atomic<bool> initialized_{false};

    // P6 D-01/D-03/D-14: driver-side audio capture spike.
    // driverAudioEnabled_ holds the result of the single Init-time
    // vr::VRSettings()->GetBool("driver_micmap","enable_driver_audio") read.
    // audioWorker_ is constructed LAST in Init when the flag is true and
    // reset FIRST in Cleanup (reverse construction order, Pitfall 4).
    bool                          driverAudioEnabled_{false};
    std::unique_ptr<AudioWorker>  audioWorker_;

    // P7 D-13/D-19/D-20: driver-side detection thread.
    // driverDetectionEnabled_ holds the result of the single Init-time
    // vr::VRSettings()->GetBool("driver_micmap","enable_driver_detection") read.
    // detectionDefaults_ caches the 4 numeric VRSettings reads (sensitivity,
    // threshold, cooldown_ms, min_duration_ms) so DetectionRunner is
    // constructed with values mirroring driver/resources/settings/default.vrsettings.
    // detectionRunner_ is constructed LAST in Init when both flags are true
    // and audioWorker_ is alive, and reset FIRST in Cleanup (strict reverse
    // construction order — Pitfall 4 / D-20).
    bool                              driverDetectionEnabled_{false};
    DetectionConfig                   detectionDefaults_{};
    std::unique_ptr<DetectionRunner>  detectionRunner_;

    // P8 D-15 / Pattern A: AppConfig atomic snapshot. Single mutator (HTTP
    // PUT /settings handler in 08-04); multi-reader (HTTP GET /settings,
    // detection thread for runtime config reads, audio thread for
    // device-config reads). Mechanism mirrors P7 detection_runner.cpp:85,99
    // exactly - std::atomic_load_explicit / atomic_store_explicit on a
    // std::shared_ptr<const T>. C++17 free-function form (deprecated in
    // C++20 but still present).
    std::shared_ptr<const core::AppConfig> configSnapshot_;

    // P8 D-23: DriverState atomic snapshot. Mutators include
    //   - DetectionRunner state-machine transitions (idle/detecting/triggered/cooldown)
    //   - DetectionRunner trigger emission (last_trigger_at)
    //   - AudioWorker IMMNotificationClient (audio_device_state) — wired in 08-04
    //   - DeviceProvider error channel (last_error; clear via POST /state/clear-error in 08-04)
    // Multiple producers serialize on the COW write path inside publishDriverState
    // (read current snapshot, mutate, atomic_store). Readers are lock-free.
    std::shared_ptr<const DriverState> stateSnapshot_;

    // P9 D-01 / D-09: training-session lifecycle state.
    //   mode_           — atomic flag read by DetectionRunner on every ring-
    //                     drain iteration via memory_order_acquire. Default
    //                     Detecting; transitions to Training inside
    //                     tryStartTrainingSession AFTER constructing
    //                     trainingSession_; transitions back to Detecting
    //                     inside resetTrainingSession BEFORE resetting
    //                     trainingSession_.
    //   trainingSession_— lazy. nullptr until POST /training/start.
    //                     HTTP-thread-only construction/destruction per D-03.
    //   trainingMutex_  — guards trainingSession_ lifecycle (D-09 single
    //                     instance + Pitfall 2 destruction race). Held
    //                     while constructing/destroying the unique_ptr;
    //                     read accessors take a short-lived shared lock
    //                     so DetectionRunner can dereference safely.
    std::atomic<DriverMode>          mode_{DriverMode::Detecting};
    std::unique_ptr<TrainingSession> trainingSession_;
    mutable std::mutex               trainingMutex_;

public:
    /// @brief P8 D-15: Lock-free read of the current AppConfig snapshot.
    ///        Safe to call from any thread. Returns nullptr only between
    ///        ctor and Init's first publish (which happens before
    ///        HttpServer::Start so HTTP handlers always see a non-null
    ///        snapshot).
    std::shared_ptr<const core::AppConfig> getConfigSnapshot() const;

    /// @brief P8 D-14 / Pitfall 2: persist-first apply for PUT /settings.
    ///        Calls saveConfigJson FIRST; on disk success swaps the atomic
    ///        snapshot. Returns false on disk failure (HTTP 500 in 08-04).
    ///        Validation (08-04 settings_validator) runs BEFORE this method.
    bool applyValidatedConfig(core::AppConfig candidate);

    /// @brief P8 D-23: Lock-free read for the GET /state HTTP handler.
    ///        Returns nullptr only between ctor and Init's first publish
    ///        (Init publishes a default-constructed DriverState before
    ///        HttpServer::Start so HTTP handlers always see a non-null
    ///        snapshot).
    std::shared_ptr<const DriverState> getStateSnapshot() const;

    /// @brief P8 D-23: COW publish of a new DriverState snapshot. Concurrent
    ///        producers race on the atomic_store but every observer sees a
    ///        consistent state (no torn fields). Producers: DetectionRunner
    ///        state transitions, AudioWorker device events, HTTP
    ///        POST /state/clear-error (08-04).
    void publishDriverState(DriverState next);

    // -------------------------------------------------------------------
    // P9 D-01 / D-09 / D-22: TrainingSession lifecycle accessors.
    // -------------------------------------------------------------------
    // Both mode() and trainingSession() are read by DetectionRunner on
    // every ring-drain iteration (mode() is acquire-load on the atomic;
    // trainingSession() is mutex-guarded). HTTP handlers in 09-02 call
    // tryStartTrainingSession / resetTrainingSession to drive the
    // lifecycle. Construction/destruction is HTTP-thread-only per D-03.
    //
    // Pitfall 1 (construction race): tryStartTrainingSession constructs
    // the session BEFORE release-storing mode_ = Training so the
    // detection thread's acquire-load observes a fully-constructed
    // session.
    // Pitfall 2 (destruction race): resetTrainingSession release-stores
    // mode_ = Detecting BEFORE resetting the unique_ptr. DetectionRunner
    // null-checks trainingSession() to absorb the brief race window.

    /// @brief P9 D-01: atomic DriverMode accessor. DetectionRunner reads
    ///        via memory_order_acquire on every ring-drain iteration;
    ///        HTTP handlers / tryStartTrainingSession write via
    ///        memory_order_release.
    std::atomic<DriverMode>&       mode() noexcept       { return mode_; }
    const std::atomic<DriverMode>& mode() const noexcept { return mode_; }

    /// @brief P9 D-09: returns the live TrainingSession pointer or
    ///        nullptr when no session is active. Mutex-guarded;
    ///        DetectionRunner re-acquires it in its hot path which is
    ///        cheap (single mutex lock per ring-drain cycle that
    ///        observes mode==Training).
    ///
    ///        Defined inline so DetectionRunner translation units that
    ///        only #include detection_runner.hpp + device_provider.hpp
    ///        do not need to drag device_provider.cpp + its full
    ///        transitive dependency chain (httplib, bindings,
    ///        config_io, settings_validator, etc.) into the link line.
    ///        unique_ptr<TrainingSession>::get() does NOT require the
    ///        full TrainingSession type — only the dtor / reset /
    ///        operator-> do — so the forward decl above is sufficient.
    TrainingSession* trainingSession() noexcept {
        std::lock_guard<std::mutex> lock(trainingMutex_);
        return trainingSession_.get();
    }
    const TrainingSession* trainingSession() const noexcept {
        std::lock_guard<std::mutex> lock(trainingMutex_);
        return trainingSession_.get();
    }

    /// @brief P9 D-09: HTTP-thread helper invoked by POST /training/start
    ///        (09-02). Constructs a new TrainingSession; release-stores
    ///        mode_ = Training; returns false if a session is already
    ///        active (single-instance per D-09; HTTP handler returns
    ///        409).
    bool tryStartTrainingSession(uint32_t sampleRate, size_t fftSize);

    /// @brief P9 D-09 / Pitfall 2: HTTP-thread helper invoked by POST
    ///        /training/finalize, POST /training/cancel, the 30 s
    ///        timeout watchdog, and Cleanup. Release-stores mode_ =
    ///        Detecting BEFORE resetting the unique_ptr<TrainingSession>;
    ///        idempotent (no-op when no session is active).
    void resetTrainingSession();

private:

    // HMD-side component state
    vr::VRInputComponentHandle_t hSystemClick_{vr::k_ulInvalidInputComponentHandle};
    HmdComponentState state_{HmdComponentState::NotReady};

    // Press / release timing (D-04, D-05)
    std::chrono::steady_clock::time_point pressTimestamp_{};
    std::optional<std::chrono::steady_clock::time_point> pendingReleaseAt_;
    bool isPressed_{false};
    bool lastWrittenValue_{false};

    // Transition-only logging flags (D-08)
    bool initLogged_{false};
    bool loggedAwaitingHmd_{false};
    bool profilePropsWritten_{false};  // SetString(ControllerType,InputProfilePath) once per (re)activation

    // Tap hold duration: DOWN -> wait kTapHold -> UP, fired per TapCommand.
    // Long enough for SteamVR's complex_button "single" classifier to see it.
    static constexpr std::chrono::milliseconds kTapHold{150};
    // Max-hold watchdog: safety net, forces UP if anything leaves the
    // component stuck DOWN longer than this.
    static constexpr std::chrono::milliseconds kMaxHold{5000};
};

} // namespace micmap::driver

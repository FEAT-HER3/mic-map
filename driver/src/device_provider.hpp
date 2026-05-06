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
                                  // command_queue.hpp + sample_ring.hpp + std
                                  // headers — no shared-lib pull-in.

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

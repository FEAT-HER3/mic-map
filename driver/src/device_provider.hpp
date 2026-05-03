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

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

namespace micmap::driver {

// Forward declarations
class HttpServer;
class CommandQueue;
class AudioWorker;

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

/**
 * @file device_provider.cpp
 * @brief Implementation of the MicMap HMD sidecar device provider.
 *
 * Sidecar architecture (Phase 1 / plan 01-03):
 *   - Does NOT register any tracked device (zero virtual controllers).
 *   - Creates /input/system/click on the HMD property container (index 0).
 *   - Drains a CommandQueue populated by the HTTP thread.
 *   - Enforces a 100 ms min-hold + 5 s max-hold safety on the driver side.
 *   - Handles VREvent_TrackedDeviceDeactivated for sleep/wake recreation.
 *   - Logs via DriverLog + VRInputErrorName on every failure (SVR-10).
 */

#include "device_provider.hpp"
#include "micmap/bindings/bindings_patcher.hpp"
#include "command_queue.hpp"
#include "http_server.hpp"
#include "driver_log.hpp"
#include "vr_error.hpp"

#include <openvr_driver.h>

using namespace vr;
using micmap::driver::VRInputErrorName;

// Phase 4 D-10: wrap DriverLog into the shared-lib LogSink shape. Keeps
// driver-side vrserver.txt output byte-identical to the pre-lift behavior.
static void driverLogSink(const char* msg) {
    DriverLog("%s", msg);
}

#ifndef MICMAP_DRIVER_VERSION
#define MICMAP_DRIVER_VERSION "0.0.0"
#endif

namespace micmap::driver {

// Interface versions this provider speaks. Sidecar mode: we only claim the
// server-device-provider interface; tracked-device-server is unused because
// no tracked device is registered.
// IN-05: Assumes IServerTrackedDeviceProvider_Version is a string literal
// (current OpenVR SDK contract -- it expands via #define to a bare "..."
// literal with static storage duration, so the returned array is valid for
// the process lifetime). If Valve ever redefines it as a constexpr
// std::string_view or similar non-literal, this array must be rebuilt
// per-call to avoid storing a dangling pointer.
static const char* const k_InterfaceVersions[] = {
    IServerTrackedDeviceProvider_Version,
    nullptr
};

DeviceProvider::DeviceProvider() = default;

DeviceProvider::~DeviceProvider() {
    Cleanup();
}

EVRInitError DeviceProvider::Init(IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    DriverLog("MicMap driver initializing (sidecar mode)\n");

    // Ensure SteamVR's generic-HMD bindings route /user/head/input/system to
    // dashboard + lasermouse leftclick. Best-effort; logs its own outcome.
    // Takes effect on the NEXT SteamVR start (vrcompositor caches bindings
    // at connect time — already happened before our Init runs).
    (void)micmap::bindings::PatchGenericHmdBindings(driverLogSink);

    commandQueue_ = std::make_unique<CommandQueue>();
    httpServer_ = std::make_unique<HttpServer>(*commandQueue_);
    if (!httpServer_->Start()) {
        DriverLog("MicMap: failed to start HTTP server\n");
        return VRInitError_Driver_Failed;
    }
    DriverLog("MicMap: HTTP server listening on port %d\n", httpServer_->GetPort());

    initialized_ = true;
    return VRInitError_None;
}

void DeviceProvider::Cleanup() {
    if (!initialized_) {
        return;
    }

    DriverLog("MicMap driver cleaning up...\n");

    if (httpServer_) {
        httpServer_->Stop();
        httpServer_.reset();
    }
    commandQueue_.reset();

    hSystemClick_ = k_ulInvalidInputComponentHandle;
    state_ = HmdComponentState::NotReady;
    pendingReleaseAt_.reset();
    isPressed_ = false;
    lastWrittenValue_ = false;
    initLogged_ = false;
    loggedAwaitingHmd_ = false;
    profilePropsWritten_ = false;
    initialized_ = false;

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();

    DriverLog("MicMap driver cleanup complete\n");
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return k_InterfaceVersions;
}

void DeviceProvider::RunFrame() {
    // 0. First-frame init log (SVR-10 / Pitfall 11).
    if (!initLogged_) {
        DriverLog("MicMap driver v%s built %s %s - RunFrame starting\n",
                  MICMAP_DRIVER_VERSION, __DATE__, __TIME__);
        initLogged_ = true;
    }

    // 1. Drain OpenVR events. A HMD deactivation must flip us to Invalidated
    //    BEFORE any handle-dependent work this tick (Pitfall 1).
    VREvent_t ev{};
    while (VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev))) {
        if (ev.eventType == VREvent_TrackedDeviceDeactivated
            && ev.trackedDeviceIndex == k_unTrackedDeviceIndex_Hmd) {
            hSystemClick_ = k_ulInvalidInputComponentHandle;
            state_ = HmdComponentState::Invalidated;
            isPressed_ = false;
            lastWrittenValue_ = false;
            pendingReleaseAt_.reset();
            profilePropsWritten_ = false;  // re-arm profile writes on reactivation
            DriverLog("MicMap: HMD deactivated, handle invalidated\n");
        }
    }

    // 2. Create-or-recreate /input/system/click while not Ready.
    if (state_ != HmdComponentState::Ready) {
        auto hmd = VRProperties()->TrackedDeviceToPropertyContainer(
            k_unTrackedDeviceIndex_Hmd);
        if (hmd != k_ulInvalidPropertyContainer) {
            // We intentionally do NOT SetStringProperty(Prop_ControllerType /
            // Prop_InputProfilePath) here. Lighthouse owns the HMD container
            // and its controller_type (e.g. "lighthouse_hmd") wins at binding-
            // resolve time, so our controller_type write has no effect.
            // Meanwhile, setting our own Prop_InputProfilePath_String *does*
            // stick (lighthouse doesn't set it on non-Index HMDs) -- but then
            // SteamVR loads OUR profile, sees its controller_type mismatches
            // the container's, and the binding layer silently drops bindings
            // (including /actions/lasermouse/in/Pointer -- so the head-locked
            // cursor disappears). The dashboard + cursor are wired via the
            // generic_hmd bindings file we patch in bindings_patcher instead.

            auto err = VRDriverInput()->CreateBooleanComponent(
                hmd, "/input/system/click", &hSystemClick_);
            if (err == VRInputError_None) {
                DriverLog("MicMap: /input/system/click created (handle=%llu)\n",
                          static_cast<unsigned long long>(hSystemClick_));
                state_ = HmdComponentState::Ready;
                loggedAwaitingHmd_ = false;   // re-arm for future invalidation cycles
            } else {
                DriverLog("MicMap: CreateBooleanComponent failed: %s (%d)\n",
                          VRInputErrorName(err), static_cast<int>(err));
                hSystemClick_ = k_ulInvalidInputComponentHandle;
            }
        } else if (!loggedAwaitingHmd_) {
            DriverLog("MicMap: awaiting HMD container\n");
            loggedAwaitingHmd_ = true;   // transition-only (D-08)
        }
    }

    // 3. Drain CommandQueue (non-blocking; lock held only inside try_pop).
    //    Each TapCommand writes DOWN immediately and schedules a matching
    //    UP at now + kTapHold. If another tap arrives while one is pending,
    //    the latest tap's DOWN restarts the hold window (the pending release
    //    is overwritten).
    while (auto cmd = commandQueue_->try_pop()) {
        (void)cmd;  // TapCommand is empty -- its presence is the signal.
        if (state_ != HmdComponentState::Ready) {
            DriverLog("MicMap: dropped tap command (handle invalid)\n");
            continue;
        }
        pressTimestamp_ = std::chrono::steady_clock::now();
        writeValue(true);
        pendingReleaseAt_ = pressTimestamp_ + kTapHold;
    }

    // 4. Tick the scheduled release once its hold deadline has passed.
    if (pendingReleaseAt_
        && std::chrono::steady_clock::now() >= *pendingReleaseAt_) {
        writeValue(false);
        pendingReleaseAt_.reset();
    }

    // 5. Max-hold watchdog (SVR-06 + Open Question 5): guard against an app
    //    crash mid-press leaving the system button stuck down.
    if (isPressed_
        && (std::chrono::steady_clock::now() - pressTimestamp_) > kMaxHold) {
        DriverLog("MicMap: max-hold watchdog fired (no UP received in %lldms)\n",
                  static_cast<long long>(kMaxHold.count()));
        writeValue(false);
        pendingReleaseAt_.reset();
    }
}

bool DeviceProvider::ShouldBlockStandbyMode() {
    return false;
}

void DeviceProvider::EnterStandby() {
    DriverLog("MicMap driver entering standby\n");
}

void DeviceProvider::LeaveStandby() {
    DriverLog("MicMap driver leaving standby\n");
}

void DeviceProvider::writeValue(bool v) {
    if (state_ != HmdComponentState::Ready) return;
    if (v == lastWrittenValue_) return;   // avoid redundant writes (Pitfall 12)

    auto err = VRDriverInput()->UpdateBooleanComponent(hSystemClick_, v, 0.0);
    if (err != VRInputError_None) {
        DriverLog("MicMap: UpdateBooleanComponent(%s) failed: %s (%d)\n",
                  v ? "down" : "up", VRInputErrorName(err),
                  static_cast<int>(err));
        hSystemClick_ = k_ulInvalidInputComponentHandle;
        state_ = HmdComponentState::Invalidated;
        isPressed_ = false;
        lastWrittenValue_ = false;
        return;
    }
    DriverLog("MicMap: UpdateBooleanComponent(%s) OK (handle=%llu)\n",
              v ? "down" : "up",
              static_cast<unsigned long long>(hSystemClick_));
    lastWrittenValue_ = v;
    isPressed_ = v;
}

} // namespace micmap::driver

/**
 * @file device_provider.hpp
 * @brief OpenVR device provider for MicMap virtual controller
 *
 * Implements IServerTrackedDeviceProvider to register our virtual controller
 * with SteamVR.
 */

#pragma once

#include <openvr_driver.h>
#include <memory>
#include <atomic>
#include <string>

#include "process_launcher.hpp"

namespace micmap::driver {

// Forward declarations
class VirtualController;
class HttpServer;

/**
 * @brief Device provider that registers the virtual controller with SteamVR
 *
 * This class implements the IServerTrackedDeviceProvider interface, which
 * SteamVR uses to discover and manage tracked devices. We use it to register
 * our virtual controller that can inject button events.
 */
class DeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    DeviceProvider();
    ~DeviceProvider();

    // IServerTrackedDeviceProvider interface
    
    /**
     * @brief Initialize the device provider
     * @param pDriverContext Driver context from SteamVR
     * @return VRInitError_None on success
     */
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;

    /**
     * @brief Cleanup the device provider
     */
    void Cleanup() override;

    /**
     * @brief Get interface versions supported by this provider
     * @return Array of interface version strings
     */
    const char* const* GetInterfaceVersions() override;

    /**
     * @brief Called each frame to update device state
     */
    void RunFrame() override;

    /**
     * @brief Check if this provider should block standby mode
     * @return True if standby should be blocked
     */
    bool ShouldBlockStandbyMode() override;

    /**
     * @brief Called when SteamVR is entering standby mode
     */
    void EnterStandby() override;

    /**
     * @brief Called when SteamVR is leaving standby mode
     */
    void LeaveStandby() override;

    /**
     * @brief Get the virtual controller instance
     * @return Pointer to the virtual controller
     */
    VirtualController* GetController() const { return controller_.get(); }

private:
    /**
     * @brief Launch the MicMap application if auto-launch is enabled
     * @return True if launch was successful or auto-launch is disabled
     */
    bool launchMicMapApp();

    /**
     * @brief Terminate the MicMap application if it was launched by us
     */
    void terminateMicMapApp();

    /**
     * @brief Get the path to the MicMap application
     * @return Path to the executable, or empty string if not found
     */
    std::string getMicMapAppPath();

    std::unique_ptr<VirtualController> controller_;
    std::unique_ptr<HttpServer> httpServer_;
    std::atomic<bool> initialized_{false};
    
    // Process management for auto-launched MicMap application
    ProcessHandle micmapProcess_;
    bool micmapLaunchedByUs_{false};
};

} // namespace micmap::driver
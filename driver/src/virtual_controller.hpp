/**
 * @file virtual_controller.hpp
 * @brief Virtual controller device for MicMap
 *
 * Implements ITrackedDeviceServerDriver to create a virtual controller
 * that can inject button events into SteamVR.
 */

#pragma once

#include <openvr_driver.h>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>

namespace micmap::driver {

/**
 * @brief Virtual controller that can inject button events
 *
 * This controller doesn't have physical tracking - it exists purely to
 * inject button events that can trigger dashboard interactions.
 */
class VirtualController : public vr::ITrackedDeviceServerDriver {
public:
    VirtualController();
    ~VirtualController();  // Note: ITrackedDeviceServerDriver doesn't have virtual destructor

    // ITrackedDeviceServerDriver interface

    /**
     * @brief Activate the device
     * @param unObjectId The device index assigned by SteamVR
     * @return VRInitError_None on success
     */
    vr::EVRInitError Activate(uint32_t unObjectId) override;

    /**
     * @brief Deactivate the device
     */
    void Deactivate() override;

    /**
     * @brief Enter standby mode
     */
    void EnterStandby() override;

    /**
     * @brief Get a component of this device
     * @param pchComponentNameAndVersion Component name and version
     * @return Pointer to component, or nullptr if not found
     */
    void* GetComponent(const char* pchComponentNameAndVersion) override;

    /**
     * @brief Handle debug request
     * @param pchRequest Debug request string
     * @param pchResponseBuffer Response buffer
     * @param unResponseBufferSize Size of response buffer
     */
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;

    /**
     * @brief Get the device's pose
     * @return Current pose of the device
     */
    vr::DriverPose_t GetPose() override;

    // MicMap-specific methods

    /**
     * @brief Get the serial number of this controller
     * @return Serial number string
     */
    const char* GetSerialNumber() const { return serialNumber_.c_str(); }

    /**
     * @brief Press the system button (simulates HMD button press)
     */
    void PressSystemButton();

    /**
     * @brief Release the system button
     */
    void ReleaseSystemButton();

    /**
     * @brief Press and release the system button (click)
     * @param durationMs Duration to hold the button in milliseconds
     */
    void ClickSystemButton(int durationMs = 100);

    /**
     * @brief Press the A button (alternative select button)
     */
    void PressAButton();

    /**
     * @brief Release the A button
     */
    void ReleaseAButton();

    /**
     * @brief Press and release the A button (click)
     * @param durationMs Duration to hold the button in milliseconds
     */
    void ClickAButton(int durationMs = 100);

    /**
     * @brief Press the trigger (primary selection button for laser mouse)
     */
    void PressTrigger();

    /**
     * @brief Release the trigger
     */
    void ReleaseTrigger();

    /**
     * @brief Press and release the trigger (click)
     * @param durationMs Duration to hold the trigger in milliseconds
     */
    void ClickTrigger(int durationMs = 100);

    /**
     * @brief Called each frame to process pending operations
     */
    void RunFrame();

    /**
     * @brief Check if the controller is active
     * @return True if active
     */
    bool IsActive() const { return deviceIndex_ != vr::k_unTrackedDeviceIndexInvalid; }

private:
    void UpdateButtonState(vr::VRInputComponentHandle_t button, bool pressed);
    void UpdateScalarState(vr::VRInputComponentHandle_t scalar, float value);

    std::string serialNumber_{"MICMAP_CONTROLLER_001"};
    uint32_t deviceIndex_{vr::k_unTrackedDeviceIndexInvalid};
    vr::PropertyContainerHandle_t propertyContainer_{vr::k_ulInvalidPropertyContainer};

    // Input component handles
    vr::VRInputComponentHandle_t systemButtonHandle_{vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t aButtonHandle_{vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t triggerValueHandle_{vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t triggerClickHandle_{vr::k_ulInvalidInputComponentHandle};

    // Button states
    std::atomic<bool> systemButtonPressed_{false};
    std::atomic<bool> aButtonPressed_{false};
    std::atomic<bool> triggerPressed_{false};

    // Pending button releases (for click operations)
    struct PendingRelease {
        vr::VRInputComponentHandle_t button;
        std::chrono::steady_clock::time_point releaseTime;
    };
    std::vector<PendingRelease> pendingReleases_;
    std::mutex pendingReleasesMutex_;
};

} // namespace micmap::driver
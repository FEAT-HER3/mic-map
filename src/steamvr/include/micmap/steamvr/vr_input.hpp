#pragma once

/**
 * @file vr_input.hpp
 * @brief VR input handling for SteamVR integration
 */

#include <memory>
#include <functional>
#include <string>

namespace micmap::steamvr {

/**
 * @brief Dashboard state enumeration
 */
enum class DashboardState {
    Closed,
    Open,
    Unknown
};

/**
 * @brief HMD button action types
 */
enum class HMDButtonAction {
    ToggleDashboard,
    DashboardClick,
    CustomAction
};

/**
 * @brief VR event types
 */
enum class VREventType {
    None,
    DashboardOpened,
    DashboardClosed,
    ButtonPressed,
    ButtonReleased,
    Quit
};

/**
 * @brief VR event data
 */
struct VREvent {
    VREventType type = VREventType::None;
    uint64_t timestamp = 0;
};

/**
 * @brief Callback for VR events
 */
using VREventCallback = std::function<void(const VREvent&)>;

/**
 * @brief Interface for VR input handling
 */
class IVRInput {
public:
    virtual ~IVRInput() = default;
    
    /**
     * @brief Initialize the VR input system
     * @return True if initialization was successful
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Shutdown the VR input system
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Check if the system is initialized
     * @return True if initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * @brief Check if VR runtime is available
     * @return True if VR runtime is running
     */
    virtual bool isVRAvailable() const = 0;
    
    /**
     * @brief Get the current dashboard state
     * @return Current dashboard state
     */
    virtual DashboardState getDashboardState() = 0;
    
    /**
     * @brief Send an HMD button event
     * @return True if event was sent successfully
     */
    virtual bool sendHMDButtonEvent() = 0;
    
    /**
     * @brief Send a dashboard click event
     * @return True if event was sent successfully
     */
    virtual bool sendDashboardClick() = 0;
    
    /**
     * @brief Poll for VR events
     */
    virtual void pollEvents() = 0;
    
    /**
     * @brief Set event callback
     * @param callback Callback function for VR events
     */
    virtual void setEventCallback(VREventCallback callback) = 0;
    
    /**
     * @brief Get the VR runtime name
     * @return Runtime name string
     */
    virtual std::string getRuntimeName() const = 0;
};

/**
 * @brief Create an OpenXR-based VR input handler
 * @return Unique pointer to VR input interface
 */
std::unique_ptr<IVRInput> createOpenXRInput();

/**
 * @brief Create an OpenVR-based VR input handler (fallback)
 * @return Unique pointer to VR input interface
 */
std::unique_ptr<IVRInput> createOpenVRInput();

} // namespace micmap::steamvr
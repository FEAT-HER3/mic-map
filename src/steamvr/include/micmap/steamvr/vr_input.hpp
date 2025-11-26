#pragma once

/**
 * @file vr_input.hpp
 * @brief VR input handling for SteamVR integration
 *
 * This module provides the interface for sending HMD button events to SteamVR.
 * The implementation uses OpenVR SDK for HMD button support.
 *
 * Key behaviors:
 * - If dashboard is closed: Opens the SteamVR dashboard (via ShowDashboard)
 * - If dashboard is open: Sends HMD button press to activate whatever is under
 *   the head-locked virtual pointer (like pressing the HMD button on Valve Index
 *   or A button on a gamepad)
 */

#include <memory>
#include <functional>
#include <string>

namespace micmap::steamvr {

/**
 * @brief Dashboard state enumeration
 */
enum class DashboardState {
    Closed,     ///< Dashboard is not visible
    Open,       ///< Dashboard is visible
    Unknown     ///< State cannot be determined (e.g., not connected)
};

/**
 * @brief HMD button action types
 */
enum class HMDButtonAction {
    ToggleDashboard,    ///< Toggle dashboard visibility
    DashboardSelect,    ///< Select/activate item under head-locked pointer
    CustomAction        ///< Custom user-defined action
};

/**
 * @brief VR event types
 */
enum class VREventType {
    None,               ///< No event
    DashboardOpened,    ///< Dashboard was opened
    DashboardClosed,    ///< Dashboard was closed
    ButtonPressed,      ///< HMD button was pressed
    ButtonReleased,     ///< HMD button was released
    SteamVRConnected,   ///< Connected to SteamVR
    SteamVRDisconnected,///< Disconnected from SteamVR
    Quit                ///< Application should quit (SteamVR closing)
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
 *
 * This interface provides methods for:
 * - Initializing and shutting down the VR connection
 * - Querying dashboard state
 * - Sending HMD button events
 * - Polling for VR events
 */
class IVRInput {
public:
    virtual ~IVRInput() = default;
    
    /**
     * @brief Initialize the VR input system
     * @return True if initialization was successful
     *
     * Connects to SteamVR as a background application.
     * If SteamVR is not running, returns false.
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Shutdown the VR input system
     *
     * Disconnects from SteamVR and releases all resources.
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Check if the system is initialized
     * @return True if initialized and connected to SteamVR
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * @brief Check if VR runtime is available
     * @return True if SteamVR is running and accessible
     *
     * This can be used to check if SteamVR is running before attempting
     * to initialize, or to detect if SteamVR has been closed.
     */
    virtual bool isVRAvailable() const = 0;
    
    /**
     * @brief Get the current dashboard state
     * @return Current dashboard state (Open, Closed, or Unknown)
     */
    virtual DashboardState getDashboardState() = 0;
    
    /**
     * @brief Send an HMD button press event to open the dashboard
     * @return True if event was sent successfully
     *
     * This opens the SteamVR dashboard using ShowDashboard().
     * Use when dashboard is closed.
     */
    virtual bool sendHMDButtonEvent() = 0;
    
    /**
     * @brief Send an HMD button press to select item under head-locked pointer
     * @return True if event was sent successfully
     *
     * This simulates pressing the HMD button (like Valve Index HMD button
     * or A button on gamepad) to activate whatever is under the head-locked
     * virtual pointer. Use when dashboard is already open.
     */
    virtual bool sendDashboardSelect() = 0;
    
    /**
     * @brief Perform the appropriate action based on dashboard state
     * @return True if action was performed successfully
     *
     * If dashboard is closed: Opens the dashboard via ShowDashboard()
     * If dashboard is open: Sends HMD button press to select item
     */
    virtual bool performDashboardAction() = 0;
    
    /**
     * @brief Poll for VR events
     *
     * Should be called regularly to process VR events.
     * Events are delivered via the callback set with setEventCallback().
     */
    virtual void pollEvents() = 0;
    
    /**
     * @brief Set event callback
     * @param callback Callback function for VR events
     */
    virtual void setEventCallback(VREventCallback callback) = 0;
    
    /**
     * @brief Get the VR runtime name
     * @return Runtime name string (e.g., "OpenVR", "SteamVR")
     */
    virtual std::string getRuntimeName() const = 0;
    
    /**
     * @brief Get the last error message
     * @return Error message string, empty if no error
     */
    virtual std::string getLastError() const = 0;
};

/**
 * @brief Create an OpenVR-based VR input handler
 * @return Unique pointer to VR input interface
 *
 * This is the recommended implementation for SteamVR integration.
 * Uses OpenVR SDK for HMD button events and dashboard interaction.
 */
std::unique_ptr<IVRInput> createOpenVRInput();

/**
 * @brief Create a stub VR input handler for testing
 * @return Unique pointer to VR input interface
 *
 * This implementation does not connect to any VR runtime.
 * Useful for testing without SteamVR.
 */
std::unique_ptr<IVRInput> createStubVRInput();

} // namespace micmap::steamvr
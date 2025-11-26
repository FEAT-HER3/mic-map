#pragma once

/**
 * @file dashboard_manager.hpp
 * @brief SteamVR dashboard and lifecycle management
 *
 * This module provides:
 * - SteamVR lifecycle monitoring (detect when SteamVR starts/stops)
 * - Dashboard state management
 * - Callbacks for connection state changes
 * - Overlay management for settings UI (Stage 2)
 */

#include "vr_input.hpp"

#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace micmap::steamvr {

/**
 * @brief SteamVR connection state
 */
enum class ConnectionState {
    Disconnected,   ///< Not connected to SteamVR
    Connecting,     ///< Attempting to connect
    Connected,      ///< Connected and running
    Reconnecting    ///< Lost connection, attempting to reconnect
};

/**
 * @brief Overlay visibility state
 */
enum class OverlayState {
    Hidden,
    Visible,
    Focused
};

/**
 * @brief Overlay configuration
 */
struct OverlayConfig {
    std::string name = "MicMap";
    std::string key = "micmap.overlay";
    float width = 1.0f;                 ///< Width in meters
    float distance = 1.5f;              ///< Distance from user in meters
    bool highQuality = true;            ///< Use high quality rendering
    bool cursorEnabled = true;          ///< Enable cursor interaction
};

/**
 * @brief Dashboard manager configuration
 */
struct DashboardManagerConfig {
    /// How often to check if SteamVR is available when disconnected
    std::chrono::milliseconds reconnectInterval{5000};
    
    /// Whether to automatically reconnect when SteamVR restarts
    bool autoReconnect = true;
    
    /// Whether to exit the application when SteamVR closes
    bool exitWithSteamVR = true;
};

/**
 * @brief Dashboard interaction callback
 */
using DashboardCallback = std::function<void(DashboardState)>;

/**
 * @brief Connection state change callback
 */
using ConnectionCallback = std::function<void(ConnectionState)>;

/**
 * @brief SteamVR quit callback (called when SteamVR is closing)
 */
using QuitCallback = std::function<void()>;

/**
 * @brief Interface for dashboard and lifecycle management
 *
 * This interface provides:
 * - SteamVR lifecycle monitoring
 * - Dashboard state queries and control
 * - Overlay management for settings UI
 * - Callbacks for state changes
 */
class IDashboardManager {
public:
    virtual ~IDashboardManager() = default;
    
    // Initialization
    
    /**
     * @brief Initialize the dashboard manager
     * @param vrInput VR input handler to use
     * @param config Configuration options
     * @return True if initialization was successful
     */
    virtual bool initialize(std::shared_ptr<IVRInput> vrInput,
                           const DashboardManagerConfig& config = DashboardManagerConfig{}) = 0;
    
    /**
     * @brief Shutdown the dashboard manager
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Check if initialized
     * @return True if initialized
     */
    virtual bool isInitialized() const = 0;
    
    // SteamVR Lifecycle
    
    /**
     * @brief Get current connection state
     * @return Current connection state
     */
    virtual ConnectionState getConnectionState() const = 0;
    
    /**
     * @brief Check if connected to SteamVR
     * @return True if connected
     */
    virtual bool isConnected() const = 0;
    
    /**
     * @brief Attempt to connect to SteamVR
     * @return True if connection was successful
     *
     * If already connected, returns true immediately.
     * If SteamVR is not running, returns false.
     */
    virtual bool connect() = 0;
    
    /**
     * @brief Disconnect from SteamVR
     */
    virtual void disconnect() = 0;
    
    // Dashboard state
    
    /**
     * @brief Get current dashboard state
     * @return Current dashboard state
     */
    virtual DashboardState getDashboardState() = 0;
    
    /**
     * @brief Toggle dashboard visibility
     * @return True if toggle was successful
     */
    virtual bool toggleDashboard() = 0;
    
    /**
     * @brief Open the dashboard
     * @return True if successful
     */
    virtual bool openDashboard() = 0;
    
    /**
     * @brief Close the dashboard
     * @return True if successful
     */
    virtual bool closeDashboard() = 0;
    
    /**
     * @brief Perform dashboard action based on current state
     * @return True if action was successful
     *
     * If dashboard is closed: Opens the dashboard
     * If dashboard is open: Sends HMD button press to select item
     */
    virtual bool performDashboardAction() = 0;
    
    // Overlay management (Stage 2)
    
    /**
     * @brief Create a settings overlay
     * @param config Overlay configuration
     * @return True if overlay was created
     */
    virtual bool createSettingsOverlay(const OverlayConfig& config = OverlayConfig{}) = 0;
    
    /**
     * @brief Destroy the settings overlay
     */
    virtual void destroySettingsOverlay() = 0;
    
    /**
     * @brief Check if settings overlay exists
     * @return True if overlay exists
     */
    virtual bool hasSettingsOverlay() const = 0;
    
    /**
     * @brief Get overlay state
     * @return Current overlay state
     */
    virtual OverlayState getOverlayState() const = 0;
    
    /**
     * @brief Show the settings overlay
     * @return True if successful
     */
    virtual bool showOverlay() = 0;
    
    /**
     * @brief Hide the settings overlay
     * @return True if successful
     */
    virtual bool hideOverlay() = 0;
    
    // Callbacks
    
    /**
     * @brief Set dashboard state change callback
     * @param callback Callback function
     */
    virtual void setDashboardCallback(DashboardCallback callback) = 0;
    
    /**
     * @brief Set connection state change callback
     * @param callback Callback function
     */
    virtual void setConnectionCallback(ConnectionCallback callback) = 0;
    
    /**
     * @brief Set quit callback (called when SteamVR is closing)
     * @param callback Callback function
     */
    virtual void setQuitCallback(QuitCallback callback) = 0;
    
    // Update
    
    /**
     * @brief Update dashboard state (call each frame/tick)
     *
     * This method:
     * - Polls for VR events
     * - Checks connection state
     * - Handles reconnection if configured
     * - Invokes callbacks as needed
     */
    virtual void update() = 0;
    
    /**
     * @brief Check if the application should exit
     * @return True if SteamVR has closed and exitWithSteamVR is enabled
     */
    virtual bool shouldExit() const = 0;
};

/**
 * @brief Create a dashboard manager
 * @return Unique pointer to dashboard manager
 */
std::unique_ptr<IDashboardManager> createDashboardManager();

} // namespace micmap::steamvr
#pragma once

/**
 * @file dashboard_manager.hpp
 * @brief SteamVR dashboard and overlay management
 */

#include "vr_input.hpp"

#include <memory>
#include <string>
#include <functional>

namespace micmap::steamvr {

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
 * @brief Dashboard interaction callback
 */
using DashboardCallback = std::function<void(DashboardState)>;

/**
 * @brief Interface for dashboard management
 */
class IDashboardManager {
public:
    virtual ~IDashboardManager() = default;
    
    /**
     * @brief Initialize the dashboard manager
     * @param vrInput VR input handler to use
     * @return True if initialization was successful
     */
    virtual bool initialize(std::shared_ptr<IVRInput> vrInput) = 0;
    
    /**
     * @brief Shutdown the dashboard manager
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Check if initialized
     * @return True if initialized
     */
    virtual bool isInitialized() const = 0;
    
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
    
    // Update
    
    /**
     * @brief Update dashboard state (call each frame)
     */
    virtual void update() = 0;
};

/**
 * @brief Create a dashboard manager
 * @return Unique pointer to dashboard manager
 */
std::unique_ptr<IDashboardManager> createDashboardManager();

} // namespace micmap::steamvr
/**
 * @file dashboard_manager.cpp
 * @brief Dashboard manager implementation
 */

#include "micmap/steamvr/dashboard_manager.hpp"
#include "micmap/common/logger.hpp"

namespace micmap::steamvr {

/**
 * @brief Dashboard manager implementation
 */
class DashboardManagerImpl : public IDashboardManager {
public:
    DashboardManagerImpl() = default;
    ~DashboardManagerImpl() override {
        shutdown();
    }
    
    bool initialize(std::shared_ptr<IVRInput> vrInput) override {
        if (initialized_) {
            return true;
        }
        
        if (!vrInput) {
            MICMAP_LOG_ERROR("Cannot initialize dashboard manager: null VR input");
            return false;
        }
        
        vrInput_ = std::move(vrInput);
        
        // Set up event callback
        vrInput_->setEventCallback([this](const VREvent& event) {
            handleVREvent(event);
        });
        
        initialized_ = true;
        MICMAP_LOG_INFO("Dashboard manager initialized");
        
        return true;
    }
    
    void shutdown() override {
        if (!initialized_) {
            return;
        }
        
        destroySettingsOverlay();
        vrInput_.reset();
        initialized_ = false;
        
        MICMAP_LOG_INFO("Dashboard manager shut down");
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    DashboardState getDashboardState() override {
        if (!vrInput_) {
            return DashboardState::Unknown;
        }
        return vrInput_->getDashboardState();
    }
    
    bool toggleDashboard() override {
        if (!vrInput_) {
            return false;
        }
        
        MICMAP_LOG_DEBUG("Toggling dashboard");
        return vrInput_->sendHMDButtonEvent();
    }
    
    bool openDashboard() override {
        if (!vrInput_) {
            return false;
        }
        
        auto state = getDashboardState();
        if (state == DashboardState::Open) {
            return true;  // Already open
        }
        
        MICMAP_LOG_DEBUG("Opening dashboard");
        return vrInput_->sendHMDButtonEvent();
    }
    
    bool closeDashboard() override {
        if (!vrInput_) {
            return false;
        }
        
        auto state = getDashboardState();
        if (state == DashboardState::Closed) {
            return true;  // Already closed
        }
        
        MICMAP_LOG_DEBUG("Closing dashboard");
        return vrInput_->sendHMDButtonEvent();
    }
    
    bool createSettingsOverlay(const OverlayConfig& config) override {
        if (!initialized_) {
            MICMAP_LOG_ERROR("Cannot create overlay: not initialized");
            return false;
        }
        
        if (hasOverlay_) {
            MICMAP_LOG_WARNING("Settings overlay already exists");
            return true;
        }
        
        overlayConfig_ = config;
        
        // TODO: Implement actual overlay creation using OpenVR
        // vr::VROverlay()->CreateDashboardOverlay(...)
        
        hasOverlay_ = true;
        overlayState_ = OverlayState::Hidden;
        
        MICMAP_LOG_INFO("Created settings overlay: ", config.name);
        return true;
    }
    
    void destroySettingsOverlay() override {
        if (!hasOverlay_) {
            return;
        }
        
        // TODO: Implement actual overlay destruction
        // vr::VROverlay()->DestroyOverlay(...)
        
        hasOverlay_ = false;
        overlayState_ = OverlayState::Hidden;
        
        MICMAP_LOG_INFO("Destroyed settings overlay");
    }
    
    bool hasSettingsOverlay() const override {
        return hasOverlay_;
    }
    
    OverlayState getOverlayState() const override {
        return overlayState_;
    }
    
    bool showOverlay() override {
        if (!hasOverlay_) {
            MICMAP_LOG_WARNING("Cannot show overlay: no overlay exists");
            return false;
        }
        
        // TODO: Implement actual overlay show
        // vr::VROverlay()->ShowOverlay(...)
        
        overlayState_ = OverlayState::Visible;
        MICMAP_LOG_DEBUG("Showing settings overlay");
        return true;
    }
    
    bool hideOverlay() override {
        if (!hasOverlay_) {
            return true;
        }
        
        // TODO: Implement actual overlay hide
        // vr::VROverlay()->HideOverlay(...)
        
        overlayState_ = OverlayState::Hidden;
        MICMAP_LOG_DEBUG("Hiding settings overlay");
        return true;
    }
    
    void setDashboardCallback(DashboardCallback callback) override {
        dashboardCallback_ = std::move(callback);
    }
    
    void update() override {
        if (!vrInput_) {
            return;
        }
        
        vrInput_->pollEvents();
        
        // Check for dashboard state changes
        auto currentState = getDashboardState();
        if (currentState != lastDashboardState_) {
            lastDashboardState_ = currentState;
            
            if (dashboardCallback_) {
                dashboardCallback_(currentState);
            }
        }
    }
    
private:
    void handleVREvent(const VREvent& event) {
        switch (event.type) {
            case VREventType::DashboardOpened:
                MICMAP_LOG_DEBUG("Dashboard opened");
                if (dashboardCallback_) {
                    dashboardCallback_(DashboardState::Open);
                }
                break;
                
            case VREventType::DashboardClosed:
                MICMAP_LOG_DEBUG("Dashboard closed");
                if (dashboardCallback_) {
                    dashboardCallback_(DashboardState::Closed);
                }
                break;
                
            case VREventType::Quit:
                MICMAP_LOG_INFO("VR quit event received");
                break;
                
            default:
                break;
        }
    }
    
    bool initialized_ = false;
    std::shared_ptr<IVRInput> vrInput_;
    
    bool hasOverlay_ = false;
    OverlayConfig overlayConfig_;
    OverlayState overlayState_ = OverlayState::Hidden;
    
    DashboardState lastDashboardState_ = DashboardState::Unknown;
    DashboardCallback dashboardCallback_;
};

std::unique_ptr<IDashboardManager> createDashboardManager() {
    return std::make_unique<DashboardManagerImpl>();
}

} // namespace micmap::steamvr
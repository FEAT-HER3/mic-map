/**
 * @file dashboard_manager.cpp
 * @brief Dashboard manager implementation with SteamVR lifecycle management
 * 
 * This implementation provides:
 * - SteamVR lifecycle monitoring (detect when SteamVR starts/stops)
 * - Automatic reconnection when SteamVR restarts
 * - Dashboard state management
 * - Callbacks for connection and dashboard state changes
 */

#include "micmap/steamvr/dashboard_manager.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <mutex>

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
    
    bool initialize(std::shared_ptr<IVRInput> vrInput,
                   const DashboardManagerConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) {
            return true;
        }
        
        if (!vrInput) {
            MICMAP_LOG_ERROR("Cannot initialize dashboard manager: null VR input");
            return false;
        }
        
        vrInput_ = std::move(vrInput);
        config_ = config;
        
        // Set up event callback
        vrInput_->setEventCallback([this](const VREvent& event) {
            handleVREvent(event);
        });
        
        // Try to connect to SteamVR
        connectionState_ = ConnectionState::Connecting;
        if (vrInput_->initialize()) {
            connectionState_ = ConnectionState::Connected;
            MICMAP_LOG_INFO("Dashboard manager connected to SteamVR");
        } else {
            connectionState_ = ConnectionState::Disconnected;
            MICMAP_LOG_WARNING("Dashboard manager initialized but not connected to SteamVR");
            // Not a failure - we can try to reconnect later
        }
        
        initialized_ = true;
        lastReconnectAttempt_ = std::chrono::steady_clock::now();
        
        MICMAP_LOG_INFO("Dashboard manager initialized");
        return true;
    }
    
    void shutdown() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!initialized_) {
            return;
        }
        
        destroySettingsOverlayInternal();
        
        if (vrInput_) {
            vrInput_->shutdown();
            vrInput_.reset();
        }
        
        connectionState_ = ConnectionState::Disconnected;
        initialized_ = false;
        shouldExit_ = false;
        
        MICMAP_LOG_INFO("Dashboard manager shut down");
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    // SteamVR Lifecycle
    
    ConnectionState getConnectionState() const override {
        return connectionState_;
    }
    
    bool isConnected() const override {
        return connectionState_ == ConnectionState::Connected;
    }
    
    bool connect() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!initialized_ || !vrInput_) {
            return false;
        }
        
        if (connectionState_ == ConnectionState::Connected) {
            return true;
        }
        
        connectionState_ = ConnectionState::Connecting;
        notifyConnectionState(connectionState_);
        
        if (vrInput_->initialize()) {
            connectionState_ = ConnectionState::Connected;
            notifyConnectionState(connectionState_);
            MICMAP_LOG_INFO("Connected to SteamVR");
            return true;
        }
        
        connectionState_ = ConnectionState::Disconnected;
        notifyConnectionState(connectionState_);
        return false;
    }
    
    void disconnect() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!initialized_ || !vrInput_) {
            return;
        }
        
        vrInput_->shutdown();
        connectionState_ = ConnectionState::Disconnected;
        notifyConnectionState(connectionState_);
        
        MICMAP_LOG_INFO("Disconnected from SteamVR");
    }
    
    // Dashboard state
    
    DashboardState getDashboardState() override {
        if (!vrInput_ || connectionState_ != ConnectionState::Connected) {
            return DashboardState::Unknown;
        }
        return vrInput_->getDashboardState();
    }
    
    bool toggleDashboard() override {
        if (!vrInput_ || connectionState_ != ConnectionState::Connected) {
            return false;
        }
        
        MICMAP_LOG_DEBUG("Toggling dashboard");
        return vrInput_->sendHMDButtonEvent();
    }
    
    bool openDashboard() override {
        if (!vrInput_ || connectionState_ != ConnectionState::Connected) {
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
        if (!vrInput_ || connectionState_ != ConnectionState::Connected) {
            return false;
        }
        
        auto state = getDashboardState();
        if (state == DashboardState::Closed) {
            return true;  // Already closed
        }
        
        // To close the dashboard, we send another HMD button event
        // This toggles it closed
        MICMAP_LOG_DEBUG("Closing dashboard");
        return vrInput_->sendHMDButtonEvent();
    }
    
    bool performDashboardAction() override {
        if (!vrInput_ || connectionState_ != ConnectionState::Connected) {
            return false;
        }
        
        return vrInput_->performDashboardAction();
    }
    
    // Overlay management
    
    bool createSettingsOverlay(const OverlayConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
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
        // This is Stage 2 functionality
        // vr::VROverlay()->CreateDashboardOverlay(...)
        
        hasOverlay_ = true;
        overlayState_ = OverlayState::Hidden;
        
        MICMAP_LOG_INFO("Created settings overlay: ", config.name);
        return true;
    }
    
    void destroySettingsOverlay() override {
        std::lock_guard<std::mutex> lock(mutex_);
        destroySettingsOverlayInternal();
    }
    
    bool hasSettingsOverlay() const override {
        return hasOverlay_;
    }
    
    OverlayState getOverlayState() const override {
        return overlayState_;
    }
    
    bool showOverlay() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
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
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!hasOverlay_) {
            return true;
        }
        
        // TODO: Implement actual overlay hide
        // vr::VROverlay()->HideOverlay(...)
        
        overlayState_ = OverlayState::Hidden;
        MICMAP_LOG_DEBUG("Hiding settings overlay");
        return true;
    }
    
    // Callbacks
    
    void setDashboardCallback(DashboardCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        dashboardCallback_ = std::move(callback);
    }
    
    void setConnectionCallback(ConnectionCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        connectionCallback_ = std::move(callback);
    }
    
    void setQuitCallback(QuitCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        quitCallback_ = std::move(callback);
    }
    
    // Update
    
    void update() override {
        if (!initialized_) {
            return;
        }
        
        // Poll VR events if connected
        if (vrInput_ && connectionState_ == ConnectionState::Connected) {
            vrInput_->pollEvents();
            
            // Check for dashboard state changes
            auto currentState = getDashboardState();
            if (currentState != lastDashboardState_ && currentState != DashboardState::Unknown) {
                lastDashboardState_ = currentState;
                notifyDashboardState(currentState);
            }
            
            // Check if VR is still available
            if (!vrInput_->isVRAvailable()) {
                MICMAP_LOG_WARNING("SteamVR connection lost");
                connectionState_ = ConnectionState::Disconnected;
                notifyConnectionState(connectionState_);
                
                if (config_.exitWithSteamVR) {
                    shouldExit_ = true;
                }
            }
        }
        
        // Handle reconnection
        if (config_.autoReconnect && 
            connectionState_ == ConnectionState::Disconnected &&
            !shouldExit_) {
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastReconnectAttempt_);
            
            if (elapsed >= config_.reconnectInterval) {
                lastReconnectAttempt_ = now;
                
                // Check if SteamVR is now available
                if (vrInput_ && vrInput_->isVRAvailable()) {
                    MICMAP_LOG_INFO("SteamVR detected - attempting reconnection");
                    connectionState_ = ConnectionState::Reconnecting;
                    notifyConnectionState(connectionState_);
                    
                    if (vrInput_->initialize()) {
                        connectionState_ = ConnectionState::Connected;
                        notifyConnectionState(connectionState_);
                        MICMAP_LOG_INFO("Reconnected to SteamVR");
                    } else {
                        connectionState_ = ConnectionState::Disconnected;
                        notifyConnectionState(connectionState_);
                    }
                }
            }
        }
    }
    
    bool shouldExit() const override {
        return shouldExit_;
    }
    
private:
    void destroySettingsOverlayInternal() {
        if (!hasOverlay_) {
            return;
        }
        
        // TODO: Implement actual overlay destruction
        // vr::VROverlay()->DestroyOverlay(...)
        
        hasOverlay_ = false;
        overlayState_ = OverlayState::Hidden;
        
        MICMAP_LOG_INFO("Destroyed settings overlay");
    }
    
    void handleVREvent(const VREvent& event) {
        switch (event.type) {
            case VREventType::DashboardOpened:
                MICMAP_LOG_DEBUG("Dashboard opened event");
                lastDashboardState_ = DashboardState::Open;
                notifyDashboardState(DashboardState::Open);
                break;
                
            case VREventType::DashboardClosed:
                MICMAP_LOG_DEBUG("Dashboard closed event");
                lastDashboardState_ = DashboardState::Closed;
                notifyDashboardState(DashboardState::Closed);
                break;
                
            case VREventType::SteamVRConnected:
                MICMAP_LOG_INFO("SteamVR connected event");
                connectionState_ = ConnectionState::Connected;
                notifyConnectionState(connectionState_);
                break;
                
            case VREventType::SteamVRDisconnected:
                MICMAP_LOG_INFO("SteamVR disconnected event");
                connectionState_ = ConnectionState::Disconnected;
                notifyConnectionState(connectionState_);
                break;
                
            case VREventType::Quit:
                MICMAP_LOG_INFO("SteamVR quit event - application should exit");
                shouldExit_ = true;
                notifyQuit();
                break;
                
            default:
                break;
        }
    }
    
    void notifyDashboardState(DashboardState state) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (dashboardCallback_) {
            dashboardCallback_(state);
        }
    }
    
    void notifyConnectionState(ConnectionState state) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (connectionCallback_) {
            connectionCallback_(state);
        }
    }
    
    void notifyQuit() {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (quitCallback_) {
            quitCallback_();
        }
    }
    
    // State
    bool initialized_ = false;
    std::shared_ptr<IVRInput> vrInput_;
    DashboardManagerConfig config_;
    ConnectionState connectionState_ = ConnectionState::Disconnected;
    bool shouldExit_ = false;
    
    // Overlay state
    bool hasOverlay_ = false;
    OverlayConfig overlayConfig_;
    OverlayState overlayState_ = OverlayState::Hidden;
    
    // Dashboard tracking
    DashboardState lastDashboardState_ = DashboardState::Unknown;
    
    // Reconnection
    std::chrono::steady_clock::time_point lastReconnectAttempt_;
    
    // Callbacks
    DashboardCallback dashboardCallback_;
    ConnectionCallback connectionCallback_;
    QuitCallback quitCallback_;
    
    // Thread safety
    mutable std::mutex mutex_;
    mutable std::mutex callbackMutex_;
};

std::unique_ptr<IDashboardManager> createDashboardManager() {
    return std::make_unique<DashboardManagerImpl>();
}

} // namespace micmap::steamvr
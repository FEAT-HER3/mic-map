/**
 * @file vr_input.cpp
 * @brief VR input implementation using OpenVR SDK
 *
 * Implementation approach for dashboard interaction:
 *
 * 1. Dashboard closed: Use IVROverlay::ShowDashboard() to open the dashboard
 * 2. Dashboard open: Use the MicMap driver via HTTP to inject button events
 *    that simulate HMD button press, activating whatever is under the head-locked
 *    virtual pointer.
 *
 * The OpenVR approach was chosen over OpenXR because:
 * - OpenVR has direct access to IVROverlay for dashboard state queries
 * - OpenVR provides ShowDashboard() for opening the dashboard
 * - The MicMap driver can inject button events via IVRDriverInput
 */

#include "micmap/steamvr/vr_input.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <mutex>

#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#endif

// Include httplib for HTTP client (without OpenSSL support)
// Note: We don't need HTTPS for localhost communication
#include <httplib.h>

namespace micmap::steamvr {

// ============================================================================
// Stub VR Input Implementation (for testing without SteamVR)
// ============================================================================

/**
 * @brief Stub VR input implementation for testing
 */
class StubVRInput : public IVRInput {
public:
    StubVRInput() = default;
    ~StubVRInput() override = default;
    
    bool initialize() override {
        MICMAP_LOG_INFO("Initializing VR input (stub implementation)");
        initialized_ = true;
        return true;
    }
    
    void shutdown() override {
        MICMAP_LOG_INFO("Shutting down VR input (stub)");
        initialized_ = false;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    bool isVRAvailable() const override {
        // Stub always returns false - no real VR
        return false;
    }
    
    DashboardState getDashboardState() override {
        return dashboardState_;
    }
    
    bool sendHMDButtonEvent() override {
        if (!initialized_) {
            lastError_ = "Not initialized";
            MICMAP_LOG_WARNING("Cannot send HMD button event: not initialized");
            return false;
        }
        
        MICMAP_LOG_INFO("Sending HMD button event (stub) - opening dashboard");
        dashboardState_ = DashboardState::Open;
        notifyEvent(VREventType::DashboardOpened);
        return true;
    }
    
    bool sendDashboardSelect() override {
        if (!initialized_) {
            lastError_ = "Not initialized";
            MICMAP_LOG_WARNING("Cannot send dashboard select: not initialized");
            return false;
        }
        
        MICMAP_LOG_INFO("Sending dashboard select (stub) - HMD button press");
        notifyEvent(VREventType::ButtonPressed);
        notifyEvent(VREventType::ButtonReleased);
        return true;
    }
    
    bool performDashboardAction() override {
        if (!initialized_) {
            lastError_ = "Not initialized";
            return false;
        }
        
        auto state = getDashboardState();
        if (state == DashboardState::Closed || state == DashboardState::Unknown) {
            MICMAP_LOG_DEBUG("Dashboard closed - opening");
            return sendHMDButtonEvent();
        } else {
            MICMAP_LOG_DEBUG("Dashboard open - sending select");
            return sendDashboardSelect();
        }
    }
    
    void pollEvents() override {
        // Stub implementation - no events to poll
    }
    
    void setEventCallback(VREventCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        eventCallback_ = std::move(callback);
    }
    
    std::string getRuntimeName() const override {
        return "Stub VR Runtime";
    }
    
    std::string getLastError() const override {
        return lastError_;
    }
    
protected:
    void notifyEvent(VREventType type) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (eventCallback_) {
            VREvent event;
            event.type = type;
            event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            eventCallback_(event);
        }
    }
    
    bool initialized_ = false;
    DashboardState dashboardState_ = DashboardState::Closed;
    std::string lastError_;
    VREventCallback eventCallback_;
    std::mutex callbackMutex_;
};

// ============================================================================
// Driver Client Implementation
// ============================================================================

/**
 * @brief HTTP client for communicating with the MicMap driver
 */
class DriverClient : public IDriverClient {
public:
    DriverClient(const std::string& host, int startPort, int endPort)
        : host_(host)
        , startPort_(startPort)
        , endPort_(endPort)
    {
        MICMAP_LOG_DEBUG("DriverClient created (host: {}, ports: {}-{})",
                         host_, startPort_, endPort_);
    }

    ~DriverClient() override {
        disconnect();
    }

    bool connect() override {
        if (connected_) {
            return true;
        }

        MICMAP_LOG_INFO("Connecting to MicMap driver...");

        // Try each port in the range
        for (int port = startPort_; port <= endPort_; ++port) {
            MICMAP_LOG_DEBUG("Trying port {}...", port);
            
            httplib::Client client(host_, port);
            client.set_connection_timeout(1);  // 1 second timeout
            client.set_read_timeout(1);

            // Try to get status
            auto res = client.Get("/health");
            if (res && res->status == 200) {
                port_ = port;
                connected_ = true;
                MICMAP_LOG_INFO("Connected to MicMap driver on port {}", port_);
                return true;
            }
        }

        lastError_ = "Could not connect to MicMap driver on any port";
        MICMAP_LOG_WARNING(lastError_);
        return false;
    }

    void disconnect() override {
        if (connected_) {
            MICMAP_LOG_INFO("Disconnecting from MicMap driver");
            connected_ = false;
            port_ = 0;
        }
    }

    bool isConnected() const override {
        return connected_;
    }

    bool click(const std::string& button, int durationMs) override {
        if (!ensureConnected()) {
            return false;
        }

        MICMAP_LOG_DEBUG("Sending click command (button: {}, duration: {}ms)",
                         button, durationMs);

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        std::string path = "/click?button=" + button + "&duration=" + std::to_string(durationMs);
        auto res = client.Post(path);

        if (!res) {
            lastError_ = "HTTP request failed";
            MICMAP_LOG_ERROR("Click command failed: {}", lastError_);
            connected_ = false;  // Mark as disconnected to retry
            return false;
        }

        if (res->status != 200) {
            lastError_ = "Server returned status " + std::to_string(res->status);
            MICMAP_LOG_ERROR("Click command failed: {}", lastError_);
            return false;
        }

        MICMAP_LOG_DEBUG("Click command successful");
        return true;
    }

    bool press(const std::string& button) override {
        if (!ensureConnected()) {
            return false;
        }

        MICMAP_LOG_DEBUG("Sending press command (button: {})", button);

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        std::string path = "/press?button=" + button;
        auto res = client.Post(path);

        if (!res) {
            lastError_ = "HTTP request failed";
            MICMAP_LOG_ERROR("Press command failed: {}", lastError_);
            connected_ = false;
            return false;
        }

        if (res->status != 200) {
            lastError_ = "Server returned status " + std::to_string(res->status);
            MICMAP_LOG_ERROR("Press command failed: {}", lastError_);
            return false;
        }

        MICMAP_LOG_DEBUG("Press command successful");
        return true;
    }

    bool release(const std::string& button) override {
        if (!ensureConnected()) {
            return false;
        }

        MICMAP_LOG_DEBUG("Sending release command (button: {})", button);

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        std::string path = "/release?button=" + button;
        auto res = client.Post(path);

        if (!res) {
            lastError_ = "HTTP request failed";
            MICMAP_LOG_ERROR("Release command failed: {}", lastError_);
            connected_ = false;
            return false;
        }

        if (res->status != 200) {
            lastError_ = "Server returned status " + std::to_string(res->status);
            MICMAP_LOG_ERROR("Release command failed: {}", lastError_);
            return false;
        }

        MICMAP_LOG_DEBUG("Release command successful");
        return true;
    }

    bool getStatus() override {
        if (!ensureConnected()) {
            return false;
        }

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        auto res = client.Get("/status");

        if (!res || res->status != 200) {
            lastError_ = "Status check failed";
            connected_ = false;
            return false;
        }

        return true;
    }

    int getPort() const override {
        return port_;
    }

    std::string getLastError() const override {
        return lastError_;
    }

private:
    bool ensureConnected() {
        if (connected_) {
            return true;
        }
        return connect();
    }

    std::string host_;
    int startPort_;
    int endPort_;
    int port_ = 0;
    bool connected_ = false;
    std::string lastError_;
};

// ============================================================================
// OpenVR Input Implementation
// ============================================================================

#ifdef MICMAP_HAS_OPENVR

/**
 * @brief OpenVR-based VR input implementation
 * 
 * Uses OpenVR SDK for:
 * - Connecting to SteamVR as a background application
 * - Querying dashboard visibility via IVROverlay
 * - Opening dashboard via ShowDashboard()
 * - Simulating HMD button press for dashboard selection
 */
class OpenVRInput : public IVRInput {
public:
    OpenVRInput() {
        MICMAP_LOG_DEBUG("Created OpenVR input handler");
    }
    
    ~OpenVRInput() override {
        shutdown();
    }
    
    bool initialize() override {
        if (initialized_) {
            return true;
        }
        
        MICMAP_LOG_INFO("Initializing OpenVR input");
        
        // Check if SteamVR is running
        if (!vr::VR_IsRuntimeInstalled()) {
            lastError_ = "OpenVR runtime is not installed";
            MICMAP_LOG_ERROR(lastError_);
            return false;
        }
        
        if (!vr::VR_IsHmdPresent()) {
            lastError_ = "No HMD detected";
            MICMAP_LOG_WARNING(lastError_);
            // Continue anyway - we might be running without HMD for testing
        }
        
        // Initialize OpenVR as a background application
        // VRApplication_Background allows us to run without rendering
        vr::EVRInitError initError = vr::VRInitError_None;
        vrSystem_ = vr::VR_Init(&initError, vr::VRApplication_Background);
        
        if (initError != vr::VRInitError_None) {
            lastError_ = std::string("Failed to initialize OpenVR: ") + 
                        vr::VR_GetVRInitErrorAsEnglishDescription(initError);
            MICMAP_LOG_ERROR(lastError_);
            vrSystem_ = nullptr;
            return false;
        }
        
        // Get overlay interface for dashboard queries
        vrOverlay_ = vr::VROverlay();
        if (!vrOverlay_) {
            lastError_ = "Failed to get IVROverlay interface";
            MICMAP_LOG_ERROR(lastError_);
            vr::VR_Shutdown();
            vrSystem_ = nullptr;
            return false;
        }
        
        initialized_ = true;
        MICMAP_LOG_INFO("OpenVR initialized successfully");
        
        // Notify connection
        notifyEvent(VREventType::SteamVRConnected);
        
        return true;
    }
    
    void shutdown() override {
        if (!initialized_) {
            return;
        }
        
        MICMAP_LOG_INFO("Shutting down OpenVR input");
        
        vrOverlay_ = nullptr;
        vrSystem_ = nullptr;
        
        vr::VR_Shutdown();
        
        initialized_ = false;
        
        notifyEvent(VREventType::SteamVRDisconnected);
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    bool isVRAvailable() const override {
        // Check if SteamVR is running
        return vr::VR_IsRuntimeInstalled() && vr::VR_IsHmdPresent();
    }
    
    DashboardState getDashboardState() override {
        if (!initialized_ || !vrOverlay_) {
            return DashboardState::Unknown;
        }
        
        // Query dashboard visibility
        bool isVisible = vrOverlay_->IsDashboardVisible();
        return isVisible ? DashboardState::Open : DashboardState::Closed;
    }
    
    bool sendHMDButtonEvent() override {
        if (!initialized_ || !vrOverlay_) {
            lastError_ = "Not initialized";
            MICMAP_LOG_WARNING("Cannot send HMD button event: not initialized");
            return false;
        }
        
        MICMAP_LOG_INFO("Opening SteamVR dashboard");
        
        // Use ShowDashboard to open the dashboard
        // The empty string opens the main dashboard
        // Note: ShowDashboard returns void in OpenVR API
        vrOverlay_->ShowDashboard("");
        
        notifyEvent(VREventType::DashboardOpened);
        return true;
    }
    
    bool sendDashboardSelect() override {
        if (!initialized_ || !vrSystem_) {
            lastError_ = "Not initialized";
            MICMAP_LOG_WARNING("Cannot send dashboard select: not initialized");
            return false;
        }
        
        MICMAP_LOG_INFO("Sending HMD button press for dashboard selection via driver");
        
        // Use the MicMap driver to inject a button event
        // This is the proper way to inject button events in OpenVR
        if (!driverClient_) {
            driverClient_ = createDriverClient();
        }
        
        if (!driverClient_->isConnected()) {
            if (!driverClient_->connect()) {
                lastError_ = "Failed to connect to MicMap driver: " + driverClient_->getLastError();
                MICMAP_LOG_WARNING(lastError_);
                MICMAP_LOG_WARNING("Make sure the MicMap driver is installed and SteamVR is running");
                return false;
            }
        }
        
        // Send click command to the driver - use trigger for laser mouse selection
        if (!driverClient_->click("trigger", 100)) {
            lastError_ = "Failed to send click command: " + driverClient_->getLastError();
            MICMAP_LOG_ERROR(lastError_);
            return false;
        }
        
        notifyEvent(VREventType::ButtonPressed);
        notifyEvent(VREventType::ButtonReleased);
        
        MICMAP_LOG_INFO("Dashboard select sent successfully via driver");
        return true;
    }
    
    bool performDashboardAction() override {
        if (!initialized_) {
            lastError_ = "Not initialized";
            return false;
        }
        
        auto state = getDashboardState();
        if (state == DashboardState::Closed || state == DashboardState::Unknown) {
            MICMAP_LOG_DEBUG("Dashboard closed - opening");
            return sendHMDButtonEvent();
        } else {
            MICMAP_LOG_DEBUG("Dashboard open - sending select");
            return sendDashboardSelect();
        }
    }
    
    void pollEvents() override {
        if (!initialized_ || !vrSystem_) {
            return;
        }
        
        vr::VREvent_t event;
        while (vrSystem_->PollNextEvent(&event, sizeof(event))) {
            processVREvent(event);
        }
    }
    
    void setEventCallback(VREventCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        eventCallback_ = std::move(callback);
    }
    
    std::string getRuntimeName() const override {
        return "OpenVR (SteamVR)";
    }
    
    std::string getLastError() const override {
        return lastError_;
    }
    
private:
    void processVREvent(const vr::VREvent_t& event) {
        switch (event.eventType) {
            case vr::VREvent_Quit:
                MICMAP_LOG_INFO("SteamVR quit event received");
                notifyEvent(VREventType::Quit);
                break;
                
            case vr::VREvent_DashboardActivated:
                MICMAP_LOG_DEBUG("Dashboard activated");
                notifyEvent(VREventType::DashboardOpened);
                break;
                
            case vr::VREvent_DashboardDeactivated:
                MICMAP_LOG_DEBUG("Dashboard deactivated");
                notifyEvent(VREventType::DashboardClosed);
                break;
                
            case vr::VREvent_ButtonPress:
                notifyEvent(VREventType::ButtonPressed);
                break;
                
            case vr::VREvent_ButtonUnpress:
                notifyEvent(VREventType::ButtonReleased);
                break;
                
            default:
                // Ignore other events
                break;
        }
    }
    
    void notifyEvent(VREventType type) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (eventCallback_) {
            VREvent event;
            event.type = type;
            event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            eventCallback_(event);
        }
    }
    
    bool initialized_ = false;
    vr::IVRSystem* vrSystem_ = nullptr;
    vr::IVROverlay* vrOverlay_ = nullptr;
    std::string lastError_;
    VREventCallback eventCallback_;
    std::mutex callbackMutex_;
    std::unique_ptr<IDriverClient> driverClient_;
};

#endif // MICMAP_HAS_OPENVR

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IVRInput> createOpenVRInput() {
#ifdef MICMAP_HAS_OPENVR
    return std::make_unique<OpenVRInput>();
#else
    MICMAP_LOG_WARNING("OpenVR not available - using stub implementation");
    return std::make_unique<StubVRInput>();
#endif
}

std::unique_ptr<IVRInput> createStubVRInput() {
    return std::make_unique<StubVRInput>();
}

std::unique_ptr<IDriverClient> createDriverClient(
    const std::string& host,
    int startPort,
    int endPort)
{
    return std::make_unique<DriverClient>(host, startPort, endPort);
}

} // namespace micmap::steamvr
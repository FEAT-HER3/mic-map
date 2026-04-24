/**
 * @file vr_input.cpp
 * @brief VR input implementation using OpenVR SDK
 *
 * This module connects to SteamVR as a background application for the sole
 * purpose of monitoring Quit/SteamVRConnected/SteamVRDisconnected events.
 * All button edges go through IDriverClient (POST /button) which the driver
 * translates into /input/system/click edges on the HMD property container
 * (Plan 01-03).
 */

#include "micmap/steamvr/vr_input.hpp"
#include "micmap/steamvr/vr_input_events.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <cstdint>
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
        MICMAP_LOG_DEBUG("DriverClient created (host: ", host_,
                         ", ports: ", startPort_, "-", endPort_, ")");
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
            MICMAP_LOG_DEBUG("Trying port ", port, "...");
            
            httplib::Client client(host_, port);
            client.set_connection_timeout(1);  // 1 second timeout
            client.set_read_timeout(1);

            // Try to get status
            auto res = client.Get("/health");
            if (res && res->status == 200) {
                port_ = port;
                connected_ = true;
                MICMAP_LOG_INFO("Connected to MicMap driver on port ", port_);
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

    bool tap() override {
        if (!ensureConnected()) {
            lastError_ = "Not connected to driver";
            MICMAP_LOG_ERROR("DriverClient::tap() failed: ", lastError_);
            return false;
        }

        MICMAP_LOG_DEBUG("Sending tap (POST /button {\"kind\":\"tap\"})");

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        auto res = client.Post("/button", R"({"kind":"tap"})", "application/json");

        if (!res) {
            lastError_ = "HTTP request failed";
            MICMAP_LOG_ERROR("DriverClient::tap() failed: ", lastError_);
            connected_ = false;  // Mark as disconnected to retry
            return false;
        }

        if (res->status != 200) {
            lastError_ = "Server returned status " + std::to_string(res->status);
            MICMAP_LOG_ERROR("DriverClient::tap() failed: ", lastError_);
            return false;
        }

        MICMAP_LOG_DEBUG("DriverClient::tap() successful");
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
 * - Connecting to SteamVR as a background application (VRApplication_Background)
 * - Polling lifecycle events (SteamVR quit, connection, etc.)
 *
 * Does NOT handle button presses — those flow through IDriverClient to the
 * MicMap driver which owns /input/system/click on the HMD container.
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
    // Adapters that let OpenVRInput::processVREvent delegate to the free
    // processVREventImpl(IVRSystemSeam&, IEventSink&, uint32_t) without
    // leaking OpenVR surface into the test-only seam header.
    //
    // Defined as nested private classes so EventSinkAdapter has access to
    // OpenVRInput::notifyEvent without a friend declaration.
    class VRSystemAdapter : public IVRSystemSeam {
    public:
        explicit VRSystemAdapter(vr::IVRSystem* sys) : sys_(sys) {}
        void AcknowledgeQuit_Exiting() override {
            // D-11 / Pitfall 2 / OpenVR #1425: this is THE call that
            // stops Valve's 2-second quit watchdog. Called BEFORE the
            // app-level notifyEvent callback in processVREventImpl.
            if (sys_) sys_->AcknowledgeQuit_Exiting();
        }
    private:
        vr::IVRSystem* sys_;
    };

    class EventSinkAdapter : public IEventSink {
    public:
        explicit EventSinkAdapter(OpenVRInput& self) : self_(self) {}
        void notifyEvent(VREventType t) override { self_.notifyEvent(t); }
    private:
        OpenVRInput& self_;
    };

    void processVREvent(const vr::VREvent_t& event) {
        // Delegate to the testable free function so production and unit
        // tests share the same ack-before-notify ordering logic.
        // See src/steamvr/include/micmap/steamvr/vr_input_events.hpp.
        VRSystemAdapter  sysAdapter(vrSystem_);
        EventSinkAdapter sinkAdapter(*this);
        processVREventImpl(sysAdapter, sinkAdapter,
                           static_cast<uint32_t>(event.eventType));
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
    std::string lastError_;
    VREventCallback eventCallback_;
    std::mutex callbackMutex_;
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
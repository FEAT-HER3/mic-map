#pragma once

/**
 * @file vr_input.hpp
 * @brief VR input handling for SteamVR integration
 *
 * This module provides:
 * - IVRInput: connection + Quit-lifecycle monitoring against SteamVR via OpenVR
 *   (used by apps to detect SteamVR shutdown and to know whether the runtime
 *   is available). No dashboard-state branching — the MicMap driver owns
 *   /input/system/click directly (Plan 01-03) and the app simply pushes edges
 *   over HTTP via IDriverClient.
 * - IDriverClient: HTTP surface that sends a single tap() to the driver's
 *   POST /button endpoint. The driver handles the full press+release
 *   sequence internally (D-04 / D-05 semantics).
 */

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <chrono>   // P7 D-10: DriverClient::isDriverDetectionActive cache TTL

namespace micmap::steamvr {

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
 * - Initializing and shutting down the connection to SteamVR
 * - Polling for VR lifecycle events (Quit, SteamVRConnected/Disconnected)
 *
 * Button presses are NOT sent through this interface; they go through
 * IDriverClient -> POST /button to the MicMap driver.
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
 * Uses OpenVR SDK as a VRApplication_Background for lifecycle monitoring
 * (Quit / SteamVRConnected / SteamVRDisconnected events).
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

/**
 * @brief Interface for communicating with the MicMap driver
 *
 * This client connects to the MicMap OpenVR driver's HTTP server
 * to send button injection commands.
 */
class IDriverClient {
public:
    virtual ~IDriverClient() = default;

    /**
     * @brief Connect to the driver
     * @return True if connection was successful
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from the driver
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected to the driver
     * @return True if connected
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Fire a single tap on the SteamVR HMD system button.
     * @return true if the HTTP request returned 200 OK.
     *
     * Sends POST /button with body {"kind":"tap"}. The driver performs
     * UpdateBooleanComponent(true), holds for ~150 ms (its own min-hold
     * floor), then UpdateBooleanComponent(false). SteamVR's
     * complex_button binding interprets the resulting press+release as a
     * single-click -> ToggleDashboard action.
     */
    virtual bool tap() = 0;

    /**
     * @brief Get driver status
     * @return True if driver is healthy
     */
    virtual bool getStatus() = 0;

    /**
     * @brief Get the port the driver is running on
     * @return Port number, or 0 if not connected
     */
    virtual int getPort() const = 0;

    /**
     * @brief Get the last error message
     * @return Error message string
     */
    virtual std::string getLastError() const = 0;

    /**
     * @brief Returns true iff the driver currently owns the detection path.
     *
     * P7 D-10: polls the driver's `GET /health` endpoint and parses the
     * `driver_detection_active` boolean field (added by P7 D-09 / 07-05
     * Task 1). Result is cached for ~1 second to keep onTrigger latency
     * bounded (Pitfall 10 mitigation — client suppresses its own POST
     * /button when the driver owns the detection path; state machine
     * cooldown is the belt-and-suspenders backstop per D-11).
     *
     * Returns false defensively when not connected, when /health is
     * unreachable, when the field is missing, or when JSON parse fails.
     * The principle: if we cannot determine that the driver actively
     * owns detection, we do NOT suppress — the client falls back to its
     * own trigger path (no restart required when the driver flag flips
     * mid-session or the driver crashes).
     *
     * Deleted in P10 (D-12) along with tap() and the entire trigger-
     * coexistence scaffolding once cutover completes.
     */
    virtual bool isDriverDetectionActive() = 0;
};

/**
 * @brief Create a driver client
 * @param host Host to connect to (default: 127.0.0.1)
 * @param startPort Starting port to try (default: 27015)
 * @param endPort Ending port to try (default: 27025)
 * @return Unique pointer to driver client interface
 *
 * The client will try ports in the range [startPort, endPort] to find
 * the driver's HTTP server.
 */
std::unique_ptr<IDriverClient> createDriverClient(
    const std::string& host = "127.0.0.1",
    int startPort = 27015,
    int endPort = 27025);

} // namespace micmap::steamvr
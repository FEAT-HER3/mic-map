#pragma once

/**
 * @file driver_api.hpp
 * @brief MicMap driver HTTP IPC surface (P8 D-22 rename of vr_input.hpp).
 *
 * This header is the single source of truth for the client -> driver HTTP
 * contract. It exposes:
 *
 * - IDriverApi: API client for the MicMap driver HTTP IPC surface. Sends
 *   tap() over POST /button and probes /health for connectivity + driver
 *   detection-active status. The 3-state ConnectResult enum on connect()
 *   distinguishes ECONNREFUSED (driver not running) from read/write
 *   timeouts (driver up but slow), per Pitfall 6.
 *
 * - IVRInput: connection + Quit-lifecycle monitoring against SteamVR via
 *   OpenVR (used by apps to detect SteamVR shutdown and to know whether
 *   the runtime is available). Co-located here historically; both surfaces
 *   are owned by micmap_steamvr. No dashboard-state branching — the
 *   MicMap driver owns /input/system/click directly (Plan 01-03) and the
 *   app simply pushes edges over HTTP via IDriverApi.
 *
 * Phase 8 D-22 rename: file was vr_input.hpp; the prior driver-side
 * client class and factory now use the API naming (IDriverApi /
 * createDriverApi).
 */

#include <memory>
#include <functional>
#include <optional>   // P8 IPC-01..04: getState / getSettings / getDevices / getTelemetryLevel return std::optional
#include <string>
#include <vector>
#include <chrono>     // P7 D-10: DriverApi::isDriverDetectionActive cache TTL
                      // P8: DriverStateView::last_trigger_at system_clock::time_point

#include "micmap/core/config_manager.hpp"   // P8 IPC-04: AppConfig type for getSettings()

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
 * IDriverApi -> POST /button to the MicMap driver.
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
 * @brief P8 IPC-01: read-only client view of GET /state. Mirrors the
 *        driver-side DriverState POD (driver/src/driver_state.hpp) but
 *        adds the two derived fields (driver_loaded / steamvr_running)
 *        that the driver computes at request time.
 */
struct DriverStateView {
    bool driver_loaded{false};
    bool steamvr_running{false};
    std::string detection_state;     ///< "idle" | "training" | "detecting" | "triggered" | "cooldown"
    std::optional<std::chrono::system_clock::time_point> last_trigger_at;
    std::optional<std::string> last_error;
    std::string audio_device_id;
    std::string audio_device_state;  ///< "ok" | "missing" | "permission_denied"
};

/**
 * @brief P8 IPC-03: read-only client view of one entry in GET /devices.
 *        UTF-8 strings (driver does the WASAPI UTF-16 -> UTF-8 conversion).
 */
struct DeviceInfoView {
    std::string id;
    std::string name;
    bool isDefault{false};
};

/**
 * @brief P8 IPC-02 / HEALTH-06: read-only client view of GET /telemetry/level.
 */
struct TelemetryLevel {
    float rms_normalized{0.0f};
    float dbfs{-60.0f};
};

// ============================================================================
// P9 09-02 — training-mode views (mirror driver-side wire shape from
// driver/src/http_server.hpp + driver/src/training_session.hpp). Surfaced
// through IDriverApi::getTrainingProgress (TRAIN-02 client-side).
// ============================================================================

/**
 * @brief Client-side mirror of driver/src/http_server.hpp ThresholdsPreviewView.
 *        Populated when /training/progress reports thresholds_preview != null
 *        (state == ready / finalized).
 */
struct ThresholdsPreviewView {
    float sensitivity{0.0f};
    float energy_threshold{0.0f};
    struct {
        float  mean{0.0f};
        float  stddev{0.0f};
        size_t size{0};
    } spectral_profile_summary;
};

/**
 * @brief Client-side mirror of driver/src/http_server.hpp TrainingProgressView.
 *        state is one of "idle" | "collecting" | "computing" | "ready" |
 *        "cancelled" | "finalized". last_error populated only on a failed
 *        compute / timeout transition.
 */
struct TrainingProgressView {
    size_t                                  samples_collected{0};
    size_t                                  target{100};
    std::optional<ThresholdsPreviewView>    thresholds_preview;
    std::string                             state;
    std::optional<std::string>              last_error;
};

/**
 * @brief P9 09-02 — 5-state result of IDriverApi training-write methods.
 *
 * Status outcomes the UI must distinguish:
 *   Ok                — driver returned 200; the training operation succeeded.
 *   ValidationFailed  — driver returned 400 with {field, reason} envelope.
 *                       errorField/errorReason carry the field name + human
 *                       reason for surfacing in the UI.
 *   ConnectionFailed  — httplib reported Error::Connection (driver not running).
 *   OtherError        — transport timeout, HTTP 5xx, malformed body, etc.
 *                       For 503 audio_disabled the body's {error, reason} are
 *                       parsed into errorField/errorReason for UI surfacing.
 *   Conflict          — driver returned 409: training_in_progress (start),
 *                       insufficient_samples (finalize), no_active_session
 *                       (finalize/cancel/recompute), not_ready (recompute).
 */
struct TrainingResult {
    enum Status { Ok, ValidationFailed, ConnectionFailed, OtherError, Conflict };
    Status status{OtherError};
    std::optional<std::string> errorField;
    std::optional<std::string> errorReason;
};

/**
 * @brief P9 09-02 — read-only client view of GET /health.
 *
 * Mirrors the driver's /health JSON envelope: status field is always
 * "healthy" when 200; the four boolean flags below are the migration
 * handshake the client uses to gate UI enablement (Train button visible iff
 * driver_audio_enabled, etc.).
 *
 * driver_loaded is true when the /health response was 200 (the endpoint
 * reached us, so driver is alive). The caller of getHealth() infers
 * driver_loaded from the std::optional return — nullopt = not loaded.
 */
struct HealthView {
    bool driver_loaded{false};            ///< inferred from HTTP 200; true when this view is returned
    bool driver_detection_active{false};  ///< P7 D-09
    bool driver_training_active{false};   ///< P9 D-07
    bool driver_audio_enabled{false};     ///< P9 09-02 / 09-03 T2 — proactive disable contract
};

/**
 * @brief P8 D-09 / IPC-04 — 4-state result of IDriverApi::putSettings().
 *
 * Status outcomes the UI must distinguish (per UI-SPEC):
 *   Ok                — driver returned 200; new config is persisted + published.
 *   ValidationFailed  — driver returned 400 with {"field","reason"} envelope.
 *                       errorField/errorReason carry the dot-path + human text
 *                       so the settings panel can highlight the offending input.
 *   ConnectionFailed  — httplib reported Error::Connection (driver not running).
 *   OtherError        — transport timeout, HTTP 5xx, malformed body, etc.
 */
struct PutSettingsResult {
    enum Status { Ok, ValidationFailed, ConnectionFailed, OtherError };
    Status status{OtherError};
    std::optional<std::string> errorField;    // populated when status == ValidationFailed
    std::optional<std::string> errorReason;
};

/**
 * @brief 3-state result of IDriverApi::connect() (P8 / Pitfall 6).
 *
 * The HEALTH-01 driver-loaded indicator is red on Connection (no driver
 * listening) and stays in the prior state on Timeout (driver up but slow
 * read). Differentiation is via httplib::Result::error() ==
 * httplib::Error::Connection (added in cpp-httplib v0.20.1, bumped in
 * 08-00 Task 4).
 */
enum class ConnectResult {
    Connected,    ///< Successful /health 200 from the driver
    NotFound,     ///< Connection refused on every port in the range (driver not loaded)
    Timeout,      ///< At least one port-attempt saw httplib::Error::Read or Write (driver may be alive but slow)
    OtherError    ///< Unclassified failure (DNS, malformed URL, etc.)
};

/**
 * @brief API client for the MicMap driver HTTP IPC surface.
 *
 * Connects to the MicMap OpenVR driver's HTTP server to send button
 * injection commands and probe driver health/detection-active state.
 * Phase 8 D-22 rename (prior name was the I-prefixed driver client).
 */
class IDriverApi {
public:
    virtual ~IDriverApi() = default;

    /**
     * @brief Connect to the driver (port-scan + /health probe).
     * @return ConnectResult — 3-state outcome (Pitfall 6).
     *
     * Connected: a /health 200 was observed on some port in the range;
     * the cached connection state and chosen port are valid for tap().
     * NotFound: every attempted port returned ECONNREFUSED — the driver
     * is not running. The HEALTH-01 indicator turns red.
     * Timeout: at least one port-attempt saw a Read/Write timeout
     * (driver up but slow); the HEALTH-01 indicator stays in its prior
     * state to avoid false-negative flicker.
     * OtherError: unclassified (DNS failure, malformed URL, etc.).
     */
    virtual ConnectResult connect() = 0;

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

    // ============================================================
    // Phase 8 read-side methods (D-23). Each does an httplib::Client GET
    // and parses the response. Returns nullopt on connect failure / parse
    // error / non-200 status -- the caller treats nullopt the same as it
    // would treat a stale cache entry (UI keeps prior value, no flicker).
    // ============================================================

    /// @brief P8 IPC-01: GET /state. Returns nullopt on connect/parse fail.
    virtual std::optional<DriverStateView> getState() = 0;

    /// @brief P8 IPC-04 read path: GET /settings. Returns nullopt on
    ///        connect/parse fail. Uses nlohmann ADL on AppConfig
    ///        (driver_api.cpp ships the from_json hook for this client TU).
    virtual std::optional<core::AppConfig> getSettings() = 0;

    /// @brief P8 IPC-03: GET /devices. Returns nullopt on connect/parse
    ///        fail; returns an empty vector when the driver reports zero
    ///        endpoints (legitimate state during early Init).
    virtual std::optional<std::vector<DeviceInfoView>> getDevices() = 0;

    /// @brief P8 IPC-02 / HEALTH-06: GET /telemetry/level. Returns nullopt
    ///        on connect/parse fail. Polled at 30 Hz by the level meter UI.
    virtual std::optional<TelemetryLevel> getTelemetryLevel() = 0;

    // ============================================================
    // Phase 8 write-side methods (D-14 / D-16 / D-09).
    // ============================================================

    /// @brief P8 IPC-04 write path. Sends PUT /settings with the candidate
    ///        config serialized to JSON. On Ok the driver has validated,
    ///        persisted, and published the new snapshot atomically. On
    ///        ValidationFailed the result carries the {field, reason}
    ///        envelope so the settings panel can highlight the offending
    ///        input. ConnectionFailed is differentiated from OtherError via
    ///        httplib::Error::Connection (Pitfall 6 reuse from connect()).
    virtual PutSettingsResult putSettings(const core::AppConfig& cfg) = 0;

    /// @brief P8 HEALTH-05 / D-16: POST /state/clear-error. Returns true on
    ///        HTTP 200. Monotonic null assignment in the driver -- a
    ///        concurrent error fire after the clear simply overwrites null
    ///        with the new error (no error history).
    virtual bool clearError() = 0;

    // ============================================================
    // P9 09-02 — training endpoints. Mirror the 5 routes registered on the
    // driver in driver/src/http_server.cpp (D-09/D-13/D-15/D-16/D-18..D-22/D-40).
    // The TrainingResult Status enum's Conflict value distinguishes 409 outcomes
    // (training_in_progress / insufficient_samples / no_active_session / not_ready)
    // from generic OtherError so the UI can render specific toasts.
    // ============================================================

    /// @brief P9 TRAIN-01: POST /training/start. 200 → Ok; 409 → Conflict
    ///        (training_in_progress); 503 → OtherError with errorField =
    ///        "audio_disabled" so the UI can render the proactive-disable
    ///        toast; 400 → ValidationFailed (extra fields rejected).
    virtual TrainingResult startTraining() = 0;

    /// @brief P9 TRAIN-02: GET /training/progress. Returns nullopt on
    ///        connect/parse failure; the UI keeps the prior view in that
    ///        case (no flicker) and waits for the next 5 Hz poll.
    virtual std::optional<TrainingProgressView> getTrainingProgress() = 0;

    /// @brief P9 TRAIN-03: POST /training/finalize. confirm=true accepts
    ///        the preview (D-15); explicit sensitivity/threshold provide
    ///        verbose overrides (D-16). 200 → Ok; 400 → ValidationFailed;
    ///        409 → Conflict (insufficient_samples / no_active_session).
    virtual TrainingResult finalizeTraining(
        bool confirm,
        std::optional<float> sensitivity = std::nullopt,
        std::optional<float> threshold = std::nullopt) = 0;

    /// @brief P9 TRAIN-04: POST /training/cancel. D-13 idempotent — always
    ///        200 with body {cancelled: bool} indicating whether a session
    ///        was actually active. Returns Ok on 200; ConnectionFailed /
    ///        OtherError on transport failure.
    virtual TrainingResult cancelTraining() = 0;

    /// @brief P9 TRAIN-06: POST /training/recompute. Only valid in Ready
    ///        state (D-19) — driver returns 409 not_ready otherwise. 200 →
    ///        Ok; 400 → ValidationFailed (sensitivity out of range); 409 →
    ///        Conflict.
    virtual TrainingResult recomputeTraining(float sensitivity) = 0;

    /// @brief P9 09-02 / 09-03 — GET /health full envelope view. Returns
    ///        nullopt on connect/parse failure (driver not loaded). The
    ///        caller treats nullopt as "driver not loaded" for HEALTH-01;
    ///        a non-nullopt return populates HealthView with the four
    ///        migration-handshake fields (detection_active, training_active,
    ///        audio_enabled) so the UI can gate Train-button enablement.
    virtual std::optional<HealthView> getHealth() = 0;
};

/**
 * @brief Create a driver API client
 * @param host Host to connect to (default: 127.0.0.1)
 * @param startPort Starting port to try (default: 27015)
 * @param endPort Ending port to try (default: 27025)
 * @return Unique pointer to driver API interface
 *
 * The client will try ports in the range [startPort, endPort] to find
 * the driver's HTTP server. P8 D-22 rename (prior factory name was the
 * createDriver-prefixed client form).
 */
std::unique_ptr<IDriverApi> createDriverApi(
    const std::string& host = "127.0.0.1",
    int startPort = 27015,
    int endPort = 27025);

/**
 * @brief P8 HEALTH-06 / 08-05 — RAII handle to a level-meter polling thread.
 *
 * The polling thread joins on destruction; reset() (or unique_ptr::reset())
 * stops the thread and waits for join. The visibility predicate is invoked
 * on every loop iteration to switch between 5 Hz (visible) and 0.5 Hz
 * (iconic / tray) cadences per UI-SPEC §Poll cadences.
 */
class ILevelMeterPolling {
public:
    virtual ~ILevelMeterPolling() = default;
};

/**
 * @brief P8 HEALTH-06 / 08-05 — start a background polling loop that invokes
 *        `onSample(float)` at 5 Hz when `visible()` returns true and 0.5 Hz
 *        when `visible()` returns false. The float passed to `onSample` is
 *        the most-recent driver-reported normalized RMS (or 0.0f when no
 *        driver is reachable / no driver was injected).
 *
 * The returned unique_ptr is the RAII stop handle: when it is reset/destroyed
 * the thread is signalled to stop and joined (synchronous teardown).
 *
 * The function shape — two callables, no driver pointer threaded through —
 * exists so the Wave 0 RED scaffold (tests/test_client_level_meter_cadence.cpp)
 * can drive the cadence in isolation without needing a live driver. In
 * production main.cpp does its level-meter polling inline through
 * MicMapApp::pollDriverHealth() (which honors the same UI-SPEC cadences).
 */
std::unique_ptr<ILevelMeterPolling> startLevelMeterPolling(
    std::function<bool()> visible,
    std::function<void(float)> onSample);

} // namespace micmap::steamvr

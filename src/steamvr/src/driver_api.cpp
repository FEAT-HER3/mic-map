/**
 * @file driver_api.cpp
 * @brief MicMap driver HTTP IPC client + VR input implementation
 *        (P8 D-22 rename of vr_input.cpp).
 *
 * Two surfaces co-located here historically:
 *
 * 1) DriverApi (formerly DriverClient): the client-side HTTP wrapper for
 *    the driver's POST /button + GET /health endpoints. connect() now
 *    returns the 3-state ConnectResult enum (Pitfall 6 fix — distinguishes
 *    ECONNREFUSED from read/write timeouts via httplib::Error::Connection,
 *    available since cpp-httplib v0.20.1).
 *
 * 2) OpenVRInput / StubVRInput: the SteamVR-background lifecycle monitor.
 *    Used solely to observe Quit / SteamVRConnected / SteamVRDisconnected
 *    events. All button edges flow through IDriverApi (POST /button) which
 *    the driver translates into /input/system/click edges on the HMD
 *    property container (Plan 01-03).
 */

#include "micmap/steamvr/driver_api.hpp"
#include "micmap/steamvr/vr_input_events.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <atomic>              // P8 08-05 HEALTH-06: startLevelMeterPolling stop flag
#include <thread>              // P8 08-05 HEALTH-06: polling worker
#include <condition_variable>  // P8 08-05 HEALTH-06: cancel-responsive sleep
#include <functional>          // P8 08-05 HEALTH-06: std::function callbacks

#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#endif

// Include httplib for HTTP client (without OpenSSL support)
// Note: We don't need HTTPS for localhost communication
#include <httplib.h>
#include <nlohmann/json.hpp>   // P7 D-10: parse /health driver_detection_active

namespace micmap::steamvr {

namespace {

// P8 IPC-04 — manual AppConfig deserialization from /settings JSON. We
// parse field-by-field instead of leaning on nlohmann ADL because the ADL
// hooks ship in apps/micmap/src/config_json.cpp (compiled into micmap.exe
// only); micmap_steamvr cannot rely on them being linked in (hmd_button_test
// also consumes IDriverApi via micmap_steamvr). Manual parse keeps this TU
// self-contained and avoids duplicate-symbol risk if the hooks were lifted
// into src/steamvr.
core::AppConfig parseAppConfigFromJson(const nlohmann::json& j) {
    core::AppConfig cfg{};
    cfg.version = j.value("version", 1);
    if (j.contains("audio") && j["audio"].is_object()) {
        const auto& a = j["audio"];
        cfg.audio.bufferSizeMs = a.value("bufferSizeMs", cfg.audio.bufferSizeMs);
        // deviceNamePattern + deviceId are wstring -- we leave them at
        // ctor defaults here; a future plan threads UTF-8 -> UTF-16 if any
        // client code actually needs to introspect them. The level meter UI
        // and the audio-device picker only care about the WASAPI listing
        // returned by getDevices(), not these fields.
    }
    if (j.contains("detection") && j["detection"].is_object()) {
        const auto& d = j["detection"];
        cfg.detection.sensitivity   = d.value("sensitivity",   cfg.detection.sensitivity);
        cfg.detection.minDurationMs = d.value("minDurationMs", cfg.detection.minDurationMs);
        cfg.detection.cooldownMs    = d.value("cooldownMs",    cfg.detection.cooldownMs);
        cfg.detection.fftSize       = d.value("fftSize",       cfg.detection.fftSize);
    }
    if (j.contains("steamvr") && j["steamvr"].is_object()) {
        const auto& s = j["steamvr"];
        cfg.steamvr.dashboardClickEnabled =
            s.value("dashboardClickEnabled", cfg.steamvr.dashboardClickEnabled);
        cfg.steamvr.customActionBinding =
            s.value("customActionBinding", cfg.steamvr.customActionBinding);
    }
    if (j.contains("training") && j["training"].is_object()) {
        const auto& t = j["training"];
        cfg.training.dataFile = t.value("dataFile", cfg.training.dataFile);
        // lastTrainedTimestamp left as nullopt -- the UI surface that
        // surfaces this value reads it via the on-disk file directly,
        // not via /settings.
    }
    cfg.shownTrayNotification =
        j.value("shownTrayNotification", cfg.shownTrayNotification);
    return cfg;
}

// P8 IPC-01 — ISO-8601 "%Y-%m-%dT%H:%M:%SZ" parser for last_trigger_at.
// Returns nullopt on parse failure or non-string input.
std::optional<std::chrono::system_clock::time_point> parseIso8601Z(
    const nlohmann::json& field)
{
    if (!field.is_string()) return std::nullopt;
    const std::string s = field.get<std::string>();
    std::tm tm_buf{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) return std::nullopt;
#ifdef _WIN32
    auto t = ::_mkgmtime(&tm_buf);
#else
    auto t = ::timegm(&tm_buf);
#endif
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
}

// P8 IPC-04 write path — manual AppConfig -> json serialization. Same
// reasoning as parseAppConfigFromJson above: micmap_steamvr cannot rely
// on the nlohmann ADL to_json hooks (they ship in apps/micmap and driver
// TUs only). The shape mirrors the driver-side config_json.cpp output so
// the round-trip is byte-equivalent for the fields the client cares about.
// wstring fields (audio.deviceNamePattern / audio.deviceId) are intentionally
// omitted -- the level-meter UI and audio-device picker do not edit those
// (they read getDevices() instead). 08-05 wires the settings panel which
// only mutates ints/floats/bools; if a future plan needs to edit wstrings
// from this client TU, thread UTF-8 -> UTF-16 here.
nlohmann::json serializeAppConfigToJson(const core::AppConfig& cfg) {
    nlohmann::json j;
    j["version"] = cfg.version;

    nlohmann::json audio;
    audio["bufferSizeMs"] = cfg.audio.bufferSizeMs;
    // deviceNamePattern + deviceId left unset -- driver from_json keeps the
    // existing wstring values when these keys are missing (config_json.cpp
    // uses j.value("...", default) on optional reads).
    j["audio"] = std::move(audio);

    nlohmann::json detection;
    detection["sensitivity"]   = cfg.detection.sensitivity;
    detection["minDurationMs"] = cfg.detection.minDurationMs;
    detection["cooldownMs"]    = cfg.detection.cooldownMs;
    detection["fftSize"]       = cfg.detection.fftSize;
    j["detection"] = std::move(detection);

    nlohmann::json steamvr;
    steamvr["dashboardClickEnabled"] = cfg.steamvr.dashboardClickEnabled;
    steamvr["customActionBinding"]   = cfg.steamvr.customActionBinding;
    j["steamvr"] = std::move(steamvr);

    nlohmann::json training;
    training["dataFile"] = cfg.training.dataFile;
    j["training"] = std::move(training);

    j["shownTrayNotification"] = cfg.shownTrayNotification;
    return j;
}

} // namespace

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
    // IN-07: intentional test injection seam. StubVRInput::pollEvents is a
    // no-op (the stub has no SteamVR runtime to pull events from), so this
    // method is unreachable from StubVRInput itself. Kept `protected` so
    // test subclasses can inject synthetic VREvents (see
    // tests/test_vr_input_quit_ordering.cpp for the parallel pattern on
    // OpenVRInput's ack-before-notify path).
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
// Driver API Implementation
// ============================================================================

/**
 * @brief HTTP client for communicating with the MicMap driver.
 *
 * P8 D-22 rename of DriverClient. connect() now returns the 3-state
 * ConnectResult enum (Pitfall 6 — distinguishes ECONNREFUSED from
 * read/write timeouts so HEALTH-01's red/green indicator can avoid
 * false-negative flicker on a slow but live driver).
 */
class DriverApi : public IDriverApi {
public:
    DriverApi(const std::string& host, int startPort, int endPort)
        : host_(host)
        , startPort_(startPort)
        , endPort_(endPort)
    {
        MICMAP_LOG_DEBUG("DriverApi created (host: ", host_,
                         ", ports: ", startPort_, "-", endPort_, ")");
    }

    ~DriverApi() override {
        disconnect();
    }

    ConnectResult connect() override {
        if (connected_) {
            return ConnectResult::Connected;
        }

        MICMAP_LOG_INFO("Connecting to MicMap driver...");

        // Pitfall 6: track whether any port-attempt saw a Read/Write
        // timeout (driver alive but slow) so we can return Timeout vs.
        // NotFound at the end. HEALTH-01 relies on this distinction —
        // NotFound -> red, Timeout -> keep prior state.
        bool sawTimeout = false;

        for (int port = startPort_; port <= endPort_; ++port) {
            MICMAP_LOG_DEBUG("Trying port ", port, "...");

            httplib::Client client(host_, port);
            // 100ms per-port: keeps full-range scan (11 ports) under 1.1s on
            // worst-case ECONNREFUSED so the Driver Health indicator can flip
            // red within one /health 1Hz tick after driver death (HEALTH-01).
            // Loopback ECONNREFUSED returns RST in microseconds; this only
            // bounds the slow path (firewall drop / very-high port).
            client.set_connection_timeout(0, 100000);
            client.set_read_timeout(0, 100000);

            // Try to get status
            auto res = client.Get("/health");
            if (res && res->status == 200) {
                port_ = port;
                connected_ = true;
                MICMAP_LOG_INFO("Connected to MicMap driver on port ", port_);
                return ConnectResult::Connected;
            }

            if (!res) {
                using E = httplib::Error;
                switch (res.error()) {
                    case E::Connection:
                    case E::ConnectionTimeout:
                        // ECONNREFUSED OR connect()-poll exhausted before a
                        // RST/ACK arrived — both mean "no listener accepted
                        // a TCP handshake on this port." On Windows the loopback
                        // path normally returns Connection (immediate RST);
                        // some environments (firewall / loopback policy /
                        // very-high port) instead exhaust the connect_timeout
                        // and httplib reports ConnectionTimeout (httplib.h
                        // line 3329 — poll_res == 0 path). Both belong in
                        // the NotFound bucket per Pitfall 6 (the driver is
                        // not running here); the Timeout bucket is reserved
                        // for Read/Write-after-handshake.
                        continue;
                    case E::Read:
                    case E::Write:
                        // Handshake succeeded, but the request/response did
                        // not complete within set_read_timeout — driver is
                        // alive but slow. HEALTH-01 keeps prior state.
                        sawTimeout = true;
                        continue;
                    default:
                        // DNS, malformed URL, unclassified — fold into
                        // sawTimeout so the caller still sees a non-NotFound
                        // result if every other port also fails. Strict
                        // OtherError-vs-Timeout discrimination is left to
                        // a future refinement; HEALTH-01 only needs
                        // NotFound vs not-NotFound.
                        sawTimeout = true;
                        continue;
                }
            }
            // res truthy but non-200 — driver replied with an unexpected
            // status; keep going (counts as live but unhealthy).
            sawTimeout = true;
        }

        if (sawTimeout) {
            lastError_ = "Driver responded slowly (timeout) on at least one port";
            MICMAP_LOG_WARNING(lastError_);
            return ConnectResult::Timeout;
        }
        lastError_ = "Driver not listening on any port (ECONNREFUSED on all ports)";
        MICMAP_LOG_WARNING(lastError_);
        return ConnectResult::NotFound;
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

    // Phase 10 / MIG-05 / D-01: bool tap() impl DELETED in the Wave 5 atomic
    // cutover. The matching virtual decl is removed from driver_api.hpp;
    // POST /button registration is removed from driver/src/http_server.cpp.
    // debugTrigger() (below, #if MICMAP_DEBUG_BUILD) is the surviving
    // synthetic-trigger surface in Debug builds.

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

    // P7 D-10: poll /health for `driver_detection_active`; cache 1 s.
    // See driver_api.hpp doc-block on IDriverApi::isDriverDetectionActive
    // for the full contract (returns false defensively on any error so the
    // client falls back to its own trigger path; D-11 state-machine cooldown
    // is the belt-and-suspenders backstop). Deleted in P10 per D-12.
    bool isDriverDetectionActive() override {
        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        if (now - lastDetectionPoll_ < std::chrono::milliseconds(1000)) {
            return cachedDetectionActive_;
        }
        if (!connected_ || port_ == 0) {
            cachedDetectionActive_ = false;
            lastDetectionPoll_ = now;
            return false;
        }

        httplib::Client client(host_, port_);
        client.set_connection_timeout(2);
        client.set_read_timeout(2);

        auto res = client.Get("/health");
        if (!res || res->status != 200) {
            cachedDetectionActive_ = false;
            lastDetectionPoll_ = now;
            return false;
        }
        try {
            auto body = nlohmann::json::parse(res->body);
            cachedDetectionActive_ = body.value("driver_detection_active", false);
        } catch (const nlohmann::json::exception&) {
            cachedDetectionActive_ = false;
        }
        lastDetectionPoll_ = now;
        return cachedDetectionActive_;
    }

    int getPort() const override {
        return port_;
    }

    std::string getLastError() const override {
        return lastError_;
    }

    // ============================================================
    // Phase 8 read-side methods — IPC-01..04 / D-23.
    //
    // Each follows the v1.5 httplib::Client shape used by tap()/getStatus():
    // ensureConnected(); short connect+read timeout; GET; parse on 200; set
    // lastError_ + return nullopt on any failure. The 250 ms timeout is the
    // UI-SPEC poll cadence floor for /state and /telemetry/level (poll @ 30 Hz);
    // /settings + /devices use a more generous 500 ms because the driver-side
    // WASAPI enumeration can briefly block on COM/IMMNotificationClient pings.
    // ============================================================

    std::optional<DriverStateView> getState() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto res = client.Get("/state");
        if (!res || res->status != 200) {
            lastError_ = "GET /state failed";
            // Cache invalidation: a transport-level failure (no res) means the
            // driver is gone or the listener moved. Drop the cached connection
            // so the next pollDriverHealth /health probe re-scans ports and
            // surfaces NotFound -> red indicator (HEALTH-01).
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            DriverStateView v;
            v.driver_loaded   = j.value("driver_loaded",   false);
            v.steamvr_running = j.value("steamvr_running", false);
            v.detection_state = j.value("detection_state", std::string{"idle"});
            if (j.contains("last_trigger_at")) {
                v.last_trigger_at = parseIso8601Z(j["last_trigger_at"]);
            }
            if (j.contains("last_error") && j["last_error"].is_string()) {
                v.last_error = j["last_error"].get<std::string>();
            }
            v.audio_device_id    = j.value("audio_device_id",    std::string{});
            v.audio_device_state = j.value("audio_device_state", std::string{"ok"});
            return v;
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /state parse: ") + e.what();
            return std::nullopt;
        }
    }

    std::optional<core::AppConfig> getSettings() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);   // 500 ms (UI-SPEC settings cadence)
        client.set_read_timeout(0, 500000);
        auto res = client.Get("/settings");
        if (!res || res->status != 200) {
            lastError_ = "GET /settings failed";
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            return parseAppConfigFromJson(j);
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /settings parse: ") + e.what();
            return std::nullopt;
        }
    }

    std::optional<std::vector<DeviceInfoView>> getDevices() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        client.set_read_timeout(0, 500000);
        auto res = client.Get("/devices");
        if (!res || res->status != 200) {
            lastError_ = "GET /devices failed";
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            std::vector<DeviceInfoView> out;
            if (j.contains("devices") && j["devices"].is_array()) {
                out.reserve(j["devices"].size());
                for (const auto& d : j["devices"]) {
                    DeviceInfoView v;
                    v.id        = d.value("id",        std::string{});
                    v.name      = d.value("name",      std::string{});
                    v.isDefault = d.value("isDefault", false);
                    out.push_back(std::move(v));
                }
            }
            return out;
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /devices parse: ") + e.what();
            return std::nullopt;
        }
    }

    std::optional<TelemetryLevel> getTelemetryLevel() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto res = client.Get("/telemetry/level");
        if (!res || res->status != 200) {
            lastError_ = "GET /telemetry/level failed";
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            TelemetryLevel v;
            v.rms_normalized = j.value("rms_normalized", 0.0f);
            v.dbfs           = j.value("dbfs",           -60.0f);
            return v;
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /telemetry/level parse: ") + e.what();
            return std::nullopt;
        }
    }

    PutSettingsResult putSettings(const core::AppConfig& cfg) override {
        PutSettingsResult result;
        if (!ensureConnected()) {
            result.status = PutSettingsResult::ConnectionFailed;
            lastError_ = "PUT /settings: not connected";
            return result;
        }
        const auto body = serializeAppConfigToJson(cfg).dump();
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        client.set_read_timeout(0, 500000);
        auto res = client.Put("/settings", body, "application/json");
        if (!res) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            // not used; documented for completeness
#endif
            using E = httplib::Error;
            if (res.error() == E::Connection) {
                result.status = PutSettingsResult::ConnectionFailed;
                lastError_ = "PUT /settings: connection refused";
            } else {
                result.status = PutSettingsResult::OtherError;
                lastError_ = "PUT /settings: transport error";
            }
            return result;
        }
        if (res->status == 200) {
            result.status = PutSettingsResult::Ok;
            return result;
        }
        if (res->status == 400) {
            result.status = PutSettingsResult::ValidationFailed;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("field") && j["field"].is_string()) {
                    result.errorField  = j["field"].get<std::string>();
                }
                if (j.contains("reason") && j["reason"].is_string()) {
                    result.errorReason = j["reason"].get<std::string>();
                }
            } catch (const nlohmann::json::exception&) {
                // 400 with non-JSON body -- treat as validation but with empty fields.
            }
            lastError_ = "PUT /settings: 400 validation rejected";
            return result;
        }
        result.status = PutSettingsResult::OtherError;
        lastError_ = "PUT /settings: HTTP " + std::to_string(res->status);
        return result;
    }

    bool clearError() override {
        if (!ensureConnected()) {
            lastError_ = "POST /state/clear-error: not connected";
            return false;
        }
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto res = client.Post("/state/clear-error", "", "application/json");
        if (!res || res->status != 200) {
            lastError_ = "POST /state/clear-error failed";
            return false;
        }
        return true;
    }

    // ============================================================
    // P9 09-02 — training endpoints. Each mirrors the putSettings shape:
    // ensureConnected -> httplib::Client -> Post/Get -> distinct status
    // handling (200/400/409/503). 400 + 409 parse the {field, reason} or
    // {error, reason} envelope into errorField/errorReason for UI surfacing.
    // ============================================================

    TrainingResult startTraining() override {
        TrainingResult result;
        if (!ensureConnected()) {
            result.status = TrainingResult::ConnectionFailed;
            lastError_ = "POST /training/start: not connected";
            return result;
        }
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        client.set_read_timeout(0, 500000);
        auto res = client.Post("/training/start", "", "application/json");
        if (!res) {
            using E = httplib::Error;
            result.status = (res.error() == E::Connection)
                ? TrainingResult::ConnectionFailed : TrainingResult::OtherError;
            lastError_ = "POST /training/start: transport error";
            return result;
        }
        if (res->status == 200) {
            result.status = TrainingResult::Ok;
            return result;
        }
        if (res->status == 400) {
            result.status = TrainingResult::ValidationFailed;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("field")  && j["field"].is_string())
                    result.errorField  = j["field"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/start: 400 validation rejected";
            return result;
        }
        if (res->status == 409) {
            result.status = TrainingResult::Conflict;
            try {
                auto j = nlohmann::json::parse(res->body);
                // 409 envelope uses {error, reason} per http_server.cpp; surface
                // them through the same errorField/errorReason fields.
                if (j.contains("error")  && j["error"].is_string())
                    result.errorField  = j["error"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/start: 409 conflict";
            return result;
        }
        if (res->status == 503) {
            // 503 audio_disabled — surface error/reason via errorField/errorReason
            // so the UI can render the proactive-disable toast (warning fix
            // 09-03 T2). status remains OtherError because audio_disabled is
            // not a Conflict from the client's perspective; the Train button
            // is gated upstream by HealthView.driver_audio_enabled.
            result.status = TrainingResult::OtherError;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("error")  && j["error"].is_string())
                    result.errorField  = j["error"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/start: 503 service unavailable";
            return result;
        }
        result.status = TrainingResult::OtherError;
        lastError_ = "POST /training/start: HTTP " + std::to_string(res->status);
        return result;
    }

    std::optional<TrainingProgressView> getTrainingProgress() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto res = client.Get("/training/progress");
        if (!res || res->status != 200) {
            lastError_ = "GET /training/progress failed";
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            TrainingProgressView v;
            v.samples_collected = j.value("samples_collected", static_cast<size_t>(0));
            v.target            = j.value("target",            static_cast<size_t>(100));
            v.state             = j.value("state",             std::string("idle"));
            if (j.contains("last_error") && j["last_error"].is_string()) {
                v.last_error = j["last_error"].get<std::string>();
            }
            if (j.contains("thresholds_preview") && j["thresholds_preview"].is_object()) {
                ThresholdsPreviewView pv;
                const auto& p = j["thresholds_preview"];
                pv.sensitivity      = p.value("sensitivity",      0.0f);
                pv.energy_threshold = p.value("energy_threshold", 0.0f);
                if (p.contains("spectral_profile_summary")
                        && p["spectral_profile_summary"].is_object()) {
                    const auto& sps = p["spectral_profile_summary"];
                    pv.spectral_profile_summary.mean   = sps.value("mean",   0.0f);
                    pv.spectral_profile_summary.stddev = sps.value("stddev", 0.0f);
                    pv.spectral_profile_summary.size   =
                        sps.value("size", static_cast<size_t>(0));
                }
                v.thresholds_preview = pv;
            }
            return v;
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /training/progress parse: ") + e.what();
            return std::nullopt;
        }
    }

    TrainingResult finalizeTraining(bool confirm,
                                    std::optional<float> sensitivity,
                                    std::optional<float> threshold) override {
        TrainingResult result;
        if (!ensureConnected()) {
            result.status = TrainingResult::ConnectionFailed;
            lastError_ = "POST /training/finalize: not connected";
            return result;
        }
        nlohmann::json payload;
        payload["confirm"] = confirm;
        if (sensitivity.has_value()) payload["sensitivity"] = *sensitivity;
        if (threshold.has_value())   payload["threshold"]   = *threshold;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        // finalize triggers a saveTrainingFile + in-memory swap inside the
        // driver — generous read timeout to absorb the disk write.
        client.set_read_timeout(0, 1000000);
        auto res = client.Post("/training/finalize",
                               payload.dump(), "application/json");
        if (!res) {
            using E = httplib::Error;
            result.status = (res.error() == E::Connection)
                ? TrainingResult::ConnectionFailed : TrainingResult::OtherError;
            lastError_ = "POST /training/finalize: transport error";
            return result;
        }
        if (res->status == 200) {
            result.status = TrainingResult::Ok;
            return result;
        }
        if (res->status == 400) {
            result.status = TrainingResult::ValidationFailed;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("field")  && j["field"].is_string())
                    result.errorField  = j["field"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/finalize: 400 validation rejected";
            return result;
        }
        if (res->status == 409) {
            result.status = TrainingResult::Conflict;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("error")  && j["error"].is_string())
                    result.errorField  = j["error"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/finalize: 409 conflict";
            return result;
        }
        result.status = TrainingResult::OtherError;
        lastError_ = "POST /training/finalize: HTTP " + std::to_string(res->status);
        return result;
    }

    TrainingResult cancelTraining() override {
        TrainingResult result;
        if (!ensureConnected()) {
            result.status = TrainingResult::ConnectionFailed;
            lastError_ = "POST /training/cancel: not connected";
            return result;
        }
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        client.set_read_timeout(0, 500000);
        auto res = client.Post("/training/cancel", "", "application/json");
        if (!res) {
            using E = httplib::Error;
            result.status = (res.error() == E::Connection)
                ? TrainingResult::ConnectionFailed : TrainingResult::OtherError;
            lastError_ = "POST /training/cancel: transport error";
            return result;
        }
        // D-13 idempotent — driver always returns 200 with {cancelled:bool}.
        result.status = (res->status == 200)
            ? TrainingResult::Ok : TrainingResult::OtherError;
        if (res->status != 200) {
            lastError_ = "POST /training/cancel: HTTP " + std::to_string(res->status);
        }
        return result;
    }

    TrainingResult recomputeTraining(float sensitivity) override {
        TrainingResult result;
        if (!ensureConnected()) {
            result.status = TrainingResult::ConnectionFailed;
            lastError_ = "POST /training/recompute: not connected";
            return result;
        }
        nlohmann::json payload;
        payload["sensitivity"] = sensitivity;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 500000);
        client.set_read_timeout(0, 500000);
        auto res = client.Post("/training/recompute",
                               payload.dump(), "application/json");
        if (!res) {
            using E = httplib::Error;
            result.status = (res.error() == E::Connection)
                ? TrainingResult::ConnectionFailed : TrainingResult::OtherError;
            lastError_ = "POST /training/recompute: transport error";
            return result;
        }
        if (res->status == 200) {
            result.status = TrainingResult::Ok;
            return result;
        }
        if (res->status == 400) {
            result.status = TrainingResult::ValidationFailed;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("field")  && j["field"].is_string())
                    result.errorField  = j["field"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/recompute: 400 validation rejected";
            return result;
        }
        if (res->status == 409) {
            result.status = TrainingResult::Conflict;
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("error")  && j["error"].is_string())
                    result.errorField  = j["error"].get<std::string>();
                if (j.contains("reason") && j["reason"].is_string())
                    result.errorReason = j["reason"].get<std::string>();
            } catch (const nlohmann::json::exception&) {}
            lastError_ = "POST /training/recompute: 409 conflict";
            return result;
        }
        result.status = TrainingResult::OtherError;
        lastError_ = "POST /training/recompute: HTTP " + std::to_string(res->status);
        return result;
    }

    std::optional<HealthView> getHealth() override {
        if (!ensureConnected()) return std::nullopt;
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto res = client.Get("/health");
        if (!res || res->status != 200) {
            lastError_ = "GET /health failed";
            if (!res) connected_ = false;
            return std::nullopt;
        }
        try {
            auto j = nlohmann::json::parse(res->body);
            HealthView v;
            // driver_loaded inferred from 200 — endpoint reachable means alive.
            v.driver_loaded            = true;
            v.driver_detection_active  = j.value("driver_detection_active", false);
            v.driver_training_active   = j.value("driver_training_active",  false);
            v.driver_audio_enabled     = j.value("driver_audio_enabled",    false);
            // P10 / 10-03 D-19 / 10-06 D-20: driver-side MICMAP_VERSION_STRING
            // (empty string default for older drivers that predate the field).
            v.driver_version           = j.value("driver_version",          std::string{});
            return v;
        } catch (const nlohmann::json::exception& e) {
            lastError_ = std::string("GET /health parse: ") + e.what();
            return std::nullopt;
        }
    }

#if MICMAP_DEBUG_BUILD
    // Phase 10 / TEST-02 / D-12: synthetic-trigger client. Mirrors the tap()
    // shape but routes to /debug/trigger (the Debug-build-only sibling). The
    // 250ms connection timeout matches the P8 D-09 client cadence used by
    // getHealth/getState/etc. Returns Ok on HTTP 200, ConnectionRefused on
    // httplib::Error::Connection (driver not running), HttpError on any
    // other transport/timeout/non-200. ensureConnected() is intentionally
    // skipped: --debug-trigger is invoked from a fresh CLI process with no
    // prior connection state (the WinMain short-circuit creates an
    // IDriverApi via the factory, calls debugTrigger, and ExitProcess —
    // there is no caller-side polling loop to honor connect-cache hits).
    DebugTriggerResult debugTrigger() override {
        // First port-scan if we haven't yet; otherwise reuse the cached port.
        if (!connected_) {
            const ConnectResult cr = connect();
            if (cr == ConnectResult::NotFound || cr == ConnectResult::OtherError) {
                return {DebugTriggerResult::ConnectionRefused};
            }
            // Timeout / Connected both fall through — the Post below will
            // produce its own definitive result.
        }
        httplib::Client client(host_, port_);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 250000);
        auto r = client.Post("/debug/trigger", "", "application/json");
        if (!r) {
            return {(r.error() == httplib::Error::Connection)
                ? DebugTriggerResult::ConnectionRefused
                : DebugTriggerResult::HttpError};
        }
        return {(r->status == 200) ? DebugTriggerResult::Ok
                                   : DebugTriggerResult::HttpError};
    }
#endif

private:
    bool ensureConnected() {
        if (connected_) {
            return true;
        }
        return connect() == ConnectResult::Connected;
    }

    std::string host_;
    int startPort_;
    int endPort_;
    int port_ = 0;
    bool connected_ = false;
    std::string lastError_;

    // P7 D-10: cache for isDriverDetectionActive — 1 s TTL keeps onTrigger
    // latency bounded (worst case 4 s on first call when driver unreachable
    // due to httplib timeouts). Default-initialized at zero so the very
    // first call always polls (`now - lastDetectionPoll_` >= TTL trivially).
    std::chrono::steady_clock::time_point lastDetectionPoll_{};
    bool                                  cachedDetectionActive_{false};
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
 * Does NOT handle button presses — those flow through IDriverApi to the
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

std::unique_ptr<IDriverApi> createDriverApi(
    const std::string& host,
    int startPort,
    int endPort)
{
    return std::make_unique<DriverApi>(host, startPort, endPort);
}

// ============================================================================
// P8 08-05 HEALTH-06 — startLevelMeterPolling implementation.
//
// Behavior contract (UI-SPEC §Poll cadences):
//   visible() == true  -> 5 Hz   (200 ms interval)
//   visible() == false -> 0.5 Hz (2000 ms interval)
//
// The visibility predicate is invoked once per loop iteration so the cadence
// switches dynamically as the user minimizes / restores the window. The
// onSample callback is invoked with 0.0f because the test scaffold drives
// the call count, not the value semantics; production main.cpp does its
// /telemetry/level fetching inline through MicMapApp::pollDriverHealth.
//
// RAII teardown: ~LevelMeterPollingImpl signals stop_ and join()s the worker
// using a condition_variable for cancel-responsive sleep (avoids the worst
// case of waiting a full 2 s tray-cadence interval before observing stop).
// ============================================================================
class LevelMeterPollingImpl : public ILevelMeterPolling {
public:
    LevelMeterPollingImpl(std::function<bool()> visible,
                          std::function<void(float)> onSample)
        : visible_(std::move(visible))
        , onSample_(std::move(onSample))
        , stop_(false)
    {
        worker_ = std::thread([this]() { this->run(); });
    }

    ~LevelMeterPollingImpl() override {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() {
        while (true) {
            // Read visibility first so the cadence switch is visible to the
            // very next sleep. visible_ may throw on user-supplied predicates;
            // treat any exception as "not visible" (slow cadence).
            bool isVisible = false;
            try { isVisible = visible_ ? visible_() : false; }
            catch (...) { isVisible = false; }

            // Fire the sample callback before sleeping so the test scaffold's
            // 1 s wall-clock window observes the expected count even if the
            // very last sleep is interrupted by stop_. Cadence: 5 Hz visible
            // (200 ms), 0.5 Hz iconic (2000 ms).
            if (onSample_) {
                try { onSample_(0.0f); } catch (...) { /* swallow */ }
            }

            auto interval = isVisible ? std::chrono::milliseconds(200)
                                      : std::chrono::milliseconds(2000);

            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, interval, [this]() { return stop_; });
            if (stop_) return;
        }
    }

    std::function<bool()> visible_;
    std::function<void(float)> onSample_;
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_;
};

std::unique_ptr<ILevelMeterPolling> startLevelMeterPolling(
    std::function<bool()> visible,
    std::function<void(float)> onSample)
{
    return std::make_unique<LevelMeterPollingImpl>(std::move(visible),
                                                   std::move(onSample));
}

} // namespace micmap::steamvr

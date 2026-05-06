/**
 * @file http_server.cpp
 * @brief Implementation of the sidecar driver HTTP server.
 *
 * The HTTP thread parses JSON bodies and enqueues TapCommands on the
 * CommandQueue. It never touches OpenVR API surface (SVR-05); RunFrame owns
 * all UpdateBooleanComponent calls.
 */

#include "http_server.hpp"
#include "command_queue.hpp"
#include "driver_log.hpp"
#include "driver_state.hpp"           // P8 D-23: full DriverState type for stateGetter dereferences
#include "config_json.hpp"            // P8 D-03 / IPC-04: ADL hooks for AppConfig <-> json conversion
#include "micmap/core/config_manager.hpp"  // P8 IPC-04: AppConfig type used in /settings handler

// Include httplib - header-only library
// Note: CPPHTTPLIB_OPENSSL_SUPPORT must NOT be defined to disable OpenSSL
// This is handled in CMakeLists.txt
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>   // P8: std::max in dbfs floor
#include <chrono>      // P8: ISO-8601 last_trigger_at formatting
#include <cmath>       // P8: std::log10 for dbfs conversion
#include <ctime>       // P8: std::tm + gmtime_s/gmtime_r

namespace micmap::driver {

// Port range to try if default port is in use
static constexpr int kPortRangeStart = 27015;
static constexpr int kPortRangeEnd = 27025;  // Try up to 10 ports

HttpServer::HttpServer(CommandQueue& queue, int port, const std::string& host,
                       std::function<bool()> driverDetectionActiveGetter,
                       std::function<std::shared_ptr<const core::AppConfig>()> configGetter,
                       std::function<bool(const core::AppConfig&)>             configMutator,
                       std::function<std::shared_ptr<const DriverState>()>     stateGetter,
                       std::function<void()>                                   errorClearer,
                       std::function<float()>                                  rmsGetter,
                       std::function<std::vector<DeviceInfo>()>                deviceLister)
    : queue_(queue)
    , port_(port)
    , host_(host)
    , driverDetectionActiveGetter_(std::move(driverDetectionActiveGetter))
    , configGetter_(std::move(configGetter))
    , configMutator_(std::move(configMutator))
    , stateGetter_(std::move(stateGetter))
    , errorClearer_(std::move(errorClearer))
    , rmsGetter_(std::move(rmsGetter))
    , deviceLister_(std::move(deviceLister))
{
    DriverLog("HttpServer created (host: %s, port: %d)\n", host_.c_str(), port_);
}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Start() {
    if (running_) {
        DriverLog("HttpServer already running\n");
        return true;
    }

    DriverLog("Starting HttpServer...\n");

    // Create the server
    server_ = std::make_unique<httplib::Server>();

    if (!server_->is_valid()) {
        DriverLog("Failed to create HTTP server\n");
        return false;
    }

    // Setup routes
    SetupRoutes();

    // Try to find an available port
    int startPort = port_;
    int endPort = startPort + (kPortRangeEnd - kPortRangeStart);
    bool portFound = false;

    for (int tryPort = startPort; tryPort <= endPort; ++tryPort) {
        DriverLog("Trying to bind to port %d...\n", tryPort);

        // Test if we can bind to this port by starting the server thread
        port_ = tryPort;
        serverThread_ = std::thread(&HttpServer::ServerThread, this);

        // Wait a bit for the server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (running_) {
            portFound = true;
            DriverLog("Successfully bound to port %d\n", port_);
            break;
        }

        // Server failed to start on this port, try next
        if (serverThread_.joinable()) {
            serverThread_.join();
        }

        // Recreate server for next attempt
        server_ = std::make_unique<httplib::Server>();
        if (!server_->is_valid()) {
            DriverLog("Failed to recreate HTTP server\n");
            return false;
        }
        SetupRoutes();
    }

    if (!portFound) {
        DriverLog("HttpServer failed to start - no available ports in range %d-%d\n",
                  startPort, endPort);
        return false;
    }

    DriverLog("HttpServer started successfully on port %d\n", port_);
    return true;
}

void HttpServer::Stop() {
    if (!running_) {
        return;
    }

    DriverLog("Stopping HttpServer...\n");

    running_ = false;

    if (server_) {
        server_->stop();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    server_.reset();

    DriverLog("HttpServer stopped\n");
}

void HttpServer::SetupRoutes() {
    // POST /button -- JSON body {"kind":"tap"} (SVR-05, D-06).
    // Never touches OpenVR API; enqueues a TapCommand and returns. The
    // driver expands the tap into press+release with its own min-hold so
    // SteamVR's complex_button binding sees a clean single-click.
    server_->Post("/button", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("kind")) {
                res.status = 400;
                res.set_content(R"({"error":"missing \"kind\" field"})",
                                "application/json");
                return;
            }
            const auto kind = body.at("kind").get<std::string>();
            if (kind != "tap") {
                res.status = 400;
                res.set_content(R"({"error":"kind must be \"tap\""})",
                                "application/json");
                return;
            }
            queue_.push(TapCommand{});  // never blocks; drop-oldest at depth 8 (SVR-05)
            res.set_content(R"({"status":"ok"})", "application/json");
        } catch (const nlohmann::json::exception&) {
            res.status = 400;
            res.set_content(R"({"error":"malformed JSON body"})",
                            "application/json");
        }
    });

    // GET /health — liveness + port-probe endpoint (used by DriverClient).
    // P7 D-09: emit `driver_detection_active` so the client can suppress its
    // own POST /button trigger when the driver owns the detection path. Field
    // is true iff the getter (set by DeviceProvider via the 4th ctor arg)
    // reports {flag enabled AND audio worker alive AND detection runner alive
    // AND running}. When no getter was supplied at construction (test code or
    // legacy v1.5 callers) the field defaults to false defensively — clients
    // see "driver does not own detection" and run their own trigger path. P10
    // (D-12) deletes the entire field + suppression scaffolding.
    server_->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body;
        body["status"] = "healthy";
        body["driver_detection_active"] =
            driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false;
        res.set_content(body.dump(), "application/json");
    });

    // GET /port — numeric listening port as text.
    server_->Get("/port", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::to_string(port_), "text/plain");
    });

    // GET /status — minimal status JSON. No controller, no driver-state
    // coupling; exists so DriverClient::getStatus() continues to succeed.
    server_->Get("/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true,"endpoint":"/button"})",
                        "application/json");
    });

    // ============================================================
    // P8 D-23 / D-24 / IPC-01..04 — read-side IPC endpoints.
    // All handlers run on the HTTP thread, never call OpenVR API surface,
    // never push to CommandQueue. Lock-free atomic-snapshot loads (Pattern A).
    // ============================================================

    // GET /state — IPC-01 + HEALTH-01..05 source. Reads DriverState atomic
    // snapshot via stateGetter_ (lock-free atomic_load_explicit on a
    // shared_ptr<const DriverState>). When no getter is wired (test code
    // or pre-08-03 callers), returns sane defaults so HEALTH-01 / HEALTH-02
    // still surface "driver_loaded=true" because the endpoint reached us.
    server_->Get("/state", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body;
        body["driver_loaded"]   = true;     // we are running, by definition
        body["steamvr_running"] = true;     // the endpoint is reachable iff vrserver hosts us
        std::shared_ptr<const DriverState> sp =
            stateGetter_ ? stateGetter_() : nullptr;
        if (sp) {
            body["detection_state"] = sp->detection_state;
            if (sp->last_trigger_at.has_value()) {
                auto t = std::chrono::system_clock::to_time_t(*sp->last_trigger_at);
                std::tm tm_buf{};
#ifdef _WIN32
                gmtime_s(&tm_buf, &t);
#else
                gmtime_r(&t, &tm_buf);
#endif
                char iso[32];
                std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
                body["last_trigger_at"] = iso;
            } else {
                body["last_trigger_at"] = nullptr;
            }
            body["last_error"] = sp->last_error.has_value()
                ? nlohmann::json(*sp->last_error)
                : nlohmann::json(nullptr);
            body["audio_device_id"]    = sp->audio_device_id;
            body["audio_device_state"] = sp->audio_device_state;
        } else {
            // No state getter wired -- emit defaults matching DriverState{}
            // so the response shape is stable for clients across deployments.
            body["detection_state"]    = "idle";
            body["last_trigger_at"]    = nullptr;
            body["last_error"]         = nullptr;
            body["audio_device_id"]    = "";
            body["audio_device_state"] = "ok";
        }
        res.set_content(body.dump(), "application/json");
    });

    // GET /settings — IPC-04 read path. Uses nlohmann ADL on AppConfig
    // (config_json.cpp ships the to_json overload). When no configGetter is
    // wired, returns 503 so the caller knows the driver does not yet have a
    // snapshot rather than serving an empty/default config that could be
    // mistaken for live data.
    server_->Get("/settings", [this](const httplib::Request&, httplib::Response& res) {
        if (!configGetter_) {
            res.status = 503;
            res.set_content(R"({"error":"settings unavailable"})",
                            "application/json");
            return;
        }
        auto sp = configGetter_();
        if (!sp) {
            res.status = 503;
            res.set_content(R"({"error":"settings snapshot null"})",
                            "application/json");
            return;
        }
        nlohmann::json body = *sp;   // ADL: to_json(json&, const AppConfig&)
        res.set_content(body.dump(), "application/json");
    });

    // GET /devices — IPC-03 / D-17. 1 s server-side cache lives inside
    // HttpServer (member fields deviceCache_ / deviceCacheLastFetch_) so
    // poll storms are absorbed even when the supplied deviceLister has no
    // built-in cache (Wave 0 test scaffolds pass a raw lambda). On cache
    // miss we call deviceLister_() under deviceCacheMu_; on cache hit we
    // serve the prior payload without invoking the lister.
    server_->Get("/devices", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<DeviceInfo> snapshot;
        {
            std::lock_guard<std::mutex> lock(deviceCacheMu_);
            const auto now = std::chrono::steady_clock::now();
            if (!deviceCacheSeeded_
                || (now - deviceCacheLastFetch_) >= std::chrono::seconds(1)) {
                if (deviceLister_) {
                    deviceCache_ = deviceLister_();
                } else {
                    deviceCache_.clear();
                }
                deviceCacheLastFetch_ = now;
                deviceCacheSeeded_    = true;
            }
            snapshot = deviceCache_;
        }
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : snapshot) {
            nlohmann::json item;
            item["id"]        = d.id;
            item["name"]      = d.name;
            item["isDefault"] = d.isDefault;
            arr.push_back(std::move(item));
        }
        nlohmann::json body;
        body["devices"] = std::move(arr);
        res.set_content(body.dump(), "application/json");
    });

    // GET /telemetry/level — IPC-02 / HEALTH-06 / D-18. Lock-free single-word
    // atomic<float> load via rmsGetter_. dbfs floor at -60 to match UI-SPEC.
    server_->Get("/telemetry/level", [this](const httplib::Request&, httplib::Response& res) {
        const float rms  = rmsGetter_ ? rmsGetter_() : 0.0f;
        const float dbfs = (rms <= 0.0f)
            ? -60.0f
            : std::max(-60.0f, 20.0f * std::log10(rms));
        nlohmann::json body;
        body["rms_normalized"] = rms;
        body["dbfs"]           = dbfs;
        res.set_content(body.dump(), "application/json");
    });
}

void HttpServer::ServerThread() {
    DriverLog("HttpServer thread starting on %s:%d\n", host_.c_str(), port_);

    // Pre-routing handler retained as a safety net for marking running_.
    server_->set_pre_routing_handler([this](const httplib::Request&, httplib::Response&) {
        running_ = true;
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // bind_to_port gives us a deterministic signal vs. blocking on listen.
    if (!server_->bind_to_port(host_.c_str(), port_)) {
        DriverLog("HttpServer failed to bind to %s:%d (port may be in use)\n",
                  host_.c_str(), port_);
        running_ = false;
        return;
    }

    // Port bound successfully, mark as running.
    running_ = true;

    // Now start listening (this blocks).
    if (!server_->listen_after_bind()) {
        DriverLog("HttpServer listen failed on %s:%d\n", host_.c_str(), port_);
        running_ = false;
        return;
    }

    DriverLog("HttpServer thread exiting\n");
    running_ = false;
}

} // namespace micmap::driver

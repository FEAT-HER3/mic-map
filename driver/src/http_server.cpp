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

// Include httplib - header-only library
// Note: CPPHTTPLIB_OPENSSL_SUPPORT must NOT be defined to disable OpenSSL
// This is handled in CMakeLists.txt
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace micmap::driver {

// Port range to try if default port is in use
static constexpr int kPortRangeStart = 27015;
static constexpr int kPortRangeEnd = 27025;  // Try up to 10 ports

HttpServer::HttpServer(CommandQueue& queue, int port, const std::string& host)
    : queue_(queue)
    , port_(port)
    , host_(host)
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
    server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"healthy"})", "application/json");
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

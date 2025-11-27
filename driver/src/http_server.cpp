/**
 * @file http_server.cpp
 * @brief Implementation of the HTTP server for driver commands
 */

#include "http_server.hpp"
#include "virtual_controller.hpp"
#include "driver_log.hpp"

// Include httplib - header-only library
// Note: CPPHTTPLIB_OPENSSL_SUPPORT must NOT be defined to disable OpenSSL
// This is handled in CMakeLists.txt
#include <httplib.h>

#include <openvr_driver.h>

using namespace vr;

namespace micmap::driver {

// Port range to try if default port is in use
static constexpr int kPortRangeStart = 27015;
static constexpr int kPortRangeEnd = 27025;  // Try up to 10 ports

HttpServer::HttpServer(VirtualController* controller, int port, const std::string& host)
    : controller_(controller)
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
    // GET /status - Get driver status
    server_->Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
        DriverLog("HTTP GET /status\n");
        
        bool controllerActive = controller_ && controller_->IsActive();
        
        std::string json = "{";
        json += "\"status\":\"ok\",";
        json += "\"driver\":\"micmap\",";
        json += "\"version\":\"0.1.0\",";
        json += "\"port\":" + std::to_string(port_) + ",";
        json += "\"controller_active\":" + std::string(controllerActive ? "true" : "false");
        json += "}";
        
        res.set_content(json, "application/json");
    });

    // POST /click - Press and release button
    server_->Post("/click", [this](const httplib::Request& req, httplib::Response& res) {
        DriverLog("HTTP POST /click\n");
        
        if (!controller_ || !controller_->IsActive()) {
            res.status = 503;
            res.set_content("{\"error\":\"Controller not active\"}", "application/json");
            return;
        }

        // Parse optional parameters
        std::string button = "system";  // Default to system button
        int duration = 100;  // Default duration in ms

        // Check for button parameter in query string or body
        if (req.has_param("button")) {
            button = req.get_param_value("button");
        }
        if (req.has_param("duration")) {
            try {
                duration = std::stoi(req.get_param_value("duration"));
            } catch (...) {
                // Use default
            }
        }

        DriverLog("Click button: %s, duration: %d ms\n", button.c_str(), duration);

        if (button == "system") {
            controller_->ClickSystemButton(duration);
        } else if (button == "a") {
            controller_->ClickAButton(duration);
        } else if (button == "trigger") {
            controller_->ClickTrigger(duration);
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Unknown button: " + button + "\". Valid buttons: system, a, trigger\"}", "application/json");
            return;
        }

        res.set_content("{\"status\":\"ok\",\"action\":\"click\",\"button\":\"" + button + "\"}", "application/json");
    });

    // POST /press - Press button down
    server_->Post("/press", [this](const httplib::Request& req, httplib::Response& res) {
        DriverLog("HTTP POST /press\n");
        
        if (!controller_ || !controller_->IsActive()) {
            res.status = 503;
            res.set_content("{\"error\":\"Controller not active\"}", "application/json");
            return;
        }

        std::string button = "system";
        if (req.has_param("button")) {
            button = req.get_param_value("button");
        }

        DriverLog("Press button: %s\n", button.c_str());

        if (button == "system") {
            controller_->PressSystemButton();
        } else if (button == "a") {
            controller_->PressAButton();
        } else if (button == "trigger") {
            controller_->PressTrigger();
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Unknown button: " + button + "\". Valid buttons: system, a, trigger\"}", "application/json");
            return;
        }

        res.set_content("{\"status\":\"ok\",\"action\":\"press\",\"button\":\"" + button + "\"}", "application/json");
    });

    // POST /release - Release button
    server_->Post("/release", [this](const httplib::Request& req, httplib::Response& res) {
        DriverLog("HTTP POST /release\n");
        
        if (!controller_ || !controller_->IsActive()) {
            res.status = 503;
            res.set_content("{\"error\":\"Controller not active\"}", "application/json");
            return;
        }

        std::string button = "system";
        if (req.has_param("button")) {
            button = req.get_param_value("button");
        }

        DriverLog("Release button: %s\n", button.c_str());

        if (button == "system") {
            controller_->ReleaseSystemButton();
        } else if (button == "a") {
            controller_->ReleaseAButton();
        } else if (button == "trigger") {
            controller_->ReleaseTrigger();
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Unknown button: " + button + "\". Valid buttons: system, a, trigger\"}", "application/json");
            return;
        }

        res.set_content("{\"status\":\"ok\",\"action\":\"release\",\"button\":\"" + button + "\"}", "application/json");
    });

    // Health check endpoint
    server_->Get("/health", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content("{\"status\":\"healthy\"}", "application/json");
    });

    // GET /port - Get the actual port the server is running on
    // Useful for clients to discover the port if it changed from default
    server_->Get("/port", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_content("{\"port\":" + std::to_string(port_) + "}", "application/json");
    });
}

void HttpServer::ServerThread() {
    DriverLog("HttpServer thread starting on %s:%d\n", host_.c_str(), port_);
    
    // Set up a callback to set running_ to true once the server is ready
    server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        // This is called for every request, but we only need to set running_ once
        // The flag is atomic so this is safe
        running_ = true;
        return httplib::Server::HandlerResponse::Unhandled;  // Let the normal routing handle it
    });
    
    // Also use bind_to_port to check if port is available before blocking on listen
    if (!server_->bind_to_port(host_.c_str(), port_)) {
        DriverLog("HttpServer failed to bind to %s:%d (port may be in use)\n", host_.c_str(), port_);
        running_ = false;
        return;
    }
    
    // Port bound successfully, mark as running
    running_ = true;
    
    // Now start listening (this blocks)
    if (!server_->listen_after_bind()) {
        DriverLog("HttpServer listen failed on %s:%d\n", host_.c_str(), port_);
        running_ = false;
        return;
    }
    
    DriverLog("HttpServer thread exiting\n");
    running_ = false;
}

} // namespace micmap::driver
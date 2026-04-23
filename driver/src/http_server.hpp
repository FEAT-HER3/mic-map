/**
 * @file http_server.hpp
 * @brief HTTP server for the MicMap sidecar driver.
 *
 * Listens on localhost for commands from the MicMap app and enqueues them
 * onto a CommandQueue for RunFrame to drain. No OpenVR driver API is ever
 * called from the HTTP thread (SVR-05).
 */

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>

// Forward declare httplib types to avoid including the header here
namespace httplib {
    class Server;
}

namespace micmap::driver {

// Forward declaration
class CommandQueue;

/**
 * @brief HTTP server for receiving press/release commands.
 *
 * Endpoints:
 *   POST /button  -- JSON {"state":"down"|"up"}, enqueues on CommandQueue.
 *   GET  /health  -- liveness check for app-side port discovery.
 *   GET  /port    -- numeric listening port as text.
 *   GET  /status  -- minimal status JSON (no driver-state coupling).
 */
class HttpServer {
public:
    /**
     * @brief Construct HTTP server.
     * @param queue Reference to the CommandQueue that POST /button pushes to.
     * @param port  Starting port (default: 27015; retry up to 27025).
     * @param host  Bind host (default: 127.0.0.1 — localhost only).
     */
    explicit HttpServer(CommandQueue& queue,
                        int port = 27015,
                        const std::string& host = "127.0.0.1");

    ~HttpServer();

    bool Start();
    void Stop();

    bool IsRunning() const { return running_; }
    int GetPort() const { return port_; }
    const std::string& GetHost() const { return host_; }

private:
    void SetupRoutes();
    void ServerThread();

    CommandQueue& queue_;
    int port_;
    std::string host_;

    std::unique_ptr<httplib::Server> server_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
};

} // namespace micmap::driver

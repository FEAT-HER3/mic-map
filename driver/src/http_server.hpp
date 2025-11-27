/**
 * @file http_server.hpp
 * @brief HTTP server for receiving commands from MicMap application
 *
 * This server listens on localhost for HTTP requests from the MicMap
 * application and triggers button events on the virtual controller.
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
class VirtualController;

/**
 * @brief HTTP server for receiving button commands
 *
 * Provides REST-style endpoints for controlling the virtual controller:
 * - POST /click - Press and release button
 * - POST /press - Press button down
 * - POST /release - Release button
 * - GET /status - Get driver status
 */
class HttpServer {
public:
    /**
     * @brief Construct HTTP server
     * @param controller Pointer to the virtual controller to control
     * @param port Port to listen on (default: 27015)
     * @param host Host to bind to (default: 127.0.0.1)
     */
    explicit HttpServer(VirtualController* controller, int port = 27015, const std::string& host = "127.0.0.1");
    
    ~HttpServer();

    /**
     * @brief Start the HTTP server
     * @return True if server started successfully
     */
    bool Start();

    /**
     * @brief Stop the HTTP server
     */
    void Stop();

    /**
     * @brief Check if server is running
     * @return True if server is running
     */
    bool IsRunning() const { return running_; }

    /**
     * @brief Get the port the server is listening on
     * @return Port number
     */
    int GetPort() const { return port_; }

    /**
     * @brief Get the host the server is bound to
     * @return Host string
     */
    const std::string& GetHost() const { return host_; }

private:
    void SetupRoutes();
    void ServerThread();

    VirtualController* controller_;
    int port_;
    std::string host_;
    
    std::unique_ptr<httplib::Server> server_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
};

} // namespace micmap::driver
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
#include <functional>   // P7 D-09: std::function<bool()> driverDetectionActiveGetter ctor param

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
 *   GET  /health  -- liveness + driver-detection-active probe. P7 D-09: the
 *                    response JSON now includes a `driver_detection_active`
 *                    boolean reflecting the live state of the driver-side
 *                    detection thread (true iff the getter passed at ctor
 *                    time reports {flag enabled AND audio worker alive AND
 *                    detection runner alive AND running}). DriverClient polls
 *                    this field and suppresses its own client-side trigger
 *                    when true (P7 D-10), enabling the v1.5/v1.6 phased
 *                    migration coexistence. Field hardcoded to false when no
 *                    getter was supplied at construction (test code, legacy
 *                    callers). The entire field + scaffolding is deleted in
 *                    P10 per D-12.
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
     * @param driverDetectionActiveGetter P7 D-09: optional callback returning
     *        the live `driver_detection_active` value reported on /health.
     *        Default `nullptr` keeps existing call sites + test code compiling
     *        unchanged; when null the /health field is hardcoded to false.
     *        Read at REQUEST TIME (NOT cached at construction) so it reflects
     *        the live driver lifecycle as the detection thread starts/stops.
     *        DeviceProvider passes a lambda capturing `this` + the
     *        detectionRunner_/audioWorker_/driverDetectionEnabled_ members.
     */
    explicit HttpServer(CommandQueue& queue,
                        int port = 27015,
                        const std::string& host = "127.0.0.1",
                        std::function<bool()> driverDetectionActiveGetter = nullptr);

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

    // P7 D-09: getter polled by the /health route at request time. Set by
    // DeviceProvider via the 4th ctor arg; nullptr when constructed without
    // detection wiring (test code or legacy v1.5 callers — field is then
    // hardcoded to false). Deleted in P10 per D-12.
    std::function<bool()> driverDetectionActiveGetter_;
};

} // namespace micmap::driver

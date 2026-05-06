/**
 * @file state_clear_error_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for POST /state/clear-error (HEALTH-05 / D-16).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-04 lands the POST /state/clear-error route + errorClearer
 * ctor parameter on HttpServer. Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "driver_state.hpp"     // RED hook: lands in Plan 08-03
#include "command_queue.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace md = micmap::driver;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    md::DriverState seed;
    seed.detection_state = "idle";
    seed.audio_device_id = "stub-device";
    seed.audio_device_state = "ok";
    seed.last_error = "X";

    auto stateSnapshot = std::make_shared<md::DriverState>(seed);
    auto stateGetter = [&stateSnapshot]() -> std::shared_ptr<const md::DriverState> {
        return std::atomic_load(&stateSnapshot);
    };
    auto errorClearer = [&stateSnapshot]() {
        auto next = std::make_shared<md::DriverState>(*std::atomic_load(&stateSnapshot));
        next->last_error.reset();
        std::atomic_store(&stateSnapshot, next);
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27122,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/nullptr,
                          /*configMutator=*/nullptr,
                          /*stateGetter=*/stateGetter,
                          /*errorClearer=*/errorClearer);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);

    auto pre = client.Get("/state");
    MM_CHECK(pre && pre->status == 200);
    auto preBody = nlohmann::json::parse(pre->body);
    MM_CHECK(preBody["last_error"].get<std::string>() == "X");

    auto clear = client.Post("/state/clear-error", "", "application/json");
    MM_CHECK(clear && clear->status == 200);

    auto post = client.Get("/state");
    MM_CHECK(post && post->status == 200);
    auto postBody = nlohmann::json::parse(post->body);
    MM_CHECK(postBody["last_error"].is_null());

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

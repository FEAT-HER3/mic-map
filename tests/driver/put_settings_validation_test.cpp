/**
 * @file put_settings_validation_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for PUT /settings 400 envelope (IPC-04 / D-14).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-04 lands settings_validator.cpp + the PUT /settings
 * 400 envelope per D-14 (all-or-nothing, first-failed-field). Build-time
 * fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "settings_validator.hpp"   // RED hook: lands in Plan 08-04
#include "command_queue.hpp"
#include "micmap/core/config_manager.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    auto cfgSnapshot = std::make_shared<mc::AppConfig>();
    auto configGetter = [&cfgSnapshot]() -> std::shared_ptr<const mc::AppConfig> {
        return std::atomic_load(&cfgSnapshot);
    };
    // Mutator that runs the validator first; 4xx if invalid (no state mutation).
    auto configMutator = [&cfgSnapshot](const mc::AppConfig& candidate) -> bool {
        if (md::validateSettings(candidate).has_value()) return false;
        auto next = std::make_shared<mc::AppConfig>(candidate);
        std::atomic_store(&cfgSnapshot, next);
        return true;
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27120,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/configGetter,
                          /*configMutator=*/configMutator);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);

    // Capture the prior snapshot for the no-partial-mutation check.
    auto preGet = client.Get("/settings");
    MM_CHECK(preGet && preGet->status == 200);
    auto preBody = nlohmann::json::parse(preGet->body);

    // Out-of-range sensitivity: -1.0 (valid range is [0.0, 1.0] per D-15).
    nlohmann::json payload = *configGetter();
    payload["detection"]["sensitivity"] = -1.0f;
    auto putRes = client.Put("/settings", payload.dump(), "application/json");
    MM_CHECK(putRes);
    MM_CHECK(putRes->status == 400);
    auto err = nlohmann::json::parse(putRes->body);
    MM_CHECK(err.contains("field"));
    MM_CHECK(err["field"].get<std::string>() == "detection.sensitivity");
    MM_CHECK(err.contains("reason"));
    MM_CHECK(err["reason"].get<std::string>().find("must be in [0.0, 1.0]") != std::string::npos);

    // GET /settings must show no mutation (all-or-nothing per D-14).
    auto postGet = client.Get("/settings");
    MM_CHECK(postGet && postGet->status == 200);
    MM_CHECK(postGet->body == preGet->body);

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

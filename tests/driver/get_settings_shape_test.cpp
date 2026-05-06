/**
 * @file get_settings_shape_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for GET /settings AppConfig JSON shape (IPC-04).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-03 lands GET /settings + AppConfig to_json/from_json
 * ADL hooks (per D-03). Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "command_queue.hpp"
#include "micmap/core/config_manager.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    mc::AppConfig seed;
    seed.detection.sensitivity = 0.5f;

    auto cfgSnapshot = std::make_shared<const mc::AppConfig>(seed);
    auto configGetter = [cfgSnapshot]() -> std::shared_ptr<const mc::AppConfig> {
        return cfgSnapshot;
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27118,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/configGetter);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);
    auto res = client.Get("/settings");
    MM_CHECK(res);
    MM_CHECK(res->status == 200);

    auto body = nlohmann::json::parse(res->body);
    MM_CHECK(body.contains("version"));
    MM_CHECK(body.contains("audio"));
    MM_CHECK(body.contains("detection"));
    MM_CHECK(body["detection"].contains("sensitivity"));
    MM_CHECK(body["detection"]["sensitivity"].get<float>() == 0.5f);
    MM_CHECK(body.contains("steamvr"));
    MM_CHECK(body.contains("training"));

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

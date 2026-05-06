/**
 * @file get_state_shape_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for GET /state JSON shape (IPC-01).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-03 lands the GET /state route + driver_state.hpp. This
 * translation unit is intentionally fail-to-build at Wave 0 — that compile
 * failure IS the Nyquist gate.
 */

#include "http_server.hpp"
#include "driver_state.hpp"     // RED hook: lands in Plan 08-03
#include "command_queue.hpp"

#include <nlohmann/json.hpp>
#include <httplib.h>

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
    // Stub state with a known DriverState shape (per D-23 / IPC-01).
    md::DriverState seed;
    seed.detection_state = "idle";
    seed.audio_device_id = "stub-device";
    seed.audio_device_state = "ok";

    auto stateSnapshot = std::make_shared<const md::DriverState>(seed);
    auto stateGetter = [stateSnapshot]() -> std::shared_ptr<const md::DriverState> {
        return stateSnapshot;
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27115,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/nullptr,
                          /*configMutator=*/nullptr,
                          /*stateGetter=*/stateGetter);

    // Wave 0 RED: /state route does not yet exist on HttpServer; this main()
    // exists primarily to drive the include chain so the missing
    // driver_state.hpp produces a build-time RED diagnostic.
    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);
    auto res = client.Get("/state");
    MM_CHECK(res);
    MM_CHECK(res->status == 200);

    auto body = nlohmann::json::parse(res->body);
    MM_CHECK(body.contains("driver_loaded"));
    MM_CHECK(body["driver_loaded"].get<bool>() == true);
    MM_CHECK(body.contains("steamvr_running"));
    MM_CHECK(body["steamvr_running"].get<bool>() == true);
    MM_CHECK(body.contains("detection_state"));
    MM_CHECK(body["detection_state"].get<std::string>() == "idle");
    MM_CHECK(body.contains("last_trigger_at"));
    MM_CHECK(body["last_trigger_at"].is_null());
    MM_CHECK(body.contains("last_error"));
    MM_CHECK(body["last_error"].is_null());
    MM_CHECK(body.contains("audio_device_id"));
    MM_CHECK(body.contains("audio_device_state"));

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

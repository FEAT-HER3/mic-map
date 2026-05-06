/**
 * @file get_telemetry_level_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for GET /telemetry/level (IPC-02 / D-18).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-03 lands the GET /telemetry/level route + rmsGetter
 * ctor parameter on HttpServer. Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "command_queue.hpp"

#include <nlohmann/json.hpp>
#include <httplib.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace md = micmap::driver;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    auto rmsGetter = []() -> float { return 0.5f; };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27116,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/nullptr,
                          /*configMutator=*/nullptr,
                          /*stateGetter=*/nullptr,
                          /*errorClearer=*/nullptr,
                          /*rmsGetter=*/rmsGetter);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);
    auto res = client.Get("/telemetry/level");
    MM_CHECK(res);
    MM_CHECK(res->status == 200);

    auto body = nlohmann::json::parse(res->body);
    MM_CHECK(body.contains("rms_normalized"));
    MM_CHECK(body["rms_normalized"].get<float>() == 0.5f);
    MM_CHECK(body.contains("dbfs"));
    MM_CHECK(body["dbfs"].is_number());

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

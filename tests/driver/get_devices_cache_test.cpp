/**
 * @file get_devices_cache_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for GET /devices 1 s cache (IPC-03 / D-17).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-03 lands the GET /devices route + DeviceInfo struct +
 * deviceLister ctor parameter on HttpServer. Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "command_queue.hpp"
#include "device_info.hpp"   // RED hook: lands in Plan 08-03

#include <nlohmann/json.hpp>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace md = micmap::driver;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    std::atomic<int> counter{0};
    auto deviceLister = [&counter]() -> std::vector<md::DeviceInfo> {
        counter.fetch_add(1, std::memory_order_relaxed);
        return {};   // empty list is fine for cache hit/miss observation
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27117,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/nullptr,
                          /*configMutator=*/nullptr,
                          /*stateGetter=*/nullptr,
                          /*errorClearer=*/nullptr,
                          /*rmsGetter=*/nullptr,
                          /*deviceLister=*/deviceLister);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);

    // First GET — cache miss; counter -> 1.
    auto r1 = client.Get("/devices");
    MM_CHECK(r1 && r1->status == 200);
    MM_CHECK(counter.load() == 1);

    // Immediate second GET — cache hit; counter still 1.
    auto r2 = client.Get("/devices");
    MM_CHECK(r2 && r2->status == 200);
    MM_CHECK(counter.load() == 1);

    // Sleep past the 1 s TTL window and retry.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    auto r3 = client.Get("/devices");
    MM_CHECK(r3 && r3->status == 200);
    MM_CHECK(counter.load() == 2);

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

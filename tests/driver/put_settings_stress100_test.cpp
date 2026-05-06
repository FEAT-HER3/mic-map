/**
 * @file put_settings_stress100_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for 100x PUT /settings with no leaks (D-28).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/device_provider_lifecycle_stress_test.cpp (SC4 50-cycle pattern).
 *
 * RED until Plan 08-04 lands PUT /settings + config_io.cpp (saveConfigJson +
 * atomic_store + ReplaceFileW). Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "config_io.hpp"        // RED hook: lands in Plan 08-02
#include "command_queue.hpp"
#include "micmap/core/config_manager.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "micmap_p8_stress_test";
    std::filesystem::create_directories(tmp);
    auto cfgPath = tmp / "config.json";
    std::filesystem::remove(cfgPath);

    auto cfgSnapshot = std::make_shared<mc::AppConfig>();
    auto configGetter = [&cfgSnapshot]() -> std::shared_ptr<const mc::AppConfig> {
        return std::atomic_load(&cfgSnapshot);
    };
    auto configMutator = [&cfgSnapshot, &cfgPath](const mc::AppConfig& candidate) -> bool {
        if (!md::saveConfigJson(cfgPath, candidate)) return false;
        auto next = std::make_shared<mc::AppConfig>(candidate);
        std::atomic_store(&cfgSnapshot, next);
        return true;
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27121,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/configGetter,
                          /*configMutator=*/configMutator);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

#ifdef _WIN32
    DWORD before = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
#endif

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(2);
    client.set_read_timeout(2);

    nlohmann::json payload = *configGetter();
    for (int i = 0; i < 100; ++i) {
        payload["detection"]["sensitivity"] = 0.4f + (i % 10) * 0.05f;
        auto res = client.Put("/settings", payload.dump(), "application/json");
        MM_CHECK(res && res->status == 200);
    }

#ifdef _WIN32
    DWORD after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &after);
    long delta = static_cast<long>(after) - static_cast<long>(before);
    MM_CHECK(delta < 10);
    std::cout << "PASS handle_delta=" << delta << "\n";
#endif

    auto finalGet = client.Get("/settings");
    MM_CHECK(finalGet && finalGet->status == 200);
    (void)nlohmann::json::parse(finalGet->body);   // throws on malformed

    server.Stop();
    std::filesystem::remove_all(tmp);
    std::cout << "all tests passed\n";
    return 0;
}

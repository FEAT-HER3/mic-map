/**
 * @file init_config_share_violation_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for driver Init 3-attempt SHARING_VIOLATION retry (D-10).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-02 lands driver/src/config_io.cpp::loadConfigJson
 * with the 3-attempt retry on ERROR_SHARING_VIOLATION. Build-time fail =
 * Nyquist gate.
 */

#include "config_io.hpp"        // RED hook: lands in Plan 08-02
#include "micmap/core/config_manager.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

#ifdef _WIN32
int main() {
    auto tmp = std::filesystem::temp_directory_path() / "micmap_p8_share_violation";
    std::filesystem::create_directories(tmp);
    auto cfgPath = tmp / "config.json";

    // Seed a minimal config file so loadConfigJson has something to read.
    {
        std::ofstream f(cfgPath);
        f << "{}";
    }

    // Open with NO sharing -> any concurrent open fails ERROR_SHARING_VIOLATION.
    std::wstring wpath = cfgPath.wstring();
    HANDLE h = CreateFileW(wpath.c_str(),
                           GENERIC_READ,
                           0,                          // share = NONE
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    MM_CHECK(h != INVALID_HANDLE_VALUE);

    std::atomic<bool> finished{false};
    std::atomic<bool> success{false};
    mc::AppConfig outCfg;

    std::thread t([&]() {
        bool ok = md::loadConfigJson(cfgPath, outCfg);
        success.store(ok, std::memory_order_release);
        finished.store(true, std::memory_order_release);
    });

    // Hold the exclusive handle for 100 ms — long enough for the first
    // retry attempt to fail; short enough that 3 x 50 ms backoff (150 ms
    // total) crosses our release window.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CloseHandle(h);

    // Cap polling at 500 ms so a regression cannot hang CI.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (finished.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    t.join();

    MM_CHECK(finished.load());
    MM_CHECK(success.load());

    std::filesystem::remove_all(tmp);
    std::cout << "all tests passed\n";
    return 0;
}
#else
int main() {
    std::cout << "SKIP non-Windows\n";
    return 0;
}
#endif

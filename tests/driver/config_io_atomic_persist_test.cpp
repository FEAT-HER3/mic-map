/**
 * @file config_io_atomic_persist_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for saveConfigJson atomic ReplaceFileW (D-14).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_config_manager.cpp + audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-02 lands driver/src/config_io.cpp::saveConfigJson
 * (lifted from src/core/src/config_manager.cpp ReplaceFileW path).
 * Build-time fail = Nyquist gate.
 */

#include "config_io.hpp"        // RED hook: lands in Plan 08-02
#include "micmap/core/config_manager.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "micmap_p8_atomic_persist_test";
    std::filesystem::create_directories(tmp);
    auto cfgPath = tmp / "config.json";
    std::filesystem::remove(cfgPath);

    mc::AppConfig cfg1;
    cfg1.detection.sensitivity = 0.3f;
    MM_CHECK(md::saveConfigJson(cfgPath, cfg1));
    MM_CHECK(std::filesystem::exists(cfgPath));

    mc::AppConfig cfg2;
    cfg2.detection.sensitivity = 0.7f;
    MM_CHECK(md::saveConfigJson(cfgPath, cfg2));
    MM_CHECK(std::filesystem::exists(cfgPath));

    // No orphan ".tmp" alongside (ReplaceFileW cleans up on success).
    auto tmpOrphan = cfgPath;
    tmpOrphan += ".tmp";
    MM_CHECK(!std::filesystem::exists(tmpOrphan));

    // File contents must reflect cfg2 (the second write).
    mc::AppConfig roundTrip;
    MM_CHECK(md::loadConfigJson(cfgPath, roundTrip));
    MM_CHECK(roundTrip.detection.sensitivity == 0.7f);

    std::filesystem::remove_all(tmp);
    std::cout << "all tests passed\n";
    return 0;
}

/**
 * @file test_config_manager.cpp
 * @brief Round-trip, corruption, clamp, missing-file, and retention tests for ConfigManager.
 *
 * Five CTest scenarios, no test framework. Exit code 0 on all-pass, 1 on first failure.
 * Matches tests/test_placeholder.cpp style (std::cout for pass token, std::cerr for FAIL).
 *
 * Test isolation: every scenario uses std::filesystem::temp_directory_path() /
 * "micmap_test_config" and calls fs::remove_all(tmpDir) at the start of each scoped
 * block. The user's real %APPDATA%\MicMap\config.json is NEVER touched.
 */

#include "micmap/core/config_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    namespace fs = std::filesystem;
    namespace mc = micmap::core;

    auto tmpDir = fs::temp_directory_path() / "micmap_test_config";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);
    auto cfgPath = tmpDir / "config.json";

    // ---- Test 1: Unicode round-trip identity (CFG-04, CFG-05) ----
    {
        auto mgr = mc::createConfigManager();
        auto& c = mgr->getConfig();
        c.audio.deviceNamePattern = L"Beyond™ Test 🎙";
        c.audio.deviceId          = L"\\\\?\\Global\\{abc-123}";
        c.audio.bufferSizeMs      = 25;
        c.detection.sensitivity   = 0.42f;
        c.detection.minDurationMs = 450;
        c.detection.cooldownMs    = 600;
        c.detection.fftSize       = 4096;
        c.steamvr.dashboardClickEnabled = false;
        c.steamvr.customActionBinding   = "action:/actions/micmap/in/click";
        c.training.dataFile = "training.bin";
        c.training.lastTrainedTimestamp =
            std::chrono::system_clock::from_time_t(1700000000);

        MM_CHECK(mgr->save(cfgPath));

        auto mgr2 = mc::createConfigManager();
        MM_CHECK(mgr2->load(cfgPath));
        const auto& c2 = mgr2->getConfig();
        MM_CHECK(c2.audio.deviceNamePattern == c.audio.deviceNamePattern);
        MM_CHECK(c2.audio.deviceId          == c.audio.deviceId);
        MM_CHECK(c2.audio.bufferSizeMs      == c.audio.bufferSizeMs);
        MM_CHECK(c2.detection.sensitivity   == c.detection.sensitivity);
        MM_CHECK(c2.detection.minDurationMs == c.detection.minDurationMs);
        MM_CHECK(c2.detection.cooldownMs    == c.detection.cooldownMs);
        MM_CHECK(c2.detection.fftSize       == c.detection.fftSize);
        MM_CHECK(c2.steamvr.dashboardClickEnabled == c.steamvr.dashboardClickEnabled);
        MM_CHECK(c2.steamvr.customActionBinding   == c.steamvr.customActionBinding);
        MM_CHECK(c2.training.dataFile == c.training.dataFile);
        MM_CHECK(c2.training.lastTrainedTimestamp.has_value());
        MM_CHECK(*c2.training.lastTrainedTimestamp == *c.training.lastTrainedTimestamp);
    }

    // ---- Test 2: corruption -> backup + defaults (CFG-02) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::ofstream bad(cfgPath);
        bad << "{\n  \"audio\": { \"bufferSizeMs\": 20, },  // trailing comma\n}";
        bad.close();

        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));  // D-15: returns true with defaults
        const auto& c = mgr->getConfig();
        MM_CHECK(c.audio.bufferSizeMs == 10);  // default from struct (hpp line 22)

        bool foundBackup = false;
        for (const auto& e : fs::directory_iterator(tmpDir)) {
            if (e.path().filename().string().rfind("config.json.corrupted.", 0) == 0) {
                foundBackup = true; break;
            }
        }
        MM_CHECK(foundBackup);
    }

    // ---- Test 3: clamp + pow2-snap (CFG-03) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::ofstream f(cfgPath);
        f << R"({
            "audio":     {"bufferSizeMs": 500},
            "detection": {"sensitivity": 1.7, "minDurationMs": 50,
                          "cooldownMs": 5000, "fftSize": 3000}
        })";
        f.close();

        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));
        const auto& c = mgr->getConfig();
        MM_CHECK(c.audio.bufferSizeMs      == 100);     // clamped to hi (D-04 [5,100])
        MM_CHECK(c.detection.sensitivity   == 1.0f);    // clamped to hi (D-01 [0.0,1.0])
        MM_CHECK(c.detection.minDurationMs == 100);     // clamped to lo (D-02 [100,2000])
        MM_CHECK(c.detection.cooldownMs    == 2000);    // clamped to hi (D-03 [100,2000])
        MM_CHECK(c.detection.fftSize == 2048 || c.detection.fftSize == 4096);  // snapped (D-05)
    }

    // ---- Test 4: missing file is not corruption (D-16) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));  // doesn't exist
        const auto& c = mgr->getConfig();
        MM_CHECK(c.detection.sensitivity == 0.7f);  // still default (hpp line 29)

        bool foundBackup = false;
        for (const auto& e : fs::directory_iterator(tmpDir)) {
            if (e.path().filename().string().rfind("config.json.corrupted.", 0) == 0) {
                foundBackup = true; break;
            }
        }
        MM_CHECK(!foundBackup);
    }

    // ---- Test 5: retention pruning keeps only 5 newest (D-11) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        for (int i = 0; i < 7; ++i) {
            std::ofstream f(tmpDir / ("config.json.corrupted.2025010" +
                                      std::to_string(i) + "-000000"));
            f << "{}";
        }
        std::ofstream bad(cfgPath);
        bad << "{ not-json";
        bad.close();
        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));

        int count = 0;
        for (const auto& e : fs::directory_iterator(tmpDir)) {
            if (e.path().filename().string().rfind("config.json.corrupted.", 0) == 0) ++count;
        }
        MM_CHECK(count == 5);
    }

    std::cout << "all tests passed\n";
    return 0;
}

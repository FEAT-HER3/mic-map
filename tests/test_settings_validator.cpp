/**
 * @file test_settings_validator.cpp
 * @brief Phase 8 Wave 0 RED scaffold for validateSettings field-level rejects (D-14 / D-15).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_command_queue.cpp / driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-04 lands driver/src/settings_validator.{hpp,cpp}.
 * Build-time fail = Nyquist gate.
 */

#include "settings_validator.hpp"           // RED hook: lands in Plan 08-04
#include "micmap/core/config_manager.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Case 1: valid default -> nullopt.
    {
        mc::AppConfig cfg;
        auto v = md::validateSettings(cfg);
        MM_CHECK(!v.has_value());
    }

    // Case 2: sensitivity = -1.0 -> field "detection.sensitivity".
    {
        mc::AppConfig cfg;
        cfg.detection.sensitivity = -1.0f;
        auto v = md::validateSettings(cfg);
        MM_CHECK(v.has_value());
        MM_CHECK(v->field == "detection.sensitivity");
    }

    // Case 3: cooldownMs = -100 -> field "detection.cooldownMs".
    {
        mc::AppConfig cfg;
        cfg.detection.cooldownMs = -100;
        auto v = md::validateSettings(cfg);
        MM_CHECK(v.has_value());
        MM_CHECK(v->field == "detection.cooldownMs");
    }

    // Case 4: fftSize = 1000 (not power-of-2) -> field "detection.fftSize".
    {
        mc::AppConfig cfg;
        cfg.detection.fftSize = 1000;
        auto v = md::validateSettings(cfg);
        MM_CHECK(v.has_value());
        MM_CHECK(v->field == "detection.fftSize");
    }

    // Case 5: bufferSizeMs = 0 -> field "audio.bufferSizeMs".
    {
        mc::AppConfig cfg;
        cfg.audio.bufferSizeMs = 0;
        auto v = md::validateSettings(cfg);
        MM_CHECK(v.has_value());
        MM_CHECK(v->field == "audio.bufferSizeMs");
    }

    std::cout << "all tests passed\n";
    return 0;
}

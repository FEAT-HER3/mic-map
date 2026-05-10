/**
 * @file test_tray_glyph_state_machine.cpp
 * @brief Phase 10 Wave 0 RED scaffold for HEALTH-08 deriveTrayGlyph state derivation (D-04, D-05).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / tests/test_settings_validator.cpp.
 *
 * RED until Plan 10-02 lands apps/micmap/src/tray_glyph.{hpp,cpp}. This translation
 * unit is intentionally fail-to-build at Wave 0 — the compile failure IS the
 * Nyquist gate per .planning/phases/10-cutover-cleanup/RESEARCH.md
 * "Validation Architecture" section.
 *
 * Test cases (CONTEXT D-04 / D-05):
 *  (1) Pure armed when driver loaded, idle, no errors.
 *  (2) Error from FAIL-02 (driverLoaded=false) trumps everything.
 *  (3) Error from /state.last_error (non-null, non-cleared) -> Error.
 *  (4) Error from FAIL-01 (audioDeviceState=permission_denied) -> Error.
 *  (5) Error from FAIL-05 (audioDeviceState=missing) -> Error.
 *  (6) Triggered observed via detection_state == "triggered".
 *  (7) Triggered pulse window: lastTriggeredAt = now-100ms, state=cooldown -> Triggered.
 *  (8) Triggered pulse expired: lastTriggeredAt = now-400ms, state=idle -> Armed.
 *  (9) Error trumps triggered (driverLoaded=false AND state=triggered).
 *  (10) Triggered trumps armed (no errors, state=triggered).
 */

#include "tray_glyph.hpp"   // RED hook: lands in Plan 10-02

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace mc = micmap::client;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    using clock_t_ = std::chrono::steady_clock;
    using std::chrono::milliseconds;

    auto now = clock_t_::now();

    // Case 1: pure armed.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{
            /*detectionState=*/"idle",
            /*lastError=*/std::nullopt,
            /*audioDeviceState=*/"active"
        };
        auto pulse = clock_t_::time_point{};   // never triggered
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Armed);
    }

    // Case 2: FAIL-02 driverLoaded=false -> Error.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/false };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Error);
    }

    // Case 3: /state.last_error non-null -> Error.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "idle", std::optional<std::string>{"some_err"}, "active" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Error);
    }

    // Case 4: FAIL-01 mic permission -> Error.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "permission_denied" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Error);
    }

    // Case 5: FAIL-05 device removed -> Error.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "missing" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Error);
    }

    // Case 6: detection_state == "triggered" observed -> Triggered.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "triggered", std::nullopt, "active" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Triggered);
    }

    // Case 7: pulse window — lastTriggeredAt = now - 100ms, state cooled to "cooldown" -> Triggered.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "cooldown", std::nullopt, "active" };
        auto pulse = now - milliseconds(100);
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Triggered);
    }

    // Case 8: pulse expired — lastTriggeredAt = now - 400ms, state idle -> Armed.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pulse = now - milliseconds(400);
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Armed);
    }

    // Case 9: Error trumps Triggered when driver gone AND state=triggered.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/false };
        mc::StateSnapshot state{ "triggered", std::nullopt, "active" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Error);
    }

    // Case 10: Triggered trumps Armed when no errors and state=triggered.
    {
        mc::HealthSnapshot health{ /*driverLoaded=*/true };
        mc::StateSnapshot state{ "triggered", std::nullopt, "active" };
        auto pulse = clock_t_::time_point{};
        MM_CHECK(mc::deriveTrayGlyph(health, state, pulse, now) == mc::TrayGlyph::Triggered);
    }

    std::cout << "all tests passed\n";
    return 0;
}

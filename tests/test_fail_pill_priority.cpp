/**
 * @file test_fail_pill_priority.cpp
 * @brief Phase 10 Wave 0 RED scaffold for D-08 priority stacking
 *        (FAIL-02 > FAIL-03 > FAIL-01 > FAIL-05).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / tests/test_settings_validator.cpp.
 *
 * RED until Plan 10-03 lands apps/micmap/src/fail_pill.{hpp,cpp}. This translation
 * unit is intentionally fail-to-build at Wave 0 — the compile failure IS the
 * Nyquist gate per .planning/phases/10-cutover-cleanup/RESEARCH.md
 * "Validation Architecture" section.
 *
 * Test cases (CONTEXT D-07 / D-08 / D-09 / D-10):
 *  (1) No fails: pickActivePill() returns nullopt.
 *  (2) FAIL-01 only: kind=MicPermission, deepLink contains ms-settings.
 *  (3) FAIL-02 only: kind=DriverNotLoaded, action label "Open SteamVR".
 *  (4) FAIL-03 only: kind=SteamVRNotRunning.
 *  (5) FAIL-05 only: kind=DeviceRemoved.
 *  (6) FAIL-02 trumps FAIL-01.
 *  (7) FAIL-02 vs FAIL-03 disambiguation by vrserverRunning.
 *  (8) FAIL-01 trumps FAIL-05 when both present.
 *  (9) Dismissable bits: FAIL-01 + FAIL-05 dismissable; FAIL-02 + FAIL-03 not.
 */

#include "fail_pill.hpp"   // RED hook: lands in Plan 10-03

#include <iostream>
#include <optional>
#include <string>

namespace mc = micmap::client;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Case 1: no fails -> nullopt.
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{
            /*detectionState=*/"idle",
            /*lastError=*/std::nullopt,
            /*audioDeviceState=*/"active"
        };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(!pill.has_value());
    }

    // Case 2: FAIL-01 only -> MicPermission with ms-settings deep-link.
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "permission_denied" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::MicPermission);
        MM_CHECK(pill->deepLink.find("ms-settings:privacy-microphone") != std::string::npos);
    }

    // Case 3: FAIL-02 only -> DriverNotLoaded with "Open SteamVR" action label.
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::DriverNotLoaded);
        MM_CHECK(pill->actionLabel.find("Open SteamVR") != std::string::npos);
    }

    // Case 4: FAIL-03 only -> SteamVRNotRunning.
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/false);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::SteamVRNotRunning);
    }

    // Case 5: FAIL-05 only -> DeviceRemoved.
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "missing" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::DeviceRemoved);
    }

    // Case 6: FAIL-02 trumps FAIL-01 (defensive ordering — ECONNREFUSED hides the audio status).
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "permission_denied" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::DriverNotLoaded);
    }

    // Case 7: FAIL-02 vs FAIL-03 disambiguation by vrserverRunning.
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pill_03 = mc::pickActivePill(health, state, /*vrserverRunning=*/false);
        MM_CHECK(pill_03.has_value());
        MM_CHECK(pill_03->kind == mc::FailKind::SteamVRNotRunning);
        auto pill_02 = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill_02.has_value());
        MM_CHECK(pill_02->kind == mc::FailKind::DriverNotLoaded);
    }

    // Case 8: FAIL-01 trumps FAIL-05 when both fire (permission_denied wins over missing).
    // The driver reports ONE audioDeviceState string; this case is constructed by setting
    // permission_denied and asserting that the resulting pill is MicPermission, not
    // DeviceRemoved (the priority is encoded in the audioDeviceState value space itself —
    // permission_denied takes precedence over missing because the action is more actionable).
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "permission_denied" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::MicPermission);
    }

    // Case 9a: FAIL-01 dismissable (D-10).
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "permission_denied" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->dismissable == true);
    }
    // Case 9b: FAIL-05 dismissable.
    {
        mc::HealthSnapshot health{ /*ok=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "missing" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->dismissable == true);
    }
    // Case 9c: FAIL-02 NOT dismissable.
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/true);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->dismissable == false);
    }
    // Case 9d: FAIL-03 NOT dismissable.
    {
        mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };
        mc::StateSnapshot state{ "idle", std::nullopt, "active" };
        auto pill = mc::pickActivePill(health, state, /*vrserverRunning=*/false);
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->dismissable == false);
    }

    std::cout << "all tests passed\n";
    return 0;
}

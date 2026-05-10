/**
 * @file test_version_mismatch.cpp
 * @brief Phase 10 Wave 0 RED scaffold for D-20 client startup version-mismatch
 *        detection (INST-09).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / tests/test_settings_validator.cpp.
 *
 * RED until Plan 10-06 lands apps/micmap/src/version_mismatch.{hpp,cpp}. This
 * translation unit is intentionally fail-to-build at Wave 0 — the compile
 * failure IS the Nyquist gate per .planning/phases/10-cutover-cleanup/RESEARCH.md
 * "Validation Architecture" section.
 *
 * Test cases (CONTEXT D-20):
 *  (1) Match path: client + driver same version -> Match, no pill.
 *  (2) Mismatch path: differing versions -> Mismatch + VersionMismatch pill
 *      whose text contains both versions.
 *  (3) Empty driver_version (driver predates field) -> DriverVersionMissing
 *      + still produces a pill (graceful pre-INST-09 driver).
 *  (4) Pill is NEVER blocking (warn-only per D-20).
 *  (5) Pill is dismissable (one-time per session).
 */

#include "version_mismatch.hpp"   // RED hook: lands in Plan 10-06

#include <iostream>
#include <optional>
#include <string>

namespace mc = micmap::client;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Case 1: Match path -> Match, no pill.
    {
        auto result = mc::compareVersions(/*client=*/"1.6.0", /*driver=*/"1.6.0");
        MM_CHECK(result == mc::VersionCompareResult::Match);
        auto pill = mc::buildVersionMismatchPill(/*client=*/"1.6.0", /*driver=*/"1.6.0");
        MM_CHECK(!pill.has_value());
    }

    // Case 2: Mismatch path -> Mismatch, pill with both versions in text.
    {
        auto result = mc::compareVersions(/*client=*/"1.6.0", /*driver=*/"1.5.0");
        MM_CHECK(result == mc::VersionCompareResult::Mismatch);
        auto pill = mc::buildVersionMismatchPill(/*client=*/"1.6.0", /*driver=*/"1.5.0");
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::VersionMismatch);
        MM_CHECK(pill->text.find("1.6.0") != std::string::npos);
        MM_CHECK(pill->text.find("1.5.0") != std::string::npos);
    }

    // Case 3: Empty driver_version -> DriverVersionMissing, still produces pill.
    {
        auto result = mc::compareVersions(/*client=*/"1.6.0", /*driver=*/"");
        MM_CHECK(result == mc::VersionCompareResult::DriverVersionMissing);
        auto pill = mc::buildVersionMismatchPill(/*client=*/"1.6.0", /*driver=*/"");
        MM_CHECK(pill.has_value());
        MM_CHECK(pill->kind == mc::FailKind::VersionMismatch);
    }

    // Case 4: Pill is NEVER blocking (warn-only — D-20).
    {
        auto pill_mismatch = mc::buildVersionMismatchPill("1.6.0", "1.5.0");
        MM_CHECK(pill_mismatch.has_value());
        MM_CHECK(pill_mismatch->blocking == false);
        auto pill_missing = mc::buildVersionMismatchPill("1.6.0", "");
        MM_CHECK(pill_missing.has_value());
        MM_CHECK(pill_missing->blocking == false);
    }

    // Case 5: Pill is dismissable (one-time warning per session).
    {
        auto pill_mismatch = mc::buildVersionMismatchPill("1.6.0", "1.5.0");
        MM_CHECK(pill_mismatch.has_value());
        MM_CHECK(pill_mismatch->dismissable == true);
        auto pill_missing = mc::buildVersionMismatchPill("1.6.0", "");
        MM_CHECK(pill_missing.has_value());
        MM_CHECK(pill_missing->dismissable == true);
    }

    std::cout << "all tests passed\n";
    return 0;
}

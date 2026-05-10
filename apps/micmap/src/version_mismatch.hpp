/**
 * @file version_mismatch.hpp
 * @brief Phase 10 / INST-09 / D-20: client-side driver-vs-client version-mismatch check.
 *
 * Pure compareVersions() function over (clientVersion, driverVersion) -> enum result.
 * buildVersionMismatchPill() helper produces a low-priority FailPill describing the
 * mismatch (separate from the FAIL-01..05 priority stack per Discretion -- version
 * mismatch is install hygiene, not reachability).
 *
 * Per D-20: warn-only, NEVER blocks detection. Hard-block was rejected (would brick
 * users mid-upgrade where driver/client update seconds apart). Pill is dismissable
 * (one-time per session) and never sets pill.blocking = true.
 *
 * Field-name alignment with tests/test_version_mismatch.cpp Wave 0 RED scaffold:
 *   - struct FailPill carries `text` (matches scaffold; renamed from "headlineText"
 *     in Plan 10-06 alongside this introduction -- 10-03's tests/test_fail_pill_priority
 *     scaffold does NOT reference this field, so the rename is regression-free).
 */
#pragma once

#include "fail_pill.hpp"   // FailPill, FailKind::VersionMismatch (declared in 10-03)

#include <optional>
#include <string>

namespace micmap::client {

enum class VersionCompareResult {
    Match,                  ///< client == driver (exact-string compare)
    Mismatch,               ///< client != driver (both populated, exact-string compare)
    DriverVersionMissing    ///< driver returned empty driver_version (predates 10-03 D-19)
};

/**
 * Pure exact-string version comparison. Empty driverVersion is treated as
 * DriverVersionMissing (graceful pre-INST-09 driver detection); semver-aware
 * compatibility is explicitly out of scope for v1.6 per CONTEXT D-20.
 */
VersionCompareResult compareVersions(const std::string& clientVersion,
                                     const std::string& driverVersion);

/**
 * Build a low-priority pill describing the mismatch. Returns nullopt for Match.
 *
 * Pill semantics (D-20):
 *   - text contains BOTH client and driver versions (or notes the missing field).
 *   - kind == FailKind::VersionMismatch (declared in fail_pill.hpp by 10-03).
 *   - dismissable == true (one-time per session; user can suppress).
 *   - blocking == false (ALWAYS in v1.6 -- pill warns, never gates detection).
 *   - actionLabel + deepLink left empty: there is no canonical "reinstall" URI;
 *     user is prompted to reinstall manually.
 */
std::optional<FailPill> buildVersionMismatchPill(const std::string& clientVersion,
                                                 const std::string& driverVersion);

} // namespace micmap::client

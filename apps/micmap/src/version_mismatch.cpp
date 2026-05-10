/**
 * @file version_mismatch.cpp
 * @brief Phase 10 / INST-09 / D-20: implementation of compareVersions +
 *        buildVersionMismatchPill (pure functions, no Win32, no I/O).
 */
#include "version_mismatch.hpp"

namespace micmap::client {

VersionCompareResult compareVersions(const std::string& clientVersion,
                                     const std::string& driverVersion) {
    // Empty driver version -> the driver predates 10-03 D-19 wiring (no
    // driver_version field in /health). Surface as DriverVersionMissing so
    // the pill text can explain the situation (vs a generic "Mismatch").
    if (driverVersion.empty()) {
        return VersionCompareResult::DriverVersionMissing;
    }
    // Exact-string compare. v1.6 explicitly rejects semver-component
    // comparison (CONTEXT D-20) -- a future plan can add semver-aware
    // compatibility if the user-base demands it.
    return (clientVersion == driverVersion)
        ? VersionCompareResult::Match
        : VersionCompareResult::Mismatch;
}

std::optional<FailPill> buildVersionMismatchPill(const std::string& clientVersion,
                                                 const std::string& driverVersion) {
    const auto cmp = compareVersions(clientVersion, driverVersion);
    if (cmp == VersionCompareResult::Match) {
        return std::nullopt;
    }

    FailPill pill;
    pill.kind = FailKind::VersionMismatch;
    if (cmp == VersionCompareResult::DriverVersionMissing) {
        pill.text = "Driver version unknown -- driver predates this client (v"
                  + clientVersion + "). Reinstall recommended.";
    } else {
        pill.text = "Version mismatch -- driver v" + driverVersion
                  + " vs client v" + clientVersion + ". Reinstall recommended.";
    }
    pill.actionLabel = "";   // No canonical "reinstall MicMap" deep-link URI; user manual action.
    pill.deepLink    = "";
    pill.dismissable = true; // D-20: one-time-per-session warning; user may suppress.
    pill.blocking    = false;// D-20: ALWAYS false in v1.6 -- pill warns, never blocks detection.
    return pill;
}

} // namespace micmap::client

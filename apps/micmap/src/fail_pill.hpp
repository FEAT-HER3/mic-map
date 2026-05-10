/**
 * @file fail_pill.hpp
 * @brief Phase 10 / FAIL-01..05 / D-07 / D-08 / D-10: priority-stacked failure pills.
 *
 * pickActivePill() is a pure function that consumes (HealthSnapshot, StateSnapshot,
 * vrserverRunning bool) and returns the single topmost-priority FailPill (or nullopt).
 * Render is in the existing P8 D-11 driver-health pane (no modals, no balloons).
 *
 * Priority order (D-08): FAIL-02 (DriverNotLoaded) > FAIL-03 (SteamVRNotRunning)
 *                      > FAIL-01 (MicPermission) > FAIL-05 (DeviceRemoved).
 * FAIL-04 (DoubleInstance) is handled at WinMain entry -- never reaches this surface.
 * FAIL-02 vs FAIL-03 disambiguation: vrserverRunning bool from process_check.hpp.
 *
 * Field naming alignment with tests/test_fail_pill_priority.cpp Wave 0 RED scaffold:
 *   - struct FailPill carries `deepLink` (matches scaffold; equivalent to plan's "actionUri")
 *   - HealthSnapshot brace-init {driverLoaded, econnrefused} matches scaffold positional args
 */
#pragma once

#include "tray_glyph.hpp"   // HealthSnapshot, StateSnapshot canonical declarations (10-02 SUMMARY)

#include <optional>
#include <string>

namespace micmap::client {

enum class FailKind {
    None,
    MicPermission,        // FAIL-01
    DriverNotLoaded,      // FAIL-02
    SteamVRNotRunning,    // FAIL-03
    DoubleInstance,       // FAIL-04 (NEVER returned by pickActivePill -- handled at WinMain)
    DeviceRemoved,        // FAIL-05
    VersionMismatch       // 10-06 reuses this enum; pickActivePill never returns it (10-06 stacks separately per Discretion)
};

struct FailPill {
    FailKind kind{FailKind::None};
    std::string headlineText;     ///< e.g., "Mic access blocked"
    std::string actionLabel;      ///< e.g., "Open Windows mic settings"; empty for no action button
    std::string deepLink;         ///< e.g., "ms-settings:privacy-microphone"; empty if no shell-launch (test scaffold names this field "deepLink"; render path passes it to ShellExecuteW)
    bool dismissable{false};      ///< true -> render Dismiss button that POSTs /state/clear-error (D-10)
    bool blocking{false};         ///< ALWAYS false in v1.6 (D-20 -- pills warn, never block detection)
};

/**
 * @brief Pure D-08 priority-stacked picker. Testable headless via test_fail_pill_priority.
 *
 * @param health         /health snapshot (driverLoaded + econnrefused + getter fields)
 * @param state          /state snapshot (detectionState, lastError, audioDeviceState)
 * @param vrserverRunning true if isProcessRunning(L"vrserver.exe") returned true within
 *                        the 2s cache window. Caller is responsible for the process check
 *                        (typically only meaningful when health.driverLoaded == false; when
 *                        the driver IS loaded the bool is conventionally true since the
 *                        endpoint reached us, but pickActivePill does not depend on that
 *                        invariant).
 */
std::optional<FailPill> pickActivePill(const HealthSnapshot& health,
                                       const StateSnapshot& state,
                                       bool vrserverRunning);

} // namespace micmap::client

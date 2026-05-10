#include "fail_pill.hpp"

namespace micmap::client {

std::optional<FailPill> pickActivePill(const HealthSnapshot& health,
                                       const StateSnapshot& state,
                                       bool vrserverRunning) {
    // Priority 1: ECONNREFUSED / driver unreachable -> FAIL-02 or FAIL-03.
    //   Disambiguate via vrserverRunning bool (Pitfall 6 -- caller computes via 2s-cached snapshot).
    //   D-08: FAIL-02/03 trump FAIL-01 (mic permission) because if the driver is unreachable,
    //   the audioDeviceState field cannot be trusted (the value is stale from the last successful
    //   /state poll, if any).
    if (!health.driverLoaded) {
        if (!vrserverRunning) {
            // FAIL-03: SteamVR not running.
            //   No deep-link button -- there is no canonical URI to "launch SteamVR" that
            //   doesn't conflict with the user's preferred launcher (Steam vs SteamVR direct).
            //   Not dismissable -- the underlying condition would re-fire on next poll, so
            //   the dismiss button would be a no-op.
            return FailPill{
                FailKind::SteamVRNotRunning,
                "SteamVR not running",
                "",                        // no action button
                "",
                false,                     // not dismissable
                false                      // never blocking (D-20)
            };
        } else {
            // FAIL-02: SteamVR is up but the MicMap driver is not loaded.
            //   "Open SteamVR" -> steam://rungameid/250820 launches the SteamVR
            //   client UI (where the user can verify driver registration).
            //   Not dismissable -- same reason as FAIL-03.
            return FailPill{
                FailKind::DriverNotLoaded,
                "Driver not installed -- run installer or enable in SteamVR",
                "Open SteamVR",
                "steam://rungameid/250820",
                false,
                false
            };
        }
    }

    // Priority 2: mic-permission blocked (FAIL-01).
    //   Deep-link to Windows Settings privacy microphone page.
    //   Dismissable per D-10 (user may have intentionally denied).
    if (state.audioDeviceState == "permission_denied") {
        return FailPill{
            FailKind::MicPermission,
            "Mic access blocked",
            "Open Windows mic settings",
            "ms-settings:privacy-microphone",
            true,                          // dismissable via /state/clear-error (D-10)
            false
        };
    }

    // Priority 3: device disconnected (FAIL-05).
    //   No action button -- the driver auto-recovers via IMMNotificationClient rebind
    //   (P6/P7); user just needs to reconnect the device.
    //   Dismissable per D-10.
    if (state.audioDeviceState == "missing") {
        return FailPill{
            FailKind::DeviceRemoved,
            "Microphone disconnected -- reconnect to resume detection",
            "",
            "",
            true,                          // dismissable via /state/clear-error
            false
        };
    }

    // Priority 4: last_error fallthrough -- any other non-cleared error from /state.last_error.
    //   This catches edge cases not covered by the structured FAIL-01/05 audio_device_state
    //   fields (e.g., a driver-side detection-thread crash that surfaces last_error=
    //   "detection_thread_died"). Surface as a generic dismissable pill.
    if (state.lastError.has_value() && !state.lastError->empty()) {
        return FailPill{
            FailKind::None,                // kind=None signals "generic last_error -- render text but no canonical action"
            *state.lastError,
            "",
            "",
            true,                          // dismissable
            false
        };
    }

    return std::nullopt;
}

} // namespace micmap::client

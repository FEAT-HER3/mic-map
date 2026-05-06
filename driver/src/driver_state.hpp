/**
 * @file driver_state.hpp
 * @brief Phase 8 D-23: read-only POD snapshotted into the GET /state response.
 *
 * Held inside DeviceProvider as std::shared_ptr<const DriverState>; published
 * via std::atomic_store_explicit on each meaningful state change (detection
 * machine transition, trigger fire, audio device change, error fire/clear).
 * Read from the HTTP thread via std::atomic_load_explicit on the same
 * shared_ptr (P7 atomic-snapshot pattern, generalized).
 *
 * Field semantics match GET /state response (CONTEXT IPC-01):
 *  - detection_state: "idle" | "training" | "detecting" | "triggered" | "cooldown"
 *  - last_trigger_at: UTC time_point (nullopt if never)
 *  - last_error:      driver-supplied error string (nullopt if no current error)
 *  - audio_device_id: empty if no device selected; else WASAPI endpoint id (UTF-8)
 *  - audio_device_state: "ok" | "missing" | "permission_denied"
 *
 * driver_loaded + steamvr_running fields in the JSON response are derived
 * (always true if the endpoint was reachable) -- they are NOT stored here.
 */

#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace micmap::driver {

struct DriverState {
    std::string detection_state{"idle"};
    std::optional<std::chrono::system_clock::time_point> last_trigger_at;
    std::optional<std::string> last_error;
    std::string audio_device_id;        // UTF-8 of std::wstring endpoint id
    std::string audio_device_state{"ok"};
};

} // namespace micmap::driver

/**
 * @file vr_input_events.cpp
 * @brief Testable VR event processing — ack-first for VREvent_Quit (D-11)
 *
 * See vr_input_events.hpp for the invariant: for VREvent_Quit, the
 * AcknowledgeQuit_Exiting call happens BEFORE the app callback fires.
 * This is the load-bearing correctness fix for AUTO-05 / OpenVR #1425 /
 * research Pitfall 2 — Valve's 2-second quit watchdog is satisfied
 * regardless of how long the app-side ordered teardown takes.
 */

#include "micmap/steamvr/vr_input_events.hpp"
#include "micmap/common/logger.hpp"

#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#endif

#include <cstdint>

namespace micmap::steamvr {

// VREvent_Quit event-type value. The openvr.h enum is canonical when
// MICMAP_HAS_OPENVR is defined; the literal 700u fallback applies only
// to stub builds (no OpenVR headers available). The value has been
// stable in the OpenVR EVREventType enum since the SDK's inception.
#ifdef MICMAP_HAS_OPENVR
static constexpr uint32_t kVREventQuit = static_cast<uint32_t>(vr::VREvent_Quit);
#else
static constexpr uint32_t kVREventQuit = 700u;
#endif

void processVREventImpl(IVRSystemSeam& system, IEventSink& sink, uint32_t eventType) {
    if (eventType == kVREventQuit) {
        MICMAP_LOG_INFO("SteamVR quit event received");
        // D-11 / Pitfall 2 / OpenVR #1425:
        // Ack IMMEDIATELY, BEFORE the app callback. This stops Valve's
        // 2-second quit watchdog clock so downstream ordered teardown
        // (audio stop, detector reset, driver disconnect, vrInput shutdown,
        // tray removal, ImGui/D3D destruction) can run to completion
        // without racing a force-kill.
        system.AcknowledgeQuit_Exiting();
        sink.notifyEvent(VREventType::Quit);
        return;
    }
    // Default branch: other event types are a no-op here. Button edges go
    // through IDriverApi; dashboard-state polling is not needed by the
    // app layer (the driver owns /input/system/click on the HMD container).
}

} // namespace micmap::steamvr

#pragma once

/**
 * @file vr_input_events.hpp
 * @brief Testable VR event processing (AUTO-05 / D-11)
 *
 * Extracted from OpenVRInput::processVREvent so the ack-first ordering of
 * VREvent_Quit can be unit-tested without linking OpenVR or booting SteamVR.
 * Production code calls via OpenVRInput's member adapters that forward to
 * vr::VRSystem()->AcknowledgeQuit_Exiting() and OpenVRInput::notifyEvent.
 *
 * Naming: the free function is `processVREventImpl` rather than
 * `processVREvent` so OpenVRInput::processVREvent (the existing member)
 * can delegate to it unambiguously without name-lookup gymnastics.
 */

#include "micmap/steamvr/vr_input.hpp"  // for VREventType

#include <cstdint>

namespace micmap::steamvr {

/**
 * @brief Test seam over vr::IVRSystem for the subset of methods
 *        processVREventImpl needs (just AcknowledgeQuit_Exiting today).
 *
 * Keeps the header OpenVR-free: tests can instantiate their own stubs
 * without pulling in openvr.h or booting SteamVR.
 */
class IVRSystemSeam {
public:
    virtual ~IVRSystemSeam() = default;
    virtual void AcknowledgeQuit_Exiting() = 0;
};

/**
 * @brief Callback sink for app-level VR events.
 *
 * Production implementation forwards into OpenVRInput::notifyEvent which
 * dispatches to the currently-registered VREventCallback; test stubs
 * record each call for ordering assertions.
 */
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void notifyEvent(VREventType type) = 0;
};

/**
 * @brief Process a single VR event.
 *
 * @param system     Seam for calling AcknowledgeQuit_Exiting.
 * @param sink       Sink for app-level event notifications.
 * @param eventType  vr::EVREventType value cast to uint32_t so this header
 *                   carries no OpenVR dependency. VREvent_Quit == 700.
 *
 * Invariant (D-11 / Pitfall 2 / OpenVR issue #1425):
 * For VREvent_Quit, AcknowledgeQuit_Exiting is called BEFORE notifyEvent.
 * Valve's 2-second quit watchdog is satisfied regardless of downstream
 * teardown latency — the ack has already landed by the time MicMapApp's
 * ordered shutdown() starts running.
 */
void processVREventImpl(IVRSystemSeam& system, IEventSink& sink, uint32_t eventType);

} // namespace micmap::steamvr

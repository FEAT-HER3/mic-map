/**
 * @file test_vr_input_quit_ordering.cpp
 * @brief RED unit test for AUTO-05 ack-first invariant (D-11).
 *
 * Phase 3 Plan 01 Task 2 (Wave 0 test scaffold). Verifies that
 * vr::IVRSystem::AcknowledgeQuit_Exiting() is called BEFORE the app's
 * VREvent_Quit callback fires — the load-bearing correctness rule that
 * decouples Valve's 2-second quit watchdog from app-side teardown latency
 * (OpenVR issue #1425, research Pitfall 2).
 *
 * Plan 03-05 supplies:
 *   - src/steamvr/include/micmap/steamvr/vr_input_events.hpp
 *       declares IVRSystemSeam, IEventSink, processVREvent() (free fn).
 *   - src/steamvr/src/vr_input_events.cpp
 *       implements processVREvent so the VREvent_Quit branch calls
 *       seam.AcknowledgeQuit_Exiting() first, then sink.notifyEvent(Quit).
 *
 * Plan 03-05 also rewrites vr_input.cpp's existing private processVREvent
 * member to delegate into the free function so the production code path
 * exercises the same logic this test verifies.
 *
 * Until then this translation unit fails to compile with a missing
 * "micmap/steamvr/vr_input_events.hpp" diagnostic — the expected RED state.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Matches
 * tests/test_config_manager.cpp.
 */

#include "micmap/steamvr/vr_input_events.hpp"

#include <openvr.h>

#include <iostream>
#include <string>
#include <vector>

namespace svr = micmap::steamvr;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace {

class StubVRSystemSeam : public svr::IVRSystemSeam {
public:
    std::vector<std::string>* log_;
    explicit StubVRSystemSeam(std::vector<std::string>& log) : log_(&log) {}

    void AcknowledgeQuit_Exiting() override {
        log_->push_back("AcknowledgeQuit_Exiting()");
    }
};

class StubEventSink : public svr::IEventSink {
public:
    std::vector<std::string>* log_;
    explicit StubEventSink(std::vector<std::string>& log) : log_(&log) {}

    void notifyEvent(svr::VREventType type) override {
        if (type == svr::VREventType::Quit) {
            log_->push_back("notifyEvent(Quit)");
        } else {
            log_->push_back("notifyEvent(other)");
        }
    }
};

}  // namespace

int main() {
    // ---- Case 1: VREvent_Quit -> ack BEFORE notifyEvent ----
    {
        std::vector<std::string> log;
        StubVRSystemSeam system(log);
        StubEventSink    sink(log);

        svr::processVREvent(system, sink, static_cast<uint32_t>(vr::VREvent_Quit));

        // Both calls must have happened.
        MM_CHECK(log.size() == 2);
        // Strict ordering: AcknowledgeQuit_Exiting at index 0, callback after.
        MM_CHECK(log[0] == "AcknowledgeQuit_Exiting()");
        MM_CHECK(log[1] == "notifyEvent(Quit)");
        std::cout << "PASS case_1_quit_ack_before_notify\n";
    }

    // ---- Case 2: non-Quit event -> no ack, no Quit notify ----
    // Ensures the ack call is gated specifically on VREvent_Quit, not fired
    // for unrelated events (would otherwise destabilize SteamVR runtime).
    {
        std::vector<std::string> log;
        StubVRSystemSeam system(log);
        StubEventSink    sink(log);

        svr::processVREvent(system, sink,
                            static_cast<uint32_t>(vr::VREvent_DashboardActivated));

        // No AcknowledgeQuit_Exiting for a non-Quit event.
        bool sawAck = false;
        for (const auto& s : log) {
            if (s == "AcknowledgeQuit_Exiting()") { sawAck = true; break; }
        }
        MM_CHECK(!sawAck);
        std::cout << "PASS case_2_non_quit_no_ack\n";
    }

    std::cout << "all tests passed\n";
    return 0;
}

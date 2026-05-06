/**
 * @file test_client_level_meter_cadence.cpp
 * @brief Phase 8 Wave 0 RED scaffold for level-meter 5 Hz visible / 0.5 Hz tray (HEALTH-06).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_command_queue.cpp / driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-05 lands the client-side level-meter polling loop
 * (apps/micmap/main.cpp + driver_api.hpp visibility-state coupling).
 * Build-time fail = Nyquist gate.
 *
 * Timing-tolerant: real wall clock 1 s; 5 Hz poll asserts 4..6 calls
 * (allowing for jitter); 0.5 Hz poll asserts 0..1 calls. The polling-loop
 * factory is part of the Plan 08-05 deliverables; this scaffold expects
 * it to be reachable through driver_api.hpp's level-meter helper or a
 * dedicated header in apps/micmap/.
 */

#include "micmap/steamvr/driver_api.hpp"     // RED hook: lands in Plan 08-01

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace ms = micmap::steamvr;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Counter increments inside the level-meter polling loop. The loop
    // factory will be wired to an injectable visibility-state source in
    // Plan 08-05; for the Wave 0 scaffold we only need to drive the
    // include chain so the absent symbol fails the build.
    std::atomic<int> calls{0};

    // Visible mode: 5 Hz expected.
    {
        calls.store(0);
        auto loop = ms::startLevelMeterPolling(
            /*visible=*/[]() { return true; },
            /*onSample=*/[&calls](float) { calls.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::seconds(1));
        loop.reset();   // RAII stop
        int n = calls.load();
        MM_CHECK(n >= 4 && n <= 6);
    }

    // Tray (iconic) mode: 0.5 Hz expected -> 0 or 1 sample in 1 s.
    {
        calls.store(0);
        auto loop = ms::startLevelMeterPolling(
            /*visible=*/[]() { return false; },
            /*onSample=*/[&calls](float) { calls.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::seconds(1));
        loop.reset();
        int n = calls.load();
        MM_CHECK(n >= 0 && n <= 1);
    }

    std::cout << "all tests passed\n";
    return 0;
}

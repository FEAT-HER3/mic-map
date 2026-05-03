/**
 * @file detection_settings_propagation_test.cpp
 * @brief Phase 7 / D-25(4) / MIG-06: assert publish() -> detection-thread
 *        observed-swap < 50 ms.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_command_queue.cpp / tests/driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 07-03 lands detection_runner.hpp / .cpp. This translation
 * unit is intentionally fail-to-build at Wave 0 — that compile failure IS
 * the Nyquist gate, not the AssertDetectionRunnerNoVrApi lint (which stays
 * green via skip-on-NOT-EXISTS) nor the cmake configure step (which stays
 * green via if(EXISTS detection_runner.cpp) source-list gate).
 */

#include "detection_runner.hpp"   // resolved via target_include_directories tests/CMakeLists.txt
#include "sample_ring.hpp"
#include "command_queue.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace md = micmap::driver;
using clock_t_ = std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Construct DetectionRunner with an unbacked SampleRing + CommandQueue.
    md::SampleRing<16, 480> ring;
    md::CommandQueue queue;
    md::DetectionConfig initial{0.7f, 0.6f, 1000, 200};

    md::DetectionRunner runner(ring, queue, /*sampleRate=*/48000, initial);
    MM_CHECK(runner.Start());

    // Initial publish (so the detection thread has a config to load).
    auto cfg0 = std::make_shared<const md::DetectionConfig>(initial);
    runner.publish(cfg0);

    // Spin briefly to let the thread observe cfg0.
    std::this_thread::sleep_for(milliseconds(5));

    // Publish a new config from this (worker) thread; measure t0.
    md::DetectionConfig next{0.5f, 0.4f, 750, 150};
    auto cfg1 = std::make_shared<const md::DetectionConfig>(next);
    auto t0 = clock_t_::now();
    runner.publish(cfg1);

    // Poll the test-only accessor until pointer-identity changes.
    // Cap polling at 200 ms so a regression never hangs CI.
    auto deadline = t0 + milliseconds(200);
    while (clock_t_::now() < deadline) {
        auto observed = runner.active_config_for_test();
        if (observed.get() == cfg1.get()) {
            auto t1 = clock_t_::now();
            auto elapsed = duration_cast<milliseconds>(t1 - t0);
            MM_CHECK(elapsed.count() < 50);
            std::cout << "PASS case_propagation_under_50ms elapsed_ms="
                      << elapsed.count() << "\n";
            runner.Stop();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cerr << "FAIL: detection thread did not observe new config within 200 ms\n";
    runner.Stop();
    return 1;
}

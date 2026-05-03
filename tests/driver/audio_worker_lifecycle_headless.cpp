/**
 * @file audio_worker_lifecycle_headless.cpp
 * @brief Phase 6 Wave 0 RED test for AudioWorker lifecycle invariants.
 *
 * Verifies — without any real WASAPI device — that:
 *   1. AudioWorker constructed → destructor on a never-Started worker
 *      returns within ~100 ms (no thread spawn, no COM init).
 *   2. AudioWorker constructed → Start() (fail-soft on a headless box
 *      with no default WASAPI capture endpoint) → scope-exit destructor
 *      joins the worker thread within the 2 s watchdog (D-13 / Pitfall 4).
 *   3. (SKIPPED at Wave 0 — pending P7 hook) Pitfall 13 alive-flag-before-
 *      shutdown ordering: state->alive must be set false BEFORE the
 *      shutdown CV is signalled so in-flight audio callbacks bail out
 *      before the worker tears down. Requires a test-only accessor on
 *      AudioWorker::State (e.g. state_for_test()) which Plan 02 may add.
 *      Until then the Pitfall 13 ordering is verified via the D-17(3)
 *      manual UAT only.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Matches
 * tests/test_command_queue.cpp / tests/test_vr_input_quit_ordering.cpp.
 *
 * RED state: until audio_worker.cpp lands, this TU fails to build with
 * a missing "audio_worker.hpp" diagnostic — the expected RED state.
 * Plan 06-02 closes this Nyquist gate by landing the impl.
 */

#include "audio_worker.hpp"   // resolved via target_include_directories tests/CMakeLists.txt

#include <chrono>
#include <iostream>
#include <thread>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    using namespace std::chrono;
    namespace md = micmap::driver;

    // ---- Case 1: no-Start destructor returns within 100 ms ----
    // Construct an AudioWorker without calling Start(). The destructor on a
    // never-started worker must NOT block — no thread to join, no COM to
    // unwind. Loose 100 ms ceiling is comfortably above any reasonable
    // construction/destruction overhead and well below the 2 s watchdog.
    {
        auto t0 = steady_clock::now();
        {
            md::AudioWorker w;
            (void)w;
        }
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);
        MM_CHECK(elapsed.count() < 100);
        std::cout << "PASS case_1_no_start_destructor_fast\n";
    }

    // ---- Case 2: Start() then scope-exit destructor within 2 s watchdog ----
    // On a headless box with no WASAPI default capture endpoint, Start() may
    // succeed (worker thread spawns; createWASAPICapture returns a stub) OR
    // fail-soft (worker thread bails on createWASAPICapture==null). EITHER
    // outcome is acceptable for SC2 — what matters is that ~AudioWorker
    // joins the worker thread within 2 s + 500 ms slack (= 2500 ms ceiling
    // matching the D-13 watchdog precedent).
    {
        auto t0 = steady_clock::now();
        {
            md::AudioWorker w;
            bool started = w.Start();
            (void)started;
            // Destructor runs at end of this scope.
        }
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);
        MM_CHECK(elapsed.count() < 2500);
        std::cout << "PASS case_2_start_then_destruct_within_watchdog\n";
    }

    // ---- Case 3: alive-flag-before-shutdown ordering (Pitfall 13) ----
    // Spike-grade: requires a test-only hook on AudioWorker::State (e.g.
    // state_for_test()) that this test can observe. If Plan 06-02 chooses
    // to expose such an accessor, this case becomes runnable; otherwise
    // the Pitfall 13 ordering is verified by D-17(3) manual UAT on real
    // hardware. Print SKIP and continue.
    std::cout << "SKIP case_3_alive_before_shutdown_pending_p7_hook\n";

    std::cout << "all tests passed\n";
    return 0;
}

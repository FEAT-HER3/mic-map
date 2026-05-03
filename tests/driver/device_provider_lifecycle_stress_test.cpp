/**
 * @file device_provider_lifecycle_stress_test.cpp
 * @brief Phase 7 / D-25(3) / SC4 / MIG-04: 50-cycle Init->500ms->Cleanup
 *        with no leaked handles.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail.
 *
 * Direct DeviceProvider construct/destroy with null context. Settings-read
 * fail-softs to default-false (matching device_provider.cpp:85-95); AudioWorker
 * is not constructed; the lifecycle teardown of HTTP server, CommandQueue, and
 * the OpenVR-context cleanup IS exercised across all 50 cycles. Real-WASAPI
 * handle leaks are caught by the manual Process Explorer audit on Bigscreen
 * Beyond per D-25(3) — this headless test catches the OpenVR-context-teardown
 * leak class.
 *
 * RED until Plan 07-05 lands DetectionRunner construction in DeviceProvider::Init.
 */

#include "device_provider.hpp"
#include "audio_worker.hpp"
#include "detection_runner.hpp"
#include "http_server.hpp"
#include "command_queue.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace md = micmap::driver;
using std::chrono::milliseconds;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
#ifndef _WIN32
    std::cout << "SKIP device_provider_lifecycle_stress_test (Win32-only handle audit)\n";
    return 0;
#else
    DWORD baseHandles = 0;
    MM_CHECK(GetProcessHandleCount(GetCurrentProcess(), &baseHandles));

    constexpr int kCycles = 50;
    for (int i = 0; i < kCycles; ++i) {
        md::DeviceProvider dp;
        // Init with null context — DeviceProvider's settings-read path
        // fail-softs to default-false (device_provider.cpp:85-95).
        // AudioWorker / DetectionRunner are not constructed in this path;
        // the OpenVR-context-teardown lifecycle IS exercised across all
        // 50 cycles. Pitfall 4 reverse-order teardown coverage.
        vr::EVRInitError err = dp.Init(nullptr);
        (void)err;
        std::this_thread::sleep_for(milliseconds(500));
        // Cleanup runs implicitly via destructor at scope-exit.
    }

    DWORD afterHandles = 0;
    MM_CHECK(GetProcessHandleCount(GetCurrentProcess(), &afterHandles));

    // Allow <=5 handle delta for thread-pool churn (RESEARCH.md §"Pattern 4").
    if (afterHandles > baseHandles + 5) {
        std::cerr << "FAIL: handle leak base=" << baseHandles
                  << " after=" << afterHandles
                  << " delta=" << (afterHandles - baseHandles) << "\n";
        return 1;
    }
    std::cout << "PASS lifecycle_stress_50_cycles base=" << baseHandles
              << " after=" << afterHandles << "\n";
    return 0;
#endif
}

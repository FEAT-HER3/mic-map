/**
 * @file test_client_driver_loaded_indicator.cpp
 * @brief Phase 8 Wave 0 RED scaffold for IDriverApi::connect() 3-state result (HEALTH-01 / Pitfall 6).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_vr_input_quit_ordering.cpp / driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-01 lands src/steamvr/include/micmap/steamvr/driver_api.hpp
 * (post-rename of vr_input.hpp; see D-22) with the new ConnectResult enum
 * upgrading the previous bool return on connect(). Build-time fail =
 * Nyquist gate.
 *
 * NOTE on coverage: full ECONNREFUSED-vs-Read-timeout differentiation
 * requires a running stub HTTP server held with `set_read_timeout(0)` to
 * exercise the Timeout branch. This Wave 0 scaffold asserts only the
 * NotFound branch (no listener at all). The Timeout assertion is documented
 * here but skipped at scaffold time; Plan 08-01 lands the impl + grows
 * this test if it lands the timeout-path coverage too.
 */

#include "micmap/steamvr/driver_api.hpp"     // RED hook: lands in Plan 08-01

#include <iostream>

namespace ms = micmap::steamvr;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Construct DriverApi pointing at an unreachable port on the loopback.
    //
    // Port choice: a high-numbered ephemeral port nothing should be bound
    // to (65532 picked because it sits outside the typical %WINDIR%
    // dynamic-port range 49152-65000 and outside Steam/SteamVR's port
    // allocations). Windows TCP/IP returns WSAECONNREFUSED on connect()
    // to an unbound localhost port immediately (not via timeout), which
    // httplib::Result::error() surfaces as httplib::Error::Connection.
    // That is the Pitfall 6 NotFound branch — distinguished from Timeout
    // (which only fires when a listener exists but is slow to respond).
    auto api = ms::createDriverApi("127.0.0.1", /*startPort=*/65532, /*endPort=*/65532);
    MM_CHECK(api);

    auto result = api->connect();
    MM_CHECK(result == ms::ConnectResult::NotFound);

    std::cout << "all tests passed\n";
    return 0;
}

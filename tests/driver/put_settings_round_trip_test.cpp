/**
 * @file put_settings_round_trip_test.cpp
 * @brief Phase 8 Wave 0 RED scaffold for PUT /settings persist + GET reflect (IPC-04 / D-09).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-04 lands PUT /settings + driver/src/config_io.cpp
 * (saveConfigJson + atomic_store). Build-time fail = Nyquist gate.
 */

#include "http_server.hpp"
#include "config_io.hpp"        // RED hook: lands in Plan 08-02
#include "config_json.hpp"      // ADL hooks for AppConfig <-> json (08-02)
#include "command_queue.hpp"
#include "micmap/core/config_manager.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#ifdef _WIN32
#  include <cstdlib>
#  include <crtdbg.h>
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
#ifdef _WIN32
    // Suppress MSVC debug-runtime modal popups when abort() / assertions fire under ctest.
    // Without this, an unattended ctest run blocks on a "Debug Error!" dialog box.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

    auto tmp = std::filesystem::temp_directory_path() / "micmap_p8_round_trip_test";
    std::filesystem::create_directories(tmp);
    auto cfgPath = tmp / "config.json";
    std::filesystem::remove(cfgPath);

    mc::AppConfig live;
    auto cfgSnapshot = std::make_shared<mc::AppConfig>(live);

    auto configGetter = [&cfgSnapshot]() -> std::shared_ptr<const mc::AppConfig> {
        return std::atomic_load(&cfgSnapshot);
    };
    auto configMutator = [&cfgSnapshot, &cfgPath](const mc::AppConfig& candidate) -> bool {
        if (!md::saveConfigJson(cfgPath, candidate)) return false;
        auto next = std::make_shared<mc::AppConfig>(candidate);
        std::atomic_store(&cfgSnapshot, next);
        return true;
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27119,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/configGetter,
                          /*configMutator=*/configMutator);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(2);
    client.set_read_timeout(2);

    // Build a candidate config payload with sensitivity = 0.42f.
    nlohmann::json payload = *configGetter();
    payload["detection"]["sensitivity"] = 0.42f;

    auto putRes = client.Put("/settings", payload.dump(), "application/json");
    MM_CHECK(putRes && putRes->status == 200);

    auto getRes = client.Get("/settings");
    MM_CHECK(getRes && getRes->status == 200);
    auto body = nlohmann::json::parse(getRes->body);
    MM_CHECK(body["detection"]["sensitivity"].get<float>() == 0.42f);

    // Verify on-disk persistence by re-reading the file directly.
    std::ifstream f(cfgPath);
    MM_CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    auto disk = nlohmann::json::parse(ss.str());
    MM_CHECK(disk["detection"]["sensitivity"].get<float>() == 0.42f);

    server.Stop();
    std::filesystem::remove_all(tmp);
    std::cout << "all tests passed\n";
    return 0;
}

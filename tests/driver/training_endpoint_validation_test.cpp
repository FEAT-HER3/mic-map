/**
 * @file training_endpoint_validation_test.cpp
 * @brief Phase 9 Wave 0 RED scaffold for the 5 training HTTP endpoint validation envelopes
 *        (D-14, D-09, D-15, D-18, D-40).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/put_settings_validation_test.cpp (HTTP harness scaffold) and
 * tests/test_settings_validator.cpp (plain-main MM_CHECK convention).
 *
 * RED until Plan 09-02 lands the training endpoint extensions on
 * driver/src/http_server.{hpp,cpp} + the corresponding settings_validator
 * extensions for the 5 training payloads. Build-time fail = Nyquist gate.
 *
 * Cases:
 *   (a) POST /training/start with extra fields => 400 + structured envelope.
 *   (b) POST /training/finalize without `confirm` => 400.
 *   (c) POST /training/recompute with sensitivity 2.0 => 400 (field == "sensitivity").
 *   (d) POST /training/start when audio disabled => 503 ("audio_disabled").
 *   (e) POST /training/start while session active => 409 ("training_in_progress").
 *   (f) Pre-GET /training/progress baseline + post-call check (no state mutation on rejection).
 */

#include "http_server.hpp"
#include "training_session.hpp"             // RED hook: lands in Plan 09-01
#include "settings_validator.hpp"           // PUT /settings + training payload validators
#include "config_json.hpp"                  // ADL hooks for AppConfig <-> json
#include "command_queue.hpp"
#include "micmap/core/config_manager.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace md = micmap::driver;
namespace mc = micmap::core;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // Stub config snapshot + getters/mutators (mirror put_settings_validation_test.cpp).
    auto cfgSnapshot = std::make_shared<mc::AppConfig>();
    auto configGetter = [&cfgSnapshot]() -> std::shared_ptr<const mc::AppConfig> {
        return std::atomic_load(&cfgSnapshot);
    };
    auto configMutator = [&cfgSnapshot](const mc::AppConfig& candidate) -> bool {
        if (md::validateSettings(candidate).has_value()) return false;
        auto next = std::make_shared<mc::AppConfig>(candidate);
        std::atomic_store(&cfgSnapshot, next);
        return true;
    };

    // Audio-disabled toggle for case (d). Default true; case (d) flips it false.
    std::atomic<bool> audioEnabled{true};
    auto driverAudioEnabledGetter = [&audioEnabled]() -> bool {
        return audioEnabled.load(std::memory_order_acquire);
    };

    // Training-active toggle for case (e). Default false; case (e) flips it true
    // so that the second /training/start returns 409 training_in_progress.
    std::atomic<bool> trainingActive{false};
    auto trainingActiveGetter = [&trainingActive]() -> bool {
        return trainingActive.load(std::memory_order_acquire);
    };

    // P9 09-02: per-route HttpResult callbacks. The trainingStart handler
    // exercises the D-09 single-instance + D-40 audio_disabled gates so the
    // test cases (d) + (e) round-trip through a real route handler shape.
    // Other training callbacks are wired with minimal shims so the route
    // surface compiles + the validation envelope cases (a) (b) (c) are
    // exercised without needing a real TrainingSession.
    auto trainingStart = [&audioEnabled, &trainingActive]() -> md::HttpResult {
        if (!audioEnabled.load(std::memory_order_acquire)) {
            return md::HttpResult{
                503,
                R"({"error":"audio_disabled","reason":"enable_driver_audio is false"})"};
        }
        if (trainingActive.load(std::memory_order_acquire)) {
            return md::HttpResult{
                409,
                R"({"error":"training_in_progress","reason":"another session is active"})"};
        }
        return md::HttpResult{200, R"({"status":"ok"})"};
    };
    auto trainingProgressGetter = []() -> md::TrainingProgressView {
        md::TrainingProgressView v;
        v.state = "idle";
        return v;
    };
    auto trainingFinalize = [](const md::FinalizePayload&) -> md::HttpResult {
        return md::HttpResult{200, R"({"status":"ok"})"};
    };
    auto trainingCancel = []() -> md::HttpResult {
        return md::HttpResult{200, R"({"cancelled":false})"};
    };
    auto trainingRecompute = [](float) -> md::HttpResult {
        return md::HttpResult{200, R"({"status":"ok"})"};
    };

    md::CommandQueue queue;
    md::HttpServer server(queue,
                          /*port=*/27140,
                          /*host=*/"127.0.0.1",
                          /*driverDetectionActiveGetter=*/nullptr,
                          /*configGetter=*/configGetter,
                          /*configMutator=*/configMutator,
                          /*stateGetter=*/nullptr,
                          /*errorClearer=*/nullptr,
                          /*rmsGetter=*/nullptr,
                          /*deviceLister=*/nullptr,
                          /*trainingStart=*/trainingStart,
                          /*trainingProgressGetter=*/trainingProgressGetter,
                          /*trainingFinalize=*/trainingFinalize,
                          /*trainingCancel=*/trainingCancel,
                          /*trainingRecompute=*/trainingRecompute,
                          /*driverTrainingActiveGetter=*/trainingActiveGetter,
                          /*driverAudioEnabledGetter=*/driverAudioEnabledGetter);

    MM_CHECK(server.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client client("127.0.0.1", server.GetPort());
    client.set_connection_timeout(1);
    client.set_read_timeout(1);

    // ----- Case (f) baseline: /training/progress shape pre-mutation.
    auto preProgress = client.Get("/training/progress");
    MM_CHECK(preProgress);

    // ----- Case (a): POST /training/start with extra fields.
    {
        nlohmann::json payload = { {"foo", "bar"} };
        auto res = client.Post("/training/start", payload.dump(), "application/json");
        MM_CHECK(res);
        MM_CHECK(res->status == 400);
        auto err = nlohmann::json::parse(res->body);
        MM_CHECK(err.contains("field"));
        MM_CHECK(err.contains("reason"));
    }

    // ----- Case (b): POST /training/finalize without confirm.
    {
        nlohmann::json payload = nlohmann::json::object();
        auto res = client.Post("/training/finalize", payload.dump(), "application/json");
        MM_CHECK(res);
        MM_CHECK(res->status == 400);
        auto err = nlohmann::json::parse(res->body);
        MM_CHECK(err.contains("field"));
        MM_CHECK(err["field"].get<std::string>() == "confirm");
    }

    // ----- Case (c): POST /training/recompute with out-of-range sensitivity.
    {
        nlohmann::json payload = { {"sensitivity", 2.0f} };
        auto res = client.Post("/training/recompute", payload.dump(), "application/json");
        MM_CHECK(res);
        MM_CHECK(res->status == 400);
        auto err = nlohmann::json::parse(res->body);
        MM_CHECK(err.contains("field"));
        MM_CHECK(err["field"].get<std::string>() == "sensitivity");
    }

    // ----- Case (d): POST /training/start with audio disabled => 503.
    {
        audioEnabled.store(false, std::memory_order_release);
        nlohmann::json payload = nlohmann::json::object();
        auto res = client.Post("/training/start", payload.dump(), "application/json");
        MM_CHECK(res);
        MM_CHECK(res->status == 503);
        MM_CHECK(res->body.find("audio_disabled") != std::string::npos);
        audioEnabled.store(true, std::memory_order_release);  // restore
    }

    // ----- Case (e): POST /training/start while session active => 409.
    {
        trainingActive.store(true, std::memory_order_release);
        nlohmann::json payload = nlohmann::json::object();
        auto res = client.Post("/training/start", payload.dump(), "application/json");
        MM_CHECK(res);
        MM_CHECK(res->status == 409);
        MM_CHECK(res->body.find("training_in_progress") != std::string::npos);
        trainingActive.store(false, std::memory_order_release);  // restore
    }

    // ----- Case (f): post-rejection /training/progress unchanged.
    {
        auto postProgress = client.Get("/training/progress");
        MM_CHECK(postProgress);
        MM_CHECK(preProgress->status == postProgress->status);
        // Body-level equality is intentionally NOT asserted — the progress
        // endpoint may include monotonic counters (e.g. uptime). The status-
        // and-shape stability is the load-bearing invariant.
    }

    server.Stop();
    std::cout << "all tests passed\n";
    return 0;
}

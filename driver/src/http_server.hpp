/**
 * @file http_server.hpp
 * @brief HTTP server for the MicMap sidecar driver.
 *
 * Listens on localhost for commands from the MicMap app and enqueues them
 * onto a CommandQueue for RunFrame to drain. No OpenVR driver API is ever
 * called from the HTTP thread (SVR-05).
 */

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <chrono>       // P8 D-17: deviceCacheLastFetch_ steady_clock::time_point
#include <memory>
#include <mutex>        // P8 D-17: deviceCacheMu_ serializes the 1 s cache refill
#include <functional>   // P7 D-09: std::function<bool()> driverDetectionActiveGetter ctor param
#include <optional>     // P9 09-02: TrainingProgressView.thresholds_preview / last_error
#include <vector>       // P8 D-17: deviceLister callback returns std::vector<DeviceInfo>

#include "device_info.hpp"  // P8 D-17 / IPC-03: DeviceInfo struct used in deviceLister callback

// Forward declare httplib types to avoid including the header here
namespace httplib {
    class Server;
}

// Forward declarations for callback signatures.
namespace micmap::core { struct AppConfig; }

namespace micmap::driver {

// Forward declaration
class CommandQueue;
struct DriverState;     // P8 D-23: full type lives in driver_state.hpp; only the
                        // ctor signature needs the forward decl here.
struct FinalizePayload; // P9 09-02: full def in settings_validator.hpp; the .cpp
                        // includes that header — header-side forward decl keeps
                        // the public .hpp surface free of the json transitive
                        // include for non-driver consumers.

// =====================================================================
// P9 09-02 — wire types for the training HTTP layer.
//
// HttpResult is the small envelope returned by the per-route DeviceProvider
// callbacks; the HttpServer route handler copies status + body verbatim onto
// the httplib::Response. ThresholdsPreviewView / TrainingProgressView mirror
// the driver-side TrainingSession::ProgressSnapshot wire shape so the
// /training/progress route handler can serialize without dragging
// training_session.hpp into the HttpServer ctor surface.
// =====================================================================

struct HttpResult {
    int         status{200};
    std::string body;          ///< Already-serialized JSON or short envelope.
};

struct ThresholdsPreviewView {
    float sensitivity{0.0f};
    float energy_threshold{0.0f};
    struct {
        float  mean{0.0f};
        float  stddev{0.0f};
        size_t size{0};
    } spectral_profile_summary;
};

struct TrainingProgressView {
    size_t                                  samples_collected{0};
    size_t                                  target{100};
    std::optional<ThresholdsPreviewView>    thresholds_preview;
    std::string                             state;        ///< "collecting" | "computing" | "ready" | "cancelled" | "finalized" | "idle"
    std::optional<std::string>              last_error;
};

/**
 * @brief HTTP server for receiving press/release commands.
 *
 * Endpoints:
 *   POST /button  -- JSON {"state":"down"|"up"}, enqueues on CommandQueue.
 *   GET  /health  -- liveness + driver-detection-active probe. P7 D-09: the
 *                    response JSON now includes a `driver_detection_active`
 *                    boolean reflecting the live state of the driver-side
 *                    detection thread (true iff the getter passed at ctor
 *                    time reports {flag enabled AND audio worker alive AND
 *                    detection runner alive AND running}). DriverClient polls
 *                    this field and suppresses its own client-side trigger
 *                    when true (P7 D-10), enabling the v1.5/v1.6 phased
 *                    migration coexistence. Field hardcoded to false when no
 *                    getter was supplied at construction (test code, legacy
 *                    callers). The entire field + scaffolding is deleted in
 *                    P10 per D-12.
 *   GET  /port    -- numeric listening port as text.
 *   GET  /status  -- minimal status JSON (no driver-state coupling).
 */
class HttpServer {
public:
    /**
     * @brief Construct HTTP server.
     * @param queue Reference to the CommandQueue that POST /button pushes to.
     * @param port  Starting port (default: 27015; retry up to 27025).
     * @param host  Bind host (default: 127.0.0.1 — localhost only).
     * @param driverDetectionActiveGetter P7 D-09: optional callback returning
     *        the live `driver_detection_active` value reported on /health.
     *        Default `nullptr` keeps existing call sites + test code compiling
     *        unchanged; when null the /health field is hardcoded to false.
     *        Read at REQUEST TIME (NOT cached at construction) so it reflects
     *        the live driver lifecycle as the detection thread starts/stops.
     *        DeviceProvider passes a lambda capturing `this` + the
     *        detectionRunner_/audioWorker_/driverDetectionEnabled_ members.
     */
    /**
     * @brief Phase 8 ctor expansion (D-23 / D-24 / IPC-01..04 / IPC-07).
     *
     * Six new optional callbacks (one per route family). All default to
     * nullptr so existing test code + the v1.5 callsites compile unchanged
     * until DeviceProvider supplies them. Positional ordering matches the
     * Wave 0 RED scaffolds in tests/driver/get_*_test.cpp + state_clear_error_test.cpp:
     *
     *   driverDetectionActiveGetter (P7 D-09 — retained verbatim until P10)
     *   configGetter   (GET /settings — atomic-shared_ptr load on AppConfig)
     *   configMutator  (PUT /settings — bodies land in 08-04; ctor param ships now)
     *   stateGetter    (GET /state    — atomic-shared_ptr load on DriverState)
     *   errorClearer   (POST /state/clear-error — body lands in 08-04; ctor param ships now)
     *   rmsGetter      (GET /telemetry/level — atomic<float> load on AudioWorker)
     *   deviceLister   (GET /devices  — DeviceProvider's 1 s cache lambda)
     *
     * SVR-05 / D-24 / Pitfall 3: every callback runs on the HTTP thread; none
     * may call OpenVR API surface or push to CommandQueue. Lints
     * AssertHttpServerNoVrApi + AssertHttpServerLocalhostOnly enforce these
     * invariants at ctest time.
     */
    /**
     * @brief Phase 9 09-02 ctor expansion (D-07 / D-09 / D-13 / D-15 / D-18 / D-22 / D-40).
     *
     * Seven new optional callbacks (one per training route + two /health-field
     * getters). All default to nullptr so existing callsites + Wave 0 RED test
     * scaffolds compile unchanged until DeviceProvider supplies them. The new
     * callbacks are appended at the END of the existing parameter list per
     * 09-02-PLAN.md Task 2:
     *
     *   trainingStart              POST /training/start  — D-09 single-instance
     *                              + D-40 audio_disabled gate (callback returns
     *                              an HttpResult so the route hander forwards
     *                              status + body verbatim).
     *   trainingProgressGetter     GET  /training/progress — D-22 wire shape.
     *                              Lock-free read. Pulls from TrainingSession
     *                              ::snapshot inside DeviceProvider.
     *   trainingFinalize           POST /training/finalize — D-15/D-16 validated
     *                              body; persist + in-memory profile refresh
     *                              via training_io::saveTrainingFile.
     *   trainingCancel             POST /training/cancel  — D-13 idempotent.
     *   trainingRecompute          POST /training/recompute — D-18..D-21
     *                              ready-only sensitivity adjust.
     *   driverTrainingActiveGetter D-07 — /health.driver_training_active
     *                              (parallels driverDetectionActiveGetter).
     *   driverAudioEnabledGetter   warning fix 09-03 T2 — /health
     *                              .driver_audio_enabled (proactive disable
     *                              contract; client gates Train Pattern button
     *                              on this field plus driver_loaded).
     *
     * SVR-05 / Pitfall 3: every callback runs on the HTTP thread; none may call
     * OpenVR API surface or push to CommandQueue. AssertHttpServerNoVrApi +
     * AssertHttpServerLocalhostOnly lints stay GREEN.
     */
    explicit HttpServer(CommandQueue& queue,
                        int port = 27015,
                        const std::string& host = "127.0.0.1",
                        std::function<bool()> driverDetectionActiveGetter = nullptr,
                        std::function<std::shared_ptr<const core::AppConfig>()> configGetter = nullptr,
                        std::function<bool(const core::AppConfig&)>             configMutator = nullptr,
                        std::function<std::shared_ptr<const DriverState>()>     stateGetter = nullptr,
                        std::function<void()>                                   errorClearer = nullptr,
                        std::function<float()>                                  rmsGetter = nullptr,
                        std::function<std::vector<DeviceInfo>()>                deviceLister = nullptr,
                        // P9 09-02 — training endpoint callbacks + /health field getters.
                        std::function<HttpResult()>                             trainingStart = nullptr,
                        std::function<TrainingProgressView()>                   trainingProgressGetter = nullptr,
                        std::function<HttpResult(const FinalizePayload&)>       trainingFinalize = nullptr,
                        std::function<HttpResult()>                             trainingCancel = nullptr,
                        std::function<HttpResult(float)>                        trainingRecompute = nullptr,
                        std::function<bool()>                                   driverTrainingActiveGetter = nullptr,
                        std::function<bool()>                                   driverAudioEnabledGetter = nullptr);

    ~HttpServer();

    bool Start();
    void Stop();

    bool IsRunning() const { return running_; }
    int GetPort() const { return port_; }
    const std::string& GetHost() const { return host_; }

private:
    void SetupRoutes();
    void ServerThread();

    CommandQueue& queue_;
    int port_;
    std::string host_;

    std::unique_ptr<httplib::Server> server_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};

    // P7 D-09: getter polled by the /health route at request time. Set by
    // DeviceProvider via the 4th ctor arg; nullptr when constructed without
    // detection wiring (test code or legacy v1.5 callers — field is then
    // hardcoded to false). Deleted in P10 per D-12.
    std::function<bool()> driverDetectionActiveGetter_;

    // P8 D-23 / D-24 / IPC-01..04 — read-side and write-side callbacks for the
    // 4 new GET routes (this plan) and the 2 new write routes (08-04). Each
    // is checked for null before use (defensive default-empty payload when
    // unset). Lock-free atomic-snapshot loads via the shared_ptr<const T>
    // factories in DeviceProvider (configGetter / stateGetter).
    std::function<std::shared_ptr<const core::AppConfig>()> configGetter_;
    std::function<bool(const core::AppConfig&)>             configMutator_;
    std::function<std::shared_ptr<const DriverState>()>     stateGetter_;
    std::function<void()>                                   errorClearer_;
    std::function<float()>                                  rmsGetter_;
    std::function<std::vector<DeviceInfo>()>                deviceLister_;

    // P8 D-17 / IPC-03 — 1 s cache for /devices. Applied uniformly inside
    // HttpServer (regardless of who supplies the underlying lister) so
    // poll storms are absorbed even in test scaffolds that pass a raw
    // deviceLister directly. The mutex serializes refills; cache hits
    // do an unguarded read of the std::vector copy returned to the caller.
    mutable std::mutex                          deviceCacheMu_;
    std::chrono::steady_clock::time_point       deviceCacheLastFetch_{};
    std::vector<DeviceInfo>                     deviceCache_;
    bool                                        deviceCacheSeeded_{false};

    // P9 09-02 — training endpoint callbacks + /health field getters. All
    // run on the HTTP thread; null-checked at request time so test scaffolds
    // that wire only a subset see the documented "unavailable" 503/idle
    // defaults rather than a crash.
    std::function<HttpResult()>                             trainingStart_;
    std::function<TrainingProgressView()>                   trainingProgressGetter_;
    std::function<HttpResult(const FinalizePayload&)>       trainingFinalize_;
    std::function<HttpResult()>                             trainingCancel_;
    std::function<HttpResult(float)>                        trainingRecompute_;
    std::function<bool()>                                   driverTrainingActiveGetter_;
    std::function<bool()>                                   driverAudioEnabledGetter_;
};

} // namespace micmap::driver

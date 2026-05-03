/**
 * @file audio_worker.cpp
 * @brief Implementation of the driver-side audio capture worker.
 *
 * The worker thread owns CoInitializeEx(MTA) for its own apartment, then
 * constructs WASAPIAudioCapture (whose inner CoInitializeEx returns S_FALSE
 * — same apartment), wires an RMS-logging audio callback, waits on the
 * shutdown CV, and tears down in reverse order on signal.
 *
 * Pitfalls mitigated:
 *   - Pitfall 1: COM apartment mismatch — ::CoInitializeEx(MTA) runs on the
 *     worker thread itself, not on DeviceProvider::Init's caller thread.
 *     D-04 apartment-trick: do NOT move WASAPIAudioCapture construction back
 *     into DeviceProvider::Init — its inner CoInitializeEx would land on
 *     the wrong thread.
 *   - Pitfall 3: no OpenVR API call from the audio thread. Only DriverLog
 *     (driver-host service interface from driver_log.hpp) is used.
 *   - Pitfall 4: reverse-order teardown ALL on the worker thread —
 *     capture_->stopCapture() then capture_.reset() (which runs
 *     ~WASAPIAudioCapture: IMMNotificationClient unregister + ComPtr
 *     release + CoUninitialize) then our own ::CoUninitialize().
 *   - Pitfall 13: weak_ptr<State> + atomic<bool> alive in the callback
 *     so in-flight WASAPI / IMMNotificationClient callbacks bail before
 *     touching destructed state.
 *
 * D-13 2 s watchdog: Stop() polls thread_finished_; on overrun, log +
 * thread_.detach() last-resort to avoid blocking vrserver.exe shutdown
 * (T3 mitigation).
 */

#include "audio_worker.hpp"
#include "driver_log.hpp"

// P5 link-only restriction explicitly lifted in P6 — driver TUs may
// now #include shared-lib headers (driver/CMakeLists.txt:79 comment).
#include <micmap/audio/audio_capture.hpp>

#ifdef _WIN32
#include <Windows.h>     // CoInitializeEx, CoUninitialize, RPC_E_CHANGED_MODE
#include <Objbase.h>     // (CoInitializeEx flags / RPC_E_CHANGED_MODE on some SDKs)
#endif

#include <algorithm>
#include <chrono>
#include <cmath>

namespace micmap::driver {

namespace {

// ~1 s @ 10 ms WASAPI shared-mode period (D-08). After kRmsBudget RMS
// log lines have been emitted, the callback continues to drain frames
// (so WASAPI's buffer never overflows) but skips DriverLog writes —
// log-flood mitigation, T6 / Pitfall 6 carry.
constexpr uint32_t kRmsBudget = 100;

// 2 s shutdown watchdog (D-13) matching the v1.5 VREvent_Quit precedent.
constexpr auto kShutdownWatchdog = std::chrono::seconds(2);
constexpr auto kWatchdogPoll     = std::chrono::milliseconds(25);

} // namespace

AudioWorker::AudioWorker()
    : state_(std::make_shared<State>()) {
    DriverLog("MicMap: AudioWorker created\n");
}

AudioWorker::~AudioWorker() {
    Stop();
}

bool AudioWorker::Start() {
    if (running_.load(std::memory_order_acquire)) {
        DriverLog("MicMap: AudioWorker::Start called while already running\n");
        return thread_.joinable();
    }
    shutdown_.store(false, std::memory_order_release);
    thread_finished_.store(false, std::memory_order_release);
    if (state_) {
        state_->alive.store(true, std::memory_order_release);
        state_->rms_logs_emitted.store(0, std::memory_order_release);
        state_->frames_seen.store(0, std::memory_order_release);
    }
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&AudioWorker::ThreadEntry, this);
    DriverLog("MicMap: AudioWorker thread spawned\n");
    return thread_.joinable();
}

void AudioWorker::Stop() {
    if (!thread_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }
    if (state_) {
        state_->alive.store(false, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_.store(true, std::memory_order_release);
    }
    cv_.notify_all();

    // 2 s watchdog (D-13) matching v1.5 VREvent_Quit precedent. Poll
    // thread_finished_ every 25 ms; on overrun log + detach so we never
    // block vrserver.exe shutdown (T3 mitigation).
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + kShutdownWatchdog;
    while (!thread_finished_.load(std::memory_order_acquire) &&
           clock::now() < deadline) {
        std::this_thread::sleep_for(kWatchdogPoll);
    }
    if (thread_finished_.load(std::memory_order_acquire)) {
        thread_.join();
        DriverLog("MicMap: AudioWorker thread joined cleanly\n");
    } else {
        DriverLog("MicMap: audio worker did not exit within 2 s watchdog "
                  "- detaching (T3 mitigation)\n");
        thread_.detach();
    }
    running_.store(false, std::memory_order_release);
}

void AudioWorker::ThreadEntry(AudioWorker* self) {
    self->RunWorker();
}

void AudioWorker::RunWorker() {
#ifdef _WIN32
    // Pitfall 1 / D-04 / D-05: own the COM apartment for this thread
    // BEFORE constructing WASAPIAudioCapture (whose inner CoInitializeEx
    // will then return S_FALSE — already initialized, same apartment).
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        // D-06 / SC2: distinct log path. The literal log line below is
        // grep-asserted by 06-VALIDATION.md and the plan acceptance
        // criteria — keep the wording byte-stable.
        DriverLog("MicMap: audio worker thread already in another COM apartment "
                  "(RPC_E_CHANGED_MODE = 0x80010106) \xe2\x80\x94 bailing out\n");
        if (state_) state_->alive.store(false, std::memory_order_release);
        thread_finished_.store(true, std::memory_order_release);
        return;
    }
    if (FAILED(hr) && hr != S_FALSE) {
        DriverLog("MicMap: audio worker CoInitializeEx failed hr=0x%08X\n",
                  static_cast<unsigned>(hr));
        if (state_) state_->alive.store(false, std::memory_order_release);
        thread_finished_.store(true, std::memory_order_release);
        return;
    }
    DriverLog("MicMap: audio worker thread COM apartment = MTA (hr=0x%08X)\n",
              static_cast<unsigned>(hr));
#else
    // Non-Windows: no COM. The audio capture stub on this platform is a
    // no-op anyway, but we keep the lifecycle skeleton symmetrical so the
    // headless test still exercises Start/Stop on any host.
    DriverLog("MicMap: audio worker thread started (non-Windows stub)\n");
#endif

    // D-04 apartment-trick — load-bearing comment:
    // The factory below ultimately runs WASAPIAudioCapture's ctor, which
    // calls CoInitializeEx AGAIN; it returns S_FALSE (already-init same
    // apartment) and the existing comInitialized_ logic accepts it. Do
    // NOT move this construction back into DeviceProvider::Init - it
    // would land COM init on the wrong thread and violate Pitfall 1.
    auto capture = micmap::audio::createWASAPICapture();
    if (!capture) {
        DriverLog("MicMap: audio worker createWASAPICapture returned null - "
                  "bailing out\n");
        if (state_) state_->alive.store(false, std::memory_order_release);
#ifdef _WIN32
        ::CoUninitialize();
#endif
        thread_finished_.store(true, std::memory_order_release);
        return;
    }
    capture_ = std::move(capture);

    // RMS callback wired with weak_ptr alive-flag (Pitfall 13 / D-15 / D-16).
    // The audio callback fires on the WASAPI internal capture thread (per
    // audio_capture.cpp captureLoop). The weak_ptr lock + alive check at
    // the head guarantees no UAF after Stop() flips alive=false.
    std::weak_ptr<State> weak = state_;
    capture_->setAudioCallback(
        [weak](const float* samples, size_t count) {
            auto sp = weak.lock();
            if (!sp || !sp->alive.load(std::memory_order_acquire)) {
                return;
            }
            sp->frames_seen.fetch_add(1, std::memory_order_relaxed);
            double sumSq = 0.0;
            for (size_t i = 0; i < count; ++i) {
                const double s = static_cast<double>(samples[i]);
                sumSq += s * s;
            }
            const float rms = static_cast<float>(
                std::sqrt(sumSq / static_cast<double>(std::max<size_t>(count, 1))));
            const uint32_t emitted =
                sp->rms_logs_emitted.fetch_add(1, std::memory_order_relaxed);
            if (emitted < kRmsBudget) {
                DriverLog("MicMap audio: rms[%u]=%.6f\n", emitted, rms);
            }
            // After budget: continue draining frames so WASAPI buffer never
            // overflows, but skip DriverLog writes (D-08).
        });

    if (!capture_->startCapture()) {
        DriverLog("MicMap: audio worker capture_->startCapture() failed - "
                  "bailing out\n");
        // Reverse-order teardown even on the start-failure path
        // (Pitfall 4 — same thread that did register/CoInit unregisters).
        capture_.reset();
#ifdef _WIN32
        ::CoUninitialize();
#endif
        if (state_) state_->alive.store(false, std::memory_order_release);
        thread_finished_.store(true, std::memory_order_release);
        return;
    }
    DriverLog("MicMap: audio worker capture started\n");

    // Wait on shutdown signal. cv_.wait re-checks the predicate under the
    // mutex, so spurious wakeups don't escape the loop.
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] {
            return shutdown_.load(std::memory_order_acquire);
        });
    }

    // Reverse-order teardown — ALL on this worker thread (Pitfall 4 / D-13).
    // ~WASAPIAudioCapture (audio_capture.cpp:222-235) unregisters the
    // IMMNotificationClient, releases ComPtrs, and calls CoUninitialize on
    // the capture's own balanced count - all on the same thread that did
    // the register, automatically.
    if (capture_) {
        capture_->stopCapture();
        capture_.reset();
    }
#ifdef _WIN32
    ::CoUninitialize();   // matches our own CoInitializeEx above
#endif
    DriverLog("MicMap: audio worker thread exiting cleanly\n");
    if (state_) state_->alive.store(false, std::memory_order_release);
    thread_finished_.store(true, std::memory_order_release);
}

} // namespace micmap::driver

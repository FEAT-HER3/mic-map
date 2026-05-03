/**
 * @file audio_worker.hpp
 * @brief Driver-side audio capture worker (Phase 6 spike).
 *
 * Owns its own std::thread, calls CoInitializeEx(MTA) inside the thread
 * entry, and constructs WASAPIAudioCapture on the worker thread so its
 * inner CoInitializeEx lands on the same MTA apartment (Pitfall 1, D-04).
 *
 * No OpenVR API surface is touched from the worker thread (D-07 / Pitfall 3
 * / SVR-05). The only OpenVR-shaped surface allowed is DriverLog (the
 * documented thread-safe driver-host service interface — and that lives in
 * driver_log.hpp, not in this translation unit).
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

// Forward-declare to avoid pulling micmap/audio/* into the header.
namespace micmap::audio { class IAudioCapture; }

namespace micmap::driver {

/**
 * @brief Lifecycle owner for the driver-side audio capture thread.
 *
 * Construction is cheap (no thread spawn). Start() spawns the worker
 * thread, which:
 *   1. Calls ::CoInitializeEx(nullptr, COINIT_MULTITHREADED) (Pitfall 1).
 *      Distinct handling for RPC_E_CHANGED_MODE per D-06 / SC2.
 *   2. Constructs IAudioCapture on the worker thread (D-04 apartment-trick:
 *      WASAPIAudioCapture's inner CoInitializeEx then returns S_FALSE,
 *      same apartment).
 *   3. Wires an RMS-budgeted audio callback with a weak_ptr<State>
 *      alive-flag check (Pitfall 13 / D-15 / D-16).
 *   4. Waits on a shutdown condition variable.
 *   5. On signal, performs reverse-order teardown ALL on the worker
 *      thread: capture_->stopCapture() -> capture_.reset() ->
 *      ::CoUninitialize() (Pitfall 4 / D-13).
 *
 * Stop() / ~AudioWorker() implements a 2 s watchdog (D-13) matching the
 * v1.5 VREvent_Quit shutdown precedent: poll thread_finished_ atomic;
 * on overrun, log + thread_.detach() (last-resort to avoid blocking
 * vrserver.exe shutdown — T3 mitigation).
 */
class AudioWorker {
public:
    AudioWorker();
    ~AudioWorker();

    AudioWorker(const AudioWorker&) = delete;
    AudioWorker& operator=(const AudioWorker&) = delete;

    /// Spawns the worker thread. Returns true if joinable.
    bool Start();

    /// Idempotent shutdown; called by destructor. 2 s watchdog (D-13).
    void Stop();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Pitfall 13 alive-flag mitigation state.
     *
     * Held by std::shared_ptr; the audio callback captures a weak_ptr
     * and locks + checks alive before any state mutation, so callbacks
     * scheduled by WASAPI's internal capture thread (and any
     * IMMNotificationClient callbacks routed through the capture's
     * onDeviceRemoved hook) bail cleanly when the worker is shutting
     * down (D-15 / D-16).
     */
    struct State {
        std::atomic<bool>     alive{true};
        std::atomic<uint32_t> rms_logs_emitted{0};
        std::atomic<uint32_t> frames_seen{0};
    };

    /**
     * @brief Test-only accessor for State, used by
     *        tests/driver/audio_worker_lifecycle_headless.cpp case 3
     *        (Pitfall 13 alive-before-shutdown ordering check).
     *
     * Test-only — may be deleted in P7 if unused. Does NOT widen the
     * production API surface (Start/Stop/IsRunning are the production
     * surface; State exposure is a zero-cost convenience).
     */
    std::shared_ptr<State> state_for_test() const { return state_; }

private:
    static void ThreadEntry(AudioWorker* self);
    void RunWorker();

    std::shared_ptr<State>                        state_;
    std::unique_ptr<micmap::audio::IAudioCapture> capture_;  // owned by worker thread
    std::thread                                   thread_;
    std::mutex                                    mu_;
    std::condition_variable                       cv_;
    std::atomic<bool>                             shutdown_{false};
    std::atomic<bool>                             running_{false};
    std::atomic<bool>                             thread_finished_{false};
};

} // namespace micmap::driver

/**
 * @file device_provider.cpp
 * @brief Implementation of the MicMap HMD sidecar device provider.
 *
 * Sidecar architecture (Phase 1 / plan 01-03):
 *   - Does NOT register any tracked device (zero virtual controllers).
 *   - Creates /input/system/click on the HMD property container (index 0).
 *   - Drains a CommandQueue populated by the HTTP thread.
 *   - Enforces a 100 ms min-hold + 5 s max-hold safety on the driver side.
 *   - Handles VREvent_TrackedDeviceDeactivated for sleep/wake recreation.
 *   - Logs via DriverLog + VRInputErrorName on every failure (SVR-10).
 */

#include "device_provider.hpp"
#include "micmap/bindings/bindings_patcher.hpp"
#include "command_queue.hpp"
#include "http_server.hpp"
#include "audio_worker.hpp"      // P6 — AudioWorker class for conditional Init/Cleanup
#include "detection_runner.hpp"  // P7 D-19 — DetectionRunner full type for ctor/dtor
#include "driver_log.hpp"
#include "vr_error.hpp"

#include <openvr_driver.h>

// P7 REVIEW WR-01: poll-with-timeout while AudioWorker publishes the
// WASAPI-negotiated sample rate (worker thread runs startCapture
// asynchronously; we wait briefly before falling back).
#include <chrono>
#include <thread>

using namespace vr;
using micmap::driver::VRInputErrorName;

// Phase 4 D-10: wrap DriverLog into the shared-lib LogSink shape. Keeps
// driver-side vrserver.txt output byte-identical to the pre-lift behavior.
static void driverLogSink(const char* msg) {
    DriverLog("%s", msg);
}

#ifndef MICMAP_DRIVER_VERSION
#define MICMAP_DRIVER_VERSION "0.0.0"
#endif

namespace micmap::driver {

// Interface versions this provider speaks. Sidecar mode: we only claim the
// server-device-provider interface; tracked-device-server is unused because
// no tracked device is registered.
// IN-05: Assumes IServerTrackedDeviceProvider_Version is a string literal
// (current OpenVR SDK contract -- it expands via #define to a bare "..."
// literal with static storage duration, so the returned array is valid for
// the process lifetime). If Valve ever redefines it as a constexpr
// std::string_view or similar non-literal, this array must be rebuilt
// per-call to avoid storing a dangling pointer.
static const char* const k_InterfaceVersions[] = {
    IServerTrackedDeviceProvider_Version,
    nullptr
};

DeviceProvider::DeviceProvider() = default;

DeviceProvider::~DeviceProvider() {
    Cleanup();
}

EVRInitError DeviceProvider::Init(IVRDriverContext* pDriverContext) {
    // P7 07-04 Rule-2 defensive guard: VR_INIT_SERVER_DRIVER_CONTEXT(nullptr)
    // SEGFAULTs because COpenVRDriverContext::VRSettings() lazy-loads via
    // VRDriverContext()->GetGenericInterface(...) — but VRDriverContext() was
    // just assigned nullptr by InitServerDriverContext, so the deref crashes
    // (openvr_driver.h:4228 + 4422). vrserver.exe always passes a real
    // context, but the headless DeviceProviderLifecycleStress test (SC4 /
    // MIG-04 50-cycle audit) calls Init(nullptr) deliberately to exercise
    // the OpenVR-context-teardown lifecycle. Bail-with-error preserves the
    // existing fail-soft contract (initialized_ stays false → Cleanup is a
    // no-op → no leaks across cycles).
    if (!pDriverContext) {
        DriverLog("MicMap: DeviceProvider::Init called with null IVRDriverContext "
                  "— bailing out (headless test path)\n");
        return VRInitError_Init_InvalidInterface;
    }

    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    DriverLog("MicMap driver initializing (sidecar mode)\n");

    // Ensure SteamVR's generic-HMD bindings route /user/head/input/system to
    // dashboard + lasermouse leftclick. Best-effort; logs its own outcome.
    // Takes effect on the NEXT SteamVR start (vrcompositor caches bindings
    // at connect time — already happened before our Init runs).
    (void)micmap::bindings::PatchGenericHmdBindings(driverLogSink);

    commandQueue_ = std::make_unique<CommandQueue>();

    // P7 D-09: pass a getter lambda so /health reports `driver_detection_active`
    // reflecting the LIVE driver state. The lambda captures `this` and reads
    // members AT REQUEST TIME — so even though HttpServer is constructed BEFORE
    // the VRSettings reads + DetectionRunner construction (D-19 ordering), the
    // field correctly transitions false → true once detection actually starts,
    // and back to false during Cleanup. RESEARCH Open Question 1 recommendation
    // (a). Deleted in P10 per D-12.
    auto driverDetectionActiveGetter = [this]() {
        return driverDetectionEnabled_
            && audioWorker_
            && detectionRunner_
            && detectionRunner_->IsRunning();
    };
    httpServer_ = std::make_unique<HttpServer>(
        *commandQueue_,
        /*port=*/27015,
        /*host=*/"127.0.0.1",
        std::move(driverDetectionActiveGetter));
    if (!httpServer_->Start()) {
        DriverLog("MicMap: failed to start HTTP server\n");
        return VRInitError_Driver_Failed;
    }
    DriverLog("MicMap: HTTP server listening on port %d\n", httpServer_->GetPort());

    // D-01: read the flag once, here, on the vrserver thread. Single-read
    // pattern matches v1.5 atomic-config-read shape (Pitfall 11). Default
    // false on UnsetSettingHasNoDefault — SC4 safety net.
    {
        vr::EVRSettingsError err = vr::VRSettingsError_None;
        driverAudioEnabled_ = vr::VRSettings()->GetBool(
            "driver_micmap", "enable_driver_audio", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            driverAudioEnabled_ = false;   // explicit default per D-01 + SC4
            DriverLog("MicMap: enable_driver_audio unset, defaulting to false\n");
        } else if (err != vr::VRSettingsError_None) {
            DriverLog("MicMap: VRSettings GetBool(enable_driver_audio) error=%d\n",
                      static_cast<int>(err));
            driverAudioEnabled_ = false;
        } else {
            DriverLog("MicMap: enable_driver_audio = %s\n",
                      driverAudioEnabled_ ? "true" : "false");
        }
    }

    // D-14: construct AudioWorker LAST so an audio failure does not corrupt
    // the v1.5 trigger path. D-03: when flag is OFF, never construct the
    // worker — no thread, no COM, no WASAPI. Byte-identical to Phase 5.
    if (driverAudioEnabled_) {
        audioWorker_ = std::make_unique<AudioWorker>();
        if (!audioWorker_->Start()) {
            DriverLog("MicMap: AudioWorker::Start failed — continuing without audio\n");
            audioWorker_.reset();   // do NOT fail Init — v1.5 trigger path stays alive
        }
    }

    // P7 D-13: read the 5 new detection_* keys once, here, on the vrserver
    // thread. Single-read pattern matches v1.5 atomic-config-read shape
    // (Pitfall 11) and P6 D-01's enable_driver_audio block above.
    // Default-on-UnsetSettingHasNoDefault per Shared Pattern 2.
    {
        vr::EVRSettingsError err = vr::VRSettingsError_None;

        driverDetectionEnabled_ = vr::VRSettings()->GetBool(
            "driver_micmap", "enable_driver_detection", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            driverDetectionEnabled_ = false;
            DriverLog("MicMap: enable_driver_detection unset, defaulting to false\n");
        } else if (err != vr::VRSettingsError_None) {
            DriverLog("MicMap: VRSettings GetBool(enable_driver_detection) error=%d\n",
                      static_cast<int>(err));
            driverDetectionEnabled_ = false;
        } else {
            DriverLog("MicMap: enable_driver_detection = %s\n",
                      driverDetectionEnabled_ ? "true" : "false");
        }

        // P7 REVIEW IN-03: distinguish UnsetSettingHasNoDefault from other
        // errors so logs match the precedent set by the enable_driver_audio
        // block above. Same fail-soft default-on-error semantics; just two
        // log shapes instead of one collapsed "unset/error" message.
        err = vr::VRSettingsError_None;
        detectionDefaults_.sensitivity = vr::VRSettings()->GetFloat(
            "driver_micmap", "detection_sensitivity", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            detectionDefaults_.sensitivity = 0.7f;
            DriverLog("MicMap: detection_sensitivity unset, defaulting to 0.7\n");
        } else if (err != vr::VRSettingsError_None) {
            detectionDefaults_.sensitivity = 0.7f;
            DriverLog("MicMap: VRSettings GetFloat(detection_sensitivity) error=%d, defaulting to 0.7\n",
                      static_cast<int>(err));
        } else {
            DriverLog("MicMap: detection_sensitivity = %.3f\n",
                      detectionDefaults_.sensitivity);
        }

        err = vr::VRSettingsError_None;
        detectionDefaults_.threshold = vr::VRSettings()->GetFloat(
            "driver_micmap", "detection_threshold", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            detectionDefaults_.threshold = 0.6f;
            DriverLog("MicMap: detection_threshold unset, defaulting to 0.6\n");
        } else if (err != vr::VRSettingsError_None) {
            detectionDefaults_.threshold = 0.6f;
            DriverLog("MicMap: VRSettings GetFloat(detection_threshold) error=%d, defaulting to 0.6\n",
                      static_cast<int>(err));
        } else {
            DriverLog("MicMap: detection_threshold = %.3f\n",
                      detectionDefaults_.threshold);
        }

        err = vr::VRSettingsError_None;
        detectionDefaults_.cooldown_ms = vr::VRSettings()->GetInt32(
            "driver_micmap", "detection_cooldown_ms", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            detectionDefaults_.cooldown_ms = 1000;
            DriverLog("MicMap: detection_cooldown_ms unset, defaulting to 1000\n");
        } else if (err != vr::VRSettingsError_None) {
            detectionDefaults_.cooldown_ms = 1000;
            DriverLog("MicMap: VRSettings GetInt32(detection_cooldown_ms) error=%d, defaulting to 1000\n",
                      static_cast<int>(err));
        } else {
            DriverLog("MicMap: detection_cooldown_ms = %d\n",
                      detectionDefaults_.cooldown_ms);
        }

        err = vr::VRSettingsError_None;
        detectionDefaults_.min_duration_ms = vr::VRSettings()->GetInt32(
            "driver_micmap", "detection_min_duration_ms", &err);
        if (err == vr::VRSettingsError_UnsetSettingHasNoDefault) {
            detectionDefaults_.min_duration_ms = 200;
            DriverLog("MicMap: detection_min_duration_ms unset, defaulting to 200\n");
        } else if (err != vr::VRSettingsError_None) {
            detectionDefaults_.min_duration_ms = 200;
            DriverLog("MicMap: VRSettings GetInt32(detection_min_duration_ms) error=%d, defaulting to 200\n",
                      static_cast<int>(err));
        } else {
            DriverLog("MicMap: detection_min_duration_ms = %d\n",
                      detectionDefaults_.min_duration_ms);
        }
    }

    // P7 D-19: construct DetectionRunner LAST, AFTER AudioWorker. Fail-soft
    // when audio is unavailable — log the warning + skip; do NOT fail Init
    // (P6 D-14 fail-soft semantics extended). The v1.5 trigger path stays
    // alive even when detection cannot start.
    if (driverDetectionEnabled_) {
        if (!audioWorker_) {
            DriverLog("MicMap: enable_driver_detection requires enable_driver_audio "
                      "— skipping detection construction\n");
        } else {
            // P7 REVIEW WR-01: read the WASAPI-negotiated sample rate from
            // AudioWorker. The worker thread spawned by audioWorker_->Start()
            // publishes the rate after its own startCapture() succeeds; we
            // poll briefly because Init runs on vrserver's thread and Start
            // is asynchronous. If the rate is still 0 after the poll window
            // (capture has not yet bound, or failed silently), fall back to
            // 48000 with a clear log so the assumption is auditable in
            // vrserver.txt instead of buried in code.
            uint32_t sampleRate = 0;
            {
                using clock = std::chrono::steady_clock;
                const auto deadline = clock::now() + std::chrono::milliseconds(500);
                while (clock::now() < deadline) {
                    sampleRate = audioWorker_->sample_rate();
                    if (sampleRate != 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            if (sampleRate == 0) {
                sampleRate = 48000;
                DriverLog("MicMap: WARNING - AudioWorker did not publish a sample rate "
                          "within 500 ms; FFT detector built against assumed %u Hz "
                          "(actual device rate may differ; trained profile may not match)\n",
                          sampleRate);
            } else {
                DriverLog("MicMap: detection sampleRate=%u Hz (live from AudioWorker)\n",
                          sampleRate);
            }
            detectionRunner_ = std::make_unique<DetectionRunner>(
                audioWorker_->ring(),
                *commandQueue_,
                sampleRate,
                detectionDefaults_);
            if (!detectionRunner_->Start()) {
                DriverLog("MicMap: DetectionRunner::Start failed — continuing without detection\n");
                detectionRunner_.reset();   // do NOT fail Init
            } else {
                // Attach the runner pointer so the audio callback can wake it
                // via NotifyOne(). Order matters: SetDetectionRunner ONLY after
                // a successful Start() so the audio cb never dereferences a
                // half-constructed runner.
                audioWorker_->SetDetectionRunner(detectionRunner_.get());
                DriverLog("MicMap: DetectionRunner active (sampleRate=%u)\n", sampleRate);
            }
        }
    }

    initialized_ = true;
    return VRInitError_None;
}

void DeviceProvider::Cleanup() {
    if (!initialized_) {
        return;
    }

    DriverLog("MicMap driver cleaning up...\n");

    // P7 REVIEW IN-01: clear the audio callback's runner pointer BEFORE
    // resetting detectionRunner_. The audio callback already guards via
    // weak_ptr<State> + state->alive, and the construction order in Init
    // (audio worker first, then runner) means today's teardown is safe even
    // without this clear. But pairing the SetDetectionRunner(runner.get())
    // call in Init with a SetDetectionRunner(nullptr) here makes the intent
    // self-evident locally and survives any future reordering.
    if (audioWorker_) {
        audioWorker_->SetDetectionRunner(nullptr);
    }

    // P7 D-20 step 1: detectionRunner_.reset() FIRST (strict reverse
    // construction order — Pitfall 4). DetectionRunner's destructor signals
    // shutdown_, notify_all, joins the detection thread with a 2 s watchdog.
    // The detection thread exits cleanly BEFORE AudioWorker stops feeding the
    // ring — DetectionRunner holds a reference to AudioWorker's ring_, which
    // becomes a dangling reference if AudioWorker dies first.
    // Reverse-order = correctness. SC4 / MIG-04 50-cycle stress
    // (DeviceProviderLifecycleStress) verifies handle delta <= 5.
    if (detectionRunner_) {
        detectionRunner_.reset();
    }

    // D-13 step 2: AudioWorker NEXT (existing P6 reverse-order step). Now
    // happens AFTER detectionRunner_.reset() so the detection thread is
    // already joined and the ring is no longer being read by anyone.
    // Destructor sets state->alive=false, signals shutdown CV, joins thread
    // with 2 s watchdog. Worker thread itself runs the WASAPI/COM teardown
    // on its own apartment (Pitfall 4 — IMMNotificationClient unregister
    // BEFORE COM Release, both on the same thread that did the register).
    if (audioWorker_) {
        audioWorker_.reset();
    }

    // D-13 step 3-onwards: existing v1.5 sequence unchanged.
    if (httpServer_) {
        httpServer_->Stop();
        httpServer_.reset();
    }
    commandQueue_.reset();

    hSystemClick_ = k_ulInvalidInputComponentHandle;
    state_ = HmdComponentState::NotReady;
    pendingReleaseAt_.reset();
    isPressed_ = false;
    lastWrittenValue_ = false;
    initLogged_ = false;
    loggedAwaitingHmd_ = false;
    profilePropsWritten_ = false;
    driverAudioEnabled_ = false;   // P6 — symmetry with the Init-time read
    driverDetectionEnabled_ = false;            // P7 D-20 — symmetry with Init-time read
    detectionDefaults_ = DetectionConfig{};     // reset cached defaults to construct-time
    initialized_ = false;

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();

    DriverLog("MicMap driver cleanup complete\n");
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return k_InterfaceVersions;
}

void DeviceProvider::RunFrame() {
    // 0. First-frame init log (SVR-10 / Pitfall 11).
    if (!initLogged_) {
        DriverLog("MicMap driver v%s built %s %s - RunFrame starting\n",
                  MICMAP_DRIVER_VERSION, __DATE__, __TIME__);
        initLogged_ = true;
    }

    // 1. Drain OpenVR events. A HMD deactivation must flip us to Invalidated
    //    BEFORE any handle-dependent work this tick (Pitfall 1).
    VREvent_t ev{};
    while (VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev))) {
        if (ev.eventType == VREvent_TrackedDeviceDeactivated
            && ev.trackedDeviceIndex == k_unTrackedDeviceIndex_Hmd) {
            hSystemClick_ = k_ulInvalidInputComponentHandle;
            state_ = HmdComponentState::Invalidated;
            isPressed_ = false;
            lastWrittenValue_ = false;
            pendingReleaseAt_.reset();
            profilePropsWritten_ = false;  // re-arm profile writes on reactivation
            DriverLog("MicMap: HMD deactivated, handle invalidated\n");
        }
    }

    // 2. Create-or-recreate /input/system/click while not Ready.
    if (state_ != HmdComponentState::Ready) {
        auto hmd = VRProperties()->TrackedDeviceToPropertyContainer(
            k_unTrackedDeviceIndex_Hmd);
        if (hmd != k_ulInvalidPropertyContainer) {
            // We intentionally do NOT SetStringProperty(Prop_ControllerType /
            // Prop_InputProfilePath) here. Lighthouse owns the HMD container
            // and its controller_type (e.g. "lighthouse_hmd") wins at binding-
            // resolve time, so our controller_type write has no effect.
            // Meanwhile, setting our own Prop_InputProfilePath_String *does*
            // stick (lighthouse doesn't set it on non-Index HMDs) -- but then
            // SteamVR loads OUR profile, sees its controller_type mismatches
            // the container's, and the binding layer silently drops bindings
            // (including /actions/lasermouse/in/Pointer -- so the head-locked
            // cursor disappears). The dashboard + cursor are wired via the
            // generic_hmd bindings file we patch in bindings_patcher instead.

            auto err = VRDriverInput()->CreateBooleanComponent(
                hmd, "/input/system/click", &hSystemClick_);
            if (err == VRInputError_None) {
                DriverLog("MicMap: /input/system/click created (handle=%llu)\n",
                          static_cast<unsigned long long>(hSystemClick_));
                state_ = HmdComponentState::Ready;
                loggedAwaitingHmd_ = false;   // re-arm for future invalidation cycles
            } else {
                DriverLog("MicMap: CreateBooleanComponent failed: %s (%d)\n",
                          VRInputErrorName(err), static_cast<int>(err));
                hSystemClick_ = k_ulInvalidInputComponentHandle;
            }
        } else if (!loggedAwaitingHmd_) {
            DriverLog("MicMap: awaiting HMD container\n");
            loggedAwaitingHmd_ = true;   // transition-only (D-08)
        }
    }

    // 3. Drain CommandQueue (non-blocking; lock held only inside try_pop).
    //    Each TapCommand writes DOWN immediately and schedules a matching
    //    UP at now + kTapHold. If another tap arrives while one is pending,
    //    the latest tap's DOWN restarts the hold window (the pending release
    //    is overwritten).
    while (auto cmd = commandQueue_->try_pop()) {
        (void)cmd;  // TapCommand is empty -- its presence is the signal.
        if (state_ != HmdComponentState::Ready) {
            DriverLog("MicMap: dropped tap command (handle invalid)\n");
            continue;
        }
        pressTimestamp_ = std::chrono::steady_clock::now();
        writeValue(true);
        pendingReleaseAt_ = pressTimestamp_ + kTapHold;
    }

    // 4. Tick the scheduled release once its hold deadline has passed.
    if (pendingReleaseAt_
        && std::chrono::steady_clock::now() >= *pendingReleaseAt_) {
        writeValue(false);
        pendingReleaseAt_.reset();
    }

    // 5. Max-hold watchdog (SVR-06 + Open Question 5): guard against an app
    //    crash mid-press leaving the system button stuck down.
    if (isPressed_
        && (std::chrono::steady_clock::now() - pressTimestamp_) > kMaxHold) {
        DriverLog("MicMap: max-hold watchdog fired (no UP received in %lldms)\n",
                  static_cast<long long>(kMaxHold.count()));
        writeValue(false);
        pendingReleaseAt_.reset();
    }
}

bool DeviceProvider::ShouldBlockStandbyMode() {
    return false;
}

void DeviceProvider::EnterStandby() {
    DriverLog("MicMap driver entering standby\n");
    // P7 D-21 / MIG-03: pause the detection thread while the HMD is asleep.
    // Pause() emits its own "MicMap detection: paused" log line; do NOT add a
    // second log line here. AudioWorker continues capturing — its WASAPI
    // handles stay valid and the ring continues to fill (but with the
    // detection consumer paused, drop-OLDEST kicks in within ~50 ms;
    // expected and harmless during standby).
    if (detectionRunner_) detectionRunner_->Pause();
}

void DeviceProvider::LeaveStandby() {
    DriverLog("MicMap driver leaving standby\n");
    // P7 D-21 / MIG-03: resume the detection thread when the HMD wakes.
    // Resume() emits its own "MicMap detection: resumed" log line.
    if (detectionRunner_) detectionRunner_->Resume();
}

void DeviceProvider::writeValue(bool v) {
    if (state_ != HmdComponentState::Ready) return;
    if (v == lastWrittenValue_) return;   // avoid redundant writes (Pitfall 12)

    auto err = VRDriverInput()->UpdateBooleanComponent(hSystemClick_, v, 0.0);
    if (err != VRInputError_None) {
        DriverLog("MicMap: UpdateBooleanComponent(%s) failed: %s (%d)\n",
                  v ? "down" : "up", VRInputErrorName(err),
                  static_cast<int>(err));
        hSystemClick_ = k_ulInvalidInputComponentHandle;
        state_ = HmdComponentState::Invalidated;
        isPressed_ = false;
        lastWrittenValue_ = false;
        return;
    }
    DriverLog("MicMap: UpdateBooleanComponent(%s) OK (handle=%llu)\n",
              v ? "down" : "up",
              static_cast<unsigned long long>(hSystemClick_));
    lastWrittenValue_ = v;
    isPressed_ = v;
}

} // namespace micmap::driver

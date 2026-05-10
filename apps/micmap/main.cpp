/**
 * @file main.cpp
 * @brief MicMap Main Application - ImGui GUI with SteamVR Integration
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <ShlObj.h>     // P8 LIB-04 / D-20: SHGetFolderPathW for %APPDATA%\\MicMap log path

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "resource.h"
#include "micmap/audio/audio_capture.hpp"
#include "micmap/detection/noise_detector.hpp"
#include "micmap/steamvr/driver_api.hpp"   // P8 D-22: renamed from vr_input.hpp
#include "micmap/core/state_machine.hpp"
#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"
#include "micmap/common/log_sink.hpp"   // P8 LIB-04 / D-19: composition-root sinks
#include "micmap/common/cli_flags.hpp"
#include "micmap/steamvr/manifest_registrar.hpp"
#include "micmap/bindings/bindings_patcher.hpp"
#include "first_launch_balloon.hpp"
#include "src/tray_glyph.hpp"   // Phase 10 / HEALTH-08 D-04: tray-icon state glyphs
#include "src/fail_pill.hpp"    // Phase 10 / FAIL-01..05 D-07/D-08: priority-stacked failure pills
#include "src/process_check.hpp" // Phase 10 / Pitfall 6: FAIL-02 vs FAIL-03 disambiguation
#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#endif

#include <memory>
#include <atomic>
#include <chrono>
#include <string>
#include <mutex>
#include <thread>
#include <future>
#include <filesystem>
#include <vector>      // P8 LIB-04: composition-root sink list
#include <optional>    // P8 08-05 HEALTH-04/05: lastTriggerAt, lastError
#include <cmath>       // P8 08-05 HEALTH-06: std::pow for fallback rms-from-dbfs
#include <cctype>      // P8 08-05 HEALTH-03: std::toupper for detection-state pill title-case
#include <cstdio>      // P8 08-05 HEALTH-04: std::snprintf for relative timestamps

using namespace micmap;

#if MICMAP_DEBUG_BUILD
#include <iostream>   // std::wcerr for --debug-trigger error reporting (Debug-build-only)

namespace {

// Phase 10 / TEST-02 / D-12: --debug-trigger CLI short-circuit.
//
// Mirror of apps/mic_test/main.cpp:150 tryRunReplayCli pattern (P9-04). When
// `--debug-trigger` appears in argv, instantiate IDriverApi via the existing
// factory, call debugTrigger() (which POSTs /debug/trigger on the driver),
// and return an exit code per HTTP result:
//   0 = HTTP 200 (TapCommand enqueued; HMD dashboard toggles)
//   1 = HTTP non-200 (driver reachable but route refused — e.g. version skew)
//   2 = ECONNREFUSED (driver not running; no port in 27015..27025 listens)
//
// Returns -1 if the flag is NOT present so WinMain falls through to the GUI
// path. Caller convention matches tryRunReplayCli: `if (rc != -1) return rc;`.
//
// Placement requirement: this short-circuit MUST run AFTER the existing CLI
// parses (--register-vrmanifest, --patch-bindings) BUT BEFORE the FAIL-04
// named mutex (10-03). Otherwise a CI runner invoking --debug-trigger would
// contend with a live GUI instance over the named mutex and exit with the
// "second instance found" code instead of the trigger result.
//
// In Release builds the entire #if MICMAP_DEBUG_BUILD block (function +
// WinMain call site below) is elided. The --debug-trigger argument falls
// through to the GUI as a no-op; downstream parsers ignore unknown flags
// (cli_flags.cpp behavior).
static int tryRunDebugTriggerCli() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return -1;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--debug-trigger") == 0) { found = true; break; }
    }
    LocalFree(argv);
    if (!found) return -1;

    // Construct the same IDriverApi the GUI uses. Default factory picks
    // host=127.0.0.1, port-range 27015..27025 — matches the driver-side
    // bind range in driver/src/http_server.cpp::kPortRangeStart..kPortRangeEnd.
    auto driverApi = micmap::steamvr::createDriverApi();
    if (!driverApi) {
        std::wcerr << L"--debug-trigger: createDriverApi() returned null\n";
        return 1;
    }
    auto r = driverApi->debugTrigger();
    switch (r.status) {
        case micmap::steamvr::DebugTriggerResult::Ok:
            return 0;
        case micmap::steamvr::DebugTriggerResult::HttpError:
            std::wcerr << L"--debug-trigger: HTTP error (driver reachable but route refused)\n";
            return 1;
        case micmap::steamvr::DebugTriggerResult::ConnectionRefused:
            std::wcerr << L"--debug-trigger: connection refused (driver not running?)\n";
            return 2;
    }
    return 1;
}

} // anonymous namespace
#endif // MICMAP_DEBUG_BUILD

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Detection timing constants (matching mic_test)
constexpr int MIN_TRAINING_SAMPLES = 50;  // Valid samples needed (detector may reject some)

struct MicMapApp {
    std::unique_ptr<audio::IAudioCapture> audioCapture;
    std::unique_ptr<detection::INoiseDetector> detector;
    std::unique_ptr<steamvr::IVRInput> vrInput;
    std::unique_ptr<core::IStateMachine> stateMachine;
    std::unique_ptr<core::IConfigManager> configManager;
    std::unique_ptr<steamvr::IDriverApi> driverClient;

    std::vector<audio::AudioDevice> devices;
    int selectedDeviceIndex = 0;

    std::atomic<bool> running{true};
    std::atomic<float> currentLevel{0.0f};
    std::atomic<float> currentLevelDb{-60.0f};
    std::atomic<float> currentConfidence{0.0f};
    std::atomic<float> currentSpectralFlatness{0.0f};
    std::atomic<float> currentEnergy{0.0f};
    std::atomic<float> currentEnergyDb{-60.0f};
    std::atomic<bool> isDetected{false};
    // P9 09-03 / IPC-06 / D-05 / D-23: the v1.5 client-side training
    // session flag + sample counter members are deleted here; the driver
    // owns the training session lifecycle now. hasProfile is preserved to
    // gate client-side detection rendering and the new "Discard Profile"
    // UI path until P10 cutover.
    std::atomic<bool> hasProfile{false};

    // Button fire tracking (matching mic_test)
    std::chrono::steady_clock::time_point detectionStartTime;
    std::atomic<bool> detectionActive{false};
    std::atomic<bool> buttonWouldFire{false};
    std::atomic<int> detectionDurationMs{0};

    // Cooldown tracking to prevent repeated triggers
    std::chrono::steady_clock::time_point lastTriggerTime;
    std::atomic<bool> inCooldown{false};

    std::chrono::steady_clock::time_point lastUpdate;

    int detectionTimeMs = 300;

    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid = {};
    bool minimizedToTray = false;
    std::mutex audioMutex;

    // Phase 3 Plan 07: fire-and-forget re-registration thread (D-15 amended,
    // Pitfall 6). MUST be std::thread + atomic stop — std::async's future
    // destructor blocks in its dtor, which would defeat fire-and-forget.
    std::unique_ptr<steamvr::IManifestRegistrar> manifestRegistrar;
    std::thread                                   manifestRetryThread;
    std::atomic<bool>                             manifestRetryCancel{false};

    // WR-01: initial first-boot async init thread. Previously detached, which
    // left no way for shutdown() to wait on in-flight driverClient->connect()
    // / vrInput->initialize() calls before step 3 (disconnect) destroyed the
    // objects they were touching. Tracked and joined in shutdown() like the
    // manifest retry thread.
    std::thread                                   initialConnectThread;
    std::atomic<bool>                             initialConnectCancel{false};

    // WR-05: reconnect futures tracked on the app so shutdown() can wait on
    // them BEFORE tearing down driverClient / vrInput. Previously these were
    // function-local `static std::future<void>` in WinMain's message loop,
    // which meant shutdown() destroyed driverClient while an in-flight
    // connect() / initialize() was still touching it. Unlike the retry
    // thread, these use std::async — their destructors block anyway — but
    // waiting explicitly before teardown avoids operating on half-destroyed
    // state.
    std::future<void> driverConnectFuture;
    std::future<void> vrInitFuture;

    // P8 08-05 — driver-health pane state (HEALTH-01..07).
    std::atomic<bool>       driverLoadedIndicator{false};      // HEALTH-01: from /health success
    std::atomic<bool>       steamvrRunningIndicator{false};    // HEALTH-02: derived from same poll
    std::string             detectionStateStr{"idle"};         // HEALTH-03
    std::optional<std::chrono::system_clock::time_point> lastTriggerAt; // HEALTH-04
    std::optional<std::string> lastError;                       // HEALTH-05
    std::string             audioDeviceState{"ok"};             // HEALTH-07: ok|missing|permission_denied
    std::mutex              healthMu;                           // guards detectionStateStr / lastTriggerAt / lastError / audioDeviceState

    // P8 08-05 — driver-sourced level meter values (HEALTH-06).
    std::atomic<float>      driverLevelDbfs{-60.0f};
    std::atomic<float>      driverLevelRmsNormalized{0.0f};

    // P8 08-05 D-13 — devices polled from driver (GET /devices).
    std::vector<steamvr::DeviceInfoView> driverDevices;
    std::mutex              devicesMu;
    bool                    devicesFetched{false};

    // P8 08-05 D-26 — poll-cadence timers (steady_clock for monotonic intervals).
    std::chrono::steady_clock::time_point lastStatePoll{};
    std::chrono::steady_clock::time_point lastLevelPoll{};
    std::chrono::steady_clock::time_point lastHealthPoll{};
    // P9 09-03 — 5 Hz training progress poll (visible window only, paused when tray-minimized).
    std::chrono::steady_clock::time_point lastTrainingPoll{};

    // P8 08-05 D-09 — ephemeral PUT /settings validation toast (3 s orange).
    std::string             validationToastField;
    std::string             validationToastReason;
    std::chrono::steady_clock::time_point validationToastUntil{};

    // P9 09-03 — endpoint-driven training pane state (replaces deleted v1.5
    // client-side training UI body per CONTEXT D-05 / D-23). Driver is sole
    // trainer (TRAIN-AF-01); UI flows through IDriverApi::startTraining /
    // getTrainingProgress / finalizeTraining / cancelTraining / recomputeTraining.
    std::atomic<bool> driverAudioEnabled{false};       // /health.driver_audio_enabled — proactive disable contract
    std::atomic<bool> driverTrainingActive{false};     // /health.driver_training_active — orphan-recovery
    struct TrainingUiState {
        bool active{false};                                            ///< local "client believes a session is in flight"
        std::chrono::steady_clock::time_point lastPoll{};
        steamvr::TrainingProgressView lastProgress;
        std::string toastMessage;
        std::chrono::steady_clock::time_point toastUntil{};
        float pendingSensitivity{0.5f};
        bool showDiscardConfirmModal{false};
    };
    TrainingUiState trainingUi_;

    bool initialize();
    void shutdown();
    void onTrigger();
    void renderUI();
    void pollDriverHealth();   // P8 08-05: called once per main-loop frame.
};

static MicMapApp g_app;

// Phase 10 / HEALTH-08 D-04..D-06: tray-icon state glyphs.
// Persistent HICONs are loaded once at WinMain startup (Pitfall 2 — no per-swap
// allocation); applyTrayGlyph() called from pollDriverHealth() each tick;
// destroyTrayIcons() called at WinMain teardown.
static micmap::client::TrayState g_tray;

// Phase 10 / FAIL-01..05 / D-07/D-08: active FAIL pill state, populated by the
// poll callback and consumed by the driver-health pane render. activePill is
// std::nullopt when no fail condition is firing; pickActivePill enforces D-08
// priority order so this is always the topmost-priority pill (or none).
static struct FailUxState {
    std::optional<micmap::client::FailPill> activePill;
} g_failUx;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    // IN-13 iter-3: check HRESULTs. GetBuffer failure leaves pBackBuffer
    // uninitialized (UB on subsequent deref/Release); CreateRenderTargetView
    // failure leaves g_mainRenderTargetView null, which the render loop
    // then binds via OMSetRenderTargets -> crash on ClearRenderTargetView.
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer) {
        MICMAP_LOG_ERROR("GetBuffer failed: hr=0x", std::hex, hr);
        g_mainRenderTargetView = nullptr;
        return;
    }
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        MICMAP_LOG_ERROR("CreateRenderTargetView failed: hr=0x", std::hex, hr);
        g_mainRenderTargetView = nullptr;
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void SetupSystemTray(HWND hwnd) {
    g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd = hwnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_app.nid.szTip, L"MicMap");
    // IN-03: Shell_NotifyIconW can return FALSE if the shell is not ready
    // (per-user context still loading at auto-launch) or taskbar is unhappy.
    // Log the failure so the absent-tray + never-fired-balloon case is
    // visible in the log instead of silently misattributed.
    if (!Shell_NotifyIconW(NIM_ADD, &g_app.nid)) {
        MICMAP_LOG_WARNING("Shell_NotifyIconW(NIM_ADD) failed; tray icon may be missing (GetLastError=",
                           GetLastError(), ")");
    }

    // Phase 10 / HEALTH-08 D-04: load 3 tray glyphs once (Pitfall 2 — no per-swap
    // allocation). The boot-time icon (LoadIcon IDI_APPLICATION above) stays in
    // place until pollDriverHealth() calls applyTrayGlyph() for the first time
    // and swaps to tray_armed.ico (or tray_error.ico if the driver is down at
    // startup). initTrayIcons logs WARNINGs on partial-load and is otherwise
    // best-effort — applyTrayGlyph silently no-ops on null HICON, so a missing
    // .ico file degrades to "no glyph swap", never to a crash.
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(::GetModuleHandleW(nullptr));
    micmap::client::initTrayIcons(hInst, g_tray);
}

bool MicMapApp::initialize() {
    configManager = core::createConfigManager();
    configManager->loadDefault();
    auto& config = configManager->getConfig();
    detectionTimeMs = config.detection.minDurationMs;

    audioCapture = audio::createWASAPICapture();
    if (!audioCapture) return false;

    devices = audioCapture->enumerateDevices();
    bool deviceSelected = false;

    // First try to find a device with "Beyond" in the name
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].name.find(L"Beyond") != std::wstring::npos) {
            deviceSelected = audioCapture->selectDeviceById(devices[i].id);
            if (deviceSelected) {
                selectedDeviceIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // If no Beyond device, try saved device ID
    if (!deviceSelected && !config.audio.deviceId.empty()) {
        deviceSelected = audioCapture->selectDeviceById(config.audio.deviceId);
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].id == config.audio.deviceId) {
                selectedDeviceIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // Fall back to first device
    if (!deviceSelected && !devices.empty()) {
        deviceSelected = audioCapture->selectDeviceById(devices[0].id);
        selectedDeviceIndex = 0;
    }

    auto device = audioCapture->getCurrentDevice();
    if (device.sampleRate > 0) {
        detector = detection::createFFTDetector(device.sampleRate, config.detection.fftSize);
        detector->setMinDetectionDuration(config.detection.minDurationMs);
        // P9 09-03: preserved — client-side detection alive until P10 cutover;
        // this load also refreshes after a successful POST /training/finalize
        // per CONTEXT D-24 (the optimistic-apply path lives in the new
        // finalize-poll handler in the Training pane added by 09-03 Task 2).
        detector->loadTrainingData(configManager->getTrainingDataPath());
    }

    // Initialize driver client (non-blocking - will connect in background)
    driverClient = steamvr::createDriverApi();   // P8 D-22 rename

    // Initialize VR input (don't initialize yet - will do async).
    // VR input is only used for SteamVR-quit lifecycle notifications now;
    // all button presses flow through driverClient (POST /button).
    vrInput = steamvr::createOpenVRInput();
    vrInput->setEventCallback([this](const steamvr::VREvent& event) {
        if (event.type == steamvr::VREventType::Quit) {
            running = false;
            PostMessage(hwnd, WM_STEAMVR_QUIT, 0, 0);
        }
    });

    // Phase 3 Plan 07 / D-15 amended (Pitfall 6): fire-and-forget manifest
    // re-registration on every GUI boot. Self-heals across SteamVR upgrades
    // and user-initiated manifest removal (AUTO-04). MUST use std::thread
    // with std::atomic<bool> cancel — std::async's future destructor would
    // block in its dtor and defeat fire-and-forget semantics (Pitfall 6).
    manifestRegistrar = steamvr::createManifestRegistrar();
    manifestRetryThread = std::thread([this]() {
#ifdef MICMAP_HAS_OPENVR
        // Fix (Phase 4 UAT): the sidecar owns a single in-process VR session
        // via vrInput (VRApplication_Background). vr::VRApplications() is a
        // process-global accessor, so manifestRegistrar->ensureRegistered() can
        // operate against that existing session without its own VR_Init. Calling
        // VR_Init a second time here while the Background session was still
        // active caused OpenVR to log
        //   "VR_Init a second time without an intervening VR_Shutdown"
        // and the process SEGV'd ~150ms into startup (see
        // .planning/debug/micmap-startup-segv.md).
        //
        // Teardown ordering is unchanged: the cancel+join at shutdown() step 0
        // (D-14) pre-empts any in-flight ensureRegistered call. The offline
        // case (SteamVR not running at startup) is handled by the main-loop
        // reconnect branch which retries vrInput->initialize(); this thread's
        // poll will observe isInitialized() == true whenever that happens.
        while (!manifestRetryCancel.load()) {
            // Wait up to 3s for vrInput to finish its VR_Init(Background).
            // Cancel-responsive on 100ms ticks.
            bool ready = false;
            for (int i = 0; i < 30 && !manifestRetryCancel.load(); ++i) {
                if (vrInput && vrInput->isInitialized()) { ready = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (manifestRetryCancel.load()) break;
            if (ready) {
                const auto r = manifestRegistrar->ensureRegistered();
                if (r == steamvr::RegisterResult::Success) {
                    // D-18: state-change-only logging. ensureRegistered already
                    // logs INFO on not-installed -> installed transition; no-op
                    // when already installed. Thread exits after success.
                    return;
                }
                // ensureRegistered logs its own WARNINGs on failure paths;
                // do not spam here (D-16 / D-18).
            }
            // D-16: SteamVR not running (or registrar failure): silent retry.
            // 30s sleep in 1s cancel-responsive ticks so shutdown is prompt.
            for (int i = 0; i < 30 && !manifestRetryCancel.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
#endif
    });

    // State machine acts as a pure edge-latch + cooldown over the
    // detector's already temporally-gated DetectionResult::isWhiteNoise
    // boolean. The detector owns the "sustain for minDurationMs" logic
    // (via detector->setMinDetectionDuration); the state machine only
    // adds cooldown and a triggerCallback edge. Do NOT apply
    // minDetectionDuration on both sides -- that double-gates the
    // trigger path and also makes the state machine's threshold check
    // depend on the noisy instantaneous `confidence` value instead of
    // the stable `isWhiteNoise` flag that drives the rest of the UI.
    core::StateMachineConfig smConfig;
    smConfig.minDetectionDuration = std::chrono::milliseconds(0);
    smConfig.cooldownDuration = std::chrono::milliseconds(config.detection.cooldownMs);
    smConfig.detectionThreshold = 0.5f;  // any boolean-true (1.0) crosses; boolean-false (0.0) does not
    stateMachine = core::createStateMachine(smConfig);
    stateMachine->setTriggerCallback([this]() { onTrigger(); });

    // Check if we have a profile loaded
    hasProfile = detector && detector->hasTrainingData();

    if (audioCapture && detector) {
        audioCapture->setAudioCallback([this](const float* samples, size_t count) {
            std::lock_guard<std::mutex> lock(audioMutex);

            // Calculate RMS level (matching mic_test)
            float rms = 0.0f;
            for (size_t i = 0; i < count; ++i) rms += samples[i] * samples[i];
            rms = std::sqrt(rms / count);

            // Scale for display (0-1 range)
            float scaledLevel = rms * 10.0f;
            currentLevel = (scaledLevel > 1.0f) ? 1.0f : scaledLevel;
            currentLevelDb = (rms <= 0.0f) ? -60.0f : std::max(-60.0f, 20.0f * std::log10(rms));

            // P9 09-03: client-side training removed (driver is sole trainer per
            // IPC-06 / 09-CONTEXT D-05 / D-23). Client-side detection still runs
            // until P10 cutover; only the training-specific call sites are gone.
            // Detection (only detect if we have a profile) - matching mic_test
            if (detector->hasTrainingData()) {
                // Only run detection if we have training data
                auto result = detector->analyze(samples, count);
                currentConfidence = result.confidence;
                currentSpectralFlatness = result.spectralFlatness;
                currentEnergy = result.energy;
                currentEnergyDb = (result.energy <= 0.0f) ? -60.0f : std::max(-60.0f, 20.0f * std::log10(result.energy));
                isDetected = result.isWhiteNoise;

                // Track detection duration for button fire (matching mic_test)
                if (result.isWhiteNoise) {
                    if (!detectionActive) {
                        detectionStartTime = std::chrono::steady_clock::now();
                        detectionActive = true;
                    }

                    auto now = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - detectionStartTime).count();
                    detectionDurationMs = static_cast<int>(duration);

                    // Check cooldown
                    auto cooldownElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastTriggerTime).count();
                    bool cooldownExpired = cooldownElapsed >= 300; // 300ms cooldown

                    if (duration >= detectionTimeMs && !buttonWouldFire && cooldownExpired && !inCooldown) {
                        buttonWouldFire = true;
                        // Note: actual tap dispatch flows through the state
                        // machine -> setTriggerCallback -> onTrigger(). This
                        // branch only updates the legacy "buttonWouldFire"
                        // UI hint + cooldown flag.
                        lastTriggerTime = now;
                        inCooldown = true;
                    }
                } else {
                    detectionActive = false;
                    buttonWouldFire = false;
                    detectionDurationMs = 0;
                    inCooldown = false; // Reset cooldown when detection stops
                }

                // Update state machine.
                //
                // Drive the state machine from the detector's temporally-gated
                // boolean `result.isWhiteNoise` (encoded as 1.0/0.0), NOT the
                // instantaneous `result.confidence`. The raw confidence score
                // dips below any reasonable threshold between audio callbacks
                // even while a cover is sustained, which would cause
                // IStateMachine::updateDetecting() to bounce back to Idle
                // before minDetectionDuration elapses -- i.e. the tap would
                // never fire. `isWhiteNoise` already incorporates the
                // spike-gate + temporal smoothing that the rest of the UI
                // trusts.
                auto now = std::chrono::steady_clock::now();
                auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
                lastUpdate = now;
                if (stateMachine) {
                    float smInput = result.isWhiteNoise ? 1.0f : 0.0f;
                    stateMachine->update(smInput, delta);
                }
            } else {
                // No profile - reset detection state
                currentConfidence = 0.0f;
                currentSpectralFlatness = 0.0f;
                currentEnergy = 0.0f;
                currentEnergyDb = -60.0f;
                isDetected = false;
                detectionActive = false;
                buttonWouldFire = false;
                detectionDurationMs = 0;
            }

            hasProfile = detector->hasTrainingData();
        });
        audioCapture->startCapture();
    }
    lastUpdate = std::chrono::steady_clock::now();
    return true;
}

// P8 08-05 — driver-health poll loop. Called once per main-loop frame from
// WinMain (BEFORE the ImGui frame begin). Honors UI-SPEC poll cadences via
// IsIconic gate: 1 Hz /health both modes; 2 Hz visible / 0.5 Hz tray /state;
// 5 Hz visible / 0.5 Hz tray /telemetry/level. Skips /state + /telemetry/level
// when driver-loaded indicator is red (saves N HTTP timeouts per second on
// driver-down). Pitfall 6 mitigation: differentiates ConnectResult::NotFound
// from Timeout — only NotFound flips the indicator red, Timeout preserves
// prior state to avoid false-red flicker on transient slowdowns.
void MicMapApp::pollDriverHealth() {
    if (!driverClient) return;

    auto now = std::chrono::steady_clock::now();

    bool isMinimized = (hwnd != nullptr) && (::IsIconic(hwnd) != FALSE);
    // minimizedToTray captures the WS_MINIMIZE+ShowWindow(SW_HIDE) tray case
    // (see WindowProc WM_SIZE/SC_MINIMIZE). IsIconic alone misses that path
    // because the window is hidden, not iconic.
    bool isTrayMode = isMinimized || minimizedToTray;

    // /health - 1 Hz both visible and tray.
    if (now - lastHealthPoll >= std::chrono::milliseconds(1000)) {
        lastHealthPoll = now;
        steamvr::ConnectResult r;
        if (driverClient->isConnected()) {
            // Already connected: no need to re-issue connect(); cache says
            // we have a port and a previous /health 200 was observed. The
            // next /state poll will surface a stale-cache problem if the
            // driver dies mid-session (the GET will fail and we'll fall
            // back into the connect() branch on the following 1 Hz tick).
            r = steamvr::ConnectResult::Connected;
        } else {
            r = driverClient->connect();
        }
        switch (r) {
            case steamvr::ConnectResult::Connected:
                driverLoadedIndicator.store(true);
                steamvrRunningIndicator.store(true);
                break;
            case steamvr::ConnectResult::NotFound:
                // ECONNREFUSED on every port — driver not loaded. Flip RED.
                MICMAP_LOG_DEBUG("pollDriverHealth: NotFound (ECONNREFUSED)");
                driverLoadedIndicator.store(false);
                steamvrRunningIndicator.store(false);
                driverAudioEnabled.store(false);
                driverTrainingActive.store(false);
                break;
            case steamvr::ConnectResult::Timeout:
                // Pitfall 6: driver may be alive but slow on /health Read/Write.
                // Do NOT flip the indicator on Timeout — leave the prior value
                // intact to avoid false-red flicker.
                MICMAP_LOG_DEBUG("pollDriverHealth: Timeout (keeping prior indicator state)");
                break;
            case steamvr::ConnectResult::OtherError:
            default:
                // Unclassified failures (DNS, malformed URL) treated as red —
                // actionable UX matches NotFound (re-launch / re-install).
                MICMAP_LOG_DEBUG("pollDriverHealth: OtherError (treating as red)");
                driverLoadedIndicator.store(false);
                steamvrRunningIndicator.store(false);
                driverAudioEnabled.store(false);
                driverTrainingActive.store(false);
                break;
        }

        // P9 09-03 — fetch /health full envelope to populate the new
        // driver_audio_enabled / driver_training_active flags. The proactive
        // Train-button gate (UI-SPEC §"Idle state") + orphan-recovery hook
        // (UI-SPEC §"Poll cadences") both consume these atomics. Defensive
        // nullopt handling: if the GET fails, leave the prior values intact
        // (no flicker) — the next 1 Hz tick will re-poll.
        if (driverLoadedIndicator.load()) {
            auto h = driverClient->getHealth();
            if (h.has_value()) {
                driverAudioEnabled.store(h->driver_audio_enabled);
                driverTrainingActive.store(h->driver_training_active);
            }
        }
    }

    if (!driverLoadedIndicator.load()) return;   // skip subsequent polls if down

    // /state — 2 Hz visible / 0.5 Hz tray.
    auto stateInterval = isTrayMode ? std::chrono::milliseconds(2000)
                                    : std::chrono::milliseconds(500);
    if (now - lastStatePoll >= stateInterval) {
        lastStatePoll = now;
        auto state = driverClient->getState();
        if (state.has_value()) {
            std::lock_guard<std::mutex> lk(healthMu);
            detectionStateStr = state->detection_state;
            lastTriggerAt     = state->last_trigger_at;
            lastError         = state->last_error;
            audioDeviceState  = state->audio_device_state;
        }
    }

    // /telemetry/level — 5 Hz visible / 0.5 Hz tray.
    auto levelInterval = isTrayMode ? std::chrono::milliseconds(2000)
                                    : std::chrono::milliseconds(200);
    if (now - lastLevelPoll >= levelInterval) {
        lastLevelPoll = now;
        auto lvl = driverClient->getTelemetryLevel();
        if (lvl.has_value()) {
            driverLevelDbfs.store(lvl->dbfs);
            driverLevelRmsNormalized.store(lvl->rms_normalized);
        }
    }

    // P9 09-03 — 5 Hz GET /training/progress poll (UI-SPEC §"Poll cadences"):
    // visible window only (paused while tray-minimized — training requires user
    // at desk per UI-SPEC). Stop conditions: state == finalized | cancelled,
    // ECONNREFUSED, user clicks Cancel (which flips trainingUi_.active = false).
    // The poll handler is also the canonical finalize-success path
    // (UI-SPEC §"Confirm flow"): on state == finalized we trigger the
    // optimistic profile reload (CONTEXT D-24) + 3 s "Profile saved" toast.
    // Orphan recovery: if /health.driver_training_active is observed true
    // while trainingUi_.active is still false (e.g. session opened by another
    // client, or this client restarted mid-session), seed the UI from one
    // synchronous /training/progress fetch.
    if (!isTrayMode && trainingUi_.active
            && (now - lastTrainingPoll) >= std::chrono::milliseconds(200)) {
        lastTrainingPoll = now;
        auto progress = driverClient->getTrainingProgress();
        if (progress.has_value()) {
            trainingUi_.lastProgress = *progress;
            if (progress->state == "finalized") {
                // Canonical finalize success path (≤200 ms latency = 1×5Hz interval).
                trainingUi_.active = false;
                // Optimistic profile reload (CONTEXT D-24): re-load the on-disk
                // training_data.bin into the in-memory client-side detector so
                // local detection picks up the new profile without a restart.
                // WR-03 / WR-07: serialize with the WASAPI callback.
                if (detector && configManager) {
                    std::lock_guard<std::mutex> lock(audioMutex);
                    detector->loadTrainingData(configManager->getTrainingDataPath());
                }
                hasProfile = true;
                trainingUi_.toastMessage = "Profile saved";
                trainingUi_.toastUntil = now + std::chrono::seconds(3);
            } else if (progress->state == "cancelled") {
                trainingUi_.active = false;
                // last_error (e.g. training_timed_out_no_samples) is rendered
                // by the UI block via trainingUi_.lastProgress.last_error.
            }
        } else {
            // ECONNREFUSED / parse fail — drop to P8 1 Hz /health poll, no retry storm.
            trainingUi_.active = false;
        }
    }

    // P9 09-03 orphan recovery (UI-SPEC §"Poll cadences"): observe a driver
    // training session opened by another client (or surviving a client crash)
    // by comparing /health.driver_training_active to the local active flag.
    if (!isTrayMode && !trainingUi_.active && driverTrainingActive.load()) {
        auto progress = driverClient->getTrainingProgress();
        if (progress.has_value() && progress->state != "idle"
                && progress->state != "finalized" && progress->state != "cancelled") {
            trainingUi_.active = true;
            trainingUi_.lastProgress = *progress;
            lastTrainingPoll = now;
        }
    }

    // Phase 10 / HEALTH-08 D-04..D-06: derive + apply the tray glyph from the
    // SAME poll envelope (no new poll, no new thread per D-06). Materialize the
    // snapshot inputs from the atomics + healthMu-guarded fields populated above
    // (P8 inlined the JSON parse; the dedicated HealthSnapshot/StateSnapshot
    // structs live only on this poll-tick stack frame).
    micmap::client::HealthSnapshot healthSnap;
    healthSnap.driverLoaded            = driverLoadedIndicator.load();
    healthSnap.driverDetectionActive   = false;   // populated by getHealth() if needed in 10-03
    healthSnap.driverTrainingActive    = driverTrainingActive.load();

    micmap::client::StateSnapshot stateSnap;
    {
        std::lock_guard<std::mutex> lk(healthMu);
        stateSnap.detectionState   = detectionStateStr;
        stateSnap.lastError        = lastError;
        stateSnap.audioDeviceState = audioDeviceState;
    }

    // Start the 300ms pulse window when /state.detection_state == "triggered".
    // The window is held in g_tray.lastTriggeredAt and consumed by the next
    // deriveTrayGlyph call(s) until the elapsed delta exceeds 300ms — at which
    // point the glyph naturally falls back to Armed (or Error if a fail-pill
    // condition appeared in the meantime).
    if (stateSnap.detectionState == "triggered") {
        g_tray.lastTriggeredAt = now;
    }
    const auto desired = micmap::client::deriveTrayGlyph(
        healthSnap, stateSnap, g_tray.lastTriggeredAt, now);
    micmap::client::applyTrayGlyph(nid, g_tray, desired);

    // Phase 10 / FAIL-01..05 / D-08: pick topmost-priority pill from the SAME poll envelope.
    //   isProcessRunning(L"vrserver.exe") is called only when the driver is unreachable, so
    //   FAIL-02 vs FAIL-03 disambiguation kicks in just when it's needed (Pitfall 6 -- the
    //   2-second cache in process_check.cpp absorbs the 1Hz poll cadence). When the driver
    //   IS reachable (driverLoaded=true), vrserverRunning is implicitly true (the endpoint
    //   reached us, so vrserver.exe must be running) -- the pickActivePill code path doesn't
    //   actually consume vrserverRunning in that branch, but we pass true for clarity.
    const bool vrserverRunning = !healthSnap.driverLoaded
        ? micmap::client::isProcessRunning(L"vrserver.exe")
        : true;
    g_failUx.activePill = micmap::client::pickActivePill(healthSnap, stateSnap, vrserverRunning);
}

void MicMapApp::shutdown() {
    // Phase 3 Plan 07 / D-12 / D-14: idempotent ordered teardown. Called from
    // the main-loop exit path in WinMain after `running=false` breaks the
    // message pump; may also be reached via fatal-error paths. Guard
    // re-entry so a second invocation is a no-op (all exit paths converge).
    static std::atomic<bool> alreadyShutdown{false};
    if (alreadyShutdown.exchange(true)) return;

    running = false;

    // 0. Stop retry thread FIRST — before vrInput->shutdown — so no retry
    //    attempt is mid-VR_Init while vrInput::shutdown is calling VR_Shutdown
    //    (T-03-07-02 mitigation). Cancel is atomic; join blocks up to 1s
    //    worst-case thanks to the 1s-tick cancel-responsive sleep loop.
    manifestRetryCancel.store(true);
    if (manifestRetryThread.joinable()) manifestRetryThread.join();

    // WR-01: signal cancel + join the first-boot async init thread BEFORE
    // step 3 (driverClient->disconnect) / step 4 (vrInput->shutdown) tear
    // down the objects it's calling into. Previously this thread was
    // detached, leaving no join handle and a latent race on fast-quit.
    initialConnectCancel.store(true);
    if (initialConnectThread.joinable()) initialConnectThread.join();

    // WR-05: wait for any in-flight reconnect futures (driverClient->connect
    // and vrInput->initialize launched via std::async from the main loop)
    // BEFORE the teardown below touches those objects. Without this wait,
    // shutdown() could call driverClient->disconnect() / vrInput->shutdown()
    // while the background connect/initialize is still reading those members.
    // std::async's future destructor blocks anyway; waiting explicitly here
    // makes the ordering intent readable and prevents touching half-destroyed
    // state if the wait were deferred to future dtors after teardown.
    if (driverConnectFuture.valid()) driverConnectFuture.wait();
    if (vrInitFuture.valid())         vrInitFuture.wait();

    // D-12 ordered teardown (reverse-init):
    // 1. Stop audio capture
    if (audioCapture) audioCapture->stopCapture();
    // 2. Reset detector. P9 09-03 / IPC-06 / D-05 / D-23: the prior
    //    client-side training-data persistence call on shutdown is DELETED
    //    here — driver is sole writer for training_data.bin (single-writer
    //    cutover). Client-side detection still loads the profile at startup
    //    (line ~300) until P10 deletes the client audio path entirely.
    if (detector) detector.reset();
    // 3. Disconnect driver client
    if (driverClient) driverClient->disconnect();
    // 4. Shutdown VR input (calls VR_Shutdown under MICMAP_HAS_OPENVR)
    if (vrInput) vrInput->shutdown();
    // 5. P8 08-05 D-07 / IPC-05: client no longer writes config.json.
    // Settings flow through PUT /settings (renderUI -> driverClient->putSettings)
    // and the first-launch tray-notification flag flips through the same path
    // (first_launch_balloon.cpp). The driver is the sole writer for this
    // milestone; configManager stays alive as the in-memory snapshot source
    // for client-side detection (which still runs until P10 cutover deletes
    // the client-side audio path).
    // 6. Remove tray icon
    // IN-12 iter-3: symmetric WARNING log with IN-03 NIM_ADD path so a
    // "stuck zombie tray icon after MicMap quit" is diagnosable from the log.
    if (nid.cbSize != 0) {
        if (!Shell_NotifyIconW(NIM_DELETE, &nid)) {
            MICMAP_LOG_WARNING("Shell_NotifyIconW(NIM_DELETE) failed; tray icon "
                               "may persist until explorer restart (GetLastError=",
                               GetLastError(), ")");
        }
        nid.cbSize = 0;  // mark removed so re-entry into shutdown is a no-op
    }
    // Phase 10 / HEALTH-08: release the 3 persistent HICONs (Pitfall 2).
    // destroyTrayIcons is idempotent — null pointers are skipped — so a
    // second shutdown() call (already guarded by alreadyShutdown above) or
    // a partial-load init at startup leaves nothing dangling.
    micmap::client::destroyTrayIcons(g_tray);
    // Steps 7-8 (ImGui shutdown, D3D/window cleanup, UnregisterClassW) are
    // handled by WinMain's tail after shutdown() returns — preserves the
    // existing call-site topology.
}

void MicMapApp::onTrigger() {
    if (!driverClient || !driverClient->isConnected()) {
        MICMAP_LOG_DEBUG("onTrigger: driver not connected, skipping");
        return;
    }
    // P7 D-10: suppress local trigger when driver owns the detection path.
    // State machine cooldown is the belt-and-suspenders backstop per D-11.
    // The /health poll is cached for 1 s in DriverApi so the latency
    // overhead per onTrigger is bounded. Deleted in P10 per D-12.
    if (driverClient->isDriverDetectionActive()) {
        MICMAP_LOG_DEBUG("onTrigger: driver_detection_active=true, suppressing");
        return;
    }
    if (!driverClient->tap()) {
        MICMAP_LOG_WARNING("onTrigger failed: ", driverClient->getLastError());
    }
}

void MicMapApp::renderUI() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("MicMap", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Status");
    ImGui::Separator();
    bool vrOk = vrInput && vrInput->isInitialized();
    ImGui::TextColored(vrOk ? ImVec4(0,1,0,1) : ImVec4(1,0.5f,0,1), "SteamVR: %s", vrOk ? "Connected" : "Not Connected");
    bool drvOk = driverClient && driverClient->isConnected();
    ImGui::TextColored(drvOk ? ImVec4(0,1,0,1) : ImVec4(1,0.5f,0,1), "Driver: %s", drvOk ? "Connected" : "Not Connected");

    // ===========================================================
    // P8 08-05 — Driver Health pane (HEALTH-01..07).
    // Section order per UI-SPEC §Section order: between Status and Audio Device.
    // ===========================================================
    ImGui::Spacing();
    ImGui::Text("Driver Health");
    ImGui::Separator();

    // Phase 10 / FAIL-01..05 / D-07: FAIL pill rendering (single topmost only per D-08).
    //   Rendered at the TOP of the driver-health pane so the user sees the actionable
    //   nudge before scrolling past the green/orange health indicators below. The pill
    //   carries a deep-link button (ms-settings: for FAIL-01, steam:// for FAIL-02) and
    //   an optional Dismiss button (FAIL-01 + FAIL-05 per D-10). All pills are non-blocking
    //   per D-20 -- they warn but never gate detection.
    if (g_failUx.activePill.has_value()) {
        const auto& pill = *g_failUx.activePill;

        // Headline -- pill-red so it visually pops against the existing white "State:" /
        // green "Driver: Loaded" / orange "Driver: Not loaded" lines.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", pill.headlineText.c_str());
        ImGui::PopStyleColor();

        // Action button -- ShellExecuteW the deep-link URI. ASCII-safe conversion is
        // sufficient because the URIs are string literals in fail_pill.cpp (no Unicode).
        if (!pill.actionLabel.empty() && !pill.deepLink.empty()) {
            if (ImGui::Button(pill.actionLabel.c_str())) {
                std::wstring wuri(pill.deepLink.begin(), pill.deepLink.end());
                ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
        }

        // Dismiss button -- FAIL-01 + FAIL-05 only per D-10. POST /state/clear-error via
        // the existing IDriverApi::clearError() (P8 D-16). Optimistic local clear so the
        // pill disappears immediately; the next /state poll confirms (or re-fires if the
        // condition is still active driver-side).
        if (pill.dismissable) {
            if (ImGui::Button("Dismiss")) {
                if (driverClient && driverClient->clearError()) {
                    std::lock_guard<std::mutex> lk(healthMu);
                    lastError = std::nullopt;
                }
                g_failUx.activePill.reset();   // optimistic local clear
            }
        }

        ImGui::Separator();
    }

    bool drvLoaded = driverLoadedIndicator.load();

    // HEALTH-01 — driver-loaded indicator.
    if (drvLoaded) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Driver: Loaded");
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
            "Driver: Not loaded - install or enable in SteamVR");
    }

    // HEALTH-02 — SteamVR-running indicator (derived from same /health poll).
    bool svrRunning = steamvrRunningIndicator.load();
    if (svrRunning) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "SteamVR: Running");
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "SteamVR: Not running");
    }

    // HEALTH-03 / HEALTH-04 / HEALTH-05 / HEALTH-07 — snapshot under healthMu.
    {
        std::string ds; std::optional<std::chrono::system_clock::time_point> lt;
        std::optional<std::string> le; std::string ads;
        {
            std::lock_guard<std::mutex> lk(healthMu);
            ds = detectionStateStr; lt = lastTriggerAt; le = lastError; ads = audioDeviceState;
        }

        // HEALTH-03 — detection-state pill.
        ImVec4 pillCol = ImVec4(1, 1, 1, 1);                    // body default
        if (ds == "triggered") pillCol = ImVec4(0, 1, 0, 1);
        else if (ds == "cooldown") pillCol = ImVec4(1, 0.5f, 0, 1);
        // Title-case the first letter; rest passes through.
        std::string pretty = ds;
        if (!pretty.empty()) {
            pretty[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(pretty[0])));
        } else {
            pretty = "-";   // empty before first /state poll
        }
        ImGui::TextColored(pillCol, "State: %s", pretty.c_str());

        // HEALTH-04 — last-trigger relative timestamp.
        if (!lt.has_value()) {
            ImGui::Text("Last trigger: -");
        } else {
            auto nowUtc = std::chrono::system_clock::now();
            auto delta = nowUtc - *lt;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(delta).count();
            char buf[64];
            if (secs < 5) {
                std::snprintf(buf, sizeof(buf), "Last trigger: just now");
            } else if (secs < 60) {
                std::snprintf(buf, sizeof(buf), "Last trigger: %lld s ago", (long long)secs);
            } else if (secs < 3600) {
                std::snprintf(buf, sizeof(buf), "Last trigger: %lld m ago", (long long)(secs / 60));
            } else if (secs < 86400) {
                std::snprintf(buf, sizeof(buf), "Last trigger: %lld h ago", (long long)(secs / 3600));
            } else {
                std::snprintf(buf, sizeof(buf), "Last trigger: %lld d ago", (long long)(secs / 86400));
            }
            ImGui::Text("%s", buf);
        }

        // HEALTH-05 — last_error display + Clear button.
        if (le.has_value() && !le->empty()) {
            ImGui::Spacing();
            ImGui::Text("Last error");
            ImGui::TextColored(ImVec4(0.86f, 0.20f, 0.20f, 1), "%s", le->c_str());
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(80, 24))) {
                if (driverClient && driverClient->clearError()) {
                    std::lock_guard<std::mutex> lk(healthMu);
                    lastError = std::nullopt;
                }
            }
        }

        // HEALTH-07 — device-disappeared indicator + Re-pick device button.
        if (ads == "missing" || ads == "permission_denied") {
            ImVec4 col = ImVec4(1, 0.5f, 0, 1);
            const char* msg = (ads == "permission_denied")
                ? "Mic access blocked - open Windows mic settings"
                : "Audio device unavailable - Re-pick device";
            ImGui::TextColored(col, "%s", msg);
            ImGui::SameLine();
            if (ImGui::Button("Re-pick device", ImVec2(120, 24))) {
                std::lock_guard<std::mutex> lk(devicesMu);
                devicesFetched = false;   // forces re-fetch on next picker render
            }
        }
    }

    ImGui::Spacing();
    ImGui::Text("Audio Device");
    ImGui::Separator();

    // P8 08-05 D-13 — driver-sourced device list (GET /devices, 1 s server-side cache).
    // First fetch lazily on UI render; HEALTH-07 "Re-pick device" sets
    // devicesFetched=false to force a re-fetch on the next render.
    if (!devicesFetched && driverLoadedIndicator.load() && driverClient) {
        auto list = driverClient->getDevices();
        if (list.has_value()) {
            std::lock_guard<std::mutex> lk(devicesMu);
            driverDevices = std::move(*list);
            devicesFetched = true;
        }
    }

    {
        const bool drvOkForDevices = driverLoadedIndicator.load();
        if (!drvOkForDevices) ImGui::BeginDisabled();

        std::lock_guard<std::mutex> lk(devicesMu);
        if (driverDevices.empty()) {
            ImGui::TextDisabled("Loading devices...");
        } else {
            std::vector<const char*> ptrs;
            ptrs.reserve(driverDevices.size());
            for (auto& d : driverDevices) ptrs.push_back(d.name.c_str());

            // Re-resolve selectedDeviceIndex against the driver list every
            // frame: the v1.5 selectedDeviceIndex was an index into the
            // local WASAPI enumeration, which the driver list need not
            // mirror. Rebuild from the current AppConfig audio.deviceId
            // so the combo highlights the correct entry on first render.
            int driverSel = 0;
            if (configManager) {
                const auto& cfgDevId = configManager->getConfig().audio.deviceId;
                if (!cfgDevId.empty()) {
                    // Convert wstring deviceId to UTF-8 for compare against driver list.
                    int needed = WideCharToMultiByte(CP_UTF8, 0, cfgDevId.c_str(), -1,
                                                     nullptr, 0, nullptr, nullptr);
                    std::string cfgUtf8;
                    if (needed > 0) {
                        cfgUtf8.resize(static_cast<size_t>(needed - 1));
                        WideCharToMultiByte(CP_UTF8, 0, cfgDevId.c_str(), -1,
                                            cfgUtf8.data(), needed, nullptr, nullptr);
                    }
                    for (size_t i = 0; i < driverDevices.size(); ++i) {
                        if (driverDevices[i].id == cfgUtf8) {
                            driverSel = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
            int prev = driverSel;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Dev", &driverSel, ptrs.data(), (int)ptrs.size())
                    && prev != driverSel && configManager && driverClient) {
                // P8 D-09 — PUT /settings ladder. UTF-8 device id from the
                // driver list -> wstring; build the candidate AppConfig from
                // the current snapshot + the new audio.deviceId.
                core::AppConfig next = configManager->getConfig();
                std::wstring widId;
                int n = MultiByteToWideChar(CP_UTF8, 0,
                    driverDevices[driverSel].id.c_str(), -1,
                    nullptr, 0);
                if (n > 0) {
                    widId.resize(static_cast<size_t>(n - 1));
                    MultiByteToWideChar(CP_UTF8, 0,
                        driverDevices[driverSel].id.c_str(), -1,
                        widId.data(), n);
                }
                next.audio.deviceId = std::move(widId);
                auto r = driverClient->putSettings(next);
                if (r.status == steamvr::PutSettingsResult::Ok) {
                    // Optimistic in-memory apply (D-09): mutate the client's
                    // AppConfig snapshot so client-side detection (live until
                    // P10) sees the new device immediately.
                    configManager->getConfig() = next;
                    selectedDeviceIndex = driverSel;
                    // v1.5 client-side audio retake: re-bind WASAPI to the
                    // new device so the local detection callback keeps
                    // running until the P10 cutover deletes the client-side
                    // audio path entirely.
                    if (audioCapture) {
                        audioCapture->stopCapture();
                        audioCapture->selectDeviceById(next.audio.deviceId);
                        auto dev = audioCapture->getCurrentDevice();
                        if (dev.sampleRate > 0) {
                            std::lock_guard<std::mutex> lock(audioMutex);
                            detector = detection::createFFTDetector(dev.sampleRate);
                            detector->setMinDetectionDuration(detectionTimeMs);
                            if (configManager) {
                                detector->loadTrainingData(configManager->getTrainingDataPath());
                            }
                            audioCapture->startCapture();
                        } else {
                            MICMAP_LOG_WARNING("Device switch (driver list): new device reported sampleRate=0; capture NOT restarted");
                        }
                    }
                } else if (r.status == steamvr::PutSettingsResult::ValidationFailed) {
                    // 4xx — UI rolls back via driverSel re-resolution next
                    // frame; surface the orange toast for 3 s.
                    validationToastField  = r.errorField.value_or("audio.deviceId");
                    validationToastReason = r.errorReason.value_or("validation rejected");
                    validationToastUntil  = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                } else {
                    // ConnectionFailed / OtherError — silently revert. Gate
                    // should prevent this; if observed, the next /health
                    // poll will flip drvLoaded red and disable the combo.
                    MICMAP_LOG_WARNING("Device PUT /settings non-Ok: status=", (int)r.status);
                }
            }
        }
        if (!drvOkForDevices) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Driver not loaded - settings cannot be changed");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Text("Settings");
    ImGui::Separator();

    // P8 08-05 D-09 — driver-loaded gate. When the indicator is red the
    // entire Settings block renders disabled with a tooltip.
    const bool drvOkForSettings = driverLoadedIndicator.load();
    if (!drvOkForSettings) ImGui::BeginDisabled();

    ImGui::Text("Detection Time: %d ms", detectionTimeMs);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##Time", &detectionTimeMs, 100, 1000, "")) {
        // P8 08-05 D-09 — slider on-change goes through PUT /settings
        // instead of mutating the local ConfigManager directly.
        // WR-07: detector mutations stay under audioMutex so the WASAPI
        // callback can't race with the duration-threshold write.
        if (configManager && driverClient) {
            core::AppConfig next = configManager->getConfig();
            const int prevDur = next.detection.minDurationMs;
            next.detection.minDurationMs = detectionTimeMs;
            auto r = driverClient->putSettings(next);
            if (r.status == steamvr::PutSettingsResult::Ok) {
                configManager->getConfig() = next;
                if (detector) {
                    std::lock_guard<std::mutex> lock(audioMutex);
                    detector->setMinDetectionDuration(detectionTimeMs);
                }
            } else if (r.status == steamvr::PutSettingsResult::ValidationFailed) {
                // Roll back the slider to the prior value; show 3 s orange toast.
                detectionTimeMs = prevDur;
                validationToastField  = r.errorField.value_or("detection.minDurationMs");
                validationToastReason = r.errorReason.value_or("validation rejected");
                validationToastUntil  = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            } else {
                detectionTimeMs = prevDur;   // silent revert (gate should prevent reachability)
                MICMAP_LOG_WARNING("Detection time PUT /settings non-Ok: status=", (int)r.status);
            }
        }
    }

    if (!drvOkForSettings) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Driver not loaded - settings cannot be changed");
        }
    }

    // P8 08-05 D-09 — ephemeral 3 s validation toast (orange).
    if (std::chrono::steady_clock::now() < validationToastUntil) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
            "Invalid %s: %s",
            validationToastField.c_str(),
            validationToastReason.c_str());
    }

    // ---- P9 09-03 Training section (endpoint-driven; driver is sole trainer
    //      per CONTEXT D-05 / D-23 / IPC-06 / TRAIN-AF-01) ----
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Training");

    // Proactive gating (warning fix): driver-loaded AND driver_audio_enabled
    // (sourced from /health per 09-02 / 09-03 HealthView extension). The
    // 503 audio_disabled response from POST /training/start is now defense-
    // in-depth only — covers the race window between health-poll and click.
    {
        const bool drv_loaded   = driverLoadedIndicator.load();
        const bool audio_on     = driverAudioEnabled.load();

        if (!trainingUi_.active) {
            // ---- IDLE STATE (UI-SPEC §"Idle state") ----
            const bool can_train = drv_loaded && audio_on;

            ImGui::BeginDisabled(!can_train);
            if (ImGui::Button("Train Pattern", ImVec2(120, 30))) {
                auto result = driverClient->startTraining();
                switch (result.status) {
                    case steamvr::TrainingResult::Ok:
                        trainingUi_.active = true;
                        trainingUi_.lastProgress = steamvr::TrainingProgressView{};
                        trainingUi_.lastProgress.state = "collecting";
                        // Force the next /training/progress poll to fire on the
                        // next pollDriverHealth tick (no 200 ms delay) so the UI
                        // updates promptly with real driver-side sample counts.
                        lastTrainingPoll = std::chrono::steady_clock::now()
                                           - std::chrono::milliseconds(200);
                        break;
                    case steamvr::TrainingResult::Conflict:
                        // Orphan recovery — another session is already active.
                        trainingUi_.active = true;
                        trainingUi_.toastMessage = "Training already in progress";
                        trainingUi_.toastUntil = std::chrono::steady_clock::now()
                                                 + std::chrono::seconds(3);
                        break;
                    case steamvr::TrainingResult::OtherError:
                        if (result.errorField.value_or("") == "audio_disabled") {
                            // Defense-in-depth fallback — proactive gate above
                            // SHOULD have prevented this click, but the race
                            // window between health-poll and click can still
                            // produce 503.
                            trainingUi_.toastMessage =
                                "Driver audio is disabled - enable in driver settings to train";
                            trainingUi_.toastUntil = std::chrono::steady_clock::now()
                                                     + std::chrono::seconds(3);
                        }
                        break;
                    case steamvr::TrainingResult::ConnectionFailed:
                    case steamvr::TrainingResult::ValidationFailed:
                        break;
                }
            }
            ImGui::EndDisabled();

            // Hint copy under disabled button (UI-SPEC §"Idle state").
            if (!drv_loaded) {
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Driver not loaded - settings cannot be changed");
                }
            } else if (!audio_on) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
                    "Driver audio is disabled - enable in driver settings to train");
            }

            if (hasProfile.load()) {
                ImGui::SameLine();
                if (ImGui::Button("Discard Profile", ImVec2(120, 24))) {
                    trainingUi_.showDiscardConfirmModal = true;
                }
            }

            // Status line (UI-SPEC §"Idle state").
            if (hasProfile.load()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Profile trained and ready");
            } else {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Status: No profile loaded");
            }

            // Discard Profile destructive modal (UI-SPEC §"Destructive Confirmations").
            if (trainingUi_.showDiscardConfirmModal) {
                ImGui::OpenPopup("Discard trained profile?");
                trainingUi_.showDiscardConfirmModal = false;
            }
            if (ImGui::BeginPopupModal("Discard trained profile?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped(
                    "Your client-side detection will stop using this profile until "
                    "you train again or restart the driver. The on-disk profile "
                    "(used by the driver) is unaffected.");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.20f, 0.20f, 1));
                if (ImGui::Button("Discard", ImVec2(120, 30))) {
                    // Drop the in-memory profile by re-creating the detector.
                    // WR-03 / WR-07: serialize with the WASAPI callback.
                    if (detector && audioCapture) {
                        auto dev = audioCapture->getCurrentDevice();
                        if (dev.sampleRate > 0) {
                            std::lock_guard<std::mutex> lock(audioMutex);
                            detector = detection::createFFTDetector(dev.sampleRate);
                            detector->setMinDetectionDuration(detectionTimeMs);
                        }
                    }
                    hasProfile = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Keep", ImVec2(120, 30))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            // ---- ACTIVE STATES (UI-SPEC §"Collecting / Computing / Ready") ----
            const auto& progress = trainingUi_.lastProgress;
            if (progress.state == "collecting") {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Cover mic now!");
                const float ratio = (progress.target > 0)
                    ? static_cast<float>(progress.samples_collected)
                      / static_cast<float>(progress.target)
                    : 0.0f;
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "%zu/%zu samples",
                              progress.samples_collected, progress.target);
                ImGui::ProgressBar(ratio, ImVec2(-1, 18), overlay);
                if (ImGui::Button("Cancel Training", ImVec2(120, 30))) {
                    driverClient->cancelTraining();
                    trainingUi_.active = false;
                }
            } else if (progress.state == "computing") {
                ImGui::Text("Computing thresholds...");
                ImGui::ProgressBar(1.0f, ImVec2(-1, 18), "Computing");
            } else if (progress.state == "ready") {
                ImGui::TextWrapped(
                    "Preview ready - confirm to save, or recompute with a "
                    "different sensitivity");
                ImGui::Spacing();
                ImGui::Text("Preview thresholds");
                if (progress.thresholds_preview.has_value()) {
                    const auto& p = *progress.thresholds_preview;
                    ImGui::Text("Sensitivity: %.2f", p.sensitivity);
                    ImGui::Text("Energy threshold: %.4f", p.energy_threshold);
                    ImGui::Text("Spectral profile: mean %.3f, stddev %.3f, %zu bins",
                                p.spectral_profile_summary.mean,
                                p.spectral_profile_summary.stddev,
                                p.spectral_profile_summary.size);
                }
                ImGui::Spacing();
                ImGui::Text("Recompute sensitivity:");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##recompute_sensitivity",
                                   &trainingUi_.pendingSensitivity, 0.0f, 1.0f, "%.2f");
                if (ImGui::Button("Recompute Thresholds", ImVec2(140, 30))) {
                    auto r = driverClient->recomputeTraining(trainingUi_.pendingSensitivity);
                    if (r.status == steamvr::TrainingResult::ValidationFailed) {
                        trainingUi_.toastMessage =
                            "Invalid sensitivity: must be between 0.0 and 1.0";
                        trainingUi_.toastUntil = std::chrono::steady_clock::now()
                                                 + std::chrono::seconds(3);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Confirm & Save", ImVec2(140, 30))) {
                    // Canonical finalize flow (UI-SPEC §"Confirm flow"):
                    // direct response is checked ONLY for error envelopes
                    // (ValidationFailed / Conflict). The success transition
                    // (200 OK -> state=="finalized") is observed by the next
                    // /training/progress poll (<=200 ms latency = 1x5Hz
                    // interval); the poll handler triggers the optimistic
                    // profile reload + "Profile saved" toast.
                    auto r = driverClient->finalizeTraining(/*confirm=*/true);
                    if (r.status == steamvr::TrainingResult::ValidationFailed
                            || r.status == steamvr::TrainingResult::Conflict) {
                        trainingUi_.toastMessage =
                            "Could not save profile - try training again";
                        trainingUi_.toastUntil = std::chrono::steady_clock::now()
                                                 + std::chrono::seconds(3);
                        trainingUi_.active = false;
                    }
                    // On TrainingResult::Ok: do nothing here. The next
                    // progress poll observes state=="finalized" within <=200 ms
                    // and triggers the success toast + reload via the
                    // canonical poll-driven path.
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard Preview", ImVec2(120, 24))) {
                    driverClient->cancelTraining();
                    trainingUi_.active = false;
                }
            }
        }

        // Toast rendering (UI-SPEC §"Error / failure copywriting").
        if (!trainingUi_.toastMessage.empty()
                && std::chrono::steady_clock::now() < trainingUi_.toastUntil) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s",
                               trainingUi_.toastMessage.c_str());
        }

        // Last-error rendering for training_timed_out_no_samples
        // (UI-SPEC §"Error / failure copywriting" — destructive red).
        if (trainingUi_.lastProgress.last_error.has_value()
                && trainingUi_.lastProgress.last_error.value()
                       == "training_timed_out_no_samples") {
            ImGui::TextColored(ImVec4(0.86f, 0.20f, 0.20f, 1),
                "Training timed out - no samples collected in 30 s");
        }
    }
    // ---- end P9 09-03 Training section ----

    ImGui::Spacing();
    ImGui::Text("Audio Levels");
    ImGui::Separator();

    // P8 08-05 HEALTH-06: rewire input-level meter to /telemetry/level when
    // the driver is loaded. Falls back to the local audio callback's
    // currentLevelDb / currentLevel until driverLoadedIndicator goes green
    // (P8 keeps client-side detection live until P10). Stale tag appears
    // when last poll > 1 s old.
    {
        bool useDriver = driverLoadedIndicator.load();
        float dbfs = useDriver ? driverLevelDbfs.load() : currentLevelDb.load();
        float rmsNorm = useDriver ? driverLevelRmsNormalized.load() : currentLevel.load();
        bool stale = useDriver && (std::chrono::steady_clock::now() - lastLevelPoll
                                   > std::chrono::seconds(1));
        ImGui::Text("Input Level: %.1f dB%s", dbfs, stale ? " (stale)" : "");
        ImGui::ProgressBar(rmsNorm, ImVec2(-1, 18));
    }

    // Confidence meter (matching mic_test)
    ImGui::Text("Confidence: %.0f%%", currentConfidence.load() * 100.0f);
    float conf = currentConfidence.load();
    if (isDetected.load()) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1,0.55f,0,1));
    ImGui::ProgressBar(conf, ImVec2(-1, 18));
    if (isDetected.load()) ImGui::PopStyleColor();

    // Spectral flatness and energy (matching mic_test)
    ImGui::Text("Spectral Flatness: %.3f", currentSpectralFlatness.load());
    ImGui::Text("Energy: %.1f dB", currentEnergyDb.load());

    ImGui::Spacing();

    // Detection indicator (matching mic_test style)
    bool buttonFire = buttonWouldFire.load();
    bool detected = isDetected.load();

    // IN-08: detectionBuf was declared unconditionally at outer scope even
    // though only the `detected` branch needs a formatted string; the
    // "TRIGGERED" / "NOT DETECTED" branches use string literals. Keeping the
    // buffer at outer scope here (rather than inside the branch) is REQUIRED
    // because `detectionText` is a `const char*` that must remain valid until
    // the ImGui::Button call below — moving the buffer into the branch would
    // dangle the pointer. We now populate the buffer only when needed and
    // document the lifetime so future refactors don't regress.
    ImVec4 boxColor;
    const char* detectionText;
    char detectionBuf[128];  // lifetime: must outlive ImGui::Button(detectionText, ...) below

    if (buttonFire) {
        boxColor = ImVec4(0, 0.78f, 0, 1);  // Green - triggered
        detectionText = "TRIGGERED";
    } else if (detected) {
        boxColor = ImVec4(1, 0.78f, 0, 1);  // Yellow - detected but not long enough
        snprintf(detectionBuf, sizeof(detectionBuf), "DETECTING... (%d ms / %d ms)",
                 detectionDurationMs.load(), detectionTimeMs);
        detectionText = detectionBuf;
    } else {
        boxColor = ImVec4(0.24f, 0.24f, 0.24f, 1);  // Dark gray - not detected
        detectionText = "NOT DETECTED";
    }

    // Draw detection box
    ImGui::PushStyleColor(ImGuiCol_Button, boxColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, boxColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, boxColor);
    ImGui::Button(detectionText, ImVec2(-1, 50));
    ImGui::PopStyleColor(3);
    ImGui::End();
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    // Phase 10 Pitfall 1 mitigation: explorer.exe restart broadcasts the
    // RegisterWindowMessageW(L"TaskbarCreated") message to all top-level
    // windows. The static UINT is initialized once on first call (RegisterWindowMessage
    // is idempotent); subsequent calls are integer comparisons. On receipt we
    // re-NIM_ADD the icon (the explorer-side state was lost) and force the
    // next pollDriverHealth() tick to re-apply the current glyph by inverting
    // ts.current — applyTrayGlyph's idempotency check then sees a mismatch
    // and runs Shell_NotifyIconW again.
    static const UINT WM_TASKBAR_CREATED = ::RegisterWindowMessageW(L"TaskbarCreated");
    if (WM_TASKBAR_CREATED != 0 && msg == WM_TASKBAR_CREATED) {
        if (g_app.nid.cbSize != 0) {
            if (!Shell_NotifyIconW(NIM_ADD, &g_app.nid)) {
                MICMAP_LOG_WARNING("WM_TASKBAR_CREATED: NIM_ADD re-registration failed (GLE=",
                                   GetLastError(), ")");
            }
            // Force next applyTrayGlyph() to re-issue NIM_MODIFY (idempotency
            // check would otherwise skip the swap because g_tray.current still
            // equals the desired glyph).
            g_tray.current = (g_tray.current == micmap::client::TrayGlyph::Armed)
                                 ? micmap::client::TrayGlyph::Error
                                 : micmap::client::TrayGlyph::Armed;
        }
        return 0;
    }

    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            if (wParam == SIZE_MINIMIZED) { ShowWindow(hWnd, SW_HIDE); g_app.minimizedToTray = true; }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_MINIMIZE) { ShowWindow(hWnd, SW_HIDE); g_app.minimizedToTray = true; return 0; }
            break;
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); g_app.minimizedToTray = false; }
            else if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, IDM_SHOW, L"Show");
                AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");
                SetForegroundWindow(hWnd);
                TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
                DestroyMenu(m);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDM_SHOW) { ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); g_app.minimizedToTray = false; }
            else if (LOWORD(wParam) == IDM_EXIT) g_app.running = false;
            return 0;
        case WM_STEAMVR_QUIT: g_app.running = false; return 0;
        case WM_CLOSE: ShowWindow(hWnd, SW_HIDE); g_app.minimizedToTray = true; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR /*lpCmdLine-unused*/, int nCmdShow) {
    // P8 LIB-04 / D-19 / D-20 / Pitfall 8: composition-root logger wiring.
    // Hoisted to FIRST step in WinMain so the very first MICMAP_LOG_* call
    // — including failures inside the CLI register/unregister fork below
    // and CreateWindowW failures further down — lands in the file sink,
    // not just stderr (Pitfall 8 — client-side equivalent of the driver-
    // side Pitfall 3 mandate that lands in 08-02).
    {
        namespace mc = micmap::common;
        // Resolve %APPDATA%\\MicMap\\micmap.log (mirrors src/core/src/config_manager.cpp:33-49).
        std::filesystem::path logPath;
        wchar_t appdata[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
            logPath = std::filesystem::path(appdata) / L"MicMap" / L"micmap.log";
        } else {
            logPath = std::filesystem::current_path() / L".micmap" / L"micmap.log";
        }
        std::vector<std::shared_ptr<mc::ILogSink>> sinks;
        sinks.push_back(mc::makeStdoutLogSink());
        sinks.push_back(mc::makeFileLogSink(logPath));
        mc::Logger::setLogger(mc::makeMultiSinkLogger(std::move(sinks)));
        MICMAP_LOG_INFO("MicMap client logger wired (file: ", logPath.string(), ")");
    }

    // Phase 3 D-01: parse CLI flags FIRST, before anything else. Use the
    // wide-char argv from CommandLineToArgvW (Pitfall 8: free it the moment
    // CliFlags is populated — no argv pointers stored).
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    micmap::common::CliFlags flags = micmap::common::parseCliArgs(argc, argvW);
    if (argvW) { LocalFree(argvW); argvW = nullptr; }
    // argvW is now INVALID; use only `flags` below.

    // D-02 / D-03 / D-04: CLI fork — headless register/unregister BEFORE
    // any GUI init (no RegisterClassExW, no CreateWindowW, no D3D, no
    // ImGui). D-04: no console allocation — the default ConsoleLogger's
    // stdout writes are dropped on the floor in headless CLI mode; exit
    // code carries the result (0 success, 1 failure).
    if (flags.registerManifest || flags.unregisterManifest) {
#ifdef MICMAP_HAS_OPENVR
        vr::EVRInitError initErr = vr::VRInitError_None;
        vr::VR_Init(&initErr, vr::VRApplication_Utility);
        if (initErr != vr::VRInitError_None) {
            MICMAP_LOG_ERROR("CLI VR_Init(Utility) failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
            return 1;  // D-03
        }
        auto registrar = micmap::steamvr::createManifestRegistrar();
        micmap::steamvr::RegisterResult r = flags.registerManifest
            ? registrar->registerApp()
            : registrar->unregisterApp();
        vr::VR_Shutdown();
        return (r == micmap::steamvr::RegisterResult::Success) ? 0 : 1;
#else
        MICMAP_LOG_ERROR("OpenVR not available in this build; cannot register manifest");
        return 1;
#endif
    }

    // Phase 4 INST-08 / D-09 / D-12: --patch-bindings / --unpatch-bindings
    // CLI fork. MUST run BEFORE the single-instance mutex below so an
    // installer invocation does not contend with a live GUI instance.
    // No VR_Init: patcher resolves SteamVR config dir internally via
    // openvrpaths.vrpath parse; we only need file-system access, not a
    // running vrserver. Exit-code contract per Phase 3 D-03: 0 = success,
    // 1 = failure (inspected by Inno Setup [Run] Check: clauses).
    if (flags.patchBindings || flags.unpatchBindings) {
        auto appLogSink = [](const char* msg) {
            MICMAP_LOG_INFO(msg);
        };
        namespace fs = std::filesystem;
        fs::path configDir = micmap::bindings::ResolveSteamVrConfigDir(appLogSink);
        if (configDir.empty()) {
            MICMAP_LOG_ERROR("Could not resolve SteamVR config dir from openvrpaths.vrpath");
            return 1;
        }
        bool ok;
        if (flags.patchBindings) {
            ok = micmap::bindings::PatchGenericHmdBindingsFile(configDir, appLogSink)
              && micmap::bindings::EnsureControllerTypeFiles(configDir, "lighthouse_hmd", appLogSink);
        } else {
            ok = micmap::bindings::UnpatchGenericHmdBindings(appLogSink);
        }
        return ok ? 0 : 1;
    }

#if MICMAP_DEBUG_BUILD
    // Phase 10 / TEST-02 / D-12: --debug-trigger short-circuit (Debug-build-only).
    // Placement: AFTER existing CLI parses (so --register-vrmanifest etc keep
    // priority on the rare overlapping invocation) but BEFORE the FAIL-04
    // named mutex below (so a CI runner invoking --debug-trigger doesn't
    // contend with a live GUI instance over the mutex and exit with the
    // "second instance" code instead of the trigger result). In Release
    // builds this block is elided — --debug-trigger is a no-op argument
    // that falls through to the GUI.
    {
        const int rc = tryRunDebugTriggerCli();
        if (rc != -1) {
            return rc;
        }
    }
#endif

    // Phase 10 / FAIL-04 / D-09 hardening (RESEARCH §Pattern 2):
    //  - Local\\ session-scoping prefix (explicit; matches default behavior but documented;
    //    keeps the mutex per-Windows-session so a fast-user-switch scenario doesn't share
    //    the handle across user sessions on the same box).
    //  - _v1 namespace suffix (lets future versions shed a stuck handle from a crashed v0
    //    process; bumping to _v2 in a hypothetical migration is a one-line change).
    //  - SW_RESTORE before SetForegroundWindow when surfacing a tray-minimized first
    //    instance (the existing code only did SetForegroundWindow which silently fails
    //    against a minimized window).
    //  - Existing P3 D-08 minimized-skip carve-out preserved (SteamVR --minimized
    //    auto-relaunch must not steal focus mid-VR-session).
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\MicMap_Client_SingleInstance_v1");
    // IN-08: CreateMutexW can return NULL on failure (rare — security
    // descriptor errors, kernel object exhaustion). If it did, hMutex is
    // NULL, GetLastError() will not be ERROR_ALREADY_EXISTS (so the
    // single-instance gate below falls through), and we would boot a second
    // instance without a guard. Bail explicitly.
    if (!hMutex) {
        MICMAP_LOG_ERROR("CreateMutexW failed: ", GetLastError());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // D-08: --minimized second instance = SteamVR re-launching while the
        // first instance is already alive. Silent exit; do NOT steal focus.
        if (!flags.minimized) {
            HWND w = FindWindowW(L"MicMapMain", nullptr);
            if (w) {
                PostMessageW(w, WM_COMMAND, IDM_SHOW, 0);
                ShowWindow(w, SW_RESTORE);     // P10 D-09: surface a tray-minimized peer (was missing pre-P10)
                SetForegroundWindow(w);
            }
        }
        // WR-02: CreateMutexW returns a valid handle even on
        // ERROR_ALREADY_EXISTS; every other exit path in WinMain closes it,
        // so close it here too for consistency (handle is benign at process
        // scope, but leaking it here is a footgun relative to every sibling
        // exit path).
        CloseHandle(hMutex);
        return 0;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WindowProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"MicMapMain", nullptr };
    RegisterClassExW(&wc);
    g_app.hwnd = CreateWindowW(L"MicMapMain", L"MicMap", WS_OVERLAPPEDWINDOW, 100, 100, 500, 620, nullptr, nullptr, hInstance, nullptr);

    // WR-04: CreateWindowW can fail (rare — typically GDI object exhaustion
    // or high-DPI quirks on locked-down systems). If hwnd is null, the
    // D3D11CreateDeviceAndSwapChain below would receive OutputWindow=nullptr,
    // which is UB on older Windows builds. Bail out cleanly and release the
    // initial-owner mutex so the next launch is not stuck seeing
    // ERROR_ALREADY_EXISTS during crash-restart cycles.
    if (!g_app.hwnd) {
        MICMAP_LOG_ERROR("CreateWindowW failed: ", GetLastError());
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CloseHandle(hMutex);
        return 1;
    }

    // WR-04: on D3D init failure also destroy the window and close the
    // single-instance mutex — the original code only unregistered the
    // window class and relied on OS process-exit cleanup for the rest.
    if (!CreateDeviceD3D(g_app.hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(g_app.hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CloseHandle(hMutex);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_app.hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // WR-03: fail-closed on initialize() failure. If audio capture couldn't
    // be created the app has no way to detect; proceeding would (a) still
    // register the manifest via the retry thread, causing SteamVR to
    // auto-launch a broken instance on next boot, and (b) leak a tray icon
    // / window / mutex into a zombie process. Tear everything down cleanly
    // and propagate the failure as exit code 1 to WinMain's caller.
    if (!g_app.initialize()) {
        MICMAP_LOG_ERROR("MicMapApp::initialize failed; exiting");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(g_app.hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CloseHandle(hMutex);
        return 1;
    }
    SetupSystemTray(g_app.hwnd);

    // Start async initialization of VR and driver.
    // WR-01: tracked on g_app (not detached) so shutdown() can join before
    // step 3 (driverClient->disconnect). The cancel flag short-circuits the
    // remaining work if the user quits during first-boot init.
    //
    // WR-08: seed driverConnectFuture / vrInitFuture from the initial thread's
    // packaged_task futures so the main-loop reconnect guard (which checks
    // `driverConnectFuture.valid() && wait_for(0) != ready`) naturally
    // serializes against the initial thread's in-flight
    // driverClient->connect() / vrInput->initialize() calls. Without this
    // seeding, the main loop's reconnect branch would fire ~2s into boot
    // while the initial thread's connect() was still running, launching a
    // *second* concurrent connect() on the same IDriverApi instance.
    std::packaged_task<void()> connectTask([]() {
        if (g_app.initialConnectCancel.load()) return;
        if (g_app.driverClient) {
            // P8 Pitfall 6: connect() now returns ConnectResult. Treat any
            // non-Connected outcome as the legacy false (caller code paths
            // already key off isConnected() for downstream gating).
            auto r = g_app.driverClient->connect();
            if (r != steamvr::ConnectResult::Connected) {
                MICMAP_LOG_WARNING("driverClient->connect() returned non-Connected: ",
                                   r == steamvr::ConnectResult::NotFound ? "NotFound" :
                                   r == steamvr::ConnectResult::Timeout  ? "Timeout"  : "OtherError");
            }
        }
    });
    std::packaged_task<void()> vrInitTask([]() {
        if (g_app.initialConnectCancel.load()) return;
        if (g_app.vrInput) {
            g_app.vrInput->initialize();
        }
    });
    g_app.driverConnectFuture = connectTask.get_future();
    g_app.vrInitFuture = vrInitTask.get_future();
    // IN-11 iter-3: invoke both packaged_tasks unconditionally so their
    // futures always resolve to a valid void() rather than broken_promise.
    // Each task body already short-circuits on initialConnectCancel (lines
    // 925-929, 931-935), so the fast-cancel intent is preserved without
    // leaving the main-loop reconnect guard looking at broken-promise futures.
    g_app.initialConnectThread = std::thread(
        [connectTask = std::move(connectTask),
         vrInitTask  = std::move(vrInitTask)]() mutable {
            connectTask();
            vrInitTask();
        });

    // D-06: silent-mode window policy — never ShowWindow when auto-launched
    // by SteamVR. The legacy command-line substring check on lpCmdLine has
    // been replaced by flags.minimized parsed at WinMain entry.
    if (flags.minimized) {
        g_app.minimizedToTray = true;
    } else {
        ShowWindow(g_app.hwnd, nCmdShow);
        UpdateWindow(g_app.hwnd);
    }

    // D-09: first-silent-launch balloon — fires once per install, persisted
    // via AppConfig.shownTrayNotification. Gated on flags.minimized so a
    // user-clicked boot never consumes the one-shot.
    // P8 08-05 D-07: persistence flows through driverClient->putSettings now,
    // so we plumb the IDriverApi pointer through to the balloon code path.
    if (flags.minimized && g_app.configManager) {
        micmap::apps::ProductionShellNotifySeam shellAdapter(g_app.nid);
        micmap::apps::fireBalloonIfFirstSilentLaunch(
            shellAdapter, *g_app.configManager, flags.minimized,
            g_app.driverClient.get());
    }

    ImVec4 clear_color(0.1f, 0.1f, 0.1f, 1.0f);
    while (g_app.running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_app.running = false;
        }
        if (!g_app.running) break;

        // Non-blocking updates - only poll if initialized
        if (g_app.vrInput && g_app.vrInput->isInitialized()) {
            g_app.vrInput->pollEvents();
        }

        // Async reconnection attempts using futures to avoid blocking.
        // WR-05: futures live on g_app (see MicMapApp::driverConnectFuture /
        // vrInitFuture) so shutdown() can wait on them before teardown.
        static int reconnectCounter = 0;
        int reconnectInterval = g_app.minimizedToTray ? 100 : 40;

        if (++reconnectCounter >= reconnectInterval) {
            reconnectCounter = 0;

            // Check if driver needs reconnection (async)
            if (g_app.driverClient && !g_app.driverClient->isConnected()) {
                if (!g_app.driverConnectFuture.valid() ||
                    g_app.driverConnectFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    g_app.driverConnectFuture = std::async(std::launch::async, []() {
                        // P8 Pitfall 6: connect() returns ConnectResult; the
                        // reconnect loop only cares whether a successful
                        // /health was observed. Other branches surface as
                        // a debug log; the loop itself self-corrects on the
                        // next interval if the driver comes up later.
                        auto r = g_app.driverClient->connect();
                        if (r != steamvr::ConnectResult::Connected) {
                            MICMAP_LOG_DEBUG("reconnect: connect() returned non-Connected (",
                                             r == steamvr::ConnectResult::NotFound ? "NotFound" :
                                             r == steamvr::ConnectResult::Timeout  ? "Timeout"  : "OtherError",
                                             ")");
                        }
                    });
                }
            }

            // Check if VR needs initialization (async)
            if (g_app.vrInput && !g_app.vrInput->isInitialized()) {
                if (!g_app.vrInitFuture.valid() ||
                    g_app.vrInitFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    g_app.vrInitFuture = std::async(std::launch::async, []() {
                        g_app.vrInput->initialize();
                    });
                }
            }
        }

        // P8 08-05: poll driver-health endpoints once per main-loop frame.
        // Cadence-gated internally (1 Hz /health, 2 Hz /state, 5 Hz /telemetry/level
        // visible; 0.5 Hz tray). Runs even when minimized so the indicator stays
        // fresh and the level-meter snapshot is up-to-date when the user restores.
        g_app.pollDriverHealth();

        if (!g_app.minimizedToTray) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            g_app.renderUI();
            ImGui::Render();
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_pSwapChain->Present(1, 0);
        } else {
            Sleep(50);
        }
    }

    g_app.shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_app.hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CloseHandle(hMutex);
    return 0;
}

#else
#include <iostream>
int main() { std::cerr << "Windows only\n"; return 1; }
#endif

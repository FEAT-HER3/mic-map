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

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "resource.h"
#include "micmap/audio/audio_capture.hpp"
#include "micmap/detection/noise_detector.hpp"
#include "micmap/steamvr/vr_input.hpp"
#include "micmap/core/state_machine.hpp"
#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"
#include "micmap/common/cli_flags.hpp"
#include "micmap/steamvr/manifest_registrar.hpp"
#include "first_launch_balloon.hpp"
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

using namespace micmap;

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
    std::unique_ptr<steamvr::IDriverClient> driverClient;

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
    std::atomic<bool> isTraining{false};
    std::atomic<int> trainingSampleCount{0};
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

    bool initialize();
    void shutdown();
    void onTrigger();
    void renderUI();
};

static MicMapApp g_app;

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
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
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
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
}

void RemoveSystemTray() { Shell_NotifyIconW(NIM_DELETE, &g_app.nid); }

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
        detector->loadTrainingData(configManager->getTrainingDataPath());
    }

    // Initialize driver client (non-blocking - will connect in background)
    driverClient = steamvr::createDriverClient();

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
        // WR-01 mitigation: narrow the startup-window race between this thread's
        // VR_Init(Utility) and the detached initThread's VR_Init(Background) by
        // deferring the FIRST utility init until the background vrInput has
        // either finished its VR_Init OR clearly isn't coming up (no SteamVR).
        // Teardown ordering is already handled by the cancel+join at the top of
        // shutdown() (D-14). We poll isInitialized() on 100ms ticks for up to
        // 3 seconds; after that we fall through to the normal retry loop so a
        // SteamVR-started-mid-session still gets the manifest registered.
        // NOTE: this does not eliminate the race entirely — if vrInput's
        // VR_Init is in-flight exactly when our first VR_Init runs, both calls
        // still race. The cancel+join on shutdown prevents the teardown race
        // (see shutdown() step 0); the window narrowed here is startup-only.
        for (int i = 0; i < 30 && !manifestRetryCancel.load(); ++i) {
            if (vrInput && vrInput->isInitialized()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        while (!manifestRetryCancel.load()) {
            vr::EVRInitError err = vr::VRInitError_None;
            vr::VR_Init(&err, vr::VRApplication_Utility);
            if (err == vr::VRInitError_None) {
                const auto r = manifestRegistrar->ensureRegistered();
                vr::VR_Shutdown();
                if (r == steamvr::RegisterResult::Success) {
                    // D-18: state-change-only logging. ensureRegistered already
                    // logs INFO on not-installed -> installed transition; no-op
                    // when already installed. Thread exits after success.
                    return;
                }
                // ensureRegistered logs its own WARNINGs on failure paths;
                // do not spam here (D-16 / D-18).
            } else if (err == vr::VRInitError_Init_HmdNotFound ||
                       err == vr::VRInitError_Init_NoServerForBackgroundApp) {
                // D-16: SteamVR not running — silent retry, no log spam.
            } else {
                // D-16: unexpected VRInitError — one WARNING with enum name.
                MICMAP_LOG_WARNING("manifest retry VR_Init unexpected error: ",
                                   vr::VR_GetVRInitErrorAsEnglishDescription(err));
            }
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

            // Training or detection (only detect if we have a profile) - matching mic_test
            if (isTraining) {
                detector->addTrainingSample(samples, count);
                trainingSampleCount++;
            } else if (detector->hasTrainingData()) {
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

    // D-12 ordered teardown (reverse-init):
    // 1. Stop audio capture
    if (audioCapture) audioCapture->stopCapture();
    // 2. Persist detector training (pre-reset) + reset detector
    if (detector && detector->hasTrainingData() && configManager)
        detector->saveTrainingData(configManager->getTrainingDataPath());
    if (detector) detector.reset();
    // 3. Disconnect driver client
    if (driverClient) driverClient->disconnect();
    // 4. Shutdown VR input (calls VR_Shutdown under MICMAP_HAS_OPENVR)
    if (vrInput) vrInput->shutdown();
    // 5. Persist config (shownTrayNotification flag + any UI-edited fields)
    if (configManager) configManager->saveDefault();
    // 6. Remove tray icon
    if (nid.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &nid);
        nid.cbSize = 0;  // mark removed so RemoveSystemTray is a no-op
    }
    // Steps 7-8 (ImGui shutdown, D3D/window cleanup, UnregisterClassW) are
    // handled by WinMain's tail after shutdown() returns — preserves the
    // existing call-site topology.
}

void MicMapApp::onTrigger() {
    if (!driverClient || !driverClient->isConnected()) {
        MICMAP_LOG_DEBUG("onTrigger: driver not connected, skipping");
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

    ImGui::Spacing();
    ImGui::Text("Audio Device");
    ImGui::Separator();
    if (!devices.empty()) {
        std::vector<std::string> names;
        for (auto& d : devices) {
            // Convert wstring to string properly
            std::string name(d.name.length(), '\0');
            WideCharToMultiByte(CP_UTF8, 0, d.name.c_str(), -1, &name[0], (int)name.size() + 1, nullptr, nullptr);
            name.resize(strlen(name.c_str()));
            names.push_back(name);
        }
        std::vector<const char*> ptrs;
        for (auto& n : names) ptrs.push_back(n.c_str());
        int prev = selectedDeviceIndex;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Dev", &selectedDeviceIndex, ptrs.data(), (int)ptrs.size()) && prev != selectedDeviceIndex && audioCapture) {
            audioCapture->stopCapture();
            audioCapture->selectDeviceById(devices[selectedDeviceIndex].id);
            auto dev = audioCapture->getCurrentDevice();
            if (dev.sampleRate > 0) {
                detector = detection::createFFTDetector(dev.sampleRate);
                detector->setMinDetectionDuration(detectionTimeMs);
                if (configManager) detector->loadTrainingData(configManager->getTrainingDataPath());
            }
            audioCapture->startCapture();
            if (configManager) configManager->getConfig().audio.deviceId = devices[selectedDeviceIndex].id;
        }
    }

    ImGui::Spacing();
    ImGui::Text("Settings");
    ImGui::Separator();
    ImGui::Text("Detection Time: %d ms", detectionTimeMs);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##Time", &detectionTimeMs, 100, 1000, "")) {
        if (detector) detector->setMinDetectionDuration(detectionTimeMs);
        if (configManager) configManager->getConfig().detection.minDurationMs = detectionTimeMs;
    }

    ImGui::Spacing();
    ImGui::Text("Training");
    ImGui::Separator();

    // Check for auto-stop training (matching mic_test)
    if (isTraining && trainingSampleCount >= MIN_TRAINING_SAMPLES * 3) {
        if (detector) {
            bool success = detector->finishTraining();
            isTraining = false;
            if (success) {
                hasProfile = true;
                if (configManager) detector->saveTrainingData(configManager->getTrainingDataPath());
            }
        }
    }

    if (isTraining) {
        if (ImGui::Button("Stop Training", ImVec2(120, 30))) {
            if (detector) {
                bool success = detector->finishTraining();
                if (success && configManager) {
                    detector->saveTrainingData(configManager->getTrainingDataPath());
                    hasProfile = true;
                }
            }
            isTraining = false;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0.5f,0,1), "Cover mic now! (%d samples)", trainingSampleCount.load());
    } else {
        if (ImGui::Button("Train Pattern", ImVec2(120, 30)) && detector) {
            detector->startTraining();
            isTraining = true;
            trainingSampleCount = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(60, 30)) && detector) {
            auto dev = audioCapture->getCurrentDevice();
            if (dev.sampleRate > 0) {
                detector = detection::createFFTDetector(dev.sampleRate);
                detector->setMinDetectionDuration(detectionTimeMs);
            }
            hasProfile = false;
            trainingSampleCount = 0;
        }
    }

    // Training status (matching mic_test)
    if (hasProfile) {
        ImGui::TextColored(ImVec4(0,1,0,1), "Status: Profile trained and ready");
    } else {
        ImGui::TextColored(ImVec4(1,0.5f,0,1), "Status: No profile loaded");
    }

    ImGui::Spacing();
    ImGui::Text("Audio Levels");
    ImGui::Separator();

    // Input level with dB display (matching mic_test)
    ImGui::Text("Input Level: %.1f dB", currentLevelDb.load());
    ImGui::ProgressBar(currentLevel.load(), ImVec2(-1, 18));

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

    ImVec4 boxColor;
    const char* detectionText;
    char detectionBuf[128];

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

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // D-08: --minimized second instance = SteamVR re-launching while the
        // first instance is already alive. Silent exit; do NOT steal focus.
        if (!flags.minimized) {
            HWND w = FindWindowW(L"MicMapMain", nullptr);
            if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
        }
        return 0;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WindowProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION), LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"MicMapMain", nullptr };
    RegisterClassExW(&wc);
    g_app.hwnd = CreateWindowW(L"MicMapMain", L"MicMap", WS_OVERLAPPEDWINDOW, 100, 100, 500, 620, nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(g_app.hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

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

    // Start async initialization of VR and driver
    std::thread initThread([]() {
        if (g_app.driverClient) {
            g_app.driverClient->connect();
        }
        if (g_app.vrInput) {
            g_app.vrInput->initialize();
        }
    });
    initThread.detach();

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
    if (flags.minimized && g_app.configManager) {
        micmap::apps::ProductionShellNotifySeam shellAdapter(g_app.nid);
        micmap::apps::fireBalloonIfFirstSilentLaunch(
            shellAdapter, *g_app.configManager, flags.minimized);
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

        // Async reconnection attempts using futures to avoid blocking
        static std::future<void> driverConnectFuture;
        static std::future<void> vrInitFuture;
        static int reconnectCounter = 0;
        int reconnectInterval = g_app.minimizedToTray ? 100 : 40;

        if (++reconnectCounter >= reconnectInterval) {
            reconnectCounter = 0;

            // Check if driver needs reconnection (async)
            if (g_app.driverClient && !g_app.driverClient->isConnected()) {
                if (!driverConnectFuture.valid() ||
                    driverConnectFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    driverConnectFuture = std::async(std::launch::async, []() {
                        g_app.driverClient->connect();
                    });
                }
            }

            // Check if VR needs initialization (async)
            if (g_app.vrInput && !g_app.vrInput->isInitialized()) {
                if (!vrInitFuture.valid() ||
                    vrInitFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    vrInitFuture = std::async(std::launch::async, []() {
                        g_app.vrInput->initialize();
                    });
                }
            }
        }

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

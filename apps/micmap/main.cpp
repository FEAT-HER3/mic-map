/**
 * @file main.cpp
 * @brief MicMap Main Application - Desktop Window with SteamVR Integration
 * 
 * Main application that combines all modules:
 * - Audio capture and monitoring
 * - White noise pattern detection
 * - SteamVR integration for HMD button events
 * - Configuration management
 * 
 * Runs as a desktop windowed application that registers as a SteamVR add-on,
 * starting and stopping alongside SteamVR.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")
#endif

#include "micmap/audio/audio_capture.hpp"
#include "micmap/detection/noise_detector.hpp"
#include "micmap/steamvr/vr_input.hpp"
#include "micmap/steamvr/dashboard_manager.hpp"
#include "micmap/core/state_machine.hpp"
#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

using namespace micmap;

// Window dimensions
constexpr int WINDOW_WIDTH = 500;
constexpr int WINDOW_HEIGHT = 500;

// Control IDs
constexpr int ID_DEVICE_COMBO = 101;
constexpr int ID_TRAIN_BUTTON = 102;
constexpr int ID_SENSITIVITY_SLIDER = 103;
constexpr int ID_TIMER = 104;

// Application state
struct MicMapApp {
    // Modules
    std::unique_ptr<audio::IAudioCapture> audioCapture;
    std::unique_ptr<detection::INoiseDetector> detector;
    std::unique_ptr<steamvr::IVRInput> vrInput;
    std::unique_ptr<steamvr::IDashboardManager> dashboardManager;
    std::unique_ptr<core::IStateMachine> stateMachine;
    std::unique_ptr<core::IConfigManager> configManager;
    
    // Device list
    std::vector<audio::AudioDevice> devices;
    
    // State
    std::atomic<bool> running{true};
    std::atomic<float> currentLevel{0.0f};
    std::atomic<float> currentConfidence{0.0f};
    std::atomic<bool> isDetected{false};
    std::atomic<bool> isTraining{false};
    std::chrono::steady_clock::time_point lastUpdate;
    
    // Window handles
    HWND hwnd = nullptr;
    HWND deviceCombo = nullptr;
    HWND trainButton = nullptr;
    HWND sensitivitySlider = nullptr;
    HWND statusLabel = nullptr;
    HWND vrStatusLabel = nullptr;
    
    bool initialize();
    void shutdown();
    void onTrigger();
    void updateVRStatus();
};

static MicMapApp g_app;

#ifdef _WIN32

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void UpdateDisplay(HWND hwnd);
void OnDeviceSelected(int index);
void OnTrainClicked();
void OnSensitivityChanged(int value);

std::wstring GetTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    
    std::wostringstream oss;
    oss << std::setfill(L'0') << std::setw(2) << tm_buf.tm_hour << L":"
        << std::setfill(L'0') << std::setw(2) << tm_buf.tm_min << L":"
        << std::setfill(L'0') << std::setw(2) << tm_buf.tm_sec;
    return oss.str();
}

bool MicMapApp::initialize() {
    MICMAP_LOG_INFO("Initializing MicMap...");
    
    // Load configuration
    configManager = core::createConfigManager();
    configManager->loadDefault();
    auto& config = configManager->getConfig();
    
    // Initialize audio capture
    audioCapture = audio::createWASAPICapture();
    if (!audioCapture) {
        MICMAP_LOG_ERROR("Failed to create audio capture");
        return false;
    }
    
    // Enumerate devices
    devices = audioCapture->enumerateDevices();
    
    // Select audio device
    bool deviceSelected = false;
    if (!config.audio.deviceId.empty()) {
        deviceSelected = audioCapture->selectDeviceById(config.audio.deviceId);
    }
    if (!deviceSelected) {
        deviceSelected = audioCapture->selectDevice(config.audio.deviceNamePattern);
    }
    if (!deviceSelected && !devices.empty()) {
        deviceSelected = audioCapture->selectDeviceById(devices[0].id);
    }
    
    if (!deviceSelected) {
        MICMAP_LOG_WARNING("No audio device available");
    }
    
    auto device = audioCapture->getCurrentDevice();
    if (device.sampleRate > 0) {
        MICMAP_LOG_INFO("Using audio device: ", 
            std::string(device.name.begin(), device.name.end()));
        
        // Initialize detector
        detector = detection::createFFTDetector(device.sampleRate, config.detection.fftSize);
        detector->setSensitivity(config.detection.sensitivity);
        
        // Load training data if available
        auto trainingPath = configManager->getTrainingDataPath();
        if (detector->loadTrainingData(trainingPath)) {
            MICMAP_LOG_INFO("Loaded training data");
        }
    }
    
    // Initialize VR
    vrInput = steamvr::createOpenXRInput();
    vrInput->setEventCallback([this](const steamvr::VREvent& event) {
        if (event.type == steamvr::VREventType::Quit) {
            MICMAP_LOG_INFO("SteamVR quit event received");
            running = false;
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
    });
    
    if (!vrInput->initialize()) {
        MICMAP_LOG_WARNING("VR not available, running in standalone mode");
    }
    
    dashboardManager = steamvr::createDashboardManager();
    auto sharedVR = std::shared_ptr<steamvr::IVRInput>(
        steamvr::createOpenXRInput().release(),
        [](steamvr::IVRInput* p) { delete p; }
    );
    sharedVR->initialize();
    dashboardManager->initialize(sharedVR);
    
    // Initialize state machine
    core::StateMachineConfig smConfig;
    smConfig.minDetectionDuration = std::chrono::milliseconds(config.detection.minDurationMs);
    smConfig.cooldownDuration = std::chrono::milliseconds(config.detection.cooldownMs);
    smConfig.detectionThreshold = config.detection.sensitivity;
    
    stateMachine = core::createStateMachine(smConfig);
    stateMachine->setTriggerCallback([this]() {
        onTrigger();
    });
    
    // Set up audio callback
    if (audioCapture && detector) {
        audioCapture->setAudioCallback([this](const float* samples, size_t count) {
            // Calculate RMS level
            float rms = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                rms += samples[i] * samples[i];
            }
            rms = std::sqrt(rms / count);
            float scaledLevel = rms * 10.0f;
            currentLevel = (scaledLevel > 1.0f) ? 1.0f : scaledLevel;
            
            // Training or detection
            if (isTraining) {
                detector->addTrainingSample(samples, count);
            } else {
                auto result = detector->analyze(samples, count);
                currentConfidence = result.confidence;
                isDetected = result.isWhiteNoise;
                
                auto now = std::chrono::steady_clock::now();
                auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastUpdate);
                lastUpdate = now;
                
                if (stateMachine) {
                    stateMachine->update(result.confidence, delta);
                }
            }
        });
        
        audioCapture->startCapture();
    }
    
    lastUpdate = std::chrono::steady_clock::now();
    
    MICMAP_LOG_INFO("MicMap initialized successfully");
    return true;
}

void MicMapApp::shutdown() {
    MICMAP_LOG_INFO("Shutting down MicMap...");
    
    running = false;
    
    if (audioCapture) {
        audioCapture->stopCapture();
    }
    
    // Save training data if available
    if (detector && detector->hasTrainingData() && configManager) {
        detector->saveTrainingData(configManager->getTrainingDataPath());
    }
    
    if (dashboardManager) {
        dashboardManager->shutdown();
    }
    
    if (vrInput) {
        vrInput->shutdown();
    }
    
    // Save configuration
    if (configManager) {
        configManager->saveDefault();
    }
    
    MICMAP_LOG_INFO("MicMap shutdown complete");
}

void MicMapApp::onTrigger() {
    MICMAP_LOG_INFO("Pattern detected - triggering action");
    
    if (!dashboardManager || !vrInput) {
        return;
    }
    
    auto state = dashboardManager->getDashboardState();
    
    if (state == steamvr::DashboardState::Closed) {
        // Open dashboard
        MICMAP_LOG_DEBUG("Opening dashboard");
        dashboardManager->openDashboard();
    } else if (state == steamvr::DashboardState::Open) {
        // Send click
        auto& config = configManager->getConfig();
        if (config.steamvr.dashboardClickEnabled) {
            MICMAP_LOG_DEBUG("Sending dashboard click");
            vrInput->sendDashboardClick();
        }
    }
}

void MicMapApp::updateVRStatus() {
    if (vrInput && vrStatusLabel) {
        if (vrInput->isVRAvailable()) {
            SetWindowTextW(vrStatusLabel, L"SteamVR: Connected");
        } else {
            SetWindowTextW(vrStatusLabel, L"SteamVR: Not Connected");
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Check for existing instance
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"MicMap is already running.", L"MicMap", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"MicMapMain";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // Create window
    g_app.hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"MicMap - Microphone Pattern Detection",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );
    
    if (!g_app.hwnd) {
        CloseHandle(hMutex);
        return 1;
    }
    
    // Initialize application
    if (!g_app.initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize MicMap.\nCheck that audio devices are available.",
            L"MicMap Error", MB_OK | MB_ICONWARNING);
        // Continue anyway - user can select device
    }
    
    ShowWindow(g_app.hwnd, nCmdShow);
    
    // Set up timer for display updates
    SetTimer(g_app.hwnd, ID_TIMER, 50, nullptr);
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Shutdown
    g_app.shutdown();
    
    CloseHandle(hMutex);
    
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;
            
        case WM_TIMER:
            if (wParam == ID_TIMER) {
                // Update VR status
                if (g_app.dashboardManager) {
                    g_app.dashboardManager->update();
                }
                if (g_app.vrInput) {
                    g_app.vrInput->pollEvents();
                }
                g_app.updateVRStatus();
                
                // Redraw
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            UpdateDisplay(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_HSCROLL:
            if ((HWND)lParam == g_app.sensitivitySlider) {
                int pos = (int)SendMessage(g_app.sensitivitySlider, TBM_GETPOS, 0, 0);
                OnSensitivityChanged(pos);
            }
            return 0;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_DEVICE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int index = (int)SendMessage(g_app.deviceCombo, CB_GETCURSEL, 0, 0);
                        OnDeviceSelected(index);
                    }
                    break;
                case ID_TRAIN_BUTTON:
                    OnTrainClicked();
                    break;
            }
            return 0;
            
        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER);
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CreateControls(HWND hwnd) {
    int y = 10;
    
    // VR Status
    g_app.vrStatusLabel = CreateWindowW(L"STATIC", L"SteamVR: Checking...",
        WS_VISIBLE | WS_CHILD,
        10, y, 460, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 30;
    
    // Device label
    CreateWindowW(L"STATIC", L"Audio Device:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    // Device combo box
    g_app.deviceCombo = CreateWindowW(L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        110, y, 360, 200,
        hwnd, (HMENU)ID_DEVICE_COMBO, nullptr, nullptr);
    
    // Populate device combo
    for (size_t i = 0; i < g_app.devices.size(); ++i) {
        SendMessageW(g_app.deviceCombo, CB_ADDSTRING, 0, 
            (LPARAM)g_app.devices[i].name.c_str());
    }
    if (!g_app.devices.empty()) {
        SendMessage(g_app.deviceCombo, CB_SETCURSEL, 0, 0);
    }
    
    y += 35;
    
    // Sensitivity label
    CreateWindowW(L"STATIC", L"Sensitivity:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    // Sensitivity slider
    g_app.sensitivitySlider = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
        110, y, 300, 30,
        hwnd, (HMENU)ID_SENSITIVITY_SLIDER, nullptr, nullptr);
    
    SendMessage(g_app.sensitivitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(g_app.sensitivitySlider, TBM_SETPOS, TRUE, 70);  // Default 70%
    
    y += 40;
    
    // Train button
    g_app.trainButton = CreateWindowW(L"BUTTON", L"Train Pattern",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 120, 35,
        hwnd, (HMENU)ID_TRAIN_BUTTON, nullptr, nullptr);
    
    y += 45;
    
    // Status label
    g_app.statusLabel = CreateWindowW(L"STATIC", L"Status: Ready",
        WS_VISIBLE | WS_CHILD,
        10, y, 460, 20,
        hwnd, nullptr, nullptr, nullptr);
}

void UpdateDisplay(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    
    // Get client rect
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    
    // Create double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);
    
    int y = 180;
    
    // Draw level meter
    SetBkMode(memDC, TRANSPARENT);
    TextOutW(memDC, 10, y, L"Audio Level:", 12);
    y += 20;
    
    RECT levelRect = {10, y, 470, y + 25};
    DrawEdge(memDC, &levelRect, EDGE_SUNKEN, BF_RECT);
    
    int levelWidth = (int)(g_app.currentLevel * 456);
    RECT levelFill = {12, y + 2, 12 + levelWidth, y + 23};
    HBRUSH levelBrush = CreateSolidBrush(RGB(0, 180, 0));
    FillRect(memDC, &levelFill, levelBrush);
    DeleteObject(levelBrush);
    
    y += 40;
    
    // Draw detection meter
    TextOutW(memDC, 10, y, L"Detection Confidence:", 21);
    y += 20;
    
    RECT confRect = {10, y, 470, y + 25};
    DrawEdge(memDC, &confRect, EDGE_SUNKEN, BF_RECT);
    
    int confWidth = (int)(g_app.currentConfidence * 456);
    RECT confFill = {12, y + 2, 12 + confWidth, y + 23};
    COLORREF confColor = g_app.isDetected ? RGB(255, 100, 0) : RGB(100, 100, 200);
    HBRUSH confBrush = CreateSolidBrush(confColor);
    FillRect(memDC, &confFill, confBrush);
    DeleteObject(confBrush);
    
    y += 35;
    
    // Detection status
    if (g_app.isDetected) {
        HFONT boldFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, boldFont);
        SetTextColor(memDC, RGB(255, 100, 0));
        TextOutW(memDC, 10, y, L"PATTERN DETECTED!", 17);
        SelectObject(memDC, oldFont);
        DeleteObject(boldFont);
    }
    
    y += 30;
    
    // State machine status
    if (g_app.stateMachine) {
        auto state = g_app.stateMachine->getCurrentState();
        std::wstring stateStr = L"State: ";
        switch (state) {
            case core::State::Idle: stateStr += L"Idle"; break;
            case core::State::Training: stateStr += L"Training"; break;
            case core::State::Detecting: stateStr += L"Detecting..."; break;
            case core::State::Triggered: stateStr += L"Triggered!"; break;
            case core::State::Cooldown: stateStr += L"Cooldown"; break;
        }
        TextOutW(memDC, 10, y, stateStr.c_str(), (int)stateStr.length());
    }
    
    y += 25;
    
    // Dashboard state
    if (g_app.dashboardManager) {
        auto dashState = g_app.dashboardManager->getDashboardState();
        std::wstring dashStr = L"Dashboard: ";
        switch (dashState) {
            case steamvr::DashboardState::Open: dashStr += L"Open"; break;
            case steamvr::DashboardState::Closed: dashStr += L"Closed"; break;
            default: dashStr += L"Unknown"; break;
        }
        TextOutW(memDC, 10, y, dashStr.c_str(), (int)dashStr.length());
    }
    
    // Copy to screen
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
}

void OnDeviceSelected(int index) {
    if (index >= 0 && index < (int)g_app.devices.size()) {
        if (g_app.audioCapture) {
            g_app.audioCapture->stopCapture();
            g_app.audioCapture->selectDeviceById(g_app.devices[index].id);
            
            auto device = g_app.audioCapture->getCurrentDevice();
            if (device.sampleRate > 0) {
                g_app.detector = detection::createFFTDetector(device.sampleRate);
                if (g_app.configManager) {
                    auto trainingPath = g_app.configManager->getTrainingDataPath();
                    g_app.detector->loadTrainingData(trainingPath);
                }
            }
            
            g_app.audioCapture->startCapture();
            SetWindowTextW(g_app.statusLabel, L"Status: Device changed");
        }
    }
}

void OnTrainClicked() {
    if (!g_app.detector) return;
    
    if (!g_app.isTraining) {
        g_app.detector->startTraining();
        g_app.isTraining = true;
        SetWindowTextW(g_app.trainButton, L"Stop Training");
        SetWindowTextW(g_app.statusLabel, L"Status: Training... Cover your microphone now!");
    } else {
        if (g_app.detector->finishTraining()) {
            SetWindowTextW(g_app.statusLabel, L"Status: Training complete!");
            // Save training data
            if (g_app.configManager) {
                g_app.detector->saveTrainingData(g_app.configManager->getTrainingDataPath());
            }
        } else {
            SetWindowTextW(g_app.statusLabel, L"Status: Training failed - not enough samples");
        }
        g_app.isTraining = false;
        SetWindowTextW(g_app.trainButton, L"Train Pattern");
    }
}

void OnSensitivityChanged(int value) {
    float sensitivity = value / 100.0f;
    
    if (g_app.detector) {
        g_app.detector->setSensitivity(sensitivity);
    }
    
    if (g_app.stateMachine) {
        auto config = g_app.stateMachine->getConfig();
        config.detectionThreshold = sensitivity;
        g_app.stateMachine->configure(config);
    }
    
    if (g_app.configManager) {
        g_app.configManager->getConfig().detection.sensitivity = sensitivity;
    }
}

#else
// Non-Windows entry point
#include <iostream>
int main(int argc, char* argv[]) {
    std::cerr << "This application requires Windows.\n";
    return 1;
}
#endif
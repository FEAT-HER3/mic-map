/**
 * @file main.cpp
 * @brief MicMap Audio Test Application - Win32 GUI
 *
 * Test Program 1: Audio capture and white noise detection test
 * - Device enumeration and selection dropdown
 * - Real-time audio level meter
 * - Training and detection testing
 * - Profile save/load functionality
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

#include "micmap/audio/audio_capture.hpp"
#include "micmap/audio/device_enumerator.hpp"
#include "micmap/detection/noise_detector.hpp"
#include "micmap/common/logger.hpp"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <cmath>
#include <chrono>
#include <iostream>

using namespace micmap;

// Window dimensions
constexpr int WINDOW_WIDTH = 520;
constexpr int WINDOW_HEIGHT = 500;

// Control IDs
constexpr UINT_PTR ID_DEVICE_COMBO = 101;
constexpr UINT_PTR ID_TRAIN_BUTTON = 102;
constexpr UINT_PTR ID_CLEAR_BUTTON = 103;
constexpr UINT_PTR ID_SAVE_BUTTON = 104;
constexpr UINT_PTR ID_LOAD_BUTTON = 105;
constexpr UINT_PTR ID_TIMER = 106;

// Detection timing
constexpr int BUTTON_FIRE_DURATION_MS = 300;
constexpr int MIN_TRAINING_SAMPLES = 50;  // Valid samples needed (detector may reject some)

// Global state
struct AppState {
    std::unique_ptr<audio::IAudioCapture> audioCapture;
    std::unique_ptr<detection::INoiseDetector> detector;
    std::vector<audio::AudioDevice> devices;
    
    // Thread-safe state updated from audio callback
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
    
    // Button fire tracking
    std::chrono::steady_clock::time_point detectionStartTime;
    std::atomic<bool> detectionActive{false};
    std::atomic<bool> buttonWouldFire{false};
    std::atomic<int> detectionDurationMs{0};
    
    // Selected device index
    int selectedDeviceIndex = -1;
    
    // Window handles
    HWND hwnd = nullptr;
    HWND deviceCombo = nullptr;
    HWND trainButton = nullptr;
    HWND clearButton = nullptr;
    HWND saveButton = nullptr;
    HWND loadButton = nullptr;
    HWND deviceStatusLabel = nullptr;
    HWND trainingStatusLabel = nullptr;
    HWND trainingProgressLabel = nullptr;
};

static AppState g_state;

#ifdef _WIN32

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void UpdateDisplay(HDC hdc, RECT* updateRect);
void OnDeviceSelected(int index);
void OnTrainClicked();
void OnClearClicked();
void OnSaveClicked();
void OnLoadClicked();
void UpdateDeviceStatus();
void UpdateTrainingStatus();

// Helper function to convert linear amplitude to dB
float LinearToDb(float linear) {
    if (linear <= 0.0f) return -60.0f;
    float db = 20.0f * std::log10(linear);
    return (db < -60.0f) ? -60.0f : db;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);
    
    // Initialize audio capture BEFORE creating window so devices are available
    g_state.audioCapture = audio::createWASAPICapture();
    if (g_state.audioCapture) {
        g_state.devices = g_state.audioCapture->enumerateDevices();
        
        // Find device with "Beyond" in name, or use first device
        int beyondIndex = -1;
        for (size_t i = 0; i < g_state.devices.size(); ++i) {
            if (g_state.devices[i].name.find(L"Beyond") != std::wstring::npos) {
                beyondIndex = static_cast<int>(i);
                break;
            }
        }
        
        bool selected = false;
        if (beyondIndex >= 0) {
            selected = g_state.audioCapture->selectDeviceById(g_state.devices[beyondIndex].id);
            if (selected) {
                g_state.selectedDeviceIndex = beyondIndex;
            }
        }
        
        if (!selected && !g_state.devices.empty()) {
            selected = g_state.audioCapture->selectDeviceById(g_state.devices[0].id);
            if (selected) {
                g_state.selectedDeviceIndex = 0;
            }
        }
        
        auto device = g_state.audioCapture->getCurrentDevice();
        if (device.sampleRate > 0) {
            g_state.detector = detection::createFFTDetector(device.sampleRate);
            if (g_state.detector) {
                g_state.detector->setMinDetectionDuration(BUTTON_FIRE_DURATION_MS);
            }
        }
        
        // Set audio callback
        g_state.audioCapture->setAudioCallback([](const float* samples, size_t count) {
            // Calculate RMS level
            float rms = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                rms += samples[i] * samples[i];
            }
            rms = std::sqrt(rms / count);
            
            // Scale for display (0-1 range)
            float scaledLevel = rms * 10.0f;
            g_state.currentLevel = (scaledLevel > 1.0f) ? 1.0f : scaledLevel;
            g_state.currentLevelDb = LinearToDb(rms);
            
            // Training or detection (only detect if we have a profile)
            if (g_state.detector) {
                if (g_state.isTraining) {
                    g_state.detector->addTrainingSample(samples, count);
                    g_state.trainingSampleCount++;
                    // Note: trainingSampleCount tracks calls, not accepted samples
                    // The detector internally filters samples by energy and flatness
                } else if (g_state.detector->hasTrainingData()) {
                    // Only run detection if we have training data
                    auto result = g_state.detector->analyze(samples, count);
                    g_state.currentConfidence = result.confidence;
                    g_state.currentSpectralFlatness = result.spectralFlatness;
                    g_state.currentEnergy = result.energy;
                    g_state.currentEnergyDb = LinearToDb(result.energy);
                    g_state.isDetected = result.isWhiteNoise;
                    
                    // Track detection duration for button fire
                    if (result.isWhiteNoise) {
                        if (!g_state.detectionActive) {
                            g_state.detectionStartTime = std::chrono::steady_clock::now();
                            g_state.detectionActive = true;
                        }
                        
                        auto now = std::chrono::steady_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - g_state.detectionStartTime).count();
                        g_state.detectionDurationMs = static_cast<int>(duration);
                        
                        if (duration >= BUTTON_FIRE_DURATION_MS) {
                            g_state.buttonWouldFire = true;
                        }
                    } else {
                        g_state.detectionActive = false;
                        g_state.buttonWouldFire = false;
                        g_state.detectionDurationMs = 0;
                    }
                } else {
                    // No profile - reset detection state
                    g_state.currentConfidence = 0.0f;
                    g_state.currentSpectralFlatness = 0.0f;
                    g_state.currentEnergy = 0.0f;
                    g_state.currentEnergyDb = -60.0f;
                    g_state.isDetected = false;
                    g_state.detectionActive = false;
                    g_state.buttonWouldFire = false;
                    g_state.detectionDurationMs = 0;
                }
                
                g_state.hasProfile = g_state.detector->hasTrainingData();
            }
        });
        
        // Start audio capture BEFORE creating window so status is correct
        g_state.audioCapture->startCapture();
    }
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"MicMapAudioTest";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // Create window (fixed size) - use explicit wide string for title
    const wchar_t* windowTitle = L"MicMap - Microphone Test";
    g_state.hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        windowTitle,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );
    
    if (!g_state.hwnd) {
        return 0;
    }
    
    // Set window title explicitly after creation to ensure it's set correctly
    SetWindowTextW(g_state.hwnd, L"MicMap - Microphone Test");
    
    ShowWindow(g_state.hwnd, nCmdShow);
    
    // Set up timer for display updates (~20 Hz)
    SetTimer(g_state.hwnd, ID_TIMER, 50, nullptr);
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    if (g_state.audioCapture) {
        g_state.audioCapture->stopCapture();
    }
    
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;
            
        case WM_TIMER:
            if (wParam == ID_TIMER) {
                // Check if training should auto-stop (after collecting enough samples)
                // We use a higher threshold for auto-stop since not all samples are accepted
                if (g_state.isTraining && g_state.trainingSampleCount >= MIN_TRAINING_SAMPLES * 3) {
                    // Try to finish training - detector will check if it has enough valid samples
                    if (g_state.detector) {
                        bool success = g_state.detector->finishTraining();
                        g_state.isTraining = false;
                        if (success) {
                            g_state.hasProfile = true;
                            MessageBoxW(hwnd, L"Training completed automatically!\nProfile is ready.",
                                L"Training Complete", MB_OK | MB_ICONINFORMATION);
                        } else {
                            MessageBoxW(hwnd,
                                L"Training stopped but not enough valid samples were collected.\n"
                                L"Make sure to cover the microphone firmly to create white noise.\n"
                                L"Try again with a longer duration.",
                                L"Training Incomplete", MB_OK | MB_ICONWARNING);
                        }
                    }
                }
                
                UpdateTrainingStatus();
                // Only invalidate the custom-drawn regions (level meter and detection area)
                RECT levelRect = {10, 102, 490, 130};
                RECT detectionRect = {10, 287, 490, 440};
                InvalidateRect(hwnd, &levelRect, FALSE);
                InvalidateRect(hwnd, &detectionRect, FALSE);
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            UpdateDisplay(hdc, &ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_DEVICE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int index = (int)SendMessage(g_state.deviceCombo, CB_GETCURSEL, 0, 0);
                        OnDeviceSelected(index);
                    }
                    break;
                case ID_TRAIN_BUTTON:
                    OnTrainClicked();
                    break;
                case ID_CLEAR_BUTTON:
                    OnClearClicked();
                    break;
                case ID_SAVE_BUTTON:
                    OnSaveClicked();
                    break;
                case ID_LOAD_BUTTON:
                    OnLoadClicked();
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
    int controlX = 90;
    int controlWidth = 390;
    
    // Device section
    CreateWindowW(L"STATIC", L"Device:",
        WS_VISIBLE | WS_CHILD,
        10, y + 3, 75, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.deviceCombo = CreateWindowW(L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        controlX, y, controlWidth, 200,
        hwnd, (HMENU)ID_DEVICE_COMBO, nullptr, nullptr);
    
    // Populate device combo
    for (size_t i = 0; i < g_state.devices.size(); ++i) {
        SendMessageW(g_state.deviceCombo, CB_ADDSTRING, 0,
            (LPARAM)g_state.devices[i].name.c_str());
    }
    if (g_state.selectedDeviceIndex >= 0) {
        SendMessage(g_state.deviceCombo, CB_SETCURSEL, g_state.selectedDeviceIndex, 0);
    } else if (!g_state.devices.empty()) {
        SendMessage(g_state.deviceCombo, CB_SETCURSEL, 0, 0);
    }
    
    y += 30;
    
    // Device status label
    g_state.deviceStatusLabel = CreateWindowW(L"STATIC", L"Status: Not capturing",
        WS_VISIBLE | WS_CHILD,
        controlX, y, controlWidth, 20,
        hwnd, nullptr, nullptr, nullptr);
    UpdateDeviceStatus();
    
    y += 30;
    
    // Separator
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        10, y, 480, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    // Audio Level section - y is now 80
    // Level meter will be drawn at y+22 to y+47 (25px height)
    CreateWindowW(L"STATIC", L"Audio Level:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    // Store the Y position for level meter drawing
    // Level meter drawn at: y+22 = 102 to y+47 = 127
    
    y += 55; // Skip past level meter area (y is now 135)
    
    // Separator
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        10, y, 480, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    // Training section header - y is now 145
    CreateWindowW(L"STATIC", L"Training:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Training buttons - y is now 170
    g_state.trainButton = CreateWindowW(L"BUTTON", L"Start Training",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 110, 28,
        hwnd, (HMENU)ID_TRAIN_BUTTON, nullptr, nullptr);
    
    g_state.clearButton = CreateWindowW(L"BUTTON", L"Clear",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        130, y, 70, 28,
        hwnd, (HMENU)ID_CLEAR_BUTTON, nullptr, nullptr);
    
    g_state.saveButton = CreateWindowW(L"BUTTON", L"Save Profile",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        210, y, 100, 28,
        hwnd, (HMENU)ID_SAVE_BUTTON, nullptr, nullptr);
    
    g_state.loadButton = CreateWindowW(L"BUTTON", L"Load Profile",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        320, y, 100, 28,
        hwnd, (HMENU)ID_LOAD_BUTTON, nullptr, nullptr);
    
    y += 35;
    
    // Training status - y is now 205
    g_state.trainingStatusLabel = CreateWindowW(L"STATIC", L"Status: No profile loaded",
        WS_VISIBLE | WS_CHILD,
        10, y, 470, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 22;
    
    // Training progress - y is now 227
    g_state.trainingProgressLabel = CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD,
        10, y, 470, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 28;
    
    // Separator - y is now 255
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        10, y, 480, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    // Detection section header - y is now 265
    // Detection area will be drawn starting at y+22 = 287
    CreateWindowW(L"STATIC", L"Detection:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
}

void UpdateDisplay(HDC hdc, RECT* updateRect) {
    // Set up text
    SetBkMode(hdc, TRANSPARENT);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    
    // Level meter area: starts at y=80 (Audio Level label), meter at y=102-127
    RECT levelArea = {10, 102, 490, 130};
    RECT intersection;
    if (IntersectRect(&intersection, updateRect, &levelArea)) {
        // Fill background for level meter area
        HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(hdc, &levelArea, bgBrush);
        DeleteObject(bgBrush);
        
        // Level meter background
        RECT levelRect = {10, 102, 480, 127};
        DrawEdge(hdc, &levelRect, EDGE_SUNKEN, BF_RECT);
        
        // Level meter fill
        float level = g_state.currentLevel.load();
        int levelWidth = (int)(level * 466);
        if (levelWidth > 0) {
            RECT levelFill = {12, 104, 12 + levelWidth, 125};
            HBRUSH levelBrush = CreateSolidBrush(RGB(0, 180, 0));
            FillRect(hdc, &levelFill, levelBrush);
            DeleteObject(levelBrush);
        }
        
        // Level text
        wchar_t levelText[64];
        float levelDb = g_state.currentLevelDb.load();
        swprintf_s(levelText, L"%.1f dB", levelDb);
        RECT levelTextRect = {12, 104, 478, 125};
        DrawTextW(hdc, levelText, -1, &levelTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    
    // Detection area: starts at y=265 (Detection label), content at y=287+
    RECT detectionArea = {10, 287, 490, 440};
    if (IntersectRect(&intersection, updateRect, &detectionArea)) {
        // Fill background for detection area
        HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(hdc, &detectionArea, bgBrush);
        DeleteObject(bgBrush);
        
        int y = 287;
        
        // Confidence meter
        wchar_t confLabel[32];
        swprintf_s(confLabel, L"Confidence:");
        TextOutW(hdc, 10, y, confLabel, (int)wcslen(confLabel));
        
        RECT confRect = {100, y - 2, 400, y + 20};
        DrawEdge(hdc, &confRect, EDGE_SUNKEN, BF_RECT);
        
        float confidence = g_state.currentConfidence.load();
        int confWidth = (int)(confidence * 296);
        if (confWidth > 0) {
            RECT confFill = {102, y, 102 + confWidth, y + 18};
            COLORREF confColor = g_state.isDetected ? RGB(255, 140, 0) : RGB(100, 100, 180);
            HBRUSH confBrush = CreateSolidBrush(confColor);
            FillRect(hdc, &confFill, confBrush);
            DeleteObject(confBrush);
        }
        
        wchar_t confText[32];
        swprintf_s(confText, L"%.0f%%", confidence * 100.0f);
        TextOutW(hdc, 410, y, confText, (int)wcslen(confText));
        
        y += 28;
        
        // Spectral Flatness
        wchar_t flatnessText[64];
        swprintf_s(flatnessText, L"Spectral Flatness: %.3f", g_state.currentSpectralFlatness.load());
        TextOutW(hdc, 10, y, flatnessText, (int)wcslen(flatnessText));
        
        y += 22;
        
        // Energy
        wchar_t energyText[64];
        swprintf_s(energyText, L"Energy: %.1f dB", g_state.currentEnergyDb.load());
        TextOutW(hdc, 10, y, energyText, (int)wcslen(energyText));
        
        y += 30;
        
        // Detection indicator box
        RECT detectionBox = {10, y, 480, y + 55};
        
        bool buttonFire = g_state.buttonWouldFire.load();
        bool detected = g_state.isDetected.load();
        
        COLORREF boxColor;
        if (buttonFire) {
            boxColor = RGB(0, 200, 0);  // Green - button would fire
        } else if (detected) {
            boxColor = RGB(255, 200, 0);  // Yellow - detected but not long enough
        } else {
            boxColor = RGB(60, 60, 60);  // Dark gray - not detected
        }
        
        HBRUSH boxBrush = CreateSolidBrush(boxColor);
        FillRect(hdc, &detectionBox, boxBrush);
        DeleteObject(boxBrush);
        
        // Draw border
        HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);
        Rectangle(hdc, detectionBox.left, detectionBox.top, detectionBox.right, detectionBox.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);
        
        // Detection text
        SetTextColor(hdc, RGB(255, 255, 255));
        wchar_t detectionText[128];
        if (buttonFire) {
            swprintf_s(detectionText, L"DETECTED - BUTTON WOULD FIRE");
        } else if (detected) {
            int durationMs = g_state.detectionDurationMs.load();
            swprintf_s(detectionText, L"DETECTING... (%d ms / %d ms)", durationMs, BUTTON_FIRE_DURATION_MS);
        } else {
            swprintf_s(detectionText, L"NOT DETECTED");
        }
        
        RECT textRect = detectionBox;
        DrawTextW(hdc, detectionText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        SetTextColor(hdc, RGB(0, 0, 0));
    }
    
    SelectObject(hdc, oldFont);
}

void UpdateDeviceStatus() {
    if (!g_state.deviceStatusLabel) return;
    
    wchar_t statusText[256];
    if (g_state.audioCapture && g_state.audioCapture->isCapturing()) {
        auto device = g_state.audioCapture->getCurrentDevice();
        swprintf_s(statusText, L"Capturing: \"%s\" (%u Hz)",
            device.name.c_str(), device.sampleRate);
    } else {
        swprintf_s(statusText, L"Not capturing");
    }
    SetWindowTextW(g_state.deviceStatusLabel, statusText);
}

void UpdateTrainingStatus() {
    if (!g_state.trainingStatusLabel || !g_state.trainingProgressLabel) return;
    
    wchar_t statusText[256];
    wchar_t progressText[256];
    
    if (g_state.isTraining) {
        int samples = g_state.trainingSampleCount.load();
        swprintf_s(statusText, L"Status: Training in progress...");
        swprintf_s(progressText, L"Collecting samples: %d (cover mic with finger)", samples);
        SetWindowTextW(g_state.trainButton, L"Stop Training");
    } else if (g_state.hasProfile) {
        swprintf_s(statusText, L"Status: Profile trained and ready");
        swprintf_s(progressText, L"");
        SetWindowTextW(g_state.trainButton, L"Start Training");
    } else {
        swprintf_s(statusText, L"Status: No profile loaded");
        swprintf_s(progressText, L"");
        SetWindowTextW(g_state.trainButton, L"Start Training");
    }
    
    SetWindowTextW(g_state.trainingStatusLabel, statusText);
    SetWindowTextW(g_state.trainingProgressLabel, progressText);
}

void OnDeviceSelected(int index) {
    if (index >= 0 && index < (int)g_state.devices.size()) {
        if (g_state.audioCapture) {
            g_state.audioCapture->stopCapture();
            
            if (g_state.audioCapture->selectDeviceById(g_state.devices[index].id)) {
                g_state.selectedDeviceIndex = index;
                
                auto device = g_state.audioCapture->getCurrentDevice();
                if (device.sampleRate > 0) {
                    g_state.detector = detection::createFFTDetector(device.sampleRate);
                    if (g_state.detector) {
                        g_state.detector->setMinDetectionDuration(BUTTON_FIRE_DURATION_MS);
                    }
                    g_state.hasProfile = false;
                }
                
                g_state.audioCapture->startCapture();
            }
            
            UpdateDeviceStatus();
            UpdateTrainingStatus();
        }
    }
}

void OnTrainClicked() {
    if (!g_state.detector) return;
    
    if (!g_state.isTraining) {
        // Prompt user before starting training
        int result = MessageBoxW(g_state.hwnd,
            L"Training will begin when you click OK.\n\n"
            L"Please FIRMLY cover your microphone with your finger to create\n"
            L"the white noise pattern. The detector needs samples with:\n"
            L"  - Sufficient energy (press firmly)\n"
            L"  - High spectral flatness (characteristic of white noise)\n\n"
            L"Training will automatically complete after collecting enough\n"
            L"valid samples, or you can click 'Stop Training' to finish early.\n\n"
            L"Click OK when ready to start.",
            L"Start Training", MB_OKCANCEL | MB_ICONINFORMATION);
        
        if (result == IDOK) {
            g_state.detector->startTraining();
            g_state.isTraining = true;
            g_state.trainingSampleCount = 0;
        }
    } else {
        // Manual stop - finish training
        g_state.isTraining = false;
        if (g_state.detector->finishTraining()) {
            g_state.hasProfile = true;
            MessageBoxW(g_state.hwnd, L"Training completed successfully!\nProfile is ready for detection.",
                L"Training Complete", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_state.hwnd,
                L"Training stopped but not enough valid samples were collected.\n\n"
                L"The detector requires samples with:\n"
                L"  - Energy > 0.001 (press mic firmly)\n"
                L"  - Spectral flatness > 0.1 (white noise characteristic)\n\n"
                L"Try again and make sure to cover the microphone firmly.",
                L"Training Incomplete", MB_OK | MB_ICONWARNING);
        }
    }
    
    UpdateTrainingStatus();
}

void OnClearClicked() {
    if (!g_state.detector) return;
    
    if (g_state.isTraining) {
        g_state.isTraining = false;
    }
    
    auto device = g_state.audioCapture->getCurrentDevice();
    if (device.sampleRate > 0) {
        g_state.detector = detection::createFFTDetector(device.sampleRate);
        if (g_state.detector) {
            g_state.detector->setMinDetectionDuration(BUTTON_FIRE_DURATION_MS);
        }
    }
    
    g_state.hasProfile = false;
    g_state.trainingSampleCount = 0;
    
    UpdateTrainingStatus();
}

void OnSaveClicked() {
    if (!g_state.detector || !g_state.detector->hasTrainingData()) {
        MessageBoxW(g_state.hwnd, L"No training data to save.\nTrain a profile first.",
            L"Save Profile", MB_OK | MB_ICONWARNING);
        return;
    }
    
    wchar_t filename[MAX_PATH] = L"micmap_profile.bin";
    
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_state.hwnd;
    ofn.lpstrFilter = L"MicMap Profile (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"bin";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileNameW(&ofn)) {
        char narrowPath[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, narrowPath, MAX_PATH, nullptr, nullptr);
        
        if (g_state.detector->saveTrainingData(narrowPath)) {
            MessageBoxW(g_state.hwnd, L"Profile saved successfully!", L"Save Profile", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_state.hwnd, L"Failed to save profile.", L"Save Profile", MB_OK | MB_ICONERROR);
        }
    }
}

void OnLoadClicked() {
    if (!g_state.detector) return;
    
    wchar_t filename[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_state.hwnd;
    ofn.lpstrFilter = L"MicMap Profile (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    
    if (GetOpenFileNameW(&ofn)) {
        char narrowPath[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, narrowPath, MAX_PATH, nullptr, nullptr);
        
        if (g_state.detector->loadTrainingData(narrowPath)) {
            g_state.hasProfile = true;
            MessageBoxW(g_state.hwnd, L"Profile loaded successfully!", L"Load Profile", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_state.hwnd, L"Failed to load profile.", L"Load Profile", MB_OK | MB_ICONERROR);
        }
    }
    
    UpdateTrainingStatus();
}

#else
// Non-Windows stub
int main() {
    std::cerr << "This application requires Windows.\n";
    return 1;
}
#endif
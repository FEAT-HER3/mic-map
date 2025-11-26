/**
 * @file main.cpp
 * @brief MicMap Audio Test Application - Win32 GUI
 * 
 * Test Program 1: Audio capture and white noise detection test
 * - Device enumeration and selection dropdown
 * - Real-time audio level meter
 * - FFT spectrum display
 * - Training and detection testing
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")
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

using namespace micmap;

// Window dimensions
constexpr int WINDOW_WIDTH = 500;
constexpr int WINDOW_HEIGHT = 400;

// Control IDs
constexpr int ID_DEVICE_COMBO = 101;
constexpr int ID_TRAIN_BUTTON = 102;
constexpr int ID_SAVE_BUTTON = 103;
constexpr int ID_LOAD_BUTTON = 104;
constexpr int ID_TIMER = 105;

// Global state
struct AppState {
    std::unique_ptr<audio::IAudioCapture> audioCapture;
    std::unique_ptr<detection::INoiseDetector> detector;
    std::vector<audio::AudioDevice> devices;
    
    std::atomic<float> currentLevel{0.0f};
    std::atomic<float> currentConfidence{0.0f};
    std::atomic<bool> isDetected{false};
    std::atomic<bool> isTraining{false};
    
    HWND hwnd = nullptr;
    HWND deviceCombo = nullptr;
    HWND trainButton = nullptr;
    HWND saveButton = nullptr;
    HWND loadButton = nullptr;
    HWND statusLabel = nullptr;
};

static AppState g_state;

#ifdef _WIN32

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void UpdateDisplay(HWND hwnd);
void OnDeviceSelected(int index);
void OnTrainClicked();
void OnSaveClicked();
void OnLoadClicked();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"MicMapAudioTest";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // Create window
    g_state.hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"MicMap Audio Test",
        WS_OVERLAPPEDWINDOW,
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
    
    // Initialize audio capture
    g_state.audioCapture = audio::createWASAPICapture();
    if (g_state.audioCapture) {
        g_state.devices = g_state.audioCapture->enumerateDevices();
        
        // Select first device or one matching "Beyond"
        bool selected = g_state.audioCapture->selectDevice(L"Beyond");
        if (!selected && !g_state.devices.empty()) {
            g_state.audioCapture->selectDeviceById(g_state.devices[0].id);
        }
        
        auto device = g_state.audioCapture->getCurrentDevice();
        if (device.sampleRate > 0) {
            g_state.detector = detection::createFFTDetector(device.sampleRate);
        }
        
        // Set audio callback
        g_state.audioCapture->setAudioCallback([](const float* samples, size_t count) {
            // Calculate RMS level
            float rms = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                rms += samples[i] * samples[i];
            }
            rms = std::sqrt(rms / count);
            float scaledLevel = rms * 10.0f;
            g_state.currentLevel = (scaledLevel > 1.0f) ? 1.0f : scaledLevel;
            
            // Training or detection
            if (g_state.detector) {
                if (g_state.isTraining) {
                    g_state.detector->addTrainingSample(samples, count);
                } else {
                    auto result = g_state.detector->analyze(samples, count);
                    g_state.currentConfidence = result.confidence;
                    g_state.isDetected = result.isWhiteNoise;
                }
            }
        });
        
        g_state.audioCapture->startCapture();
    }
    
    ShowWindow(g_state.hwnd, nCmdShow);
    
    // Set up timer for display updates
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
    
    // Device label
    CreateWindowW(L"STATIC", L"Audio Device:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    // Device combo box
    g_state.deviceCombo = CreateWindowW(L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        110, y, 360, 200,
        hwnd, (HMENU)ID_DEVICE_COMBO, nullptr, nullptr);
    
    // Populate device combo
    for (size_t i = 0; i < g_state.devices.size(); ++i) {
        SendMessageW(g_state.deviceCombo, CB_ADDSTRING, 0, 
            (LPARAM)g_state.devices[i].name.c_str());
    }
    if (!g_state.devices.empty()) {
        SendMessage(g_state.deviceCombo, CB_SETCURSEL, 0, 0);
    }
    
    y += 40;
    
    // Buttons
    g_state.trainButton = CreateWindowW(L"BUTTON", L"Train",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 80, 30,
        hwnd, (HMENU)ID_TRAIN_BUTTON, nullptr, nullptr);
    
    g_state.saveButton = CreateWindowW(L"BUTTON", L"Save",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        100, y, 80, 30,
        hwnd, (HMENU)ID_SAVE_BUTTON, nullptr, nullptr);
    
    g_state.loadButton = CreateWindowW(L"BUTTON", L"Load",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        190, y, 80, 30,
        hwnd, (HMENU)ID_LOAD_BUTTON, nullptr, nullptr);
    
    y += 40;
    
    // Status label
    g_state.statusLabel = CreateWindowW(L"STATIC", L"Status: Ready",
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
    
    int y = 130;
    
    // Draw level meter
    RECT levelRect = {10, y, 470, y + 30};
    DrawEdge(memDC, &levelRect, EDGE_SUNKEN, BF_RECT);
    
    int levelWidth = (int)(g_state.currentLevel * 456);
    RECT levelFill = {12, y + 2, 12 + levelWidth, y + 28};
    HBRUSH levelBrush = CreateSolidBrush(RGB(0, 200, 0));
    FillRect(memDC, &levelFill, levelBrush);
    DeleteObject(levelBrush);
    
    // Level label
    SetBkMode(memDC, TRANSPARENT);
    wchar_t levelText[64];
    swprintf_s(levelText, L"Level: %.0f%%", g_state.currentLevel * 100.0f);
    TextOutW(memDC, 10, y + 35, levelText, (int)wcslen(levelText));
    
    y += 70;
    
    // Draw detection meter
    RECT confRect = {10, y, 470, y + 30};
    DrawEdge(memDC, &confRect, EDGE_SUNKEN, BF_RECT);
    
    int confWidth = (int)(g_state.currentConfidence * 456);
    RECT confFill = {12, y + 2, 12 + confWidth, y + 28};
    COLORREF confColor = g_state.isDetected ? RGB(255, 100, 0) : RGB(100, 100, 200);
    HBRUSH confBrush = CreateSolidBrush(confColor);
    FillRect(memDC, &confFill, confBrush);
    DeleteObject(confBrush);
    
    // Detection label
    wchar_t confText[64];
    swprintf_s(confText, L"Detection: %.0f%% %s", 
        g_state.currentConfidence * 100.0f,
        g_state.isDetected ? L"[DETECTED]" : L"");
    TextOutW(memDC, 10, y + 35, confText, (int)wcslen(confText));
    
    y += 70;
    
    // Draw spectrum placeholder
    RECT specRect = {10, y, 470, y + 80};
    DrawEdge(memDC, &specRect, EDGE_SUNKEN, BF_RECT);
    TextOutW(memDC, 200, y + 35, L"Spectrum", 8);
    
    // Copy to screen
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
}

void OnDeviceSelected(int index) {
    if (index >= 0 && index < (int)g_state.devices.size()) {
        if (g_state.audioCapture) {
            g_state.audioCapture->stopCapture();
            g_state.audioCapture->selectDeviceById(g_state.devices[index].id);
            
            auto device = g_state.audioCapture->getCurrentDevice();
            if (device.sampleRate > 0) {
                g_state.detector = detection::createFFTDetector(device.sampleRate);
            }
            
            g_state.audioCapture->startCapture();
            SetWindowTextW(g_state.statusLabel, L"Status: Device changed");
        }
    }
}

void OnTrainClicked() {
    if (!g_state.detector) return;
    
    if (!g_state.isTraining) {
        g_state.detector->startTraining();
        g_state.isTraining = true;
        SetWindowTextW(g_state.trainButton, L"Stop");
        SetWindowTextW(g_state.statusLabel, L"Status: Training... Cover your microphone");
    } else {
        if (g_state.detector->finishTraining()) {
            SetWindowTextW(g_state.statusLabel, L"Status: Training complete!");
        } else {
            SetWindowTextW(g_state.statusLabel, L"Status: Training failed - not enough samples");
        }
        g_state.isTraining = false;
        SetWindowTextW(g_state.trainButton, L"Train");
    }
}

void OnSaveClicked() {
    if (g_state.detector && g_state.detector->hasTrainingData()) {
        if (g_state.detector->saveTrainingData("training_data.bin")) {
            SetWindowTextW(g_state.statusLabel, L"Status: Training data saved");
        } else {
            SetWindowTextW(g_state.statusLabel, L"Status: Failed to save training data");
        }
    } else {
        SetWindowTextW(g_state.statusLabel, L"Status: No training data to save");
    }
}

void OnLoadClicked() {
    if (g_state.detector) {
        if (g_state.detector->loadTrainingData("training_data.bin")) {
            SetWindowTextW(g_state.statusLabel, L"Status: Training data loaded");
        } else {
            SetWindowTextW(g_state.statusLabel, L"Status: Failed to load training data");
        }
    }
}

#else
// Non-Windows stub
int main() {
    std::cerr << "This application requires Windows.\n";
    return 1;
}
#endif
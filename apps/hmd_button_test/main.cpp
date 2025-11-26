/**
 * @file main.cpp
 * @brief MicMap HMD Button Test Application - Win32 GUI
 * 
 * Test Program 2: SteamVR HMD button event testing
 * - SteamVR connection status
 * - Dashboard state indicator
 * - Manual trigger button
 * - Event log display
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")
#endif

#include "micmap/steamvr/vr_input.hpp"
#include "micmap/steamvr/dashboard_manager.hpp"
#include "micmap/common/logger.hpp"

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <chrono>

using namespace micmap;

// Window dimensions
constexpr int WINDOW_WIDTH = 450;
constexpr int WINDOW_HEIGHT = 450;

// Control IDs
constexpr int ID_SEND_BUTTON = 101;
constexpr int ID_CLICK_BUTTON = 102;
constexpr int ID_RECONNECT_BUTTON = 103;
constexpr int ID_LOG_LIST = 104;
constexpr int ID_TIMER = 105;

// Global state
struct AppState {
    std::unique_ptr<steamvr::IVRInput> vrInput;
    std::unique_ptr<steamvr::IDashboardManager> dashboardManager;
    
    bool vrConnected = false;
    steamvr::DashboardState dashboardState = steamvr::DashboardState::Unknown;
    
    HWND hwnd = nullptr;
    HWND statusLabel = nullptr;
    HWND dashboardLabel = nullptr;
    HWND sendButton = nullptr;
    HWND clickButton = nullptr;
    HWND reconnectButton = nullptr;
    HWND logList = nullptr;
};

static AppState g_state;

#ifdef _WIN32

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void UpdateStatus();
void AddLogEntry(const std::wstring& message);
void OnSendClicked();
void OnClickClicked();
void OnReconnectClicked();

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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"MicMapSteamVRTest";
    
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
        L"MicMap SteamVR Test",
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
    
    // Initialize VR input
    g_state.vrInput = steamvr::createOpenXRInput();
    g_state.dashboardManager = steamvr::createDashboardManager();
    
    if (g_state.vrInput) {
        // Set up event callback
        g_state.vrInput->setEventCallback([](const steamvr::VREvent& event) {
            switch (event.type) {
                case steamvr::VREventType::DashboardOpened:
                    AddLogEntry(L"Dashboard opened");
                    break;
                case steamvr::VREventType::DashboardClosed:
                    AddLogEntry(L"Dashboard closed");
                    break;
                case steamvr::VREventType::ButtonPressed:
                    AddLogEntry(L"Button pressed");
                    break;
                case steamvr::VREventType::ButtonReleased:
                    AddLogEntry(L"Button released");
                    break;
                case steamvr::VREventType::Quit:
                    AddLogEntry(L"VR quit event");
                    break;
                default:
                    break;
            }
        });
        
        AddLogEntry(L"Initializing VR...");
        g_state.vrConnected = g_state.vrInput->initialize();
        
        if (g_state.vrConnected) {
            std::wstring runtime(g_state.vrInput->getRuntimeName().begin(), 
                                g_state.vrInput->getRuntimeName().end());
            AddLogEntry(L"Connected to " + runtime);
        } else {
            AddLogEntry(L"VR not available (stub mode)");
        }
        
        // Initialize dashboard manager with shared VR input
        auto sharedInput = std::shared_ptr<steamvr::IVRInput>(
            g_state.vrInput.release(),
            [](steamvr::IVRInput* p) { delete p; }
        );
        g_state.dashboardManager->initialize(sharedInput);
        
        // Recreate vrInput for our use
        g_state.vrInput = steamvr::createOpenXRInput();
        g_state.vrInput->initialize();
    }
    
    AddLogEntry(L"Ready");
    
    ShowWindow(g_state.hwnd, nCmdShow);
    
    // Set up timer for status updates
    SetTimer(g_state.hwnd, ID_TIMER, 500, nullptr);
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    if (g_state.dashboardManager) {
        g_state.dashboardManager->shutdown();
    }
    if (g_state.vrInput) {
        g_state.vrInput->shutdown();
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
                if (g_state.dashboardManager) {
                    g_state.dashboardManager->update();
                }
                UpdateStatus();
            }
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SEND_BUTTON:
                    OnSendClicked();
                    break;
                case ID_CLICK_BUTTON:
                    OnClickClicked();
                    break;
                case ID_RECONNECT_BUTTON:
                    OnReconnectClicked();
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
    
    // SteamVR status
    CreateWindowW(L"STATIC", L"SteamVR:",
        WS_VISIBLE | WS_CHILD,
        10, y, 80, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.statusLabel = CreateWindowW(L"STATIC", L"Initializing...",
        WS_VISIBLE | WS_CHILD,
        100, y, 300, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 30;
    
    // Dashboard status
    CreateWindowW(L"STATIC", L"Dashboard:",
        WS_VISIBLE | WS_CHILD,
        10, y, 80, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.dashboardLabel = CreateWindowW(L"STATIC", L"Unknown",
        WS_VISIBLE | WS_CHILD,
        100, y, 300, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 40;
    
    // Buttons
    g_state.sendButton = CreateWindowW(L"BUTTON", L"Send HMD Button Event",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 180, 35,
        hwnd, (HMENU)ID_SEND_BUTTON, nullptr, nullptr);
    
    g_state.clickButton = CreateWindowW(L"BUTTON", L"Send Dashboard Click",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        200, y, 180, 35,
        hwnd, (HMENU)ID_CLICK_BUTTON, nullptr, nullptr);
    
    y += 45;
    
    g_state.reconnectButton = CreateWindowW(L"BUTTON", L"Reconnect to SteamVR",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 180, 35,
        hwnd, (HMENU)ID_RECONNECT_BUTTON, nullptr, nullptr);
    
    y += 50;
    
    // Event log label
    CreateWindowW(L"STATIC", L"Event Log:",
        WS_VISIBLE | WS_CHILD,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Event log listbox
    g_state.logList = CreateWindowW(L"LISTBOX", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
        10, y, 410, 180,
        hwnd, (HMENU)ID_LOG_LIST, nullptr, nullptr);
}

void UpdateStatus() {
    // Update SteamVR status
    if (g_state.vrInput) {
        if (g_state.vrInput->isVRAvailable()) {
            SetWindowTextW(g_state.statusLabel, L"● Connected");
        } else {
            SetWindowTextW(g_state.statusLabel, L"○ Not Connected (Stub Mode)");
        }
    }
    
    // Update dashboard status
    if (g_state.dashboardManager) {
        auto state = g_state.dashboardManager->getDashboardState();
        switch (state) {
            case steamvr::DashboardState::Open:
                SetWindowTextW(g_state.dashboardLabel, L"Open");
                break;
            case steamvr::DashboardState::Closed:
                SetWindowTextW(g_state.dashboardLabel, L"Closed");
                break;
            default:
                SetWindowTextW(g_state.dashboardLabel, L"Unknown");
                break;
        }
    }
}

void AddLogEntry(const std::wstring& message) {
    std::wstring entry = GetTimeString() + L" - " + message;
    
    // Add to listbox
    SendMessageW(g_state.logList, LB_ADDSTRING, 0, (LPARAM)entry.c_str());
    
    // Scroll to bottom
    int count = (int)SendMessage(g_state.logList, LB_GETCOUNT, 0, 0);
    SendMessage(g_state.logList, LB_SETTOPINDEX, count - 1, 0);
    
    // Limit entries
    while (count > 100) {
        SendMessage(g_state.logList, LB_DELETESTRING, 0, 0);
        count--;
    }
}

void OnSendClicked() {
    AddLogEntry(L"Sending HMD button event...");
    
    if (g_state.dashboardManager) {
        if (g_state.dashboardManager->toggleDashboard()) {
            AddLogEntry(L"HMD button event sent");
        } else {
            AddLogEntry(L"Failed to send HMD button event");
        }
    }
}

void OnClickClicked() {
    AddLogEntry(L"Sending dashboard click...");
    
    if (g_state.vrInput) {
        if (g_state.vrInput->sendDashboardClick()) {
            AddLogEntry(L"Dashboard click sent");
        } else {
            AddLogEntry(L"Failed to send dashboard click");
        }
    }
}

void OnReconnectClicked() {
    AddLogEntry(L"Reconnecting to SteamVR...");
    
    if (g_state.vrInput) {
        g_state.vrInput->shutdown();
        
        if (g_state.vrInput->initialize()) {
            AddLogEntry(L"Reconnected successfully");
        } else {
            AddLogEntry(L"Reconnection failed");
        }
    }
}

#else
// Non-Windows stub
#include <iostream>
int main() {
    std::cerr << "This application requires Windows.\n";
    return 1;
}
#endif
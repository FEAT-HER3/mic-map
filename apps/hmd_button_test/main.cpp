/**
 * @file main.cpp
 * @brief MicMap HMD Button Test Application - Win32 GUI
 *
 * Test Program 2: SteamVR HMD button event testing
 * - SteamVR connection status
 * - Dashboard state indicator
 * - Manual trigger buttons for dashboard actions
 * - Event log display
 *
 * This test app demonstrates the SteamVR integration:
 * - Dashboard closed: "Open Dashboard" button opens the SteamVR dashboard
 * - Dashboard open: "Select Item" button sends HMD button press to activate
 *   whatever is under the head-locked virtual pointer
 * - "Auto Action" button performs the appropriate action based on dashboard state
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
constexpr int WINDOW_WIDTH = 500;
constexpr int WINDOW_HEIGHT = 500;

// Control IDs
constexpr int ID_OPEN_DASHBOARD_BUTTON = 101;
constexpr int ID_SELECT_BUTTON = 102;
constexpr int ID_AUTO_ACTION_BUTTON = 103;
constexpr int ID_RECONNECT_BUTTON = 104;
constexpr int ID_LOG_LIST = 105;
constexpr int ID_TIMER = 106;

// Global state
struct AppState {
    std::shared_ptr<steamvr::IVRInput> vrInput;
    std::unique_ptr<steamvr::IDashboardManager> dashboardManager;
    
    steamvr::ConnectionState connectionState = steamvr::ConnectionState::Disconnected;
    steamvr::DashboardState dashboardState = steamvr::DashboardState::Unknown;
    
    HWND hwnd = nullptr;
    HWND statusLabel = nullptr;
    HWND connectionLabel = nullptr;
    HWND dashboardLabel = nullptr;
    HWND openDashboardButton = nullptr;
    HWND selectButton = nullptr;
    HWND autoActionButton = nullptr;
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
void OnOpenDashboardClicked();
void OnSelectClicked();
void OnAutoActionClicked();
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
    
    // Initialize VR input (using OpenVR for SteamVR integration)
    g_state.vrInput = std::shared_ptr<steamvr::IVRInput>(
        steamvr::createOpenVRInput().release()
    );
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
                    AddLogEntry(L"HMD button pressed");
                    break;
                case steamvr::VREventType::ButtonReleased:
                    AddLogEntry(L"HMD button released");
                    break;
                case steamvr::VREventType::SteamVRConnected:
                    AddLogEntry(L"SteamVR connected");
                    break;
                case steamvr::VREventType::SteamVRDisconnected:
                    AddLogEntry(L"SteamVR disconnected");
                    break;
                case steamvr::VREventType::Quit:
                    AddLogEntry(L"SteamVR quit event - application should exit");
                    PostQuitMessage(0);
                    break;
                default:
                    break;
            }
        });
    }
    
    // Initialize dashboard manager with shared VR input
    if (g_state.dashboardManager) {
        steamvr::DashboardManagerConfig config;
        config.autoReconnect = true;
        config.exitWithSteamVR = true;
        config.reconnectInterval = std::chrono::milliseconds(3000);
        
        // Set up dashboard callback
        g_state.dashboardManager->setDashboardCallback([](steamvr::DashboardState state) {
            switch (state) {
                case steamvr::DashboardState::Open:
                    AddLogEntry(L"Dashboard state: Open");
                    break;
                case steamvr::DashboardState::Closed:
                    AddLogEntry(L"Dashboard state: Closed");
                    break;
                default:
                    break;
            }
        });
        
        // Set up connection callback
        g_state.dashboardManager->setConnectionCallback([](steamvr::ConnectionState state) {
            switch (state) {
                case steamvr::ConnectionState::Connected:
                    AddLogEntry(L"Connection state: Connected");
                    break;
                case steamvr::ConnectionState::Disconnected:
                    AddLogEntry(L"Connection state: Disconnected");
                    break;
                case steamvr::ConnectionState::Connecting:
                    AddLogEntry(L"Connection state: Connecting...");
                    break;
                case steamvr::ConnectionState::Reconnecting:
                    AddLogEntry(L"Connection state: Reconnecting...");
                    break;
            }
        });
        
        // Set up quit callback
        g_state.dashboardManager->setQuitCallback([]() {
            AddLogEntry(L"SteamVR is closing - exiting application");
            PostQuitMessage(0);
        });
        
        AddLogEntry(L"Initializing dashboard manager...");
        if (g_state.dashboardManager->initialize(g_state.vrInput, config)) {
            if (g_state.dashboardManager->isConnected()) {
                std::wstring runtime(g_state.vrInput->getRuntimeName().begin(),
                                    g_state.vrInput->getRuntimeName().end());
                AddLogEntry(L"Connected to " + runtime);
            } else {
                AddLogEntry(L"VR not available (stub mode)");
            }
        } else {
            AddLogEntry(L"Failed to initialize dashboard manager");
        }
    }
    
    AddLogEntry(L"Ready - Use buttons to test dashboard interaction");
    
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
                case ID_OPEN_DASHBOARD_BUTTON:
                    OnOpenDashboardClicked();
                    break;
                case ID_SELECT_BUTTON:
                    OnSelectClicked();
                    break;
                case ID_AUTO_ACTION_BUTTON:
                    OnAutoActionClicked();
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
    
    // SteamVR status section
    CreateWindowW(L"STATIC", L"SteamVR Status:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 120, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.statusLabel = CreateWindowW(L"STATIC", L"Initializing...",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        130, y, 340, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Connection status
    CreateWindowW(L"STATIC", L"Connection:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 120, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.connectionLabel = CreateWindowW(L"STATIC", L"Disconnected",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        130, y, 340, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Dashboard status
    CreateWindowW(L"STATIC", L"Dashboard:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 120, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.dashboardLabel = CreateWindowW(L"STATIC", L"Unknown",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        130, y, 340, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 40;
    
    // Separator
    CreateWindowW(L"STATIC", L"Dashboard Actions:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 200, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Action buttons
    g_state.openDashboardButton = CreateWindowW(L"BUTTON", L"Open Dashboard",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 150, 35,
        hwnd, (HMENU)ID_OPEN_DASHBOARD_BUTTON, nullptr, nullptr);
    
    g_state.selectButton = CreateWindowW(L"BUTTON", L"Select Item (HMD Btn)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        170, y, 150, 35,
        hwnd, (HMENU)ID_SELECT_BUTTON, nullptr, nullptr);
    
    g_state.autoActionButton = CreateWindowW(L"BUTTON", L"Auto Action",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        330, y, 140, 35,
        hwnd, (HMENU)ID_AUTO_ACTION_BUTTON, nullptr, nullptr);
    
    y += 45;
    
    g_state.reconnectButton = CreateWindowW(L"BUTTON", L"Reconnect to SteamVR",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, y, 180, 35,
        hwnd, (HMENU)ID_RECONNECT_BUTTON, nullptr, nullptr);
    
    y += 50;
    
    // Event log label
    CreateWindowW(L"STATIC", L"Event Log:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Event log listbox
    g_state.logList = CreateWindowW(L"LISTBOX", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
        10, y, 460, 200,
        hwnd, (HMENU)ID_LOG_LIST, nullptr, nullptr);
}

void UpdateStatus() {
    // Update SteamVR status
    if (g_state.vrInput) {
        if (g_state.vrInput->isVRAvailable()) {
            SetWindowTextW(g_state.statusLabel, L"● SteamVR Running");
        } else {
            SetWindowTextW(g_state.statusLabel, L"○ SteamVR Not Running (Stub Mode)");
        }
    }
    
    // Update connection status
    if (g_state.dashboardManager) {
        auto connState = g_state.dashboardManager->getConnectionState();
        switch (connState) {
            case steamvr::ConnectionState::Connected:
                SetWindowTextW(g_state.connectionLabel, L"● Connected");
                break;
            case steamvr::ConnectionState::Connecting:
                SetWindowTextW(g_state.connectionLabel, L"◐ Connecting...");
                break;
            case steamvr::ConnectionState::Reconnecting:
                SetWindowTextW(g_state.connectionLabel, L"◐ Reconnecting...");
                break;
            case steamvr::ConnectionState::Disconnected:
            default:
                SetWindowTextW(g_state.connectionLabel, L"○ Disconnected");
                break;
        }
        
        // Update dashboard status
        auto dashState = g_state.dashboardManager->getDashboardState();
        switch (dashState) {
            case steamvr::DashboardState::Open:
                SetWindowTextW(g_state.dashboardLabel, L"Open (use Select Item to click)");
                break;
            case steamvr::DashboardState::Closed:
                SetWindowTextW(g_state.dashboardLabel, L"Closed (use Open Dashboard)");
                break;
            default:
                SetWindowTextW(g_state.dashboardLabel, L"Unknown");
                break;
        }
        
        // Check if we should exit
        if (g_state.dashboardManager->shouldExit()) {
            AddLogEntry(L"SteamVR closed - exiting");
            PostQuitMessage(0);
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

void OnOpenDashboardClicked() {
    AddLogEntry(L"Opening SteamVR dashboard...");
    
    if (g_state.dashboardManager) {
        if (g_state.dashboardManager->openDashboard()) {
            AddLogEntry(L"Dashboard open command sent");
        } else {
            AddLogEntry(L"Failed to open dashboard");
            if (g_state.vrInput) {
                std::wstring error(g_state.vrInput->getLastError().begin(),
                                  g_state.vrInput->getLastError().end());
                if (!error.empty()) {
                    AddLogEntry(L"Error: " + error);
                }
            }
        }
    }
}

void OnSelectClicked() {
    AddLogEntry(L"Sending HMD button press (select item under pointer)...");
    
    if (g_state.vrInput) {
        if (g_state.vrInput->sendDashboardSelect()) {
            AddLogEntry(L"HMD button press sent - item should be selected");
        } else {
            AddLogEntry(L"Failed to send HMD button press");
            std::wstring error(g_state.vrInput->getLastError().begin(),
                              g_state.vrInput->getLastError().end());
            if (!error.empty()) {
                AddLogEntry(L"Error: " + error);
            }
        }
    }
}

void OnAutoActionClicked() {
    AddLogEntry(L"Performing auto action based on dashboard state...");
    
    if (g_state.dashboardManager) {
        auto state = g_state.dashboardManager->getDashboardState();
        if (state == steamvr::DashboardState::Closed) {
            AddLogEntry(L"Dashboard is closed - opening...");
        } else if (state == steamvr::DashboardState::Open) {
            AddLogEntry(L"Dashboard is open - sending HMD button press...");
        }
        
        if (g_state.dashboardManager->performDashboardAction()) {
            AddLogEntry(L"Action performed successfully");
        } else {
            AddLogEntry(L"Failed to perform action");
        }
    }
}

void OnReconnectClicked() {
    AddLogEntry(L"Reconnecting to SteamVR...");
    
    if (g_state.dashboardManager) {
        g_state.dashboardManager->disconnect();
        
        if (g_state.dashboardManager->connect()) {
            AddLogEntry(L"Reconnected successfully");
        } else {
            AddLogEntry(L"Reconnection failed - SteamVR may not be running");
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
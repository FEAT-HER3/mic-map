/**
 * @file main.cpp
 * @brief MicMap HMD Button Test Application - Win32 GUI
 *
 * Test Program 2: SteamVR HMD button event testing
 * - SteamVR connection status with visual indicator
 * - Dashboard state indicator (Open/Closed)
 * - Manual trigger buttons for dashboard actions
 * - Event log display with timestamps
 * - Last result status bar
 *
 * This test app demonstrates the SteamVR integration:
 * - Dashboard closed: "Open Dashboard" button opens the SteamVR dashboard
 * - Dashboard open: "Send Click" button sends HMD button press to activate
 *   whatever is under the head-locked virtual pointer
 * - "Auto" button performs the appropriate action based on dashboard state
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
constexpr int WINDOW_HEIGHT = 520;

// Control IDs
constexpr int ID_OPEN_DASHBOARD_BUTTON = 101;
constexpr int ID_SEND_CLICK_BUTTON = 102;
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
    
    std::wstring lastResult = L"Ready";
    bool lastResultSuccess = true;
    
    HWND hwnd = nullptr;
    HWND steamvrStatusLabel = nullptr;
    HWND dashboardLabel = nullptr;
    HWND openDashboardButton = nullptr;
    HWND sendClickButton = nullptr;
    HWND autoActionButton = nullptr;
    HWND reconnectButton = nullptr;
    HWND logList = nullptr;
    HWND lastResultLabel = nullptr;
};

static AppState g_state;

#ifdef _WIN32

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void UpdateStatus();
void AddLogEntry(const std::wstring& message);
void SetLastResult(const std::wstring& result, bool success);
void OnOpenDashboardClicked();
void OnSendClickClicked();
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
    const wchar_t CLASS_NAME[] = L"MicMapHMDButtonTest";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    
    RegisterClassW(&wc);
    
    // Create window - centered on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowX = (screenWidth - WINDOW_WIDTH) / 2;
    int windowY = (screenHeight - WINDOW_HEIGHT) / 2;
    
    g_state.hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"MicMap - HMD Button Test",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        windowX, windowY,
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
                    AddLogEntry(L"Event: Dashboard opened");
                    break;
                case steamvr::VREventType::DashboardClosed:
                    AddLogEntry(L"Event: Dashboard closed");
                    break;
                case steamvr::VREventType::ButtonPressed:
                    AddLogEntry(L"Event: HMD button pressed");
                    break;
                case steamvr::VREventType::ButtonReleased:
                    AddLogEntry(L"Event: HMD button released");
                    break;
                case steamvr::VREventType::SteamVRConnected:
                    AddLogEntry(L"Event: Connected to SteamVR");
                    SetLastResult(L"Connected to SteamVR", true);
                    break;
                case steamvr::VREventType::SteamVRDisconnected:
                    AddLogEntry(L"Event: Disconnected from SteamVR");
                    SetLastResult(L"Disconnected from SteamVR", false);
                    break;
                case steamvr::VREventType::Quit:
                    AddLogEntry(L"Event: SteamVR quit - exiting");
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
            g_state.dashboardState = state;
            switch (state) {
                case steamvr::DashboardState::Open:
                    AddLogEntry(L"Dashboard state: OPEN");
                    break;
                case steamvr::DashboardState::Closed:
                    AddLogEntry(L"Dashboard state: CLOSED");
                    break;
                default:
                    break;
            }
        });
        
        // Set up connection callback
        g_state.dashboardManager->setConnectionCallback([](steamvr::ConnectionState state) {
            g_state.connectionState = state;
            switch (state) {
                case steamvr::ConnectionState::Connected:
                    AddLogEntry(L"Connected to SteamVR");
                    SetLastResult(L"Connected to SteamVR", true);
                    break;
                case steamvr::ConnectionState::Disconnected:
                    AddLogEntry(L"Disconnected from SteamVR");
                    break;
                case steamvr::ConnectionState::Connecting:
                    AddLogEntry(L"Connecting to SteamVR...");
                    break;
                case steamvr::ConnectionState::Reconnecting:
                    AddLogEntry(L"Reconnecting to SteamVR...");
                    break;
            }
        });
        
        // Set up quit callback
        g_state.dashboardManager->setQuitCallback([]() {
            AddLogEntry(L"SteamVR is closing - exiting application");
            SetLastResult(L"SteamVR closed", false);
            PostQuitMessage(0);
        });
        
        AddLogEntry(L"Initializing SteamVR connection...");
        if (g_state.dashboardManager->initialize(g_state.vrInput, config)) {
            if (g_state.dashboardManager->isConnected()) {
                g_state.connectionState = steamvr::ConnectionState::Connected;
                std::wstring runtime(g_state.vrInput->getRuntimeName().begin(),
                                    g_state.vrInput->getRuntimeName().end());
                AddLogEntry(L"Connected to " + runtime);
                SetLastResult(L"Connected to " + runtime, true);
            } else {
                AddLogEntry(L"SteamVR not running (stub mode)");
                SetLastResult(L"SteamVR not running - stub mode", false);
            }
        } else {
            AddLogEntry(L"Failed to initialize dashboard manager");
            SetLastResult(L"Initialization failed", false);
        }
    }
    
    AddLogEntry(L"Ready - Use buttons to test dashboard interaction");
    
    ShowWindow(g_state.hwnd, nCmdShow);
    UpdateWindow(g_state.hwnd);
    
    // Set up timer for status updates (100ms for responsive UI)
    SetTimer(g_state.hwnd, ID_TIMER, 100, nullptr);
    
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
                case ID_SEND_CLICK_BUTTON:
                    OnSendClickClicked();
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
    int y = 15;
    int leftMargin = 15;
    int contentWidth = WINDOW_WIDTH - 50;
    
    // ========== Status Section ==========
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    // SteamVR Status row
    CreateWindowW(L"STATIC", L"SteamVR Status:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 110, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.steamvrStatusLabel = CreateWindowW(L"STATIC", L"Initializing...",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin + 115, y, 280, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Dashboard Status row
    CreateWindowW(L"STATIC", L"Dashboard:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 110, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.dashboardLabel = CreateWindowW(L"STATIC", L"UNKNOWN",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin + 115, y, 280, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 30;
    
    // ========== Actions Section ==========
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    CreateWindowW(L"STATIC", L"Actions:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 25;
    
    // Action buttons - three in a row
    int buttonWidth = 125;
    int buttonHeight = 35;
    int buttonSpacing = 8;
    
    g_state.openDashboardButton = CreateWindowW(L"BUTTON", L"Open Dashboard",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)ID_OPEN_DASHBOARD_BUTTON, nullptr, nullptr);
    
    g_state.sendClickButton = CreateWindowW(L"BUTTON", L"Send Click",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin + buttonWidth + buttonSpacing, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)ID_SEND_CLICK_BUTTON, nullptr, nullptr);
    
    g_state.autoActionButton = CreateWindowW(L"BUTTON", L"Auto",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin + (buttonWidth + buttonSpacing) * 2, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)ID_AUTO_ACTION_BUTTON, nullptr, nullptr);
    
    y += buttonHeight + 10;
    
    // Reconnect button
    g_state.reconnectButton = CreateWindowW(L"BUTTON", L"Reconnect to SteamVR",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin, y, 180, 30,
        hwnd, (HMENU)ID_RECONNECT_BUTTON, nullptr, nullptr);
    
    y += 40;
    
    // ========== Event Log Section ==========
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 10;
    
    CreateWindowW(L"STATIC", L"Event Log:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 100, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 22;
    
    // Event log listbox
    int logHeight = 180;
    g_state.logList = CreateWindowW(L"LISTBOX", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
        leftMargin, y, contentWidth, logHeight,
        hwnd, (HMENU)ID_LOG_LIST, nullptr, nullptr);
    
    y += logHeight + 10;
    
    // ========== Last Result Section ==========
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);
    
    y += 8;
    
    CreateWindowW(L"STATIC", L"Last Result:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 85, 20,
        hwnd, nullptr, nullptr, nullptr);
    
    g_state.lastResultLabel = CreateWindowW(L"STATIC", L"Ready",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin + 90, y, 300, 20,
        hwnd, nullptr, nullptr, nullptr);
}

void UpdateStatus() {
    // Update SteamVR status
    if (g_state.dashboardManager) {
        auto connState = g_state.dashboardManager->getConnectionState();
        g_state.connectionState = connState;
        
        switch (connState) {
            case steamvr::ConnectionState::Connected:
                SetWindowTextW(g_state.steamvrStatusLabel, L"[●] Connected");
                break;
            case steamvr::ConnectionState::Connecting:
                SetWindowTextW(g_state.steamvrStatusLabel, L"[◐] Connecting...");
                break;
            case steamvr::ConnectionState::Reconnecting:
                SetWindowTextW(g_state.steamvrStatusLabel, L"[◐] Reconnecting...");
                break;
            case steamvr::ConnectionState::Disconnected:
            default:
                if (g_state.vrInput && g_state.vrInput->isVRAvailable()) {
                    SetWindowTextW(g_state.steamvrStatusLabel, L"[○] Available (not connected)");
                } else {
                    SetWindowTextW(g_state.steamvrStatusLabel, L"[○] Not Running");
                }
                break;
        }
        
        // Update dashboard status
        auto dashState = g_state.dashboardManager->getDashboardState();
        g_state.dashboardState = dashState;
        
        switch (dashState) {
            case steamvr::DashboardState::Open:
                SetWindowTextW(g_state.dashboardLabel, L"OPEN");
                break;
            case steamvr::DashboardState::Closed:
                SetWindowTextW(g_state.dashboardLabel, L"CLOSED");
                break;
            default:
                SetWindowTextW(g_state.dashboardLabel, L"UNKNOWN");
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
    
    // Limit entries to prevent memory issues
    while (count > 100) {
        SendMessage(g_state.logList, LB_DELETESTRING, 0, 0);
        count--;
    }
}

void SetLastResult(const std::wstring& result, bool success) {
    g_state.lastResult = result;
    g_state.lastResultSuccess = success;
    
    std::wstring prefix = success ? L"Success - " : L"Failed - ";
    SetWindowTextW(g_state.lastResultLabel, (prefix + result).c_str());
}

void OnOpenDashboardClicked() {
    AddLogEntry(L"Opening SteamVR dashboard...");
    
    if (g_state.dashboardManager) {
        if (g_state.dashboardManager->openDashboard()) {
            AddLogEntry(L"Dashboard open command sent");
            SetLastResult(L"Dashboard opened", true);
        } else {
            AddLogEntry(L"Failed to open dashboard");
            if (g_state.vrInput) {
                std::wstring error(g_state.vrInput->getLastError().begin(),
                                  g_state.vrInput->getLastError().end());
                if (!error.empty()) {
                    AddLogEntry(L"Error: " + error);
                    SetLastResult(error, false);
                } else {
                    SetLastResult(L"Could not open dashboard", false);
                }
            } else {
                SetLastResult(L"VR input not available", false);
            }
        }
    } else {
        SetLastResult(L"Dashboard manager not available", false);
    }
}

void OnSendClickClicked() {
    AddLogEntry(L"Sending HMD button press (click)...");
    
    if (g_state.vrInput) {
        if (g_state.vrInput->sendDashboardSelect()) {
            AddLogEntry(L"HMD button press sent - item should be selected");
            SetLastResult(L"Click sent", true);
        } else {
            AddLogEntry(L"Failed to send HMD button press");
            std::wstring error(g_state.vrInput->getLastError().begin(),
                              g_state.vrInput->getLastError().end());
            if (!error.empty()) {
                AddLogEntry(L"Error: " + error);
                SetLastResult(error, false);
            } else {
                SetLastResult(L"Could not send click", false);
            }
        }
    } else {
        SetLastResult(L"VR input not available", false);
    }
}

void OnAutoActionClicked() {
    AddLogEntry(L"Performing auto action based on dashboard state...");
    
    if (g_state.dashboardManager) {
        auto state = g_state.dashboardManager->getDashboardState();
        if (state == steamvr::DashboardState::Closed || state == steamvr::DashboardState::Unknown) {
            AddLogEntry(L"Dashboard is closed - opening...");
        } else if (state == steamvr::DashboardState::Open) {
            AddLogEntry(L"Dashboard is open - sending click...");
        }
        
        if (g_state.dashboardManager->performDashboardAction()) {
            if (state == steamvr::DashboardState::Closed || state == steamvr::DashboardState::Unknown) {
                SetLastResult(L"Dashboard opened", true);
            } else {
                SetLastResult(L"Click sent", true);
            }
            AddLogEntry(L"Action performed successfully");
        } else {
            AddLogEntry(L"Failed to perform action");
            SetLastResult(L"Action failed", false);
        }
    } else {
        SetLastResult(L"Dashboard manager not available", false);
    }
}

void OnReconnectClicked() {
    AddLogEntry(L"Reconnecting to SteamVR...");
    
    if (g_state.dashboardManager) {
        g_state.dashboardManager->disconnect();
        
        if (g_state.dashboardManager->connect()) {
            AddLogEntry(L"Reconnected successfully");
            SetLastResult(L"Reconnected to SteamVR", true);
        } else {
            AddLogEntry(L"Reconnection failed - SteamVR may not be running");
            SetLastResult(L"Reconnection failed", false);
        }
    } else {
        SetLastResult(L"Dashboard manager not available", false);
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
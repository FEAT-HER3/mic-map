/**
 * @file main.cpp
 * @brief MicMap HMD Button Test Application - Win32 GUI
 *
 * Test harness for the MicMap driver's POST /button API. Operator-facing
 * buttons:
 *   - "Tap"               -> driverClient->tap() (POST /button {"kind":"tap"})
 *   - "Test Driver"       -> probes /health + /status for the driver
 *   - "Reconnect Driver"  -> disconnect + reconnect the HTTP client
 *
 * No dashboard-manager / virtual-controller wiring: the driver owns the
 * HMD /input/system/click component directly (Plan 01-03) and expands a
 * single tap command into press+release internally.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")
#endif

#include "micmap/steamvr/vr_input.hpp"
#include "micmap/common/logger.hpp"

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <chrono>

using namespace micmap;

// Window dimensions
constexpr int WINDOW_WIDTH = 470;
constexpr int WINDOW_HEIGHT = 560;

// Control IDs
constexpr int ID_SEND_TAP_BUTTON = 203;
constexpr int ID_TEST_DRIVER_BUTTON = 204;
constexpr int ID_RECONNECT_DRIVER_BUTTON = 205;
constexpr int ID_LOG_LIST = 210;
constexpr int ID_TIMER = 211;

// Global state
struct AppState {
    std::shared_ptr<steamvr::IVRInput> vrInput;
    std::unique_ptr<steamvr::IDriverClient> driverClient;

    bool driverConnected = false;
    int driverPort = 0;

    std::wstring lastResult = L"Ready";
    bool lastResultSuccess = true;

    HWND hwnd = nullptr;
    HWND steamvrStatusLabel = nullptr;
    HWND driverStatusLabel = nullptr;
    HWND sendTapButton = nullptr;
    HWND testDriverButton = nullptr;
    HWND reconnectDriverButton = nullptr;
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
void OnSendTapClicked();
void OnTestDriverClicked();
void OnReconnectDriverClicked();
void UpdateDriverStatus();
void EnsureDriverClient();
std::wstring Utf8ToWide(const std::string& s);

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

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                     static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &w[0], needed);
    return w;
}

void EnsureDriverClient() {
    if (!g_state.driverClient) {
        g_state.driverClient = steamvr::createDriverClient("127.0.0.1", 27015, 27025);
    }
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
        L"MicMap - HMD Button Test (POST /button)",
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

    // Initialize VR input (OpenVR for SteamVR-quit monitoring only; button
    // presses flow exclusively through driverClient -> POST /button).
    g_state.vrInput = std::shared_ptr<steamvr::IVRInput>(
        steamvr::createOpenVRInput().release()
    );

    if (g_state.vrInput) {
        g_state.vrInput->setEventCallback([](const steamvr::VREvent& event) {
            if (event.type == steamvr::VREventType::Quit) {
                AddLogEntry(L"Event: SteamVR quit - exiting");
                PostQuitMessage(0);
            }
        });

        AddLogEntry(L"Initializing SteamVR connection (for Quit event)...");
        if (g_state.vrInput->initialize()) {
            std::wstring runtime = Utf8ToWide(g_state.vrInput->getRuntimeName());
            AddLogEntry(L"SteamVR connected: " + runtime);
            SetLastResult(L"Connected to " + runtime, true);
        } else {
            AddLogEntry(L"SteamVR not running (stub mode) - button presses still work via driver HTTP");
            SetLastResult(L"SteamVR not running - stub mode", false);
        }
    }

    // Driver client is lazily created on first use so startup does not
    // depend on the driver being live; Press/Release/Tap will create it
    // on demand.
    AddLogEntry(L"Click 'Test Driver' to check driver HTTP connection");
    AddLogEntry(L"Ready - Click 'Tap' to fire one POST /button {\"kind\":\"tap\"}");

    ShowWindow(g_state.hwnd, nCmdShow);
    UpdateWindow(g_state.hwnd);

    // Set up timer for status updates (250ms is plenty for human-scale UI)
    SetTimer(g_state.hwnd, ID_TIMER, 250, nullptr);

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    if (g_state.driverClient) {
        g_state.driverClient->disconnect();
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
                if (g_state.vrInput && g_state.vrInput->isInitialized()) {
                    g_state.vrInput->pollEvents();
                }
                UpdateStatus();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SEND_TAP_BUTTON:
                    OnSendTapClicked();
                    break;
                case ID_TEST_DRIVER_BUTTON:
                    OnTestDriverClicked();
                    break;
                case ID_RECONNECT_DRIVER_BUTTON:
                    OnReconnectDriverClicked();
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
        leftMargin, y, 120, 20,
        hwnd, nullptr, nullptr, nullptr);

    g_state.steamvrStatusLabel = CreateWindowW(L"STATIC", L"Initializing...",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin + 125, y, 280, 20,
        hwnd, nullptr, nullptr, nullptr);

    y += 25;

    // Driver Status row
    CreateWindowW(L"STATIC", L"Driver Status:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 120, 20,
        hwnd, nullptr, nullptr, nullptr);

    g_state.driverStatusLabel = CreateWindowW(L"STATIC", L"Not Connected",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin + 125, y, 280, 20,
        hwnd, nullptr, nullptr, nullptr);

    y += 30;

    // ========== Actions Section ==========
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);

    y += 10;

    CreateWindowW(L"STATIC", L"POST /button actions:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 200, 20,
        hwnd, nullptr, nullptr, nullptr);

    y += 25;

    // Single Tap button spans the action row.
    int btnHeight = 35;
    int btnWidth1 = 200;
    int btnSpacing = 10;

    g_state.sendTapButton = CreateWindowW(L"BUTTON", L"Tap",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin, y, btnWidth1 * 2 + btnSpacing, btnHeight,
        hwnd, (HMENU)ID_SEND_TAP_BUTTON, nullptr, nullptr);

    y += btnHeight + 14;

    // Driver utility section separator
    CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
        leftMargin, y, contentWidth, 2,
        hwnd, nullptr, nullptr, nullptr);

    y += 10;

    CreateWindowW(L"STATIC", L"Driver utilities:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        leftMargin, y, 150, 20,
        hwnd, nullptr, nullptr, nullptr);

    y += 25;

    // Row 3: Test Driver / Reconnect Driver
    int utilBtnWidth = 200;
    int utilBtnHeight = 30;
    g_state.testDriverButton = CreateWindowW(L"BUTTON", L"Test Driver (/health + /status)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin, y, utilBtnWidth, utilBtnHeight,
        hwnd, (HMENU)ID_TEST_DRIVER_BUTTON, nullptr, nullptr);

    g_state.reconnectDriverButton = CreateWindowW(L"BUTTON", L"Reconnect Driver",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        leftMargin + utilBtnWidth + btnSpacing, y, utilBtnWidth, utilBtnHeight,
        hwnd, (HMENU)ID_RECONNECT_DRIVER_BUTTON, nullptr, nullptr);

    y += utilBtnHeight + 15;

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
        leftMargin + 90, y, 320, 20,
        hwnd, nullptr, nullptr, nullptr);
}

void UpdateStatus() {
    // SteamVR status (IVRInput-based; dashboard-state polling was removed in Plan 01-04)
    if (g_state.vrInput) {
        if (g_state.vrInput->isInitialized()) {
            SetWindowTextW(g_state.steamvrStatusLabel, L"[●] Connected");
        } else if (g_state.vrInput->isVRAvailable()) {
            SetWindowTextW(g_state.steamvrStatusLabel, L"[○] Available (not connected)");
        } else {
            SetWindowTextW(g_state.steamvrStatusLabel, L"[○] Not Running");
        }
    }

    UpdateDriverStatus();
}

void UpdateDriverStatus() {
    if (g_state.driverClient) {
        if (g_state.driverClient->isConnected()) {
            g_state.driverConnected = true;
            g_state.driverPort = g_state.driverClient->getPort();
            std::wstring status = L"[●] Connected (port " +
                                  std::to_wstring(g_state.driverPort) + L")";
            SetWindowTextW(g_state.driverStatusLabel, status.c_str());
        } else {
            g_state.driverConnected = false;
            SetWindowTextW(g_state.driverStatusLabel, L"[○] Not Connected");
        }
    } else {
        SetWindowTextW(g_state.driverStatusLabel, L"[○] Not Initialized");
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

void OnSendTapClicked() {
    AddLogEntry(L"Sending POST /button {\"kind\":\"tap\"}...");

    EnsureDriverClient();
    if (!g_state.driverClient) {
        SetLastResult(L"Driver client not available", false);
        return;
    }

    if (!g_state.driverClient->isConnected()) {
        AddLogEntry(L"Driver not connected, attempting to connect...");
        if (!g_state.driverClient->connect()) {
            std::wstring err = Utf8ToWide(g_state.driverClient->getLastError());
            AddLogEntry(L"Connect failed: " + err);
            SetLastResult(L"Tap FAILED: " + err, false);
            return;
        }
        g_state.driverPort = g_state.driverClient->getPort();
        AddLogEntry(L"Connected to driver on port " +
                    std::to_wstring(g_state.driverPort));
    }

    if (g_state.driverClient->tap()) {
        AddLogEntry(L"tap() OK");
        SetLastResult(L"tap() OK (POST /button tap)", true);
    } else {
        std::wstring err = Utf8ToWide(g_state.driverClient->getLastError());
        AddLogEntry(L"tap() FAILED: " + err);
        SetLastResult(L"tap() FAILED: " + err, false);
    }

    UpdateDriverStatus();
}

void OnTestDriverClicked() {
    AddLogEntry(L"Testing driver connection...");

    EnsureDriverClient();
    if (!g_state.driverClient) {
        SetLastResult(L"Driver client not available", false);
        return;
    }

    // Disconnect first to force a fresh /health probe
    g_state.driverClient->disconnect();

    AddLogEntry(L"Attempting to connect to driver HTTP server...");
    if (g_state.driverClient->connect()) {
        g_state.driverConnected = true;
        g_state.driverPort = g_state.driverClient->getPort();
        AddLogEntry(L"Connected to driver on port " +
                    std::to_wstring(g_state.driverPort));

        AddLogEntry(L"Sending GET /status to driver...");
        if (g_state.driverClient->getStatus()) {
            AddLogEntry(L"Driver /status: OK");
            SetLastResult(L"Driver connected on port " +
                          std::to_wstring(g_state.driverPort), true);
        } else {
            std::wstring err = Utf8ToWide(g_state.driverClient->getLastError());
            AddLogEntry(L"Driver /status failed: " + err);
            SetLastResult(L"Driver /status failed", false);
        }
    } else {
        g_state.driverConnected = false;
        std::wstring err = Utf8ToWide(g_state.driverClient->getLastError());
        AddLogEntry(L"Failed to connect to driver: " + err);
        AddLogEntry(L"Make sure SteamVR is running with MicMap driver installed");
        SetLastResult(L"Driver not available", false);
    }

    UpdateDriverStatus();
}

void OnReconnectDriverClicked() {
    AddLogEntry(L"Reconnecting driver client...");

    EnsureDriverClient();
    if (!g_state.driverClient) {
        SetLastResult(L"Driver client not available", false);
        return;
    }

    g_state.driverClient->disconnect();
    if (g_state.driverClient->connect()) {
        g_state.driverPort = g_state.driverClient->getPort();
        AddLogEntry(L"Reconnected on port " +
                    std::to_wstring(g_state.driverPort));
        SetLastResult(L"Driver reconnected", true);
    } else {
        std::wstring err = Utf8ToWide(g_state.driverClient->getLastError());
        AddLogEntry(L"Reconnect failed: " + err);
        SetLastResult(L"Reconnect failed", false);
    }

    UpdateDriverStatus();
}

#else
// Non-Windows stub
#include <iostream>
int main() {
    std::cerr << "This application requires Windows.\n";
    return 1;
}
#endif

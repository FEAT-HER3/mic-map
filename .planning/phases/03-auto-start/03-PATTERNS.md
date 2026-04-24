# Phase 3: Auto-Start - Pattern Map

**Mapped:** 2026-04-23
**Files analyzed:** 8 (3 created, 5 modified)
**Analogs found:** 8 / 8

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `apps/micmap/app.vrmanifest.in` | config (template) | build-time transform | root `CMakeLists.txt:102` `configure_file(... COPYONLY)` loop (COPYONLY precedent only; `@ONLY` variable-sub is net-new to repo) | role-match (partial: no `@ONLY` precedent) |
| `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` (NEW) | interface header (service) | request-response (wraps OpenVR API) | `src/steamvr/include/micmap/steamvr/vr_input.hpp` (full `IVRInput` + factory pattern) | exact |
| `src/steamvr/src/manifest_registrar.cpp` (NEW) | service implementation | request-response + event-driven (retry loop) | `src/steamvr/src/vr_input.cpp` (`OpenVRInput` class, VR_Init/Shutdown wrapper, stub fallback, factory) | exact |
| `apps/micmap/main.cpp` (MOD) | app / controller | event-driven + request-response | itself (existing `WinMain`, `SetupSystemTray`, `NOTIFYICONDATAW`, single-instance mutex, tray icon, async init thread) | in-place extension |
| `apps/micmap/CMakeLists.txt` (MOD) | build config | build-time transform | itself + root `CMakeLists.txt:99-103` for `configure_file` precedent; own file for `target_link_libraries` + `add_executable(... WIN32)` | in-place extension |
| `src/steamvr/CMakeLists.txt` (MOD) | build config | build-time transform | itself (add source to `add_library(micmap_steamvr STATIC ...)` list) | in-place extension |
| `src/steamvr/src/vr_input.cpp` (MOD) | service implementation | event-driven | itself — `processVREvent()` VREvent_Quit branch at line 354-365 | in-place extension |
| `src/core/include/micmap/core/config_manager.hpp` + `src/core/src/config_manager.cpp` (MOD) | model + service | CRUD (JSON persistence) | itself — `SteamVRConfig` / `readBool` / `steamvrToJson` one-field additions are the direct template | in-place extension |

## Pattern Assignments

---

### `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` (NEW — interface header)

**Analog:** `src/steamvr/include/micmap/steamvr/vr_input.hpp` (entire file is the template)

**Header guard + file-doc pattern** (vr_input.hpp:1-16):
```cpp
#pragma once

/**
 * @file vr_input.hpp
 * @brief VR input handling for SteamVR integration
 *
 * This module provides: ...
 */

#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace micmap::steamvr {
```

**Factory-returns-unique_ptr pattern** (vr_input.hpp:124-141):
```cpp
/**
 * @brief Create an OpenVR-based VR input handler
 * @return Unique pointer to VR input interface
 *
 * This is the recommended implementation for SteamVR integration.
 * Uses OpenVR SDK as a VRApplication_Background for lifecycle monitoring ...
 */
std::unique_ptr<IVRInput> createOpenVRInput();

/**
 * @brief Create a stub VR input handler for testing
 */
std::unique_ptr<IVRInput> createStubVRInput();
```

**Virtual interface shape** (vr_input.hpp:62-122):
```cpp
class IVRInput {
public:
    virtual ~IVRInput() = default;

    virtual bool initialize() = 0;                        // returns bool for success/failure
    virtual void shutdown() = 0;                          // void for teardown
    virtual bool isInitialized() const = 0;               // is-prefix query
    virtual bool isVRAvailable() const = 0;
    virtual void pollEvents() = 0;
    virtual void setEventCallback(VREventCallback callback) = 0;
    virtual std::string getRuntimeName() const = 0;
    virtual std::string getLastError() const = 0;         // last-error accessor convention
};
```

**Namespace-close comment** (vr_input.hpp:216):
```cpp
} // namespace micmap::steamvr
```

**Applied-to-new-file guidance:** declare `enum class RegisterResult { Success, AddFailed, PollTimeout, AutoLaunchFailed };` inside namespace; `class IManifestRegistrar` with `registerApp() / unregisterApp() / ensureRegistered()` returning `RegisterResult`, plus `getLastError()`. Factory: `std::unique_ptr<IManifestRegistrar> createManifestRegistrar(std::string appKey, std::wstring manifestAbsPath);`. Keep stub-factory sibling (`createStubManifestRegistrar()`) if the `MICMAP_HAS_OPENVR` path warrants it (mirrors `createStubVRInput`).

---

### `src/steamvr/src/manifest_registrar.cpp` (NEW — service impl)

**Analog:** `src/steamvr/src/vr_input.cpp` (entire OpenVRInput class is the template)

**File-doc + includes + MICMAP_HAS_OPENVR gate** (vr_input.cpp:1-26):
```cpp
/**
 * @file vr_input.cpp
 * @brief VR input implementation using OpenVR SDK
 * ...
 */

#include "micmap/steamvr/vr_input.hpp"       // own header first
#include "micmap/common/logger.hpp"

#include <chrono>
#include <mutex>

#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#endif
...

namespace micmap::steamvr {
```

**VR_Init error handling + MICMAP_LOG_ERROR + return-false pattern** (vr_input.cpp:282-302):
```cpp
// Initialize OpenVR as a background application
vr::EVRInitError initError = vr::VRInitError_None;
vrSystem_ = vr::VR_Init(&initError, vr::VRApplication_Background);

if (initError != vr::VRInitError_None) {
    lastError_ = std::string("Failed to initialize OpenVR: ") +
                vr::VR_GetVRInitErrorAsEnglishDescription(initError);
    MICMAP_LOG_ERROR(lastError_);
    vrSystem_ = nullptr;
    return false;
}

initialized_ = true;
MICMAP_LOG_INFO("OpenVR initialized successfully");
```

**Applied-to-new-file guidance:** the registrar's CLI-mode call uses `vr::VRApplication_Utility` (D-02) instead of `VRApplication_Background` and, per canonical_refs, calls `vr::VRApplications()` accessor (never hardcodes `"IVRApplications_008"`).

**VR_Shutdown + reset-members idempotent teardown** (vr_input.cpp:304-318):
```cpp
void shutdown() override {
    if (!initialized_) {
        return;
    }
    MICMAP_LOG_INFO("Shutting down OpenVR input");
    vrSystem_ = nullptr;
    vr::VR_Shutdown();
    initialized_ = false;
    notifyEvent(VREventType::SteamVRDisconnected);
}
```

**Factory function conditional-on-MICMAP_HAS_OPENVR** (vr_input.cpp:389-404):
```cpp
// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IVRInput> createOpenVRInput() {
#ifdef MICMAP_HAS_OPENVR
    return std::make_unique<OpenVRInput>();
#else
    MICMAP_LOG_WARNING("OpenVR not available - using stub implementation");
    return std::make_unique<StubVRInput>();
#endif
}

std::unique_ptr<IVRInput> createStubVRInput() {
    return std::make_unique<StubVRInput>();
}
```

**Private-members convention** (vr_input.cpp:379-385):
```cpp
private:
    ...
    bool initialized_ = false;
    vr::IVRSystem* vrSystem_ = nullptr;
    std::string lastError_;
    VREventCallback eventCallback_;
    std::mutex callbackMutex_;
};
```
(Trailing-underscore member-variable convention, default-initialized in-class, `std::mutex` for callback guard — apply if registrar exposes callbacks; otherwise registrar is stateless except for `appKey_` / `manifestAbsPath_` / `lastError_`.)

---

### `src/steamvr/src/vr_input.cpp` (MODIFIED — AcknowledgeQuit_Exiting injection)

**Analog:** itself — the single-switch-case `processVREvent` method.

**Exact site** (vr_input.cpp:354-365, the `VREvent_Quit` branch):
```cpp
void processVREvent(const vr::VREvent_t& event) {
    switch (event.eventType) {
        case vr::VREvent_Quit:
            MICMAP_LOG_INFO("SteamVR quit event received");
            notifyEvent(VREventType::Quit);    // <-- D-11: inject AcknowledgeQuit_Exiting() BEFORE this line
            break;

        default:
            // Ignore other events ...
            break;
    }
}
```

**D-11 applied:** insert `if (vrSystem_) { vrSystem_->AcknowledgeQuit_Exiting(); }` between the existing `MICMAP_LOG_INFO(...)` and `notifyEvent(VREventType::Quit);` calls. Research Pattern 2 excerpt in 03-RESEARCH.md lines 272-286 matches this site precisely.

---

### `apps/micmap/main.cpp` (MODIFIED — multiple injection points)

**Analog:** itself — existing `WinMain`, `SetupSystemTray`, tray handling, async init thread, single-instance mutex.

**`NOTIFYICONDATAW` init pattern** (main.cpp:155-164):
```cpp
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
```

**D-09 applied (balloon fire):** after `SetupSystemTray`, if `configManager->getConfig().shownTrayNotification == false` AND argv contains `--minimized`, re-populate the existing `g_app.nid` with `NIF_INFO` + `szInfoTitle` / `szInfo` / `dwInfoFlags = NIIF_INFO` and call `Shell_NotifyIconW(NIM_MODIFY, &g_app.nid)`. Flip the flag and `configManager->saveDefault()`.

**Single-instance mutex + focus-steal site** (main.cpp:562-568):
```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"MicMapMain", nullptr);
        if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
        return 0;
    }
```

**D-08 applied:** wrap the `PostMessageW`/`SetForegroundWindow` pair in `if (!flags.minimized)` — where `flags` is the parsed-argv struct produced earlier in WinMain from `CommandLineToArgvW`.

**Current CLI parse (to be REPLACED per D-01)** (main.cpp:596-597):
```cpp
bool startMin = lpCmdLine && strstr(lpCmdLine, "--minimized");
if (startMin) { g_app.minimizedToTray = true; } else { ShowWindow(g_app.hwnd, nCmdShow); UpdateWindow(g_app.hwnd); }
```

**D-01 applied:** replace with `CommandLineToArgvW(GetCommandLineW(), &argc)` + `wcscmp` loop populating `struct Flags { bool register_manifest; bool unregister_manifest; bool minimized; }` at top of `WinMain`. The `startMin` local becomes `flags.minimized`. Free with `LocalFree(argv)`.

**D-02 applied (CLI fork):** immediately after argv parse, before `CreateMutexW`:
```cpp
if (flags.register_manifest || flags.unregister_manifest) {
    // VR_Init(Utility) → registrar call → VR_Shutdown() → return 0|1
    // No window, no D3D, no tray. common::Logger still writes to %APPDATA%\MicMap\micmap.log (D-04).
}
```

**Existing async-init-thread precedent** (main.cpp:585-594 — the pattern to EXTEND, NOT to copy into async-with-future form):
```cpp
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
```

**D-15 (amended) applied:** the re-registration thread follows THIS shape (`std::thread` + lambda), NOT the `std::async` shape below. Critically: do NOT detach; store the `std::thread` on `MicMapApp` and `join()` it in `shutdown()` after setting `std::atomic<bool> stopRegistrationThread_{false}` to true. Research SUMMARY Pitfall 6 explicitly rules out `std::async`.

**Existing std::async + future pattern in main.cpp (AVOID for D-15)** (main.cpp:615-639):
```cpp
static std::future<void> driverConnectFuture;
static std::future<void> vrInitFuture;
...
if (g_app.driverClient && !g_app.driverClient->isConnected()) {
    if (!driverConnectFuture.valid() ||
        driverConnectFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        driverConnectFuture = std::async(std::launch::async, []() {
            g_app.driverClient->connect();
        });
    }
}
```
**Do not copy this pattern for re-registration.** It works here because the futures live in `static` scope inside the main loop and wait-for drives the poll; for fire-and-forget on boot, the async future's destructor blocks at shutdown (RESEARCH Pitfall 6).

**MicMapApp struct member declarations pattern** (main.cpp:57-103):
```cpp
struct MicMapApp {
    std::unique_ptr<audio::IAudioCapture> audioCapture;
    std::unique_ptr<detection::INoiseDetector> detector;
    std::unique_ptr<steamvr::IVRInput> vrInput;
    ...
    std::atomic<bool> running{true};
    ...
    bool initialize();
    void shutdown();      // <-- already declared at line 100; currently has no definition
    void onTrigger();
    void renderUI();
};
```
**D-12 applied:** add members `std::unique_ptr<steamvr::IManifestRegistrar> manifestRegistrar;`, `std::thread regThread;`, `std::atomic<bool> stopRegistrationThread{false};`. Implement `MicMapApp::shutdown()` with the reverse-init ordering spelled out in D-12. The declaration at line 100 already exists; only the body is new.

---

### `src/core/include/micmap/core/config_manager.hpp` + `src/core/src/config_manager.cpp` (MODIFIED — one bool field)

**Analog:** itself — `SteamVRConfig` is the direct template for `shownTrayNotification`.

**Header struct field pattern** (config_manager.hpp:36-60, especially `SteamVRConfig`):
```cpp
struct SteamVRConfig {
    bool dashboardClickEnabled = true;  ///< Enable dashboard click when open
    std::string customActionBinding;    ///< Custom action binding (optional)
};
...
struct AppConfig {
    int version = 1;
    AudioConfig audio;
    DetectionConfig detection;
    SteamVRConfig steamvr;
    TrainingConfig training;
};
```
**D-09 applied:** add to `AppConfig` directly (not a sub-struct, since it is app-level UX state, not a subsystem): `bool shownTrayNotification = false;  ///< Set once after first silent-launch tray balloon fires`.

**Defensive reader helper precedent** (config_manager.cpp:98-103):
```cpp
bool readBool(const json& obj, const char* key, bool defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) return defaultVal;
    return it->get<bool>();
}
```

**SteamVR-section reader + writer as the closest sibling** (config_manager.cpp:186-190 + 226-233):
```cpp
void readSteamVR(const json& root, SteamVRConfig& out) {
    const json& s = readSubObject(root, "steamvr");
    out.dashboardClickEnabled = readBool(s, "dashboardClickEnabled", out.dashboardClickEnabled);
    out.customActionBinding   = readString(s, "customActionBinding", out.customActionBinding);
}
...
json steamvrToJson(const SteamVRConfig& c) {
    json j;
    j["dashboardClickEnabled"] = c.dashboardClickEnabled;
    j["customActionBinding"]   = ...;
    return j;
}
```

**Reader/writer wiring sites** (config_manager.cpp:403-406 reader dispatch, 247-255 writer dispatch):
```cpp
readAudio(j, config_.audio);
readDetection(j, config_.detection);
readSteamVR(j, config_.steamvr);
readTraining(j, config_.training);
```
```cpp
json appConfigToJson(const AppConfig& c) {
    json j;
    j["version"]   = c.version;
    j["audio"]     = audioToJson(c.audio);
    j["detection"] = detectionToJson(c.detection);
    j["steamvr"]   = steamvrToJson(c.steamvr);
    j["training"]  = trainingToJson(c.training);
    return j;
}
```

**D-09 applied:** since `shownTrayNotification` is a single top-level bool (not a grouped sub-struct), read it directly at top level: `config_.shownTrayNotification = readBool(j, "shownTrayNotification", false);` in the load path (after `readTraining`), and `j["shownTrayNotification"] = c.shownTrayNotification;` in `appConfigToJson`. The `readBool` helper already treats missing-key-as-default (.value-equivalent semantics preserved from Phase 2).

---

### `apps/micmap/CMakeLists.txt` (MODIFIED — configure_file + Pathcch)

**Analog:** root `CMakeLists.txt:99-103` (configure_file precedent) + own file (link / executable pattern).

**configure_file precedent (root CMakeLists.txt:99-103)**:
```cmake
# Copy scripts to build output directory
file(GLOB SCRIPT_FILES "${CMAKE_SOURCE_DIR}/scripts/*.bat")
foreach(SCRIPT_FILE ${SCRIPT_FILES})
    get_filename_component(SCRIPT_NAME ${SCRIPT_FILE} NAME)
    configure_file(${SCRIPT_FILE} ${CMAKE_BINARY_DIR}/bin/${SCRIPT_NAME} COPYONLY)
endforeach()
```
**Note:** this is `COPYONLY` — Phase 3 is the first `@ONLY` (variable substitution) use of `configure_file` in the repo. Pattern is net-new per RESEARCH §Pitfall 10 but follows the exact same function name.

**D-19 applied:** in `apps/micmap/CMakeLists.txt`, before `add_executable`:
```cmake
set(MICMAP_VERSION ${PROJECT_VERSION})
set(MICMAP_APP_KEY "bigscreen.micmap")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app.vrmanifest.in"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/app.vrmanifest"
    @ONLY
)
```
(Explicit `set(MICMAP_VERSION ${PROJECT_VERSION})` per Pitfall 10; use `@ONLY` not `COPYONLY`.)

**Current Windows-lib link pattern** (apps/micmap/CMakeLists.txt:25-27):
```cmake
if(WIN32)
    target_link_libraries(micmap PRIVATE shell32 dwmapi)
endif()
```
**D-21 applied:** append `Pathcch` to this list: `target_link_libraries(micmap PRIVATE shell32 dwmapi Pathcch)`.

**Current `WIN32` subsystem wiring** (apps/micmap/CMakeLists.txt:9-11):
```cmake
add_executable(micmap WIN32
    ${MICMAP_SOURCES}
)
```
**D-07 applied:** `WIN32` in `add_executable(... WIN32 ...)` is the CMake idiom that emits `/SUBSYSTEM:WINDOWS`. Already in place; phase exit-gate `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` should return zero.

---

### `src/steamvr/CMakeLists.txt` (MODIFIED — add registrar source)

**Analog:** itself — the existing `add_library(micmap_steamvr STATIC ...)` list.

**Current sources declaration** (src/steamvr/CMakeLists.txt:12-14):
```cmake
add_library(micmap_steamvr STATIC
    src/vr_input.cpp
)
```
**D-20 applied:** add `src/manifest_registrar.cpp` to this list:
```cmake
add_library(micmap_steamvr STATIC
    src/vr_input.cpp
    src/manifest_registrar.cpp
)
```
No change to `target_link_libraries` (OpenVR is already wired via `if(TARGET OpenVR::openvr_api)` at line 33-35); registrar uses the same `MICMAP_HAS_OPENVR` gate.

---

### `apps/micmap/app.vrmanifest.in` (NEW — JSON template for configure_file)

**Analog:** `driver/driver.vrdrivermanifest` is the nearest sibling concept (an OpenVR-runtime JSON config shipped next to a binary), and RESEARCH.md §Pattern 3 (lines 293-) gives the literal JSON shape.

**File is net-new:** no existing `*.vrmanifest.in` or other `configure_file` `@VAR@`-substituted file exists in the repo. Planner should copy the JSON shape from RESEARCH.md Pattern 3 directly, using `@MICMAP_VERSION@` and `@MICMAP_APP_KEY@` tokens. Honor the `@ONLY` contract — no `${VAR}` forms inside the JSON (avoids collision with nlohmann-agnostic JSON emitters down the line and with `$`-prefixed strings SteamVR might embed).

---

## Shared Patterns

### Logger macro usage

**Source:** `common::Logger` via `MICMAP_LOG_INFO / _WARNING / _ERROR / _DEBUG` (macros from `src/common/include/micmap/common/logger.hpp`)
**Apply to:** every new `.cpp` in this phase (registrar, main.cpp CLI fork, config_manager read/write for new field). Per D-04, headless CLI modes log to the same sink as the GUI (`%APPDATA%\MicMap\micmap.log`) — no `AllocConsole`.

Call-site precedent (variadic, concat via fold-expression; vr_input.cpp:267, 287-290, 309, 357):
```cpp
MICMAP_LOG_INFO("Initializing OpenVR input");
...
lastError_ = std::string("Failed to initialize OpenVR: ") +
            vr::VR_GetVRInitErrorAsEnglishDescription(initError);
MICMAP_LOG_ERROR(lastError_);
...
MICMAP_LOG_INFO("Shutting down OpenVR input");
...
MICMAP_LOG_INFO("SteamVR quit event received");
```

Variadic-concat precedent (config_manager.cpp:119, 136-142):
```cpp
MICMAP_LOG_WARNING("Config field has invalid UTF-8; using default: ", key);
...
MICMAP_LOG_WARNING("Config ", name, "=", value, " out of range; clamping to ", lo);
```

### Factory + interface + unique_ptr

**Source:** `createConfigManager()`, `createOpenVRInput()`, `createStubVRInput()`, `createDriverClient()` (all return `std::unique_ptr<IFoo>`, no raw `new` anywhere)
**Apply to:** `createManifestRegistrar(...)` MUST return `std::unique_ptr<IManifestRegistrar>`. Implementation class (`OpenVRManifestRegistrar`) private to the `.cpp`, inside an anonymous namespace or just defined in-file above the factory — both precedents exist in vr_input.cpp.

### Trailing-underscore member variables + default-init

**Source:** vr_input.cpp `OpenVRInput` private members (line 380-384)
**Apply to:** any impl-class members in `manifest_registrar.cpp` — `appKey_`, `manifestAbsPath_`, `lastError_`, `initialized_`, etc.

### Defensive `nlohmann/json` reader with `.value`-equivalent default

**Source:** `readBool` / `readInt` / `readString` helpers in `config_manager.cpp:84-131` (Phase 2 landed these)
**Apply to:** adding `shownTrayNotification` — use `readBool(j, "shownTrayNotification", false)` at top-level in the load path; `j["shownTrayNotification"] = c.shownTrayNotification;` in `appConfigToJson`. Missing-key or wrong-type silently returns default (`false`), matching forward-compat contract.

### `MICMAP_HAS_OPENVR` compile-gate + stub fallback

**Source:** `src/steamvr/src/vr_input.cpp:18-20, 240, 387-404, 393-400`
**Apply to:** `manifest_registrar.cpp` — wrap the real implementation class in `#ifdef MICMAP_HAS_OPENVR ... #endif` and provide a stub that always returns `RegisterResult::Success` (or a dedicated `RegistrarUnavailable` variant) so non-OpenVR builds compile.

### No try-catch / no exceptions

**Source:** CONVENTIONS.md + `allow_exceptions=false` in `config_manager.cpp:386` (`json::parse(content, nullptr, /*allow_exceptions=*/false)`)
**Apply to:** all new code — registrar returns enum-classified `RegisterResult`; OpenVR C API returns `EVRApplicationError` enums; CLI-fork exit codes are 0/1 via `return` values. No `throw`, no `catch`.

### Subsystem guardrail

**Source:** `add_executable(micmap WIN32 ...)` in `apps/micmap/CMakeLists.txt:9-11`
**Apply to:** unchanged; verify via phase-exit `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` returns zero (D-07).

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| *(none — all new files have strong analogs in `vr_input.hpp/cpp`, `config_manager.hpp/cpp`, existing `main.cpp`, and the root `CMakeLists.txt` `configure_file` precedent)* | | | |

**Partial-analog note for `app.vrmanifest.in`:** the filename pattern `*.in` + `configure_file(... @ONLY)` variable substitution is net-new to this codebase (root uses `COPYONLY` only). Planner should use RESEARCH.md §Pattern 3 JSON body verbatim and RESEARCH §Pitfall 10 for the `@ONLY` + explicit-`set(MICMAP_VERSION …)` mitigation.

## Metadata

**Analog search scope:**
- `src/steamvr/` (vr_input.hpp, vr_input.cpp, CMakeLists.txt)
- `src/core/` (config_manager.hpp, config_manager.cpp)
- `apps/micmap/` (main.cpp, CMakeLists.txt)
- Root `CMakeLists.txt` (for `configure_file` precedent)

**Files scanned:** 8

**Pattern extraction date:** 2026-04-23

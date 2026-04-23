# Phase 1: Driver Sidecar Migration - Pattern Map

**Mapped:** 2026-04-23
**Files analyzed:** 15 (7 new/modified driver-side, 4 modified steamvr/core, 2 modified apps, 2 new tests/CMake)
**Analogs found:** 13 / 15 (two new files — `command_queue` and `vr_error` — have no exact analog; use cited excerpts from existing mutex/deque + enum-to-string idioms)

## Source of Truth

- **In-tree analogs (HIGH priority):** All source paths rooted at `C:/Users/decid/Documents/projects/mic-map/`. Concrete excerpts verified by Read in this pass.
- **Out-of-tree reference:** `D:/Documents/Projects/bey-closer-t1/HMD Button Stub.md` is cited by CONTEXT.md and RESEARCH.md as authoritative prior art for the sidecar-on-HMD technique. That path is NOT visible to the current shell session (verified: `ls D:/Documents/Projects/bey-closer-t1/` returns "No such file or directory"). The planner/executor MUST Read this file directly at plan/exec time — the sidecar state-machine pattern quoted in RESEARCH.md §Pattern 1 (lines 207-247) is the authoritative transcription of it, and should be treated as the canonical shape. Do not re-derive; the HMD Button Stub research already did the validation work.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `driver/src/device_provider.{hpp,cpp}` (REWRITE) | driver / IServerTrackedDeviceProvider impl | event-driven + queue-drain | self (current `device_provider.cpp`) + `virtual_controller.cpp::RunFrame` deferred-release block | exact role, new data flow |
| `driver/src/http_server.{hpp,cpp}` (MODIFY) | driver / HTTP route handlers | request-response → queue-push | self (current `http_server.cpp::SetupRoutes`) | exact |
| `driver/src/command_queue.hpp` (NEW, header-only) | driver / producer-consumer primitive | thread-safe FIFO (bounded, drop-oldest) | `virtual_controller.cpp` `pendingReleases_` + `pendingReleasesMutex_` (lines 157-162, 256-261, 357-378) | partial (same concurrency idiom, different data shape) |
| `driver/src/vr_error.hpp` (NEW, header-only) | driver / enum-to-string utility | pure function | `src/core/include/micmap/core/state_machine.hpp::stateToString` (lines 37-46) | partial (same switch-on-enum shape, different enum) |
| `driver/src/driver_main.cpp` (UNCHANGED) | driver / factory entrypoint | — | — | n/a — confirmed untouched by RESEARCH line 167 |
| `driver/src/driver_log.hpp` (UNCHANGED; usage expanded) | driver / logging wrapper | — | self | n/a |
| `driver/CMakeLists.txt` (MODIFY) | build / CMake target definition | build | self (lines 22-28, 99-146) | exact |
| `driver/resources/settings/default.vrsettings` (MAYBE MODIFY) | config / SteamVR settings blob | config-load | self | exact |
| `src/steamvr/include/micmap/steamvr/vr_input.hpp` (MODIFY) | interface / `IDriverClient` | request-response | self (lines 201-261) | exact |
| `src/steamvr/src/vr_input.cpp` (MODIFY) | impl / `DriverClient` class | request-response | self (lines 150-349) | exact |
| `src/steamvr/CMakeLists.txt` (MODIFY) | build | build | self (line 14) | exact |
| `src/core/include/micmap/core/state_machine.hpp` (MODIFY) | interface / state-machine shape | event-driven | self (lines 14-137) | exact |
| `src/core/src/state_machine.cpp` (MODIFY) | impl / state-machine impl | event-driven | self (lines 14-163) | exact |
| `apps/micmap/main.cpp` (MODIFY) | app / wiring + `onTrigger` | event-driven | self (callsite inventory at lines 62, 218, 230, 241, 293, 334, 341-378, 390-393) | exact |
| `apps/hmd_button_test/main.cpp` (MODIFY) | app / Win32 GUI test harness | request-response (buttons → HTTP) | self (lines 60-61, 164-271, 605-838) | exact |
| `tests/test_command_queue.cpp` (NEW) | test | unit test | `tests/test_placeholder.cpp` | partial (framework-less `main()` test) |
| `tests/CMakeLists.txt` (MODIFY) | build / test | build | self (lines 26-29) | exact |

### Files to DELETE (not to create/modify — listed for completeness)

These are tracked here so the planner can cross-check the deletion task:

- `driver/src/virtual_controller.{hpp,cpp}` — the entire `VirtualController` class goes; its patterns are harvested into the new `device_provider.cpp` (see Pattern Assignments below).
- `driver/src/process_launcher.{hpp,cpp}` — Phase 3 replaces this with `app.vrmanifest`-based auto-launch; deleted here with no in-phase replacement.
- `driver/resources/input/micmap_controller_profile.json` and `vrcompositor_bindings_micmap_controller.json`.
- `src/steamvr/src/dashboard_manager.cpp` and `src/steamvr/include/micmap/steamvr/dashboard_manager.hpp` — entire `IDashboardManager` surface removed per D-14.
- `src/steamvr/virtual_controller.{hpp,cpp}` and `src/steamvr/process_launcher.{hpp,cpp}` — **do not exist** in the tree per RESEARCH line 701-703. CONTEXT.md D-14 lists them for historical reasons; planner should skip.

---

## Pattern Assignments

### `driver/src/device_provider.{hpp,cpp}` (REWRITE)

**Role:** `IServerTrackedDeviceProvider` implementation. Owns HMD-container state machine, OpenVR event pump, `CommandQueue` drain, min-hold deferred release.

**Primary analog:** self (current `device_provider.cpp` lines 33-73 for shape; lines 107-114 for RunFrame skeleton). Everything controller-related and process-launch-related is deleted; the file keeps only the `IServerTrackedDeviceProvider` interface methods and grows the new responsibilities.

**Secondary analog for deferred-release mechanics:** `driver/src/virtual_controller.cpp` lines 256-261 (push a pending release with absolute deadline) and lines 355-378 (drain and release on deadline). This is the mechanical template for min-hold in the new `RunFrame`.

**Tertiary analog (out-of-tree):** `D:/Documents/Projects/bey-closer-t1/HMD Button Stub.md` §2. The RESEARCH §Pattern 1 block (lines 207-247 of 01-RESEARCH.md) is the authoritative transcription — copy from there if the sister-project file is not readable.

**Imports pattern** (current `device_provider.cpp:6-13`, keep shape, drop `process_launcher.hpp` and `virtual_controller.hpp`):
```cpp
#include "device_provider.hpp"
#include "command_queue.hpp"      // NEW
#include "http_server.hpp"
#include "driver_log.hpp"
#include "vr_error.hpp"           // NEW

#include <openvr_driver.h>
#include <chrono>
#include <optional>

using namespace vr;
```

**Init pattern** (replaces current `device_provider.cpp:33-73`):
```cpp
// Current Init (to be REMOVED):
//   VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);   // KEEP
//   controller_ = std::make_unique<VirtualController>();   // DELETE
//   VRServerDriverHost()->TrackedDeviceAdded(...);   // DELETE — per SVR-01
//   httpServer_ = std::make_unique<HttpServer>(controller_.get());   // MODIFY: ctor takes CommandQueue&
//   launchMicMapApp();   // DELETE — per D-14
//
// New Init:
EVRInitError DeviceProvider::Init(IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
    DriverLog("MicMap driver initializing (sidecar mode)\n");

    commandQueue_ = std::make_unique<CommandQueue>();
    httpServer_ = std::make_unique<HttpServer>(*commandQueue_);
    if (!httpServer_->Start()) {
        DriverLog("Failed to start HTTP server\n");
        return VRInitError_Driver_Failed;
    }
    initialized_ = true;
    return VRInitError_None;
}
```

**RunFrame pattern** (see RESEARCH §Pattern 1 lines 217-247 for the event-pump + state-machine transcription; see `virtual_controller.cpp:355-378` for the deferred-release loop shape):

Skeleton (order is load-bearing per Pitfall 12 and Pitfall 1 acceptance criteria):
```cpp
void DeviceProvider::RunFrame() {
    // 0. First-frame init log (SVR-10 / Pitfall 11)
    if (!initLogged_) {
        DriverLog("MicMap driver v%s built %s %s - RunFrame starting\n",
                  MICMAP_VERSION, __DATE__, __TIME__);
        initLogged_ = true;
    }

    // 1. Drain OpenVR events (may flip state to Invalidated)
    vr::VREvent_t ev;
    while (vr::VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev))) {
        if (ev.eventType == vr::VREvent_TrackedDeviceDeactivated
            && ev.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) {
            hSystemClick_ = vr::k_ulInvalidInputComponentHandle;
            state_ = HmdComponentState::Invalidated;
            DriverLog("MicMap: HMD deactivated, handle invalidated\n");
        }
    }

    // 2. Try to (re)create the component while not Ready.
    if (state_ != HmdComponentState::Ready) {
        auto hmd = vr::VRProperties()->TrackedDeviceToPropertyContainer(
            vr::k_unTrackedDeviceIndex_Hmd);
        if (hmd != vr::k_ulInvalidPropertyContainer) {
            auto err = vr::VRDriverInput()->CreateBooleanComponent(
                hmd, "/input/system/click", &hSystemClick_);
            if (err == vr::VRInputError_None) {
                DriverLog("MicMap: /input/system/click created (handle=%llu)\n",
                          hSystemClick_);
                state_ = HmdComponentState::Ready;
            } else {
                DriverLog("MicMap: CreateBooleanComponent failed: %s (%d)\n",
                          VRInputErrorName(err), static_cast<int>(err));
                hSystemClick_ = vr::k_ulInvalidInputComponentHandle;
            }
        } else if (!loggedAwaitingHmd_) {
            DriverLog("MicMap: awaiting HMD container\n");
            loggedAwaitingHmd_ = true;          // transition-only per D-08
        }
    }

    // 3. Drain CommandQueue (non-blocking — see anti-patterns in RESEARCH §269).
    //    DOWN -> stamp pressTimestamp_, Update(true).
    //    UP   -> if (now - pressTimestamp_) >= kMinHold: Update(false); else defer.
    while (auto cmd = commandQueue_->try_pop()) {
        /* branch on cmd->kind; see virtual_controller.cpp:256-261, 357-378 for
           the steady_clock+deadline shape. Key difference: UP is the trigger
           for deferred release here, not a duration param. */
    }

    // 4. Tick any deferred UP; emit if deadline passed.
    //    (Same iteration pattern as virtual_controller.cpp:360-377.)
}
```

**Deferred-release mechanics — copy from `driver/src/virtual_controller.cpp:355-378`:**
```cpp
// Process pending button releases
auto now = std::chrono::steady_clock::now();

std::lock_guard<std::mutex> lock(pendingReleasesMutex_);

// Find and process expired releases
auto it = pendingReleases_.begin();
while (it != pendingReleases_.end()) {
    if (now >= it->releaseTime) {
        /* ... */
        UpdateButtonState(it->button, false);
        it = pendingReleases_.erase(it);
    } else {
        ++it;
    }
}
```
Adapt: the new code tracks exactly one pending release (the system-click UP) — a `std::optional<std::chrono::steady_clock::time_point> pendingReleaseAt_` is enough, no container needed.

**Error-handling pattern** — every OpenVR call routed through `VRInputErrorName()`:
```cpp
auto err = vr::VRDriverInput()->UpdateBooleanComponent(hSystemClick_, true, 0.0);
if (err != vr::VRInputError_None) {
    DriverLog("MicMap: UpdateBooleanComponent failed: %s (%d)\n",
              VRInputErrorName(err), static_cast<int>(err));
    // Flip to Invalidated so RunFrame re-creates next tick (Pitfall 1).
    hSystemClick_ = vr::k_ulInvalidInputComponentHandle;
    state_ = HmdComponentState::Invalidated;
}
```

**Header (`device_provider.hpp`) field changes** — **remove** `controller_`, `micmapProcess_`, `micmapLaunchedByUs_` (lines 101, 106, 107), **remove** `#include "process_launcher.hpp"` (line 16), **remove** forward decl `class VirtualController;` (line 21). **Add:** `std::unique_ptr<CommandQueue> commandQueue_;`, `vr::VRInputComponentHandle_t hSystemClick_{vr::k_ulInvalidInputComponentHandle};`, `HmdComponentState state_{HmdComponentState::NotReady};`, `std::optional<std::chrono::steady_clock::time_point> pendingReleaseAt_;`, `std::chrono::steady_clock::time_point pressTimestamp_{};`, `bool initLogged_{false};`, `bool loggedAwaitingHmd_{false};`.

---

### `driver/src/http_server.{hpp,cpp}` (MODIFY)

**Role:** HTTP server on driver side. Hosts `POST /button` that enqueues into `CommandQueue`.

**Primary analog:** self. Current `http_server.cpp:123-258` has three POST handler lambdas (`/click`, `/press`, `/release`) all of which need to be replaced by one `POST /button`. Keep port-range retry (lines 57-95) and thread setup (lines 260-290) exactly as-is — RESEARCH line 281 explicitly flags that as solved.

**Ctor signature change** (replaces `http_server.hpp:43`):
```cpp
// OLD: explicit HttpServer(VirtualController* controller, int port = 27015, ...);
// NEW: explicit HttpServer(CommandQueue& queue, int port = 27015,
//                          const std::string& host = "127.0.0.1");
```
Drop `controller_` member (line 80); add `CommandQueue& queue_` reference member. Rationale: Open Question 4 in RESEARCH recommends `CommandQueue&` injection so `HttpServer` does not reach into `DeviceProvider`.

**POST /button handler** — exact shape from RESEARCH §Code Example 2 lines 390-414 (preferred over inventing):
```cpp
server_->Post("/button", [this](const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        const auto& state = body.at("state").get<std::string>();
        PressCommand cmd;
        if (state == "down")      cmd = { PressCommand::Kind::Down };
        else if (state == "up")   cmd = { PressCommand::Kind::Up };
        else {
            res.status = 400;
            res.set_content("{\"error\":\"state must be \\\"down\\\" or \\\"up\\\"\"}",
                            "application/json");
            return;
        }
        queue_.push(cmd);               // never blocks; drop-oldest at depth 8
        res.set_content("{\"status\":\"ok\"}", "application/json");
    } catch (const nlohmann::json::exception&) {
        res.status = 400;
        res.set_content("{\"error\":\"malformed JSON body\"}", "application/json");
    }
});
```

**CRITICAL anti-pattern to kill from the current file:** the `if (!controller_ || !controller_->IsActive()) { res.status = 503; ... }` gate at `http_server.cpp:145-149, 188-192, 220-224` — all three copies must go. Per RESEARCH §Anti-Patterns line 270, the new HTTP handler does NOT gate on handle readiness; drops happen at the drain site with a log line per D-09.

**Routes to delete in full:** `POST /click` (142-182), `POST /press` (185-214), `POST /release` (217-246). Optionally keep `GET /health` (249-251) and `GET /port` (255-257); `GET /status` at 125-139 must drop the `controller_active` field (or be removed). Planner's call, but simpler: delete all but `POST /button` and `GET /health`.

**Imports added** (new in `http_server.cpp`):
```cpp
#include "command_queue.hpp"
#include <nlohmann/json.hpp>
```
Note `CPPHTTPLIB_NO_EXCEPTIONS` is set at `driver/CMakeLists.txt:50-52`; nlohmann::json throws by default and the try/catch stays inside the lambda, so exceptions never cross into cpp-httplib frames — see RESEARCH Open Question 3 (line 847-851).

---

### `driver/src/command_queue.hpp` (NEW, header-only)

**Role:** Thread-safe bounded deque with drop-oldest overflow, single-producer (HTTP thread) / single-consumer (RunFrame). Depth 8.

**Primary analog:** `driver/src/virtual_controller.cpp` — the `pendingReleases_` + `pendingReleasesMutex_` pair is the existing mutex/deque concurrency idiom in this codebase.

**Analog excerpt** (`virtual_controller.hpp:157-162`):
```cpp
struct PendingRelease {
    vr::VRInputComponentHandle_t button;
    std::chrono::steady_clock::time_point releaseTime;
};
std::vector<PendingRelease> pendingReleases_;
std::mutex pendingReleasesMutex_;
```
And push (`virtual_controller.cpp:256-261`):
```cpp
std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
pendingReleases_.push_back({
    systemButtonHandle_,
    std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs)
});
```

**Target implementation** — copy verbatim from RESEARCH §Code Example 3 lines 420-467 (already written, already reviewed in research):
```cpp
#pragma once
#include <deque>
#include <mutex>
#include <optional>

namespace micmap::driver {

struct PressCommand {
    enum class Kind { Down, Up };
    Kind kind;
};

class CommandQueue {
public:
    static constexpr size_t kMaxDepth = 8;

    // Producer (HTTP thread). Returns true if queue was full and oldest dropped.
    bool push(PressCommand cmd) {
        std::lock_guard<std::mutex> lk(m_);
        bool dropped = false;
        if (q_.size() >= kMaxDepth) { q_.pop_front(); dropped = true; }
        q_.push_back(cmd);
        return dropped;
    }

    // Consumer (RunFrame). Never blocks.
    std::optional<PressCommand> try_pop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        auto c = q_.front();
        q_.pop_front();
        return c;
    }

private:
    std::mutex m_;
    std::deque<PressCommand> q_;
};

} // namespace micmap::driver
```

**Anti-pattern** — do not try to "never drop an UP" clever logic (RESEARCH line 556). Accept drop-oldest; the min-hold + max-hold safety catches the stuck-button risk.

---

### `driver/src/vr_error.hpp` (NEW, header-only)

**Role:** `const char* VRInputErrorName(vr::EVRInputError)` — 21-case switch.

**Primary analog (shape):** `src/core/include/micmap/core/state_machine.hpp` lines 37-46:
```cpp
inline const char* stateToString(State state) {
    switch (state) {
        case State::Idle: return "Idle";
        case State::Training: return "Training";
        case State::Detecting: return "Detecting";
        case State::Triggered: return "Triggered";
        case State::Cooldown: return "Cooldown";
        default: return "Unknown";
    }
}
```

**Target implementation** — copy verbatim from RESEARCH §Code Example 4 lines 473-506. The 21 enum values are grounded in the vendored SDK header; RESEARCH confirms they match `_003`/`_004` both.

**Namespace / file placement:** Use `namespace micmap::driver {}` to match `driver_log.hpp:15`. Header-only — do not add a `.cpp`; do not add to `driver/CMakeLists.txt` source list.

**Anti-pattern** (RESEARCH line 276): no X-macro cleverness. A flat switch is faster to audit than a metaprogram.

---

### `src/steamvr/include/micmap/steamvr/vr_input.hpp` (MODIFY `IDriverClient`)

**Role:** Interface for app → driver HTTP client.

**Primary analog:** self, lines 201-261 (current `IDriverClient`). Collapse is in-place.

**API change — recommended option (split, matches soon-to-be-deleted `VirtualController::Press/ReleaseSystemButton`):**
```cpp
class IDriverClient {
public:
    virtual ~IDriverClient() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // REPLACES click(string, int) + press(string) + release(string).
    virtual bool press() = 0;     // POST /button {"state":"down"}
    virtual bool release() = 0;   // POST /button {"state":"up"}

    virtual bool getStatus() = 0;
    virtual int getPort() const = 0;
    virtual std::string getLastError() const = 0;
};
```
Rationale in RESEARCH §Open Questions line 840-841: split methods match the two state-machine edges, mirror the existing `PressSystemButton`/`ReleaseSystemButton` shape in `virtual_controller.hpp:81-86`, and grep cleanly.

**Also delete** the `DashboardState`, `HMDButtonAction`, `VREventType`, `VREvent`, `VREventCallback` symbols if they are only consumed by `IDashboardManager` — planner runs grep to confirm before removing. (They live in the same header.)

**Factory signature** (line 273-276) stays the same shape:
```cpp
std::unique_ptr<IDriverClient> createDriverClient(
    const std::string& host = "127.0.0.1",
    int startPort = 27015,
    int endPort = 27025);
```

---

### `src/steamvr/src/vr_input.cpp` (MODIFY `DriverClient` impl)

**Role:** HTTP-client impl of `IDriverClient`.

**Primary analog:** self, lines 150-349 (current `DriverClient` class).

**Connection + getStatus patterns UNCHANGED** — copy from current `vr_input.cpp:171-199` (`connect`) and `vr_input.cpp:307-325` (`getStatus`). The port-range probe on `/health` stays — it is orthogonal to the endpoint collapse.

**New press/release impl** — follow the existing `press()` at lines 245-274 but swap path/body:
```cpp
bool DriverClient::press() override {
    if (!ensureConnected()) return false;

    httplib::Client client(host_, port_);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);

    auto res = client.Post("/button", R"({"state":"down"})", "application/json");

    if (!res) {
        lastError_ = "HTTP request failed";
        MICMAP_LOG_ERROR("press() failed: {}", lastError_);
        connected_ = false;
        return false;
    }
    if (res->status != 200) {
        lastError_ = "Server returned status " + std::to_string(res->status);
        MICMAP_LOG_ERROR("press() failed: {}", lastError_);
        return false;
    }
    return true;
}
```
`release()` is the same with body `{"state":"up"}`.

**cpp-httplib `Post(path, body, content_type)` signature** is verified in current use elsewhere; the existing file uses `client.Post(path)` with query-string params (line 226, 257, 288) — switch to the three-arg form. No new include needed.

**Delete** the three existing methods `click`, `press(button)`, `release(button)` at 213-305.

---

### `src/core/include/micmap/core/state_machine.hpp` + `src/core/src/state_machine.cpp` (MODIFY)

**Role:** App-side detection state machine.

**Primary analog:** self.

**Config struct delta** (header lines 17-21) — see RESEARCH §State-Machine Changes lines 584-596:
```cpp
struct StateMachineConfig {
    std::chrono::milliseconds minDetectionDuration{100};  // was 500 per D-04
    std::chrono::milliseconds minReleaseDuration{80};     // NEW per D-10
    std::chrono::milliseconds cooldownDuration{200};      // post-release per D-11
    float detectionThreshold{0.7f};
};
```

**State enum delta** (header lines 26-32):
```cpp
enum class State {
    Idle, Training, Detecting, Triggered,
    Releasing,     // NEW — emits UP on transition to Cooldown
    Cooldown
};
```
And update `stateToString` at lines 37-46 to add the `Releasing` case.

**Callback delta** (header line 51) — recommended option B from RESEARCH line 600-603 (single callback + edge enum; fewer file edits):
```cpp
enum class PressEdge { Down, Up };
using TriggerCallback = std::function<void(PressEdge)>;
```

**Impl delta** — `state_machine.cpp`:
- `updateDetecting` (lines 123-140): on transition to `Triggered`, emit `triggerCallback_(PressEdge::Down)` (currently emits a no-arg `triggerCallback_()` at line 137).
- `updateTriggered` (lines 142-145): replace immediate cooldown transition with check `if (detectionConfidence < config_.detectionThreshold) transitionTo(State::Releasing);`.
- ADD `updateReleasing(float detectionConfidence)`: if confidence rises above threshold, back to `Triggered`; if `timeInState_ >= minReleaseDuration`, transition to `Cooldown` and emit `triggerCallback_(PressEdge::Up)`.
- `updateCooldown` (lines 147-151): unchanged semantic (cooldown-then-Idle), but semantic is now "post-release cooldown" per D-11.

**Existing transition logging pattern to keep** (lines 100-115):
```cpp
void transitionTo(State newState) {
    if (newState == currentState_) return;
    State oldState = currentState_;
    currentState_ = newState;
    timeInState_ = std::chrono::milliseconds(0);
    MICMAP_LOG_DEBUG("State transition: ", stateToString(oldState),
                    " -> ", stateToString(newState));
    if (stateChangeCallback_) stateChangeCallback_(oldState, newState);
}
```

---

### `apps/micmap/main.cpp` (MODIFY)

**Role:** App wiring + `onTrigger` callback.

**Primary analog:** self. Deletion lines: 62 (dashboardManager field), 230 (createDashboardManager), 293 (direct `onTrigger()` fallback call), 334 (dashboardManager shutdown), 341-378 (three-layer `onTrigger` body), 390-393 (connection-status UI reads). Keep lines 218 (`driverClient = steamvr::createDriverClient();`) and 241 (setTriggerCallback) but change the lambda.

**New onTrigger signature + body** — RESEARCH line 607 gives the 2-4 line target:
```cpp
void MicMapApp::onTrigger(core::PressEdge edge) {
    if (!driverClient || !driverClient->isConnected()) return;
    if (edge == core::PressEdge::Down) driverClient->press();
    else                                driverClient->release();
}
```

**setTriggerCallback wiring** (line 241):
```cpp
// OLD: stateMachine->setTriggerCallback([this]() { onTrigger(); });
// NEW:
stateMachine->setTriggerCallback([this](core::PressEdge e) { onTrigger(e); });
```

**Include removal:** drop `#include <micmap/steamvr/dashboard_manager.hpp>` (wherever it lives in the file's include block; planner greps).

---

### `apps/hmd_button_test/main.cpp` (MODIFY)

**Role:** Win32 GUI test harness, the D-02 reactivation spike tool.

**Primary analog:** self. Per D-13, use as-is with minimal edit.

**Minimal edit set** — per RESEARCH §Deletion Blast-Radius line 711:
- Remove `dashboardManager` field and its entire lifecycle (init at lines 165, 207-294; update at 311-312; shutdown at 293-294, 713).
- Remove `vrInput` paths that depend on `IDashboardManager` (lines 256-257, 269-271). The Win32 UI still wants a "is SteamVR connected" indicator — use `driverClient->isConnected()` instead of `dashboardManager->getConnectionState()`.
- Button "Send Click" (current line 636-672, calls `driverClient->click("trigger", 100)`) becomes two buttons: "Send Press" (`driverClient->press()`) and "Send Release" (`driverClient->release()`). Alternative acceptable per D-13: a single "Tap" button that calls `press()`, `Sleep(150)`, `release()` for quick operator testing.
- Button "Open Dashboard" (line 608-630) — delete entirely; no longer meaningful without dashboard manager.
- Button "Click A" (line 769-803) and "Click Trigger" (line 806+) — delete entirely or repurpose; they were virtual-controller-specific.

**Keep:** Win32 message pump, window creation, the HTTP-client-connect logic at lines 730-757 (it hits `/health` and reports port).

---

### `tests/test_command_queue.cpp` (NEW)

**Role:** Unit test covering `CommandQueue` push/pop/drop-oldest/depth-8 bound.

**Primary analog:** `tests/test_placeholder.cpp` (entire file below — this is the framework-less shape).

**Analog excerpt** (`tests/test_placeholder.cpp`):
```cpp
#include <iostream>

int main() {
    std::cout << "MicMap test placeholder - PASSED\n";
    return 0;
}
```

**Target shape** — a `main()` that returns non-zero on any assertion failure. Avoid `MICMAP_USE_GTEST` unless the planner decides to flip it ON globally (RESEARCH line 803).

```cpp
#include "command_queue.hpp"
#include <cassert>
#include <iostream>

using micmap::driver::CommandQueue;
using micmap::driver::PressCommand;

static int test_push_pop() {
    CommandQueue q;
    assert(!q.try_pop().has_value());
    q.push({PressCommand::Kind::Down});
    auto c = q.try_pop();
    assert(c.has_value() && c->kind == PressCommand::Kind::Down);
    assert(!q.try_pop().has_value());
    return 0;
}

static int test_drop_oldest() {
    CommandQueue q;
    for (int i = 0; i < 8; ++i) q.push({PressCommand::Kind::Down});
    bool dropped = q.push({PressCommand::Kind::Up});   // 9th
    assert(dropped);
    // drain 8 items — last one must be the Up we just pushed
    std::optional<PressCommand> last;
    while (auto c = q.try_pop()) last = c;
    assert(last && last->kind == PressCommand::Kind::Up);
    return 0;
}

int main() {
    test_push_pop();
    test_drop_oldest();
    std::cout << "test_command_queue PASSED\n";
    return 0;
}
```

---

### `tests/CMakeLists.txt` (MODIFY)

**Role:** Register the new test.

**Primary analog:** self, lines 26-29 (placeholder registration is the template).

**Target excerpt** — copy the placeholder stanza, point at the new file, link the driver's include dir (since `command_queue.hpp` is header-only under `driver/src/`):
```cmake
add_executable(test_command_queue test_command_queue.cpp)
target_compile_features(test_command_queue PRIVATE cxx_std_17)
target_include_directories(test_command_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
add_test(NAME test_command_queue COMMAND test_command_queue)
```
No OpenVR link needed: `command_queue.hpp` is pure stdlib.

---

### `driver/CMakeLists.txt` (MODIFY)

**Role:** Driver DLL build target.

**Primary analog:** self, lines 22-28 (source list) and lines 99-146 (post-build + install rules).

**Source list change** (lines 22-28):
```cmake
# OLD:
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/virtual_controller.cpp      # DELETE
    src/http_server.cpp
    src/process_launcher.cpp        # DELETE
)
# NEW:
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
)
# command_queue.hpp and vr_error.hpp are header-only — do NOT list.
```

**Post-build + install changes** (lines 114-119, 141-146):
- Delete the `copy_if_different` stanzas for `micmap_controller_profile.json` and `vrcompositor_bindings_micmap_controller.json`.
- Delete the two `install(FILES .../micmap_controller_profile.json ...)` blocks.
- `make_directory .../resources/input` (line 107) can stay (harmless) or go — planner's call.

**Add** (required per Pitfall 7 / SVR-07):
```cmake
# Treat warnings as errors on the driver target during the rewrite
if(MSVC)
    target_compile_options(driver_micmap PRIVATE /WX)
else()
    target_compile_options(driver_micmap PRIVATE -Werror -Wunused-function -Wunused-variable)
endif()
```

**Link** nlohmann/json (new dep for the driver; already vendored):
```cmake
target_link_libraries(driver_micmap PRIVATE nlohmann_json::nlohmann_json)
```

---

### `driver/resources/settings/default.vrsettings` (MAYBE MODIFY)

**Role:** Driver default settings blob consumed by `VRSettings()`.

**Primary analog:** self.

**Expected edits** (per RESEARCH Open Question 2 lines 843-846): remove the `autoLaunchApp`, `appPath`, `appArgs` keys under the `driver_micmap` section if they exist — they become dead code once `process_launcher` is deleted. SteamVR tolerates extra keys, so this is cosmetic; planner's call whether to land in this phase or defer.

---

## Shared Patterns

### Logging separation (driver vs app)

**Rule:** Driver uses `DriverLog(...)` (a printf-style macro via `driver/src/driver_log.hpp:44`). App uses `MICMAP_LOG_*` (stream-style macros from `src/common/include/micmap/common/logger.hpp`). NEVER include the app logger from driver code.

**Source excerpt** (`driver/src/driver_log.hpp:24-44`):
```cpp
inline void SafeDriverLog(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (vr::VRDriverLog()) {
        vr::VRDriverLog()->Log(buffer);
    } else {
        fprintf(stderr, "[MicMap Driver] %s", buffer);
    }
}
// ...
#define DriverLog(...) micmap::driver::SafeDriverLog(__VA_ARGS__)
```

**Apply to:** every new `.cpp` in `driver/src/`.

**Apply on app side** — use the `MICMAP_LOG_*` style already in `vr_input.cpp` (e.g. line 163-164: `MICMAP_LOG_DEBUG("DriverClient created (host: {}, ports: {}-{})", host_, startPort_, endPort_);`).

### Factory + interface (app side)

**Rule:** Interfaces live in `include/micmap/<layer>/<name>.hpp`; impls in `src/<name>.cpp`; a `createX(...)` free function is the only exported constructor (no direct impl-class use).

**Source excerpt** (`src/steamvr/include/micmap/steamvr/vr_input.hpp:184, 273-276`):
```cpp
std::unique_ptr<IVRInput> createOpenVRInput();
// ...
std::unique_ptr<IDriverClient> createDriverClient(
    const std::string& host = "127.0.0.1",
    int startPort = 27015,
    int endPort = 27025);
```

**Apply to:** `IDriverClient` (keep factory as-is). `IDashboardManager` factory is deleted per D-14.

**Do NOT apply:** `CommandQueue` is driver-internal, no interface, no factory — direct construction is correct.

### Ownership (`unique_ptr`, no raw `new`)

**Rule:** All heap-allocated lifetimes are `std::unique_ptr<T>` with construction via `std::make_unique`. No `new` in source.

**Source excerpts:**
- `driver/src/device_provider.hpp:101-102`: `std::unique_ptr<VirtualController> controller_; std::unique_ptr<HttpServer> httpServer_;`
- `driver/src/device_provider.cpp:41`: `controller_ = std::make_unique<VirtualController>();`
- `driver/src/device_provider.cpp:56`: `httpServer_ = std::make_unique<HttpServer>(controller_.get());`

**Apply to:** `commandQueue_` field on `DeviceProvider` uses same pattern. `HttpServer` takes the queue by reference (not `unique_ptr`), so its argument shape mirrors the existing raw-pointer-to-controller shape.

### Mutex-guarded state (driver)

**Rule:** Any state touched by both HTTP thread and RunFrame thread is guarded by a `std::mutex` + `std::lock_guard`. No atomics for structured state.

**Source excerpt** (`driver/src/virtual_controller.cpp:256-261`):
```cpp
std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
pendingReleases_.push_back({
    systemButtonHandle_,
    std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs)
});
```

**Apply to:** `CommandQueue` internals. RunFrame-only state (pressTimestamp_, pendingReleaseAt_, state_ enum, hSystemClick_) is NOT shared and does NOT need a mutex — see RESEARCH §Runtime State Inventory and Pitfall 12 acceptance criteria (line 357).

### Per-error logging helper (SVR-10)

**Rule:** Every OpenVR error is logged with its enum name via the new `VRInputErrorName(err)` helper (see `driver/src/vr_error.hpp` above).

**Apply to:** every `vr::VRDriverInput()` and `vr::VRProperties()` call in `driver/src/device_provider.cpp` that can fail. Grep-check in validation (RESEARCH line 789).

### HTTP JSON parse safety

**Rule:** Parse inside a `try { ... } catch (const nlohmann::json::exception&) { 400; }`. Never let JSON exceptions escape into cpp-httplib (`CPPHTTPLIB_NO_EXCEPTIONS` is set — see `driver/CMakeLists.txt:50-52`).

**Apply to:** `POST /button` handler in `http_server.cpp`. This phase has exactly one endpoint that parses JSON; Phase 2 will add more.

---

## No Analog Found

Two files in the new set have no exact in-tree analog (only partial / pattern-shape analogs, cited above):

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `driver/src/command_queue.hpp` | producer-consumer primitive | bounded FIFO | No existing standalone queue class; the `pendingReleases_` idiom is embedded inside `VirtualController`. Shape analog exists; standalone-class analog does not. Copy the implementation verbatim from RESEARCH §Code Example 3. |
| `driver/src/vr_error.hpp` | enum stringifier | pure function | No existing `EVRInputError`-to-string helper anywhere in the tree (grep-checked: neither driver nor app has one). `stateToString` in `state_machine.hpp:37-46` is the shape analog. Copy implementation verbatim from RESEARCH §Code Example 4. |

**Out-of-tree analog (reference only, not readable from current shell):**
- `D:/Documents/Projects/bey-closer-t1/HMD Button Stub.md` §2 — the sidecar-on-HMD lifecycle. The planner should Read this directly if the path resolves at plan/exec time. If not, treat RESEARCH §Pattern 1 (lines 207-247) + §Example 1 (lines 363-382) + §API Reference (lines 514-521) as the authoritative transcription; those blocks were already grounded against the vendored SDK headers in the research pass.

---

## Metadata

**Analog search scope:**
- `driver/src/` (all 5 `.{cpp,hpp}` files Read in this pass)
- `src/steamvr/` (vr_input.hpp Read; vr_input.cpp targeted grep + excerpt Read)
- `src/core/` (state_machine.hpp + state_machine.cpp Read)
- `apps/micmap/main.cpp` and `apps/hmd_button_test/main.cpp` (targeted grep for callsites)
- `tests/` (test_placeholder.cpp + CMakeLists.txt Read)
- `driver/CMakeLists.txt` Read

**Files scanned:** 14 in-tree source files + 2 build files.

**Strong matches found:** 13 of 15 new/modified targets have exact or role-match in-tree analogs. The 2 with no analog (CommandQueue, vr_error) have complete code provided in RESEARCH.md — no ambiguity remains for planning.

**Pattern extraction date:** 2026-04-23.

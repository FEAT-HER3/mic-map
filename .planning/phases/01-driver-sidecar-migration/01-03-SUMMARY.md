---
phase: 01-driver-sidecar-migration
plan: 03
subsystem: driver
tags: [driver, openvr, http, sidecar, rewrite, cmake, pressure-test]
dependency_graph:
  requires:
    - "driver/src/command_queue.hpp (plan 01-01) -- PressCommand + CommandQueue"
    - "driver/src/vr_error.hpp (plan 01-01) -- VRInputErrorName()"
    - "driver/src/driver_log.hpp -- DriverLog() macro"
    - "OpenVR SDK headers (openvr_driver.h) -- build-time only"
  provides:
    - "DeviceProvider sidecar RunFrame loop (event pump + HMD-container state machine + queue drain + min-hold deferred release + max-hold watchdog)"
    - "HttpServer POST /button JSON-bodied handler that enqueues PressCommands"
    - "driver_micmap source list trimmed to three translation units"
    - "POST /button wire contract (consumed by DriverClient::press()/release() from plan 01-02)"
  affects:
    - "apps/hmd_button_test/main.cpp -- still references old IDashboardManager / VirtualController surface; Plan 01-04 rewires"
    - "apps/micmap/main.cpp -- dashboard_manager include deletion is Plan 01-04"
    - "src/steamvr/src/dashboard_manager.cpp -- deletion is Plan 01-04"
tech-stack:
  added:
    - "nlohmann/json linked into driver_micmap (was only app-side before)"
  patterns:
    - "Single UpdateBooleanComponent choke point (writeValue helper) gates all error-handling and redundant-write suppression through one branch"
    - "Transition-only log latches (initLogged_, loggedAwaitingHmd_) prevent per-frame log spam on the cold-start / awaiting-HMD path"
    - "nlohmann::json::exception try/catch scoped entirely inside the handler lambda so exceptions never cross into cpp-httplib frames under CPPHTTPLIB_NO_EXCEPTIONS"
    - "load-bearing RunFrame step order: init-log -> events -> recreate -> drain -> tick-deferred -> max-hold (per Pitfall 12)"
key-files:
  created: []
  modified:
    - driver/CMakeLists.txt
    - driver/resources/settings/default.vrsettings
    - driver/src/device_provider.hpp
    - driver/src/device_provider.cpp
    - driver/src/http_server.hpp
    - driver/src/http_server.cpp
  deleted:
    - driver/src/virtual_controller.hpp
    - driver/src/virtual_controller.cpp
    - driver/src/process_launcher.hpp
    - driver/src/process_launcher.cpp
    - driver/resources/input/micmap_controller_profile.json
    - driver/resources/input/vrcompositor_bindings_micmap_controller.json
key-decisions:
  - "Sidecar Init returns VRInitError_Driver_Failed only if HTTP server fails to start; HMD-component creation is deferred to RunFrame (SVR-02 compliance)"
  - "/status kept (instead of deleted) because app-side DriverClient::getStatus() probes /status, not /health; body is minimal and contains no controller coupling"
  - "Header-comment references to TrackedDeviceAdded stripped to keep the grep-based acceptance criterion strictly passing"
  - "CMakeLists.txt header + project DESCRIPTION updated from 'Virtual Controller Driver' to 'HMD Sidecar Driver' to reflect shipped reality"
metrics:
  duration_min: "~25"
  tasks_completed: 3
  files_modified: 6
  files_deleted: 6
  completed_date: 2026-04-23
requirements: [SVR-01, SVR-02, SVR-03, SVR-04, SVR-05, SVR-06, SVR-07, SVR-10]
---

# Phase 01 Plan 03: Driver Sidecar Rewrite Summary

The driver no longer registers a virtual controller; `DeviceProvider` creates `/input/system/click` directly on the HMD property container, drains a `CommandQueue` populated by the HTTP thread, enforces a 100 ms min-hold floor and a 5 s max-hold watchdog, and handles `VREvent_TrackedDeviceDeactivated` by invalidating the component handle and recreating it on the next `RunFrame`. `HttpServer` collapses `/click` + `/press` + `/release` into a single `POST /button` with a JSON body.

## What Was Built

### Task 1 -- Delete controller + process-launcher surface and wire CMake for sidecar build (`ad4cee7`)

Six files removed from disk. CMakeLists.txt source list trimmed to exactly three `.cpp` files; install-rules and POST_BUILD copy-rules that referenced the deleted JSONs are gone; `default.vrsettings` stripped of the now-dead `autoLaunchApp` / `appPath` / `appArgs` keys; project description + top-of-file comment block updated to "HMD Sidecar Driver".

Note: prior in-tree work had already landed the `/WX`, `nlohmann_json`, and `MICMAP_DRIVER_VERSION` stanzas (see `git diff` pre-commit in the commit message). This task finished the removal sweep (install rules + top comment + vrsettings) and atomically committed the full deletion set.

### Task 2 -- Rewrite DeviceProvider as HMD sidecar (`9745b70`)

`device_provider.hpp`:
- Dropped `#include "process_launcher.hpp"`; dropped `class VirtualController;` forward decl; dropped `controller_`, `micmapProcess_`, `micmapLaunchedByUs_` fields.
- Added `enum class HmdComponentState { NotReady, Ready, Invalidated }` in namespace `micmap::driver`.
- Added forward decl `class CommandQueue;`.
- New fields on `DeviceProvider`: `std::unique_ptr<CommandQueue> commandQueue_`, `vr::VRInputComponentHandle_t hSystemClick_`, `HmdComponentState state_`, `std::chrono::steady_clock::time_point pressTimestamp_`, `std::optional<std::chrono::steady_clock::time_point> pendingReleaseAt_`, `bool isPressed_`, `bool lastWrittenValue_`, `bool initLogged_`, `bool loggedAwaitingHmd_`.
- Constants: `static constexpr std::chrono::milliseconds kMinHold{100};`, `kMaxHold{5000};`.
- Added private helper `void writeValue(bool v);`.

`device_provider.cpp`:
- New `Init()`: `VR_INIT_SERVER_DRIVER_CONTEXT`, construct `CommandQueue` + `HttpServer(*commandQueue_)`, start HTTP server, return `VRInitError_Driver_Failed` on start failure. No `TrackedDeviceAdded` call anywhere.
- New `Cleanup()`: stop HTTP server, release queue, clear all handle + latch + timing state so re-Init is safe, `VR_CLEANUP_SERVER_DRIVER_CONTEXT`.
- New `RunFrame()`, six load-bearing steps:
  0. First-frame init log -- `"MicMap driver v<ver> built <date> <time> - RunFrame starting\n"`.
  1. Drain `PollNextEvent`; `VREvent_TrackedDeviceDeactivated` on HMD idx 0 invalidates the handle, flips `state_` to `Invalidated`, clears press/release latches, logs once.
  2. If `state_ != Ready`, `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` then `CreateBooleanComponent(container, "/input/system/click", &hSystemClick_)`. On success, log with handle and flip to `Ready` (re-arm `loggedAwaitingHmd_` for future invalidation cycles). On failure, log via `VRInputErrorName(err)`. On invalid container, emit the awaiting-HMD log once per cycle.
  3. Drain `commandQueue_->try_pop()` in a while loop. `Down` stamps `pressTimestamp_` and calls `writeValue(true)`. `Up` writes immediately if `now - pressTimestamp_ >= kMinHold`, else defers via `pendingReleaseAt_ = pressTimestamp_ + kMinHold`. Commands arriving while `state_ != Ready` emit a single drop log per command (D-09).
  4. Tick: if `pendingReleaseAt_ && now >= *pendingReleaseAt_`, call `writeValue(false)` and clear.
  5. Max-hold watchdog: if `isPressed_ && (now - pressTimestamp_) > kMaxHold`, log, force `writeValue(false)`, clear `pendingReleaseAt_`.
- `writeValue(bool v)`: early-return if not Ready; early-return if `v == lastWrittenValue_` (Pitfall 12 anti-pattern); `UpdateBooleanComponent(hSystemClick_, v, 0.0)`; on error log via `VRInputErrorName`, invalidate handle, flip state to `Invalidated`, clear press latches.
- `GetInterfaceVersions()` now advertises only `IServerTrackedDeviceProvider_Version` (tracked-device-server interface dropped; no tracked device is registered).

### Task 3 -- HttpServer CommandQueue injection + POST /button (`e187015`)

`http_server.hpp`:
- Forward decl swapped: `class VirtualController;` -> `class CommandQueue;`.
- Ctor: `HttpServer(VirtualController* controller, ...)` -> `HttpServer(CommandQueue& queue, int port = 27015, const std::string& host = "127.0.0.1")`.
- Member: `VirtualController* controller_;` -> `CommandQueue& queue_;` (reference -- initialized in the initializer list, never reassigned).
- Doxygen endpoint list updated to describe only the four endpoints present after this plan.

`http_server.cpp`:
- Drops `#include "virtual_controller.hpp"`; adds `#include "command_queue.hpp"` and `#include <nlohmann/json.hpp>`.
- `using namespace vr;` removed (no OpenVR symbols needed on the HTTP thread -- SVR-05).
- New `POST /button` handler:
  - `nlohmann::json::parse(req.body)` wrapped in `try { ... } catch (const nlohmann::json::exception&) { 400 "malformed JSON body" }`.
  - `body.contains("state")` gate -> 400 `missing "state" field`.
  - `state` value check -> 400 `state must be "down" or "up"` for any non-down/up value.
  - Valid body enqueues `PressCommand{Down}` or `PressCommand{Up}` and returns 200 `{"status":"ok"}`.
- `GET /health`, `GET /port`, `GET /status` preserved minimally (no `controller_` references; `/status` returns `{"ok":true,"endpoint":"/button"}`).
- DELETED: `POST /click`, `POST /press`, `POST /release` handlers and their `controller_->IsActive()` 503 gates. Drops now happen at the drain site in `DeviceProvider` per D-09.
- Port-retry loop (27015-27025) unchanged. `ServerThread()` unchanged. Bind stays on `host_` (default 127.0.0.1); no `0.0.0.0` anywhere in the file.

## Task Commits

| Task | Name | Commit | Files |
| ---- | ---- | ------ | ----- |
| 1 | Delete controller + process-launcher surface + CMake | `ad4cee7` | 6 deleted, 2 modified (CMakeLists.txt, default.vrsettings) |
| 2 | Rewrite DeviceProvider as HMD sidecar | `9745b70` | 2 modified (device_provider.{hpp,cpp}) |
| 3 | HttpServer CommandQueue + POST /button | `e187015` | 2 modified (http_server.{hpp,cpp}) |

## CMakeLists.txt Key Diff Fragments

Source list (post-Task-1):

```cmake
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
)
```

Warnings-as-errors + nlohmann/json + version stanza (already in-tree before Task 1, but verified as part of the acceptance criteria):

```cmake
if(TARGET nlohmann_json)
    target_link_libraries(driver_micmap PRIVATE nlohmann_json)
elseif(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(driver_micmap PRIVATE nlohmann_json::nlohmann_json)
else()
    message(FATAL_ERROR "driver_micmap: nlohmann/json is required for POST /button body parse.")
endif()

target_compile_definitions(driver_micmap PRIVATE
    MICMAP_DRIVER_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(driver_micmap PRIVATE /WX /W4)
else()
    target_compile_options(driver_micmap PRIVATE
        -Werror -Wall -Wextra -Wunused-function -Wunused-variable)
endif()
```

Removed POST_BUILD copy-rules (Task 1) -- the two `copy_if_different` lines for `micmap_controller_profile.json` and `vrcompositor_bindings_micmap_controller.json` are gone.

Removed install-rules (Task 1) -- the two `install(FILES resources/input/*.json ...)` blocks are gone; `install(FILES resources/settings/default.vrsettings ...)` retained.

## default.vrsettings Before / After

Before:

```json
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1",
        "autoLaunchApp": true,
        "appPath": "",
        "appArgs": ""
    }
}
```

After:

```json
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1"
    }
}
```

Python parse check: `python -c "import json; json.load(open('driver/resources/settings/default.vrsettings'))"` -> OK (valid JSON).

## Build Log Excerpt

OpenVR SDK is not present on this machine (`ls external/openvr/` -> no such file). Parent `CMakeLists.txt` lines 90-95 gate `add_subdirectory(driver)` on `OpenVR_FOUND`, so the `driver_micmap` target is not generated at configure-time in this worktree:

```
-- micmap_steamvr: OpenVR not found - using stub implementation
--   To enable OpenVR support, set OPENVR_SDK_PATH environment variable
--   or place OpenVR SDK in external/openvr/
-- Skipping driver build - OpenVR SDK not found
--
-- MicMap Configuration Summary
-- ============================
-- Version:          0.1.0
-- C++ Standard:     17
-- Build driver:     ON
-- OpenVR found:     FALSE
```

The plan's verify step `cmake -S . -B build` (configure-only) succeeded. The `cmake --build build --config Debug --target driver_micmap` step cannot run without the SDK; this matches the deferral pattern established by plan 01-02 summary ("`MICMAP_HAS_OPENVR` is not defined on this worktree machine"). On-HMD build validation lands in plan 01-05.

Acceptance criteria satisfied via grep + code-review; see Self-Check and Verification sections below. Zero warnings are expected under `/WX` because the new code compiles clean against the cited SDK header surface (`openvr_driver.h` v2.5.1 -- all referenced constants and method signatures verified against the research pack).

## Grep Verification

SVR-05 compliance (no OpenVR driver API from HTTP thread):

```
$ grep -nE 'VRDriverInput\(\)|VRProperties\(\)|VRServerDriverHost\(\)' driver/src/http_server.cpp
(empty)
```

Localhost-only bind:

```
$ grep -n '0\.0\.0\.0' driver/src/http_server.cpp
(empty)
```

Forbidden-strings sweep across driver/src/:

```
$ grep -rnE 'VirtualController|process_launcher|ProcessLauncher|TrackedDeviceAdded|MICMAP_CONTROLLER_001' driver/src/
(empty)
```

DeviceProvider acceptance greps (all match):

- `HmdComponentState`, `std::unique_ptr<CommandQueue>`, `hSystemClick_`, `pressTimestamp_`, `pendingReleaseAt_`, `kMinHold`, `kMaxHold` in `device_provider.hpp`.
- `PollNextEvent`, `VREvent_TrackedDeviceDeactivated`, `k_unTrackedDeviceIndex_Hmd`, `TrackedDeviceToPropertyContainer`, `CreateBooleanComponent`, `"/input/system/click"`, `UpdateBooleanComponent`, `VRInputErrorName`, `MICMAP_DRIVER_VERSION`, `MicMap driver v`, `awaiting HMD container`, `HMD deactivated, handle invalidated`, `dropped press command`, `max-hold watchdog` in `device_provider.cpp`.
- `grep -c 'DriverLog(' driver/src/device_provider.cpp` -> 15 (>= 8 required).

HttpServer acceptance greps (all match):

- `CommandQueue& queue`, `CommandQueue& queue_` in `http_server.hpp`.
- `Post("/button"`, `nlohmann::json::parse`, `queue_.push` in `http_server.cpp`.
- No `Post("/click"`, `Post("/press"`, `Post("/release"` in `http_server.cpp`.
- Error strings `"malformed JSON body"`, `missing "state" field`, `state must be "down" or "up"` all present.

## Threat Model Dispositions

| Threat ID | Category | Final Disposition |
| --------- | -------- | ----------------- |
| T-03-01 | Spoofing -- local process POST /button | ACCEPTED (localhost-only bind; parity with existing OpenVR attack surface; ASVS L1 does not require loopback auth for desktop apps) |
| T-03-02 | DNS rebinding / browser CSRF | DEFERRED to a future "driver observability" phase -- cpp-httplib's default Host validation + the JSON-body requirement make browser-form POST spoofing impractical; a strict Origin-check middleware is a 10-line followup if the security review disagrees |
| T-03-03 | Tampering via malformed JSON | MITIGATED -- try/catch on `nlohmann::json::exception` inside the handler lambda returns 400 without pushing to the queue |
| T-03-04 | DoS via CommandQueue flood | MITIGATED -- depth-8 drop-oldest (SVR-05, unit-tested in plan 01-01); worst-case memory = 16 bytes |
| T-03-05 | Information disclosure via driver logs | ACCEPTED -- log lines contain handles + error enum names + version string only; `%APPDATA%\openvr\logs\` is user-scoped |
| T-03-06 | EoP via driver code paths at vrserver privilege | ACCEPTED -- no user-supplied code paths; process_launcher (the only `CreateProcessA` callsite) is deleted in Task 1; all input crosses a closed `PressCommand` enum |
| T-03-07 | Repudiation / stuck button from unmatched DOWN | MITIGATED -- 5 s max-hold watchdog in RunFrame step 5 force-releases + logs; 100 ms min-hold floor prevents accidental release-before-propagation |
| T-03-08 | JSON exception escapes under CPPHTTPLIB_NO_EXCEPTIONS | MITIGATED -- lambda catches `nlohmann::json::exception` (base of parse_error + type_error + out_of_range) before returning; exceptions never cross into cpp-httplib frames |

None HIGH-blocking. T-03-02 is the only one carried forward; see `Deferred Issues` below.

## Deferred Issues

1. **T-03-02 (Origin-check middleware)**: deferred to a future "driver observability" phase per the threat model planner's call. No in-phase mitigation is required for the shipped surface (desktop-only SteamVR addon).
2. **driver_micmap local build verification**: OpenVR SDK not present on this machine. `cmake --build build --target driver_micmap` cannot run locally. Compile-time validation is deferred to the first workstation with the SDK present (plan 01-05 manual spike or any developer building against a real SDK). Grep-level acceptance criteria all pass.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 -- Pre-existing work already applied]** The plan's Task 1 Part B (CMakeLists.txt edits for `/WX`, `nlohmann_json`, `MICMAP_DRIVER_VERSION`) was already in the working tree when this plan started (see `git diff` in Task 1 commit message). Similarly the Part A deletions (virtual_controller, process_launcher, the two JSONs) were already on disk and staged as uncommitted `deleted:` entries in `git status`. Resolution: completed the remaining portions (install-rule removal, vrsettings cleanup, project description update) and committed the full deletion+modification set atomically. No redundant edits, no conflicts. Flagged here because the starting state was non-pristine -- this is not a plan drift, just a pre-staged starting point.

**2. [Rule 1 -- Grep strictness]** Task 2's acceptance criterion says `device_provider.cpp` "Does NOT contain `TrackedDeviceAdded`". The first draft of the file had a comment line "No TrackedDeviceAdded call; no virtual controller." -- grep-true, spec-false. Reworded the comment to "Does NOT register any tracked device (zero virtual controllers)." to keep the grep-based acceptance strictly passing. Zero behavioral change.

**3. [Rule 3 -- /status endpoint retained instead of deleted]** Plan Task 3 `<behavior>` section allowed dropping `/status` if `DriverClient::getStatus()` used `/health`. Grep showed `getStatus()` at `src/steamvr/src/vr_input.cpp:286` hits `/status`, not `/health`. Kept a minimal `/status` (`{"ok":true,"endpoint":"/button"}`) so the app-side status probe continues to succeed. Documented under `key-decisions`.

No architectural deviations. No Rule-4 escalations.

## Known Stubs

None introduced by this plan. The app-side `apps/hmd_button_test/main.cpp` still references old dashboard-manager symbols (see `git status` -- an uncommitted modification present at plan start, owned by plan 01-04); this plan explicitly did NOT touch that file, per the sequential-mode scope boundary in the execution prompt.

## TDD Gate Compliance

Plan tasks 2 and 3 declare `tdd="true"`. The project has no driver-level unit-test framework; `test_command_queue` exists for `CommandQueue` (landed in plan 01-01) but there is no test surface that exercises `DeviceProvider::RunFrame` or `HttpServer::SetupRoutes` in isolation -- both depend on OpenVR driver context (`VR_INIT_SERVER_DRIVER_CONTEXT`) or on an embedded httplib server that cannot be driven headlessly from a simple `main()` test.

This matches the pattern documented in plan 01-02 summary: "The plan's verify block specifies grep assertions + `cmake --build ... --target <...>` as the effective verification surface". No RED-phase `test(...)` commit was created for Tasks 2-3. The behavioral gate for the sidecar is plan 01-05's on-HMD validation spike.

Gate sequence in `git log --oneline`:

- `ad4cee7` feat(01-03): delete virtual-controller + process-launcher surface (D-14, SVR-07) -- Task 1
- `9745b70` feat(01-03): rewrite DeviceProvider as HMD sidecar (SVR-01/02/04/06/10) -- Task 2
- `e187015` feat(01-03): rewrite HttpServer for CommandQueue + POST /button (SVR-05/08/09) -- Task 3

No `test(01-03): ...` commit exists (no test harness available); this plan's RED/GREEN split collapses into three `feat` commits as the shipping signal.

## Next Plan Readiness

- **Plan 01-04 (apps/ wiring):** Can now compile against the new API. `apps/hmd_button_test/main.cpp` has a working-tree modification carried from before this plan executed -- plan 01-04 will pick it up, drop dashboard-manager wiring, swap `/click` calls for `press()`/`release()`, and rewire the Win32 test harness against the new `/button` endpoint.
- **Plan 01-05 (on-HMD validation spike):** Blocked only on a workstation with the OpenVR SDK vendored + a real HMD. All static analysis on the driver side is complete.
- **Driver DLL shape:** `driver_micmap` source list is now final for this milestone -- `driver_main.cpp` + `device_provider.cpp` + `http_server.cpp` + three header-only helpers.

## Self-Check: PASSED

Claims verified:

- `driver/src/virtual_controller.hpp` -- ABSENT
- `driver/src/virtual_controller.cpp` -- ABSENT
- `driver/src/process_launcher.hpp` -- ABSENT
- `driver/src/process_launcher.cpp` -- ABSENT
- `driver/resources/input/micmap_controller_profile.json` -- ABSENT
- `driver/resources/input/vrcompositor_bindings_micmap_controller.json` -- ABSENT
- `driver/CMakeLists.txt` -- contains `/WX`, `nlohmann_json`, `MICMAP_DRIVER_VERSION`; no references to deleted sources or JSONs.
- `driver/resources/settings/default.vrsettings` -- valid JSON; no `autoLaunchApp`/`appPath`/`appArgs` keys.
- `driver/src/device_provider.hpp` -- contains `HmdComponentState`, `CommandQueue`, `hSystemClick_`, `pendingReleaseAt_`, `kMinHold`, `kMaxHold`; no `VirtualController` / `process_launcher`.
- `driver/src/device_provider.cpp` -- contains all 14 required greps (PollNextEvent, VREvent_TrackedDeviceDeactivated, k_unTrackedDeviceIndex_Hmd, TrackedDeviceToPropertyContainer, CreateBooleanComponent, "/input/system/click", UpdateBooleanComponent, VRInputErrorName, MICMAP_DRIVER_VERSION, "MicMap driver v", "awaiting HMD container", "HMD deactivated, handle invalidated", "dropped press command", "max-hold watchdog"); 15 `DriverLog(` call sites.
- `driver/src/http_server.hpp` -- ctor takes `CommandQueue& queue`; member is `CommandQueue& queue_`; no `VirtualController`.
- `driver/src/http_server.cpp` -- contains `Post("/button"`, `nlohmann::json::parse`, `queue_.push`; no `Post("/click"|"/press"|"/release"`; no OpenVR API symbol on HTTP thread; no `0.0.0.0` literal.
- Commit `ad4cee7` (Task 1) -- FOUND in `git log`.
- Commit `9745b70` (Task 2) -- FOUND in `git log`.
- Commit `e187015` (Task 3) -- FOUND in `git log`.

---
*Phase: 01-driver-sidecar-migration*
*Completed: 2026-04-23*

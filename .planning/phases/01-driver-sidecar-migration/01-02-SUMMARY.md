---
phase: 01-driver-sidecar-migration
plan: 02
subsystem: [core, steamvr]
tags: [app, state-machine, interface, contract, http-client]
dependency_graph:
  requires:
    - existing micmap_core state machine
    - existing micmap_steamvr IDriverClient interface
    - cpp-httplib (already vendored)
  provides:
    - PressEdge enum + TriggerCallback = std::function<void(PressEdge)>
    - State::Releasing + minReleaseDuration config knob
    - IDriverClient::press() / IDriverClient::release() (parameterless)
    - DriverClient wire format: POST /button {"state":"down|up"}
  affects:
    - apps/micmap/main.cpp (will break until Plan 04 rewires)
    - apps/hmd_button_test/main.cpp (will break until Plan 04 rewires)
    - driver/src/http_server.cpp (server-side /button handler lands in Plan 03)
tech_stack:
  added: []
  patterns:
    - Single-surface client API (press/release vs. multi-button/string-parameter)
    - Edge-carrying callback pattern (PressEdge::Down / PressEdge::Up)
    - Debounce via intermediate state (Releasing) instead of timer callback
key_files:
  created: []
  modified:
    - src/core/include/micmap/core/state_machine.hpp
    - src/core/src/state_machine.cpp
    - src/steamvr/include/micmap/steamvr/vr_input.hpp
    - src/steamvr/src/vr_input.cpp
decisions:
  - "Deferred removal of DashboardState/HMDButtonAction/VREventType/VREvent/VREventCallback to Plan 04: grep shows 6 consumer files including the IVRInput interface itself. Plan explicitly authorises defer-with-justification."
  - "Bridged OpenVRInput::sendDashboardSelect's driverClient_->click('trigger',100) to press()+release() pair. Rule 3 fix: otherwise micmap_steamvr fails to compile under MICMAP_HAS_OPENVR. Full OpenVRInput dashboard-branching path is scheduled for Plan 04 deletion."
metrics:
  duration_sec: 205
  tasks_completed: 2
  files_modified: 4
  completed_date: 2026-04-23
requirements: [SVR-08, SVR-09]
---

# Phase 01 Plan 02: App-Side Contracts (State Machine + IDriverClient) Summary

State machine grows a Releasing-state debounce and a PressEdge-carrying callback; IDriverClient collapses to parameterless press()/release() hitting POST /button with JSON bodies. Both targets (`micmap_core` and `micmap_steamvr`) build clean under Debug; apps/ will not link until Plan 04 rewires main.cpp callsites.

## What Was Built

### Task 1 — State machine: Releasing state + PressEdge callback

**Header deltas (`src/core/include/micmap/core/state_machine.hpp`):**
- `StateMachineConfig::minDetectionDuration`: `500` -> `100` ms (D-04)
- `StateMachineConfig::minReleaseDuration`: NEW, `80` ms (D-10)
- `StateMachineConfig::cooldownDuration`: `300` -> `200` ms (D-11, post-release semantic)
- `State` enum: inserted `Releasing` between `Triggered` and `Cooldown`
- `stateToString`: added `case State::Releasing: return "Releasing";`
- Added `enum class PressEdge { Down, Up };`
- `TriggerCallback`: `std::function<void()>` -> `std::function<void(PressEdge)>`

**Implementation deltas (`src/core/src/state_machine.cpp`):**
- `update()` switch now dispatches `State::Releasing` -> `updateReleasing()` and `State::Triggered` -> `updateTriggered(detectionConfidence)` (signature changed to take confidence)
- `updateDetecting`: on DOWN-edge transition, now emits `triggerCallback_(PressEdge::Down)` (replaces old no-arg call)
- `updateTriggered`: now only debounces confidence drop into `State::Releasing`; does NOT emit a callback
- **NEW** `updateReleasing`: on confidence recovery -> `State::Triggered` (no callback); on `timeInState_ >= minReleaseDuration` -> `State::Cooldown` and emit `triggerCallback_(PressEdge::Up)`
- `updateCooldown`: unchanged logic, comment updated to document post-release semantic

Commit: `8c7f9c9 feat(01-02): add Releasing state + PressEdge callback to state machine`

### Task 2 — IDriverClient collapse to press()/release() over POST /button

**Header deltas (`src/steamvr/include/micmap/steamvr/vr_input.hpp`):**
- Removed `virtual bool click(const std::string&, int) = 0;`
- Removed `virtual bool press(const std::string&) = 0;`
- Removed `virtual bool release(const std::string&) = 0;`
- Added `virtual bool press() = 0;` (POST /button `{"state":"down"}`)
- Added `virtual bool release() = 0;` (POST /button `{"state":"up"}`)
- `connect()`, `disconnect()`, `isConnected()`, `getStatus()`, `getPort()`, `getLastError()` untouched
- `createDriverClient(...)` factory signature untouched

**Implementation deltas (`src/steamvr/src/vr_input.cpp`):**
- Replaced three methods (`click`/`press(string)`/`release(string)`) with two parameterless methods
- Both methods call `httplib::Client::Post("/button", R"({"state":"down"|"up"})", "application/json")`
- Kept the existing `ensureConnected()` helper idiom and the same lastError_ conventions
- Bridged `OpenVRInput::sendDashboardSelect`'s stale `driverClient_->click("trigger", 100)` to a `press()` + `release()` pair (Rule 3 fix — see Deviations)

Commit: `2e727a1 feat(01-02): collapse IDriverClient to press()/release() over POST /button`

## Build Log Excerpt

```
$ cmake --build build --config Debug --target micmap_core
  state_machine.cpp
  config_manager.cpp
  micmap_core.vcxproj -> build\lib\Debug\micmap_core.lib

$ cmake --build build --config Debug --target micmap_steamvr
  vr_input.cpp
  dashboard_manager.cpp
  micmap_steamvr.vcxproj -> build\lib\Debug\micmap_steamvr.lib
```

Both targets compile clean under `--config Debug`. No warnings surfaced beyond the pre-existing FetchContent deprecation warning on `cpp_httplib`/`imgui` (out of scope).

Note: `MICMAP_HAS_OPENVR` is not defined on this worktree machine (OpenVR SDK not present), so the `OpenVRInput` path is conditionally excluded. The press/release bridge in `sendDashboardSelect` was still authored to keep the OpenVR path compilable when the SDK is available.

## Dashboard-Symbol Consumer Audit

Ran `grep -rEn 'DashboardState|HMDButtonAction|VREventType|VREventCallback' apps/ src/ driver/`. Unique consumer files:

```
apps/hmd_button_test/main.cpp
apps/micmap/main.cpp
src/steamvr/include/micmap/steamvr/dashboard_manager.hpp
src/steamvr/include/micmap/steamvr/vr_input.hpp
src/steamvr/src/dashboard_manager.cpp
src/steamvr/src/vr_input.cpp
```

Additional hits (raw grep): 89 total occurrences across the above files. Consumers include:
- `IVRInput::getDashboardState()` pure virtual and `IVRInput::setEventCallback(VREventCallback)` in `vr_input.hpp` (the interface itself depends on the five symbols).
- `apps/micmap/main.cpp` lines 223, 353-394 (dashboard state branching, tray UI).
- `apps/hmd_button_test/main.cpp` lines 64, 176-196, 214-220, 538-694 (dashboard polling + callbacks).
- `dashboard_manager.hpp`/`dashboard_manager.cpp` (the dashboard manager itself — dies with Plan 04).

### Decision: DEFER removal to Plan 04

Per this plan's Task 2 decision rule: "If hits exist in `apps/micmap/main.cpp` or `apps/hmd_button_test/main.cpp`: DO NOT delete the symbols here." Hits exist in both, plus the symbols are load-bearing for the `IVRInput` interface surface (`getDashboardState()`, `sendHMDButtonEvent()`, `sendDashboardSelect()`, `performDashboardAction()`, `setEventCallback(VREventCallback)`).

Plan 04 is responsible for:
1. Rewriting `apps/micmap/main.cpp` and `apps/hmd_button_test/main.cpp` to drop dashboard-branching.
2. Deleting `dashboard_manager.{hpp,cpp}` entirely.
3. Deleting `IVRInput::getDashboardState()`, `performDashboardAction()`, `sendDashboardSelect()`, `sendHMDButtonEvent()` from `vr_input.hpp`.
4. Deleting `OpenVRInput` / `StubVRInput` (or at minimum the dashboard-state members).
5. THEN removing the five dashboard-state symbol definitions from `vr_input.hpp`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking Issue] Bridged `OpenVRInput::sendDashboardSelect`'s stale `driverClient_->click("trigger", 100)` call to `press() + release()`**

- **Found during:** Task 2 — grepping for `driverClient_->click`.
- **Issue:** `src/steamvr/src/vr_input.cpp:507` inside `OpenVRInput::sendDashboardSelect()` called `driverClient_->click("trigger", 100)`. The plan removes `click(string,int)` from the interface. Under `MICMAP_HAS_OPENVR` this would cause `micmap_steamvr` to fail compilation — which violates Task 2's acceptance criterion "`micmap_steamvr` target builds under `--config Debug`."
- **Why local:** The whole `OpenVRInput::sendDashboardSelect` method is on the chopping block in Plan 04 (dashboard-branching deletion). Writing a new parameterless API then leaving a caller that demands the old API would be self-inconsistent.
- **Fix:** Replaced the single `click(...)` call with sequential `press()` + `release()` calls — semantically a "click", using the new API surface. Added a block comment noting Plan 04 ownership for the full removal.
- **Files modified:** `src/steamvr/src/vr_input.cpp` (inside `OpenVRInput::sendDashboardSelect`)
- **Commit:** `2e727a1` (same commit as the Task 2 DriverClient rewrite — both changes are coupled to the interface swap)

**2. [Rule 3 — Signature evolution] Added `detectionConfidence` parameter to `updateTriggered`**

- **Found during:** Task 1 — writing the dispatch switch.
- **Issue:** The plan spec says "Replace the body with: if `detectionConfidence < config_.detectionThreshold` -> `transitionTo(State::Releasing);`". The existing `updateTriggered()` was parameterless. Implementing the new behaviour requires the current confidence.
- **Fix:** Changed the signature to `void updateTriggered(float detectionConfidence)` and wired it in the dispatch switch. This is consistent with the other state updaters (`updateIdle`, `updateDetecting`, `updateReleasing` all take confidence).
- **Files modified:** `src/core/src/state_machine.cpp` (private helper; no header change needed).
- **Commit:** `8c7f9c9`.

### Scope Adherence

- No edits outside `src/core/` and `src/steamvr/`. No edits to `apps/`, `driver/`, `tests/`, or `CMakeLists.txt` files.
- The `test_placeholder` build target is untouched. Project has no unit-test framework (plan spec's `tdd="true"` effectively maps to grep+build verification, per verify block).

## Known Stubs

None introduced by this plan. Pre-existing stubs (overlay UI stubs, non-Windows audio stubs) untouched.

## Deferred Issues

None — both tasks completed cleanly on the first auto-fix pass.

## Notes for Downstream Consumers

- **Plan 03 (driver-side `/button` handler):** Expect request body shape `{"state":"down"}` or `{"state":"up"}` with `Content-Type: application/json`. Client retries by probing `/health` across port range (unchanged).
- **Plan 04 (apps/ wiring):** Must:
  1. Update `apps/micmap/main.cpp:241` lambda from `[](){ ... }` to `[](PressEdge edge){ switch(edge){...} }`.
  2. Replace any `driverClient_->click(...)` / `driverClient_->press("system")` / `driverClient_->release("system")` callsites with parameterless `press()` / `release()`.
  3. Handle the DashboardState/HMDButtonAction/VREventType/VREvent/VREventCallback removal sweep.
- **`apps/micmap` will not link until Plan 04 lands** — this is expected per the plan. The `micmap_core` and `micmap_steamvr` library targets build individually.

## TDD Gate Compliance

Plan declares `tdd="true"` on both tasks, but the project has no unit-test framework (`tests/CMakeLists.txt` only exposes `test_placeholder`). The plan's verify block specifies grep assertions + `cmake --build ... --target micmap_core/micmap_steamvr` as the effective verification surface — which both passed. No RED-phase `test(...)` commit was created because there is no test code to author. Adding a test framework would be an architectural change (Rule 4) out of scope for this plan.

## Self-Check: PASSED

**Commits:**
- `8c7f9c9 feat(01-02): add Releasing state + PressEdge callback to state machine` — FOUND
- `2e727a1 feat(01-02): collapse IDriverClient to press()/release() over POST /button` — FOUND

**Files modified:**
- `src/core/include/micmap/core/state_machine.hpp` — FOUND
- `src/core/src/state_machine.cpp` — FOUND
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` — FOUND
- `src/steamvr/src/vr_input.cpp` — FOUND

**Verification greps (from plan verify block):**
- `grep -q 'enum class PressEdge' src/core/include/micmap/core/state_machine.hpp` — PASS
- `grep -c 'triggerCallback_(PressEdge::' src/core/src/state_machine.cpp` == 2 — PASS
- `grep -q 'Post("/button"' src/steamvr/src/vr_input.cpp` — PASS
- `! grep -qE 'virtual bool click\(|virtual bool press\(const std::string|virtual bool release\(const std::string' src/steamvr/include/micmap/steamvr/vr_input.hpp` — PASS
- `cmake --build build --config Debug --target micmap_core --target micmap_steamvr` — PASS

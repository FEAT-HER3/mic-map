---
phase: 01-driver-sidecar-migration
plan: 04
subsystem: [app, steamvr]
tags: [app, apps, state-machine, wiring, deletion, svr-07, grep-gate]
dependency_graph:
  requires:
    - "src/core/include/micmap/core/state_machine.hpp — PressEdge + TriggerCallback<PressEdge> (Plan 01-02)"
    - "src/steamvr/include/micmap/steamvr/vr_input.hpp — IDriverClient::press()/release() (Plan 01-02)"
    - "driver/src/http_server.cpp — POST /button handler (Plan 01-03)"
  provides:
    - "apps/micmap onTrigger(PressEdge) → driverClient->press()/release() direct wire"
    - "apps/hmd_button_test Send Press / Send Release / Tap harness hitting POST /button"
    - "micmap_steamvr.lib stripped of dashboard_manager translation unit"
    - "Phase 1 code completion — SVR-07 forbidden-string grep sweep returns zero hits"
  affects:
    - "src/steamvr/src/vr_input.cpp — StubVRInput + OpenVRInput slimmed (dashboard methods removed)"
    - "Plan 01-05 manual-VR validation spike can begin (hmd_button_test.exe ready)"
tech-stack:
  added: []
  patterns:
    - "Direct edge wiring: state machine → IDriverClient (no intermediate dashboard-state indirection)"
    - "Win32 push-button harness: three first-class operations (Press/Release/Tap) + two driver utilities (Test/Reconnect)"
    - "Lazy driver-client construction in test harness — startup does not depend on driver being live"
key-files:
  created: []
  modified:
    - apps/micmap/main.cpp
    - apps/hmd_button_test/main.cpp
    - src/steamvr/CMakeLists.txt
    - src/steamvr/include/micmap/steamvr/vr_input.hpp
    - src/steamvr/src/vr_input.cpp
  deleted:
    - src/steamvr/src/dashboard_manager.cpp
    - src/steamvr/include/micmap/steamvr/dashboard_manager.hpp
key-decisions:
  - "Executed Part D (bonus removal) from the plan: stripped DashboardState, HMDButtonAction, getDashboardState(), sendHMDButtonEvent(), sendDashboardSelect(), performDashboardAction() from IVRInput + its implementations. Rationale: zero remaining in-repo consumers after app rewrite, and Plan 01-02 SUMMARY explicitly handed these to Plan 01-04."
  - "Kept VREventType / VREvent / VREventCallback — apps consume VREventType::Quit (SteamVR-closing lifecycle) and vr_input.cpp emits SteamVRConnected/Disconnected. The unused DashboardOpened/DashboardClosed/ButtonPressed/ButtonReleased enum values are retained to avoid touching a second enum for zero functional benefit; they are not forbidden strings under SVR-07."
  - "OpenVRInput no longer holds vrOverlay_ or driverClient_ fields. Button presses never traverse vr_input.cpp; they go straight through IDriverClient."
  - "hmd_button_test is a developer-only tool. Replaced Open Dashboard / Send Click / Auto / Send A / Send System (6 buttons + handlers) with Send Press / Send Release / Tap / Reconnect Driver (Test Driver retained). Harness size dropped 841 LOC → 612 LOC."
metrics:
  duration_sec: 2580
  tasks_completed: 3
  files_modified: 5
  files_deleted: 2
  completed_date: 2026-04-23
requirements: [SVR-07, SVR-08, SVR-11]
---

# Phase 01 Plan 04: App-Side Rewire + Dashboard-Manager Delete Summary

Finished the app-side port to the new Plan-02/03 wire contracts: `apps/micmap` now drives `IDriverClient::press()/release()` directly from the state machine's `PressEdge` callback; `apps/hmd_button_test` is a clean Press/Release/Tap harness for the Plan 05 on-HMD spike; the entire `IDashboardManager` surface (file pair + CMake entry + IVRInput dashboard methods) is gone. SVR-07 forbidden-string grep across `driver/src src/ apps/` returns zero hits. All non-driver targets link green; `test_placeholder`, `test_config_manager`, and `test_command_queue` all pass under `ctest -C Debug`.

## What Was Built

### Task 1 — `apps/micmap/main.cpp` onTrigger(PressEdge) rewire (pre-landed as `9545811`)

Task 1 was already committed in-tree when this executor started. Verification against the plan spec:

- `onTrigger` declared as `void onTrigger(core::PressEdge edge)` (header ~line 101).
- Body (lines 334-345): null-guard on `driverClient` + `isConnected()`, then `driverClient->press()` on Down / `driverClient->release()` on Up, with a `MICMAP_LOG_WARNING` on failure that surfaces `driverClient->getLastError()`.
- `setTriggerCallback` lambda (line 234): `[this](core::PressEdge edge) { onTrigger(edge); }`.
- Zero references to `dashboard_manager`, `IDashboardManager`, `createDashboardManager`, `performDashboardAction`, `DashboardState`, or `isDashboardOpen` in `apps/micmap/main.cpp`.
- `driverClient->press()` occurs exactly once in onTrigger; same for `driverClient->release()`.

Commit: **`9545811 feat(01-04): rewire apps/micmap onTrigger(PressEdge) to driverClient press/release`** (pre-existing, verified not re-done).

### Task 2 — `apps/hmd_button_test/main.cpp` Press/Release/Tap harness (`d416d62`)

Rewrote the harness in full (612 LOC replacing 841 LOC). Summary of edits:

**Before / After button matrix:**

| Button | Old behaviour (virtual-controller era) | New behaviour |
|---|---|---|
| Open Dashboard | `dashboardManager->openDashboard()` | **DELETED** (control ID + handler + CreateWindowW all removed) |
| Send Click | `driverClient->click("trigger", 100)` | **DELETED** |
| Auto | `dashboardManager->performDashboardAction()` | **DELETED** |
| Send A Button | `driverClient->click("a", 100)` | **DELETED** |
| Send System | `driverClient->click("system", 100)` | **DELETED** |
| Reconnect SteamVR | `dashboardManager->disconnect() + connect()` | **RENAMED→Reconnect Driver** — disconnects/reconnects `IDriverClient` only |
| Test Driver | `driverClient->connect() + getStatus()` | KEPT — unchanged semantics |
| — (new) | — | **Send Press (Down)** → `driverClient_->press()` |
| — (new) | — | **Send Release (Up)** → `driverClient_->release()` |
| — (new) | — | **Tap (Press + 150ms + Release)** → press() + Sleep(150) + release() |

**Removed state:** `std::unique_ptr<IDashboardManager> dashboardManager_`, `DashboardState dashboardState_`, `ConnectionState connectionState_`, the Dashboard status label, all `setDashboardCallback` / `setConnectionCallback` / `setQuitCallback` wiring, the DashboardState switch blocks in `UpdateStatus()` and the event callback.

**Added state:** three `HWND` slots for the new buttons, `Utf8ToWide` helper for rendering driver error strings in the Win32 log.

**Control IDs:** `ID_SEND_PRESS_BUTTON=201`, `ID_SEND_RELEASE_BUTTON=202`, `ID_SEND_TAP_BUTTON=203`, `ID_TEST_DRIVER_BUTTON=204`, `ID_RECONNECT_DRIVER_BUTTON=205`. New range deliberately does not collide with the old 101-109 range.

**KEEPT unchanged:** WinMain, `RegisterClassW` / `CreateWindowExW` scaffolding, the GetMessage loop, `AddLogEntry` + `SetLastResult` helpers, the event-log ListBox, the `SetTimer(250ms)` cadence, `IVRInput::setEventCallback([]{ if (Quit) PostQuitMessage(0); })` (this is the only SteamVR-lifecycle hook the harness still needs).

Grep gates:

```
$ grep -cE 'dashboard_manager|IDashboardManager|DashboardState|performDashboardAction|HMDButtonAction' apps/hmd_button_test/main.cpp
0

$ grep -qE 'driverClient_?->click\(' apps/hmd_button_test/main.cpp
(no hits)

$ grep -qE 'driverClient_?->press\(\)' apps/hmd_button_test/main.cpp
(hits, 3 call sites)

$ grep -qE 'driverClient_?->release\(\)' apps/hmd_button_test/main.cpp
(hits, 3 call sites)
```

Build:

```
hmd_button_test.vcxproj -> C:\Users\decid\Documents\projects\mic-map\build\bin\Debug\hmd_button_test.exe
```

Commit: **`d416d62 feat(01-04): repurpose hmd_button_test harness for POST /button press/release`** (1 file, +328 / -517).

### Task 3 — Delete dashboard_manager + strip IVRInput dashboard surface (`fd7cd91`)

**Part A — Deletions:**

```
D src/steamvr/include/micmap/steamvr/dashboard_manager.hpp
D src/steamvr/src/dashboard_manager.cpp
```

**Part B — CMake trim (`src/steamvr/CMakeLists.txt`):**

```diff
 add_library(micmap_steamvr STATIC
     src/vr_input.cpp
-    src/dashboard_manager.cpp
 )
```

`micmap_steamvr` now builds from a single translation unit.

**Part C — SVR-07 forbidden-string grep sweep:**

```
$ grep -rnE 'VirtualController|virtual_controller|TrackedDeviceAdded|ProcessLauncher|process_launcher|IDashboardManager|DashboardManager|dashboard_manager|DashboardState|isDashboardOpen|dashboard_open|performDashboardAction|ControllerDevice|ITrackedDeviceServerDriver|open_vs_select|micmap_controller_profile|MICMAP_CONTROLLER_001' driver/src src/ apps/
(empty output — exit code 1 — zero hits)
```

Gate passes.

**Part D — vr_input.hpp / vr_input.cpp dashboard-method strip (bonus, greenlit by plan):**

Headers removed:

- `enum class DashboardState` (Closed/Open/Unknown)
- `enum class HMDButtonAction` (ToggleDashboard/DashboardSelect/CustomAction)
- `IVRInput::getDashboardState()`, `sendHMDButtonEvent()`, `sendDashboardSelect()`, `performDashboardAction()`
- Stale class-header prose that described dashboard-state branching

Kept:

- `VREventType` enum (apps consume `::Quit`, `::SteamVRConnected`, `::SteamVRDisconnected`; OpenVR backend emits those three — the `DashboardOpened`/`DashboardClosed`/`ButtonPressed`/`ButtonReleased` values remain defined but unreferenced, not forbidden strings)
- `VREvent` struct + `VREventCallback` typedef
- `IVRInput::initialize/shutdown/isInitialized/isVRAvailable/pollEvents/setEventCallback/getRuntimeName/getLastError`
- `createOpenVRInput()` + `createStubVRInput()` factories
- `IDriverClient` surface untouched from Plan 01-02

Implementation changes in `vr_input.cpp`:

- **StubVRInput:** dropped `getDashboardState`, `sendHMDButtonEvent`, `sendDashboardSelect`, `performDashboardAction` method bodies and the `dashboardState_` member.
- **OpenVRInput:** dropped the same four methods + the `vrOverlay_` (IVROverlay*) and `driverClient_` (IDriverClient) fields; `initialize()` no longer calls `vr::VROverlay()`; `shutdown()` no longer nulls `vrOverlay_`.
- **processVREvent:** now only handles `vr::VREvent_Quit`. The `VREvent_DashboardActivated`, `VREvent_DashboardDeactivated`, `VREvent_ButtonPress`, `VREvent_ButtonUnpress` cases are gone — app-layer dashboard-state awareness is no longer a feature.

**Part E — Full build verification:**

```
cmake --build build --config Debug --target micmap_steamvr micmap_core micmap hmd_button_test mic_test test_command_queue test_placeholder
```

All targets link green:

```
  kissfft.vcxproj -> ...lib\Debug\kissfft.lib
  micmap_common.vcxproj -> ...lib\Debug\micmap_common.lib
  micmap_audio.vcxproj -> ...lib\Debug\micmap_audio.lib
  micmap_detection.vcxproj -> ...lib\Debug\micmap_detection.lib
  micmap_core.vcxproj -> ...lib\Debug\micmap_core.lib
  micmap_steamvr.vcxproj -> ...lib\Debug\micmap_steamvr.lib
  micmap.vcxproj -> ...bin\Debug\micmap.exe
  hmd_button_test.vcxproj -> ...bin\Debug\hmd_button_test.exe
  mic_test.vcxproj -> ...bin\Debug\mic_test.exe
  test_command_queue.vcxproj -> ...bin\Debug\test_command_queue.exe
  test_placeholder.vcxproj -> ...bin\Debug\test_placeholder.exe
```

Remaining warnings are all pre-existing patterns in `hmd_button_test/main.cpp`: `C4005` (WIN32_LEAN_AND_MEAN / NOMINMAX redefine — defined on the MSBuild command line AND in the source; harmless), `C4100` (WinMain's `hPrevInstance` / `lpCmdLine` — Windows idiom), `C4312` (`HMENU` int→pointer cast — Windows idiom for control IDs). None are regressions.

The bare `cmake --build build --config Debug` invocation fails on the `copy_distributable_files` custom step because it expects `build/driver/micmap/` to exist — which it does not without the OpenVR SDK. This matches the known deferral carried from Plan 01-03 Summary. **This is NOT a regression of Plan 01-04**; it is a pre-existing consequence of OpenVR SDK absence on this workstation, and `copy_distributable_files` is out of scope for Plan 01-04. Plan 01-05 addresses the OpenVR SDK setup + on-HMD driver build.

**Part F — ctest verification:**

```
$ ctest --test-dir build --output-on-failure -C Debug
    Start 1: test_placeholder
1/3 Test #1: test_placeholder .................   Passed    0.01 sec
    Start 2: test_config_manager
2/3 Test #2: test_config_manager ..............   Passed    0.02 sec
    Start 3: test_command_queue
3/3 Test #3: test_command_queue ...............   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

**Part G — Post-build ghost-artifact check:**

```
$ find build/driver/micmap/resources -name 'micmap_controller_profile.json' -o -name 'vrcompositor_bindings_micmap_controller.json' 2>/dev/null
(empty)
```

No controller-profile JSONs. (Also, `build/driver/` does not exist at all on this machine — Plan 01-03 skipped driver configure due to missing OpenVR.)

Commit: **`fd7cd91 feat(01-04): delete dashboard_manager + strip IVRInput dashboard surface (SVR-07)`** (6 files, +38 / -989).

## Task Commits

| Task | Name | Commit | Files |
| ---- | ---- | ------ | ----- |
| 1 | apps/micmap onTrigger(PressEdge) rewire | `9545811` (pre-existing) | 1 modified (apps/micmap/main.cpp) |
| 2 | hmd_button_test Press/Release/Tap harness | `d416d62` | 1 modified (apps/hmd_button_test/main.cpp; +328/-517) |
| 3 | dashboard_manager delete + IVRInput strip + SVR-07 sweep | `fd7cd91` | 6 touched (2 deleted, 4 modified; +38/-989) |

## Before/After — `apps/micmap/main.cpp` onTrigger

Before (virtual-controller era, pre-phase):

```cpp
void MicMapApp::onTrigger() {
    // Three-layer fallback: dashboardManager->performDashboardAction() → vrInput->sendDashboardSelect() → driverClient->click("system")
    if (dashboardManager && dashboardManager->performDashboardAction()) return;
    if (vrInput && vrInput->sendDashboardSelect()) return;
    if (driverClient) driverClient->click("system", 100);
}
```

After (this plan, via pre-existing commit `9545811`):

```cpp
void MicMapApp::onTrigger(core::PressEdge edge) {
    if (!driverClient || !driverClient->isConnected()) {
        MICMAP_LOG_DEBUG("onTrigger({}): driver not connected, skipping",
                         edge == core::PressEdge::Down ? "down" : "up");
        return;
    }
    bool ok = (edge == core::PressEdge::Down) ? driverClient->press()
                                              : driverClient->release();
    if (!ok) {
        MICMAP_LOG_WARNING("onTrigger failed: {}", driverClient->getLastError());
    }
}
```

And the corresponding `setTriggerCallback` wiring (line 234):

```cpp
stateMachine->setTriggerCallback([this](core::PressEdge edge) { onTrigger(edge); });
```

## Before/After — hmd_button_test UI surface

Before: three status rows (SteamVR / Driver / Dashboard), eight buttons (Open Dashboard, Send Click, Auto, Reconnect SteamVR, Test Driver, Send A Button, Send System) split across three rows, Dashboard state polling driven by a DashboardManager shared with the main app.

After: two status rows (SteamVR / Driver), five buttons across three rows:

- Row 1 (POST /button actions): `Send Press (Down)` | `Send Release (Up)`
- Row 2 (POST /button convenience): `Tap (Press + 150ms + Release)` (wide)
- Row 3 (driver utilities): `Test Driver (/health + /status)` | `Reconnect Driver`

Operator workflow for Plan 01-05 spike: launch SteamVR → launch `hmd_button_test.exe` → click `Test Driver` (confirms driver HTTP endpoint is alive) → click `Tap` → observe dashboard toggle on-HMD. If the dashboard does not toggle, use `Send Press` / `Send Release` individually to isolate whether the /button handler or the HMD component update is the issue.

## `git status` / `git log` snapshot at plan close

```
$ git status --short
?? .claude/   (Claude Code scratch; untracked, ignored)

$ git log --oneline -6
fd7cd91 feat(01-04): delete dashboard_manager + strip IVRInput dashboard surface (SVR-07)
d416d62 feat(01-04): repurpose hmd_button_test harness for POST /button press/release
9545811 feat(01-04): rewire apps/micmap onTrigger(PressEdge) to driverClient press/release
8979a40 docs(phase-02): sync plan artifacts before wave 2 execution
d0e2c72 chore: merge executor worktree (worktree-agent-a4587a3b)
274b2ee chore: merge executor worktree (worktree-agent-a1454420)
```

## SVR-07 Forbidden-String Sweep — Canonical Output

```
$ grep -rnE 'VirtualController|virtual_controller|TrackedDeviceAdded|ProcessLauncher|process_launcher|IDashboardManager|DashboardManager|dashboard_manager|DashboardState|isDashboardOpen|dashboard_open|performDashboardAction|ControllerDevice|ITrackedDeviceServerDriver|open_vs_select|micmap_controller_profile|MICMAP_CONTROLLER_001' driver/src src/ apps/
(zero output)
$ echo $?
1
```

(grep returns 1 on "no matches" — the pass condition.)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 — Bug] Reworded a comment in `apps/hmd_button_test/main.cpp` that contained the forbidden string "DashboardManager"**

- **Found during:** Task 3, Part C SVR-07 grep sweep.
- **Issue:** After Task 2 rewrote the file, one comment still read `// SteamVR status (IVRInput-based; no DashboardManager anymore)`. That `DashboardManager` substring hits the SVR-07 forbidden-string regex.
- **Fix:** Reworded to `// SteamVR status (IVRInput-based; dashboard-state polling was removed in Plan 01-04)`.
- **Files modified:** `apps/hmd_button_test/main.cpp` (folded into commit `fd7cd91`).

**2. [Bonus — Greenlit by plan Part D] Executed the "DashboardState/HMDButtonAction/IVRInput dashboard methods" removal from `vr_input.hpp` + `vr_input.cpp`**

- **Found during:** Task 3, Part C SVR-07 grep showed hits in `src/steamvr/`.
- **Context:** Plan 01-02 Summary explicitly deferred these removals to Plan 01-04 ("Plan 04 is responsible for: ... Deleting IVRInput::getDashboardState(), performDashboardAction(), sendDashboardSelect(), sendHMDButtonEvent() from vr_input.hpp. THEN removing the five dashboard-state symbol definitions."). Plan 01-04 Task 3 Part D explicitly allows this as bonus cleanup.
- **Action:** Removed `DashboardState` enum, `HMDButtonAction` enum, and 4 IVRInput dashboard methods + their StubVRInput/OpenVRInput bodies. Removed `vrOverlay_` and `driverClient_` fields from OpenVRInput. Simplified `processVREvent` to Quit-only.
- **Files modified:** `src/steamvr/include/micmap/steamvr/vr_input.hpp`, `src/steamvr/src/vr_input.cpp` (folded into commit `fd7cd91`).

**3. [Rule 3 — Scope] Left `VREventType::DashboardOpened|DashboardClosed|ButtonPressed|ButtonReleased` enum values in place**

- **Issue:** After the implementation strip, nothing emits or consumes these four enum values (`::Quit`, `::SteamVRConnected`, `::SteamVRDisconnected`, `::None` are the only live ones).
- **Decision:** They are NOT forbidden strings under SVR-07 — the regex matches `DashboardState` (enum) and `performDashboardAction`, not these names. Removing them would require recompiling every TU that includes `vr_input.hpp` without any functional benefit. Left in, marked as "unused enum values" in the enum doc comment.
- **Impact:** None. Zero build warnings, zero runtime impact.

### No architectural deviations

No Rule 4 escalations required. No scope overrun; all edits stayed inside the files listed in the plan's `files_modified` + `files_deleted` sets.

## Known Stubs

None. The harness `Reconnect Driver` / `Test Driver` buttons invoke live `IDriverClient` methods; no mocked responses or hardcoded-empty UI slots.

Pre-existing non-plan-scope stubs remain untouched (non-Windows audio stubs, etc.).

## Deferred Issues

1. **driver_micmap full build still blocked on OpenVR SDK absence.** Same deferral as Plan 01-03 Summary; all app / library / test targets this plan depends on build green. On-HMD build validation is Plan 01-05's responsibility.
2. **`copy_distributable_files` CMake target fails when invoked via bare `cmake --build build --config Debug`** because it tries to copy `build/driver/micmap/` to `build/bin/driver/micmap/` and the source doesn't exist without an OpenVR-enabled driver build. Not a regression of this plan. Targeted builds (`--target micmap`, `--target hmd_button_test`, `--target test_command_queue`) all succeed.
3. **`VREventType::DashboardOpened|DashboardClosed|ButtonPressed|ButtonReleased` are dead enum values.** Retained for minimal-diff reasons (see Deviation #3). A future cleanup plan may delete them in a single-file edit when the enum is next touched.

## Ready-for-Spike Statement

`hmd_button_test.exe` is built at `build/bin/Debug/hmd_button_test.exe`, exercises `IDriverClient::press()`, `release()`, and a 150ms `Tap` via the POST /button wire, and is ready for Plan 01-05's manual on-HMD validation session. Operator workflow is documented in the UI (event log + Last Result status). The app version (`micmap.exe`) drives the same IDriverClient instance from the detection state machine via `onTrigger(PressEdge)` — same code path, different edge source.

## Threat Model Dispositions

| Threat ID | Category | Final Disposition |
|-----------|----------|-------------------|
| T-04-01 | Tampering — incomplete press/release pair | MITIGATED — state machine (Plan 01-02) is the sole PressEdge source; Releasing→Cooldown guarantees Up follows Down unless the process crashes mid-edge, in which case the driver's 5s max-hold watchdog (Plan 01-03) force-releases. |
| T-04-02 | DoS via hmd_button_test spam | ACCEPTED — developer tool only. Max stuck-down = 5s (driver watchdog). No auth bypass, no data corruption. |
| T-04-03 | EoP via hmd_button_test | ACCEPTED — same `IDriverClient` factory as main app; no new privileged surface. |
| T-04-04 | Info Disclosure via MICMAP_LOG_WARNING error text | ACCEPTED — driver error strings are diagnostic ("Server returned status 400"), not sensitive. User-scoped log file. |
| T-04-05 | Spoofing via IDashboardManager removal | ACCEPTED — IDashboardManager had no auth; removing it does not reduce security posture. POST /button server-side threat lives in Plan 01-03 (T-03-01/02). |

No new HIGH-severity threats introduced. All items are either managed by Plan 01-02/03 mitigations or accepted as out-of-scope for ASVS L1 on local developer tooling.

## TDD Gate Compliance

Plan declares `tdd="true"` on Tasks 1 and 2. The project still has no unit-test framework for app-level Win32 GUI code or state-machine-to-IDriverClient integration. Per the pattern established in Plans 01-02 and 01-03, the verify block's grep assertions + `cmake --build ... --target <app>` is the effective verification surface.

Gate sequence in `git log --oneline`:
- `9545811` feat(01-04): rewire apps/micmap onTrigger(PressEdge) ... — Task 1 GREEN (pre-landed)
- `d416d62` feat(01-04): repurpose hmd_button_test harness ... — Task 2 GREEN
- `fd7cd91` feat(01-04): delete dashboard_manager + strip IVRInput dashboard surface ... — Task 3 GREEN

No RED-phase `test(...)` commits — no test harness exists to author. Plan 01-05's manual on-HMD validation is the behavioural gate.

## Self-Check: PASSED

Files:
- `apps/micmap/main.cpp` — PRESENT, contains `onTrigger(core::PressEdge` + `setTriggerCallback([this](core::PressEdge` + `driverClient->press()` + `driverClient->release()`; FREE of `dashboard_manager|IDashboardManager|DashboardState|performDashboardAction|HMDButtonAction`.
- `apps/hmd_button_test/main.cpp` — PRESENT, contains `Send Press` + `Send Release` + `Tap` literal strings + 3 call sites each of `driverClient_->press()` and `driverClient_->release()`; FREE of `dashboard_manager|IDashboardManager|DashboardState|performDashboardAction|HMDButtonAction|driverClient_->click(`.
- `src/steamvr/src/dashboard_manager.cpp` — ABSENT (confirmed via `! test -f`).
- `src/steamvr/include/micmap/steamvr/dashboard_manager.hpp` — ABSENT.
- `src/steamvr/CMakeLists.txt` — contains `src/vr_input.cpp` as the only source; does NOT contain `dashboard_manager.cpp`.
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` — does NOT contain `DashboardState`, `HMDButtonAction`, `getDashboardState`, `sendDashboardSelect`, `sendHMDButtonEvent`, `performDashboardAction`; KEEPT `IDriverClient::press()` + `IDriverClient::release()`, `VREventType` enum (for Quit), `IVRInput` minimal surface.
- `src/steamvr/src/vr_input.cpp` — `StubVRInput` + `OpenVRInput` compile without the removed methods; `OpenVRInput::processVREvent` only handles `VREvent_Quit`; no `vrOverlay_` / `IsDashboardVisible()` / `ShowDashboard()`.

Grep gates:
- SVR-07 forbidden-string regex across `driver/src src/ apps/` → **0 hits**.
- `grep -c 'driverClient->press()' apps/micmap/main.cpp` → **1**.
- `grep -c 'driverClient->release()' apps/micmap/main.cpp` → **1**.

Commits:
- `9545811` — FOUND in `git log --oneline` (pre-existing, Task 1).
- `d416d62` — FOUND in `git log --oneline` (Task 2).
- `fd7cd91` — FOUND in `git log --oneline` (Task 3).

Build:
- `cmake --build build --config Debug --target micmap_steamvr micmap_core micmap hmd_button_test mic_test test_command_queue test_placeholder` → success.
- `ctest --test-dir build --output-on-failure -C Debug` → 3/3 passed.

Binary:
- `build/bin/Debug/hmd_button_test.exe` → PRESENT (1.3 MB, ready for Plan 01-05 on-HMD spike).

---
*Phase: 01-driver-sidecar-migration*
*Completed: 2026-04-23*

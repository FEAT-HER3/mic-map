# Phase 1: Driver Sidecar Migration - Context

**Gathered:** 2026-04-22
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the virtual-controller driver with a pure sidecar that creates its own `/input/system/click` boolean component on the HMD property container. Detection trigger flows: app state machine → HTTP request → driver `CommandQueue` → `RunFrame` → `UpdateBooleanComponent`. No virtual controller, no laser beam, no dashboard-state branching, no "open vs select" split.

In scope: driver rewrite, HTTP endpoint redesign, app-side state machine adjustment for press/release semantics, deletion of virtual-controller and dashboard-manager surface, validation via `hmd_button_test.exe` on real HMD.

Out of scope (other phases or future milestones): config read-back (Phase 2), `app.vrmanifest` / auto-launch (Phase 3), installer (Phase 4), README/architecture docs (Phase 5), overlay UI stubs in `dashboard_manager.cpp` (deferred milestone).

</domain>

<decisions>
## Implementation Decisions

### Reactivation strategy

- **D-01:** Hybrid reactivation — driver subscribes to `VREvent_TrackedDeviceDeactivated` for device index 0; on receipt, invalidates the cached `/input/system/click` handle (sets to `k_ulInvalidInputComponentHandle`) and clears the "creation attempted" latch. Each `RunFrame` re-checks `IVRProperties::TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` while the handle is invalid and re-creates the component when the container becomes valid again. Defense-in-depth: survives both reliable event delivery and silent device-index churn.
- **D-02:** Half-day validation spike with `hmd_button_test.exe` on a real HMD before declaring Phase 1 exit. Goal: characterize Case D (sleep/wake reactivation) behavior — verify event arrives, verify re-creation succeeds, verify no handle leak across N cycles. If `VREvent_TrackedDeviceDeactivated` proves unreliable on the test hardware, the polling fallback already covers the gap (D-01 is correct by construction either way).

### Press model

- **D-03:** Press/release pair tracking detection state — the SteamVR system button is held down while the noise pattern is detected and released when detection ends. NOT a fixed 100ms tap. This supersedes the implied "scheduled timer release" reading of SVR-06: SVR-06 still holds (driver owns release timing) but the driver releases on app-signaled release-edge OR min-hold expiry, whichever is later, not on a fixed timer.
- **D-04:** Minimum hold = **100ms** (matches existing `virtual_controller` default). Brief detection blips still register as a tap.
- **D-05:** Min-hold timer lives **driver-side**. Driver records `press_timestamp` on the down command; if an `up` command arrives before `press_timestamp + 100ms`, the driver defers the `UpdateBooleanComponent(false)` call in `RunFrame` until the floor is met. Robust to app-side latency or a crashed app sending `down` then never sending `up` (see also: max-hold safety release — Claude's discretion).

### HTTP API

- **D-06:** Single endpoint: **`POST /button`** with JSON body `{"state": "down" | "up"}`. Driver enqueues a `PressCommand{state}` onto `CommandQueue`; `RunFrame` drains and applies via `UpdateBooleanComponent`. App-side `IDriverClient::click(action, durationMs)` is replaced with `IDriverClient::setButton(state)` (or equivalent two-method `press()`/`release()` shape — Claude's discretion at planning time).
- **D-07:** No back-compat for the old `/click` endpoint or the old `click(action, durationMs)` API. Per SVR-07's "no feature flag" stance and SVR-09's "single click endpoint" requirement, the old surface is removed in the same PR that lands the new one. `hmd_button_test.exe` migrates atomically.

### Driver init failure UX

- **D-08:** When the HMD container never becomes available (headless SteamVR / Null driver / pre-HMD-attach): forever-poll silently. Log on transitions only — one `DriverLog` line on first `RunFrame` ("awaiting HMD container"), one line when the container appears and the click component is created. No periodic polling spam in `vrserver.txt`.
- **D-09:** Press commands arriving while `click_component_handle` is invalid are dropped with a single warning log line per drop. Aligns with the `CommandQueue` drop-oldest policy (SVR-05) — no buffering of stale presses to avoid surprise activations when the user puts the headset on minutes later.

### App-side state machine

- **D-10:** Release edge fires after detection confidence drops below threshold for **`min_release_ms` (default ~80ms)**. Symmetric with the entry guard. Prevents chatter from confidence flicker mid-cover.
- **D-11:** Cooldown semantics shift to **post-release only** (default ~200ms). After the app sends `up`, the state machine blocks any new `down` for the cooldown window. Prevents accidental double-trigger on quick uncover/re-cover. The existing `cooldown_ms` config field is reused — pre-press cooldown is removed.
- **D-12:** Implied state-machine shape: `Idle → Detecting (confidence > threshold) → Triggered (sustained min_press_ms; emit DOWN) → Releasing (confidence < threshold) → Cooldown (sustained min_release_ms; emit UP) → Idle (after cooldown_ms)`. Exact transition guards and field names are Claude's discretion at planning time, as long as the semantics above hold.

### Test harness

- **D-13:** Use `hmd_button_test.exe` as-is for the reactivation spike — manual press triggering with operator placing/removing the headset between presses. Existing test app already has the manual-press path needed; no extension required.

### File deletions

- **D-14:** Per SVR-07 + SVR-08: delete `src/steamvr/virtual_controller.{hpp,cpp}`, `src/steamvr/process_launcher.{hpp,cpp}`, `driver/src/virtual_controller.{hpp,cpp}`, `driver/src/process_launcher.{hpp,cpp}`, `micmap_controller_profile.json`, AND `src/steamvr/src/dashboard_manager.cpp` + `src/steamvr/include/.../dashboard_manager.hpp` (entire `IDashboardManager` surface — no deprecated stubs, no no-op pass-through). Callsites in `apps/micmap/main.cpp` migrate to call `IDriverClient::setButton()` directly.

### Claude's Discretion

- Exact `CommandQueue` API surface (push/pop signatures, blocking vs try-pop semantics).
- Whether `IDriverClient` exposes `setButton(state)` vs split `press()`/`release()` methods.
- Max-hold safety release in driver (e.g. force-release if button held > 5s with no up event arriving — defensive against app crash mid-press).
- Exact cooldown_ms / min_press_ms / min_release_ms default values within the ~80–200ms range; tune during validation.
- Logging verbosity beyond the mandated init line + per-error logging (SVR-10).
- Spike protocol details (cycle count, timing, what counts as pass/fail) for the Phase 1 exit reactivation validation.
- Whether `dashboard_manager.cpp` removal lands as a separate atomic commit before/after the driver rewrite, or in the same commit.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Sidecar-on-HMD technique (validated prior art)

- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — full writeup of the sidecar technique. Cross-driver `UpdateBooleanComponent` is blocked (`VRInputError_WrongType`); cross-driver `CreateBooleanComponent` on a duplicate path on the HMD container IS allowed and SteamVR propagates updates. Validated SteamVR March 2026 + OpenVR SDK v2.5.1 + Windows 11.
- `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — Inno Setup reference (relevant to Phase 4, listed for cross-phase awareness).
- `D:\Documents\Projects\bey-closer-t1\.planning\todos\pending\2026-03-26-explore-input-system-click-handle-probing-on-hmd.md` — the originating todo this milestone fulfills.

### Project requirements & roadmap

- `.planning/PROJECT.md` — milestone vision, key decisions, in/out of scope.
- `.planning/REQUIREMENTS.md` §SVR (SVR-01 through SVR-11) — falsifiable Phase 1 requirements.
- `.planning/ROADMAP.md` §"Phase 1: Driver Sidecar Migration" — goal, dependencies, success criteria, research-spike note.
- `.planning/research/SUMMARY.md` §"Phase 1: Driver Sidecar Rewrite" + §"Critical Pitfalls" — recommended architecture, OpenVR SDK 2.15.6 interface notes, Pitfalls 1/6/7/11/12.

### Codebase intel

- `.planning/codebase/ARCHITECTURE.md` — current two-process model, layered libraries, current state machine shape (Idle / Training / Detecting / Triggered / Cooldown).
- `.planning/codebase/STRUCTURE.md` — directory layout for `src/steamvr/`, `driver/src/`, `apps/`.
- `.planning/codebase/CONVENTIONS.md` — C++17 style, factory pattern, interface-based abstractions.
- `.planning/codebase/CONCERNS.md` — known concerns, including overlay stubs and config read-back stub.
- `.planning/codebase/TESTING.md` — `mic_test.exe` and `hmd_button_test.exe` test harness conventions.

### OpenVR API surface (research-summarized; read SDK headers directly when planning)

- `IVRDriverInput_004::CreateBooleanComponent`, `UpdateBooleanComponent` — driver-side input injection.
- `IVRProperties::TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` — HMD container handle, may be invalid until HMD activates.
- `IVRServerDriverHost::PollNextEvent` — driver-side event pump for `VREvent_TrackedDeviceDeactivated`.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`driver/src/http_server.{cpp,hpp}`** — cpp-httplib-based HTTP server already running in driver. Add `POST /button` handler here; replace existing endpoint(s).
- **`driver/src/driver_log.hpp`** — `DriverLog` wrapper available; SVR-10 wires it into init + error paths.
- **`driver/src/device_provider.{cpp,hpp}`** — current device provider; this is where the HMD-container state machine (NotReady → Ready → Invalidated), the `CreateBooleanComponent` call, and the `RunFrame` loop live. Will absorb the scheduled-release timing logic from the deleted `virtual_controller`.
- **`src/steamvr/src/driver_client.cpp`** + interface — current HTTP client. Replace `click(action, durationMs)` with `setButton(state)` (or equivalent); strip "open vs select" branching.
- **`src/core/src/state_machine.cpp`** — current `IStateMachine` (Idle/Training/Detecting/Triggered/Cooldown). Update to emit DOWN on Triggered entry and UP on a new Releasing→Cooldown edge with `min_release_ms` debounce.
- **`apps/hmd_button_test/main.cpp`** — exit-criterion harness. Use as-is for the spike per D-13; only update its outgoing HTTP call to match the new `/button` endpoint.

### Established Patterns

- **Factory + interface pattern** (`createOpenVRInput`, `createDriverClient`, `createStateMachine`, `createDashboardManager`) — keep for `IDriverClient`; remove for `IDashboardManager` along with the surface.
- **`unique_ptr` ownership, no raw `new`** — established in `src/common`, `src/audio`, `src/detection`. New `CommandQueue` should follow the same shape (factory or direct construction; mutex-guarded internal state).
- **Logging via `common::Logger` (app side) and `DriverLog` (driver side)** — separate log sinks; do not leak app `Logger` into driver code.

### Integration Points

- App `apps/micmap/main.cpp` `onTrigger()` callback currently calls `dashboardManager->performDashboardAction()`. This becomes a direct `driverClient->setButton(ButtonState::Down)` (and a corresponding Down→Up emission from the state machine on the release edge).
- Driver `device_provider::RunFrame` becomes the single point where: (a) HMD container is polled until ready, (b) `CommandQueue` is drained, (c) min-hold timer is checked for deferred release, (d) the deactivation event is consumed.
- HTTP wire format change crosses the app/driver boundary atomically — both ends ship in the same PR per D-07.

</code_context>

<specifics>
## Specific Ideas

- The user explicitly steered the press model away from the "fixed 100ms tap" implied by SUMMARY.md and SVR-06's wording, toward a **dynamic press that mirrors detection duration** — "press-hold duration should be dynamic according to how long the user covers the microphone / the noise pattern is detected." This is the most consequential decision in this discussion: it reshapes the HTTP API, the driver's responsibility, and the app-side state machine.
- The minimum-hold floor (D-04, 100ms) is the safety net for the "brief blip" case so a quick tap-and-uncover still opens the dashboard.
- Symmetric entry/exit guards (D-10): the same shape that prevents false-trigger on entry should prevent false-release on exit.

</specifics>

<deferred>
## Deferred Ideas

- **Auto-start toggle in MicMap UI (UX-01)** — already in REQUIREMENTS.md v2 list; not Phase 1.
- **In-VR settings overlay (UX-02 / dashboard_manager overlay stubs)** — already deferred per PROJECT.md; deletion of `dashboard_manager.cpp` per D-14 implies the overlay stub work also exits Phase 1's surface entirely. When the overlay milestone arrives, it will start from scratch rather than reviving the deleted stubs.
- **Per-mic detection presets (DET-02)** — out of milestone.
- **`/health` HTTP endpoint exposing driver readiness** — considered as an option for D-08; rejected for Phase 1 (silent forever-poll wins on simplicity). Re-surface in a future "driver observability" phase if the manual debugging cost becomes real.
- **Max-hold safety release in driver** — noted under Claude's Discretion but worth elevating to a deferred backlog item if the planner does not include it: an app crash mid-press would leave the SteamVR system button stuck down indefinitely. Low probability, high blast radius.

</deferred>

---

*Phase: 01-driver-sidecar-migration*
*Context gathered: 2026-04-22*

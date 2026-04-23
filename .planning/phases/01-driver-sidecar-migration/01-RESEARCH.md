# Phase 1: Driver Sidecar Migration - Research

**Researched:** 2026-04-23
**Domain:** OpenVR sidecar-driver rewrite + cross-thread HTTP→RunFrame plumbing + app-side press/release state machine
**Confidence:** HIGH (API signatures verified against local SDK headers; callsites grep-verified; pitfalls quoted from project research archive)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Hybrid reactivation — driver subscribes to `VREvent_TrackedDeviceDeactivated` for device index 0; on receipt, invalidates the cached `/input/system/click` handle (sets to `k_ulInvalidInputComponentHandle`) and clears the "creation attempted" latch. Each `RunFrame` re-checks `IVRProperties::TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` while the handle is invalid and re-creates the component when the container becomes valid again. Defense-in-depth: survives both reliable event delivery and silent device-index churn.
- **D-02:** Half-day validation spike with `hmd_button_test.exe` on a real HMD before declaring Phase 1 exit. If `VREvent_TrackedDeviceDeactivated` proves unreliable, the polling fallback already covers the gap.
- **D-03:** Press/release pair mirroring detection state — NOT a fixed 100ms tap. Driver releases on app-signaled release-edge OR min-hold expiry, whichever is later.
- **D-04:** Minimum hold = **100ms** (matches existing `virtual_controller` default).
- **D-05:** Min-hold timer lives **driver-side**. Driver records `press_timestamp` on down command; if `up` arrives early, driver defers `UpdateBooleanComponent(false)` in `RunFrame` until floor is met.
- **D-06:** Single endpoint: **`POST /button`** with JSON body `{"state": "down" | "up"}`. Enqueues a `PressCommand{state}` onto `CommandQueue`; `RunFrame` drains and applies via `UpdateBooleanComponent`. App-side `IDriverClient::click(action, durationMs)` is replaced with `IDriverClient::setButton(state)` (or equivalent two-method `press()`/`release()` — Claude's discretion).
- **D-07:** No back-compat for `/click` endpoint or old `click(action, durationMs)` API. Old surface removed in same PR. `hmd_button_test.exe` migrates atomically.
- **D-08:** When HMD container never becomes available: forever-poll silently. Log on transitions only — one `DriverLog` line on first `RunFrame` ("awaiting HMD container"), one line when container appears and click component created.
- **D-09:** Press commands arriving while `click_component_handle` is invalid are dropped with single warning log per drop.
- **D-10:** Release edge fires after detection confidence drops below threshold for **`min_release_ms` (default ~80ms)**.
- **D-11:** Cooldown semantics shift to **post-release only** (default ~200ms). Existing `cooldown_ms` config field is reused; pre-press cooldown removed.
- **D-12:** Shape: `Idle → Detecting → Triggered (emit DOWN) → Releasing → Cooldown (emit UP) → Idle`.
- **D-13:** Use `hmd_button_test.exe` as-is for the reactivation spike — manual press triggering, operator places/removes headset between presses.
- **D-14:** Full deletions: `src/steamvr/virtual_controller.{hpp,cpp}` (do not exist — see finding below), `src/steamvr/process_launcher.{hpp,cpp}` (do not exist), `driver/src/virtual_controller.{hpp,cpp}`, `driver/src/process_launcher.{hpp,cpp}`, `micmap_controller_profile.json` and its `vrcompositor_bindings_micmap_controller.json` sibling, `src/steamvr/src/dashboard_manager.cpp` + `src/steamvr/include/micmap/steamvr/dashboard_manager.hpp`.

### Claude's Discretion

- Exact `CommandQueue` API surface (push/pop signatures, blocking vs try-pop semantics).
- Whether `IDriverClient` exposes `setButton(state)` vs split `press()`/`release()` methods.
- Max-hold safety release in driver (e.g. force-release if button held > 5s with no up event — defensive against app crash mid-press).
- Exact cooldown_ms / min_press_ms / min_release_ms default values within ~80–200ms range; tune during validation.
- Logging verbosity beyond mandated init line + per-error logging.
- Spike protocol details (cycle count, timing, pass/fail) for reactivation validation.
- Whether `dashboard_manager.cpp` removal lands as separate atomic commit before/after driver rewrite, or in same commit.

### Deferred Ideas (OUT OF SCOPE)

- Auto-start toggle in MicMap UI (UX-01) — Phase 3+, not Phase 1.
- In-VR settings overlay (UX-02 / dashboard_manager overlay stubs) — a future overlay milestone, starting fresh rather than reviving the deleted stubs.
- Per-mic detection presets (DET-02) — out of milestone.
- `/health` HTTP endpoint exposing driver readiness — rejected for Phase 1 (silent forever-poll wins on simplicity). *Note: existing `/health` GET handler in `http_server.cpp:249-251` is unrelated — planner's call whether to keep or remove.*
- Max-hold safety release in driver — explicitly noted as a candidate deferred item if planner drops it from SVR-06.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SVR-01 | Driver starts with zero registered devices — no `TrackedDeviceAdded` in `device_provider` | Delete lines `device_provider.cpp:45-51` wholesale; `DeviceProvider::Init` becomes: init context → start HTTP server → mark initialized. No `TrackedDeviceAdded`, no controller owned. |
| SVR-02 | Defer HMD-container component creation until `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` returns a valid container — polled each `RunFrame`, not at `Init` | HMD Button Stub §2 pattern: `m_hSystemClick = k_ulInvalidInputComponentHandle`; loop in `RunFrame` polls the container, creates component once valid. See §API Reference below. |
| SVR-03 | Once HMD container available, driver creates its own `/input/system/click` boolean component via `IVRDriverInput` (interface version shipped with SDK — see §API Reference) on the HMD container and retains returned handle | Signature confirmed `[VERIFIED: openvr_driver.h:3710]`. Path string is `"/input/system/click"` exactly — no device-specific prefix. |
| SVR-04 | Subscribe to `VREvent_TrackedDeviceDeactivated` for device index 0; invalidate cached handle; next `RunFrame` re-creates against re-activated HMD container | Event code = `101` `[VERIFIED: openvr_driver.h:778]`. Device index on event = `pEvent.trackedDeviceIndex` `[VERIFIED: openvr_driver.h:1387]`. `PollNextEvent(VREvent_t*, uint32_t)` via `VRServerDriverHost()` `[VERIFIED: openvr_driver.h:3789]`. |
| SVR-05 | Thread-safe `CommandQueue` (mutex-guarded bounded deque, depth 8, drop-oldest) — HTTP thread pushes, `RunFrame` drains. No OpenVR driver API ever called from HTTP thread. | Existing codebase convention: `std::deque` + `std::mutex` + `std::lock_guard`, matching `virtual_controller.cpp:357` `pendingReleases_` pattern. See §CommandQueue Design. |
| SVR-06 | Scheduled button-release timing lives in driver's `RunFrame` loop | Existing deferred-release pattern `virtual_controller.cpp:348-378` is already drivable; port the `steady_clock::time_point` + min-hold comparison directly into `device_provider::RunFrame`. |
| SVR-07 | Delete `virtual_controller.{hpp,cpp}`, `process_launcher.{hpp,cpp}`, `micmap_controller_profile.json` — no feature flag, `-Werror`/`/WX` clean | See §Deletion Blast-Radius Map for grep-verified callsite inventory. `/WX` not currently enabled — planner must add. |
| SVR-08 | Single trigger code path — remove `dashboard_manager` polling + "open vs. select" branching; every trigger issues same `/input/system/click` press | `dashboard_manager.cpp` fully deleted (D-14); `apps/micmap/main.cpp:341-378` `onTrigger()` collapses to one `setButton(Down)`. See §State-Machine Changes. |
| SVR-09 | App-side `driver_client` collapses to single "click" endpoint | `IDriverClient` per `vr_input.hpp:201-261` today exposes `click/press/release`. Collapse to `setButton(ButtonState)` or keep `press()`/`release()` as the two edges. |
| SVR-10 | `DriverLog` — first `RunFrame` emits init line (driver version, build timestamp); every OpenVR error logged with its enum name | `DriverLog` / `SafeDriverLog` wrapper already exists at `driver/src/driver_log.hpp:24`. Needs: (a) init line, (b) `VRInputError*→string` helper. See §Logging Requirements. |
| SVR-11 | End-to-end: `hmd_button_test.exe` triggers dashboard on real HMD, no laser beam, second trigger after HMD sleep/wake works | Manual validation only — human with HMD. See §Validation Architecture. |
</phase_requirements>

## Summary

Phase 1 deletes the MicMap virtual-controller stack (driver device + app-side dashboard manager + dashboard-state branching) and replaces it with a pure sidecar driver that creates its own `/input/system/click` boolean component on the HMD's property container. The sidecar-on-HMD technique is validated prior art in `bey-closer-t1` (HMD Button Stub.md); this phase ports it with two extensions beyond what the sibling project proved: (a) a reactivation lifecycle that survives HMD sleep/wake via `VREvent_TrackedDeviceDeactivated`, and (b) a press/release pair model (not a fixed-duration tap) where duration mirrors detection.

All OpenVR API surface used by this phase is confirmed present in the project's vendored OpenVR SDK. The HTTP→RunFrame handoff uses a mutex-guarded bounded deque — the same concurrency pattern already in `virtual_controller.cpp`'s `pendingReleases_`, which is the correct reference for the CommandQueue primitive. No new dependencies.

**Primary recommendation:** Land the driver rewrite and app-side collapse in a single atomic PR (per D-07, D-14, SVR-07/08/09). The old surface — `TrackedDeviceAdded`, `IDashboardManager`, `DashboardState`, "open vs. select" branching — must all disappear in the same commit sequence that introduces the new endpoint. Leaving a transitional state where both coexist is the class of brownfield bug Pitfall 7 exists to prevent.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Detect noise pattern, produce confidence score | App (audio + detection libs) | — | Already in place; unchanged this phase |
| Translate confidence to press/release edges (min-press, min-release, cooldown) | App (state machine, `src/core`) | — | Per D-10/D-11/D-12, app-side state machine emits DOWN/UP edges; driver does NOT implement these debounces |
| Transport edges to driver | App (`IDriverClient` via HTTP) → Driver (`HttpServer`) | — | Existing cpp-httplib path; only endpoint shape changes |
| Enforce min-hold floor on DOWN (100ms) | Driver (`device_provider::RunFrame` + `CommandQueue`) | — | Per D-05, floor is driver-side to survive app crash mid-press |
| Create + update `/input/system/click` on HMD | Driver (`device_provider`) | — | Only the driver links OpenVR SDK with driver privileges; cross-driver update blocked (HMD Button Stub §Key Facts) |
| Handle HMD reactivation lifecycle | Driver (`device_provider::RunFrame`, event pump) | — | `PollNextEvent` is driver-side only |
| Own the test harness for exit criterion | App (`apps/hmd_button_test`) | — | Existing binary; just repoint its HTTP calls to `POST /button` per D-13 |

## Standard Stack

### Core (locked — no version changes this phase)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| OpenVR SDK | interface version shipped in project's SDK (see note) | Driver-side `CreateBooleanComponent` / `UpdateBooleanComponent` / `PollNextEvent` / `TrackedDeviceToPropertyContainer` | Only SteamVR driver ABI; no alternatives |
| cpp-httplib | v0.14.3 `[VERIFIED: external/CMakeLists.txt:66]` | HTTP server (driver) + client (app) | Already vendored; single-header |
| nlohmann/json | v3.11.2 `[VERIFIED: external/CMakeLists.txt:9]` | JSON body parse for `POST /button` | Already vendored; Phase 2 will use it more heavily |

**OpenVR interface version note:** The vendored SDK header at `D:/Documents/Projects/bey-closer-t1/extern/openvr/headers/openvr_driver.h` (current SDK reference reachable from this project's toolchain) defines `IVRDriverInput_Version = "IVRDriverInput_003"` `[VERIFIED: openvr_driver.h:3732]`. The project's SUMMARY.md refers to `_004`. HMD Button Stub.md states "IVRDriverInput version doesn't matter. Both `_003` and `_004` share the same handle space and behave identically for Create/Update operations." `[CITED: bey-closer-t1/HMD Button Stub.md]`. **Action for planner:** Use the macro `IVRDriverInput_Version` (not a hard-coded literal) when requesting the interface, so whichever version ships with whatever SDK the build links against works transparently. The driver already includes `<openvr_driver.h>` and calls `vr::VRDriverInput()` (the helper that resolves to whatever version is current), so this just works by default `[VERIFIED: driver/src/virtual_controller.cpp:12,208]`.

### Supporting (existing — already in use)
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `std::deque<T>` + `std::mutex` + `std::lock_guard` | C++17 stdlib | `CommandQueue` backing | Per §CommandQueue Design |
| `std::chrono::steady_clock` | C++17 stdlib | `press_timestamp`, min-hold deferral | Matches existing `pendingReleases_` pattern |
| `std::atomic<bool>` | C++17 stdlib | Handle-valid flag readable from HTTP thread without locking | Allows HTTP thread to `SHORT-CIRCUIT 503` without taking the CommandQueue lock |

**Installation:** No-op — all already vendored via FetchContent.

**Version verification:** All three primary libraries are version-pinned in `external/CMakeLists.txt`; no network lookup required since vendored via FetchContent_Declare with explicit GIT_TAG values.

## Architecture Patterns

### System Architecture Diagram

```
┌────────────────── micmap.exe (app process) ──────────────────┐
│                                                                │
│  WASAPI audio ─► detector ─► IStateMachine                    │
│                                    │                           │
│                                    │ (edges)                   │
│                                    ▼                           │
│                              onTrigger(edge)                   │
│                                    │                           │
│                                    │ edge == DOWN / UP         │
│                                    ▼                           │
│                          IDriverClient::setButton              │
│                                    │                           │
└────────────────────────────────────┼───────────────────────────┘
                                     │ HTTP POST /button
                                     │ {"state":"down"|"up"}
                                     ▼
┌────────────────── driver_micmap.dll (vrserver) ──────────────┐
│                                                                │
│   HTTP thread (cpp-httplib)                                    │
│        │ parse JSON (valid?)                                   │
│        │ yes ─► CommandQueue.push(PressCommand)                │
│        │ no  ─► 400, nothing enqueued                          │
│                                                                │
│   ─ ─ ─ ─ ─ ─ thread boundary (mutex) ─ ─ ─ ─ ─ ─             │
│                                                                │
│   RunFrame (vrserver main pump, ~90-120Hz)                     │
│        1. Drain OpenVR events (PollNextEvent loop)            │
│           └─► VREvent_TrackedDeviceDeactivated(idx=0)         │
│                 → invalidate m_hSystemClick                   │
│        2. If m_hSystemClick invalid:                          │
│           └─► TrackedDeviceToPropertyContainer(HMD)           │
│                 → if valid: CreateBooleanComponent            │
│        3. Drain CommandQueue:                                 │
│           DOWN → record press_timestamp; Update(..true)       │
│           UP   → if (now - press_ts) >= min_hold:             │
│                     Update(..false)                           │
│                  else: defer until next RunFrame              │
│        4. (last) tick deferred-UP; Update(..false) if ready  │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Recommended Project Structure (delta only)

```
driver/src/
├── device_provider.{hpp,cpp}   # REWRITTEN — owns HMD-container state machine,
│                                 event pump, CommandQueue drain, min-hold timer.
│                                 No more TrackedDeviceAdded. No more controller pointer.
├── http_server.{hpp,cpp}        # MODIFIED — single POST /button handler; enqueues
│                                 to CommandQueue; removes /click /press /release.
│                                 Decouple from VirtualController* ctor arg → take
│                                 DeviceProvider* or CommandQueue&.
├── command_queue.{hpp,cpp}      # NEW — bounded deque, depth 8, drop-oldest.
├── driver_log.hpp               # UNCHANGED (already suitable).
├── driver_main.cpp              # UNCHANGED (HmdDriverFactory stays).
├── virtual_controller.{hpp,cpp} # DELETED
├── process_launcher.{hpp,cpp}   # DELETED
└── vr_error.hpp                 # NEW — VRInputError_ → const char* helper for SVR-10.

driver/resources/
├── driver.vrresources           # UNCHANGED
├── settings/default.vrsettings  # MAYBE UNCHANGED (driver_micmap.autoLaunchApp
│                                 setting becomes a no-op — planner's call to strip).
└── input/
    ├── micmap_controller_profile.json                     # DELETED
    └── vrcompositor_bindings_micmap_controller.json       # DELETED

src/steamvr/
├── include/micmap/steamvr/
│   ├── vr_input.hpp             # MODIFIED — IDriverClient API collapsed
│   │                              (setButton(state) or keep press()/release()).
│   └── dashboard_manager.hpp    # DELETED
└── src/
    ├── vr_input.cpp             # MODIFIED — DriverClient impl points at POST /button;
    │                              strip click("trigger"/"a"/...) variants.
    └── dashboard_manager.cpp    # DELETED

src/core/
└── src/state_machine.cpp        # MODIFIED — add Releasing state; emit DOWN on
                                   Triggered entry, UP on Cooldown entry; reshape
                                   cooldown to post-release (D-11).

apps/
├── micmap/main.cpp              # MODIFIED — drop dashboardManager; onTrigger()
│                                  routes press/release directly to driverClient.
└── hmd_button_test/main.cpp     # MODIFIED — Test Driver/Open Dashboard/Send Click
                                   buttons updated to use POST /button; strip
                                   dashboard-state dependencies.
```

### Pattern 1: HMD-Container State Machine

**What:** Per HMD Button Stub §2, extended for reactivation per D-01 / Pitfall 1.
**When to use:** Every `RunFrame` tick.
**Shape:**

```cpp
enum class HmdComponentState {
    NotReady,     // Waiting for HMD container to exist
    Ready,        // m_hSystemClick is valid; updates succeed
    Invalidated   // Deactivation event received; need to re-create
};
// m_state = NotReady at driver Init time.

// In RunFrame:
// 1) Drain OpenVR events first — this may flip state to Invalidated.
for (vr::VREvent_t ev; vr::VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev)); ) {
    if (ev.eventType == vr::VREvent_TrackedDeviceDeactivated
        && ev.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) {
        m_hSystemClick = vr::k_ulInvalidInputComponentHandle;
        m_state = HmdComponentState::Invalidated;
        DriverLog("HMD deactivated; invalidated /input/system/click handle\n");
    }
}

// 2) If not Ready, try to (re)create. Log on transitions only (D-08).
if (m_state != HmdComponentState::Ready) {
    auto hmd = vr::VRProperties()->TrackedDeviceToPropertyContainer(
        vr::k_unTrackedDeviceIndex_Hmd);
    if (hmd != vr::k_ulInvalidPropertyContainer) {
        auto err = vr::VRDriverInput()->CreateBooleanComponent(
            hmd, "/input/system/click", &m_hSystemClick);
        if (err == vr::VRInputError_None) {
            DriverLog("MicMap: /input/system/click created (handle=%llu)\n", m_hSystemClick);
            m_state = HmdComponentState::Ready;
        } else {
            DriverLog("MicMap: CreateBooleanComponent failed: %s\n", VRInputErrorName(err));
            m_hSystemClick = vr::k_ulInvalidInputComponentHandle;
            // Stay in current state; will retry next frame.
        }
    } else if (m_loggedAwaitingHmd == false) {
        DriverLog("MicMap: awaiting HMD container\n");
        m_loggedAwaitingHmd = true;  // Only log once, per D-08.
    }
}
```

Source: HMD Button Stub §2, extended `[CITED: bey-closer-t1/HMD Button Stub.md]`.

### Pattern 2: Command Enqueue / Drain

**What:** HTTP thread is producer; RunFrame is consumer; mutex-guarded deque.
**When to use:** Every `POST /button` and every `RunFrame` tick.
**See §CommandQueue Design below for the implementation recipe.**

### Pattern 3: Min-Hold Deferred Release

**What:** Driver owns the 100ms floor. When DOWN arrives, stamp `press_timestamp = now`. When UP arrives, compute `wait_until = press_timestamp + 100ms`; if `now >= wait_until`, update immediately; else queue a one-shot "pending_release_at = wait_until" and emit the UP in a subsequent `RunFrame` when the deadline passes.

**Source pattern:** `driver/src/virtual_controller.cpp:256-261, 355-378` already does exactly this for click-with-duration — copy the mechanics, change the trigger from "duration parameter" to "up-command arrival".

### Anti-Patterns to Avoid

- **Calling `VRDriverInput()->UpdateBooleanComponent` from the HTTP thread.** Pitfall 12. Always enqueue onto `CommandQueue` and let `RunFrame` do the update.
- **Treating `m_bComponentAttempted` as permanent.** Pitfall 1 — replace with the three-state enum above. The one-shot latch in HMD Button Stub.md is correct for cold-start only; it does not survive deactivation.
- **Calling `UpdateBooleanComponent` on every `RunFrame` tick regardless of state change.** Pitfall-adjacent: "log spam, minor CPU, possible SteamVR-side debouncing weirdness" `[CITED: PITFALLS.md §Performance Traps]`. Track `last_written_value`; only call Update on transitions.
- **Holding the CommandQueue mutex while calling `UpdateBooleanComponent`.** Pop first into a local, release the lock, then update. Pop should be cheap and O(1).
- **Parsing JSON on the RunFrame thread.** Parse on the HTTP thread; enqueue a typed `PressCommand` struct (already decoded). RunFrame never sees strings.
- **Gating the HTTP endpoint on `controller_->IsActive()` like the current code does.** The controller is going away. The new 503 condition is "CommandQueue push succeeded" (always, with drop-oldest) — not "is handle ready". Per D-09, presses arriving while handle is invalid are dropped at the *drain* site, not at HTTP accept. HTTP returns 200 if JSON parsed; the drain logs the drop.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Enum-name-to-string for `EVRInputError` | A stringifier based on reading the SDK changelog | A local `constexpr` switch that hard-codes the 21 values from `openvr_driver.h:1421-1441` | Small, closed enum; the switch is 25 lines and the enum doesn't change across minor SDK versions. Do NOT use a macro X-list trying to be clever — planner will spend more time making it compile than writing the switch. |
| JSON parsing | `strstr("\"down\"")` style string sniffing | `nlohmann::json::parse(body)` in a `try/catch (const json::parse_error&)` | nlohmann/json is already vendored. The three lines of parse code are faster to write and audit than hand-written extraction. |
| Bounded deque with drop-oldest | Lock-free SPSC ring buffer | `std::deque<PressCommand>` + `std::mutex` | Depth 8, 1 producer + 1 consumer — contention is negligible. SPSC ring is overkill and ships with its own bugs. Mirrors existing `pendingReleases_` pattern `[VERIFIED: virtual_controller.cpp:161-162]`. |
| Thread-safety assertion (no OpenVR from HTTP thread) | A thread-ID check at every call site | A single-line convention doc + `grep` in CI | The HTTP handlers are five lines each. Visual inspection and a `grep` verification (see §Validation) is faster and more reliable than runtime instrumentation. |
| HTTP port contention handling | New code | Existing code in `http_server.cpp:57-95` (port-range retry) | Already solved; leave it. |

**Key insight:** This phase has very few opportunities to invent things. The primitives — mutex-guarded deque, `steady_clock` timers, switch on enum, JSON parse — are all textbook C++17 and have first-class existing analogs in the codebase. The only *new* thing is the sidecar-on-HMD input injection, and that's validated and documented in HMD Button Stub.md.

## Runtime State Inventory

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | **None** — driver and app hold no persistent state beyond config.json (Phase 2) and training_data.bin (untouched). No SteamVR-side input bindings are keyed to the MicMap virtual-controller *at runtime in this project's files*. | No data migration in this phase |
| Live service config | **SteamVR user bindings for the old `MICMAP_CONTROLLER_001` virtual device** live in `%LOCALAPPDATA%\openvr\input\` (per SteamVR convention) and `steamvr.vrsettings`. These are outside the repo and not visible to grep. `[ASSUMED]` they exist on any machine that has run a prior version; confirmed as Pitfall 8. | Phase 4 (INST-06 upgrade cleanup) — **not this phase**. For Phase 1 validation on a dev box that previously ran the virtual controller, the operator may see a phantom "MICMAP_CONTROLLER_001" in SteamVR Settings > Devices until they "Forget" it manually. Flag this to the validation operator so they don't mistake it for Phase 1 regression. |
| OS-registered state | **None.** No Windows Task Scheduler, no Run key, no services, no scheduled tasks. The process_launcher in the current driver uses `CreateProcessA` at driver load — which goes away in this phase; no OS registration to clean. | None |
| Secrets / env vars | **None.** HTTP is localhost-unauthenticated; no secrets anywhere. `OPENVR_SDK_PATH` env var is a build-time concern only. | None |
| Build artifacts | `build/driver/micmap/` output tree contains the prior-version DLL + `micmap_controller_profile.json` + `vrcompositor_bindings_micmap_controller.json`. A fresh `cmake --build` after this phase will replace the DLL; the two JSON files are copied by the post-build custom command at `driver/CMakeLists.txt:115-119`. **Remove the `add_custom_command` lines when deleting the JSON source files**, otherwise CMake will error on the next clean build. | Edit `driver/CMakeLists.txt` per §Deletion Blast-Radius |

**Nothing found in category "Stored data" or "OS-registered state":** Verified by grep of repo + read of `driver/src/driver_main.cpp` + `device_provider.cpp` + launch/install scripts. No runtime state escapes the process or the config file.

## Common Pitfalls

> These quote (or near-quote) the project's internal `research/PITFALLS.md` and are the planner's blocking constraints. Each ends with the specific acceptance_criterion the planner MUST surface on the relevant task.

### Pitfall 1: HMD container handle stale after deactivation / reactivation

**What goes wrong (quoted):** "The driver successfully creates `/input/system/click`... Then the user puts the headset down, lighthouse deactivates the HMD, the user puts it back on, lighthouse reactivates the HMD — and now `UpdateBooleanComponent` on the cached handle silently no-ops, returns `VRInputError_InvalidHandle`, or worst case the HMD property container handle is now a different value and the old component is orphaned." `[CITED: PITFALLS.md §Pitfall 1]`

**How to avoid (quoted):**
> "Do NOT treat `m_bComponentAttempted` as permanent. Replace it with a state enum: `NotReady` -> `Ready` -> `Invalidated` -> `Ready` (re-create). Subscribe to `VREvent_TrackedDeviceDeactivated` for device index 0 (HMD) via `IVRServerDriverHost::PollNextEvent` in `RunFrame()`. On deactivate: reset `m_hSystemClick = k_ulInvalidInputComponentHandle` and set state back to `NotReady`. On next `RunFrame`, re-poll `TrackedDeviceToPropertyContainer` and re-run the `CreateBooleanComponent` sequence. Guard every `UpdateBooleanComponent` call: if return is anything except `VRInputError_None`, log the error code and flip state back to `NotReady`. Do NOT silently drop the trigger — surface it in a structured log." `[CITED: PITFALLS.md §Pitfall 1]`

**Planner must ensure the "driver HMD-container state machine" task includes acceptance criteria:**
- Three-state enum implemented (NotReady / Ready / Invalidated) — **not** a boolean latch.
- `PollNextEvent` loop called each RunFrame before the CommandQueue drain, so that a Deactivated event in the same frame invalidates before an update attempt.
- Every `UpdateBooleanComponent` return value is checked; on non-`VRInputError_None`, handle flipped to Invalidated + logged with enum name.
- Manual VR validation (the D-02 spike) has N ≥ 5 place/remove cycles with second-and-later triggers still working.

### Pitfall 6: driver.vrdrivermanifest missing or malformed

**What goes wrong (quoted):** "Driver DLL is present, `vrpathreg adddriver` succeeded, `driver.vrdrivermanifest` was forgotten from the Inno Setup `[Files]` section or has a trailing comma / wrong field name. vrserver enumerates the driver directory, fails to parse the manifest, and either: silently drops the driver, or loads it with a null name so the Settings > Startup > Add-ons panel shows a blank row." `[CITED: PITFALLS.md §Pitfall 6]`

**How to avoid (quoted):**
> "Verification test baked into the installer smoke script: after install, check `{app}\driver.vrdrivermanifest` exists and parses as JSON with `name`, `directory`, `resourceOnly`, `alwaysActivate` fields present. Lint the manifest at build time." `[CITED: PITFALLS.md §Pitfall 6]`

**Planner must ensure some Phase 1 task includes acceptance criterion:** After build, `driver.vrdrivermanifest` exists in the build output, parses as JSON, and has `alwaysActivate: true`. (Installer smoke-test is Phase 4; build-time check belongs here.) *Status of current manifest in tree is not verified here; planner reads and confirms as part of the task.*

### Pitfall 7: Virtual-controller removal leaves dangling code paths

**What goes wrong (quoted):** "The rip-out goes mostly clean... But `DeviceProvider::RunFrame` still has `if (m_pController) m_pController->RunFrame();`, or the HTTP endpoint handler still dispatches to a controller method, or dashboard-state polling still runs and writes to a stale flag. Code compiles, ships, and crashes on first detection trigger with an access violation." `[CITED: PITFALLS.md §Pitfall 7]`

**How to avoid (quoted):**
> "Make a removal checklist before touching code... Remove files entirely rather than feature-flagging... Compile with `-Werror` / `/WX` and `-Wunused-function` / `-Wunused-variable` during the rip-out. Warnings about unused code are the signal that the old code path is fully dead... Search for these strings across the whole repo: `TrackedDeviceAdded`, `VirtualController`, `dashboard_open`, `open_vs_select`, `isDashboardOpen`, `ControllerDevice`, `ITrackedDeviceServerDriver`." `[CITED: PITFALLS.md §Pitfall 7]`

**Planner must ensure the "deletion" task includes acceptance criteria:**
- `grep -rn` across `driver/` `src/` `apps/` (excluding `.planning/` and `build/`) for each of: `VirtualController`, `TrackedDeviceAdded`, `dashboard_open`, `isDashboardOpen`, `ControllerDevice`, `open_vs_select`, `IDashboardManager`, `DashboardManager`, `process_launcher`, `ProcessLauncher`, `micmap_controller_profile` returns **zero results** in source files.
- Build cleanly with `/WX` (MSVC) / `-Werror` (GCC/Clang). **Note:** this flag is not currently enabled in `CMakeLists.txt` `[VERIFIED: CMakeLists.txt:46,56]` — the task must add it at least for the driver target.
- The removal lands in the same commit sequence as the new sidecar implementation (not before, not after).

### Pitfall 11: DriverLog invisible

**What goes wrong (quoted):** "Driver isn't triggering inputs as expected. Developer goes to check `vrserver.txt` for the driver's own log output and finds... nothing." `[CITED: PITFALLS.md §Pitfall 11]`

**How to avoid (quoted):**
> "In the driver's main entry (`HmdDriverFactory`), verify `VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext)` is called and its return is checked. At the very start of `DeviceProvider::Init`, emit a distinctive log line: `DriverLog(\"MicMap driver v<git-hash> init\\n\")`... Add a structured log format: every meaningful error code gets logged with the OpenVR enum name (`vr::VRInputError_WrongType`), not a raw integer." `[CITED: PITFALLS.md §Pitfall 11]`

**Planner must ensure the "driver logging" task includes acceptance criteria:**
- First line on first `RunFrame` reads something like: `"MicMap driver v<version> built <timestamp> — RunFrame starting\n"` (the spec says first `RunFrame` per roadmap success criterion 3, not `Init`; both are fine but at-least-`RunFrame` is the concrete requirement).
- A helper `const char* VRInputErrorName(EVRInputError err)` exists (or equivalent) and every error-log callsite uses it.
- Manual check during validation: tail of `%LOCALAPPDATA%\Steam\logs\vrserver.txt` shows the init line on first SteamVR start after install.

### Pitfall 12: RunFrame must stay non-blocking

**What goes wrong (quoted):** "The driver's `RunFrame()` runs on vrserver's main pump. Any blocking or slow work there directly stalls vrserver." `[CITED: PITFALLS.md §Pitfall 12]`

**How to avoid (quoted):**
> "`RunFrame` does only non-blocking check/update operations: poll event queue, poll HTTP trigger flag (set by a background thread), call `UpdateBooleanComponent`, poll HMD container handle validity. Everything else on a worker thread. The HTTP bridge server (driver side) runs on its own thread — do not service HTTP requests from `RunFrame`. Measure: in dev builds, time each `RunFrame` iteration. Assert `< 1ms`. Log if exceeded." `[CITED: PITFALLS.md §Pitfall 12]`

**Planner must ensure the "RunFrame" task includes acceptance criteria:**
- `CommandQueue` drain uses `try_pop` (non-blocking); mutex held only for pop, not for the Update call.
- Dev-build timing assert around `RunFrame` body: measure `steady_clock::now()` before/after, log (or assert via `DriverLog` with rate limit) if > 1ms.
- No file I/O, no JSON parse, no sleep, no blocking HTTP calls inside `RunFrame`.
- No OpenVR driver-API calls on the HTTP thread (verify with grep: inside the `server_->Post("/button", ...)` lambda, no references to `vr::VRDriverInput()`, `vr::VRProperties()`, `vr::VRServerDriverHost()`).

## Code Examples

### Example 1: Decoding VREvent_t in the event pump

```cpp
// RunFrame() in device_provider.cpp
vr::VREvent_t ev;
while (vr::VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev))) {
    switch (ev.eventType) {
    case vr::VREvent_TrackedDeviceDeactivated:
        if (ev.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) {
            m_hSystemClick = vr::k_ulInvalidInputComponentHandle;
            m_state = HmdComponentState::Invalidated;
            DriverLog("MicMap: HMD deactivated, handle invalidated\n");
        }
        break;
    case vr::VREvent_TrackedDeviceActivated:
        // No-op — we re-create lazily in the next RunFrame iteration.
        break;
    default:
        break;
    }
}
```

Source: synthesized from `[VERIFIED: openvr_driver.h:777,1384-1391,3789]`.

### Example 2: POST /button handler (HTTP thread side)

```cpp
// In http_server.cpp, replacing the old /click /press /release handlers.
server_->Post("/button", [this](const httplib::Request& req, httplib::Response& res) {
    // Content-Type: permissive. Many test clients don't set it. We just parse the body.
    try {
        auto body = nlohmann::json::parse(req.body);
        const auto& state = body.at("state").get<std::string>();
        PressCommand cmd;
        if (state == "down") {
            cmd = { PressCommand::Kind::Down };
        } else if (state == "up") {
            cmd = { PressCommand::Kind::Up };
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"state must be \\\"down\\\" or \\\"up\\\"\"}",
                            "application/json");
            return;
        }
        // Enqueue — never blocks (drop-oldest if full). No OpenVR call from this thread.
        commandQueue_->push(cmd);
        res.set_content("{\"status\":\"ok\"}", "application/json");
    } catch (const nlohmann::json::exception&) {
        res.status = 400;
        res.set_content("{\"error\":\"malformed JSON body\"}", "application/json");
    }
});
```

Source: synthesized from existing `http_server.cpp:123-258` style + nlohmann/json standard usage + D-06.

### Example 3: CommandQueue primitive

```cpp
// driver/src/command_queue.hpp
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
    // Depth 8; drop-oldest if full.
    static constexpr size_t kMaxDepth = 8;

    // Producer side (HTTP thread). Returns true if queue was full and
    // oldest entry was dropped — caller may log.
    bool push(PressCommand cmd) {
        std::lock_guard<std::mutex> lk(m_);
        bool dropped = false;
        if (q_.size() >= kMaxDepth) {
            q_.pop_front();
            dropped = true;
        }
        q_.push_back(cmd);
        return dropped;
    }

    // Consumer side (RunFrame). Returns nullopt if empty. Never blocks.
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

Source: mirrors `virtual_controller.cpp:161-162, 256-261, 357-377` existing mutex+deque pattern `[VERIFIED: virtual_controller.cpp]`.

### Example 4: `VRInputErrorName` helper

```cpp
// driver/src/vr_error.hpp
#pragma once
#include <openvr_driver.h>

namespace micmap::driver {
inline const char* VRInputErrorName(vr::EVRInputError e) {
    switch (e) {
    case vr::VRInputError_None:                  return "None";
    case vr::VRInputError_NameNotFound:          return "NameNotFound";
    case vr::VRInputError_WrongType:             return "WrongType";
    case vr::VRInputError_InvalidHandle:         return "InvalidHandle";
    case vr::VRInputError_InvalidParam:          return "InvalidParam";
    case vr::VRInputError_NoSteam:               return "NoSteam";
    case vr::VRInputError_MaxCapacityReached:    return "MaxCapacityReached";
    case vr::VRInputError_IPCError:              return "IPCError";
    case vr::VRInputError_NoActiveActionSet:     return "NoActiveActionSet";
    case vr::VRInputError_InvalidDevice:         return "InvalidDevice";
    case vr::VRInputError_InvalidSkeleton:       return "InvalidSkeleton";
    case vr::VRInputError_InvalidBoneCount:      return "InvalidBoneCount";
    case vr::VRInputError_InvalidCompressedData: return "InvalidCompressedData";
    case vr::VRInputError_NoData:                return "NoData";
    case vr::VRInputError_BufferTooSmall:        return "BufferTooSmall";
    case vr::VRInputError_MismatchedActionManifest: return "MismatchedActionManifest";
    case vr::VRInputError_MissingSkeletonData:   return "MissingSkeletonData";
    case vr::VRInputError_InvalidBoneIndex:      return "InvalidBoneIndex";
    case vr::VRInputError_InvalidPriority:       return "InvalidPriority";
    case vr::VRInputError_PermissionDenied:      return "PermissionDenied";
    case vr::VRInputError_InvalidRenderModel:    return "InvalidRenderModel";
    }
    return "Unknown";
}
} // namespace micmap::driver
```

Values sourced from `[VERIFIED: openvr_driver.h:1419-1441]`. Enum values match SDK version 003/004 — if planner confirms the repo's SDK version is newer and has additional enumerants, add the cases.

## OpenVR API Reference (for planner)

**All signatures verified against the project's vendored SDK** at `D:/Documents/Projects/bey-closer-t1/extern/openvr/headers/openvr_driver.h`. If the project later vendors a newer SDK (e.g. in `mic-map/external/openvr/`), the same macro/accessor names resolve to newer interface versions transparently.

| Call | Signature | Notes |
|------|-----------|-------|
| `vr::VRDriverInput()->CreateBooleanComponent(hmdContainer, "/input/system/click", &handle)` | `EVRInputError CreateBooleanComponent(PropertyContainerHandle_t, const char*, VRInputComponentHandle_t*)` `[VERIFIED: openvr_driver.h:3710]` | Returns `VRInputError_InvalidParam` (4) if container invalid. Path string is literal, no device prefix. |
| `vr::VRDriverInput()->UpdateBooleanComponent(handle, bNewValue, fTimeOffset)` | `EVRInputError UpdateBooleanComponent(VRInputComponentHandle_t, bool, double)` `[VERIFIED: openvr_driver.h:3713]` | `fTimeOffset` = seconds into the future (or negative: past). Pass `0.0` to mean "now" — matches existing callsite `virtual_controller.cpp:208`. Thread-safety: must be called from RunFrame thread (the single-consumer rule, not documented by SDK but enforced by vrserver convention per Pitfall 12). |
| `vr::VRProperties()->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd)` | `PropertyContainerHandle_t TrackedDeviceToPropertyContainer(TrackedDeviceIndex_t)` `[VERIFIED: openvr_driver.h:3226]` | Returns `k_ulInvalidPropertyContainer` (0) `[VERIFIED: openvr_driver.h:338]` if the HMD is not yet activated. Call every RunFrame while handle invalid. |
| `vr::VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev))` | `bool PollNextEvent(VREvent_t*, uint32_t)` `[VERIFIED: openvr_driver.h:3789]` | Returns `true` if an event was dequeued (pump loop style). |
| `vr::VREvent_t::eventType` + `vr::VREvent_t::trackedDeviceIndex` | `uint32_t` + `TrackedDeviceIndex_t` `[VERIFIED: openvr_driver.h:1384-1391]` | `eventType == vr::VREvent_TrackedDeviceDeactivated` (101 `[VERIFIED: openvr_driver.h:778]`). Compare `trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd` (0 `[VERIFIED: openvr_driver.h:253]`). |

**Error codes worth matching explicitly for SVR-10 logging:**
- `VRInputError_None` (0) — success, no log.
- `VRInputError_InvalidHandle` (3) — handle has been invalidated by a deactivation we didn't observe. Flip to NotReady.
- `VRInputError_InvalidParam` (4) — HMD container not yet valid at Create time. Expected during cold start; only log at the `DriverLog("awaiting HMD container")` transition point (D-08).
- `VRInputError_WrongType` (2) — tried to update a handle we didn't create. Should not happen in MicMap's flow; log loudly if it does.
- `VRInputError_PermissionDenied` (19) — future SteamVR tightening (Pitfall 13). Graceful-degradation: log, enter Invalidated, don't crash.

**Idempotency of CreateBooleanComponent:** `[ASSUMED]` based on HMD Button Stub.md "Duplicate path names are allowed. SteamVR permits multiple components with the same path on the same device from different drivers." A re-create after a deactivation-invalidation creates a *new* handle with a *new* id, not the same one — the old handle is discarded by us and (per empirical observation in the sister project) silently dropped by SteamVR when its owning container was reactivated. Calling CreateBooleanComponent twice back-to-back on the *same live container* is untested in MicMap's context; the state machine avoids this by only re-creating when state is `Invalidated` (after we've let go of the old handle).

## CommandQueue Design

**Constraints from the problem:**
- 1 producer (HTTP thread); 1 consumer (RunFrame).
- Depth 8. Drop-oldest on overflow (SVR-05).
- Consumer must not block (RunFrame < 1ms, Pitfall 12).
- Minimal code, audit-able in 30 seconds.

**Recommended primitive:** `std::deque<PressCommand>` + `std::mutex` + `std::lock_guard`. See Code Example 3. 25 lines of code including comments.

**Why not lock-free (SPSC ring, `moodycamel::ConcurrentQueue`, etc.):**
- At depth 8 and 90Hz consumer, contention is noise.
- No dependency to add.
- Mirrors existing `pendingReleases_` pattern in `virtual_controller.cpp`. Consistency wins.

**Drop-oldest semantic tradeoff (IMPORTANT — the planner must surface this):**

If the HTTP client sends `DOWN, DOWN, DOWN, ..., UP` all very rapidly and overflow kicks in, the dropped command might be the UP. That strands the button in the pressed state until the next UP arrives. The risk is real but small:

1. Input rate is bounded by the app-side state machine (one press per detection cycle, which is at best ~100ms+ apart).
2. The CommandQueue is drained every RunFrame (~8-11ms), so 8 entries ≈ 64-88ms of lag at worst before drain happens — in practice, queue depth never exceeds 1-2.
3. The driver-side min-hold safety (D-04, 100ms floor) and optional max-hold watchdog (Claude's discretion in D-05) further contain stuck-button risk.

**Planner recommendation:** Accept drop-oldest as per SVR-05. Document the tradeoff in a comment on `CommandQueue::push`. If the max-hold watchdog is added (strongly encouraged — trivial cost, protects against app-crash-mid-press), it also covers this edge case.

**What NOT to do:** Do not try to be clever (e.g. "drop oldest *down*, never drop an *up*"). It's a single-writer queue; the logic to inspect content on overflow is more failure-prone than just accepting drop-oldest and trusting the other defenses.

## State-Machine Changes (src/core)

**Current shape (src/core/src/state_machine.cpp):**
```
Idle → Detecting (confidence > threshold)
Detecting → Idle (lost detection before minDetectionDuration)
Detecting → Triggered (timeInState >= minDetectionDuration)
Triggered → Cooldown (immediate, one tick)
Cooldown → Idle (timeInState >= cooldownDuration)
```

The current `Triggered` path calls `triggerCallback_()` exactly once — a tap. No release edge.

**Target shape (D-12):**
```
Idle → Detecting (confidence > threshold)
Detecting → Idle (lost detection before minDetectionDuration)
Detecting → Triggered (timeInState >= minDetectionDuration)
    → emit DOWN edge
Triggered → Releasing (confidence < threshold)
Releasing → Triggered (confidence > threshold again, mid-release; no release yet)
Releasing → Cooldown (timeInState_in_Releasing >= minReleaseDuration)
    → emit UP edge
Cooldown → Idle (timeInState >= cooldownDuration)
```

**Config struct changes (`state_machine.hpp:17-21`):**
```cpp
struct StateMachineConfig {
    std::chrono::milliseconds minDetectionDuration{100};   // Was 500 — D-04 says 100.
                                                            // NOTE: current `300` default in apps/micmap
                                                            // main.cpp:94 (`detectionTimeMs`) is orthogonal
                                                            // — that is the *user-visible tuning knob*, not
                                                            // the min-press floor. Planner should align.
    std::chrono::milliseconds minReleaseDuration{80};      // NEW (D-10)
    std::chrono::milliseconds cooldownDuration{200};       // Post-release cooldown (D-11).
                                                            // Was 300; shift semantic is re-purpose, not rename.
    float detectionThreshold{0.7f};                        // UNCHANGED
};
```

**Callback signature change:** Current `TriggerCallback = std::function<void()>`. Target either:
- (A) Replace with two callbacks: `PressCallback` + `ReleaseCallback`.
- (B) Keep one callback, pass a `PressEdge` enum: `callback(PressEdge::Down)`, `callback(PressEdge::Up)`.

Both are Claude's discretion at planning time (per D-06 which left the `IDriverClient` shape open). Option (B) is closer to the current code surface and requires fewer file edits.

**Updates to call sites:**
- `apps/micmap/main.cpp:241` — `stateMachine->setTriggerCallback([this]() { onTrigger(); });` becomes either (A) a pair of setters or (B) a single setter with a branch inside `onTrigger`.
- `apps/micmap/main.cpp:341-378` — the current `onTrigger()` body (which calls `dashboardManager->performDashboardAction()` and falls back through three layers) is deleted entirely. New body is two lines: `if (edge == Down) driverClient->setButton(ButtonState::Down); else driverClient->setButton(ButtonState::Up);`.

**Config key naming (for Phase 2 alignment):** Per D-10/D-11, the config keys that will become read-backable in Phase 2 are `min_detection_ms` (was `min_detection_duration_ms` or similar), `min_release_ms`, `cooldown_ms`. Phase 1 can hard-code defaults in the StateMachineConfig constructor — planner may skip plumbing these through `AppConfig` if that plumbing is already Phase 2 scope per CFG-01/05.

## Logging Requirements (SVR-10)

**API:** `DriverLog(const char* fmt, ...)` wrapper at `driver/src/driver_log.hpp:24-39` accepts printf-style args `[VERIFIED]`. Internally uses `vr::VRDriverLog()->Log(buffer)` after context init; falls back to `fprintf(stderr, ...)` before init. The `DriverLog` macro at `driver_log.hpp:44` is just a rename.

**Mandated log lines (from success criterion 3 + SVR-10):**

1. **Init line** (first RunFrame — per success criterion 3, "first RunFrame emits init log line"):
   ```cpp
   static bool s_initLogged = false;
   void DeviceProvider::RunFrame() {
       if (!s_initLogged) {
           DriverLog("MicMap driver v%s built %s %s — RunFrame starting\n",
                     /* MICMAP_VERSION */ "0.2.0",  // bump for the phase
                     __DATE__, __TIME__);
           s_initLogged = true;
       }
       // ...
   }
   ```

2. **Per-error logging** with enum name:
   ```cpp
   auto err = vr::VRDriverInput()->CreateBooleanComponent(hmd, "/input/system/click", &h);
   if (err != vr::VRInputError_None) {
       DriverLog("MicMap: CreateBooleanComponent failed: %s (%d)\n",
                 VRInputErrorName(err), static_cast<int>(err));
   }
   ```

3. **D-08 transition-only logs:**
   - On first `RunFrame` where HMD container is invalid: `DriverLog("MicMap: awaiting HMD container\n");`
   - On the `RunFrame` where CreateBooleanComponent finally succeeds: `DriverLog("MicMap: /input/system/click created (handle=%llu)\n", handle);`
   - On `VREvent_TrackedDeviceDeactivated`: `DriverLog("MicMap: HMD deactivated, handle invalidated\n");`
   - **Do NOT log per-RunFrame while waiting.** This is the spam Pitfall 11 warns against.

4. **D-09 drop warnings:**
   - Inside the CommandQueue drain in RunFrame, if a command is dropped because the handle is invalid: `DriverLog("MicMap: dropped press command (handle invalid)\n");` — once per drop, no coalescing.

**Separation from app-side `common::Logger`:** The app uses `MICMAP_LOG_INFO`, `MICMAP_LOG_DEBUG` etc. (macros from `src/common/include/micmap/common/logger.hpp`). These **must not** be included from `driver/src/`. Confirmed: current driver code does not include `micmap/common/logger.hpp` `[VERIFIED: grep]`. Maintain that boundary. Do not share headers that transitively pull `Logger` into the driver namespace.

## HTTP / JSON Edge Cases

**Request shape (D-06):**
```
POST /button HTTP/1.1
Content-Type: application/json  (optional — accept anything, parse body regardless)

{"state":"down"}    OR    {"state":"up"}
```

**Handler behavior:**

| Input | Response | CommandQueue side effect |
|-------|----------|--------------------------|
| Valid `{"state":"down"}` | `200 {"status":"ok"}` | Push `PressCommand{Down}` |
| Valid `{"state":"up"}` | `200 {"status":"ok"}` | Push `PressCommand{Up}` |
| Body empty / non-JSON | `400 {"error":"malformed JSON body"}` | No push |
| Missing `state` field | `400 {"error":"missing \"state\" field"}` | No push |
| `state` not one of "down"/"up" | `400 {"error":"state must be \"down\" or \"up\""}` | No push |
| Wrong method (GET) | `405` (default cpp-httplib behavior) | No push |

**Recommendation:** Reject at parse time; nothing enqueued on error. The 4xx response is computed entirely on the HTTP thread — this aligns with SVR-05 "HTTP thread never calls OpenVR." The HTTP thread only touches `nlohmann::json`, the CommandQueue mutex, and `httplib::Response`.

**Content-Type:** Be permissive. Do not require `application/json`; simply parse `req.body`. Matches the existing handlers' pattern of `req.has_param` / body access without content-type gating `[VERIFIED: http_server.cpp:155-165]`.

**Example parse code (3 lines of decode):**
```cpp
auto body = nlohmann::json::parse(req.body);    // throws on bad JSON
const auto& state = body.at("state").get<std::string>();  // throws on missing/wrong type
// branch on state == "down" / "up"
```
One `try/catch (const nlohmann::json::exception&)` covers both the parse and the `.at()`/`.get<>()` conversion failures.

## Deletion Blast-Radius Map (SVR-07, SVR-08, D-14)

**Grep-verified.** Every file below references at least one of the target deletion strings.

### Files to delete in full

| File | Reason | References that must also change |
|------|--------|----------------------------------|
| `driver/src/virtual_controller.hpp` | D-14 | See `.cpp` below |
| `driver/src/virtual_controller.cpp` | D-14 | Remove `src/virtual_controller.cpp` from `driver/CMakeLists.txt:25` |
| `driver/src/process_launcher.hpp` | D-14 | See `.cpp` below |
| `driver/src/process_launcher.cpp` | D-14 | Remove `src/process_launcher.cpp` from `driver/CMakeLists.txt:27`; also remove `#include "process_launcher.hpp"` from `device_provider.hpp:17` |
| `driver/resources/input/micmap_controller_profile.json` | D-14 | Remove the `COMMAND ... copy_if_different ... micmap_controller_profile.json` block from `driver/CMakeLists.txt:115-116`; remove the `install(FILES ... micmap_controller_profile.json)` block at `driver/CMakeLists.txt:141-143`; remove the `make_directory ... resources/input` at `driver/CMakeLists.txt:107` (or keep if other files live there) |
| `driver/resources/input/vrcompositor_bindings_micmap_controller.json` | D-14 co-travels | Remove corresponding `copy_if_different` block at `driver/CMakeLists.txt:117-119` |
| `src/steamvr/include/micmap/steamvr/dashboard_manager.hpp` | D-14 | See `.cpp` below |
| `src/steamvr/src/dashboard_manager.cpp` | D-14 | Remove `src/dashboard_manager.cpp` from `src/steamvr/CMakeLists.txt:14` |

### `src/steamvr/virtual_controller.{hpp,cpp}` and `src/steamvr/process_launcher.{hpp,cpp}`

**D-14 lists these, but they do not exist in the current tree.** `[VERIFIED: Glob src/steamvr/**/*.{cpp,hpp} returns only dashboard_manager.{hpp,cpp} and vr_input.{hpp,cpp}]`. Planner should treat these as already-deleted and not create a task to remove them. CONTEXT.md reflects a historical intent; the actual blast radius is driver-side only for `virtual_controller` / `process_launcher`.

### Callsite edits (app side)

| File | Line(s) | Current | New |
|------|---------|---------|-----|
| `apps/micmap/main.cpp` | 62, 241, 293, 334, 345-357, 390-393, 610-676 | Uses `std::unique_ptr<steamvr::IDashboardManager> dashboardManager;`, `dashboardManager->performDashboardAction()`, `dashboardManager->getConnectionState()` etc. | Remove the field; remove `createDashboardManager()` call; remove all `dashboardManager->...` accesses. Replace `onTrigger` with direct `driverClient->setButton(state)`. |
| `apps/micmap/main.cpp` | 103, 341-378 | `void onTrigger();` body with three-layer fallback | Signature changes to `void onTrigger(core::PressEdge)` (or equivalent) per §State-Machine Changes; body collapses to 2-4 lines |
| `apps/hmd_button_test/main.cpp` | 29-30, 60-61, 164-271, 605-838 | References `createDashboardManager`, `IDashboardManager`, `performDashboardAction`, dashboard state labels, `click("system"/"trigger"/"a", 100)` | Remove dashboard manager entirely; repurpose buttons to `POST /button` down/up calls (or collapse to a single "Send Press" button + "Send Release" button per D-13); remove the driver-client `click()` paths and substitute `setButton(state)`. |
| `src/steamvr/include/micmap/steamvr/vr_input.hpp` | 201-261, 273-276 | `IDriverClient` exposes `click/press/release(const std::string& button, ...)`, `createDriverClient(host, startPort, endPort)` | Collapse to `setButton(state)` OR keep two methods `press()` / `release()` — Claude's discretion. Drop the `button` name parameter entirely. |
| `src/steamvr/src/vr_input.cpp` | 150-350 (DriverClient impl) | Endpoints `/click`, `/press`, `/release` with button/duration query params | Single endpoint `/button` with JSON body; use `httplib::Client::Post(path, body, "application/json")` signature |
| `src/steamvr/CMakeLists.txt` | 14 | `src/dashboard_manager.cpp` listed | Remove the line |
| `driver/CMakeLists.txt` | 25, 27, 107, 115-119, 141-146 | virtual_controller.cpp + process_launcher.cpp sources; input profile json copy + install | Remove all three groups. Add `src/command_queue.cpp` (if non-header-only) and `src/vr_error.hpp` (header-only, no add needed). |

### Grep-zero strings

After the deletion, the following regex-search across `driver/`, `src/`, `apps/`, `tests/`, `cmake/` must return **zero** matches:

- `VirtualController|virtual_controller`
- `TrackedDeviceAdded`
- `ProcessLauncher|process_launcher`
- `IDashboardManager|DashboardManager|dashboard_manager`
- `DashboardState|isDashboardOpen|dashboard_open|performDashboardAction`
- `ControllerDevice|ITrackedDeviceServerDriver`
- `open_vs_select`
- `micmap_controller_profile`
- `MICMAP_CONTROLLER_001` (serial number string)

Some grep hits *may* remain legitimately in `.planning/**` and `README.md` — the rule is source files only.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| OpenVR SDK headers | Driver build | ✓ | SDK vendored in sibling project at `D:/Documents/Projects/bey-closer-t1/extern/openvr/` — MicMap's `cmake/FindOpenVR.cmake` resolves via `OPENVR_SDK_PATH` env var (currently unset) or by finding `external/openvr/` in the repo | None — driver cannot build without SDK; if not set up on dev box, planner must set `OPENVR_SDK_PATH` env var pointing at bey-closer-t1's SDK copy, or vendor one into `mic-map/external/openvr/` |
| openvr_api.dll | Runtime (driver + app) | ✓ (ships with SDK) | — | Copied post-build via `src/steamvr/CMakeLists.txt:74-80` |
| cpp-httplib | Driver HTTP server + app client | ✓ | v0.14.3 via FetchContent | — |
| nlohmann/json | `POST /button` body parse | ✓ | v3.11.2 via FetchContent | — |
| KissFFT, ImGui | App — unchanged this phase | ✓ | — | — |
| SteamVR runtime | Execution + validation | **Operator-provided** — Dev box must have SteamVR installed to validate SVR-11 | — | None — SVR-11 exit criterion demands a real HMD |
| HMD hardware | SVR-11 validation + D-02 spike | **Operator-provided** | Valve Index / Bigscreen Beyond / any SteamVR-supported HMD | None — "no laser beam" is visual |

**Missing dependencies with no fallback:** SteamVR runtime + HMD for the D-02 spike and SVR-11 exit. These are human/environment dependencies, not software dependencies to install.

**Missing dependencies with fallback:** None.

## Project Constraints (from CLAUDE.md)

| Directive | Applies to Phase 1 as |
|-----------|------------------------|
| "Stack is locked this milestone: C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. No framework changes." | No new libraries, no version bumps, only use what's in `external/CMakeLists.txt`. |
| "Visual validation on a real HMD is mandatory for the 'no laser beam' exit criterion of Phase 1 — type-checking and build-success do not substitute." | §Validation Architecture has SVR-11 as human-operated manual-VR. |
| "Bash is available via Git Bash; use Unix-style paths in shell commands (forward slashes, `/dev/null`)." | Planner task shell commands use `/` and `/dev/null`, not `\` or `NUL`. |
| "`hmd_button_test.exe` — VR input testing without audio (the exit-criterion harness for Phase 1)." | Per D-13, use as-is, minimally modified to call `POST /button`. |
| "Do not skip phase artifacts. They are the project's memory across context resets." | PLAN.md / TASKS / VALIDATION.md all live in `.planning/phases/01-driver-sidecar-migration/`. |
| "C++17 style, factory pattern, interface-based abstractions" (CONVENTIONS.md, pulled via CLAUDE.md) | `CommandQueue` is an internal class (no interface needed — internal-only, no mocking expected). `IDriverClient` keeps its interface; the impl changes. |

## Validation Architecture

> `workflow.nyquist_validation` — current status unknown; config file not read. Generating this section as required per the research-agent spec. Planner will confirm/strip based on config.

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest (placeholder) + manual integration via `mic_test.exe` and `hmd_button_test.exe` `[VERIFIED: TESTING.md]` |
| Config file | `tests/CMakeLists.txt` (gtests declared but commented out; option `MICMAP_USE_GTEST=OFF` default) |
| Quick run command | `ctest --test-dir build --output-on-failure` (runs placeholder test — currently no-op until unit tests added) |
| Full suite command | `cmake --build build && ctest --test-dir build` followed by manual HMD validation for SVR-04, SVR-11 |
| Manual test harness (Phase 1 primary) | `build/apps/hmd_button_test/hmd_button_test.exe` — Win32 GUI with manual Open Dashboard / Send Click buttons |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| SVR-01 | Driver does not call `TrackedDeviceAdded` | static (grep) | `grep -r 'TrackedDeviceAdded' driver/src apps/ src/` → zero | ✅ (grep is always available) |
| SVR-02 | HMD container polled each `RunFrame`, not at `Init` | static (code review) + integration (driver loads cleanly on fresh SteamVR with HMD powered off) | Manual: start SteamVR with HMD unplugged → confirm driver loads (no crash); plug in HMD → confirm `/input/system/click created` log line appears | ❌ Wave 0: manual protocol doc |
| SVR-03 | `/input/system/click` created on HMD container | integration (log check) | Manual: tail `%LOCALAPPDATA%\Steam\logs\vrserver.txt` → find `/input/system/click created (handle=...)` line | ❌ Wave 0: manual protocol doc |
| SVR-04 | Handle survives HMD sleep/wake | **manual-VR** (the D-02 spike) | Put headset on, trigger via `hmd_button_test.exe`, see dashboard open. Take headset off ≥30s. Put on. Trigger again. Second trigger opens dashboard. **N=5 cycles minimum; pass = all 5 cycles succeed.** | ❌ Wave 0: manual protocol doc |
| SVR-05 | No OpenVR API call from HTTP thread | static (grep) + runtime | `grep -nE 'VRDriverInput\(\)|VRProperties\(\)|VRServerDriverHost\(\)' driver/src/http_server.cpp` → only allowed inside function bodies that are *not* the Post lambdas. Visual code review is the gate. | ✅ (grep) |
| SVR-05 | CommandQueue is thread-safe, depth 8, drop-oldest | unit test (new) | `test_command_queue` — push 9 times, assert size=8 and first item was dropped. | ❌ Wave 0: `tests/test_command_queue.cpp` |
| SVR-06 | Scheduled release lives in RunFrame | static (code review) + integration | Inspect `device_provider.cpp` for the deferred-release logic. Manual test: send DOWN via `hmd_button_test`, send UP 20ms later, confirm release happens at press_ts+100ms not press_ts+20ms (manual timing, eyeball). | ❌ Wave 0: manual protocol |
| SVR-06 | RunFrame < 1ms dev-build assert | runtime assertion | Dev-build timing log: `steady_clock` around `RunFrame` body, log if > 1ms | ❌ add instrumentation |
| SVR-07 | Virtual-controller + process_launcher deleted, `-Werror`/`/WX` clean | build | `cmake --build build --config Release` with `/WX` on driver target → zero warnings | ✅ build system |
| SVR-07 | Grep sweep zero-match | static | `grep -rnE 'VirtualController\|TrackedDeviceAdded\|ProcessLauncher\|DashboardManager\|open_vs_select\|isDashboardOpen\|micmap_controller_profile\|MICMAP_CONTROLLER_001' driver/ src/ apps/` → zero | ✅ grep |
| SVR-08 | Single trigger code path | static (code review) | Inspect `onTrigger()` — one code branch per edge, no dashboard-state reads. | ✅ code review |
| SVR-09 | `driver_client` single-click surface | static | `IDriverClient` interface has no `click(string,int)` method; either `setButton(state)` or `press()`/`release()` only. | ✅ code review |
| SVR-10 | Init log line visible | integration | Manual: tail `vrserver.txt` during fresh SteamVR start → confirm `MicMap driver v... RunFrame starting` line present in first 5s. | ❌ Wave 0: manual protocol |
| SVR-10 | Errors logged with enum name | static (grep) | `grep -n 'VRInputErrorName\|VRInputError_' driver/src/*.cpp` → every error log callsite uses helper. | ✅ grep |
| SVR-11 | Full E2E on HMD: no laser beam, dashboard opens, second trigger works after sleep/wake | **manual-VR** | Run `hmd_button_test.exe` on a machine with SteamVR + HMD. Click "Send Press" + "Send Release" (mapped to POST /button). Verify: (a) dashboard opens, (b) no visible laser beam from HMD, (c) Settings > Devices shows no MicMap Controller entry (fresh install only — upgrade path haunts a separate ghost per Pitfall 8). Perform the D-02 cycle (headset off/on ≥30s) at least once within the session. | ❌ Wave 0: manual protocol |

### Sampling Rate

- **Per task commit:** `cmake --build build --config Debug` (driver + app build clean with `/WX`).
- **Per wave merge:** `ctest --test-dir build --output-on-failure` (quick, covers future unit tests including `test_command_queue`).
- **Phase gate:** Full manual validation suite (SVR-04, SVR-06 timing, SVR-10 log check, SVR-11 HMD session) green before `/gsd-verify-work`. The D-02 spike is a separate half-day budget.

### Wave 0 Gaps

- [ ] `tests/test_command_queue.cpp` — unit test for push/pop/drop-oldest/depth enforcement
- [ ] `tests/CMakeLists.txt` — enable `MICMAP_USE_GTEST=ON` OR write a main()-based test following `test_placeholder.cpp` shape (framework-less)
- [ ] Manual validation protocol doc (VALIDATION.md §Manual Protocols) — one section per manual SVR requirement with explicit pass/fail criteria and operator notes
- [ ] Dev-build timing-assert infrastructure — `MICMAP_DRIVER_PROFILE_RUNFRAME` CMake option that enables the `steady_clock`-based > 1ms log inside `RunFrame`

**Instruction for planner:** The unit test for `CommandQueue` is the one automated test worth writing this phase. Everything else is inherently manual (VR-bound) or static (grep/code review). Don't chase 100% automation — accept that SVR-04/11 are human-validated on a real HMD and design the plan around that.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Register virtual controller via `TrackedDeviceAdded`, bind `/input/system/click` on its container, inject via the controller device's handle | Sidecar: no device registered, `CreateBooleanComponent` on the HMD container directly | Validated in `bey-closer-t1` Phase 03 (SteamVR March 2026 + OpenVR SDK v2.5.1) | Eliminates laser beam, eliminates duplicate controller in Settings > Devices, removes "open vs. select" dashboard-state branching |
| HMD container handle cached once at driver Init time | HMD container handle polled each `RunFrame` until valid; re-polled on `VREvent_TrackedDeviceDeactivated` | This phase (Pitfall 1 extension) | Survives HMD sleep/wake without SteamVR restart |
| App-side state machine emits a single tap event | App emits down/up edges; driver enforces 100ms min-hold floor | This phase (D-03/04/05) | Press duration dynamically mirrors detection duration; brief blips still register as taps |

**Deprecated/outdated:**
- The "open vs. select" split in `onTrigger`: pressing `/input/system/click` is sufficient for both "open the dashboard when closed" and "click the item under the pointer when open." The split existed because the old virtual controller exposed `/input/trigger/click` as a separate component and the app had to choose which one to update. `[CITED: apps/micmap/main.cpp:341-378, apps/hmd_button_test/main.cpp:651-665]`
- Driver launching the app via `process_launcher`: replaced by SteamVR-native `app.vrmanifest` auto-launch in Phase 3.
- `controllerActive` HTTP status field and `/press` `/release` `/click` endpoints with `button` parameter: collapses to single `/button` + `{state}`.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `IVRDriverInput_Version` in the project's effective SDK is `_003` (header at bey-closer-t1) but might be `_004` in a different SDK version the build actually links | Standard Stack | Low — HMD Button Stub.md explicitly says the two versions share handle space; `vr::VRDriverInput()` accessor resolves to whichever ships. |
| A2 | `CreateBooleanComponent` on the same live HMD container with the same path, called after a prior handle was invalidated by `VREvent_TrackedDeviceDeactivated`, produces a fresh usable handle (i.e. the technique is idempotent across deactivate/reactivate cycles). | OpenVR API Reference + Pattern 1 | **Medium** — this is the exact gap the D-02 validation spike exists to close. If CreateBooleanComponent leaks handles on repeat calls, the half-day spike will surface it and the planner will need to add a handle-cleanup path (undocumented — OpenVR has no explicit `DestroyBooleanComponent`). |
| A3 | Dropping a stale DOWN while a newer DOWN/UP is in flight does not meaningfully corrupt state for the mic-cover use case. | CommandQueue Design | Low — queue depth 8 vs input rate ~10Hz means overflow is exceptional; min-hold + max-hold watchdog cover the residual risk. |
| A4 | The MicMap project's current build uses the vendored bey-closer-t1 SDK (via `OPENVR_SDK_PATH`). | Environment Availability | Low-medium — if dev box has its own SDK at `external/openvr/`, the version might differ. Does not affect correctness, only planner's expected version number when reading logs. |
| A5 | `IDashboardManager` surface (dashboard_manager.{hpp,cpp}) has no consumers outside `apps/micmap/main.cpp` and `apps/hmd_button_test/main.cpp`. | Deletion Blast-Radius | **Low** — grep confirmed: 31 files reference the removal strings and all repo-source matches outside `.planning/` are either the files-to-delete themselves or the two known callsites. |
| A6 | SteamVR's propagation model for cross-driver `/input/system/click` updates on the HMD container remains stable across the SDK version the project ships — specifically, that a DOWN on the sidecar's component still toggles the dashboard in March-2026-and-later SteamVR. | Pattern 1 | Medium — Pitfall 13 explicitly calls this out as the project's core external dependency. The D-02 spike also covers this (first trigger working at all on real hardware proves propagation is intact). |
| A7 | `common::Logger` headers never transitively reach driver code. | Logging Requirements | Low — grep-verified no driver-src file includes `micmap/common/logger.hpp`. |

**If an assumption fires during execution:** D-02 covers A2 and A6 directly; the half-day spike is scoped to surface both. A1 and A4 are benign (both resolve to "use the macro, not a literal"). A3 has the min-hold + max-hold safety net. A5 has the grep gate. A7 has the include-graph gate.

## Open Questions

1. **Should `setButton(ButtonState)` vs split `press()` / `release()` be chosen at the interface level?**
   - What we know: CONTEXT.md leaves it as Claude's discretion.
   - What's unclear: Whether callers benefit from a single method (symmetric, one less call surface) or two methods (clearer edges in the call graph, easier to grep).
   - Recommendation: Split `press()` / `release()`. Reasons: (a) matches the two edges in the state machine; (b) matches the shape of the existing `PressSystemButton` / `ReleaseSystemButton` methods in the soon-to-be-deleted `virtual_controller.hpp:81-86`, making the migration cognitively simpler; (c) grep-friendly.

2. **Does the existing `autoLaunchApp` setting in `default.vrsettings` need to be stripped when `process_launcher` is deleted?**
   - What we know: `device_provider.cpp:129-145` reads this setting and launches the app; this path goes away. The setting key becomes dead.
   - What's unclear: Whether leaving the key in the .vrsettings file causes problems or whether SteamVR ignores unknown keys.
   - Recommendation: Strip it (and `appPath`, `appArgs`) when editing the .vrsettings. SteamVR tolerates extra keys but clean is better.

3. **Is `CPPHTTPLIB_NO_EXCEPTIONS` compatible with the new JSON parse flow?**
   - What we know: Driver sets `CPPHTTPLIB_NO_EXCEPTIONS` at `driver/CMakeLists.txt:50`. nlohmann/json throws by default.
   - What's unclear: Whether nlohmann throwing inside a cpp-httplib handler when cpp-httplib itself is no-exceptions causes ABI issues.
   - Recommendation: Keep the nlohmann try/catch *inside* the handler lambda. The exception never escapes into cpp-httplib's frames; `CPPHTTPLIB_NO_EXCEPTIONS` only disables cpp-httplib-internal throws. This is safe, but worth noting for plan-check.

4. **Should the `CommandQueue` instance live on `DeviceProvider` or be injected into `HttpServer`?**
   - What we know: Current `HttpServer` takes a `VirtualController*` in its ctor `[VERIFIED: http_server.hpp:43]`.
   - Recommendation: Inject `CommandQueue&` (or `CommandQueue*`) into `HttpServer`. The `HttpServer` should not know about `DeviceProvider`. Lifetime: `CommandQueue` is owned by `DeviceProvider`, passed by reference.

5. **Max-hold safety release — include or defer?**
   - What we know: Claude's discretion in D-05; CONTEXT.md §Deferred Ideas flags it as worth adding.
   - Recommendation: **Include it this phase.** Cost is ~10 lines: if `m_hSystemClick` is held down and `now - press_timestamp > 5s`, force-release and log. Protects against app crash mid-press which would otherwise leave the dashboard stuck open / closed with no user recourse. Low cost, high asymmetric benefit.

## Sources

### Primary (HIGH confidence)

- `D:/Documents/Projects/bey-closer-t1/HMD Button Stub.md` — validated sidecar-on-HMD technique (SteamVR March 2026 + OpenVR SDK v2.5.1)
- `D:/Documents/Projects/bey-closer-t1/extern/openvr/headers/openvr_driver.h` — SDK API signatures for `IVRDriverInput` (line 3705-3732), `IVRProperties::TrackedDeviceToPropertyContainer` (3226), `IVRServerDriverHost::PollNextEvent` (3789), `VREvent_t` struct (1384), `EVRInputError` enum (1419-1441), `VREvent_TrackedDeviceDeactivated` (778), `k_unTrackedDeviceIndex_Hmd` (253), `k_ulInvalidPropertyContainer` (338), `k_ulInvalidInputComponentHandle` (3690)
- `.planning/research/PITFALLS.md` §Pitfall 1, 6, 7, 11, 12 — quoted directly into Common Pitfalls
- `.planning/research/SUMMARY.md` §Phase 1, §Critical Pitfalls — cross-referenced for stack and architecture
- `.planning/phases/01-driver-sidecar-migration/01-CONTEXT.md` — user decisions D-01..D-14 copied verbatim into User Constraints
- `.planning/REQUIREMENTS.md` §SVR — requirements copied into Phase Requirements
- `.planning/codebase/ARCHITECTURE.md`, `STRUCTURE.md`, `CONVENTIONS.md`, `CONCERNS.md`, `TESTING.md` — existing project conventions and known gaps
- `driver/src/virtual_controller.cpp`, `device_provider.cpp`, `http_server.cpp`, `driver_log.hpp`, `driver_main.cpp` — callsite and pattern verification
- `driver/CMakeLists.txt`, `src/steamvr/CMakeLists.txt`, root `CMakeLists.txt`, `external/CMakeLists.txt` — build system verification for deletion impact and dependency versions
- `src/steamvr/include/micmap/steamvr/vr_input.hpp`, `src/steamvr/src/vr_input.cpp`, `src/steamvr/src/dashboard_manager.cpp` — existing interfaces and impls to collapse/delete
- `src/core/src/state_machine.cpp`, `src/core/include/micmap/core/state_machine.hpp` — current state machine shape for the Releasing-state diff
- `apps/micmap/main.cpp`, `apps/hmd_button_test/main.cpp` — callsite inventory

### Secondary (MEDIUM confidence)

- `.planning/STATE.md`, `.planning/ROADMAP.md` — phase sequencing and exit criteria
- CLAUDE.md — project-specific directives surfaced in Project Constraints

### Tertiary (LOW confidence)

- `.planning/phases/01-driver-sidecar-migration/01-DISCUSSION-LOG.md` — inferred from CONTEXT.md; not read directly in this pass (planner should spot-check if CONTEXT.md decisions feel ambiguous).

## Metadata

**Confidence breakdown:**

- Standard stack: **HIGH** — all library versions pinned in `external/CMakeLists.txt`, all SDK signatures verified against vendored headers.
- Architecture: **HIGH** — pattern validated in sibling project, specific extensions (reactivation lifecycle, press/release model) are mechanical and grounded in explicit decisions D-01/D-03/D-05.
- Pitfalls: **HIGH** — quoted from project's internal PITFALLS.md which is itself cross-verified with bey-closer-t1 and OpenVR issue tracker.
- Deletion blast-radius: **HIGH** — grep-verified across `driver/ src/ apps/`.
- Reactivation behavior (A2, A6): **MEDIUM** — explicitly the gap the D-02 spike exists to close.

**Research date:** 2026-04-23
**Valid until:** ~30 days (2026-05-23). OpenVR SDK and Inno Setup versions are stable; the sidecar-technique risk (Pitfall 13) remains an accepted external dependency.

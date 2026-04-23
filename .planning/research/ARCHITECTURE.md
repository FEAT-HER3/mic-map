# Architecture Research

**Domain:** SteamVR sidecar driver + Windows desktop app with single-installer distribution (target architecture for "Seamless SteamVR Integration" milestone)
**Researched:** 2026-04-22
**Confidence:** HIGH (sidecar pattern + lifecycle validated in bey-closer-t1; installer pattern reused from BeyondProximity.iss; OpenVR API semantics cross-checked with Valve docs)

## Standard Architecture

### System Overview — Target State

```
┌──────────────────────────── Windows User Session ───────────────────────────┐
│                                                                             │
│  ┌──────────────────────── micmap.exe (app process) ───────────────────┐   │
│  │                                                                      │   │
│  │  ┌──────────┐   ┌────────────┐   ┌────────────┐   ┌──────────────┐  │   │
│  │  │ WASAPI   │──>│ Detection  │──>│ State      │──>│ DriverClient │  │   │
│  │  │ Capture  │   │ (FFT/RMS)  │   │ Machine    │   │ (HTTP POST)  │  │   │
│  │  └──────────┘   └────────────┘   └────────────┘   └──────┬───────┘  │   │
│  │                                                          │          │   │
│  │  ┌──────────────────────────────────────────────────┐    │          │   │
│  │  │ ImGui + D3D11 UI + tray                          │    │          │   │
│  │  └──────────────────────────────────────────────────┘    │          │   │
│  │  ┌──────────────────────────────────────────────────┐    │          │   │
│  │  │ ConfigManager (load+save %APPDATA%/config.json)  │    │          │   │
│  │  └──────────────────────────────────────────────────┘    │          │   │
│  └──────────────────────────────────────────────────────────│──────────┘   │
│                                                             │              │
│                                        localhost:27015      │ POST /trigger│
│                                              HTTP           ▼              │
│  ┌──────────────────── vrserver.exe (SteamVR) ──────────────────────────┐  │
│  │                                                                       │  │
│  │  ┌─────────────── driver_micmap.dll  (sidecar) ───────────────────┐  │  │
│  │  │                                                                 │  │  │
│  │  │  IServerTrackedDeviceProvider                                   │  │  │
│  │  │    Init()   — VR_INIT_SERVER_DRIVER_CONTEXT, start HttpServer   │  │  │
│  │  │    RunFrame — poll HMD container → CreateBooleanComponent once  │  │  │
│  │  │             — drain pending-trigger queue → Update…Component    │  │  │
│  │  │             — process scheduled releases (click duration)       │  │  │
│  │  │    Cleanup  — stop HttpServer, release component handle         │  │  │
│  │  │                                                                 │  │  │
│  │  │  HttpServer thread (cpp-httplib)                                │  │  │
│  │  │    POST /trigger → enqueue ClickRequest → (RunFrame drains)     │  │  │
│  │  │                                                                 │  │  │
│  │  │  NO TrackedDeviceAdded. NO VirtualController. NO laser beam.    │  │  │
│  │  │  NO GetPose. NO controller render model. Pure sidecar.          │  │  │
│  │  └─────────────────────────────────────────────────────────────────┘  │  │
│  │                                                                       │  │
│  │  lighthouse driver ── owns HMD device (index 0) ── owns its own        │  │
│  │                      /input/system/click component (handle X)          │  │
│  │                      coexists with our duplicate-path component        │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities — Target State

| Component | Responsibility | Notes for this milestone |
|-----------|----------------|--------------------------|
| `apps/micmap/main.cpp` | UI + orchestration + tray, audio callback, trigger dispatch | Remove dashboard-state branching (`getDashboardState` + "open vs select"); single path — call `driverClient->trigger()` |
| `src/audio/` | WASAPI capture, device enum | Unchanged |
| `src/detection/` | FFT + noise detection + training | Unchanged |
| `src/core/state_machine` | Idle/Training/Detecting/Triggered/Cooldown | Unchanged |
| `src/core/config_manager` | JSON read+write to `%APPDATA%/MicMap/config.json` | **Fix stubbed read path** (CFG-01); add `firstRun` flag for post-install UX |
| `src/steamvr/vr_input` | OpenVR app-side SDK wrapper | Slim down — no longer owns dashboard-state polling; keep only what's needed for `IVRApplications` (auto-launch registration) |
| `src/steamvr/dashboard_manager` | **Deleted or collapsed** into a thin trigger wrapper | The "unified action handler" disappears because there's only one action path |
| `src/steamvr/driver_client` | HTTP POST to driver, port discovery | Simplify to a single `trigger(durationMs)` method; drop `button=...` variants |
| `driver/src/device_provider` | IServerTrackedDeviceProvider impl | Removes `TrackedDeviceAdded` call; gains HMD-container component lifecycle state machine |
| `driver/src/http_server` | Listens on 27015-27025, localhost only | Simplify endpoints to `POST /trigger` + `GET /health`/`GET /port`; remove per-button routes. Core threading model (one worker thread) unchanged |
| `driver/src/virtual_controller` | **Deleted entirely** (SVR-04) | All ~380 lines gone, including `GetPose`, button/trigger handles, scheduled-release queue — release queue moves into DeviceProvider |
| `driver/src/process_launcher` | **Deleted entirely** | Auto-launch moves to SteamVR's `app.vrmanifest` mechanism; driver no longer spawns app |
| `driver/resources/micmap_controller_profile.json` | **Deleted** | Not needed — no controller |
| `driver/driver.vrdrivermanifest` | `alwaysActivate:true`, `resourceOnly:false` | Unchanged structurally, but no `input_profile_path` references |
| `installer/MicMap.iss` (new) | Inno Setup installer | Patterned on BeyondProximity.iss — admin elevated, WMI vrserver.exe check, `vrpathreg adddriver`, registers `app.vrmanifest` via helper |
| `apps/micmap/app.vrmanifest` (new) | SteamVR app manifest | Declares app for `IVRApplications::SetApplicationAutoLaunch(true)` so SteamVR launches `micmap.exe` on startup |
| `apps/micmap/manifest_registrar` (new) | Registers `app.vrmanifest` with SteamVR on first run | Runs once per user-install (not per-machine); uses IVRApplications API |

**What dies** (remove, don't feature-flag):
- `driver/src/virtual_controller.{hpp,cpp}` — the whole file (~380 LOC)
- `driver/src/process_launcher.{hpp,cpp}` — replaced by `app.vrmanifest` auto-launch
- `driver/resources/micmap_controller_profile.json`
- `src/steamvr/src/dashboard_manager.cpp` — "open vs select" branch logic (SVR-03 is a single path)
- `getDashboardState()` calls from `main.cpp`
- `TrackedDeviceAdded` call in `device_provider.cpp:45-51`
- `scripts/install_driver.bat` / `uninstall_driver.bat` — superseded by installer
- HTTP routes: `/click?button=a`, `/click?button=trigger`, `/press`, `/release` — only `/trigger` survives

**What's new:**
- `CommandQueue` primitive inside driver (see Data Flow §)
- HMD-activation-watcher state in DeviceProvider (polling in RunFrame)
- `app.vrmanifest` file + post-install registration step
- Inno Setup installer directory

**What's reshaped:**
- `DeviceProvider` absorbs the scheduled-release queue (was in `VirtualController::RunFrame`)
- `DriverClient` shrinks to one endpoint
- `HttpServer` becomes a thin command-enqueue shim (no direct controller calls)

## Recommended Project Structure

```
mic-map/
├── apps/
│   ├── micmap/
│   │   ├── main.cpp
│   │   ├── app.vrmanifest          # NEW — SteamVR app auto-launch descriptor
│   │   └── resources/              # existing icons etc.
│   ├── mic_test/                   # unchanged
│   └── hmd_button_test/            # keep for manual validation of sidecar
├── driver/
│   ├── src/
│   │   ├── driver_main.cpp         # HmdDriverFactory export (unchanged)
│   │   ├── device_provider.{hpp,cpp}  # reshaped: HMD-activation watcher, no TrackedDeviceAdded
│   │   ├── http_server.{hpp,cpp}   # simplified: /trigger only
│   │   ├── command_queue.hpp       # NEW — HTTP→RunFrame marshalling primitive
│   │   └── driver_log.hpp          # unchanged
│   │   # virtual_controller.{hpp,cpp}  DELETED
│   │   # process_launcher.{hpp,cpp}    DELETED
│   ├── resources/
│   │   └── default.vrsettings      # kept — driver settings (log level, port override)
│   └── driver.vrdrivermanifest     # unchanged structurally
├── src/
│   ├── audio/                      # unchanged
│   ├── common/                     # unchanged
│   ├── core/
│   │   └── src/config_manager.cpp  # FIX read-back path (CFG-01)
│   ├── detection/                  # unchanged
│   └── steamvr/
│       ├── src/driver_client.cpp   # simplified — single trigger() method
│       ├── src/vr_input.cpp        # kept for IVRApplications auto-launch registration
│       └── src/manifest_registrar.cpp  # NEW — AddApplicationManifest + SetApplicationAutoLaunch
│       # dashboard_manager.{hpp,cpp}   DELETED (or collapsed to a 20-line trigger wrapper)
├── installer/                      # NEW DIRECTORY
│   ├── MicMap.iss                  # Inno Setup script — patterned on BeyondProximity.iss
│   └── assets/                     # icons, license, etc.
└── scripts/
    # install_driver.bat / uninstall_driver.bat  DELETED
```

### Structure Rationale

- **`apps/micmap/app.vrmanifest` lives next to the exe** so the installer can copy it alongside the binary and pass an absolute path to `AddApplicationManifest`. The manifest's `"binary_path_windows"` is resolved relative to the manifest file, so colocation keeps install-time path juggling minimal.
- **`driver/src/command_queue.hpp` is a new driver-internal header**, not a shared lib, because it's a bespoke primitive with a 3-field struct tied to driver concerns. Don't hoist it to `src/common/` — different thread-safety posture than app-side types.
- **`installer/` at repo root** mirrors bey-closer-t1 convention and keeps Inno Setup artifacts out of `scripts/` (which is for dev-machine utilities).
- **`manifest_registrar` goes in `src/steamvr/`** not `apps/micmap/` because it wraps OpenVR SDK calls — same layer as `vr_input`. The app calls it on startup; the installer does not (avoids needing a separate CLI tool that links OpenVR).

## Architectural Patterns

### Pattern 1: HMD-Container Component Lifecycle State Machine (driver-side)

**What:** The driver cannot create its `/input/system/click` component at `Init()` time because the HMD's property container is invalid until the lighthouse driver activates it. Instead, the driver runs a small state machine on each `RunFrame()` tick:

```
[PendingCreation] ──poll TrackedDeviceToPropertyContainer──> valid?
      │                                                         │
      │ no (stay)                                   yes          ▼
      └────────┐                              CreateBooleanComponent("/input/system/click")
               │                                                 │
               │                                    success ─────┼────── failure
               │                                       │         │          │
               ▼                                       ▼         ▼          ▼
       [next RunFrame]                          [Ready]  [Failed — log, stop retrying]
                                                   │
                                                   │ on detection trigger
                                                   ▼
                                           UpdateBooleanComponent(handle, true)
                                           schedule release @ now+durationMs
```

**When to use:** Any sidecar driver creating input on devices it doesn't own. Validated in bey-closer-t1 + documented in `HMD Button Stub.md`.

**Trade-offs:**
- (+) No `TrackedDeviceAdded` call → no virtual controller → no laser beam artefact (the user-stated motivation).
- (+) Works around `VRInputError_InvalidParam` at Init() time.
- (-) One-shot creation (`m_bComponentAttempted` latch) doesn't handle HMD re-activation. See **Lifecycle §** for hot-swap discussion.

**Example (driver/src/device_provider.cpp, target state):**
```cpp
void DeviceProvider::RunFrame() {
    // 1. HMD-container component lifecycle
    if (!m_hmdComponentAttempted) {
        auto hmd = VRProperties()->TrackedDeviceToPropertyContainer(
                       k_unTrackedDeviceIndex_Hmd);
        if (hmd != k_ulInvalidPropertyContainer) {
            m_hmdComponentAttempted = true;
            auto err = VRDriverInput()->CreateBooleanComponent(
                           hmd, "/input/system/click", &m_hSystemClick);
            if (err != VRInputError_None) {
                m_hSystemClick = k_ulInvalidInputComponentHandle;
                DriverLog("CreateBooleanComponent failed: %d\n", err);
            }
        }
    }

    // 2. Drain any queued triggers from HTTP thread
    ClickRequest req;
    while (m_commandQueue.try_pop(req)) {
        if (m_hSystemClick != k_ulInvalidInputComponentHandle) {
            VRDriverInput()->UpdateBooleanComponent(m_hSystemClick, true, 0.0);
            m_pendingReleases.push_back({m_hSystemClick,
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(req.durationMs)});
        }
    }

    // 3. Process scheduled releases
    auto now = std::chrono::steady_clock::now();
    m_pendingReleases.erase(
        std::remove_if(m_pendingReleases.begin(), m_pendingReleases.end(),
            [&](const auto& pr) {
                if (now >= pr.releaseTime) {
                    VRDriverInput()->UpdateBooleanComponent(pr.handle, false, 0.0);
                    return true;
                }
                return false;
            }),
        m_pendingReleases.end());
}
```

### Pattern 2: Single-Producer-Single-Consumer Command Queue (HTTP thread → RunFrame thread)

**What:** The HTTP server thread (inside cpp-httplib) receives `POST /trigger` requests. It must **not** call `UpdateBooleanComponent` directly — OpenVR's driver-side APIs expect to be called on vrserver's RunFrame thread. Instead, HTTP handlers enqueue a command; RunFrame drains.

**When to use:** Any cross-thread handoff between a non-RunFrame thread and OpenVR driver calls. This is the canonical pattern for sidecar drivers accepting external input.

**Primitive recommendation:** **Mutex + `std::deque<ClickRequest>`** (simple bounded FIFO), with `try_pop()` non-blocking drain in RunFrame.

**Why this over alternatives:**

| Option | Recommendation | Rationale |
|--------|---------------|-----------|
| `std::atomic<bool> triggerPending` + `std::atomic<int> durationMs` | ❌ Reject | Coalesces rapid-fire triggers, loses duration fidelity, race on read-reset-clear |
| `std::condition_variable` | ❌ Reject | RunFrame is called unconditionally by SteamVR at ~90Hz — it's a polled loop, not event-driven. CV adds complexity with no benefit; HTTP thread never needs to wake the RunFrame thread. |
| Lock-free SPSC queue (e.g. `boost::lockfree::spsc_queue`) | ❌ Overkill | Trigger rate is <1 Hz peak (human mic-cover cadence). Mutex contention is a non-issue. Adds boost dependency for zero measurable benefit. |
| **`std::mutex` + `std::deque<ClickRequest>` with bounded capacity (e.g. 8)** | ✅ **Recommend** | Simple, correct, preserves each trigger's parameters, drops gracefully under pathological burst (log + discard oldest). Matches existing `pendingReleasesMutex_` pattern in `virtual_controller.cpp` — proven in-tree idiom. |
| `httplib::Server::set_task_queue` with custom pool | ❌ Wrong layer | Doesn't solve thread-affinity — cpp-httplib still runs handlers off-RunFrame |

**Trade-offs of the chosen mutex+deque:**
- (+) Short critical section (push one 16-byte struct).
- (+) No new dependencies.
- (+) Same idiom already used in-tree — low cognitive load.
- (-) Technically a brief lock on both threads — immaterial given trigger rate.

**Example (driver/src/command_queue.hpp, new):**
```cpp
struct ClickRequest {
    int durationMs;
    std::chrono::steady_clock::time_point enqueuedAt;
};

class CommandQueue {
public:
    void push(ClickRequest req) {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.size() >= kMaxDepth) m_queue.pop_front();  // drop oldest
        m_queue.push_back(req);
    }
    bool try_pop(ClickRequest& out) {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.empty()) return false;
        out = m_queue.front();
        m_queue.pop_front();
        return true;
    }
private:
    static constexpr size_t kMaxDepth = 8;
    std::mutex m_mtx;
    std::deque<ClickRequest> m_queue;
};
```

### Pattern 3: Duplicate-Path Component Coexistence (cross-driver input injection)

**What:** Two drivers register the same component path (`/input/system/click`) on the same device container (the HMD). SteamVR permits this and propagates updates from whichever driver calls `UpdateBooleanComponent`. **Critically:** you cannot call `UpdateBooleanComponent` on the lighthouse driver's handle — it returns `VRInputError_WrongType`. You must create your own.

**When to use:** When you need to trigger an input action that another driver owns (system button, proximity sensor, etc.) and you cannot control that driver.

**Trade-offs:**
- (+) Only documented mechanism that achieves this outcome.
- (+) Validated on SteamVR March 2026 + OpenVR SDK v2.5.1.
- (-) Undocumented behavior — Valve could in theory tighten this in future SteamVR releases (treat as a dependency risk, not a showstopper).
- (-) Debugging note: handle IDs differ per-driver. Handle `3` in MicMap is not handle `3` in lighthouse.

## Data Flow

### End-to-End Trigger Flow (target)

```
[User covers mic]
    │
    ▼
WASAPI callback (audio thread)
    │  raw float samples
    ▼
Detection (FFT/RMS, same thread)
    │  DetectionResult {confidence, ...}
    ▼
StateMachine::update()   Idle → Detecting → Triggered
    │  onTrigger() fires
    ▼
DriverClient::trigger(100ms)                    [app process, audio thread]
    │
    │  HTTP POST localhost:27015/trigger?duration=100
    ▼
────────────────── process boundary ──────────────────
    │
HttpServer cpp-httplib worker thread            [driver, HTTP thread]
    │  parse, validate, push to queue
    ▼
CommandQueue::push({durationMs=100})            [mutex acquired ~1µs]
    │
    │  (HTTP thread returns 200 OK immediately)
    ▼
    ⋯ waits up to 1/90s ⋯
    │
    ▼
DeviceProvider::RunFrame()                       [driver, RunFrame thread, ~90Hz]
    │  CommandQueue::try_pop → ClickRequest
    ▼
VRDriverInput()->UpdateBooleanComponent(
    m_hSystemClick, true, 0.0)                  [if handle valid]
    │
    │  schedule release at t+100ms
    ▼
    ⋯ ~9 RunFrame ticks later ⋯
    │
    ▼
VRDriverInput()->UpdateBooleanComponent(
    m_hSystemClick, false, 0.0)
    │
    ▼
SteamVR propagates to application input layer
    │
    ▼
[Dashboard opens / in-dashboard selection fires]
```

**Latency budget:** ~1 RunFrame tick (≤11ms @ 90Hz) from HTTP POST to button-down. Within perceptual noise floor; matches user expectation for a microphone-triggered action.

### HMD Re-Activation Flow (hot-swap, HMD sleep/wake)

```
[HMD sleeps or disconnects]
    │
    ▼
lighthouse driver may call Deactivate on HMD
    │
    ▼
Our m_hSystemClick handle: technically still valid for our driver
(handles are per-driver; we didn't destroy ours) BUT downstream
propagation may silently stop because the HMD container state is gone.
    │
    ▼
[HMD wakes / reconnects]
    │
    ▼
lighthouse driver re-activates HMD — property container handle may differ.
    │
    ▼
Our one-shot m_hmdComponentAttempted latch is still TRUE — we don't re-create.
    │
    ▼
⚠ POTENTIAL BUG: updates may go to a stale handle. Needs validation.
```

See **Lifecycle §** below for the concrete recommendation.

### State Management (driver-side)

```
Driver lifecycle:

[DLL loaded by vrserver]
    │
    ▼
HmdDriverFactory() → DeviceProvider*
    │
    ▼
DeviceProvider::Init()
    • VR_INIT_SERVER_DRIVER_CONTEXT
    • start HttpServer thread
    • state: m_hmdComponentAttempted = false
             m_hSystemClick = invalid
    │
    ▼
[SteamVR pumps RunFrame ~90Hz]
    │
    ├─→ HMD not yet activated: poll returns invalid container → do nothing
    │
    ├─→ HMD activated this frame: Create component, latch attempted=true
    │
    └─→ Handle valid: drain queue + process releases
    │
    ▼
DeviceProvider::Cleanup()
    • stop HttpServer
    • VR_CLEANUP_SERVER_DRIVER_CONTEXT
    • handles are invalidated by SteamVR
```

## HMD-Container Handle Lifecycle (deep dive)

This is the single most architecturally load-bearing topic in the milestone. Four sub-cases:

### Case A: HMD not yet activated at Init() time (normal cold start)

**What happens:** `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` returns `k_ulInvalidPropertyContainer`. `CreateBooleanComponent` on an invalid container fails with `VRInputError_InvalidParam`.
**Handling:** Defer to RunFrame polling. First RunFrame tick after HMD activation succeeds. **Validated in bey-closer-t1.**

### Case B: HMD activates after a few RunFrame ticks (normal warm start)

**What happens:** First N ticks see invalid container; tick N+1 returns valid handle; create succeeds.
**Handling:** Same polling loop. No special code — `m_bComponentAttempted` latches once successful.

### Case C: CreateBooleanComponent fails despite valid HMD container

**What happens:** E.g. SteamVR version regression, permissions issue, other driver grabbed an exclusive lock. Returns an error other than `InvalidParam`.
**Handling:** Log the error code. Set `m_hmdComponentAttempted = true` (stop retrying — don't spam SteamVR). `m_hSystemClick` remains invalid; updates become no-ops. App still runs; user gets no trigger but no crash. This is the "fallback gracefully" requirement from SVR-02.

### Case D: HMD re-activation / hot-swap (HMD sleeps and wakes, or user power-cycles HMD mid-session)

**What happens:** Underspecified in OpenVR docs and NOT validated in bey-closer-t1. Two hypotheses:
1. **Optimistic:** Property container handle is stable across HMD sleep/wake; our component handle remains live; updates work after wake.
2. **Pessimistic:** Container handle invalidates; our component handle becomes stale; updates silently fail.

**Recommendation for this milestone:**
1. **Ship with the one-shot latch** (Case A/B/C behavior) as V1.
2. **Add a Phase validation TODO**: during hmd_button_test manual validation, sleep and wake the HMD, verify trigger still works. If it doesn't, add:
   ```cpp
   // On every RunFrame tick, re-check container handle
   auto currentHmd = VRProperties()->TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd);
   if (currentHmd != m_lastHmdContainer) {
       m_lastHmdContainer = currentHmd;
       m_hmdComponentAttempted = false;  // force re-creation
       m_hSystemClick = k_ulInvalidInputComponentHandle;
   }
   ```
3. **Do NOT ship this re-creation logic speculatively** — it's ~10 LOC but the CreateBooleanComponent call is suspected to leak handles on repeated calls per-driver-session (anecdotal, unvalidated). Ship simple; fix reactively if validation fails.

**Confidence:** HIGH for Cases A-C (validated). MEDIUM for Case D (hypothesized; needs in-phase validation).

### Graceful teardown

`Cleanup()` is called by SteamVR before the DLL unloads. Stop HttpServer first (to prevent new triggers arriving during teardown), then `VR_CLEANUP_SERVER_DRIVER_CONTEXT`. Component handle doesn't need explicit release — SteamVR reclaims when the driver context tears down. **Current `device_provider.cpp:76-101` already does this correctly; retain the pattern.**

## Install-Time vs. Runtime Dependencies

This section explicitly enumerates what must be true at each stage, for the quality gate.

### Install-time (Inno Setup, admin-elevated)

| Dependency | Who provides | When |
|------------|--------------|------|
| Steam install located | Installer guesses `{autopf}\Steam`, validates presence of SteamVR | Before file copy |
| SteamVR not running | Installer checks `vrserver.exe` via WMI (pattern from BeyondProximity.iss:100-105) | Before file copy — abort if running |
| Driver files placed at `{steam}\steamapps\common\SteamVR\drivers\micmap\bin\win64\driver_micmap.dll` | Installer `[Files]` section | File copy step |
| `driver.vrdrivermanifest` placed at `{steam}\steamapps\common\SteamVR\drivers\micmap\driver.vrdrivermanifest` | Installer `[Files]` | File copy step |
| `micmap.exe` + `app.vrmanifest` placed at **`{pf}\MicMap\`** (outside Steam; app is not a driver) | Installer `[Files]` | File copy step |
| Driver registered with SteamVR | `vrpathreg.exe adddriver "{steam}\…\drivers\micmap"` | Installer `[Run]` step |
| `app.vrmanifest` registered for auto-launch | **Deferred to first app run** (see below) — NOT done by installer | n/a at install time |

**Key asymmetry with bey-closer-t1:** BeyondProximity nests under `Bigscreen Beyond Driver` because it piggybacks on an existing HMD driver. MicMap **owns its own driver directory** (`drivers\micmap\`) under SteamVR's driver root — simpler, no nesting, no resourceOnly manifest dance. The BeyondProximity.iss `RestoreRootManifest` / `RestoreVrresources` functions have no MicMap analogue and should NOT be copied.

### Runtime Boot Order

```
1. User logs in → SteamVR launches (from Steam auto-start, or manually)
2. vrserver.exe reads all drivers under paths registered via vrpathreg
3. driver_micmap.dll is loaded — HmdDriverFactory() called
4. DeviceProvider::Init() — HttpServer starts on 27015
5. DeviceProvider::RunFrame() begins ticking at ~90Hz
6. HMD activates (lighthouse driver) — our RunFrame creates /input/system/click
7. vrserver reads registered app manifests — finds MicMap's app.vrmanifest
8. If SetApplicationAutoLaunch(true) set earlier: SteamVR launches micmap.exe
9. micmap.exe starts: loads config, connects to driver HTTP, starts audio capture
```

**Critical ordering invariant:** The driver is loaded by SteamVR **before** `micmap.exe` is launched via auto-start. The app will attempt to POST to the driver HTTP endpoint; if the HTTP server isn't up yet, the DriverClient retries (existing async-connect logic in `src/steamvr/driver_client`). This timing is safe because `Init()` starts HttpServer synchronously before returning.

**If auto-launch isn't yet registered** (first-ever session after install): SteamVR starts without launching the app. User launches micmap.exe manually once; `manifest_registrar` inside the app calls `AddApplicationManifest` + `SetApplicationAutoLaunch(true)`. **From the next SteamVR session onward, auto-launch works.** This "register on first manual run" pattern is the cleanest way around the documented [SteamVR bug](https://github.com/ValveSoftware/openvr/issues/106) where `AddApplicationManifest` doesn't take effect until next vrserver restart.

### `vrpathreg` vs. `IVRApplications::AddApplicationManifest` — distinct namespaces

| Tool / API | Registers | Runtime effect |
|------------|-----------|----------------|
| `vrpathreg adddriver <path>` | A **driver** (DLL + vrdrivermanifest). Writes to SteamVR's `steamvr.vrsettings` externaldrivers list. | SteamVR loads the DLL on next startup |
| `IVRApplications::AddApplicationManifest(<path>)` + `SetApplicationAutoLaunch(appKey, true)` | An **application** (exe + vrmanifest). Writes to SteamVR's `applications.json`. | SteamVR launches the exe on next startup when `auto_launch=true` and app key matches |

These are **orthogonal registries.** The installer MUST call `vrpathreg` for the driver. The app (not the installer) calls `AddApplicationManifest` for the manifest. Uninstall must reverse both.

### Uninstall Symmetry

| Install action | Uninstall action | Notes |
|----------------|-----------------|-------|
| `vrpathreg adddriver <driverpath>` | `vrpathreg removedriver <driverpath>` | Installer `[UninstallRun]` |
| Copy `driver_micmap.dll`, `driver.vrdrivermanifest`, `app.vrmanifest` | Delete files + empty dirs | Inno Setup handles via `[UninstallDelete]` implicitly for tracked files |
| `AddApplicationManifest` (done at runtime by app) | `RemoveApplicationManifest` | **Cannot be done from installer** (OpenVR not guaranteed available). Best-effort: app does it in a "uninstall mode" CLI flag, OR installer ignores it and SteamVR self-cleans when the manifest file vanishes (imperfect but acceptable). |
| `SetApplicationAutoLaunch(true)` | `SetApplicationAutoLaunch(false)` | Same caveat — best-effort; surviving state is harmless (it's just a dangling entry in applications.json pointing at a deleted exe). |

**Recommendation:** Installer does perfect symmetry for driver. App-manifest entry is tolerated as orphaned state post-uninstall — SteamVR handles missing binaries gracefully. Don't overengineer uninstall.

**Note on Inno Setup:** BeyondProximity.iss sets `Uninstallable=no`. For MicMap, flip this to `yes` — users expect Add/Remove Programs entries. Uninstaller runs `vrpathreg removedriver` symmetrically.

## Suggested Build Order (phase slicing recommendation)

This is the load-bearing output for roadmap authoring. Ordering is driven by: (a) avoiding broken intermediate states, (b) enabling validation at each phase end, (c) minimizing rework.

### Phase 1: Driver sidecar rewrite (SVR-01 through SVR-04)
**Rationale:** Must land FIRST. Reasons:
1. Auto-start (AUTO-01) of an app that drives a broken/virtual-controller driver is strictly worse UX than no auto-start — users see a laser beam they didn't want, plus their app launches itself, which is the wrong combination.
2. The installer (INST-01) needs to know final driver layout to place files correctly. Writing the installer against the old driver layout is throwaway work.
3. `hmd_button_test` can validate the sidecar in isolation — no audio, no UI, no installer needed. Tight iteration loop.
4. `dashboard_manager` deletion and `driver_client` simplification cascade from this phase — they're touched once, not twice.

**Contents of phase:**
- Remove `TrackedDeviceAdded` call
- Create HMD-container component on RunFrame-polled HMD activation
- Add CommandQueue; wire HTTP `/trigger` → queue → RunFrame drain
- Delete `virtual_controller.{hpp,cpp}`, `process_launcher.{hpp,cpp}`, `micmap_controller_profile.json`
- Simplify `driver_client.cpp` to single `trigger()` method
- Delete `dashboard_manager` open/select branching; collapse to thin trigger wrapper
- Validation: manual test with `hmd_button_test` — cover mic surrogate → dashboard opens, no laser beam visible

**Exit criterion:** MicMap works end-to-end via the sidecar path on a developer machine that still has the manually-installed-via-batch driver.

### Phase 2: Config read-back (CFG-01)
**Rationale:** Small, independent, unblocks persistent user settings which matter for post-installer UX (user tunes sensitivity once, it sticks). Can run in parallel with Phase 1 if capacity allows, but not a blocker for Phase 3.
**Exit criterion:** Restart app → sensitivity, device choice, training data all restored.

### Phase 3: Auto-launch (AUTO-01)
**Rationale:** Depends on Phase 1 (don't auto-launch a broken driver). Depends on `app.vrmanifest` file existing. Does NOT depend on Phase 4 installer — can be tested manually by running the app once to register the manifest. Independent of CFG-01.
**Contents:**
- Author `app.vrmanifest` with correct `binary_path_windows`, `app_key`, `name`, `image_path`
- Implement `manifest_registrar` in `src/steamvr/` — calls `AddApplicationManifest` + `SetApplicationAutoLaunch(true)` on app startup (idempotent)
- Wire into app init path (after OpenVR client init)
**Exit criterion:** Run micmap.exe once, quit, restart SteamVR → micmap.exe launches automatically.

### Phase 4: Installer (INST-01, INST-02)
**Rationale:** Packages everything from Phases 1-3. Last, because it depends on final file layout and the auto-launch mechanism already being in place.
**Contents:**
- `installer/MicMap.iss` patterned on BeyondProximity.iss (strip out nested-driver / RestoreRootManifest logic)
- Admin elevation, vrserver.exe WMI check, `vrpathreg adddriver`
- Places driver at `{steam}\…\drivers\micmap`, app at `{pf}\MicMap\`
- Uninstallable (unlike BeyondProximity): Inno Setup uninstaller runs `vrpathreg removedriver`
- Optional post-install "Launch SteamVR" checkbox (pattern from BeyondProximity.iss:67-69)
**Exit criterion:** Clean VM, run installer, SteamVR finds driver, launches micmap, mic-cover triggers dashboard. Uninstaller cleanly reverses.

### Phase 5: Documentation (DOC-01)
**Rationale:** Always last — docs describe shipped reality, not plans. README sections marked crossed-out for auto-start become current.

### Dependency graph

```
Phase 1 (driver sidecar) ──┬──> Phase 3 (auto-launch) ──┐
                           │                            ├──> Phase 4 (installer) ──> Phase 5 (docs)
Phase 2 (config read)  ────┴────────────────────────────┘
         (independent, any order before Phase 4)
```

## Scaling Considerations

This is a single-user desktop app. "Scaling" in the traditional sense doesn't apply. Relevant axes:

| Concern | Current | At stress | Mitigation |
|---------|---------|-----------|------------|
| Trigger rate | <1 Hz typical | 10 Hz pathological (user spamming) | CommandQueue drops oldest at depth 8; cooldown in state_machine prevents spam upstream |
| HTTP port contention | 27015 default, 27015-27025 fallback | Other app grabs all 10 ports | Driver logs & fails gracefully; app surfaces connection error in UI |
| SteamVR API version drift | OpenVR SDK v2.5.1 | Valve changes duplicate-path behavior | Runtime error from CreateBooleanComponent → log + fall back silently; user sees "detected but not triggering" — known fail mode |
| HMD re-activation | One-shot latch | HMD sleeps/wakes mid-session | Validation TODO in Phase 1; if broken, add ~10 LOC re-check logic |

## Anti-Patterns

### Anti-Pattern 1: Feature-flagging the virtual controller
**What people do:** Keep `virtual_controller.cpp` behind a config flag "in case sidecar doesn't work."
**Why it's wrong:** Dead-code paths rot. The user explicitly stated no fallback. The sidecar is validated. Dual paths means two sets of tests, two sets of configs, two sets of edge cases.
**Do this instead:** Delete. If the sidecar breaks in the field, roll back via installer version, not via runtime flag.

### Anti-Pattern 2: Calling UpdateBooleanComponent from the HTTP thread
**What people do:** In the `/trigger` handler, directly call `VRDriverInput()->UpdateBooleanComponent(...)`.
**Why it's wrong:** OpenVR driver-side APIs are not documented as thread-safe. Observed behavior is "mostly works" but race conditions with RunFrame can corrupt internal SteamVR state. Valve devs have said "call driver APIs from RunFrame" in community posts.
**Do this instead:** Enqueue a command; RunFrame drains. See Pattern 2.

### Anti-Pattern 3: Re-registering the app.vrmanifest on every app launch
**What people do:** Call `AddApplicationManifest` on every startup as a "just in case."
**Why it's wrong:** Spams SteamVR's applications.json; `SetApplicationAutoLaunch` has documented regressions ([issue #1378](https://github.com/ValveSoftware/openvr/issues/1378)) around re-registered apps returning `UnknownApplication`.
**Do this instead:** Check `IsApplicationInstalled(app_key)` first; only register if absent. Store a "registered" flag in config.json as belt-and-braces.

### Anti-Pattern 4: Copying BeyondProximity.iss's RestoreRootManifest logic
**What people do:** Clone the whole BeyondProximity.iss, including the `RestoreRootManifest` + `RestoreVrresources` Pascal Script procedures.
**Why it's wrong:** That logic exists because BeyondProximity nests under the Bigscreen Beyond driver folder and must preserve the parent driver's manifest as `resourceOnly=true`. MicMap owns its own driver directory. No parent manifest. That logic is not just unneeded — it would corrupt a valid driver manifest.
**Do this instead:** Start from the skeleton (preprocessor defines, `[Setup]`, `[Files]`, `[Run]` for vrpathreg, `IsProcessRunning` / `PrepareToInstall` for vrserver.exe detection). Drop the `[Code]` procedures.

### Anti-Pattern 5: Driver-launches-app process spawning (current code)
**What people do:** Driver calls `CreateProcess` to launch micmap.exe from its DLL.
**Why it's wrong:** Runs under vrserver.exe's token (potentially elevated, potentially wrong session in multi-user scenarios). Bypasses SteamVR's own app lifecycle. Makes uninstall harder (dangling processes). Already a known source of weirdness in `process_launcher.cpp`.
**Do this instead:** Use SteamVR's native auto-launch (`app.vrmanifest`). SteamVR launches the app in the correct session with the correct token, tracks lifetime, and cleans up on SteamVR exit.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| SteamVR (vrserver.exe) | Driver loaded as DLL; app linked against OpenVR client lib | Driver is in-process with vrserver; app is separate process |
| Steam | Only touched by installer to locate SteamVR install path | `{autopf}\Steam\steamapps\common\SteamVR\` convention |
| Windows WASAPI | C API consumed by `src/audio` | Unchanged this milestone |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `apps/micmap` ↔ `src/*` libraries | Direct C++ function calls | Standard linkage |
| `src/steamvr/driver_client` ↔ `driver/src/http_server` | Localhost HTTP (cpp-httplib) | Process boundary; 27015-27025 port range; JSON body |
| `driver HTTP thread` ↔ `driver RunFrame thread` | **CommandQueue (mutex + deque)** | NEW primitive; see Pattern 2 |
| `src/steamvr/manifest_registrar` ↔ SteamVR | IVRApplications OpenVR client API | Only from app process; not from installer |
| `installer` ↔ SteamVR | `vrpathreg.exe` subprocess | Only at install/uninstall time |

## Sources

- [`D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md`](file:///D:/Documents/Projects/bey-closer-t1/HMD%20Button%20Stub.md) — HIGH confidence, direct validation of sidecar technique on SteamVR March 2026 + OpenVR SDK v2.5.1
- [`D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss`](file:///D:/Documents/Projects/bey-closer-t1/installer/BeyondProximity.iss) — HIGH confidence, working installer pattern to copy from (minus nested-driver Pascal procedures)
- [ValveSoftware/openvr — Driver API Documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md) — HIGH confidence for RunFrame semantics, TrackedDeviceToPropertyContainer usage
- [ValveSoftware/openvr#106 — AddApplicationManifest doesn't load until reboot](https://github.com/ValveSoftware/openvr/issues/106) — MEDIUM confidence, older issue but symptom is recurring
- [ValveSoftware/openvr#1378 — SetApplicationAutoLaunch fails with UnknownApplication](https://github.com/ValveSoftware/openvr/issues/1378) — MEDIUM confidence, relevant gotcha for manifest_registrar logic
- [ValveSoftware/openvr Wiki — API Documentation](https://github.com/ValveSoftware/openvr/wiki/API-Documentation) — HIGH confidence for IVRApplications surface
- Current codebase files inspected: `driver/src/device_provider.cpp`, `driver/src/virtual_controller.cpp`, `driver/src/http_server.cpp`, `driver/driver.vrdrivermanifest` — HIGH confidence for "what's there to remove"

---
*Architecture research for: MicMap Seamless SteamVR Integration milestone — target sidecar architecture*
*Researched: 2026-04-22*

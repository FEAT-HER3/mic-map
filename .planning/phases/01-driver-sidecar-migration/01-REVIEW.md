---
phase: 01-driver-sidecar-migration
reviewed: 2026-04-23T00:00:00Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - apps/hmd_button_test/main.cpp
  - apps/micmap/main.cpp
  - driver/CMakeLists.txt
  - driver/resources/settings/default.vrsettings
  - driver/src/command_queue.hpp
  - driver/src/device_provider.cpp
  - driver/src/device_provider.hpp
  - driver/src/http_server.cpp
  - driver/src/http_server.hpp
  - driver/src/vr_error.hpp
  - src/core/include/micmap/core/state_machine.hpp
  - src/core/src/state_machine.cpp
  - src/steamvr/CMakeLists.txt
  - src/steamvr/include/micmap/steamvr/vr_input.hpp
  - src/steamvr/src/vr_input.cpp
  - tests/CMakeLists.txt
  - tests/test_command_queue.cpp
findings:
  critical: 0
  warning: 8
  info: 6
  total: 14
status: issues_found
---

# Phase 01: Code Review Report

**Reviewed:** 2026-04-23
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

The Phase 1 sidecar architecture is clean and load-bearing invariants are
respected: the HTTP thread never touches the OpenVR driver API, RunFrame owns
all `UpdateBooleanComponent` calls, `writeValue()` is the single choke point
enforcing no-redundant-writes and transition-to-Invalidated-on-error, and the
min-hold/max-hold/HMD-deactivated safety rules are implemented in the order
the header's doc-comment describes. The `CommandQueue` is correctly bounded
and drop-oldest, with direct coverage in `test_command_queue.cpp`.

Main concerns are concentrated in the HTTP server's startup dance (a genuine
race/TOCTOU on the `running_` flag that causes incorrect port-retry decisions)
and in the app-side trigger wiring (the state machine update runs from the
audio callback, so `onTrigger` — and therefore a blocking HTTP POST — is
called on the WASAPI thread, which is the main real-time audio path). One
driver-cleanup ordering bug can leak a `HttpServer` if `RegisterClass`/D3D
setup in `DeviceProvider::Cleanup` tears down OpenVR context before the HTTP
thread has stopped accepting requests (the `Stop()` is before
`VR_CLEANUP_SERVER_DRIVER_CONTEXT`, which is correct — but the parallel issue
in `apps/micmap` on `shutdown()` is worth calling out).

No critical (security / crash / data-loss) findings. The driver runs in
`vrserver.exe`, binds only to `127.0.0.1`, and parses JSON with `try/catch`
around `nlohmann::json::parse`, which handles all malformed-body cases.

## Warnings

### WR-01: HttpServer port-retry loop races on `running_` and will spuriously "succeed" on the wrong port

**File:** `driver/src/http_server.cpp:62-96` (see also `ServerThread` at 175-204)
**Issue:** The port-retry loop in `Start()` launches `ServerThread`, sleeps
200ms, then reads `running_` to decide whether the bind succeeded. But
`ServerThread` sets `running_ = true` on successful `bind_to_port`, AND also
sets `running_ = true` from the `set_pre_routing_handler` callback ("safety
net"). More importantly, if `bind_to_port` fails, `ServerThread` sets
`running_ = false` and returns — so the next retry iteration calls
`serverThread_.join()`, then reassigns `serverThread_ = std::thread(...)`.
That works, but:
1. If `bind_to_port` takes longer than 200ms on a slow machine (unlikely but
   possible on contention), `Start()` will see `running_ == false` and treat
   a still-in-progress bind as a failure, `join()` will then block waiting
   for the thread to actually fail, and the loop will spin forward into the
   next port while httplib may still be holding resources.
2. There is no synchronization (condition variable / promise) between the
   two threads. `running_` is `std::atomic<bool>` so the reads are safe,
   but `serverThread_.join()` after reading `running_ == false` will
   silently succeed if `ServerThread` hasn't yet set `running_ = true` but
   is about to — a classic TOCTOU.
3. The pre-routing handler re-assigns `running_ = true` on every request.
   Since `ServerThread` already sets it after `bind_to_port`, this handler
   serves no purpose and adds confusion; comment calls it a "safety net"
   but it is not needed.

**Fix:** Use a `std::promise<bool>` / `std::future<bool>` pair so
`ServerThread` signals bind-success deterministically, and drop the
pre-routing handler.

```cpp
// http_server.hpp
std::promise<bool> bindPromise_;

// http_server.cpp — Start()
for (int tryPort = startPort; tryPort <= endPort; ++tryPort) {
    port_ = tryPort;
    bindPromise_ = std::promise<bool>();
    auto bindFuture = bindPromise_.get_future();
    serverThread_ = std::thread(&HttpServer::ServerThread, this);
    if (bindFuture.get()) {      // blocks until ServerThread signals
        portFound = true;
        break;
    }
    if (serverThread_.joinable()) serverThread_.join();
    server_ = std::make_unique<httplib::Server>();
    SetupRoutes();
}

// ServerThread()
if (!server_->bind_to_port(host_.c_str(), port_)) {
    bindPromise_.set_value(false);
    return;
}
running_ = true;
bindPromise_.set_value(true);
server_->listen_after_bind();
running_ = false;
```

---

### WR-02: `onTrigger` invokes blocking HTTP POST on the real-time audio callback thread

**File:** `apps/micmap/main.cpp:240-317` (callback installation) → `state_machine.cpp:140-171` (triggerCallback_ invocation) → `apps/micmap/main.cpp:334-345` (`onTrigger`)
**Issue:** `audioCapture->setAudioCallback(...)` passes a lambda that, among
other things, calls `stateMachine->update(...)`. `StateMachineImpl::update`
invokes `triggerCallback_` synchronously when a DOWN or UP edge fires
(`state_machine.cpp:141, 170`). That callback is `MicMapApp::onTrigger`,
which calls `driverClient->press()` / `release()` — each of which
constructs a fresh `httplib::Client` with a **2-second connection timeout
and 2-second read timeout** (`vr_input.cpp:170-172, 201-203`). In the worst
case (driver not responsive) a single audio-buffer callback blocks for up
to 4 seconds inside the WASAPI capture thread, which will glitch capture
and likely cause dropped buffers / missed detection windows.

Additionally, when `press()`/`release()` fails it mutates `connected_ = false`
without holding any lock, racing with the main thread's reconnect future
(`apps/micmap/main.cpp:604-621`) that can call `connect()` concurrently.
`connected_` and `port_` are plain `bool`/`int`, not atomic, so this is a
data race (though benign on x86).

**Fix:** Dispatch the press/release via a lock-free queue or `std::async`
from within `onTrigger` so the audio callback returns immediately:

```cpp
void MicMapApp::onTrigger(core::PressEdge edge) {
    // Post to a worker; never block the audio callback.
    std::thread([this, edge]() {
        if (!driverClient || !driverClient->isConnected()) return;
        bool ok = (edge == core::PressEdge::Down)
                    ? driverClient->press() : driverClient->release();
        if (!ok) MICMAP_LOG_WARNING("onTrigger failed: {}",
                                    driverClient->getLastError());
    }).detach();
}
```

Better still: serialize through an SPSC queue drained by the existing
message-pump loop to preserve edge ordering (a DOWN followed by an UP
dispatched via detached threads can be reordered on Windows under load).

---

### WR-03: `DriverClient` state (`connected_`, `port_`, `lastError_`) is not thread-safe but is touched from multiple threads

**File:** `src/steamvr/src/vr_input.cpp:118-266`
**Issue:** Looking at callers of a single `IDriverClient` instance:
- `apps/micmap/main.cpp:606-608` calls `connect()` from a detached
  `std::async` thread.
- The audio callback thread reaches `onTrigger` → `press()/release()` which
  both read `connected_` and write `connected_ = false` on failure
  (`vr_input.cpp:178, 210`).
- The main UI thread calls `isConnected()` every frame
  (`apps/micmap/main.cpp:356, 603`).

`connected_ : bool`, `port_ : int`, and `lastError_ : std::string` are plain
non-atomic members. Writing `lastError_` from one thread while another reads
it in `getLastError()` is a data race with a `std::string` destination —
undefined behavior per the C++ memory model. The hmd_button_test app
mitigates this by only touching the client from the UI thread, but
`apps/micmap` does not.

**Fix:** Either make `connected_` / `port_` `std::atomic` and guard
`lastError_` with a mutex, or document and enforce single-threaded
ownership (e.g. marshal all driver-client calls to the main thread's
message-pump queue).

---

### WR-04: `MicMapApp::shutdown()` tears down `driverClient` while detached `std::async` futures may still be running

**File:** `apps/micmap/main.cpp:565-573, 594-620, 638`
**Issue:** `initialize()` kicks off a detached `std::thread` that calls
`driverClient->connect()`; the main loop also stores `driverConnectFuture`
and `vrInitFuture` as function-local `static` futures. When
`g_app.shutdown()` runs (line 638), it calls `driverClient->disconnect()`
but does not wait on either future. If a reconnect future is in-flight
(inside `httplib::Client::Get("/health")` with 1s timeout), that future is
still touching `g_app.driverClient` (captured by raw pointer via lambda
closure) while the main thread is destroying it. Same risk for
`vrInputFuture` and `g_app.vrInput`.

After `shutdown()` returns, static local futures `driverConnectFuture` /
`vrInitFuture` will eventually run their destructors at program exit; if
they are still `valid()` those destructors will block — but the objects
they reference (`driverClient`) have already been reset. Use-after-free.

**Fix:** Wait on the futures before tearing down:
```cpp
void MicMapApp::shutdown() {
    running = false;
    if (audioCapture) audioCapture->stopCapture();
    // ... wait for any in-flight async reconnects
    // Easiest: hoist driverConnectFuture/vrInitFuture into MicMapApp
    // and .wait() them here before resetting driverClient/vrInput.
    ...
}
```

---

### WR-05: DeviceProvider state reset in `Cleanup` happens after `VR_CLEANUP_SERVER_DRIVER_CONTEXT` — order is OK, but `isPressed_` flag is reset without ever writing UP

**File:** `driver/src/device_provider.cpp:62-87`
**Issue:** On `Cleanup()`, if the button is currently pressed
(`isPressed_ == true`), the code resets `isPressed_ = false` and
`lastWrittenValue_ = false` in memory but never calls
`VRDriverInput()->UpdateBooleanComponent(hSystemClick_, false, 0.0)` to
flush an UP edge to SteamVR. This is contrary to the max-hold watchdog's
intent ("guard against an app crash mid-press leaving the system button
stuck down"). SteamVR driver shutdown on a clean `VR_CLEANUP` may reset
the component anyway, but that is an implementation detail of the runtime
— the driver's own invariant (emit UP before going away) is violated.

**Fix:** Emit the UP edge before invalidating the handle:
```cpp
void DeviceProvider::Cleanup() {
    if (!initialized_) return;
    if (isPressed_ && state_ == HmdComponentState::Ready) {
        VRDriverInput()->UpdateBooleanComponent(hSystemClick_, false, 0.0);
        DriverLog("MicMap: flushed UP on Cleanup (was pressed)\n");
    }
    if (httpServer_) { httpServer_->Stop(); httpServer_.reset(); }
    ...
}
```

---

### WR-06: `transitionTo` in state machine resets `timeInState_` but `update()` increments it BEFORE dispatching the new state handler on the very next tick — no bug, but UP-edge timing drifts by one `deltaTime`

**File:** `src/core/src/state_machine.cpp:34-62, 104-119`
**Issue:** When `updateDetecting` calls `transitionTo(State::Triggered)`
and then (on a future tick) the confidence drops, `updateTriggered` moves
to `Releasing`. The next `update(...)` starts by doing
`timeInState_ += deltaTime` — including the delta BEFORE the new state was
entered. Because `transitionTo` resets `timeInState_ = 0`, the accumulation
actually correctly represents "time in the new state since entry", so this
is not a bug. However, the check `timeInState_ >= minReleaseDuration`
becomes true **one tick later** than literal reading suggests, because the
first tick after entering `Releasing` has already added one deltaTime.
Combined with D-04's 100ms minDetectionDuration, at a ~60Hz audio-callback
cadence the jitter is ~16ms, which is within tolerance.

Call this out as an Info-level latent issue, but flagging as Warning
because if `update()` is ever called with a very large `deltaTime` (e.g.
app paused), the very first `Releasing` tick can immediately satisfy
`timeInState_ >= minReleaseDuration` and fire UP prematurely. Guard by
doing the increment AFTER the state handler dispatches, or clamp
`deltaTime`.

**Fix:** Increment `timeInState_` at the end of `update()` (not the top)
so the first tick of a new state always observes `timeInState_ == 0`,
or clamp `deltaTime` to a sane upper bound (e.g. 100ms).

---

### WR-07: Audio-callback "cooldown" and `detectionStartTime` are non-atomic but read/written from audio + UI threads

**File:** `apps/micmap/main.cpp:81-103, 240-317`
**Issue:** `detectionStartTime` and `lastTriggerTime`
(`std::chrono::steady_clock::time_point`) are written inside the audio
callback (lines 269, 288) but are not marked atomic. The UI thread's
`renderUI()` does not read them directly, but `inCooldown` and
`buttonWouldFire` are `std::atomic<bool>` — suggesting the author was
aware of the threading boundary but missed these two. Reading a
`time_point` concurrently with a write is technically UB (even though on
x86-64 it's a 64-bit aligned load so practically safe).

**Fix:** Either move the time-point fields under `audioMutex` (already
held when they are written) and require the mutex for reads, or make
them `std::atomic<std::chrono::steady_clock::time_point>` in C++20 /
convert to `std::atomic<int64_t>` counts of ms since epoch.

---

### WR-08: `hmd_button_test` Tap uses `::Sleep(150)` on the UI thread — blocks window procedure and will freeze message pump

**File:** `apps/hmd_button_test/main.cpp:541-585`
**Issue:** `OnSendTapClicked` runs on the UI thread (handling WM_COMMAND)
and calls `::Sleep(150)` mid-handler. Timer callbacks, window resizes, and
the SteamVR `Quit` event poll (which runs from WM_TIMER) are all blocked
for 150ms. This is not catastrophic for a test harness but inconsistent
with the plan's "Tap (Press+150ms+Release)" intent: a real button tap
wouldn't freeze the UI.

**Fix:** Use `SetTimer(hwnd, ID_TAP_RELEASE_TIMER, 150, ...)` and release
from the timer callback, or spawn a detached thread to do the sleep.

```cpp
void OnSendTapClicked() {
    // ... press
    SetTimer(g_state.hwnd, ID_TAP_RELEASE_TIMER, 150, [](HWND h, UINT, UINT_PTR id, DWORD) {
        KillTimer(h, id);
        if (g_state.driverClient) g_state.driverClient->release();
    });
}
```

## Info

### IN-01: `driver/resources/settings/default.vrsettings` references `http_port: 27015` but the app client tries 27015–27025 and the driver retries the same range — default value will be drifted from code if anyone edits one

**File:** `driver/resources/settings/default.vrsettings:1-7`, `driver/src/http_server.cpp:23-24`, `src/steamvr/include/micmap/steamvr/vr_input.hpp:217-220`
**Issue:** The default port (27015) and port range (27015-27025) are
hardcoded in three places (vrsettings, http_server.cpp `kPortRangeStart/End`,
and `createDriverClient` defaults). Not wired through the vrsettings —
`HttpServer` never reads `driver_micmap.http_port` from `VRSettings()`.
This is consistent with the Phase 2 scope note (config-read-back is the
next phase), but worth flagging because the default.vrsettings file is
currently decorative.

**Fix:** Noted for Phase 2; either remove the keys from
`default.vrsettings` until they are wired, or add a TODO comment in the
file referring to Phase 2.

---

### IN-02: Const-correctness: `IDriverClient::getLastError()` returns by value but the member is accessed non-const

**File:** `src/steamvr/include/micmap/steamvr/vr_input.hpp:204`, `src/steamvr/src/vr_input.cpp:248-250`
**Issue:** `std::string getLastError() const` is not `const` in the
interface (line 204 has no `const` qualifier) even though the
implementation's override IS `const` (line 248). Inconsistent with other
getters in the same file (`getPort() const`, `isConnected() const`).

**Fix:** Add `const` to the interface declaration:
```cpp
virtual std::string getLastError() const = 0;
```

---

### IN-03: `DeviceProvider::writeValue` does not log transitions to `Invalidated` in a way distinct from CreateBooleanComponent failure

**File:** `driver/src/device_provider.cpp:190-207`
**Issue:** On `UpdateBooleanComponent` failure, the method logs the error
but sets `hSystemClick_ = k_ulInvalidInputComponentHandle` and
`state_ = HmdComponentState::Invalidated`, then lets RunFrame's step (2)
attempt recreation on the next tick. Log line does not mention the
state transition explicitly. When debugging a stuck button, knowing
"Update failed → state=Invalidated → RunFrame recreated" is easier to
grep for if the transition is logged. Minor.

**Fix:**
```cpp
DriverLog("MicMap: UpdateBooleanComponent(%s) failed: %s (%d) — state → Invalidated\n",
          v ? "down" : "up", VRInputErrorName(err), static_cast<int>(err));
```

---

### IN-04: `CommandQueue::push` return value "dropped" is ignored at the only caller site

**File:** `driver/src/http_server.cpp:148`, `driver/src/command_queue.hpp:17-24`
**Issue:** `HttpServer::SetupRoutes` calls `queue_.push(cmd)` and discards
the bool return. The comment on `push` says "Returns true if queue was
full and oldest dropped" — but no telemetry records the drop. Under the
SVR-05 drop-oldest contract this is intentional, but a one-line DriverLog
when a drop occurs would aid phase-1 debugging (the whole point of a
depth-8 queue is to handle overwhelm; silent drops are hard to
distinguish from missed presses).

**Fix:**
```cpp
bool dropped = queue_.push(cmd);
if (dropped) DriverLog("MicMap: CommandQueue full, dropped oldest\n");
```

---

### IN-05: Test coverage: `test_command_queue.cpp` does not exercise concurrent producer + consumer

**File:** `tests/test_command_queue.cpp:1-46`
**Issue:** Tests verify empty / round-trip / drop-oldest behaviors, all on
a single thread. The CommandQueue's stated purpose is concurrent
producer (HTTP thread) + consumer (RunFrame). No test currently exercises
the mutex under contention. A simple pair of threads push/pop-ing with
TSan (or just running under `valgrind --tool=helgrind`) would cheaply
validate the contract.

**Fix:** Add a concurrent-smoke test:
```cpp
static void test_concurrent_producer_consumer() {
    CommandQueue q;
    std::atomic<bool> stop{false};
    std::thread producer([&]{
        for (int i = 0; i < 10000 && !stop; ++i)
            q.push({i & 1 ? PressCommand::Kind::Up : PressCommand::Kind::Down});
    });
    int popped = 0;
    while (!stop) {
        if (q.try_pop()) ++popped;
        if (popped > 1000) stop = true;
    }
    producer.join();
}
```

---

### IN-06: `default.vrsettings` / driver `CMakeLists.txt` inconsistency — `resources/input` directory created but never populated

**File:** `driver/CMakeLists.txt:127-128`
**Issue:** `add_custom_command` creates
`${CMAKE_BINARY_DIR}/driver/micmap/resources/input` but nothing is ever
copied into it. Orphan directory. Likely vestigial from the pre-sidecar
virtual-controller design that had action manifests under `resources/input/`.

**Fix:** Remove the orphan mkdir:
```cmake
# Remove this line from the add_custom_command block:
COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_BINARY_DIR}/driver/micmap/resources/input"
```

---

_Reviewed: 2026-04-23_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

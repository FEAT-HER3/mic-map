---
phase: 03-auto-start
reviewed: 2026-04-23T22:00:00Z
depth: standard
files_reviewed: 27
files_reviewed_list:
  - CMakeLists.txt
  - apps/micmap/CMakeLists.txt
  - apps/micmap/app.vrmanifest.in
  - apps/micmap/first_launch_balloon.cpp
  - apps/micmap/first_launch_balloon.hpp
  - apps/micmap/main.cpp
  - src/common/CMakeLists.txt
  - src/common/include/micmap/common/cli_flags.hpp
  - src/common/src/cli_flags.cpp
  - src/core/include/micmap/core/config_manager.hpp
  - src/core/src/config_manager.cpp
  - src/steamvr/CMakeLists.txt
  - src/steamvr/include/micmap/steamvr/manifest_registrar.hpp
  - src/steamvr/include/micmap/steamvr/vr_input_events.hpp
  - src/steamvr/src/manifest_registrar.cpp
  - src/steamvr/src/vr_input.cpp
  - src/steamvr/src/vr_input_events.cpp
  - tests/CMakeLists.txt
  - tests/test_cli_flags_parse.cpp
  - tests/test_config_manager.cpp
  - tests/test_manifest_registrar.cpp
  - tests/test_tray_balloon_once.cpp
  - tests/test_vr_input_quit_ordering.cpp
  - tests/test_vrmanifest_schema.cpp
  - tools/CMakeLists.txt
  - tools/register_manifest.cpp
findings:
  critical: 0
  warning: 6
  info: 8
  total: 14
status: issues_found
---

# Phase 3: Code Review Report

**Reviewed:** 2026-04-23T22:00:00Z
**Depth:** standard
**Files Reviewed:** 27
**Status:** issues_found

## Summary

Phase 3 delivers a cohesive, well-tested SteamVR auto-start stack: manifest
registration with the OpenVR #1378 poll guard, ack-before-notify for
VREvent_Quit (closes Valve's 2-second watchdog race), a clean CLI fork via
CommandLineToArgvW, the first-silent-launch balloon with Focus-Assist
awareness, and a carefully ordered idempotent `MicMapApp::shutdown`. Test
doubles are well-structured and exercise the load-bearing ordering invariants.

The issues below are mostly in pre-existing `main.cpp` glue (WinMain error
paths, async reconnect futures racing with the new retry thread), plus a
log-format bug that predates Phase 3 but got exercised by new Phase 3 call
sites. No Critical findings — none of the new Phase 3 surfaces introduce a
security vulnerability, data-loss risk, or crash on the nominal path. The
Warning items are worth addressing before Phase 4's installer ships, because
the installer's uninstall hook will exercise the CLI fork and the retry-thread
ordering under unusual conditions (SteamVR mid-upgrade, Windows Update reboot
pending).

## Warnings

### WR-01: Concurrent `vr::VR_Init` from retry thread and VR input init thread

**File:** `apps/micmap/main.cpp:248-278`, `apps/micmap/main.cpp:705-713`, `apps/micmap/main.cpp:769-776`
**Issue:** `MicMapApp::initialize()` spawns `manifestRetryThread` which calls
`vr::VR_Init(&err, vr::VRApplication_Utility)` in a loop (line 252). In
parallel, `WinMain` detaches `initThread` (line 705-713) that calls
`g_app.vrInput->initialize()`, which itself calls
`vr::VR_Init(&initError, vr::VRApplication_Background)` in `vr_input.cpp:287`.
The message-loop reconnect path also calls `g_app.vrInput->initialize()` via
`std::async` (lines 769-776). OpenVR's `VR_Init` is a process-global call that
is not safe to invoke concurrently from multiple threads with different
`EVRApplicationType` values — the later call can return
`VRInitError_Init_AlreadyInitialized` or overwrite the global system pointer,
and on the SteamVR side the IVRSystem instance type may be indeterminate. The
retry thread's `VR_Shutdown` on line 255 can also tear down the IVRSystem
instance that `OpenVRInput::pollEvents` is about to use via `vrSystem_`. The
existing comment at `shutdown()` line 408 only addresses the *teardown*
ordering; the *startup* window (first 30s of life) is unprotected.

**Fix:** Serialize `VR_Init`/`VR_Shutdown` across the two paths. Either:
```cpp
// Option A: retry thread defers until vrInput is initialized.
manifestRetryThread = std::thread([this]() {
    // Wait for vrInput to own the VR_Init slot before attempting utility init.
    while (!manifestRetryCancel.load() && (!vrInput || !vrInput->isInitialized())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // Now use the existing vr::VRApplications() accessor directly via
    // ensureRegistered — no VR_Init needed, we piggyback on vrInput's session.
    // ...
});
```
Or better: since `VRApplication_Background` (used by `OpenVRInput`) already
exposes `vr::VRApplications()`, drop the `VR_Init(Utility)` in the retry
thread entirely and call `manifestRegistrar->ensureRegistered()` only once
`vrInput->isInitialized()` is true. This also removes the `VR_Shutdown` that
is currently racing with `OpenVRInput::pollEvents`.

---

### WR-02: Logger format-string mismatch — `{}` placeholders logged as literals

**File:** `src/steamvr/src/vr_input.cpp:112,129,140,165,169,179,186,190`, `apps/micmap/main.cpp:444`
**Issue:** The `MicMapApp` logger (`src/common/include/micmap/common/logger.hpp:112-117`)
uses C++ fold expression with `operator<<` — it concatenates args, it does
NOT perform `{}` substitution. Multiple call sites use fmtlib-style
placeholders:
```cpp
MICMAP_LOG_DEBUG("DriverClient created (host: {}, ports: {}-{})",
                 host_, startPort_, endPort_);           // vr_input.cpp:112
MICMAP_LOG_INFO("Connected to MicMap driver on port {}", port_);   // :140
MICMAP_LOG_ERROR("DriverClient::tap() failed: {}", lastError_);    // :165
MICMAP_LOG_WARNING("onTrigger failed: {}", driverClient->getLastError()); // main.cpp:444
```
These emit the literal string `"... host: {}, ports: {}-{}<host_val><start>-<end>"`
with the `{}` characters verbatim and the values appended. Functionally the
values still reach the log, but the output is misleading and breaks any
downstream log parsing. This pre-dates Phase 3 but Plan 03-07's `onTrigger`
refactor added a new instance of the bug.

**Fix:** Use stream-concatenation style, matching the rest of the codebase
(e.g., `config_manager.cpp:119`, `manifest_registrar.cpp:173`):
```cpp
MICMAP_LOG_DEBUG("DriverClient created (host: ", host_,
                 ", ports: ", startPort_, "-", endPort_, ")");
MICMAP_LOG_WARNING("onTrigger failed: ", driverClient->getLastError());
```
Or, out of scope for Phase 3: introduce fmtlib and update the Logger
template. Pick one convention and enforce project-wide.

---

### WR-03: `MicMapApp::initialize()` return value discarded; downstream code assumes success

**File:** `apps/micmap/main.cpp:701`
**Issue:** `g_app.initialize()` returns `bool` (line 181 / 395), and returns
`false` when `audio::createWASAPICapture()` fails (line 188). The caller
discards the result:
```cpp
g_app.initialize();
SetupSystemTray(g_app.hwnd);
```
If audio capture fails, `g_app.detector` is never created, `g_app.audioCapture`
is null, and the app still proceeds to `SetupSystemTray`, enters the message
loop, and invokes `renderUI()` which accesses `detector` under null-guards
but with degraded UX. More importantly, the retry thread still starts and
eventually registers the manifest, which will cause SteamVR to auto-launch a
broken instance on next boot. A failed init should fail-closed.

**Fix:**
```cpp
if (!g_app.initialize()) {
    MICMAP_LOG_ERROR("MicMapApp::initialize failed; exiting");
    // Bail out cleanly — no tray, no retry thread, no auto-register.
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_app.hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CloseHandle(hMutex);
    return 1;
}
```
Equally: the retry thread's `manifestRegistrar` unique_ptr creation (line 247)
should be gated behind a successful init, not issued unconditionally.

---

### WR-04: Early-exit paths in WinMain leak window class / mutex and skip shutdown

**File:** `apps/micmap/main.cpp:689-693`
**Issue:** Two pre-shutdown failure windows leak resources:
1. Line 693: if `CreateDeviceD3D(g_app.hwnd)` fails, the code does
   `CleanupDeviceD3D(); UnregisterClassW(...); return 1;` — but `hMutex` is
   never closed and `DestroyWindow(g_app.hwnd)` is never called. OS cleanup
   on process exit papers over this, but `CreateMutexW(..., TRUE, ...)` was
   acquired with initial-owner ownership — subsequent launches will see
   `ERROR_ALREADY_EXISTS` until the OS releases the handle, which can create
   ghost-instance detection during crash-restart cycles.
2. Line 691: `CreateWindowW` return is not checked for `nullptr`. If
   `CreateWindowW` fails, `g_app.hwnd` is null, and `CreateDeviceD3D(nullptr)`
   on line 693 will call `D3D11CreateDeviceAndSwapChain` with
   `sd.OutputWindow = nullptr`, which on recent Windows returns `E_INVALIDARG`
   but is undefined on older builds.

**Fix:**
```cpp
g_app.hwnd = CreateWindowW(...);
if (!g_app.hwnd) {
    MICMAP_LOG_ERROR("CreateWindowW failed: ", GetLastError());
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CloseHandle(hMutex);
    return 1;
}

if (!CreateDeviceD3D(g_app.hwnd)) {
    CleanupDeviceD3D();
    DestroyWindow(g_app.hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CloseHandle(hMutex);
    return 1;
}
```

---

### WR-05: Detached `std::async` reconnect futures outlive `g_app` on shutdown

**File:** `apps/micmap/main.cpp:760-776`, `apps/micmap/main.cpp:398-436`
**Issue:** The reconnect loop starts `std::async(std::launch::async, ...)`
lambdas that capture `g_app` (global) and call `g_app.driverClient->connect()`
or `g_app.vrInput->initialize()`. The lambdas are stored in function-local
`static std::future<void>` objects (`driverConnectFuture`, `vrInitFuture`).
When the message loop exits and `g_app.shutdown()` runs, it calls
`driverClient->disconnect()` (line 423) and `vrInput->shutdown()` (line 425)
while an in-flight future may still be inside `connect()` / `initialize()`.
The future destructor will block on program exit (standard `std::async`
policy), but:
1. `shutdown()` has already reset/destroyed `detector`, removed the tray
   icon, etc. — any exception or assertion from the still-running future
   fires against a half-destroyed state.
2. If `vrInitFuture` is still in `VR_Init`, `shutdown()`'s `vrInput->shutdown`
   calls `VR_Shutdown` against a non-initialized state (this was partially
   addressed by the retry thread join in step 0, but NOT the `std::async`
   futures — they have no cancel flag).

**Fix:** Either (a) track these futures on `MicMapApp` and wait on them
explicitly at the TOP of `shutdown()` (alongside the retry-thread cancel),
or (b) add an atomic `connectInFlight_` / `initInFlight_` guard that
`shutdown()` observes before tearing down `driverClient` / `vrInput`. The
retry-thread pattern (Plan 03-07) is the right model; extend it to cover
these two futures:
```cpp
// In MicMapApp struct:
std::atomic<bool> reconnectCancel{false};
std::future<void> driverConnectFuture;
std::future<void> vrInitFuture;

// In shutdown(), immediately after the manifestRetryThread join:
reconnectCancel.store(true);
if (driverConnectFuture.valid()) driverConnectFuture.wait();
if (vrInitFuture.valid())         vrInitFuture.wait();
```

---

### WR-06: `writeAtomicWindows` leaves temp file on ofstream write failure

**File:** `src/core/src/config_manager.cpp:319-331`
**Issue:** The atomic write creates `tmp` via ofstream, and if the write
fails (disk full, access denied on the target directory), the function
returns `false` after closing the stream — but the zero-byte or partial
`.tmp` file is left on disk:
```cpp
std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
if (!f) {
    MICMAP_LOG_ERROR("Could not open temp config file: ", tmp.string());
    return false;   // tmp file orphaned (only not-opened case — probably OK)
}
f.write(utf8Content.data(), static_cast<std::streamsize>(utf8Content.size()));
if (!f) {
    MICMAP_LOG_ERROR("Write to temp config file failed");
    return false;   // <-- tmp file EXISTS here, partial write, orphaned
}
```
Over many failed save attempts this could leak orphan `config.json.tmp` files
next to the real config. The rename-success paths clean up correctly; only
the ofstream-write-failure path is leaky.

**Fix:** Add `fs::remove(tmp, ec)` on the post-open error path, mirroring the
cleanup already done in the `ReplaceFileW` / `MoveFileExW` failure paths:
```cpp
f.write(utf8Content.data(), static_cast<std::streamsize>(utf8Content.size()));
if (!f) {
    MICMAP_LOG_ERROR("Write to temp config file failed");
    f.close();
    std::error_code remEc;
    fs::remove(tmp, remEc);
    return false;
}
```

## Info

### IN-01: `SHGetFolderPathW` is deprecated in favor of `SHGetKnownFolderPath`

**File:** `src/core/src/config_manager.cpp:33-41`
**Issue:** `SHGetFolderPathW(..., CSIDL_APPDATA, ...)` has been the deprecated
form since Vista; Microsoft recommends `SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)`.
CSIDL values are capped at 32767 chars and don't support the new known-folder
semantics (per-user redirection, etc.). Works today on all supported Windows
versions; future-proofing suggestion only.

**Fix:** Migrate to `SHGetKnownFolderPath` with `FOLDERID_RoamingAppData`, or
explicitly document the deprecation decision.

---

### IN-02: `RemoveSystemTray` is dead code

**File:** `apps/micmap/main.cpp:179`
**Issue:** `void RemoveSystemTray() { Shell_NotifyIconW(NIM_DELETE, &g_app.nid); }`
is declared but never called — `shutdown()` inlines the
`Shell_NotifyIconW(NIM_DELETE, ...)` call on line 430. Dead symbol.

**Fix:** Either delete `RemoveSystemTray` or have `shutdown()` call it for
symmetry with `SetupSystemTray`.

---

### IN-03: `SetupSystemTray` ignores `Shell_NotifyIconW` return value

**File:** `apps/micmap/main.cpp:176`
**Issue:** `Shell_NotifyIconW(NIM_ADD, &g_app.nid)` can return `FALSE` (e.g.,
shell not ready, per-user context not fully loaded). The return is not
checked, so the later `NIM_DELETE` may fail silently with a "not installed"
icon that was never added.

**Fix:** Check the return and log a warning on failure so first-boot tray
issues surface in the log rather than as "balloon never fires".

---

### IN-04: `resolveManifestAbsolutePath` stack buffer is 64 KB

**File:** `src/steamvr/src/manifest_registrar.cpp:170`
**Issue:** `WCHAR buf[32768]` allocates 65,536 bytes on the stack. Default
Windows thread stack is 1 MB so this is safe today, but if the retry thread
is ever recreated with a small stack or this function is called from an
ImGui callback on a worker-pool thread with reduced stack, it could
overflow. The Microsoft `PATHCCH_MAX_CCH` constant (32767) is the canonical
size bound, and stack-allocating is documented as acceptable in the
`PathCch*` reference. Acceptable for production; note for future
maintainers.

**Fix:** Heap-allocate with `std::unique_ptr<WCHAR[]>` for defensive-programming
alignment, or leave as-is with a comment explicitly noting the default-stack
assumption.

---

### IN-05: Retry thread log for "unexpected VRInitError" concatenates a `const char*` after the format string

**File:** `apps/micmap/main.cpp:269-270`
**Issue:** Harmless functionally (matches the stream-concat logger pattern
correctly), but the message `"manifest retry VR_Init unexpected error: "`
followed by the enum description produces a two-space-free run-on in the
log. Compare to `manifest_registrar.cpp:203` which formats similar messages
with consistent spacing. Style nit.

**Fix:** Add a terminal space or formatting delimiter: `"manifest retry VR_Init unexpected error: " << desc`.

---

### IN-06: `config_manager.cpp::backupAndRotate` sorts backups lexicographically with `std::greater<>`

**File:** `src/core/src/config_manager.cpp:296`
**Issue:** `std::sort(backups.begin(), backups.end(), std::greater<>())`
sorts `fs::path` lexicographically, which happens to produce
newest-first ordering only because the timestamp format is
`YYYYMMDD-HHMMSS` (zero-padded, lexicographic === chronological). If the
timestamp format ever changes (e.g., a RFC 3339 ISO-8601 form with `T`
separator), this invariant silently breaks. Test 5 in `test_config_manager.cpp`
currently asserts `count == 5` without checking *which* five survive, so
the bug would not be caught.

**Fix:** Either leave a comment documenting the lex-sort-equals-chronological
invariant, OR strengthen the test to assert the oldest two timestamps are
gone (e.g., check that `20250100-000000` and `20250101-000000` are
removed but `20250106-000000` survives).

---

### IN-07: `StubVRInput::notifyEvent` is unreachable

**File:** `src/steamvr/src/vr_input.cpp:80-90`
**Issue:** The protected `notifyEvent` method on `StubVRInput` has no
internal caller (pollEvents is a no-op). Dead code in the stub. Not a
defect — the stub just can't generate events without a way to inject them.

**Fix:** Either delete `StubVRInput::notifyEvent`, or document that it's an
injection seam for tests that subclass `StubVRInput`.

---

### IN-08: `detectionBuf[128]` is allocated even when not used

**File:** `apps/micmap/main.cpp:581,588`
**Issue:** `char detectionBuf[128]` is declared unconditionally but only
populated in the `detected` branch. Harmless stack use; noting because the
nearby code (`detectionText`) switches between string literals and the
buffer, which makes the buffer's lifetime boundary subtle for future
refactors.

**Fix:** Scope `detectionBuf` inside the `else if (detected)` branch:
```cpp
} else if (detected) {
    char detectionBuf[128];
    // ... existing body
}
```

---

_Reviewed: 2026-04-23T22:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

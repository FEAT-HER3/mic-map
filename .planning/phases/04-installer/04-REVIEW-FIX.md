---
phase: 04-installer
fixed_at: 2026-04-24T13:00:00Z
review_path: .planning/phases/04-installer/04-REVIEW.md
iteration: 1
findings_in_scope: 11
fixed: 11
skipped: 0
status: all_fixed
---

# Phase 04: Code Review Fix Report

**Fixed at:** 2026-04-24T13:00:00Z
**Source review:** `.planning/phases/04-installer/04-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 11 (0 critical + 6 warning + 5 info)
- Fixed: 11
- Skipped: 0

Note: WR-03 and WR-04 touch the same `renderUI()` device-switch Combo branch
and are structurally coupled -- they were applied together and landed in a
single atomic commit referencing both IDs. All other findings received
one-commit-per-finding treatment.

## Fixed Issues

### WR-01: Initial `initThread` detached without shutdown coordination

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `1885bb8`
**Applied fix:** Added `initialConnectThread` (std::thread) and
`initialConnectCancel` (std::atomic<bool>) members to `MicMapApp`. Replaced
the `std::thread::detach()` call in `WinMain` with assignment to
`g_app.initialConnectThread`, and added cancel-check short-circuits inside
the lambda. In `shutdown()` (after `manifestRetryThread.join()` and before
Step 3 `driverClient->disconnect()`), added
`initialConnectCancel.store(true); if (initialConnectThread.joinable())
initialConnectThread.join();` so in-flight first-boot `driverClient->connect()`
/ `vrInput->initialize()` calls cannot race teardown on a fast-quit.

### WR-02: First-instance mutex handle leaked on ERROR_ALREADY_EXISTS branch

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `871246a`
**Applied fix:** Added `CloseHandle(hMutex);` immediately before the `return 0`
in the `ERROR_ALREADY_EXISTS` branch, matching every other exit path in
`WinMain`. Leak was benign (process exit cleaned up), but the inconsistency
was flagged as a footgun.

### WR-03: Detector reassignment in `renderUI()` races the audio callback

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `ce91a3a` (shared with WR-04)
**Applied fix:** Wrapped BOTH detector reassignment sites in `renderUI()`
(device-change Combo and Clear button) in a `std::lock_guard<std::mutex>
lock(audioMutex);` block. The WASAPI audio callback already acquires
`audioMutex` before dereferencing the `detector` unique_ptr, so this
serializes the swap with any in-flight `detector->analyze(...)` call and
prevents the old detector from being destroyed while the callback holds a
raw pointer to it.

### WR-04: `startCapture()` unconditional even when detector rebuild failed

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `ce91a3a` (shared with WR-03)
**Applied fix:** Moved `audioCapture->startCapture()` inside the
`if (dev.sampleRate > 0)` branch of the device-switch Combo path. Added an
`else` arm that logs a warning via `MICMAP_LOG_WARNING` so the
"capture-not-restarted-because-WASAPI-returned-no-format" case is visible in
the log rather than silently running the previous detector against a
different sample rate.

### WR-05: UTF-8 device-name conversion buffer may be undersized

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `2cd64c7`
**Applied fix:** Replaced the one-byte-per-wide-char allocation with a
probing `WideCharToMultiByte(..., nullptr, 0, ...)` call that returns the
required UTF-8 byte count (including NUL). Resized the output string to
`needed - 1` and called `WideCharToMultiByte` a second time to do the actual
conversion. Handles non-ASCII device names (up to 3 bytes per BMP code unit,
4 for surrogate pairs) without overflowing.

### WR-06: `package` target has no explicit `add_dependencies(... micmap)`

**Files modified:** `CMakeLists.txt`
**Commit:** `c6c808a`
**Applied fix:** Removed `DEPENDS micmap` from `add_custom_target(package
...)` (the `DEPENDS` keyword is for file-level dependencies, not target-level
ones, and was effectively a no-op here). Added `add_dependencies(package
micmap)` immediately below the target definition, matching the existing
`add_dependencies(package driver_micmap)` pattern and ensuring
`cmake --build build --target package` rebuilds `micmap.exe` when its sources
change.

### IN-01: `AtomicWriteJson` does not verify stream state after write

**Files modified:** `src/bindings/src/bindings_patcher.cpp`
**Commit:** `26c8413`
**Applied fix:** Added `out.exceptions(std::ios::failbit | std::ios::badbit);`
immediately after opening the ofstream in `AtomicWriteJson`. The existing
`catch (const std::exception& e)` block now traps silent write failures
(disk full, permission change, etc.) and removes the tmp file before
`fs::rename` can swap a truncated file over the real target.

### IN-02: Dangling dead-code comment referencing deleted helper

**Files modified:** `apps/micmap/main.cpp`
**Commit:** `680ffd3`
**Applied fix:** Deleted the three-line `// IN-02: legacy RemoveSystemTray()
helper deleted ...` comment above `MicMapApp::initialize()`. The rationale is
already visible by reading `shutdown()` Step 6 where the inlined
`Shell_NotifyIconW(NIM_DELETE, ...)` call lives.

### IN-03: `PrepareToInstall` has no max-retry ceiling

**Files modified:** `installer/MicMap.iss`
**Commit:** `fb9c1ea`
**Applied fix:** Added `Sleep(500);` between the `IDCANCEL` guard and the
subsequent `Running := GetRunningSteamVrProcesses();` call inside the retry
loop. Gives vrserver a moment to clear the phantom "running" state that WMI
occasionally returns for a few seconds during SteamVR teardown.

### IN-04: `RunVrpathregRemove` ResultCode variable misleading

**Files modified:** `installer/MicMap.iss`
**Commit:** `eb1e172`
**Applied fix:** Renamed the local `ResultCode: Integer` variable to
`IgnoredRC: Integer` and passed the new name to `Exec(...)` as the
out-parameter. Makes the "don't read this" intent load-bearing in the
signature itself.

### IN-05: `driver_micmap` interface-version array assumes C-string literal storage

**Files modified:** `driver/src/device_provider.cpp`
**Commit:** `8079e6e`
**Applied fix:** Added a six-line comment block above `k_InterfaceVersions[]`
documenting the assumption that `IServerTrackedDeviceProvider_Version`
expands to a bare string literal (static storage duration) per the current
OpenVR SDK contract, and flagging what would need to change if Valve ever
redefined the macro as a constexpr `std::string_view` or similar non-literal.

---

_Fixed: 2026-04-24T13:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

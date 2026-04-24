---
phase: 04-installer
reviewed: 2026-04-24T12:00:00Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - CMakeLists.txt
  - apps/micmap/CMakeLists.txt
  - apps/micmap/main.cpp
  - apps/micmap/micmap.rc
  - driver/CMakeLists.txt
  - driver/src/device_provider.cpp
  - installer/MicMap.iss
  - installer/micmap.ico
  - src/CMakeLists.txt
  - src/bindings/CMakeLists.txt
  - src/bindings/include/micmap/bindings/bindings_patcher.hpp
  - src/bindings/src/bindings_patcher.cpp
  - src/common/include/micmap/common/cli_flags.hpp
  - src/common/src/cli_flags.cpp
  - tests/CMakeLists.txt
  - tests/test_bindings_patcher.cpp
  - tests/test_cli_flags_parse.cpp
findings:
  critical: 0
  warning: 6
  info: 5
  total: 11
status: issues_found
---

# Phase 04: Code Review Report

**Reviewed:** 2026-04-24T12:00:00Z
**Depth:** standard
**Files Reviewed:** 17 (16 source + 1 binary icon asset)
**Status:** issues_found

## Summary

Fresh standard-depth review of Phase 04 installer integration after prior fixes (IN-02, LR-01, MR-01, MR-02) were committed. Those fixes are verified in place:

- `PromptAndMaybeRemoveUserData` logs the early-exit when `%APPDATA%\MicMap` does not exist (MicMap.iss:381).
- `SweepLegacyBindings` skips directories via `FILE_ATTRIBUTE_DIRECTORY` mask before calling `DeleteFile` (MicMap.iss:351).
- `CurUninstallStepChanged` re-derives `g_SteamVRDir` from `{app}` when empty so `vrpathreg removedriver` actually runs at uninstall (MicMap.iss:436-441).
- The double `FileExists(MicMapExe)` gate in `CurUninstallStepChanged` was collapsed to a single check wrapping both micmap.exe-driven teardown steps (MicMap.iss:450-459).

No critical security or correctness issues. Findings are concentrated in `apps/micmap/main.cpp` (mostly pre-existing lifecycle hazards unrelated to the installer itself but in review scope because the file is listed) and a handful of small robustness gaps in the Inno Setup script and the bindings patcher. The installer orchestration (Plans 05-08) is well-structured: WMI fail-open, `ewWaitUntilTerminated` + ResultCode inspection per Pitfall 17, ISPP-safe CRLF, `WizardSilent()` guard in silent-mode uninstall, write-once backup invariant, and atomic tmp-rename JSON writes all look correct.

Binary asset `installer/micmap.ico` (6.4 KB ICO, referenced by `micmap.rc` and `SetupIconFile=` in the .iss) is out of source-review scope.

## Warnings

### WR-01: Initial `initThread` detached without shutdown coordination

**File:** `apps/micmap/main.cpp:826-834`
**Issue:** The first-boot async init thread is detached and has no cancellation or join path. `shutdown()` explicitly waits on `driverConnectFuture` / `vrInitFuture` (WR-05) before tearing down `driverClient` / `vrInput`, but the detached `initThread` created at WinMain can still be mid-`driverClient->connect()` or `vrInput->initialize()` when teardown runs. Unlike the std::async-based reconnect futures, this `std::thread::detach()` provides no join handle, so there is no way for `shutdown()` to wait on it. On a fast user-exit (quit immediately after launch), this is a real race against `disconnect()` / `shutdown()`.
**Fix:** Track the initial init thread on `MicMapApp` like the manifest retry thread and join it in `shutdown()` before Step 3 (`driverClient->disconnect()`):
```cpp
// in MicMapApp
std::thread initialConnectThread;
std::atomic<bool> initialConnectCancel{false};

// at WinMain (replacing detach)
g_app.initialConnectThread = std::thread([]() {
    if (g_app.initialConnectCancel.load()) return;
    if (g_app.driverClient) g_app.driverClient->connect();
    if (g_app.initialConnectCancel.load()) return;
    if (g_app.vrInput) g_app.vrInput->initialize();
});

// in shutdown() AFTER manifestRetryThread.join() and BEFORE step 3:
initialConnectCancel.store(true);
if (initialConnectThread.joinable()) initialConnectThread.join();
```

### WR-02: First-instance mutex handle leaked on ERROR_ALREADY_EXISTS branch

**File:** `apps/micmap/main.cpp:761-770`
**Issue:** `CreateMutexW` always returns a valid handle (even when `GetLastError() == ERROR_ALREADY_EXISTS`); the returned handle must be closed by every owner, including the second instance that detected ERROR_ALREADY_EXISTS. The `return 0` at line 769 does not call `CloseHandle(hMutex)`, leaking a kernel handle per second-instance invocation. This is benign at process scope (exit cleans it up), but every other exit path in WinMain does close it, so the inconsistency is a footgun.
**Fix:**
```cpp
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (!flags.minimized) {
        HWND w = FindWindowW(L"MicMapMain", nullptr);
        if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
    }
    CloseHandle(hMutex);  // match the cleanup on every other exit path
    return 0;
}
```

### WR-03: Detector reassignment in `renderUI()` races the audio callback

**File:** `apps/micmap/main.cpp:524-535` (device-change Combo) and `apps/micmap/main.cpp:584-592` (Clear button)
**Issue:** Both sites reassign `detector` (a `unique_ptr`) without holding `audioMutex`. `stopCapture()` is called first in the Combo path, which may or may not block until the WASAPI callback has exited (depends on `IAudioCapture::stopCapture` semantics — not visible in this review's scope). The Clear button at 584 does not even call `stopCapture()` before swapping the detector. The audio callback at line 337 acquires `audioMutex` and then dereferences `detector->analyze(...)` — if the pointer is swapped mid-call, the previously-held raw pointer may still be in use, and the destructor of the old detector runs on the UI thread while the callback is using it. Classic data race on a non-atomic `unique_ptr`.
**Fix:** Acquire `audioMutex` around every `detector` assignment in `renderUI()`:
```cpp
// Clear button
if (ImGui::Button("Clear", ImVec2(60, 30)) && detector) {
    auto dev = audioCapture->getCurrentDevice();
    if (dev.sampleRate > 0) {
        std::lock_guard<std::mutex> lock(audioMutex);
        detector = detection::createFFTDetector(dev.sampleRate);
        detector->setMinDetectionDuration(detectionTimeMs);
    }
    hasProfile = false;
    trainingSampleCount = 0;
}
```
Apply the same lock around the Combo-path reassignment at line 529.

### WR-04: `startCapture()` unconditional even when detector rebuild failed

**File:** `apps/micmap/main.cpp:524-535`
**Issue:** Inside the device-change Combo branch, if `dev.sampleRate <= 0` (WASAPI returned no format), the `detector = detection::createFFTDetector(...)` block is skipped but `audioCapture->startCapture()` is still called unconditionally at line 533. The audio callback will then fire against the *previous* detector that was configured for a different sample rate, producing bogus confidence values until the user changes device again.
**Fix:**
```cpp
auto dev = audioCapture->getCurrentDevice();
if (dev.sampleRate > 0) {
    std::lock_guard<std::mutex> lock(audioMutex);
    detector = detection::createFFTDetector(dev.sampleRate);
    detector->setMinDetectionDuration(detectionTimeMs);
    if (configManager) detector->loadTrainingData(configManager->getTrainingDataPath());
    audioCapture->startCapture();
} else {
    MICMAP_LOG_WARNING("Device switch: new device reported sampleRate=0; capture NOT restarted");
}
```

### WR-05: UTF-8 device-name conversion buffer may be undersized

**File:** `apps/micmap/main.cpp:515-518`
**Issue:** `std::string name(d.name.length(), '\0')` allocates one byte per *wide-char code unit* in the device name. UTF-8 can require up to 3 bytes per BMP code unit (4 for non-BMP surrogate pairs), so a device with any non-ASCII characters can overflow the output buffer passed to `WideCharToMultiByte(..., &name[0], (int)name.size()+1, ...)`. The function returns 0 and sets `ERROR_INSUFFICIENT_BUFFER`, but the code ignores the return value and then `name.resize(strlen(name.c_str()))` sizes to whatever garbage happens to be there (possibly reading past the buffer).
**Fix:** Size the buffer via a first call to `WideCharToMultiByte` with `cbMultiByte = 0`, which returns the required byte count:
```cpp
int needed = WideCharToMultiByte(CP_UTF8, 0, d.name.c_str(), -1, nullptr, 0, nullptr, nullptr);
std::string name;
if (needed > 0) {
    name.resize(static_cast<size_t>(needed - 1));  // -1 drops the NUL
    WideCharToMultiByte(CP_UTF8, 0, d.name.c_str(), -1, name.data(), needed, nullptr, nullptr);
}
names.push_back(std::move(name));
```
A real user hits this if their audio device name contains non-ASCII (e.g. a Japanese mic product name). Mitigation is local and small.

### WR-06: `package` target has no explicit `add_dependencies(... micmap)`

**File:** `CMakeLists.txt:150`
**Issue:** The `add_custom_target(package ... DEPENDS micmap ...)` form uses `DEPENDS` which in CMake semantics refers to *file-level* dependencies, not targets. For target-level dependencies, the correct API is `add_dependencies(package micmap)` — which the file already uses for `driver_micmap` at line 156. As written, `cmake --build build --target package` may not force a rebuild of `micmap.exe` if only its sources changed, because the target-level edge is missing. In practice the `--install` step at line 133 will still lay down whatever `micmap.exe` exists in the build tree, but out-of-date binaries can slip into the installer.
**Fix:**
```cmake
add_custom_target(package
    ...
    # Remove `DEPENDS micmap` from the add_custom_target line — it's a no-op here.
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building MicMap-Setup-v${PROJECT_VERSION}.exe"
    VERBATIM
)
add_dependencies(package micmap)
if(MICMAP_BUILD_DRIVER AND OpenVR_FOUND)
    add_dependencies(package driver_micmap)
endif()
```

## Info

### IN-01: `AtomicWriteJson` does not verify stream state after write

**File:** `src/bindings/src/bindings_patcher.cpp:234-244`
**Issue:** `std::ofstream::operator<<` does not throw by default; a failed write (disk full, permission change mid-write, etc.) silently sets failbit on the stream. The current code catches `std::exception` but a successful-appearing write that actually failed will fall through and the subsequent `fs::rename` will swap a truncated/empty tmp over the real target. The `AtomicWriteJsonCrashSafety` test only exercises the rename path, not the write-failure path.
**Fix:** Check `out.good()` before closing, and optionally call `out.exceptions(std::ios::failbit | std::ios::badbit)`:
```cpp
std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
out.exceptions(std::ios::failbit | std::ios::badbit);
out << j.dump(4);
out.close();
```

### IN-02: Dangling dead-code comment referencing deleted helper

**File:** `apps/micmap/main.cpp:199-200`
**Issue:** The comment `// IN-02: legacy RemoveSystemTray() helper deleted — shutdown() inlines ...` is a historical fix marker that no longer adds value now that the code has shipped. Future readers get the same information by reading `shutdown()` directly. Not wrong, just cruft.
**Fix:** Remove the comment or move the rationale into `shutdown()` step 6 where the inlined logic now lives.

### IN-03: `PrepareToInstall` has no max-retry ceiling

**File:** `installer/MicMap.iss:202-215`
**Issue:** The while-loop prompts the user indefinitely as long as WMI reports a SteamVR process is present. WMI occasionally returns a phantom "running" state during vrserver teardown (process in WAITING_FOR_EXIT state for a few seconds after the user closes SteamVR). A user hitting Retry too quickly gets the same dialog. Not a bug per se — Cancel always works — but a small sleep between retries or a retry counter with a "force continue" escape hatch would be friendlier.
**Fix:** Optional polish; add a 500 ms sleep after the Retry response before the next WMI query:
```pascal
if Response = IDCANCEL then
begin
  Result := 'Setup was cancelled because SteamVR is still running.';
  Exit;
end;
Sleep(500);  // give vrserver a moment to clear on a racing shutdown
Running := GetRunningSteamVrProcesses();
```

### IN-04: `RunVrpathregRemove` ResultCode deliberately ignored — comment is clear but the `ResultCode` variable is still declared

**File:** `installer/MicMap.iss:240-249`
**Issue:** `ResultCode` is declared in `RunVrpathregRemove` only because `Exec` requires an out-parameter. The variable is never read, which is expected (rc ignored per Pitfall 3), but a future reader may assume the value matters. Consider renaming the variable `IgnoredRC` to make the intent load-bearing in the signature itself.
**Fix:** Cosmetic only:
```pascal
procedure RunVrpathregRemove(AppDir: String);
var
  IgnoredRC: Integer;
begin
  if VrpathregExists() then
    Exec(GetVrpathreg(''), 'removedriver "' + AppDir + '"', '', SW_HIDE, ewWaitUntilTerminated, IgnoredRC);
end;
```

### IN-05: `driver_micmap` interface-version array assumes C-string literal storage

**File:** `driver/src/device_provider.cpp:41-44`
**Issue:** `k_InterfaceVersions[]` is an array of `const char* const` pointing at `IServerTrackedDeviceProvider_Version` (a macro expanding to a string literal in the OpenVR SDK). Storage-duration-wise this is fine — string literals have static storage duration — so the returned array from `GetInterfaceVersions()` is valid for the process lifetime. Flagging only because if the macro is ever redefined to a non-literal (e.g. a `constexpr std::string_view`), the array would silently store a dangling pointer. Not actionable today; document the assumption in a comment.
**Fix:** Add a 1-line assumption comment:
```cpp
// Assumes IServerTrackedDeviceProvider_Version is a string literal (OpenVR
// SDK contract). If Valve ever changes it to a non-literal, this array must
// be rebuilt per-call.
static const char* const k_InterfaceVersions[] = { ... };
```

---

_Reviewed: 2026-04-24T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

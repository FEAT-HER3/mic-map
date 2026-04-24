---
phase: 04-installer
reviewed: 2026-04-24T14:30:00Z
depth: standard
iteration: 2
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
  warning: 2
  info: 5
  total: 7
status: issues_found
---

# Phase 04: Code Review Report (Iteration 2)

**Reviewed:** 2026-04-24T14:30:00Z
**Depth:** standard
**Iteration:** 2 (first pass: 11 findings, all fixed per 04-REVIEW-FIX.md)
**Files Reviewed:** 17 (16 source + 1 binary icon asset skipped)
**Status:** issues_found

## Summary

Second-pass review of Phase 04 after iteration-1's 11 findings were fixed
(per 04-REVIEW-FIX.md, commits 1885bb8 / 871246a / ce91a3a / 2cd64c7 /
c6c808a / 26c8413 / 680ffd3 / fb9c1ea / eb1e172 / 8079e6e). All prior fixes
are verified in place and correctly applied:

- **WR-01 verified:** `MicMapApp::initialConnectThread` + `initialConnectCancel`
  declared (main.cpp:119-120), `std::thread` assignment replaces detach at
  WinMain (main.cpp:876-885), cancel+join ordered before step 3 in
  `shutdown()` (main.cpp:457-462).
- **WR-02 verified:** `CloseHandle(hMutex)` before `return 0` on the
  `ERROR_ALREADY_EXISTS` branch (main.cpp:815).
- **WR-03 verified:** Both device-switch Combo (main.cpp:554-559) and Clear
  button (main.cpp:627-630) now wrap detector reassignment in
  `std::lock_guard<std::mutex> lock(audioMutex)`.
- **WR-04 verified:** `audioCapture->startCapture()` moved inside the
  `dev.sampleRate > 0` branch with a logged-warning else arm (main.cpp:565,
  567).
- **WR-05 verified:** Probing `WideCharToMultiByte(..., nullptr, 0, ...)` call
  sizes the UTF-8 buffer correctly (main.cpp:531-538).
- **WR-06 verified:** `add_dependencies(package micmap)` at CMakeLists.txt:159;
  `DEPENDS micmap` removed from `add_custom_target` body.
- **IN-01 verified:** `out.exceptions(std::ios::failbit | std::ios::badbit)`
  at bindings_patcher.cpp:240; tmp-cleanup in catch block.
- **IN-02 verified:** Dangling `// IN-02: legacy RemoveSystemTray() helper
  deleted ...` comment removed above `MicMapApp::initialize()`.
- **IN-03 verified:** `Sleep(500)` between `IDCANCEL` guard and next WMI
  poll (MicMap.iss:218).
- **IN-04 verified:** `ResultCode` renamed to `IgnoredRC` (MicMap.iss:247).
- **IN-05 verified:** six-line assumption comment above `k_InterfaceVersions[]`
  (device_provider.cpp:41-46).

Iteration 2 surfaces a small number of **new** issues that were out of scope
or missed during iteration 1. The most material is WR-07 — the WR-03 fix
covered two detector reassignment sites but left five other UI-thread
`detector->*` call-sites still racing the audio callback. This is the same
data-race class flagged by WR-03, just at different sites. The rest of the
new findings are defense-in-depth nits in the shared bindings library
(Unicode path handling via `std::getenv` + `fs::path(std::string)` on
Windows), a fragile first-boot reconnect overlap, and a `ctest -j` hazard
in `test_bindings_patcher`.

Binary asset `installer/micmap.ico` is skipped per review policy.

## Warnings

### WR-07: Five UI-thread `detector->*` call-sites still race the audio callback

**File:** `apps/micmap/main.cpp:579`, `588-594`, `600-608`, `613-616`, `628-629`
**Issue:** Iteration 1 WR-03 correctly identified that the audio callback
(main.cpp:342-433) dereferences `detector` under `audioMutex`, and wrapped
the two *reassignment* sites in `renderUI()` under the same lock. However,
the WR-03 fix did **not** cover the other UI-thread `detector->*` calls that
mutate detector state concurrently with the audio thread's in-flight
`detector->analyze(...)` / `detector->addTrainingSample(...)`:

- `detector->setMinDetectionDuration(detectionTimeMs)` (line 579, slider)
- auto-stop training: `detector->finishTraining()` +
  `detector->saveTrainingData(...)` (lines 590-594)
- Stop Training button: `detector->finishTraining()` +
  `detector->saveTrainingData(...)` (lines 600-608)
- Train Pattern button: `detector->startTraining()` (line 614)
- Clear button (lock covers reassignment but not the state read at line 619's
  `&& detector` gate — acceptable; main concern is the four above)

These are the same data-race class as WR-03: the audio callback holds
`audioMutex` before touching `detector`, but these UI-thread sites do not,
so in-flight `addTrainingSample` / `analyze` on the audio thread can race
`finishTraining` / `startTraining` / `setMinDetectionDuration` on the UI
thread. Concrete failure mode: user clicks Stop Training mid-`analyze()` →
`finishTraining()` mutates the FFT detector's internal pattern while the
callback is computing a confidence score against it → UB (torn reads on
`std::vector` / FFT scratch buffers).
**Fix:** Wrap each remaining UI-thread `detector->*` access in the same
`std::lock_guard<std::mutex> lock(audioMutex)` pattern used for WR-03. The
audio callback holds the lock for the duration of one `analyze()` (sub-ms),
so UI-thread contention is negligible:
```cpp
// line 579 (slider)
if (ImGui::SliderInt("##Time", &detectionTimeMs, 100, 1000, "")) {
    if (detector) {
        std::lock_guard<std::mutex> lock(audioMutex);
        detector->setMinDetectionDuration(detectionTimeMs);
    }
    if (configManager) configManager->getConfig().detection.minDurationMs = detectionTimeMs;
}

// lines 588-596 (auto-stop training)
if (isTraining && trainingSampleCount >= MIN_TRAINING_SAMPLES * 3) {
    if (detector) {
        bool success;
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            success = detector->finishTraining();
        }
        isTraining = false;
        if (success) {
            hasProfile = true;
            if (configManager) {
                std::lock_guard<std::mutex> lock(audioMutex);
                detector->saveTrainingData(configManager->getTrainingDataPath());
            }
        }
    }
}

// lines 613-616 (Train Pattern button)
if (ImGui::Button("Train Pattern", ImVec2(120, 30)) && detector) {
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        detector->startTraining();
    }
    isTraining = true;
    trainingSampleCount = 0;
}
```
(Same pattern for the Stop Training button at 600-608.)
Structurally identical to WR-03; skipping these leaves the fix incomplete.

### WR-08: First-boot reconnect overlap — `initialConnectThread` and main-loop `std::async` can run `driverClient->connect()` concurrently

**File:** `apps/micmap/main.cpp:876-885` (initial thread) + `931-938` (main-loop async)
**Issue:** After the WR-01 fix, `initialConnectThread` calls
`driverClient->connect()` (line 879) and `vrInput->initialize()` (line 883)
in sequence. Meanwhile the main message loop at line 927 enters a reconnect
check every `reconnectInterval` ticks (40 ticks = ~2 s normally) and
launches `std::async(std::launch::async, []() { g_app.driverClient->connect(); })`
whenever `driverClient->isConnected()` returns false. On first boot:

1. `initialConnectThread` starts `driverClient->connect()` (several seconds
   if the driver HTTP server is not yet listening).
2. After ~2 s of main-loop ticks, the reconnect guard fires. It only checks
   `driverConnectFuture.valid()` (which is still default-constructed = not
   valid on first entry) and `driverClient->isConnected()` (which is false
   because the initial thread hasn't finished yet).
3. The main loop launches a *second* concurrent `driverClient->connect()`
   call on a new async thread.

Two simultaneous `connect()` calls on the same `IDriverClient` instance.
Whether this is safe depends on the implementation's internal locking (not
visible in this review's file set), but the ambiguity is a hazard — the
existing code only guards against overlap of main-loop-initiated connects,
not against the initial thread's in-flight work.
**Fix:** Gate the reconnect branch on the initial thread having completed,
or seed `driverConnectFuture` / `vrInitFuture` from the initial thread so
the main-loop guard naturally serializes. Minimal approach — wrap the
initial thread's work in a `std::packaged_task` and assign its future to
`driverConnectFuture` before launching, so the main loop's
`driverConnectFuture.valid() && wait_for(0) != ready` guard
already recognizes it as in-flight:
```cpp
// WinMain, replacing the detached-thread-replacement block:
std::packaged_task<void()> initTask([]() {
    if (g_app.initialConnectCancel.load()) return;
    if (g_app.driverClient) g_app.driverClient->connect();
    if (g_app.initialConnectCancel.load()) return;
    if (g_app.vrInput) g_app.vrInput->initialize();
});
g_app.driverConnectFuture = initTask.get_future();  // reuse main-loop guard
g_app.initialConnectThread = std::thread(std::move(initTask));
```
Lower-effort mitigation: skip the main-loop reconnect for the first
`reconnectInterval * 3` ticks (~6 s grace window) — but that's a timing
hack, not a structural fix.

## Info

### IN-06: `std::getenv("LOCALAPPDATA")` + `fs::path(std::string)` munges non-ASCII user profile paths

**File:** `src/bindings/src/bindings_patcher.cpp:192`, `221-222`
**Issue:** On Windows, `std::getenv` returns an *ANSI* (active code page) copy
of the environment variable, not UTF-8 / UTF-16. If the user's
`%LOCALAPPDATA%` contains characters outside the current ACP (e.g. a Windows
user profile with accented or CJK characters: `C:\Users\Jörg\AppData\Local`),
those characters are replaced with `?` or similar in the returned string.
The subsequent `fs::path(localAppData) / "openvr" / "openvrpaths.vrpath"`
then points at a nonexistent path, `fs::exists` returns false, and the
installer silently fails to resolve the SteamVR config dir — with the
misleading log line `openvrpaths.vrpath not found at <mangled path>`.

Same issue at line 221: `j["runtime"][0].get<std::string>()` extracts a
UTF-8 string from the JSON (nlohmann/json is UTF-8 natively), then passes
it to `fs::path(const std::string&)` which on Windows interprets it as
ACP — mojibake for any SteamVR runtime path under a non-ASCII user profile.
**Fix:** Use `GetEnvironmentVariableW` + `fs::path(const wchar_t*)` on
Windows, and `fs::u8path(runtime)` for the JSON string:
```cpp
#ifdef _WIN32
wchar_t lad[MAX_PATH];
DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", lad, MAX_PATH);
if (n == 0 || n >= MAX_PATH) {
    LogFmt(log, "MicMap[patch]: LOCALAPPDATA not set or too long\n");
    return {};
}
fs::path pathsFile = fs::path(lad) / L"openvr" / L"openvrpaths.vrpath";
#endif

// line 221-222:
const std::string runtime = j["runtime"][0].get<std::string>();
return fs::u8path(runtime) / "resources" / "config";
```
Low probability (most users have ASCII profiles), but when it hits the
failure is silent and misdiagnosable as "SteamVR not installed."

### IN-07: `test_bindings_patcher` shares `micmap_test_bindings` tmp dir across ctest invocations

**File:** `tests/test_bindings_patcher.cpp:47-53`
**Issue:** `resetTmpDir()` computes `fs::temp_directory_path() /
"micmap_test_bindings"` — a fixed name with no per-process or per-scenario
suffix. Running `ctest -j N` with `test_bindings_patcher` and
`bindings_patcher_idempotent` (both registered in tests/CMakeLists.txt:107-109)
concurrently will race on the same directory: one test's `fs::remove_all`
clobbers the other's in-progress scenario, producing spurious FAIL output.
Current CI likely runs sequentially, but a developer running `ctest -j8`
locally may see flake.
**Fix:** Append the PID (or a per-scenario name) to the directory:
```cpp
static fs::path resetTmpDir(const char* scenarioName = "default") {
    auto tmp = fs::temp_directory_path()
             / ("micmap_test_bindings_"
                + std::to_string(
#ifdef _WIN32
                    GetCurrentProcessId()
#else
                    getpid()
#endif
                  )
                + "_" + scenarioName);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    return tmp;
}
```

### IN-08: `CreateMutexW` return value not checked before `GetLastError`

**File:** `apps/micmap/main.cpp:802-803`
**Issue:** `CreateMutexW` can return `NULL` on failure (e.g. security-descriptor
errors, though rare for an unnamed-ACL named mutex). The code reads
`GetLastError()` unconditionally at line 803; if `hMutex == NULL`, the subsequent
`CloseHandle(hMutex)` on every exit path is a harmless no-op on Windows, but
the GetLastError value may not be `ERROR_ALREADY_EXISTS` and the code falls
through to GUI init with a NULL hMutex — single-instance gate broken.
Low-severity (real-world `CreateMutexW` rarely fails on fresh-process Win32),
but a 2-line null-check would make the contract explicit.
**Fix:**
```cpp
HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
if (!hMutex) {
    MICMAP_LOG_ERROR("CreateMutexW failed: ", GetLastError());
    return 1;  // no single-instance gate; bail rather than boot a second instance
}
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    ...
}
```

### IN-09: `CurUninstallStepChanged` re-derivation of `g_SteamVRDir` is fragile if user picked a non-default `{app}`

**File:** `installer/MicMap.iss:443-448`
**Issue:** The MR-01 fix (iteration-0) re-derives `g_SteamVRDir` from `{app}`
at uninstall time via two applications of `ExtractFilePath`. This assumes
`{app} == {SteamVR}\drivers\micmap` — which holds for fresh installs because
`DefaultDirName={code:GetMicMapInstallDir}` computes that path and
`DisableDirPage=yes` prevents the user from changing it. However,
`UsePreviousAppDir=yes` means an upgrade over a 0.x install (hypothetical —
D-07 voids 0.x as ever-shipped, but the flag enables upgrade-over-manual-
install scenarios) could pick up a non-standard `{app}` path, and the
two-parents-up extraction would silently derive a wrong `g_SteamVRDir` for
the `vrpathreg removedriver` call.
**Fix:** Cosmetic polish — verify the derived path actually exists and
contains `bin\win64\vrpathreg.exe` before using it, or emit a log line
showing the derived `g_SteamVRDir` for post-mortem diagnosis (the existing
`Log('Uninstall: resolved g_SteamVRDir ...')` at line 447 already does this,
which is good; consider also asserting `DirExists(g_SteamVRDir + '\bin\win64')`
and failing the vrpathreg step cleanly rather than relying on
`VrpathregExists()` to implicitly detect the miss).

### IN-10: `AppDir` in `Exec(..., 'adddriver "' + AppDir + '"', ...)` is not escape-sanitized

**File:** `installer/MicMap.iss:255`, `267`, `472`
**Issue:** Three `Exec` sites interpolate `AppDir` (the `{app}` path) into a
quoted `vrpathreg` command-line argument without escaping embedded `"` or
`\`. In practice `{app}` is a Windows file-system path resolved from
`GetMicMapInstallDir` (`{SteamVR}\drivers\micmap`) and can only contain
characters Windows permits in a path — which excludes `"`. So the injection
surface is effectively empty on a real install.

Flagged as defense-in-depth: if a future refactor ever lets users influence
`{app}` through an installer task / UI field (the current `DisableDirPage=yes`
prevents that), the raw interpolation becomes a CLI-injection sink. A
one-liner `StringChangeEx(AppDir, '"', '""', True)` immediately before each
`Exec` closes the door preemptively. Pure Info; no current vulnerability.

---

_Reviewed: 2026-04-24T14:30:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 2_

---
phase: 04-installer
reviewed: 2026-04-24T16:00:00Z
depth: standard
iteration: 3
files_reviewed: 16
files_reviewed_list:
  - CMakeLists.txt
  - apps/micmap/CMakeLists.txt
  - apps/micmap/main.cpp
  - apps/micmap/micmap.rc
  - driver/CMakeLists.txt
  - driver/src/device_provider.cpp
  - installer/MicMap.iss
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
  warning: 1
  info: 3
  total: 4
status: issues_found
---

# Phase 04: Code Review Report (Iteration 3)

**Reviewed:** 2026-04-24T16:00:00Z
**Depth:** standard
**Iteration:** 3 (iterations 1 + 2 findings all closed per 04-REVIEW-FIX.md commits)
**Files Reviewed:** 16 source files
**Status:** issues_found

## Summary

Third-pass review of Phase 04 after iteration-2's seven findings were fixed
(427c898 WR-07, bb3010a WR-08, a09763e IN-06, 7f7b761 IN-07, bb8879b IN-08,
ba4bd24 IN-09, d6c7af4 IN-10). All prior fixes are verified in place:

- **WR-07 verified:** `main.cpp:584-587, 602-616, 619-635, 639-648` wrap every
  remaining UI-thread `detector->*` mutation under `audioMutex` (slider,
  auto-stop training, Stop Training button, Train Pattern button).
- **WR-08 verified:** `main.cpp:925-946` now uses two `std::packaged_task`s
  whose futures are seeded into `g_app.driverConnectFuture` /
  `g_app.vrInitFuture` before the thread is launched, so the main-loop
  reconnect guard recognizes in-flight initial work.
- **IN-06 verified:** `bindings_patcher.cpp:201-212` uses
  `GetEnvironmentVariableW` + `fs::path(wchar_t*)`; line 240-241 uses
  `fs::u8path(runtime)`.
- **IN-07 verified:** `test_bindings_patcher.cpp:65-77` appends PID to tmp dir.
- **IN-08 verified:** `main.cpp:839-842` null-checks `hMutex` before
  `GetLastError`.
- **IN-09 verified:** `MicMap.iss:467-468` logs a WARNING when derived
  `g_SteamVRDir` lacks `bin\win64`.
- **IN-10 verified:** `MicMap.iss:252-256` `QuoteExecArg` helper wired at
  the three vrpathreg `Exec` call-sites (lines 268, 280, 494).

This iteration surfaces a small number of **new** issues missed by iterations
1 and 2. The material finding (WR-09) is an actual bug in the MR-01 uninstall
`g_SteamVRDir` re-derivation: `ExtractFilePath` applied twice does NOT walk
two parents up the way the surrounding comment claims, because
`ExtractFilePath` on a string that already ends in a backslash is an
identity-return. The failure mode is silent: uninstall skips
`vrpathreg removedriver` (via the `VrpathregExists()` gate added in IN-09),
leaving the driver registered in `steamvr.vrpaths` after the files are
deleted — a cosmetic leftover, not a correctness regression, but inconsistent
with the stated MR-01 intent. Rest of the findings are defense-in-depth
nits around the new WR-08 packaged-task pattern and a pair of unchecked
Win32 return values whose failure modes are currently benign.

## Warnings

### WR-09: MR-01 `ExtractFilePath(ExtractFilePath(AppDir))` does NOT walk two parents up

**File:** `installer/MicMap.iss:458`
**Issue:** MR-01's iteration-1 fix re-derives `g_SteamVRDir` at uninstall time
via:
```pascal
SteamVRParent := ExtractFilePath(ExtractFilePath(AppDir));
g_SteamVRDir := RemoveBackslashUnlessRoot(SteamVRParent);
```
The accompanying comment at line 454 claims this walks "two parents up" from
`{SteamVR}\drivers\micmap` to `{SteamVR}`. It does not. Inno Setup /
Delphi's `ExtractFilePath` finds the last path delimiter (`\` or `:`) and
returns everything up to and including it. Applied twice:

1. `ExtractFilePath('C:\SteamVR\drivers\micmap')` → `'C:\SteamVR\drivers\'`
2. `ExtractFilePath('C:\SteamVR\drivers\')` → `'C:\SteamVR\drivers\'`
   (the last delimiter is the trailing backslash, so the function returns
   the input unchanged)

Final `RemoveBackslashUnlessRoot` strips the trailing backslash, yielding
`g_SteamVRDir = 'C:\SteamVR\drivers'` instead of the intended `'C:\SteamVR'`.

Observable failure mode:
- The IN-09 sanity check at line 467 (`DirExists(g_SteamVRDir + '\bin\win64')`)
  correctly logs the WARNING line "derived g_SteamVRDir lacks bin\win64".
- `GetVrpathreg('')` at line 236 returns
  `'C:\SteamVR\drivers\bin\win64\vrpathreg.exe'` — which doesn't exist.
- `VrpathregExists()` at line 491 returns false → `Exec(GetVrpathreg(...),
  'removedriver ...')` is silently skipped.
- Uninstaller deletes the driver files at `{app}` but leaves the driver
  registered in `steamvr.vrpaths` (a dangling entry SteamVR will probe on
  every subsequent start and ignore with a file-missing log line).

Not a correctness regression (the skip is clean; no crash, no data loss),
but the MR-01 fix advertises "removedriver now runs" and in practice it
never runs for the standard install layout because the parent-walk is
off-by-one. Severity Warning because this is an actual bug in previously-
committed fix code.
**Fix:** Use `ExtractFileDir` (which returns the directory *without*
trailing backslash, allowing iterative parent-walk) instead of
`ExtractFilePath`:
```pascal
// {app} == {SteamVR}\drivers\micmap, walk two parents up -> {SteamVR}
SteamVRParent := ExtractFileDir(ExtractFileDir(AppDir));
g_SteamVRDir := SteamVRParent;  // ExtractFileDir does not add a trailing slash
Log('Uninstall: resolved g_SteamVRDir from {app} = ' + g_SteamVRDir);
```
Behaves correctly:
- `ExtractFileDir('C:\SteamVR\drivers\micmap')` → `'C:\SteamVR\drivers'`
- `ExtractFileDir('C:\SteamVR\drivers')` → `'C:\SteamVR'`  ✓

The `RemoveBackslashUnlessRoot` call becomes unnecessary because
`ExtractFileDir` already omits the trailing backslash. Verify by visual
uninstall test: after uninstall, `vrpathreg.exe show` (run manually) should
no longer list the MicMap driver path; currently it does.

## Info

### IN-11: WR-08 packaged-task lambda leaks an uninvoked task on early-cancel path

**File:** `apps/micmap/main.cpp:939-946`
**Issue:** The WR-08 fix wraps the initial-thread work in two
`std::packaged_task<void()>` instances whose futures are seeded into
`g_app.driverConnectFuture` / `g_app.vrInitFuture` before thread launch
(line 937-938). The thread lambda (line 939-946) checks
`initialConnectCancel` between task invocations:
```cpp
g_app.initialConnectThread = std::thread(
    [connectTask = std::move(connectTask),
     vrInitTask  = std::move(vrInitTask)]() mutable {
        if (g_app.initialConnectCancel.load()) return;   // (a)
        connectTask();
        if (g_app.initialConnectCancel.load()) return;   // (b)
        vrInitTask();
    });
```
If cancel is set at points (a) or (b), the uninvoked `packaged_task` is
destroyed without being called. `std::packaged_task`'s destructor on an
uninvoked task sets a `future_error(broken_promise)` into the associated
shared state. The main-loop reconnect guard at line 993-1009 then sees the
future as "ready" (broken_promise futures are immediately ready) and
replaces `g_app.driverConnectFuture` with a fresh `std::async(...)` call
— effectively double-launching `driverClient->connect()` on the quit path.

On the fast-shutdown path, `shutdown()`'s cancel+join at line 461-462 runs
BEFORE the main-loop reconnect logic, so in practice the broken_promise
future never gets a chance to trigger the spurious reassignment. But the
state is fragile — if a future refactor ever runs the main loop past the
cancel point (e.g., processing one more frame of UI events before shutdown),
the broken-promise path becomes a real double-connect hazard. Pure Info
because today's code path converges cleanly.
**Fix:** Invoke both packaged_tasks unconditionally (they already check
`initialConnectCancel` internally via their lambda bodies), so the futures
are always fulfilled with `void()` rather than broken_promise:
```cpp
g_app.initialConnectThread = std::thread(
    [connectTask = std::move(connectTask),
     vrInitTask  = std::move(vrInitTask)]() mutable {
        connectTask();   // task body short-circuits on cancel
        vrInitTask();    // task body short-circuits on cancel
    });
```
The cancel short-circuits are already inside each task lambda (lines
925-929, 931-935), so this preserves the fast-cancel intent without
leaving the futures in the broken_promise state.

### IN-12: `Shell_NotifyIconW(NIM_DELETE)` return value unchecked

**File:** `apps/micmap/main.cpp:490`
**Issue:** `shutdown()` calls `Shell_NotifyIconW(NIM_DELETE, &nid)` without
checking the return value. `Shell_NotifyIconW` can return FALSE if the
shell is restarting, the icon was already removed (e.g., explorer.exe crash
recovery re-added it with a different handle), or taskbar is in an
inconsistent state. The IN-03 iteration-2 fix added a matching failure log
on the `NIM_ADD` side (line 201-204):
```cpp
if (!Shell_NotifyIconW(NIM_ADD, &g_app.nid)) {
    MICMAP_LOG_WARNING("Shell_NotifyIconW(NIM_ADD) failed ...");
}
```
The symmetric `NIM_DELETE` failure is silent. Low-severity — a stuck zombie
icon usually disappears on first mouse-over or the next explorer restart
— but a matching WARNING log would make the "stuck tray icon after
MicMap quit" bug report (should one ever appear) diagnosable from the log
file.
**Fix:** Mirror the IN-03 log pattern on the delete path:
```cpp
if (nid.cbSize != 0) {
    if (!Shell_NotifyIconW(NIM_DELETE, &nid)) {
        MICMAP_LOG_WARNING("Shell_NotifyIconW(NIM_DELETE) failed; tray icon "
                           "may persist until explorer restart (GetLastError=",
                           GetLastError(), ")");
    }
    nid.cbSize = 0;
}
```

### IN-13: `CreateRenderTarget` ignores `GetBuffer` / `CreateRenderTargetView` failures

**File:** `apps/micmap/main.cpp:178-183`
**Issue:** `CreateRenderTarget` calls `g_pSwapChain->GetBuffer` and
`g_pd3dDevice->CreateRenderTargetView` with no HRESULT check:
```cpp
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}
```
Two failure paths:
1. `GetBuffer` can fail (device lost, OOM) and leave `pBackBuffer` as
   an uninitialized pointer — the subsequent `CreateRenderTargetView` and
   `Release()` are then UB.
2. `CreateRenderTargetView` can fail and leave `g_mainRenderTargetView`
   null. The render loop at line 1018 then calls
   `OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr)` with a null
   RTV pointer — D3D11 will either early-bail silently or the
   ClearRenderTargetView below it will dereference null and crash.

Low probability on a working machine, but unchecked HRESULTs are a
project-wide code-quality nit (`_CRT_SECURE_NO_WARNINGS` + `/W4` + WIN32
SDK pattern). The surrounding WR-04 fix (iteration-1) for
`CreateDeviceD3D` null-checks `hwnd` and `CreateDeviceD3D` result —
`CreateRenderTarget` is the last unchecked D3D call-site.
**Fix:** Initialize `pBackBuffer` to nullptr and check both HRESULTs:
```cpp
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer) {
        MICMAP_LOG_ERROR("GetBuffer failed: 0x", std::hex, hr);
        return;
    }
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        MICMAP_LOG_ERROR("CreateRenderTargetView failed: 0x", std::hex, hr);
        g_mainRenderTargetView = nullptr;
    }
}
```
The `WM_SIZE` path at line 737-741 already implicitly relies on
`CreateRenderTarget` never failing post-resize (no branching on its
outcome); that remains a latent issue, but is out of this review's scope.

---

_Reviewed: 2026-04-24T16:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 3_

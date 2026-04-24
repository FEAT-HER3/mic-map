---
phase: 04-installer
fixed_at: 2026-04-24T15:10:00Z
review_path: .planning/phases/04-installer/04-REVIEW.md
iteration: 2
findings_in_scope: 7
fixed: 7
skipped: 0
status: all_fixed
---

# Phase 04: Code Review Fix Report (Iteration 2)

**Fixed at:** 2026-04-24T15:10:00Z
**Source review:** `.planning/phases/04-installer/04-REVIEW.md`
**Iteration:** 2

**Summary:**
- Findings in scope: 7 (all — 2 Warning + 5 Info; scope = `all`)
- Fixed: 7
- Skipped: 0

Build verified: `cmake --build build --config Debug --target micmap` and
`--target test_bindings_patcher` both compiled cleanly against the new
sources. `test_bindings_patcher.exe` runs end-to-end — all six scenarios
(idempotency, write_once_backup, unpatch_restore, unpatch_marker_erase,
unpatch_noop, atomic_write_crash_safety) PASS under the new per-PID
`resetTmpDir()`. Inno Setup script changes (MicMap.iss) are Pascal and
not compiled in this environment; they were syntax-reviewed by re-read
and the new helper (`QuoteExecArg`) follows the exact `StringChangeEx`
idiom already used at line 97.

## Fixed Issues

### WR-07: Five UI-thread `detector->*` call-sites still race the audio callback

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 427c898
**Applied fix:** Wrapped each remaining UI-thread `detector->*` access in
`std::lock_guard<std::mutex> lock(audioMutex)` to mirror WR-03's lock
discipline:
- Slider handler at line 578 (`setMinDetectionDuration`) — wrapped in a
  scoped lock inside the `if (detector)` guard.
- Auto-stop training branch at lines 600-616 — `finishTraining` and
  `saveTrainingData` each get their own scoped lock; the `isTraining =
  false` / `hasProfile = true` flag writes remain outside the lock since
  they're atomics / plain UI-thread state.
- Stop Training button at lines 619-635 — same pattern as auto-stop.
- Train Pattern button at lines 639-648 — `startTraining()` wrapped in a
  scoped lock before flipping `isTraining`.

The Clear button's `&& detector` read gate (line 650) was left untouched
per the review's explicit "acceptable" note — its actual reassignment
below is already covered by WR-03's original lock.

### WR-08: First-boot reconnect overlap — initial thread and main-loop async can run `connect()` concurrently

**Files modified:** `apps/micmap/main.cpp`
**Commit:** bb3010a
**Applied fix:** Replaced the single-lambda `std::thread(...)` initial
connect with two `std::packaged_task<void()>` instances (one for
`driverClient->connect()`, one for `vrInput->initialize()`). Their
futures are assigned to `g_app.driverConnectFuture` and
`g_app.vrInitFuture` BEFORE the thread is launched, so the main loop's
existing reconnect guard (`future.valid() && wait_for(0) != ready`)
already treats the in-flight initial-thread work as an active connect
and won't launch a second concurrent call. The `initialConnectCancel`
short-circuits are preserved between the two packaged tasks.

### IN-06: `std::getenv("LOCALAPPDATA")` + `fs::path(std::string)` munges non-ASCII user profile paths

**Files modified:** `src/bindings/src/bindings_patcher.cpp`
**Commit:** a09763e
**Applied fix:** Two Windows-specific changes in `ResolveSteamVrConfigDir`:
1. Replaced `std::getenv("LOCALAPPDATA")` + `fs::path(localAppData)`
   with `GetEnvironmentVariableW(L"LOCALAPPDATA", wchar_t buf, MAX_PATH)`
   + `fs::path(wchar_t*)`. Added a guarded `<Windows.h>` include.
2. Replaced `fs::path(runtime)` on the JSON-extracted UTF-8 runtime
   string with `fs::u8path(runtime)` so nlohmann/json's native UTF-8
   output survives as UTF-8 into the filesystem path.

The non-Windows `else` branch is unchanged (driver is Windows-only, but
the code keeps the portable `getenv("HOME")` path for clarity).

### IN-07: `test_bindings_patcher` shares `micmap_test_bindings` tmp dir across ctest invocations

**Files modified:** `tests/test_bindings_patcher.cpp`
**Commit:** 7f7b761
**Applied fix:** `resetTmpDir()` now appends the current process ID to
the tmp-dir name: `micmap_test_bindings_<pid>`. PID is obtained via
`GetCurrentProcessId()` on Windows and `getpid()` otherwise (guarded
includes added). All six existing `resetTmpDir()` call-sites continue to
work unchanged — verified by running the built test binary: all six
scenarios PASS.

### IN-08: `CreateMutexW` return value not checked before `GetLastError`

**Files modified:** `apps/micmap/main.cpp`
**Commit:** bb8879b
**Applied fix:** Added a null-check on `hMutex` immediately after
`CreateMutexW`. On failure, log via `MICMAP_LOG_ERROR` with
`GetLastError()` and return exit code 1 rather than falling through
with a broken single-instance gate.

### IN-09: `CurUninstallStepChanged` re-derivation of `g_SteamVRDir` is fragile if user picked a non-default `{app}`

**Files modified:** `installer/MicMap.iss`
**Commit:** ba4bd24
**Applied fix:** After the `Log('Uninstall: resolved g_SteamVRDir ...')`
diagnostic, added a `DirExists(g_SteamVRDir + '\bin\win64')` assertion
that logs a WARNING line when the derived path doesn't look like a
SteamVR install root. This doesn't change behavior (the existing
`VrpathregExists()` gate already handles Pitfall 10 cleanly) but
surfaces post-mortem evidence when MR-01's two-parents-up extraction
produces a wrong path.

### IN-10: `AppDir` in `Exec(..., 'adddriver "' + AppDir + '"', ...)` is not escape-sanitized

**Files modified:** `installer/MicMap.iss`
**Commit:** d6c7af4
**Applied fix:** Added a `QuoteExecArg(Arg: String): String` helper that
applies `StringChangeEx(Arg, '"', '""', True)` and returns the doubled
result. Wired it into all three vrpathreg `Exec` sites:
- `RunVrpathregRemove` (line 268) — install-time pre-adddriver sweep.
- `RunVrpathregAdd` (line 280) — install-time adddriver.
- `CurUninstallStepChanged` (line 494) — uninstall-time removedriver.

On today's code path (`DisableDirPage=yes`, `{app}` resolved from
`GetMicMapInstallDir`) embedded quotes are impossible, so this is a
pure defense-in-depth change — but any future refactor that lets user
input influence `{app}` will be covered by the escape.

## Skipped Issues

None — all seven findings in scope were applied cleanly, built, and
(where applicable) tested.

---

_Fixed: 2026-04-24T15:10:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_

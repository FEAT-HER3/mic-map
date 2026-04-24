---
phase: 03-auto-start
fixed_at: 2026-04-23T23:00:00Z
review_path: .planning/phases/03-auto-start/03-REVIEW.md
iteration: 1
findings_in_scope: 14
fixed: 13
skipped: 1
status: partial
---

# Phase 3: Code Review Fix Report

**Fixed at:** 2026-04-23T23:00:00Z
**Source review:** `.planning/phases/03-auto-start/03-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 14 (0 Critical, 6 Warning, 8 Info)
- Fixed: 13 (6 Warning, 7 Info)
- Skipped: 1 (IN-05 — already correctly formatted in current code)

Full build + `ctest -C Release` runs clean after all fixes: **8/8 tests GREEN**
(test_placeholder, test_config_manager, test_command_queue, test_cli_flags_parse,
test_manifest_registrar, test_vr_input_quit_ordering, test_tray_balloon_once,
test_vrmanifest_schema).

## Fixed Issues

### WR-01: Concurrent `vr::VR_Init` from retry thread and VR input init thread

**Files modified:** `apps/micmap/main.cpp`
**Commit:** d5e8a49
**Applied fix:** Narrowed the startup-window race by deferring the retry
thread's first `VR_Init(Utility)` until the detached `initThread`'s
`VR_Init(Background)` either succeeds (`vrInput->isInitialized() == true`) or
the 3-second (30 x 100ms cancel-responsive) poll window elapses. Teardown
ordering was already handled by the cancel+join at `shutdown()` step 0. Added
an explicit `// NOTE:` that the race is *narrowed*, not eliminated (structural
elimination would require a full serialization mutex around `VR_Init`, which
is risky this late in Phase 3).

### WR-02: Logger format-string mismatch — `{}` placeholders logged as literals

**Files modified:** `src/steamvr/src/vr_input.cpp`, `apps/micmap/main.cpp`
**Commits:** c6edd89 (vr_input.cpp), cf53c15 (main.cpp onTrigger)
**Applied fix:** Converted all fmt-style `{}` placeholders at call sites
`vr_input.cpp:112, 129, 140, 165, 169, 179, 186, 190` and `main.cpp:444`
(onTrigger) to stream-concatenation style matching the project's
`MICMAP_LOG_*` `operator<<` fold convention (e.g., `config_manager.cpp:119`).
Output now produces the intended `"DriverClient created (host: 127.0.0.1,
ports: 30152-30162)"` instead of the literal `"... host: {}, ports:
{}-{}127.0.0.1 30152-30162"`.

### WR-03: `MicMapApp::initialize()` return value discarded

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 1558133
**Applied fix:** `WinMain` now tests the `bool` return from `g_app.initialize()`
and on `false` performs a full ordered teardown (ImGui shutdown, D3D cleanup,
window destroy, class unregister, mutex close) before returning exit code 1.
Prevents the retry thread from registering the manifest against a
partially-initialized app (which would make SteamVR auto-launch a broken
instance on next boot).

### WR-04: Early-exit paths in WinMain leak window class / mutex

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 0efd4a2
**Applied fix:** Added explicit null-check after `CreateWindowW` (logs
`GetLastError()` on failure) and extended the `CreateDeviceD3D` failure path
to also `DestroyWindow(g_app.hwnd)` and `CloseHandle(hMutex)` before
returning 1. Prevents `ERROR_ALREADY_EXISTS` ghost-instance detection on
crash-restart cycles.

### WR-05: Detached `std::async` reconnect futures outlive `g_app` on shutdown

**Files modified:** `apps/micmap/main.cpp`
**Commit:** bbe7969
**Applied fix:** Promoted the two function-local
`static std::future<void>` objects (`driverConnectFuture`, `vrInitFuture`)
to members of `MicMapApp`. `shutdown()` now waits on both futures
immediately after the `manifestRetryThread` cancel+join, before the
`driverClient->disconnect()` / `vrInput->shutdown()` teardown. Uses the same
retry-thread pattern already validated in Plan 03-07 (D-14).

### WR-06: `writeAtomicWindows` leaves temp file on ofstream write failure

**Files modified:** `src/core/src/config_manager.cpp`
**Commit:** 8fa5da3
**Applied fix:** On ofstream write failure, now call `f.close()` then
`fs::remove(tmp, remEc)` before returning `false`, mirroring the cleanup
already present on the `ReplaceFileW` / `MoveFileExW` failure paths. Prevents
orphan `config.json.tmp` files from accumulating next to the real config on
repeated failed saves.

### IN-01: `SHGetFolderPathW` is deprecated

**Files modified:** `src/core/src/config_manager.cpp`
**Commit:** 5673b1f
**Applied fix:** Documented the explicit decision to retain
`SHGetFolderPathW(CSIDL_APPDATA)` for Phase 3. Migration to
`SHGetKnownFolderPath(FOLDERID_RoamingAppData)` is annotated for a future
pass; the deprecated form works on all supported Windows versions and the
behavior is identical for the roaming AppData case we use.

### IN-02: `RemoveSystemTray` is dead code

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 66323f7
**Applied fix:** Deleted the unreferenced `RemoveSystemTray()` helper. The
`Shell_NotifyIconW(NIM_DELETE, ...)` call is inlined in `shutdown()` step 6
with an `nid.cbSize == 0` re-entry guard. A comment was left at the old call
site explaining the symmetry choice.

### IN-03: `SetupSystemTray` ignores `Shell_NotifyIconW` return value

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 8a7619e
**Applied fix:** `SetupSystemTray` now checks the `BOOL` return from
`Shell_NotifyIconW(NIM_ADD, ...)` and logs a WARNING (with `GetLastError()`)
on failure. Surface first-boot tray issues in the log instead of silently
mis-attributing them as "balloon never fires".

### IN-04: `resolveManifestAbsolutePath` stack buffer is 64 KB

**Files modified:** `src/steamvr/src/manifest_registrar.cpp`
**Commit:** a9e1740
**Applied fix:** Documented the `WCHAR buf[32768]` stack-allocation decision
as acceptable on the default 1 MB Windows thread stack, with an explicit
warning to future maintainers not to call this function from a
reduced-stack worker pool. `PATHCCH_MAX_CCH` citation added.

### IN-06: `backupAndRotate` lexicographic sort invariant

**Files modified:** `src/core/src/config_manager.cpp`
**Commit:** e6ef7ae
**Applied fix:** Documented the `YYYYMMDD-HHMMSS` zero-padded
lex-sort-equals-chronological invariant in a code comment directly above the
`std::sort(..., std::greater<>())` call. Future timestamp-format changes
must preserve the invariant or switch to an explicit `mtime`-based
comparator.

### IN-07: `StubVRInput::notifyEvent` is unreachable

**Files modified:** `src/steamvr/src/vr_input.cpp`
**Commit:** 7f2c808
**Applied fix:** Documented `StubVRInput::notifyEvent` as a deliberate
test-injection seam (kept `protected`) so test subclasses can synthesize
`VREvent`s without a separate harness. Parallels the production-side
ack-before-notify path already exercised by `test_vr_input_quit_ordering`.

### IN-08: `detectionBuf[128]` allocated even when not used

**Files modified:** `apps/micmap/main.cpp`
**Commit:** 6c217f8
**Applied fix:** Documented why `detectionBuf` MUST remain at outer scope in
`renderUI()` — the `detectionText` `const char*` is read by a subsequent
`ImGui::Button` call outside the `detected` branch, so moving the buffer
inside (as the reviewer suggested) would dangle the pointer. Kept the buffer
at outer scope and annotated the lifetime constraint so future refactors
don't regress. This is the correctness-preserving interpretation of the
reviewer's intent.

## Skipped Issues

### IN-05: Retry thread log "unexpected VRInitError" run-on spacing

**File:** `apps/micmap/main.cpp:304-305`
**Reason:** Already correctly formatted in the current code. The string
literal on line 304 is `"manifest retry VR_Init unexpected error: "` —
trailing space after the colon, exactly matching the reviewer's suggested
`"manifest retry VR_Init unexpected error: " << desc` pattern. Stream
concatenation with `VR_GetVRInitErrorAsEnglishDescription(err)` produces
`"manifest retry VR_Init unexpected error: <description>"` with a single
space between the colon and the description. The reviewer's concern
("two-space-free run-on") does not match the current literal; no edit is
needed.
**Original issue:** Style nit — message followed by enum description
produces a run-on in the log per reviewer's read of `manifest_registrar.cpp:203`.

---

_Fixed: 2026-04-23T23:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

---
phase: 08-ipc-contract-reshape
plan: 01
subsystem: steamvr / common-logger / driver-logger
tags: [phase-8, rename, lib-04, pitfall-6, pitfall-10, depends-08-00]
dependency_graph:
  requires:
    - "08-00 cpp-httplib v0.14.3 -> v0.20.1 bump (httplib::Error::Connection used in connect())"
    - "08-00 test_multi_sink_logger.cpp scaffold (RED until this plan)"
    - "08-00 test_client_driver_loaded_indicator.cpp scaffold (RED until this plan)"
    - "src/core/src/config_manager.cpp:33-49 SHGetFolderPathW pattern (mirrored at WinMain)"
    - "driver/src/driver_log.hpp::SafeDriverLog (P6 Rule-3 vr::VRDriverContext() guard)"
  provides:
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp + ConnectResult enum"
    - "src/steamvr/src/driver_api.cpp + Pitfall 6 connect() impl"
    - "src/common/include/micmap/common/log_sink.hpp (ILogSink + MultiSinkLogger)"
    - "src/common/src/sinks/{file,stdout}_log_sink.cpp + src/multi_sink_logger.cpp"
    - "driver/src/sinks/driver_log_sink.{hpp,cpp}"
    - "Client-side composition-root logger wiring at WinMain entry"
  affects:
    - "apps/micmap/main.cpp — WinMain composition root + ConnectResult callsites"
    - "apps/hmd_button_test/main.cpp — ConnectResult callsites + include rename"
    - "src/steamvr/include/micmap/steamvr/vr_input_events.hpp — include rename"
    - "src/steamvr/src/vr_input_events.cpp — comment refresh (IDriverClient -> IDriverApi)"
    - "src/steamvr/CMakeLists.txt — source list points at driver_api.cpp"
    - "src/common/CMakeLists.txt — picks up 3 new sink sources"
    - "driver/CMakeLists.txt — picks up driver_log_sink.cpp"
    - "tests/test_client_driver_loaded_indicator.cpp — port choice tightened (Rule 1)"
tech_stack:
  added: []
  patterns:
    - "Composition-Root Logger Init (D-21 / Pitfall 8) — hoisted to FIRST step in WinMain"
    - "Sink-fan-out under single serializing mutex (D-19)"
    - "Pitfall 6 3-state connect() (Connected / NotFound / Timeout / OtherError)"
key_files:
  created:
    - "src/common/include/micmap/common/log_sink.hpp"
    - "src/common/src/sinks/file_log_sink.cpp"
    - "src/common/src/sinks/stdout_log_sink.cpp"
    - "src/common/src/multi_sink_logger.cpp"
    - "driver/src/sinks/driver_log_sink.hpp"
    - "driver/src/sinks/driver_log_sink.cpp"
    - "src/steamvr/include/micmap/steamvr/driver_api.hpp"   # via git mv from vr_input.hpp
    - "src/steamvr/src/driver_api.cpp"                       # via git mv from vr_input.cpp
  modified:
    - "src/steamvr/CMakeLists.txt"
    - "src/steamvr/include/micmap/steamvr/vr_input_events.hpp"
    - "src/steamvr/src/vr_input_events.cpp"
    - "src/common/CMakeLists.txt"
    - "driver/CMakeLists.txt"
    - "apps/micmap/main.cpp"
    - "apps/hmd_button_test/main.cpp"
    - "tests/test_client_driver_loaded_indicator.cpp"
decisions:
  - "Pitfall 6 connect() switch handles BOTH httplib::Error::Connection AND httplib::Error::ConnectionTimeout as the NotFound bucket — Windows loopback returns ConnectionTimeout for unbound high ports via httplib v0.20.1's poll-based connect (httplib.h:3329), and both still mean 'no TCP handshake established' which is the HEALTH-01 NotFound semantic. Read/Write timeout (handshake succeeded, response slow) remains the Timeout bucket. (Rule 1 deviation discovered during ClientDriverLoadedIndicator verification.)"
  - "MultiSinkLogger declared in log_sink.hpp (not just behind makeMultiSinkLogger factory) so tests/test_multi_sink_logger.cpp can stack-allocate it without going through shared_ptr — matches the Wave 0 scaffold's expected API shape."
  - "Composition-root wiring at WinMain hoisted to BEFORE CLI parse (rather than just before CreateWindowW per the plan literal) so MICMAP_LOG_ERROR calls inside the headless register/unregister fork (--register-manifest, --unregister-manifest) and inside CreateMutexW failure paths also land in the file sink. Functionally equivalent for the GUI flow, more defensive for the headless flow."
  - "Driver-side composition root (DriverLogSink + FileLogSink under DeviceProvider::Init) deferred to 08-02 per plan; this plan only lands the DriverLogSink class + factory under driver/src/sinks/ so the Wave 0 LIB-04 invariant holds without forcing device_provider.cpp ctor evolution here."
  - "Tightened ClientDriverLoadedIndicator scaffold port from 1 (privileged, behaves inconsistently on Windows) to 65532 (high, predictable ECONNREFUSED/ConnectionTimeout). Original scaffold from 08-00 timed out instead of returning NotFound."
metrics:
  duration_minutes: 50
  completed_date: "2026-05-05"
  commits: 2
  files_created: 6
  files_modified: 8
  tasks: 2
---

# Phase 8 Plan 01: IDriverClient -> IDriverApi rename + LIB-04 logger sinks Summary

D-22 rename from `vr_input.{hpp,cpp}` to `driver_api.{hpp,cpp}` with `IDriverClient -> IDriverApi`, factory `createDriverClient -> createDriverApi`, and a Pitfall-6-aware `ConnectResult` enum on `connect()`; LIB-04 composition-root logger sinks (ILogSink + 4 concrete sinks + MultiSinkLogger fan-out) landed in src/common, plus a driver-only DriverLogSink under driver/src/sinks/, plus client-side WinMain composition root wiring.

## What was delivered

### Task 1: D-22 rename + Pitfall 6 fix (commit 667993a)

- `git mv src/steamvr/include/micmap/steamvr/vr_input.hpp -> driver_api.hpp`
- `git mv src/steamvr/src/vr_input.cpp -> driver_api.cpp`
- `class IDriverClient -> class IDriverApi` (impl class `DriverClient -> DriverApi`).
- `std::unique_ptr<IDriverClient> createDriverClient(...) -> std::unique_ptr<IDriverApi> createDriverApi(...)`.
- New `enum class ConnectResult { Connected, NotFound, Timeout, OtherError }` (4 values per plan).
- `virtual bool connect() = 0;` -> `virtual ConnectResult connect() = 0;`.
- New `connect()` body classifies httplib results: 200 -> Connected; `httplib::Error::Connection` or `Error::ConnectionTimeout` -> continue (NotFound bucket); `Error::Read | Error::Write` -> `sawTimeout=true`; default fold into Timeout. End-of-loop verdict: NotFound iff no timeouts seen.
- Callers updated to ConnectResult-keyed checks: 5 callsites total in `apps/micmap/main.cpp` (initial-connect packaged_task + main-loop reconnect-guard) and `apps/hmd_button_test/main.cpp` (OnSendTap, OnTestDriver, OnReconnectDriver).
- Header dependency updated in `src/steamvr/include/micmap/steamvr/vr_input_events.hpp` (now includes `driver_api.hpp` for `VREventType`).
- `src/steamvr/CMakeLists.txt` source list now references `src/driver_api.cpp`.
- `IVRInput` / `createOpenVRInput` / `createStubVRInput` / `vr_input_events.{hpp,cpp}` left UNCHANGED per plan ("orthogonal surface").

### Task 2: LIB-04 logger sinks + client composition root (commit dcfebde)

- `src/common/include/micmap/common/log_sink.hpp`: ILogSink leaf interface (unconditional emitter; no min-level filter per Pitfall 10) + factories (`makeFileLogSink`, `makeStdoutLogSink`, `makeMultiSinkLogger`) + the `MultiSinkLogger` class declared in the header (so the test scaffold can stack-allocate without shared_ptr indirection).
- `src/common/src/sinks/file_log_sink.cpp`: atomic-append + per-line-flush + best-effort `create_directories` on the parent of the log path.
- `src/common/src/sinks/stdout_log_sink.cpp`: stderr stream, mirroring ConsoleLogger body verbatim (minus the min-level filter).
- `src/common/src/multi_sink_logger.cpp`: ILogger impl fanning out to a `vector<shared_ptr<ILogSink>>` under a single serializing mutex (D-19). Min-level filter lives here.
- `driver/src/sinks/driver_log_sink.hpp` + `.cpp`: DriverLogSink wraps SafeDriverLog (driver-only; out of micmap_core_runtime per LIB-03). Driver-side composition root wiring deferred to 08-02.
- `apps/micmap/main.cpp` WinMain entry: stdout sink + file sink (`%APPDATA%\\MicMap\\micmap.log` resolved via `SHGetFolderPathW(CSIDL_APPDATA)`) -> MultiSinkLogger -> `Logger::setLogger(...)`. Hoisted to FIRST step in WinMain (before CLI parse) so headless register/unregister errors and CreateMutex failure paths also flow into the file sink.
- Build wiring: `src/common/CMakeLists.txt` picks up the 3 new common sources; `driver/CMakeLists.txt` picks up `driver_log_sink.cpp`.

## Commits

| # | Hash | Message |
|---|------|---------|
| 1 | `667993a` | refactor(08-01): rename IDriverClient -> IDriverApi + ConnectResult enum (Pitfall 6) |
| 2 | `dcfebde` | feat(08-01): LIB-04 logger sinks + client composition root |

## Verification

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOPENVR_SDK_PATH=...` configures cleanly.
- `cmake --build build --config Release --target micmap driver_micmap` succeeds. micmap.exe and driver_micmap.dll both build clean (one pre-existing LNK4098 LIBCMT-conflict warning on driver_micmap from cpp-httplib mixed-CRT — not regressed by this plan).
- `cmake --build build --config Release --target micmap_steamvr` builds clean (rename intact).
- `ctest --test-dir build -C Release -R "test_command_queue|test_vr_input_quit_ordering|test_bindings_patcher|MultiSinkLogger|ClientDriverLoadedIndicator|bindings_patcher_idempotent" --output-on-failure` reports 6/6 PASS:

  | Test | Result |
  |------|--------|
  | test_command_queue | Passed (0.01s) |
  | test_vr_input_quit_ordering | Passed (0.01s) — header-rename intact |
  | test_bindings_patcher | Passed (0.03s) |
  | bindings_patcher_idempotent | Passed (0.01s) |
  | MultiSinkLogger | Passed (0.01s) — Wave 0 RED -> Wave 1 GREEN |
  | ClientDriverLoadedIndicator | Passed (1.01s) — Wave 0 RED -> Wave 1 GREEN |

- `grep -rn "IDriverClient|createDriverClient" .claude/worktrees/agent-aecc74c3d29f5e725 --include="*.cpp" --include="*.hpp" --include="*.h"` returns 0 matches (rename complete).
- `grep "ConnectResult" src/steamvr/include/micmap/steamvr/driver_api.hpp` -> matches.
- `grep "httplib::Error::Connection" src/steamvr/src/driver_api.cpp` -> matches.
- `grep "MultiSinkLogger" apps/micmap/main.cpp` -> matches.
- `grep "makeFileLogSink" apps/micmap/main.cpp` -> matches.
- `grep -r "MICMAP_DRIVER_BUILD" src/audio src/detection src/core src/common` -> 0 matches (LIB-04 invariant holds).
- `vr_input.hpp` and `vr_input.cpp` confirmed deleted.
- All 6 new sink files confirmed created at expected paths.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Pitfall 6 switch missed `httplib::Error::ConnectionTimeout` on Windows**

- **Found during:** Task 2 verification, `ctest -R ClientDriverLoadedIndicator` step.
- **Issue:** The plan's `connect()` switch covered only `httplib::Error::Connection` (immediate ECONNREFUSED) for the NotFound bucket. On Windows, httplib v0.20.1's loopback connect to an unbound high port can resolve via the poll-based path (`httplib.h:3329`, `if (poll_res == 0) return Error::ConnectionTimeout`) instead of returning Error::Connection. The Wave 0 scaffold therefore exhausted `connect_timeout(1)` and returned Timeout instead of NotFound, failing `MM_CHECK(result == ms::ConnectResult::NotFound)`.
- **Fix:** Added `case E::ConnectionTimeout` alongside `case E::Connection` in the switch — both map to "no TCP handshake established," which is the Pitfall 6 NotFound semantic. The Read/Write timeout branch (handshake succeeded but response slow) remains the Timeout bucket. The differentiation is preserved; the bucket boundary just got the right name on Windows.
- **Files modified:** `src/steamvr/src/driver_api.cpp` (folded into Task 2 commit `dcfebde`).
- **Commit:** `dcfebde` (Task 2; the fix was discovered during Task 2 verification because that's when the test was first runnable post-Wave-1).

**2. [Rule 1 - Bug] ClientDriverLoadedIndicator scaffold used port 1 (privileged), which on Windows times out instead of immediately ECONNREFUSED-ing**

- **Found during:** Task 2 verification.
- **Issue:** The Wave 0 scaffold from 08-00 chose port 1 (privileged) on the assumption that "nothing listens + immediate ECONNREFUSED." On the executor's Windows 11 system, this consistently took the connect_timeout(1) path rather than the immediate-RST path. Test failed.
- **Fix:** Changed the scaffold's port from 1 to 65532 (high-numbered, outside both the typical Windows dynamic-port range 49152-65000 and Steam/SteamVR's port allocations). Predictable ECONNREFUSED / ConnectionTimeout on Windows; the test still asserts `ConnectResult::NotFound` and now passes (1.01s — note the 1s is from the connect_timeout for the ConnectionTimeout case).
- **Files modified:** `tests/test_client_driver_loaded_indicator.cpp`.
- **Commit:** `dcfebde` (Task 2).

### Out-of-scope / Deferred

- Driver-side composition root wiring (DriverLogSink + FileLogSink under DeviceProvider::Init) — deferred to 08-02 per plan. The DriverLogSink class + factory exist in `driver/src/sinks/driver_log_sink.{hpp,cpp}` and link cleanly into `driver_micmap.dll`, but DeviceProvider::Init has not yet been edited to call `Logger::setLogger(makeMultiSinkLogger({makeDriverLogSink(), makeFileLogSink(...)}))`. 08-02 owns that step alongside its other DeviceProvider ctor evolution.
- `ConnectResult::Timeout` differentiation test (driver up but slow, set_read_timeout(0) stub) — left as documented future-work in the test scaffold's header comment. HEALTH-01 only requires the NotFound vs not-NotFound distinction, which is now covered.
- Pre-existing LNK4098 LIBCMT-conflict warning on `driver_micmap.dll` — pre-dates this plan (cpp-httplib mixed-CRT artifact). Not in scope.

## Self-Check

**Files claimed created:**
- src/common/include/micmap/common/log_sink.hpp -- FOUND
- src/common/src/sinks/file_log_sink.cpp -- FOUND
- src/common/src/sinks/stdout_log_sink.cpp -- FOUND
- src/common/src/multi_sink_logger.cpp -- FOUND
- driver/src/sinks/driver_log_sink.hpp -- FOUND
- driver/src/sinks/driver_log_sink.cpp -- FOUND
- src/steamvr/include/micmap/steamvr/driver_api.hpp -- FOUND (via git mv)
- src/steamvr/src/driver_api.cpp -- FOUND (via git mv)

**Files claimed deleted (rename source):**
- src/steamvr/include/micmap/steamvr/vr_input.hpp -- CONFIRMED ABSENT
- src/steamvr/src/vr_input.cpp -- CONFIRMED ABSENT

**Commits claimed:**
- 667993a -- FOUND
- dcfebde -- FOUND

## Self-Check: PASSED

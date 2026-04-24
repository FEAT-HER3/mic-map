---
phase: 03-auto-start
plan: 01
subsystem: tests
tags: [tdd, wave-0, scaffold, red-tests, ctest, openvr, manifest, auto-start]
dependency-graph:
  requires:
    - micmap_common (existing static lib)
    - micmap_steamvr (existing static lib, links OpenVR)
    - micmap_core (existing static lib)
    - nlohmann_json (vendored interface lib)
    - micmap (exe target — for add_dependencies on schema test)
  provides:
    - test_cli_flags_parse (RED — Plan 03-06 turns GREEN)
    - test_manifest_registrar (RED — Plan 03-04 turns GREEN)
    - test_vr_input_quit_ordering (RED — Plan 03-05 turns GREEN)
    - test_tray_balloon_once (RED — Plan 03-06 turns GREEN)
    - test_vrmanifest_schema (RED — Plan 03-02 turns GREEN)
    - micmap::common::CliFlags + parseCliArgs forward-decl (Plan 03-06 implements)
  affects:
    - tests/CMakeLists.txt (5 new add_test entries)
tech-stack:
  added: []
  patterns:
    - "Plain-main, exit-code-based, ctest-registered tests (matches Phase 2 test_config_manager precedent)"
    - "MM_CHECK macro for assertion-with-line-number-and-stderr (matches test_config_manager.cpp)"
    - "Stub-seam pattern — interface declared in header that downstream plan provides; production code injectable via factory_ForTesting helper"
    - "std::queue<T>-driven scripted return values + std::vector<std::string> call-log for ordering invariants"
    - "Build-dir-relative paths via target_compile_definitions(MICMAP_MANIFEST_PATH=\"$<CONFIG>/...\") + add_dependencies(test_target dependent_target)"
key-files:
  created:
    - src/common/include/micmap/common/cli_flags.hpp
    - tests/test_cli_flags_parse.cpp
    - tests/test_manifest_registrar.cpp
    - tests/test_vr_input_quit_ordering.cpp
    - tests/test_tray_balloon_once.cpp
    - tests/test_vrmanifest_schema.cpp
  modified:
    - tests/CMakeLists.txt
decisions:
  - "Header-location for parseCliArgs is src/common/include/micmap/common/cli_flags.hpp (D-01 explicitly leaves location to Claude's discretion). Chosen because micmap::common is the public-API home for Win32-tier helpers and avoids leaking apps/micmap private code into tests."
  - "Wide-char argv parameter type is `const wchar_t* const*` — matches the post-CommandLineToArgvW shape, enables tests to pass static literal arrays without const-correctness gymnastics."
  - "Manifest registrar test uses createManifestRegistrarForTesting(IVRApplicationsSurface&, std::string appKey, std::wstring manifestAbsPath) factory — Plan 03-04 must expose this alongside the production createManifestRegistrar()."
  - "vr_input quit-ordering test follows Plan 03-01 instruction OPTION 1 (free function processVREvent in vr_input_events.{hpp,cpp}) — simpler than refactoring OpenVRInput class internals."
  - "vrmanifest schema test uses target_compile_definitions(MICMAP_MANIFEST_PATH=\"...\") with $<CONFIG> generator-expression instead of $<TARGET_FILE_DIR:micmap> — works correctly under multi-config generators (MSBuild) and matches the actual on-disk layout (build/bin/Release/app.vrmanifest)."
  - "vrmanifest schema test accepts EITHER `arguments: \"--minimized\"` (string) OR `arguments: [\"--minimized\"]` (array) — Open-item A2 will be empirically resolved in Plan 03-02; this test then tightens to the working form."
metrics:
  duration_seconds: ~600
  completed: 2026-04-23
---

# Phase 03 Plan 01: Wave 0 RED Test Scaffold Summary

Stood up five RED ctest targets covering every AUTO-0x requirement plus published the `cli_flags.hpp` public contract — the Nyquist gate that prevents Phase 3 from exiting into manual-UAT-only.

## What This Plan Did

- Published `src/common/include/micmap/common/cli_flags.hpp` declaring `micmap::common::CliFlags` (`registerManifest` / `unregisterManifest` / `minimized` bools) and the pure `parseCliArgs(int argc, const wchar_t* const* argv)` forward-declaration. This is the public contract Plan 03-06's `WinMain` integration depends on.
- Created five RED unit tests (plain-main, exit-code-based, ctest-registered) that pin the behavioral contracts of every Phase 3 implementation plan:
  | # | Test                                  | Requirement                | RED reason                                                          | Flips GREEN in |
  |---|----------------------------------------|----------------------------|---------------------------------------------------------------------|----------------|
  | 1 | `test_cli_flags_parse`                | AUTO-06 / D-01             | Link error: unresolved external `parseCliArgs`                      | Plan 03-06     |
  | 2 | `test_manifest_registrar`             | AUTO-02, AUTO-03, AUTO-04  | Compile error: `manifest_registrar.hpp` not found                   | Plan 03-04     |
  | 3 | `test_vr_input_quit_ordering`         | AUTO-05 / D-11             | Compile error: `vr_input_events.hpp` not found                      | Plan 03-05     |
  | 4 | `test_tray_balloon_once`              | AUTO-06 / D-09 / D-10      | Compile error: `first_launch_balloon.hpp` not found                 | Plan 03-06     |
  | 5 | `test_vrmanifest_schema`              | AUTO-01 + Open-item A2     | Runtime error: `app.vrmanifest` missing from build output dir       | Plan 03-02     |
- Wired `tests/CMakeLists.txt` for all five with proper `target_link_libraries`, `target_compile_features(cxx_std_17)`, and (for the schema test) `target_compile_definitions(MICMAP_MANIFEST_PATH="$<CONFIG>/...")` + `add_dependencies(test_vrmanifest_schema micmap)` so ctest builds the manifest before running the schema check.
- Defined three injection seams via header forward-declarations Plan 03-04/05/06 must implement against:
  - `IVRApplicationsSurface` (Plan 03-04 `manifest_registrar.hpp`) — `AddApplicationManifest` / `IsApplicationInstalled` / `SetApplicationAutoLaunch` / `RemoveApplicationManifest` matching `IVRApplications_007/_008` ABI.
  - `IVRSystemSeam` + `IEventSink` (Plan 03-05 `vr_input_events.hpp`) — `AcknowledgeQuit_Exiting()` and typed `notifyEvent(VREventType)` so quit-ordering can be asserted without booting SteamVR.
  - `IShellNotifySeam` (Plan 03-06 `first_launch_balloon.hpp`) — `notifyWithBalloon(title, body)` returning bool, count-driven assertions in tests.

## Behavioral Coverage Verified by Each Test

- **CLI flag parse (6 cases):** register / unregister / minimized / combined-install / unknown-silent-ignore / no-flags-default.
- **Manifest registrar (5 cases):** happy-path with poll-then-set-autolaunch ordering invariant; Pitfall-1 poll timeout (20 attempts, no `SetApplicationAutoLaunch`); `AddApplicationManifest` failure short-circuit; `ensureRegistered` idempotence (no Add when already installed); `unregisterApp` Remove-once.
- **VR input quit ordering (2 cases):** `VREvent_Quit` triggers `AcknowledgeQuit_Exiting()` at index 0 of the call log strictly before `notifyEvent(Quit)`; non-Quit events do NOT fire the ack (gate-on-event-type invariant).
- **Tray balloon one-shot (3 cases):** first silent-launch fires balloon + flips `shownTrayNotification` flag + persists via `saveDefault`; second silent-launch with flag=true does NOT re-fire; user-clicked launch (no `--minimized`) does NOT consume the one-shot regardless of flag state.
- **vrmanifest schema (1 multi-assert case):** top-level `source==builtin`, `applications[0].app_key==bigscreen.micmap`, `launch_type==binary`, `binary_path_windows==micmap.exe`, `is_dashboard_overlay==true`, `arguments` accepting EITHER string `"--minimized"` OR 1-element array `["--minimized"]`.

## RED Evidence

Final ctest run after both task commits:

```
$ ctest --test-dir build -C Release -R "cli_flags_parse|manifest_registrar|vr_input_quit|tray_balloon|vrmanifest_schema"
4/5 Test #4: test_cli_flags_parse ............***Not Run   (build-fail)
4/5 Test #5: test_manifest_registrar .........***Not Run   (build-fail)
4/5 Test #6: test_vr_input_quit_ordering .....***Not Run   (build-fail)
4/5 Test #7: test_tray_balloon_once ..........***Not Run   (build-fail)
5/5 Test #8: test_vrmanifest_schema ..........***Failed    0.01 sec
FAIL: manifest file not found at C:/Users/decid/Documents/projects/mic-map/build/bin/Release/app.vrmanifest
      Plan 03-02 must emit app.vrmanifest beside micmap.exe.

0% tests passed, 5 tests failed out of 5
```

Build-failure diagnostics (excerpts):

```
test_cli_flags_parse.obj : error LNK2019: unresolved external symbol
  "struct micmap::common::CliFlags __cdecl micmap::common::parseCliArgs(int,wchar_t const * const *)"
  referenced in function main

tests\test_manifest_registrar.cpp(42,10): error C1083:
  Cannot open include file: 'micmap/steamvr/manifest_registrar.hpp': No such file or directory

tests\test_vr_input_quit_ordering.cpp(29,10): error C1083:
  Cannot open include file: 'micmap/steamvr/vr_input_events.hpp': No such file or directory

tests\test_tray_balloon_once.cpp(30,10): error C1083:
  Cannot open include file: 'first_launch_balloon.hpp': No such file or directory
```

Each failure cites exactly the missing artifact the next plan must produce — clean RED state, no spurious build issues.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 — Bug] Multi-config generator path for `MICMAP_MANIFEST_PATH`**
   - **Found during:** Task 2 cmake configure
   - **Issue:** Plan 03-01 specified `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/app.vrmanifest` for the schema-test compile-definition. Under MSBuild (multi-config), the actual on-disk layout is `<CMAKE_RUNTIME_OUTPUT_DIRECTORY>/<CONFIG>/app.vrmanifest` (e.g. `build/bin/Release/app.vrmanifest`); the plan's path would have made the schema test silently look in the wrong directory.
   - **Fix:** Used the generator expression `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/app.vrmanifest` so the path resolves correctly per-config. This matches the actual placement of `micmap.exe` and the `add_dependencies(micmap)` build-order guarantee.
   - **Files modified:** tests/CMakeLists.txt
   - **Commit:** a12662b

2. **[Rule 1 — Bug] nlohmann_json target name**
   - **Found during:** Task 2 cmake configure
   - **Issue:** Plan 03-01 specified `target_link_libraries(... nlohmann_json::nlohmann_json)`, but the project's vendored `external/CMakeLists.txt` declares the target as plain `nlohmann_json` (interface library, no `::` alias).
   - **Fix:** Linked `nlohmann_json` directly. Verified by reading `external/CMakeLists.txt` lines 20–21.
   - **Files modified:** tests/CMakeLists.txt
   - **Commit:** a12662b

### Not auto-fixed (intentional)

- The `--unknown` flag handling in `test_cli_flags_parse` only verifies "all flags remain false" — it does not assert any error code path because D-01 explicitly specifies "silent ignore" semantics. Plan 03-06 is free to add stderr logging if it wishes; the test will continue to pass either way.

## Threat Flags

None. This plan adds test-only code: no network, no auth, no persistent file writes outside the build directory, no untrusted-input parsing. Threat-model coverage from PLAN.md `<threat_model>` is unchanged — `T-03-01-01` (test loads vrmanifest from build dir) is mitigated by CMake-controlled path injection; `T-03-01-02` (input validation) is N/A because the schema-test uses `nlohmann::json::parse(..., allow_exceptions=false)` defensively.

## Acceptance Criteria Status

- [x] `tests/test_cli_flags_parse.cpp` exists; contains `registerManifest`, `unregisterManifest`, `minimized` strings (cases 1–6).
- [x] `tests/test_manifest_registrar.cpp` exists; contains `RegisterResult::Success`, `RegisterResult::PollTimeout`, `RegisterResult::AddFailed`, `StubApplicationsSurface`.
- [x] `tests/CMakeLists.txt` contains literal `add_test(NAME test_cli_flags_parse` and `add_test(NAME test_manifest_registrar`.
- [x] `src/common/include/micmap/common/cli_flags.hpp` exists; contains `struct CliFlags` + `parseCliArgs(int argc, const wchar_t* const* argv)` forward-decl.
- [x] Build of `test_cli_flags_parse` fails citing `parseCliArgs`; build of `test_manifest_registrar` fails citing `manifest_registrar.hpp`. (Verified via tail of cmake build output.)
- [x] `grep -c "case_" tests/test_cli_flags_parse.cpp` returns 12 (≥ 6 required).
- [x] Three Task 2 test files exist.
- [x] `tests/CMakeLists.txt` contains `add_test(NAME test_vr_input_quit_ordering`, `add_test(NAME test_tray_balloon_once`, `add_test(NAME test_vrmanifest_schema`.
- [x] `tests/CMakeLists.txt` contains `add_dependencies(test_vrmanifest_schema micmap)`.
- [x] `tests/CMakeLists.txt` contains literal `MICMAP_MANIFEST_PATH=` `target_compile_definitions` line.
- [x] `test_vr_input_quit_ordering.cpp` references `IVRSystemSeam`, `IEventSink`, `processVREvent`; asserts `AcknowledgeQuit_Exiting()` at index 0 of call log.
- [x] `test_tray_balloon_once.cpp` references `IShellNotifySeam`, `fireBalloonIfFirstSilentLaunch`; asserts both fires-once and does-not-re-fire.
- [x] `test_vrmanifest_schema.cpp` references `MICMAP_MANIFEST_PATH`; asserts `app_key=="bigscreen.micmap"`, `is_dashboard_overlay==true`, and `arguments` string-or-array branch.
- [x] Five new ctest entries (cli_flags_parse, manifest_registrar, vr_input_quit_ordering, tray_balloon_once, vrmanifest_schema) — total `^add_test` count is 8.

## Commits

| Task | Commit  | Files                                                                                                                            |
|------|---------|----------------------------------------------------------------------------------------------------------------------------------|
| 1    | fb48428 | src/common/include/micmap/common/cli_flags.hpp, tests/test_cli_flags_parse.cpp, tests/test_manifest_registrar.cpp, tests/CMakeLists.txt |
| 2    | a12662b | tests/test_vr_input_quit_ordering.cpp, tests/test_tray_balloon_once.cpp, tests/test_vrmanifest_schema.cpp, tests/CMakeLists.txt        |

## Self-Check: PASSED

- File `src/common/include/micmap/common/cli_flags.hpp`: FOUND
- File `tests/test_cli_flags_parse.cpp`: FOUND
- File `tests/test_manifest_registrar.cpp`: FOUND
- File `tests/test_vr_input_quit_ordering.cpp`: FOUND
- File `tests/test_tray_balloon_once.cpp`: FOUND
- File `tests/test_vrmanifest_schema.cpp`: FOUND
- Commit fb48428: FOUND
- Commit a12662b: FOUND
- RED evidence: 5/5 tests failing for the documented missing-impl reasons.

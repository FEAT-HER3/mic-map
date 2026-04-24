---
phase: 03-auto-start
plan: 06
subsystem: apps/micmap
tags: [winmain, cli-fork, silent-boot, tray-balloon, manifest-registrar, auto-02, auto-03, auto-06, d-01, d-02, d-03, d-04, d-06, d-08, d-09, d-10]

# Dependency graph
requires:
  - phase: 03-auto-start
    plan: 01
    provides: "test_cli_flags_parse + test_tray_balloon_once RED scaffolds; cli_flags.hpp public contract (CliFlags struct + parseCliArgs forward-decl)"
  - phase: 03-auto-start
    plan: 03
    provides: "AppConfig.shownTrayNotification field + defensive reader/writer round-trip"
  - phase: 03-auto-start
    plan: 04
    provides: "micmap::steamvr::createManifestRegistrar() + RegisterResult enum — the callable surface invoked by the CLI fork"
provides:
  - "micmap::common::parseCliArgs impl (wcscmp loop) — flips test_cli_flags_parse GREEN"
  - "micmap::apps::IShellNotifySeam + fireBalloonIfFirstSilentLaunch + ProductionShellNotifySeam — flips test_tray_balloon_once GREEN"
  - "WinMain CLI fork: --register-vrmanifest / --unregister-vrmanifest headless paths (exit 0/1)"
  - "WinMain --minimized-aware single-instance mutex (D-08 focus-skip)"
  - "WinMain silent-boot window policy (D-06) — minimizedToTray without ShowWindow"
  - "WinMain first-silent-launch balloon invocation (D-09 / D-10) wired to g_app.nid via ProductionShellNotifySeam"
affects:
  - "Plan 03-07 (retry thread + ordered shutdown) — main.cpp entry is now flag-aware; retry thread + MicMapApp::shutdown refactor can land on top of this foundation without touching WinMain pre-loop"
  - "Phase 4 installer — micmap.exe --register-vrmanifest is now invocable headlessly with 0/1 exit code; Inno Setup post-install step can call it directly"

tech-stack:
  added: []
  patterns:
    - "CLI fork before GUI init — argv parse first, CLI branches return before RegisterClassExW / CreateWindowW / D3D / ImGui (D-02)"
    - "Pitfall 8 argv lifetime: CliFlags populated from wide argv, LocalFree(argvW) immediately after, no argv pointers stored"
    - "Dual-seam balloon: IShellNotifySeam for unit tests (StubShellNotifySeam counts calls); ProductionShellNotifySeam wraps live NOTIFYICONDATAW with clear-after-fire (Pitfall 11)"
    - "Flag is 'we tried' not 'user saw it' (D-09 / Pitfall 7): shownTrayNotification flipped even when Shell suppresses balloon via Focus Assist — prevents re-fire on next silent launch"
    - "Namespace-alignment-to-test: first_launch_balloon.hpp uses `micmap::apps` (plural) to match the Plan 01 RED test; plan instructions said `micmap::app` (singular) but the landed test contract wins"

key-files:
  created:
    - apps/micmap/first_launch_balloon.hpp
    - apps/micmap/first_launch_balloon.cpp
    - src/common/src/cli_flags.cpp  (Plan 01 wrote it to disk but never committed; finalized + committed here)
  modified:
    - apps/micmap/main.cpp (WinMain body — CLI fork + mutex + silent-boot + balloon)
    - apps/micmap/CMakeLists.txt (MICMAP_SOURCES += first_launch_balloon.cpp)
    - src/common/CMakeLists.txt (add_library sources += cli_flags.cpp)
    - tests/CMakeLists.txt (test_tray_balloon_once links first_launch_balloon.cpp + micmap::common + shell32)

key-decisions:
  - "Namespace is micmap::apps (plural), not micmap::app (singular as plan suggested). Reason: tests/test_tray_balloon_once.cpp (landed by Plan 01 commit a12662b) declares `namespace ma = micmap::apps;` — changing the RED test to match singular would have invalidated the Plan 01 RED baseline. Aligned the impl to the test rather than rewrite the test."
  - "Production Shell adapter lives in the SAME .cpp as fireBalloonIfFirstSilentLaunch (not a separate TU). Reason: the adapter is a 15-line wrapper around Shell_NotifyIconW + NOTIFYICONDATAW manipulation; splitting it would create an orphan TU with only a constructor + one 8-line method. The header forward-declares NOTIFYICONDATAW via C-style typedef so the header does not drag <windows.h> into test TUs."
  - "test_tray_balloon_once links first_launch_balloon.cpp directly (not via a support static lib). Reason: the impl has one consumer in tests + one in apps/micmap; creating a micmap_app_test_support STATIC lib would be over-engineering for a single .cpp. The test_tray_balloon_once CMake target lists the source file path explicitly alongside the test .cpp."
  - "No AllocConsole / AttachConsole / FreeConsole anywhere in apps/micmap (D-04 + AUTO-06 grep gate). CLI register/unregister mode runs headless — the default ConsoleLogger's stdout writes are dropped on the floor, and headless callers read the exit code (0 success, 1 failure). Log files under %APPDATA%\\MicMap\\ would be a Plan 07 concern; not needed for AUTO-02/03 acceptance."
  - "Task 1 (parseCliArgs impl) committed separately even though Plan 01's working tree already contained src/common/src/cli_flags.cpp. The file was on disk but never git-added; Plan 01's SUMMARY lists only the header as committed. Staged + committed here under the 03-06 plan-tag so history shows a clean per-task commit chain."

patterns-established:
  - "Comments that could false-positive a regex grep gate avoid the literal token. Example: the D-04 comment says 'no console allocation' instead of 'no AllocConsole'; the D-06 comment says 'legacy command-line substring check on lpCmdLine' instead of 'legacy strstr(lpCmdLine, \"--minimized\")'. Grep gates with `.` in them match comments; comments written with gate-awareness stay passing."
  - "Headless CLI exit-code mapping: RegisterResult::Success -> 0, anything-else -> 1. Matches D-03 and the installer contract (post-install scripts check $? == 0)."

requirements-completed: [AUTO-02, AUTO-03, AUTO-06]
# AUTO-02 (register mechanics) + AUTO-03 (unregister) close at integration
# level here — Plan 04 closed them at unit level; Plan 06 makes them
# invokable from the exe. End-to-end confirmation still requires Plan 07's
# retry thread landing + real SteamVR UAT on a machine with Bigscreen Beyond.
# AUTO-06 (silent boot UX: no console, no focus steal, one-shot balloon)
# closes at the integration level here — grep gates pass, balloon fires
# once per install. Real-HMD UAT is a Plan 07 concern.

# Metrics
duration: ~15min (wall clock, 3 tasks, incremental MSBuild)
completed: 2026-04-23
tasks_completed: 3
files_changed: 7
commits: 3
---

# Phase 03 Plan 06: WinMain CLI Fork + Silent-Boot + First-Launch Balloon Summary

**micmap.exe now understands `--register-vrmanifest`, `--unregister-vrmanifest`, and `--minimized`. The CLI modes are headless (no console window, no GUI init) and return 0/1 via the Plan 04 manifest registrar. Silent auto-launch skips the focus-steal path, keeps the window hidden, and fires a one-shot tray balloon that remembers it was shown via Plan 03's `shownTrayNotification` flag. AUTO-02, AUTO-03, AUTO-06 closed at the integration level; Plan 07 adds the quiet retry thread and ordered shutdown on top of this foundation.**

## Performance

- **Duration:** ~15 min wall clock (3 tasks, incremental MSBuild)
- **Completed:** 2026-04-23
- **Tasks:** 3 (2 TDD-style flipping Plan 01 RED tests GREEN; 1 integration)
- **Files created:** 3 (first_launch_balloon.hpp/cpp; cli_flags.cpp finalized from Plan 01 working tree)
- **Files modified:** 4 (main.cpp; 3 CMakeLists)

## CLI Fork — Before / After

### Before (lines 562-597 of main.cpp — the whole WinMain prelude)

```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"MicMapMain", nullptr);
        if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
        return 0;
    }
    // ... D3D + ImGui + tray init ...
    bool startMin = lpCmdLine && strstr(lpCmdLine, "--minimized");
    if (startMin) { g_app.minimizedToTray = true; }
    else          { ShowWindow(g_app.hwnd, nCmdShow); UpdateWindow(g_app.hwnd); }
```

No CLI fork. Mutex always runs the focus-steal branch. Silent boot detected
via ASCII `strstr` on `lpCmdLine` — works but doesn't survive wide-char
args, and collides with any other `--minimized` substring in the tail.

### After

```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR /*lpCmdLine-unused*/, int nCmdShow) {
    // D-01 Pitfall 8: populate CliFlags BEFORE LocalFree; no argv pointers stored.
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    micmap::common::CliFlags flags = micmap::common::parseCliArgs(argc, argvW);
    if (argvW) { LocalFree(argvW); argvW = nullptr; }

    // D-02 / D-03 / D-04: CLI fork BEFORE RegisterClassExW / D3D / ImGui.
    if (flags.registerManifest || flags.unregisterManifest) {
#ifdef MICMAP_HAS_OPENVR
        vr::EVRInitError initErr = vr::VRInitError_None;
        vr::VR_Init(&initErr, vr::VRApplication_Utility);
        if (initErr != vr::VRInitError_None) {
            MICMAP_LOG_ERROR("CLI VR_Init(Utility) failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
            return 1;  // D-03
        }
        auto registrar = micmap::steamvr::createManifestRegistrar();
        micmap::steamvr::RegisterResult r = flags.registerManifest
            ? registrar->registerApp()
            : registrar->unregisterApp();
        vr::VR_Shutdown();
        return (r == micmap::steamvr::RegisterResult::Success) ? 0 : 1;
#else
        MICMAP_LOG_ERROR("OpenVR not available in this build; cannot register manifest");
        return 1;
#endif
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MicMapSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!flags.minimized) {      // D-08: SteamVR re-launch silent-exits
            HWND w = FindWindowW(L"MicMapMain", nullptr);
            if (w) { PostMessageW(w, WM_COMMAND, IDM_SHOW, 0); SetForegroundWindow(w); }
        }
        return 0;
    }
    // ... D3D + ImGui + tray init (unchanged) ...

    // D-06: silent-mode window policy.
    if (flags.minimized) {
        g_app.minimizedToTray = true;
    } else {
        ShowWindow(g_app.hwnd, nCmdShow);
        UpdateWindow(g_app.hwnd);
    }

    // D-09: first-silent-launch balloon (once per install via AppConfig flag).
    if (flags.minimized && g_app.configManager) {
        micmap::apps::ProductionShellNotifySeam shellAdapter(g_app.nid);
        micmap::apps::fireBalloonIfFirstSilentLaunch(
            shellAdapter, *g_app.configManager, flags.minimized);
    }
```

## Grep Gate Evidence (AUTO-06 / D-04 / D-07)

```
$ grep -r 'SUBSYSTEM:CONSOLE' apps/micmap/
(no matches)

$ grep -r 'AllocConsole\|AttachConsole\|FreeConsole' apps/micmap/
(no matches)

$ grep -r 'strstr(lpCmdLine' apps/micmap/
(no matches)
```

All three guardrails pass. The rebuild after substituting the two
documentation-only mentions of `AllocConsole` and `strstr(lpCmdLine, ...)`
with their descriptive paraphrases keeps the comments informative without
triggering the regex gates. Plan 07 should keep the same discipline.

## Test Evidence — Plan 01 RED Flipped GREEN

```
$ ctest --test-dir build -C Release --output-on-failure
    Start 1: test_placeholder
1/8 Test #1: test_placeholder .................   Passed    0.01 sec
    Start 2: test_config_manager
2/8 Test #2: test_config_manager ..............   Passed    0.04 sec
    Start 3: test_command_queue
3/8 Test #3: test_command_queue ...............   Passed    0.01 sec
    Start 4: test_cli_flags_parse
4/8 Test #4: test_cli_flags_parse .............   Passed    0.01 sec   <-- Plan 01 RED now GREEN
    Start 5: test_manifest_registrar
5/8 Test #5: test_manifest_registrar ..........   Passed    2.41 sec
    Start 6: test_vr_input_quit_ordering
6/8 Test #6: test_vr_input_quit_ordering ......   Passed    0.01 sec
    Start 7: test_tray_balloon_once
7/8 Test #7: test_tray_balloon_once ...........   Passed    0.01 sec   <-- Plan 01 RED now GREEN
    Start 8: test_vrmanifest_schema
8/8 Test #8: test_vrmanifest_schema ...........   Passed    0.01 sec

100% tests passed, 0 tests failed out of 8
```

All eight ctest entries GREEN. Phase 1 regression gate (`hmd_button_test`)
still builds warning-clean (only the pre-existing LIBCMT LNK4098 warning
on `micmap.exe`, unchanged from prior plans).

- `test_cli_flags_parse` 6/6 cases PASS (register / unregister / minimized / combined / unknown-silent-ignore / no-flags).
- `test_tray_balloon_once` 3/3 cases PASS (first silent launch fires + persists; already-shown flag blocks re-fire; user-clicked launch leaves one-shot intact).

## Smoke Test — Headless CLI Invocation (not run)

Not run automatically: requires SteamVR to be either running or not-running
and the outcome depends on that state (exit 0 if running, exit 1 if not).
The CLI branch reachability is confirmed at build + test level: the code
path compiles, links against `createManifestRegistrar()`, and every
branch terminates in `return 0;` or `return 1;` per D-03. Plan 07's UAT
matrix includes the register/unregister round-trip on a real Bigscreen
Beyond rig.

## Decisions Made

See frontmatter `key-decisions`. Headline:

1. **Namespace is `micmap::apps` (plural) — aligned to Plan 01 RED test, not to plan prose.** The Plan 06 prose said `micmap::app`; the landed RED test declared `namespace ma = micmap::apps;`. Changing the test would have invalidated Plan 01's RED commit. Cost: one extra char in the namespace name; benefit: zero churn on a Wave 0 committed artifact.
2. **Production Shell adapter co-located in `first_launch_balloon.cpp`.** The adapter is 15 lines of Win32 boilerplate; splitting it would create a TU with only a ctor + one method. The header uses a C-style `NOTIFYICONDATAW` forward-decl so it does not drag `<windows.h>` into test TUs.
3. **Test links `first_launch_balloon.cpp` directly.** One consumer in tests + one in apps/micmap = no justification for a support static lib.
4. **Task 1 committed as part of Plan 06.** Plan 01's working tree already contained `src/common/src/cli_flags.cpp` (created but never `git add`'d). Staged + committed under a 03-06 tag so the per-task commit chain stays clean and Plan 01's SUMMARY stays accurate (it only listed the header).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] Namespace mismatch: plan prose `micmap::app` vs test-on-disk `micmap::apps`**
- **Found during:** Task 2 (first_launch_balloon.hpp authoring).
- **Issue:** Plan 06 §context <interfaces> and §action A said the namespace should be `micmap::app` (singular). `tests/test_tray_balloon_once.cpp` line 37 (committed 2026-04-23 as Plan 01 Task 2 = a12662b) declares `namespace ma = micmap::apps;` (plural). Using the singular name per the plan would leave the test's `ma::fireBalloonIfFirstSilentLaunch` unresolved at link time.
- **Fix:** Declared the module's namespace as `micmap::apps` (plural). All three call sites in main.cpp also use `micmap::apps::`. No test-side edit; Plan 01's committed RED baseline stays intact.
- **Files modified:** apps/micmap/first_launch_balloon.hpp, apps/micmap/first_launch_balloon.cpp, apps/micmap/main.cpp.
- **Commit:** 170ece9 (Task 2), 91b02dc (Task 3).

**2. [Rule 3 — Blocking] Plan 01's `src/common/src/cli_flags.cpp` was on disk but never committed.**
- **Found during:** Task 1 `git status` pre-commit.
- **Issue:** The Plan 01 SUMMARY listed only `src/common/include/micmap/common/cli_flags.hpp` as a Task 1 create; `src/common/src/cli_flags.cpp` + the `src/common/CMakeLists.txt` line adding it showed up as untracked/modified in `git status`. `test_cli_flags_parse` was passing locally because MSBuild built the uncommitted .cpp into `micmap_common.lib` — a phantom-GREEN state that would have broken on a clean clone.
- **Fix:** Staged both files and committed as Task 1's commit (`f8abbbc`) under a 03-06 feat tag. Confirmed `test_cli_flags_parse` GREEN post-commit (6/6 cases).
- **Files modified:** src/common/src/cli_flags.cpp (already written by Plan 01), src/common/CMakeLists.txt.
- **Commit:** f8abbbc.

**3. [Rule 1 — Bug] Grep gate false positives from documentation comments.**
- **Found during:** Task 3 grep gate verification.
- **Issue:** The D-04 "no AllocConsole" and D-06 "legacy strstr(lpCmdLine, '--minimized')" comments contained the literal tokens the AUTO-06 grep gates forbid. Under the plan's strict grep rule (`grep -c 'AllocConsole' apps/micmap/ MUST return 0`), the comments would be counted as matches even though no code executes them.
- **Fix:** Rewrote the two comments to describe the rule without quoting the forbidden token — "no console allocation" instead of "no AllocConsole"; "legacy command-line substring check on lpCmdLine" instead of "legacy strstr(lpCmdLine, '--minimized')". Intent is unchanged; regex-match surface is clean.
- **Files modified:** apps/micmap/main.cpp.
- **Commit:** 91b02dc (rolled into Task 3's commit since the comments live in the same translation unit).

### Not auto-fixed (intentional)

- **LIBCMT LNK4098 on micmap.exe** — pre-existing warning documented in Plan 04 and Plan 05 SUMMARYs. Out of scope per executor scope-boundary rules.
- **No argv validation beyond silent-ignore.** D-01 explicitly specifies silent ignore; no exit code, no stderr logging. Deferred any future "--help" / usage message to a potential post-Phase-3 polish commit.
- **No IShellNotifySeam guard in main.cpp for `g_app.nid.hWnd == nullptr`.** SetupSystemTray runs before the balloon invocation in all code paths; if SetupSystemTray failed, the balloon would modify a default-constructed NOTIFYICONDATAW. Not a crash (kernel would reject NIM_MODIFY on an invalid hWnd), but a wasted call. Out of scope for a 3-task plan; belongs to Plan 07's shutdown / error-path hardening pass.

---

**Total deviations:** 3 auto-fixed (2 Rule 3 blocking, 1 Rule 1 bug).
**Impact on plan:** No scope creep. Deviations 1 + 2 are alignment to already-committed artifacts; deviation 3 is a grep-gate hygiene fix on comments introduced in the same task.

## Acceptance Criteria Status

**Task 1 (parseCliArgs):**
- [x] `src/common/src/cli_flags.cpp` exists + contains 3 wcscmp calls.
- [x] `src/common/include/micmap/common/cli_flags.hpp` declares struct + parseCliArgs inside `namespace micmap::common` (Plan 01 already shipped this; unchanged).
- [x] `src/common/CMakeLists.txt` lists `src/cli_flags.cpp` in micmap_common sources.
- [x] `ctest -R test_cli_flags_parse` exits 0, all 6 cases PASS.
- [x] Unknown flags silently ignored (case 5 PASS).

**Task 2 (first_launch_balloon):**
- [x] `apps/micmap/first_launch_balloon.hpp` declares IShellNotifySeam, fireBalloonIfFirstSilentLaunch, ProductionShellNotifySeam.
- [x] `first_launch_balloon.cpp` contains `NIIF_RESPECT_QUIET_TIME`, `NIM_MODIFY`, `NIF_INFO`, `Shell_NotifyIconW`, `wcscpy_s` (grep confirms).
- [x] `grep -c 'NIIF_RESPECT_QUIET_TIME' apps/micmap/first_launch_balloon.cpp` returns 1.
- [x] `grep 'nid_.szInfo\[0\] = L.\\0.;' apps/micmap/first_launch_balloon.cpp` matches (Pitfall 11 clear-after-fire).
- [x] `apps/micmap/CMakeLists.txt` MICMAP_SOURCES includes first_launch_balloon.cpp.
- [x] `ctest -R test_tray_balloon_once` exits 0, all 3 cases PASS.

**Task 3 (WinMain integration):**
- [x] main.cpp contains `CommandLineToArgvW(GetCommandLineW(), &argc)` + `LocalFree(argvW)` (Pitfall 8).
- [x] main.cpp contains `parseCliArgs(argc, argvW)` exactly once, BEFORE `CreateMutexW`.
- [x] main.cpp contains `flags.registerManifest || flags.unregisterManifest` gate followed by `vr::VR_Init(&initErr, vr::VRApplication_Utility)` + `return … ? 0 : 1;`.
- [x] main.cpp contains `if (!flags.minimized)` inside the `ERROR_ALREADY_EXISTS` branch (D-08).
- [x] main.cpp contains `fireBalloonIfFirstSilentLaunch(` exactly once.
- [x] main.cpp does NOT contain `strstr(lpCmdLine` — grep clean.
- [x] main.cpp does NOT contain `AllocConsole`, `AttachConsole`, `FreeConsole` — grep clean.
- [x] `grep -r 'SUBSYSTEM:CONSOLE' apps/micmap/` returns empty.
- [x] `cmake --build build --config Release --target micmap` exits 0.
- [x] `ctest --test-dir build -C Release` exits 0 (8/8 PASS, no regressions).
- [x] `hmd_button_test` still builds (Phase 1 regression gate PASSED).

## Commits

| Task | Commit  | Type | Files                                                                                        |
|------|---------|------|----------------------------------------------------------------------------------------------|
| 1    | f8abbbc | feat | src/common/src/cli_flags.cpp, src/common/CMakeLists.txt                                      |
| 2    | 170ece9 | feat | apps/micmap/first_launch_balloon.hpp, apps/micmap/first_launch_balloon.cpp, apps/micmap/CMakeLists.txt, tests/CMakeLists.txt |
| 3    | 91b02dc | feat | apps/micmap/main.cpp                                                                         |

## Threat Flags

None. Threat register from PLAN.md `<threat_model>` covered:

- `T-03-06-01` (Tampering — argv use-after-free Pitfall 8): **mitigated** — CliFlags is populated into a plain-struct copy BEFORE `LocalFree(argvW)`; pointer is nulled after free. No argv pointers stored anywhere.
- `T-03-06-02` (EoP — admin-context CLI injection): **mitigated** — exact-match `wcscmp` against 3 known flags; unknown flags silently ignored; no argument values consumed; no shell-out; no FS writes outside OpenVR API.
- `T-03-06-05` (Spoofing — balloon text): **mitigated** — title + body are compile-time wide-literal strings (`L"MicMap"`, `L"Running in the system tray. Click the icon to open."`). No interpolation, no user input.
- `T-03-06-03`, `-04`, `-06`, `-07`: accepted / N/A per the plan's disposition table. Unchanged.

No new threat surface introduced.

## Self-Check: PASSED

- File `apps/micmap/first_launch_balloon.hpp`: FOUND
- File `apps/micmap/first_launch_balloon.cpp`: FOUND
- File `src/common/src/cli_flags.cpp`: FOUND (committed in this plan)
- File `apps/micmap/main.cpp`: FOUND (modified)
- File `apps/micmap/CMakeLists.txt`: FOUND (modified)
- File `src/common/CMakeLists.txt`: FOUND (modified)
- File `tests/CMakeLists.txt`: FOUND (modified)
- Commit `f8abbbc`: FOUND in git log
- Commit `170ece9`: FOUND in git log
- Commit `91b02dc`: FOUND in git log
- `ctest --test-dir build -C Release`: 8/8 PASS
- `test_cli_flags_parse`: GREEN (6/6 cases)
- `test_tray_balloon_once`: GREEN (3/3 cases)
- Grep gates: strstr(lpCmdLine = 0, AllocConsole|AttachConsole|FreeConsole = 0, SUBSYSTEM:CONSOLE = 0
- `hmd_button_test`: builds warning-clean

## Next Phase Readiness

- AUTO-02 (register) + AUTO-03 (unregister) + AUTO-06 (silent boot UX) closed at integration level. Phase 4 installer can invoke `micmap.exe --register-vrmanifest` as a post-install step; exit code carries the success/failure signal.
- Plan 03-07 (quiet retry thread + ordered MicMapApp shutdown) can now assume:
  1. `flags.minimized` is available in WinMain scope (pass into initialize() or store on MicMapApp if needed).
  2. `fireBalloonIfFirstSilentLaunch` has already fired by the time the retry thread starts (initialization order: argv parse → mutex → D3D/ImGui/tray init → balloon → main loop).
  3. The CLI branches do NOT go through the main loop and do NOT need MicMapApp::shutdown orchestration.
- Real-SteamVR UAT is the remaining Phase 3 gate: multi-restart cycle to validate SetApplicationAutoLaunch persistence (research flag OpenVR #1547). That validation belongs at Phase 3 exit after Plan 07 lands.

---
*Phase: 03-auto-start*
*Plan: 06*
*Completed: 2026-04-23*

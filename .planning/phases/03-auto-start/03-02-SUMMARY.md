---
phase: 03-auto-start
plan: 02
subsystem: apps/micmap
tags: [auto-start, vrmanifest, configure_file, pathcch, a2-resolved, openvr, cmake]
dependency-graph:
  requires:
    - micmap (existing exe target — gets POST_BUILD step + Pathcch link)
    - PROJECT_VERSION (root CMakeLists.txt project() — already 0.1.0)
    - micmap_common (register_manifest helper)
    - openvr_api (register_manifest helper)
    - test_vrmanifest_schema (Plan 03-01 RED test — now GREEN)
  provides:
    - apps/micmap/app.vrmanifest.in (CMake-substituted JSON template)
    - build/bin/$<CONFIG>/app.vrmanifest (build-dir manifest sibling of micmap.exe)
    - build/bin/$<CONFIG>/register_manifest.exe (Phase 03 throwaway helper)
    - Pathcch.lib link on micmap.exe (precondition for Plan 03-04 D-21 + Plan 03-06)
    - A2 LOCKED → "arguments": "--minimized" (string form, empirically validated)
  affects:
    - tests/test_vrmanifest_schema.cpp (loosened OR-branch dropped; strict string assertion)
    - .planning/phases/03-auto-start/03-04-PLAN.md (forward-slash <critical_pitfall> callout added)
    - .planning/phases/03-auto-start/03-VALIDATION.md (A2 row checked)
tech-stack:
  added: []
  patterns:
    - "configure_file(... @ONLY) for build-time JSON token substitution (D-19)"
    - "Two-step emit: configure_file → ${CMAKE_CURRENT_BINARY_DIR}, then POST_BUILD copy_if_different to $<TARGET_FILE_DIR:micmap> for multi-config generator correctness"
    - "Naked Windows-SDK lib link (Pathcch) without find_library — built into SDK since Vista"
    - "Throwaway tooling guarded by WIN32 + MICMAP_HAS_OPENVR (delete after Phase 03 exit)"
    - "Empirical-test variant pattern: ship two configure_file invocations during the checkpoint, delete the loser in the resolution task"
key-files:
  created:
    - .planning/phases/03-auto-start/03-02-A2-RESULT.md
  modified:
    - apps/micmap/CMakeLists.txt
    - tests/test_vrmanifest_schema.cpp
    - .planning/phases/03-auto-start/03-04-PLAN.md
    - .planning/phases/03-auto-start/03-VALIDATION.md
    - tools/register_manifest.cpp
  deleted:
    - apps/micmap/app.vrmanifest.A2test_array.in
decisions:
  - "A2 RESOLVED — STRING form wins. SteamVR auto-launched `micmap.exe --minimized` from `arguments: \"--minimized\"` on Bigscreen Beyond + Win11. Array variant deleted from codebase. Test_vrmanifest_schema asserts is_string() && == \"--minimized\" strictly."
  - "Forward-slash manifest path is a SILENT KILLER — vrserver treats forward-slash UTF-8 paths as the working directory, fails to find a backslash to split on, logs `Working directory <path> is invalid. Skipping`, and returns no error to the caller. Surfaced as <critical_pitfall> in Plan 03-04 with mandatory runtime guard."
  - "register_manifest.cpp extended during empirical test to also call SetApplicationAutoLaunch (single-shot register + enable autolaunch) — folded into this commit because it directly produced the A2 evidence."
  - "POST_BUILD copy_if_different chosen over single configure_file targeting the per-config dir because configure_file() does not honor generator expressions in its OUTPUT path (CMake limitation; multi-config MSBuild requirement)."
metrics:
  duration_seconds: ~480
  completed: 2026-04-23
---

# Phase 03 Plan 02: app.vrmanifest + Pathcch + A2 Resolution Summary

CMake `configure_file(@ONLY)` template wired so every build emits
`build/bin/$<CONFIG>/app.vrmanifest` beside `micmap.exe` with substituted
`bigscreen.micmap` app key + `0.1.0` version. Pathcch.lib linked into
micmap.exe (precondition for Plan 03-04 / 03-06 path resolution). Open
item A2 (arguments-field form) empirically resolved on live SteamVR:
**string-form wins**; array-form variant deleted; schema test locked.
Forward-slash-path silent-skip pitfall surfaced for downstream plans.

## A2 Resolution — Empirical Evidence

**Decision:** STRING form wins. Canonical manifest ships with
`"arguments": "--minimized"`.

**Test environment**

| Property | Value |
|----------|-------|
| HMD | Bigscreen Beyond |
| OS | Windows 11 Pro 10.0.26200 |
| SteamVR | latest (2026-04-23) |
| Manifest path | `C:\Users\decid\Documents\projects\mic-map\build\bin\app.vrmanifest` |

**Observed command line (verbatim from PowerShell `Get-CimInstance Win32_Process`):**

```
"C:\Users\decid\Documents\projects\mic-map\build\bin\Release\micmap.exe" --minimized
```

SteamVR passed exactly **one** argument, `--minimized`, with no extra
quoting, no shell artifacts. Auto-launch fired ~1 second after `vrserver`
became ready (effectively immediate).

Full evidence + downstream prescriptions:
`.planning/phases/03-auto-start/03-02-A2-RESULT.md`.

## Critical Pitfall Surfaced

During the empirical test, registering the manifest with FORWARD slashes
(via `appconfig.json` `manifest_paths` edit) caused vrserver to log:

```
App bigscreen.micmap Working directory C:\Users\decid\...\app.vrmanifest is invalid. Skipping
```

vrserver was treating the **entire forward-slash path AS the working
directory** (no `\` to split on for parent-dir extraction). Manifest was
silently skipped, no error code surfaced. Switching the appconfig entry to
double-backslashed form caused vrserver to accept the manifest immediately
and auto-launch fired.

**Action taken:** Added `<critical_pitfall>` callout to
`.planning/phases/03-auto-start/03-04-PLAN.md` requiring:

1. The wide-char path computed by `resolveManifestAbsolutePath()` MUST
   stay backslash-canonical (no `std::filesystem::path::generic_string()`,
   no manual `\\` → `/` substitution).
2. A runtime guard in `OpenVRManifestRegistrar::registerApp` that returns
   `RegisterResult::AddFailed` if the UTF-8 path contains `/`.
3. A regression unit test in `test_manifest_registrar` covering the
   forward-slash failure mode.

## Verification Evidence

**Build** — `cmake --build build --config Release --target micmap test_vrmanifest_schema`:

```
micmap.vcxproj -> build/bin/Release/micmap.exe
Staging app.vrmanifest beside micmap.exe   Copying config files...
Generated app.vrmanifest at build/bin/Release/app.vrmanifest
test_vrmanifest_schema.vcxproj -> build/bin/Release/test_vrmanifest_schema.exe
```

**Test** — `ctest --test-dir build -C Release -R test_vrmanifest_schema`:

```
Start 8: test_vrmanifest_schema
1/1 Test #8: test_vrmanifest_schema ...........   Passed    0.02 sec
100% tests passed, 0 tests failed out of 1
```

**Pathcch link** — `grep -c "Pathcch" apps/micmap/CMakeLists.txt`: `1`
(line 56: `target_link_libraries(micmap PRIVATE shell32 dwmapi Pathcch)`).

**Sole configure_file invocation** — only the canonical
`app.vrmanifest.in → build/app.vrmanifest` block remains; A2test_array
block removed.

**Phase-exit grep gates (still passing)**

- `grep -r "SUBSYSTEM:CONSOLE" apps/micmap` → no matches.
- `grep -r "AllocConsole\|AttachConsole" apps/micmap` → no matches.

## Commits

| Task | Hash | Description |
|------|------|-------------|
| 1 | `6db28b2` | Wire app.vrmanifest configure_file + Pathcch link + register_manifest helper |
| 2 | n/a | Live SteamVR empirical test (A2 RESOLVED — see A2-RESULT.md) |
| 3 | `3615894` | Lock A2 to STRING-form arguments; surface forward-slash pitfall |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] register_manifest.cpp extended to enable auto-launch**

- **Found during:** Task 2 (empirical test execution)
- **Issue:** The Task 1 helper called only `AddApplicationManifest` — but the empirical test required SteamVR's auto-launch flag to actually fire on cold-start. Without it, the manifest registers but auto-launch stays OFF, defeating the test's purpose.
- **Fix:** Added a `SetApplicationAutoLaunch("bigscreen.micmap", true)` call after the manifest registration. Updated header comments and exit-code documentation.
- **Files modified:** `tools/register_manifest.cpp`
- **Commit:** `3615894` (folded into Task 3)

### Architectural Discovery (Surfaced for Downstream Plans)

**Forward-slash manifest path silent-skip** — see Critical Pitfall section
above. Not a deviation in Plan 03-02 itself (no code in this plan calls
`AddApplicationManifest`), but a load-bearing pitfall for Plans 03-04 and
03-06. Surfaced via `<critical_pitfall>` callout in 03-04-PLAN.md per
deviation Rule 2.

## Auth Gates

None — empirical SteamVR test required no authentication. SteamVR's
`IVRApplications` API is loopback-only and requires no admin rights.

## Threat Flags

None new. Plan 03-02 threat model (T-03-02-01..05) remains accurate;
forward-slash pitfall is a correctness issue, not a security boundary
crossing (no untrusted input ever reaches `AddApplicationManifest`).

## Self-Check: PASSED

**Files claimed as created/modified — verified on disk:**

- FOUND: `apps/micmap/CMakeLists.txt` (modified, configure_file count = 1 invocation + 1 comment)
- FOUND: `apps/micmap/app.vrmanifest.in` (string form, unchanged from Task 1)
- MISSING (intentional): `apps/micmap/app.vrmanifest.A2test_array.in` (deleted in Task 3)
- FOUND: `tests/test_vrmanifest_schema.cpp` (string-only assertion verified by ctest GREEN)
- FOUND: `.planning/phases/03-auto-start/03-02-A2-RESULT.md` (created)
- FOUND: `.planning/phases/03-auto-start/03-04-PLAN.md` (modified — `<critical_pitfall>` callout present)
- FOUND: `.planning/phases/03-auto-start/03-VALIDATION.md` (modified — A2 row checked)
- FOUND: `tools/register_manifest.cpp` (modified — autolaunch call added)

**Commits claimed — verified in git log:**

- FOUND: `6db28b2` (Task 1)
- FOUND: `3615894` (Task 3)

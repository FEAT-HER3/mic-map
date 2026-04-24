---
phase: 04-installer
plan: 03
subsystem: [build-system, installer-packaging]
tags: [INST-07, D-18, D-19, D-20, D-21, D-01]
requires:
  - CMake >=3.20
  - Inno Setup 6 (ISCC.exe on PATH or in standard Program Files location) -- WARNING-gated, not hard-required
  - micmap target (apps/micmap/CMakeLists.txt)
  - driver_micmap target (driver/CMakeLists.txt)
  - app.vrmanifest generation via configure_file (from Plan 03-02)
provides:
  - add_custom_target(package) -- opt-in (NO ALL) build target invoking cmake --install + ISCC.exe
  - find_program(ISCC_EXECUTABLE) cache FILEPATH -- overridable via -DISCC_EXECUTABLE=<path>
  - Deterministic stage tree at build/stage/ matching D-01 layout
  - install(FILES app.vrmanifest DESTINATION bin) -- single source of truth for what ships (D-19)
affects:
  - apps/micmap/CMakeLists.txt -- new install(FILES) rule for app.vrmanifest
  - CMakeLists.txt (root) -- deletes copy_distributable_files + adds package target
  - driver/CMakeLists.txt -- rename install DESTINATION driver/ -> drivers/ (deviation fix for D-01)
tech-stack:
  added:
    - ISCC.exe invocation via find_program + add_custom_target
  patterns:
    - cmake --install as single source of truth (D-19)
    - /D<NAME>=<value> defines passed to ISCC (D-21)
    - opt-in (non-ALL) package target with DEPENDS on primary artifacts
key-files:
  created: []
  modified:
    - apps/micmap/CMakeLists.txt
    - CMakeLists.txt
    - driver/CMakeLists.txt
decisions:
  - ISCC absence = WARNING, not FATAL_ERROR -- devs without Inno Setup can still build/test; only package target gated off
  - Driver install destinations renamed driver/ -> drivers/ (plural) to match D-01 and Phase 4 validation criteria
  - POST_BUILD copy of app.vrmanifest retained unchanged for local dev workflow
metrics:
  completed: 2026-04-24
  tasks: 2
  files_modified: 3
  commits: 3
---


# Phase 4 Plan 03: CMake package target + deterministic install stage

Establishes the CMake build-system machinery that produces the single shipped artifact via `cmake --build build --target package --config Release` -- running `cmake --install` into a deterministic stage tree, then invoking ISCC.exe with `/D` defines to build `build/installer/MicMap-Setup-v{version}.exe`.

## One-Liner

Opt-in `package` custom target chains `cmake --install --prefix build/stage` into `ISCC.exe` invocation with `/DMICMAP_VERSION` / `/DSTAGE_DIR` / `/DOUTPUT_DIR` defines (D-21), producing MicMap-Setup-v0.1.0.exe at build/installer/; legacy `copy_distributable_files` target deleted so `install()` rules are the lone source of truth for shipped files (D-19).

## Tasks Completed

| # | Task | Commit | Notes |
|---|------|--------|-------|
| 1 | Add install(FILES app.vrmanifest DESTINATION bin) to apps/micmap/CMakeLists.txt | c0edb5e | Placed after existing POST_BUILD block; POST_BUILD copy retained for dev-workflow. |
| 2 | Add find_program(ISCC) + add_custom_target(package) + delete copy_distributable_files in root CMakeLists.txt | 396fb4a | WARNING (not FATAL_ERROR) if ISCC absent; package target DEPENDS micmap + conditionally driver_micmap. |
| — | [Deviation Rule 1/3] Rename driver/ -> drivers/ install destinations in driver/CMakeLists.txt | 7869e3b | Fix pre-existing path violation of D-01; required for plan acceptance. |

## ISCC_EXECUTABLE Resolution

On this dev machine, `find_program` resolved ISCC_EXECUTABLE to:

```
ISCC_EXECUTABLE:FILEPATH=C:/Program Files (x86)/Inno Setup 6/ISCC.exe
```

Cached as FILEPATH so `-DISCC_EXECUTABLE=<path>` override works for portable Inno Setup installs.

## Stage Layout Confirmed

After `cmake --install build --prefix build/stage --config Release`:

```
build/stage/bin/app.vrmanifest
build/stage/bin/micmap.exe
build/stage/drivers/micmap/bin/win64/driver_micmap.dll
build/stage/drivers/micmap/driver.vrdrivermanifest
build/stage/drivers/micmap/resources/driver.vrresources
build/stage/drivers/micmap/resources/settings/default.vrsettings
build/stage/share/micmap/config.json
```

Matches D-01 (nested under `drivers/micmap/`) and feeds directly into Plan 04's `installer/MicMap.iss` via `{#STAGE_DIR}\drivers\micmap\*` and `{#STAGE_DIR}\bin\*` source paths.

## Package Target Behavior (verified end-to-end)

Running `cmake --build build --target package --config Release`:
1. Clean + recreate `build/stage` (via `cmake -E remove_directory` + `make_directory`)
2. `cmake --install build --prefix build/stage --config Release` -- staged tree above
3. `cmake -E make_directory build/installer`
4. ISCC.exe invocation with `/DMICMAP_VERSION=0.1.0 /DSTAGE_DIR=<abs>/build/stage /DOUTPUT_DIR=<abs>/build/installer <abs>/installer/MicMap.iss`
5. Echo output path: `Installer built: <abs>/build/installer/MicMap-Setup-v0.1.0.exe`

Step 4 correctly fails at this point -- `installer/MicMap.iss` does not yet exist (it's authored in Plan 04). ISCC prints its banner + "The system cannot find the path specified." -- the expected RED state per the plan's §<objective> "Plan 03 does NOT yet create installer/MicMap.iss...". Upstream steps 1-3 succeed deterministically.

## Delta vs 04-RESEARCH.md

| Item | Research recommendation | Actual implementation | Rationale |
|------|-------------------------|------------------------|-----------|
| ISCC-not-found behavior | `message(FATAL_ERROR ...)` (RESEARCH §Open Question 5) | `message(WARNING ...)` + guard package target with `if(NOT ISCC_EXECUTABLE) ... else()` | Devs without Inno Setup can still build and run ctest -- only the installer packaging is gated off. Matches 04-PATTERNS.md §"MODIFIED CMakeLists.txt" Edit-3 recommendation. |
| Package-target `ALL` flag | Not specified | NO `ALL` | Opt-in: `cmake --build` does not rebuild the installer on every iteration. Explicit `--target package` required. |

## Deviations from Plan

### Rule 1 (Bug) / Rule 3 (Blocker): Driver install destinations

- **Found during:** Task 2 stage-layout verification.
- **Issue:** `driver/CMakeLists.txt` install rules used `DESTINATION driver/micmap/...` (singular). Violated D-01 (installer lands files at `{SteamVR}\drivers\micmap\`, plural) and the plan's own acceptance-criteria command `test -f build/stage/drivers/micmap/bin/win64/driver_micmap.dll`. The 04-03-PLAN.md `<interfaces>` block at lines 85-89 incorrectly asserted "existing driver install rules ... produce build/stage/drivers/micmap/..." -- that was never true of the rules on disk.
- **Impact if left unfixed:** Plan 04's `installer/MicMap.iss` `[Files]` section would source from `{#STAGE_DIR}\driver\micmap\*` (singular) and `vrpathreg adddriver` would be called on `{SteamVR}\driver\micmap` (singular), breaking SteamVR driver discovery. Downstream Plans 4-8 would cascade wrong-path references.
- **Fix:** Rename all 5 install DESTINATION paths in `driver/CMakeLists.txt` from `driver/...` to `drivers/...`. Local dev-workflow POST_BUILD copies (lines 127-140) unchanged -- they still emit to `build/driver/micmap/**` (singular) for local SteamVR registration during development.
- **Files modified:** driver/CMakeLists.txt
- **Commit:** 7869e3b

### Rule-compliance note: scripts/*.bat files

- Plan 09 (task 04-09-01) deletes `scripts/install_driver.bat`, `uninstall_driver.bat`, `install_driver_test.bat`, `test_driver.bat` from disk. Plan 03's scope only removes CMakeLists.txt references to them (which it did -- the `file(GLOB SCRIPT_FILES)` + `configure_file` loop + `copy_distributable_files ALL` target are gone). The `.bat` files themselves remain on disk as expected; their deletion is Plan 09's concern.

## Verification Results

| Check | Command | Result |
|-------|---------|--------|
| copy_distributable_files deleted | `! grep -q "copy_distributable_files" CMakeLists.txt` | PASS |
| find_program ISCC present | `grep -q "find_program(ISCC_EXECUTABLE" CMakeLists.txt` | PASS |
| add_custom_target(package) | `grep -q "add_custom_target(package" CMakeLists.txt` | PASS |
| /D defines present | `grep -qE "DMICMAP_VERSION\|DSTAGE_DIR\|DOUTPUT_DIR" CMakeLists.txt` | PASS |
| install(FILES app.vrmanifest) | grep in apps/micmap/CMakeLists.txt | PASS |
| cmake -B build exits 0 | fresh configure | PASS |
| ISCC_EXECUTABLE in cache | `grep -q "^ISCC_EXECUTABLE" build/CMakeCache.txt` | PASS |
| drivers/micmap/bin/win64/driver_micmap.dll staged | after cmake --install | PASS |
| bin/micmap.exe staged | after cmake --install | PASS |
| bin/app.vrmanifest staged | after cmake --install | PASS |
| drivers/micmap/driver.vrdrivermanifest staged | after cmake --install | PASS |

## Hand-off for Plan 04 (installer/MicMap.iss author)

The Plan 03 `/D` defines ISCC receives are:

| Define | Value at this machine |
|--------|-----------------------|
| `MICMAP_VERSION` | `0.1.0` (from `project(MicMap VERSION 0.1.0 ...)`) |
| `STAGE_DIR` | `<abs-repo>/build/stage` |
| `OUTPUT_DIR` | `<abs-repo>/build/installer` |

Plan 04's `installer/MicMap.iss` MUST reference these via `{#STAGE_DIR}` and `{#OUTPUT_DIR}` and `{#MICMAP_VERSION}` preprocessor tokens.

Staged source paths `installer/MicMap.iss` should consume for `[Files]`:
- `{#STAGE_DIR}\drivers\micmap\*` (recursively) -- driver DLL + vrdrivermanifest + resources/
- `{#STAGE_DIR}\bin\micmap.exe` -- app executable
- `{#STAGE_DIR}\bin\app.vrmanifest` -- SteamVR app manifest (auto-launch entry)

Per D-01 nested layout, the Inno `{app}` should map to `{SteamVR}\drivers\micmap\`, so:
- `[Files] Source: "{#STAGE_DIR}\drivers\micmap\*"; DestDir: "{app}"; Flags: recursesubdirs ...`
- `[Files] Source: "{#STAGE_DIR}\bin\micmap.exe"; DestDir: "{app}\bin"; ...`
- `[Files] Source: "{#STAGE_DIR}\bin\app.vrmanifest"; DestDir: "{app}\bin"; ...`

Exit criterion for the full `cmake --build build --target package --config Release` flow: `test -f build/installer/MicMap-Setup-v0.1.0.exe`. That's Plan 10's end-to-end gate (after Plan 04 lands `MicMap.iss`).

## Environment notes

- Build verification in this worktree used `OPENVR_SDK_PATH=/c/Users/decid/Documents/projects/bey-closer-t1/extern/openvr`, matching the main-tree build's cached setting. The local `external/openvr/` subdir does NOT exist in the repo; the main tree's CMakeCache.txt pointed at bey-closer-t1's bundled OpenVR SDK. No change to project policy -- this is a developer-environment-configuration concern, not a plan-scope concern.

## Self-Check: PASSED

Files verified on disk:

- apps/micmap/CMakeLists.txt:87 contains `install(FILES ... app.vrmanifest`
- CMakeLists.txt contains `find_program(ISCC_EXECUTABLE`, `add_custom_target(package`, and all three `/D...` defines; no `copy_distributable_files` reference
- driver/CMakeLists.txt lines 151-164: all 5 DESTINATION paths use `drivers/` (plural)

Commits verified via `git log daeb40a..HEAD`:

- c0edb5e feat(04-03): install app.vrmanifest alongside micmap.exe
- 396fb4a feat(04-03): add package target (D-18/D-19/D-20/D-21); delete copy_distributable_files
- 7869e3b fix(04-03): rename driver install destination driver/ -> drivers/ (D-01)

Stage tree verified via `find build/stage -type f | sort` matches D-01 layout (see "Stage Layout Confirmed" section).

## TDD Gate Compliance

Task 1 followed TDD-flavored flow for build-config (no unit test applicable; empirical RED/GREEN via cmake --install):
- RED: verified `test -f build/stage/bin/app.vrmanifest` returned MISSING before the install() rule was added.
- GREEN: after adding install(FILES), `test -f build/stage/bin/app.vrmanifest` returns EXISTS.

Task 2 was a pure build-system migration (find_program + custom target + deletion). No isolated unit-level TDD cycle; the effective tests are configure-success + cache-var presence + stage-tree content, all green. Landed as a single feat commit per the TDD-wiring-task exception pattern (same shape as Plan 04-02 Task 2).

The deviation fix (driver/ -> drivers/) is a `fix(...)` commit not a `feat(...)` -- no TDD cycle required for a rename-only correctness fix; verification is the plan's own acceptance grep. Confirmed green end-to-end.

## Threat Flags

None. T-04-01 (find_program path tampering) mitigated as planned: default paths are Program Files (x86) + Program Files only, no PATH scan; user overrides via explicit `-DISCC_EXECUTABLE=...` are self-attested. T-04-01 (stage-dir tampering) accepted per dev-workspace trust model (code-signing deferred to v1.x per D-16).

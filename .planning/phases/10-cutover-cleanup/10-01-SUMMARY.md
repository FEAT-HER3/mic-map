---
phase: 10-cutover-cleanup
plan: 01
subsystem: build-and-runtime-plumbing
tags: [phase-10, cutover-cleanup, wave-1, log-rotation, version-ssot, plumbing]
requires:
  - cmake/AssertCoVersioning.cmake (P10 Wave 0; flips from skip-on-NOT-EXISTS to enforcing all 5 assertions after this wave)
  - tests/test_log_rotation.cpp (P10 Wave 0 RUN-RED scaffold; flips to GREEN after this wave)
  - src/common/src/sinks/file_log_sink.cpp (existing P8 FileLogSink; rotation extends in-place)
  - src/common/include/micmap/common/log_sink.hpp (factory surface unchanged)
  - apps/micmap/CMakeLists.txt (existing P3+ target wiring; .rc + define added)
  - driver/CMakeLists.txt (existing driver target; MICMAP_DRIVER_VERSION renamed)
  - driver/src/device_provider.cpp:803-804 (existing SVR-10 init log line; consumer of the renamed define)
  - root CMakeLists.txt (existing project() + ISCC pass; SSoT include + project()-VERSION bump)
provides:
  - cmake/version.cmake (SSoT MICMAP_VERSION='1.6.0' + MICMAP_VERSION_QUAD='1,6,0,0' + semver-validation guard)
  - installer/version.iss.in (Inno Setup ISPP template producing #define MICMAP_VERSION 'X.Y.Z' from SSoT)
  - apps/micmap/micmap.rc.in (NEW VS_VERSION_INFO template for micmap.exe; replaces orphaned static apps/micmap/micmap.rc)
  - driver/driver_micmap.rc.in (NEW VS_VERSION_INFO template for driver_micmap.dll; FILETYPE VFT_DLL)
  - FileLogSink::log()-driven 5MB / 5-generation MoveFileExW rotation with one-shot stderr soft-fail (D-14..D-17)
  - MICMAP_VERSION_STRING compile define on BOTH micmap and driver_micmap targets (single canonical name)
  - PROJECT_VERSION=1.6.0 (bumped from 0.1.0 to co-version with SSoT)
affects:
  - CMakeLists.txt (project() VERSION 0.1.0 -> 1.6.0; include(cmake/version.cmake) + configure_file inserted; comment added above the existing ISCC /DMICMAP_VERSION pass documenting Wave 1 vs 10-06 ownership)
  - apps/micmap/CMakeLists.txt (set(MICMAP_VERSION ${PROJECT_VERSION}) removed; configure_file(micmap.rc.in) + target_sources(micmap.rc) + target_compile_definitions(MICMAP_VERSION_STRING) added)
  - driver/CMakeLists.txt (target_compile_definitions: MICMAP_DRIVER_VERSION -> MICMAP_VERSION_STRING; configure_file(driver_micmap.rc.in) + target_sources(driver_micmap.rc) added)
  - driver/src/device_provider.cpp (#ifndef block + DriverLog format-arg consumer renamed MICMAP_DRIVER_VERSION -> MICMAP_VERSION_STRING)
  - apps/micmap/resource.h (trailing newline appended; rc.exe RC1004 fix surfaced once the static .rc was retired and the generated .rc started compiling)
  - .gitignore (extended with installer/version.iss build-artifact)
  - apps/micmap/micmap.rc (DELETED — superseded by micmap.rc.in)
tech-stack:
  added: []
  patterns:
    - "Synchronous on-write rotation in FileLogSink: file_size() check after each log() write; 5MB cap fires rotate() within the existing per-sink mutex; no rotation thread (D-14)"
    - "MoveFileExW(MOVEFILE_REPLACE_EXISTING) walk for 5-generation atomic-on-NTFS rename chain: drop .5, then .4->.5 .3->.4 .2->.3 .1->.2, then active->.1; subsequent log() reopens via std::ofstream(app)"
    - "Soft-fail rotation per D-17: one-shot stderr fprintf + warnedRotationFailure_ flag; never lose log lines; non-file egress prevents global-Logger fan-out recursion"
    - "Single-version source-of-truth via cmake/version.cmake + configure_file(installer/version.iss.in) + per-target configure_file(*.rc.in) — one MICMAP_VERSION semver edit drives every artifact (driver/client compile defines, .iss define, VS_VERSION_INFO RC tuple)"
    - "Per-target VS_VERSION_INFO via configure_file(.rc.in -> ${CMAKE_CURRENT_BINARY_DIR}/*.rc) + target_sources — RC compiler picks up the generated .rc, embeds VERSIONINFO into PE/DLL"
    - "Absolute-source-tree icon path via @MICMAP_ICON_PATH@ configure token — generated .rc lives in build/, so a relative '../../installer/micmap.ico' would not resolve"
key-files:
  created:
    - cmake/version.cmake
    - installer/version.iss.in
    - apps/micmap/micmap.rc.in
    - driver/driver_micmap.rc.in
    - .planning/phases/10-cutover-cleanup/10-01-SUMMARY.md
  modified:
    - src/common/src/sinks/file_log_sink.cpp
    - CMakeLists.txt
    - apps/micmap/CMakeLists.txt
    - driver/CMakeLists.txt
    - driver/src/device_provider.cpp
    - apps/micmap/resource.h
    - .gitignore
  deleted:
    - apps/micmap/micmap.rc
decisions:
  - "FileLogSink::log() keeps its existing open-per-write discipline (std::ofstream(path, app|binary) inside an inner block scope); the ofstream destructor closes the handle BEFORE the size-check + rotate() call so MoveFileExW can rename the active log without contending with an open Win32 handle. No cached handle, no header changes, no new threads — strict minimal diff against P8."
  - "rotate() is declared noexcept and on Windows-only #ifdef; non-Windows builds skip rotation (the audio stack is Windows-only this milestone, but FileLogSink is portable test-fixture-friendly so the no-op stub keeps test_log_rotation buildable on any host)."
  - "[Rule 3 - Blocking] PROJECT_VERSION bumped 0.1.0 -> 1.6.0 in root CMakeLists.txt project() declaration. Required because the existing ISCC /DMICMAP_VERSION=${PROJECT_VERSION} pass at line 153 stays in place for Wave 1 per the plan, but it must agree with cmake/version.cmake's MICMAP_VERSION='1.6.0' for the wave to be cohesive (otherwise installer artifacts would stamp 0.1.0 while VERSIONINFO + version.iss read 1.6.0 — exactly the drift the SSoT exists to prevent). 10-06 removes the /D pass and the SSoT becomes the sole authority."
  - "[Rule 1 - Bug] apps/micmap/resource.h had no trailing newline at EOF; rc.exe RC1004 'unexpected end of file' fired on first compile. Surfaced now because the static apps/micmap/micmap.rc was orphaned (never registered with the build) — the new generated .rc IS in target_sources, so RC actually compiles it, and the no-newline header tripped the lexer. One-byte fix."
  - "[Rule 2 - Missing critical functionality] The pre-existing apps/micmap/micmap.rc was orphaned: not listed in any target_sources, never compiled, never embedded in micmap.exe. The plan called for replacing micmap.rc with micmap.rc.in via configure_file; we additionally wired the OUTPUT into target_sources(micmap PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/micmap.rc) so VS_VERSION_INFO actually lands in the binary (verified via PowerShell Get-Item .VersionInfo: FileVersion=1.6.0.0). Same wiring applied to driver_micmap (driver had NO .rc at all pre-Wave-1)."
  - "Used absolute-source-tree icon path (@MICMAP_ICON_PATH@ = ${CMAKE_SOURCE_DIR}/installer/micmap.ico) in micmap.rc.in. The plan's verbatim '../../installer/micmap.ico' would NOT resolve because the generated .rc lives in ${CMAKE_CURRENT_BINARY_DIR} (build/apps/micmap/micmap.rc), not the source tree. RESEARCH §Pattern 5 noted the issue parenthetically; we wrote the configure-time substitution to be unambiguous on any build-dir layout."
  - "AssertCoVersioning lint passes the all-5-assertions enforcement path (was skip-on-NOT-EXISTS at Wave 0)."
metrics:
  duration: ~50 minutes
  completed_date: 2026-05-10
  task_count: 2
  file_count: 12
---

# Phase 10 Plan 01: Wave 1 Plumbing Summary (Log Rotation + Version SSoT)

Wave 1 was pure plumbing — two concerns invisible to runtime behavior: (1) FileLogSink rotation per TEST-03 / D-14..D-17, and (2) the single-version source-of-truth for cmake / installer / RC resources per INST-09 / D-18..D-21. Both Wave 0 RED gates flip GREEN: `LogRotation` ctest moves from RUN-RED to PASS, and `AssertCoVersioning` ctest moves from skip-on-NOT-EXISTS to fully enforcing all 5 assertions.

## What Shipped

**FileLogSink rotation (`src/common/src/sinks/file_log_sink.cpp`):**

- After each `log()` write, the existing per-line `std::ofstream(app|binary)` scope closes the handle, then `std::filesystem::file_size(path_)` is queried (cheap MFT lookup on Windows). If `>= 5MB` (`kMaxLogBytes`), `rotate()` runs synchronously inside the existing `fileLogMutex` lock — no rotation thread, no new synchronization (the parent `MultiSinkLogger` mutex already serializes writes; this is defense-in-depth).
- `rotate()` (private, `noexcept`, `#ifdef _WIN32`-gated) drops `.5`, then walks `.4 -> .5`, `.3 -> .4`, `.2 -> .3`, `.1 -> .2` via `MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING)`, then renames the active log to `.1`. Subsequent `log()` calls re-open `path_` via the existing `std::ofstream(app)` and naturally create a fresh empty file.
- Failure mode (D-17): any `MoveFileExW(...)` returning FALSE triggers a one-shot `std::fprintf(stderr, ...)` warning (with the offending path pair + GetLastError) and sets `warnedRotationFailure_ = true`. The flag prevents log spam if the rotation continues to fail (e.g., AV holding a handle); the bound is soft — log lines never get dropped.
- Anti-patterns avoided: no rotation thread, no `MOVEFILE_WRITE_THROUGH` (no-op for same-volume metadata renames), no global Logger call from inside `rotate()` (would recurse if the global Logger fans to this sink), no exceptions (rotate is `noexcept`).

**FileLogSink class location confirmed:** the class definition lives ENTIRELY inside `src/common/src/sinks/file_log_sink.cpp` (lines 26-63 pre-edit; file_log_sink.cpp also exposes the `makeFileLogSink` factory). No public header was added or modified — `src/common/include/micmap/common/log_sink.hpp` remains unchanged because it only declares the `ILogSink` interface and the `makeFileLogSink` factory, not the concrete class shape.

**Single-version SSoT (`cmake/version.cmake` + `installer/version.iss.in` + 2 `.rc.in` templates + CMake wiring):**

- `cmake/version.cmake`: declares `MICMAP_VERSION="1.6.0"`, validates the value matches `^[0-9]+\.[0-9]+\.[0-9]+$` (FATAL on mismatch — defensive against accidental `"1.6"` or `"1.6.0-rc1"` strings during release prep), splits into `MICMAP_VERSION_QUAD="1,6,0,0"` for RC numeric tuples, and prints `MicMap version: 1.6.0 (RC quad: 1,6,0,0)` on every configure.
- Root `CMakeLists.txt`: `include(cmake/version.cmake)` lands after `project(...)` and before any `add_subdirectory`; `configure_file(installer/version.iss.in installer/version.iss @ONLY)` runs at configure-time. The existing `/DMICMAP_VERSION=${PROJECT_VERSION}` ISCC pass at line 153 STAYS in place (per plan; 10-06 removes it after MicMap.iss adopts `#include "version.iss"`); a comment block above the line documents the temporary duplication. `project()` VERSION is bumped from `0.1.0` to `1.6.0` so the `/D` pass agrees with the SSoT (Rule 3 deviation — see Decisions).
- `installer/version.iss.in`: ISPP template producing `#define MICMAP_VERSION "1.6.0"`. The generated `installer/version.iss` is added to `.gitignore` so the build artifact is never committed.
- `apps/micmap/micmap.rc.in`: NEW VS_VERSION_INFO template; `FILEVERSION/PRODUCTVERSION = @MICMAP_VERSION_QUAD@`; string fields use `@MICMAP_VERSION@`. Replaces the orphaned static `apps/micmap/micmap.rc` (DELETED). Icon path passed via `@MICMAP_ICON_PATH@ = ${CMAKE_SOURCE_DIR}/installer/micmap.ico` so the generated .rc (which lives in `build/apps/micmap/`) finds the asset on any build-dir layout.
- `driver/driver_micmap.rc.in`: NEW VS_VERSION_INFO template; `FILETYPE VFT_DLL`; DLL-appropriate strings (FileDescription "MicMap SteamVR driver — sidecar-on-HMD trigger pipeline", OriginalFilename "driver_micmap.dll").
- `apps/micmap/CMakeLists.txt`: removed `set(MICMAP_VERSION ${PROJECT_VERSION})` (the SSoT include at root level supersedes; `MICMAP_VERSION` is in cache scope here automatically); added `configure_file(micmap.rc.in -> build/.../micmap.rc) @ONLY`, `target_sources(micmap PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/micmap.rc)`, and `target_compile_definitions(micmap PRIVATE MICMAP_VERSION_STRING="${MICMAP_VERSION}")`.
- `driver/CMakeLists.txt`: renamed `MICMAP_DRIVER_VERSION="${PROJECT_VERSION}"` -> `MICMAP_VERSION_STRING="${MICMAP_VERSION}"` (single canonical define name across both binaries); added the .rc.in configure + `target_sources` block.
- `driver/src/device_provider.cpp`: the only in-tree consumer of `MICMAP_DRIVER_VERSION` — both the `#ifndef MICMAP_DRIVER_VERSION ... #define ... "0.0.0" #endif` fallback (lines 64-66) and the `DriverLog(...)` format-arg consumer at line 803-804 — both renamed to `MICMAP_VERSION_STRING`. No other source files referenced the old define (verified via `grep -rn "MICMAP_DRIVER_VERSION" driver/ src/ apps/ tests/`).

## Verification

**Per-task automated checks (all PASS):**

- Task 1 (FileLogSink rotation): `cmake --build build --target test_log_rotation` builds clean; `ctest --test-dir build -C Debug -R LogRotation --output-on-failure` -> `100% tests passed, 0 tests failed out of 1` in 3.09s. Test exercises all 7 cases from `tests/test_log_rotation.cpp`: 1MB underflow keeps `.1` absent; `~6MB` cumulative drives at least one rotation (`.1` appears, active log size resets `< 5MB`); 7 rotations total leave `.1..5` present and `.6` absent (oldest dropped each time); `directory_iterator` finds no `.tmp` leftovers (atomic `MoveFileExW` does not produce intermediate files); cleanup `remove_all` succeeds.
- Task 2 (Version SSoT): `cmake -B build -S .` succeeds and prints `-- MicMap version: 1.6.0 (RC quad: 1,6,0,0)` from `cmake/version.cmake`; `installer/version.iss` is generated containing `#define MICMAP_VERSION "1.6.0"`; `ctest --test-dir build -C Debug -R AssertCoVersioning --output-on-failure` -> PASS in 0.02s (the lint flips from skip-on-NOT-EXISTS to fully enforcing all 5 assertions; cmake-side `MICMAP_VERSION` value-matches the value baked into `installer/version.iss`).

**Build verification (informational, not gated):**

- `cmake --build build --target micmap --config Debug` -> builds cleanly; `Get-Item build\bin\Debug\micmap.exe | Select VersionInfo` reports `FileVersion=1.6.0.0`, `ProductVersion=1.6.0`, `FileDescription="MicMap - Microphone pattern-based click input for SteamVR"`, `ProductName="MicMap"` — proves VS_VERSION_INFO is actually embedded (the orphaned static .rc had FileVersion `1.0.0.0` even when wired; the new flow embeds the SSoT-driven 1.6.0).
- `cmake --build build --target driver_micmap --config Debug` -> builds cleanly; `Get-Item build\driver\micmap\bin\win64\driver_micmap.dll | Select VersionInfo` reports `FileVersion=1.6.0.0`, `ProductVersion=1.6.0`, `FileDescription="MicMap SteamVR driver - sidecar-on-HMD trigger pipeline"`, `ProductName="MicMap Driver"` — driver DLL gains VS_VERSION_INFO for the first time (driver previously had no .rc at all).
- Pre-existing `LINK : warning LNK4098: defaultlib 'LIBCMT' conflicts...` on both targets is unchanged from prior phases (CRT mismatch noise from third-party libs; tracked elsewhere).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] PROJECT_VERSION bumped 0.1.0 -> 1.6.0**
- **Found during:** Task 2 (configuring root CMakeLists.txt)
- **Issue:** Plan calls for the existing ISCC `/DMICMAP_VERSION=${PROJECT_VERSION}` pass at line 153 to STAY in place for Wave 1 with `AssertCoVersioning` "verifying they match", but `project()` VERSION was at `0.1.0` (set in P0) while the SSoT `MICMAP_VERSION` is `1.6.0`. With both unchanged, the installer would stamp `MicMap-Setup-v0.1.0.exe` while the binaries embed `1.6.0` and `installer/version.iss` reads `1.6.0` — exactly the cross-surface drift the SSoT exists to prevent. AssertCoVersioning does NOT cross-check `PROJECT_VERSION` so the lint would not catch it.
- **Fix:** Update the `project(MicMap VERSION 1.6.0 ...)` declaration in root CMakeLists.txt; document with an inline comment. After 10-06 removes the `/D` pass, the project()-VERSION can become independent of the SSoT again (or the SSoT could subsume project() — backlog).
- **Files modified:** `CMakeLists.txt` (lines 7-19)
- **Commit:** e7c009f

**2. [Rule 2 - Missing critical functionality] apps/micmap/micmap.rc was orphaned**
- **Found during:** Task 2 (verifying RC compilation actually emits VS_VERSION_INFO)
- **Issue:** The pre-existing `apps/micmap/micmap.rc` static file was never registered with the `micmap` target — `grep "micmap.rc"` in `apps/micmap/CMakeLists.txt` returned no matches; `build/apps/micmap/micmap.vcxproj` contained no `<ResourceCompile Include=...micmap.rc...>` element. The .rc was lying dead in the source tree, never compiled, never embedded; `micmap.exe` had no VS_VERSION_INFO at all (or whatever Windows synthesized as a fallback). The plan called for replacing the static .rc with a configure_file output, but did not call out the orphaned-file part — it would have remained orphaned after the rename if we had not also wired `target_sources(micmap PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/micmap.rc)`. Same orphan situation for the driver (no driver/.rc existed at all — driver DLL had no VS_VERSION_INFO).
- **Fix:** Added `target_sources(micmap PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/micmap.rc)` and `target_sources(driver_micmap PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/driver_micmap.rc)` to the respective CMakeLists.txt files. Verified VS_VERSION_INFO embeds correctly via PowerShell `Get-Item` on the built binaries — both report 1.6.0 / 1.6.0.0.
- **Files modified:** `apps/micmap/CMakeLists.txt`, `driver/CMakeLists.txt`
- **Commit:** e7c009f

**3. [Rule 1 - Bug] apps/micmap/resource.h missing trailing newline**
- **Found during:** Task 2 (first build attempt of micmap.exe with the new .rc)
- **Issue:** `rc.exe` RC1004 "unexpected end of file found" at line 21 of resource.h. The file ended with `#define WM_STEAMVR_QUIT (WM_USER + 2)` and no newline. RC compiler is stricter than the C preprocessor about EOF behavior — the no-newline triggered the error as soon as the .rc actually started consuming the header. Surfaced now because the orphan-.rc situation (above) meant nothing previously compiled the header from RC context.
- **Fix:** `printf '\n' >> apps/micmap/resource.h` — single-byte trailing newline. Verified rebuild succeeds.
- **Files modified:** `apps/micmap/resource.h`
- **Commit:** e7c009f

**4. [Rule 1 - Bug equivalent] Plan's relative icon path would not resolve**
- **Found during:** Task 2 (writing apps/micmap/micmap.rc.in)
- **Issue:** Plan specified `IDI_MICMAP_ICON ICON "../../installer/micmap.ico"` verbatim from the static .rc. But `configure_file` puts the generated .rc under `${CMAKE_CURRENT_BINARY_DIR}` (e.g., `build/apps/micmap/micmap.rc`), and `../../installer/...` from there resolves to `build/installer/...` — which does not exist. The static .rc worked (when it did work) only because it was IN the source tree; the .rc.in flow needs an absolute path or a relocatable token.
- **Fix:** Added `set(MICMAP_ICON_PATH "${CMAKE_SOURCE_DIR}/installer/micmap.ico")` in `apps/micmap/CMakeLists.txt` before the `configure_file` call; `IDI_MICMAP_ICON ICON "@MICMAP_ICON_PATH@"` in `micmap.rc.in`. Generated .rc gets the absolute source-tree path, which is unambiguous on any build-dir layout. RESEARCH §Pattern 5 noted this issue parenthetically; the plan body did not reproduce that nuance.
- **Files modified:** `apps/micmap/CMakeLists.txt`, `apps/micmap/micmap.rc.in`
- **Commit:** e7c009f

## Threat Model Compliance

All 7 STRIDE threats from the plan's threat register are addressed:

- T-10-01-01 (Tampering, log rotation under concurrent writers): mitigated. Existing `fileLogMutex` plus parent `MultiSinkLogger` mutex serialize writes; `MoveFileExW` is atomic on NTFS for same-volume metadata-only renames; rotate() runs alone within the lock.
- T-10-01-02 (DoS, log file growth): mitigated. 5MB cap × 5 generations = 25MB max per binary (driver + client = 50MB max); D-14 enforces.
- T-10-01-03 (DoS, rotation thread starvation): mitigated. Synchronous on-write — no rotation thread; rare event (<1/day per CONTEXT D-14); per-rotation cost is microseconds.
- T-10-01-04 (DoS, rotation failure -> log loss): mitigated. D-17 soft-fail with one-shot stderr warning + `warnedRotationFailure_` flag; bound is soft, never lose log lines.
- T-10-01-05 (Tampering, %APPDATA%\MicMap symlink attack): accepted. Per-user dir; only the user can plant symlinks targeting their own files; out of scope for v1.6.
- T-10-01-06 (Tampering, attacker-controlled MICMAP_VERSION): accepted. cmake/version.cmake is in repo; whoever can edit it has repo write access (separate concern); semver regex catches accidents.
- T-10-01-07 (Tampering, drift between cmake-side and installer-side MICMAP_VERSION): mitigated. `AssertCoVersioning` lint flips to fully enforcing in this wave; FATAL on mismatch; ctest registered (runs on every build).

## Threat Flags

None — this plan introduces no new network endpoints, auth paths, file-access patterns, or trust-boundary surface beyond the already-tracked log file path (per-user `%APPDATA%\MicMap\` directory, T-10-01-05 in the existing threat model).

## Known Stubs

None. FileLogSink rotation is fully wired and exercised by the LogRotation ctest (7 cases all PASS). Version SSoT is fully consumed by both binaries (PowerShell `VersionInfo` proves embed) + the generated installer/version.iss (lint proves match). The existing ISCC `/DMICMAP_VERSION=${PROJECT_VERSION}` pass at root CMakeLists.txt:153 is a temporary co-existence (documented inline) — 10-06 removes it after `MicMap.iss` adopts `#include "version.iss"`. Not a stub; a tracked plan-of-work seam.

## Commits

| Task | Description                                                                                          | Commit  |
| ---- | ---------------------------------------------------------------------------------------------------- | ------- |
| 1    | feat(10-01): add 5MB / 5-generation rotation to FileLogSink                                          | b732b28 |
| 2    | feat(10-01): single-version source-of-truth (cmake/version.cmake + RC + ISS)                         | e7c009f |

## Self-Check: PASSED

- src/common/src/sinks/file_log_sink.cpp — FOUND (rotate() helper added; rotation behavior present)
- cmake/version.cmake — FOUND (MICMAP_VERSION="1.6.0" + MICMAP_VERSION_QUAD="1,6,0,0")
- installer/version.iss.in — FOUND (#define MICMAP_VERSION "@MICMAP_VERSION@")
- installer/version.iss (generated) — FOUND (#define MICMAP_VERSION "1.6.0")
- apps/micmap/micmap.rc.in — FOUND (VS_VERSION_INFO with @MICMAP_VERSION_QUAD@ + @MICMAP_VERSION@)
- driver/driver_micmap.rc.in — FOUND (VS_VERSION_INFO VFT_DLL)
- apps/micmap/micmap.rc (deleted) — VERIFIED ABSENT (`git ls-files` no match)
- CMakeLists.txt — FOUND (include(cmake/version.cmake) + configure_file(version.iss.in) + project() VERSION 1.6.0)
- apps/micmap/CMakeLists.txt — FOUND (configure_file(micmap.rc.in) + target_sources + MICMAP_VERSION_STRING)
- driver/CMakeLists.txt — FOUND (MICMAP_VERSION_STRING + configure_file(driver_micmap.rc.in) + target_sources)
- driver/src/device_provider.cpp — FOUND (MICMAP_DRIVER_VERSION renamed to MICMAP_VERSION_STRING in 2 places)
- apps/micmap/resource.h — FOUND (trailing newline appended)
- .gitignore — FOUND (installer/version.iss listed)
- ctest LogRotation — PASS
- ctest AssertCoVersioning — PASS (was skip at Wave 0)
- micmap.exe VersionInfo — FileVersion=1.6.0.0, ProductVersion=1.6.0
- driver_micmap.dll VersionInfo — FileVersion=1.6.0.0, ProductVersion=1.6.0
- b732b28 — FOUND (`git log` confirms; Task 1 commit)
- e7c009f — FOUND (`git log` confirms; Task 2 commit)

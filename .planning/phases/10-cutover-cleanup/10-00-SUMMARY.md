---
phase: 10-cutover-cleanup
plan: 00
subsystem: build-validation
tags: [phase-10, cutover-cleanup, wave-0, lints, red-tolerant, scaffolds]
requires:
  - tests/CMakeLists.txt P9 Wave 3 block (insertion anchor)
  - cmake/AssertNoClientTraining.cmake (verbatim shape model for AssertNoClientDetection)
  - cmake/AssertNoConfigWriteInClient.cmake (verbatim shape model for AssertNoButtonRoute single-root variant)
  - cmake/AssertReplayNoVrApi.cmake (verbatim shape model for AssertCoVersioning skip-on-NOT-EXISTS)
  - tests/driver/detection_settings_propagation_test.cpp (plain-main + MM_CHECK convention)
  - src/common/include/micmap/common/log_sink.hpp (FileLogSink factory surface for test_log_rotation)
  - apps/micmap/src/ (impl-file directory exists; 10-02/03/06 will populate)
provides:
  - cmake/AssertNoClientDetection.cmake (RED-tolerant via apps/mic_test allowlist + qualifier-narrowed regexes; FATAL on existing v1.5 client body — go-live deferred to 10-05)
  - cmake/AssertNoButtonRoute.cmake (dual-scope lint, FATAL on existing POST /button + IDriverApi::tap — go-live deferred to 10-05)
  - cmake/AssertCoVersioning.cmake (5 EXISTS-gated assertions, skip-on-NOT-EXISTS at Wave 0 — REGISTERED NOW)
  - tests/test_tray_glyph_state_machine.cpp (10 cases, build-RED until 10-02)
  - tests/test_fail_pill_priority.cpp (9 cases, build-RED until 10-03)
  - tests/test_log_rotation.cpp (7 cases, RUN-RED until 10-01 — pure runtime gate, compiles at Wave 0)
  - tests/test_version_mismatch.cpp (5 cases, build-RED until 10-06)
  - tests/CMakeLists.txt "# ---- Phase 10 Wave 0 (RED scaffold) ----" block (5 new ctest entries: AssertCoVersioning + TrayGlyphStateMachine + FailPillPriority + LogRotation + VersionMismatch)
affects:
  - tests/CMakeLists.txt (single block append after P9 Wave 3 close marker; no edits to existing blocks)
tech-stack:
  added: []
  patterns:
    - "Wave 0 RED scaffold pattern: skip-on-NOT-EXISTS lint + EXISTS-gated test source lists keep cmake configure clean while build/run failures are the Nyquist gate (canonical P7/P8/P9 idiom)"
    - "Qualifier-prefix regex narrowing for source-grep lints (e.g. detector->loadTrainingData rather than bare loadTrainingData) — mirrors P9 D-03 Rule-3 deviation precedent"
    - "Single-writer cutover protocol: lint scripts ship at Wave 0; their ctest registrations defer to the cutover wave that deletes the existing violations atomically"
    - "Multi-scope lint via two pinned dir args (BUTTON_ROUTE_ROOT + STEAMVR_ROOT) with hardcoded regex sets — T-10-00-02 mitigation (silent-broadening attacker mitigation)"
key-files:
  created:
    - cmake/AssertNoClientDetection.cmake
    - cmake/AssertNoButtonRoute.cmake
    - cmake/AssertCoVersioning.cmake
    - tests/test_tray_glyph_state_machine.cpp
    - tests/test_fail_pill_priority.cpp
    - tests/test_log_rotation.cpp
    - tests/test_version_mismatch.cpp
    - .planning/phases/10-cutover-cleanup/10-00-SUMMARY.md
  modified:
    - tests/CMakeLists.txt
decisions:
  - "Used the apps/mic_test/ allowlist guard verbatim from AssertNoClientTraining (T-10-00-01 mitigation) — even if a future caller widens CLIENT_ROOTS by mistake the headless harness stays exempt under TEST-01"
  - "Two pinned scope args in AssertNoButtonRoute (driver/src + src/steamvr) prevent silent broadening; regex sets per scope are hardcoded so an attacker editing build args alone cannot weaken the lint (T-10-00-02 mitigation)"
  - "test_log_rotation differs from the other 3 scaffolds: it is a PURE RUN-RED gate (compiles + links cleanly at Wave 0 against the existing FileLogSink) because rotation is an in-place behavior change to FileLogSink::log() rather than a new symbol — documented in the file banner and the CMakeLists comment"
  - "AssertCoVersioning regex for installer/version.iss uses literal `\\t` rather than POSIX `[[:space:]]` for cross-cmake-version portability"
  - "AssertCoVersioning is registered to ctest at Wave 0 (skip-on-NOT-EXISTS keeps it GREEN until 10-01 lands cmake/version.cmake); the OTHER two new lints are NOT registered until 10-05 because they currently FATAL on existing code — same single-writer cutover discipline as P8 D-07 / P9 D-23"
metrics:
  duration: ~30 minutes
  completed_date: 2026-05-10
  task_count: 3
  file_count: 8
---

# Phase 10 Plan 00: Cutover & Cleanup Wave 0 RED Scaffold Summary

Land the Phase 10 Wave 0 lint and test infrastructure: 3 source-grep lint scripts under `cmake/` and 4 plain-main test scaffolds under `tests/`, wired into `tests/CMakeLists.txt` with EXISTS-gated source lists so cmake configure stays clean while subsequent plans turn the RED gates GREEN.

## What Shipped

**3 new lint scripts under `cmake/`:**

- `cmake/AssertNoClientDetection.cmake` — GLOB_RECURSE scan with `apps/mic_test/` allowlist guard (D-23 / T-10-00-01); 5 forbidden-pattern regexes covering `IAudioCapture` / `INoiseDetector` / `IStateMachine` (qualifier-narrowed by non-identifier boundary) + the two specific call shapes `detector->loadTrainingData` and `createFFTDetector`. Currently FATALs on `apps/micmap/main.cpp` (expected — go-live in 10-05 alongside the v1.5 client-body deletion).
- `cmake/AssertNoButtonRoute.cmake` — dual-scope lint with two pinned root args (`BUTTON_ROUTE_ROOT` for `driver/src/`, `STEAMVR_ROOT` for `src/steamvr/`); regex sets are hardcoded per scope (T-10-00-02 mitigation). Currently FATALs on `driver/src/http_server.cpp` (POST /button) + `src/steamvr/src/driver_api.cpp` (IDriverApi::tap impl) — expected; go-live in 10-05.
- `cmake/AssertCoVersioning.cmake` — 5 EXISTS-gated assertions covering `cmake/version.cmake` definition, semver shape, `installer/version.iss.in` template existence, generated `installer/version.iss` existence, and value-match between cmake-side `MICMAP_VERSION` and the `#define MICMAP_VERSION "X.Y.Z"` baked into the generated installer file. RED-tolerant via skip-on-NOT-EXISTS at every gate; emits `STATUS skipped (cmake/version.cmake does not exist yet)` at Wave 0.

**4 new test scaffolds under `tests/`:**

- `test_tray_glyph_state_machine.cpp` — 10 cases covering HEALTH-08 `deriveTrayGlyph` state derivation per CONTEXT D-04 / D-05 (pure-armed; error trumps everything; triggered observed; 300ms pulse window inside `cooldown` state; pulse expiry; error trumps triggered; triggered trumps armed). Build-RED until 10-02 lands `apps/micmap/src/tray_glyph.{hpp,cpp}`.
- `test_fail_pill_priority.cpp` — 9+ cases covering D-08 priority stacking (FAIL-02 > FAIL-03 > FAIL-01 > FAIL-05); FAIL-02 vs FAIL-03 disambiguation by `vrserverRunning`; mic-permission deep-link string; "Open SteamVR" action label; per-FAIL-kind `dismissable` bits per D-10. Build-RED until 10-03 lands `apps/micmap/src/fail_pill.{hpp,cpp}`.
- `test_log_rotation.cpp` — 7 cases covering FileLogSink rotation per D-14..D-17 (5MB cap, .1..5 generations, atomic move, no .tmp leftovers). **PURE RUN-RED gate** — compiles + links cleanly at Wave 0 against existing `micmap::common::makeFileLogSink` (post-P8); RUN-FAILs assertions until 10-01 lands `rotate()` behavior in `FileLogSink::log()`.
- `test_version_mismatch.cpp` — 5 cases covering D-20 client startup version-mismatch (`Match` / `Mismatch` / `DriverVersionMissing`); pill `blocking == false` (warn-only); pill `dismissable == true`. Build-RED until 10-06 lands `apps/micmap/src/version_mismatch.{hpp,cpp}`.

**`tests/CMakeLists.txt` Phase 10 Wave 0 block:**

Appended after the existing `# ---- end Phase 9 Wave 3 ----` close marker. Adds `add_test(NAME AssertCoVersioning ...)` (registered NOW), inline `NOTE` comments documenting why `AssertNoClientDetection` + `AssertNoButtonRoute` ctest registrations are deferred to 10-05, and 4 EXISTS-gated test executables (`TrayGlyphStateMachine`, `FailPillPriority`, `LogRotation`, `VersionMismatch`). Closes with `# ---- end Phase 10 Wave 0 ----` for grep-ability.

## Verification

**Per-task automated checks (all PASS):**

- Task 1: `cmake -DSOURCE_DIR=. -P cmake/AssertCoVersioning.cmake` emits `STATUS skipped (cmake/version.cmake does not exist yet — Wave 0 RED-tolerant; 10-01 lands the SSoT)` — clean exit. AssertNoClientDetection + AssertNoButtonRoute correctly FATAL on existing code (proves the regexes match the v1.5/v1.6 violations they will guardrail post-cutover).
- Task 2: All 4 scaffold files exist; each contains `MM_CHECK` and the `Phase 10 Wave 0 RED scaffold` banner.
- Task 3: `cmake -B build-p10w0 -S . -DCMAKE_BUILD_TYPE=Debug` succeeds without `CMake Error` or `FATAL`. `ctest -N` lists `#28: AssertCoVersioning`, `#29: TrayGlyphStateMachine`, `#30: FailPillPriority`, `#31: LogRotation`, `#32: VersionMismatch`. AssertNoClientDetection + AssertNoButtonRoute correctly NOT in the ctest list (deferred to 10-05).
- Bonus: `ctest -C Debug -R AssertCoVersioning` runs and PASSES at Wave 0 (skip-on-NOT-EXISTS path).

**Build-RED gate confirmation (expected at Wave 0):** `test_tray_glyph_state_machine`, `test_fail_pill_priority`, `test_version_mismatch` will fail to BUILD because `tray_glyph.hpp` / `fail_pill.hpp` / `version_mismatch.hpp` don't exist yet — that build failure IS the Nyquist gate. Configure stays clean because the EXISTS-gated source lists keep the cpp file out of the source list when the impl is missing. `test_log_rotation` BUILDS at Wave 0 against existing FileLogSink.

## Deviations from Plan

None — plan executed exactly as written. The plan body was the full source for all 3 lints + all 4 test scaffolds + the CMake block; only minor fidelity adjustments:

- The plan's example regex for AssertCoVersioning Assertion 5 used `[ \\t]+` (cmake regex literal). The shipped script uses `[ \t]+` (cmake regex character class with space + tab). Functionally equivalent on cmake 3.x+; portable across the cmake 4.3 in use here.
- The `apps/mic_test` allowlist guard match in AssertNoClientDetection retained the `MATCHES "apps/mic_test"` form (forward-slash) because cmake `get_filename_component` returns forward-slash paths even on Windows — proven by the AssertNoClientTraining lint that has been working with this exact pattern through Phase 9.

## Threat Model Compliance

All 7 STRIDE threats from the plan's threat register are addressed:

- T-10-00-01 (Tampering, AssertNoClientDetection scope drift): mitigated by hardcoded `apps/mic_test` allowlist guard inside the lint body.
- T-10-00-02 (Tampering, AssertNoButtonRoute silent broadening): mitigated by two pinned scope args + hardcoded regex sets.
- T-10-00-03 (Tampering, AssertCoVersioning fixed-path-only design): no GLOB; configure-time only; an attacker who can write cmake/version.cmake already has repo write access.
- T-10-00-04 (Spoofing, EXISTS-gated source lists): accepted (local dev/CI trust boundary).
- T-10-00-05 (DoS, RED scaffolds at Wave 0): accepted by design — failing to build is the gate, not a DoS.
- T-10-00-06 (DoS, test_log_rotation %TEMP% I/O): bounded — one-shot test, removes its tmp dir on cleanup; ~36MB max during the 5-cap drive.
- T-10-00-07 (Information disclosure, MM_CHECK output): no PII; just file:line + expression text.

## Threat Flags

None — this plan creates no new network endpoints, auth paths, file access patterns, or schema surface beyond the test scaffolds' bounded `%TEMP%` I/O (already in the threat register).

## Known Stubs

None. The 4 test scaffolds have full assertion bodies — no placeholder `TODO` / "coming soon" / `=nullptr` data sources flowing to UI rendering. The build-RED gates are intentional Nyquist gates per the plan's design (`<#include "tray_glyph.hpp">` etc.) and are tracked in the per-test banner block, not as stubs.

## Commits

| Task | Description                                                                  | Commit  |
| ---- | ---------------------------------------------------------------------------- | ------- |
| 1    | feat(10-00): add 3 source-grep lints (AssertNoClientDetection, AssertNoButtonRoute, AssertCoVersioning) | 60baf81 |
| 2    | test(10-00): add 4 RED-tolerant test scaffolds (tray glyph, fail pill, log rotation, version mismatch) | 190ddb1 |
| 3    | build(10-00): wire AssertCoVersioning + 4 RED scaffolds into ctest           | a1112ba |

## Self-Check: PASSED

- cmake/AssertNoClientDetection.cmake — FOUND
- cmake/AssertNoButtonRoute.cmake — FOUND
- cmake/AssertCoVersioning.cmake — FOUND
- tests/test_tray_glyph_state_machine.cpp — FOUND
- tests/test_fail_pill_priority.cpp — FOUND
- tests/test_log_rotation.cpp — FOUND
- tests/test_version_mismatch.cpp — FOUND
- tests/CMakeLists.txt — FOUND (block appended)
- 60baf81 — FOUND
- 190ddb1 — FOUND
- a1112ba — FOUND

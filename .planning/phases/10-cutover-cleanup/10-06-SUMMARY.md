---
phase: 10-cutover-cleanup
plan: 06
subsystem: installer-co-versioning-and-version-mismatch-pill
tags: [phase-10, cutover-cleanup, wave-6, installer, co-versioning, version-mismatch-pill]
requires:
  - cmake/AssertCoVersioning.cmake (10-00; stays GREEN)
  - cmake/version.cmake (10-01; SSoT consumed by configure_file)
  - installer/version.iss.in (10-01; configure_file template)
  - installer/version.iss (10-01 generated; #include target)
  - apps/micmap/src/fail_pill.hpp (10-03; FailPill + FailKind::VersionMismatch reused)
  - apps/micmap/src/tray_glyph.hpp (10-02 / 10-03; HealthSnapshot transitively included)
  - apps/micmap/main.cpp pollDriverHealth + renderUI driver-health pane (10-02 / 10-03 hooks)
  - tests/test_version_mismatch.cpp (10-00 RED scaffold; flips GREEN)
  - src/steamvr/include/micmap/steamvr/driver_api.hpp HealthView (Rule 2 extended)
  - src/steamvr/src/driver_api.cpp DriverApi::getHealth (Rule 2 driver_version parse)
provides:
  - installer/MicMap.iss #include "version.iss" at top + 3 tray .ico [Files] entries
  - CMakeLists.txt with redundant ISCC /DMICMAP_VERSION pass REMOVED (SSoT via include)
  - apps/micmap/src/version_mismatch.{hpp,cpp} -- pure compareVersions + buildVersionMismatchPill
  - g_versionUx VersionUxState global + first-poll-success gate + amber pill render hook
  - HealthView.driver_version field consumed by version-mismatch comparison (Rule 2)
  - test_version_mismatch ctest GREEN (was build-RED at Wave 0)
affects:
  - installer/MicMap.iss (#include + 3 [Files] entries)
  - CMakeLists.txt (3 lines removed: ISCC /D pass + comment block)
  - apps/micmap/src/fail_pill.hpp (FailPill::headlineText -> text rename; Rule 3)
  - apps/micmap/main.cpp (1 include + g_versionUx + poll-callback hook + render block + pill.headlineText -> pill.text rename)
  - apps/micmap/CMakeLists.txt (src/version_mismatch.cpp registered)
  - src/steamvr/include/micmap/steamvr/driver_api.hpp (HealthView.driver_version field added; Rule 2)
  - src/steamvr/src/driver_api.cpp (DriverApi::getHealth parse driver_version; Rule 2)
tech-stack:
  added: []
  patterns:
    - "Inno Setup ISPP #include of generated version.iss (Pattern 4): the #include resolves relative to MicMap.iss directory; absolute-path ISCC invocation at root CMakeLists.txt's package target ensures correct directory (Pitfall 5 mitigation). The existing #ifndef MICMAP_VERSION fallback retained as safety net for hand-runs of ISCC without the cmake step (e.g., devs iterating on the .iss without rerunning configure)."
    - "First-poll-success gate for one-shot warnings: a session-scoped boolean (g_versionUx.firstHealthSuccessSeen) guards the version-mismatch comparison so the WARNING log fires exactly once per process lifetime regardless of the 1Hz /health poll cadence. Same shape as the orphan-recovery one-shot in 09-03."
    - "Pure-derivation seam pattern (compareVersions + buildVersionMismatchPill): inputs are two strings, output is an enum + std::optional<FailPill>; no globals, no Win32, fully testable in headless ctest. The Win32 wrapper (renderUI amber pill block) consumes the optional and the existing g_versionUx state; the seam keeps the test fixture flat (5 cases, plain-main) per the 10-02 deriveTrayGlyph + 10-03 pickActivePill pattern."
    - "Field-name alignment with Wave 0 RED scaffold: tests/test_version_mismatch.cpp accesses pill->text. The 10-03 fail_pill struct exposed a headlineText field by the same scaffold-precedent reading; renaming headlineText -> text in fail_pill.hpp (and the single render-path consumer in main.cpp) satisfies the 10-06 contract while keeping 10-03's tests/test_fail_pill_priority GREEN (that scaffold does NOT reference the renamed field by name -- positional brace-init in fail_pill.cpp is unaffected). Same load-bearing-Wave-0-contract reading 10-02 + 10-03 followed."
    - "Append-only HealthView evolution: the existing 4-field struct {driver_loaded, driver_detection_active, driver_training_active, driver_audio_enabled} gains a 5th field std::string driver_version. The /health JSON parse in getHealth() uses j.value(\"driver_version\", std::string{}) for graceful pre-INST-09 driver detection (older drivers omit the field; v1.6 driver always includes it via 10-03 D-19's getter callback)."
key-files:
  created:
    - apps/micmap/src/version_mismatch.hpp
    - apps/micmap/src/version_mismatch.cpp
    - .planning/phases/10-cutover-cleanup/10-06-SUMMARY.md
  modified:
    - installer/MicMap.iss
    - CMakeLists.txt
    - apps/micmap/src/fail_pill.hpp
    - apps/micmap/main.cpp
    - apps/micmap/CMakeLists.txt
    - src/steamvr/include/micmap/steamvr/driver_api.hpp
    - src/steamvr/src/driver_api.cpp
decisions:
  - "[Rule 3 - Blocking] Renamed FailPill::headlineText -> text. The Wave 0 RED scaffold tests/test_version_mismatch.cpp accesses pill->text; the 10-03 FailPill struct shipped with headlineText. Following the plan body's `pill.headlineText` verbatim would have caused build-RED in test_version_mismatch.exe. The 10-03 tests/test_fail_pill_priority scaffold does NOT reference this field by name (only kind, actionLabel, deepLink, dismissable) and fail_pill.cpp uses positional brace-init -- so the rename is regression-free. Verified: 5/5 tests PASS post-rename (AssertCoVersioning, TrayGlyphStateMachine, FailPillPriority, LogRotation, VersionMismatch). Same load-bearing-Wave-0-contract reading the 10-02 (deriveTrayGlyph signature) + 10-03 (FailPill::deepLink + HealthSnapshot.econnrefused) plans followed."
  - "[Rule 2 - Missing critical functionality] Extended HealthView with `std::string driver_version` field + parsed it from /health JSON in DriverApi::getHealth(). The driver-side /health endpoint emits driver_version (10-03 D-19) but the client-side HealthView struct did not consume it -- the field was being parsed driver-side and dropped client-side. Without this extension the version comparison would always see driver_version='' (DriverVersionMissing) and the pill would mis-fire on every install (showing 'driver predates this client' even on a fresh in-version install). j.value(\"driver_version\", std::string{}) preserves the graceful pre-INST-09 behavior for older drivers."
  - "Version-mismatch pill renders BELOW the FAIL pill (per Discretion §version-mismatch placement). Two render blocks in renderUI's driver-health pane: FAIL pill (red) at top, version-mismatch pill (amber) below. The ##version ImGui label suffix on the Dismiss button prevents collision when both pills are active simultaneously. If no FAIL pill is active, the version-mismatch pill renders at the same vertical slot (the FAIL pill block's #if has_value() short-circuits cleanly)."
  - "First-poll-success check fires inside the existing /health envelope-fetch branch (after driverAudioEnabled.store + driverTrainingActive.store, line ~516 pre-edit). Placed there so kClientVersion (a static const initialized from MICMAP_VERSION_STRING compile define) is computed once at first call -- subsequent ticks short-circuit on g_versionUx.firstHealthSuccessSeen without re-reading the macro. No mutex needed (the entire poll runs on the UI thread; g_versionUx is single-thread)."
  - "Pill text format: 'Version mismatch -- driver vX.Y.Z vs client vA.B.C. Reinstall recommended.' for Mismatch; 'Driver version unknown -- driver predates this client (vA.B.C). Reinstall recommended.' for DriverVersionMissing. Both contain the client version; the Mismatch path also contains the driver version. test scaffold validates both versions appear in pill->text via std::string::find -- both formats satisfy that contract."
  - "Inno Setup [Files] entries source path: {#STAGE_DIR}\\bin\\resources\\tray_*.ico (matches the 10-02 install(FILES ... DESTINATION bin/resources) rule's stage layout). DestDir: {app}\\bin\\resources. Three sibling entries (tray_armed, tray_triggered, tray_error) added directly after the existing {stage}\\bin\\*.dll entry; flag set is just `ignoreversion` (matches the surrounding pattern; restartreplace + uninsrestartdelete are reserved for the driver DLL only)."
  - "Removed the old comment block above the ISCC invocation that documented the Wave 1 carryover ('the /DMICMAP_VERSION pass below duplicates the SSoT for one wave'). Replaced with a concise comment noting the removal: 'P10 D-18 / Wave 6: redundant /DMICMAP_VERSION pass REMOVED -- the #include version.iss at the top of MicMap.iss is now the sole authority for {#MICMAP_VERSION}.' This preserves the git-archeology trail without leaving stale 'temporary' language in the codebase."
metrics:
  duration: ~25 minutes
  completed_date: 2026-05-10
  task_count: 3
  file_count: 9
---

# Phase 10 Plan 06: Wave 6 Installer Co-Versioning + Version-Mismatch Pill Summary

Wave 6 lands INST-09 installer co-versioning end-to-end and the client-side D-20 version-mismatch warn-only pill. Four concerns delivered:

1. `installer/MicMap.iss` adopts `#include "version.iss"` at the top of the file (per RESEARCH §Pattern 4 + Pitfall 5). The existing `#ifndef MICMAP_VERSION` fallback is retained as a safety net for hand-runs without `configure_file`.
2. Root `CMakeLists.txt` has the redundant ISCC `/DMICMAP_VERSION=${PROJECT_VERSION}` pass REMOVED. Without removal, two competing `#define MICMAP_VERSION` paths would land into ISPP — the `#include` from MicMap.iss and the `/D` from ISCC — producing an ISPP redefinition warning at minimum and `AssertCoVersioning` drift if `PROJECT_VERSION` ever diverged from `MICMAP_VERSION`.
3. `installer/MicMap.iss` `[Files]` section gains 3 sibling entries for `tray_armed.ico`, `tray_triggered.ico`, `tray_error.ico` (Pitfall 8 mitigation). Without these entries a clean-VM install would silently miss the icons; tray-glyph swap would fail at runtime (`LoadImageW` returns nullptr → default Windows icon shown).
4. `apps/micmap/src/version_mismatch.{hpp,cpp}` + main.cpp wiring per D-20: after the first successful `/health` poll, compare `health.driverVersion` vs `MICMAP_VERSION_STRING`. On Mismatch / DriverVersionMissing: log warning ONCE per session + surface a low-priority dismissable pill in the driver-health pane (warn-only, never blocks per D-20).

The Wave 0 `test_version_mismatch` ctest flips from build-RED to GREEN; `AssertCoVersioning` stays GREEN through the wave; all 10 lints + tests PASS.

## What Shipped

**Installer co-versioning (`installer/MicMap.iss` + root `CMakeLists.txt`):**

- `installer/MicMap.iss` line 4-15: `#include "version.iss"` at top (above the existing `#ifndef MICMAP_VERSION` fallback). Comment block documents the SSoT flow + Pitfall 5 absolute-path resolution + safety-net rationale for the retained fallback.
- `installer/MicMap.iss` `[Files]` section (after the existing `{#STAGE_DIR}\bin\*.dll` entry): 3 sibling entries for `tray_armed.ico`, `tray_triggered.ico`, `tray_error.ico`. Source path `{#STAGE_DIR}\bin\resources\tray_*.ico`; DestDir `{app}\bin\resources`; Flag `ignoreversion`. Matches the 10-02 `install(FILES ... DESTINATION bin/resources)` stage layout exactly.
- `installer/MicMap.iss` `[Setup]` section: `AppVersion={#MICMAP_VERSION}` + `OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}` already in place from Wave 1 -- no edits needed (verified via direct read; the lines were correct from the earlier 10-01 pass).
- Root `CMakeLists.txt` `package` target: removed the `"/DMICMAP_VERSION=${PROJECT_VERSION}"` ISCC pass + the surrounding NOTE comment block. Replaced with a 6-line comment documenting the removal + the new SSoT flow. The remaining ISCC invocation passes only `/DSTAGE_DIR` + `/DOUTPUT_DIR`.
- `installer/version.iss` (generated by `configure_file` from `installer/version.iss.in`) currently contains `#define MICMAP_VERSION "1.6.0"` -- consumed by `MicMap.iss` via the new `#include` and stamped into the installer at `AppVersion=1.6.0` + `OutputBaseFilename=MicMap-Setup-v1.6.0`.

**`apps/micmap/src/version_mismatch.{hpp,cpp}`:**

- `enum class VersionCompareResult { Match, Mismatch, DriverVersionMissing }` -- 3-state result envelope. Empty driverVersion -> DriverVersionMissing (graceful pre-INST-09 driver detection); semver-aware compatibility explicitly out of scope for v1.6 per CONTEXT D-20.
- `compareVersions(client, driver)` -- pure exact-string compare. No semver parse, no normalization. Mirrors the test scaffold's case-1/case-2/case-3 expectations exactly.
- `buildVersionMismatchPill(client, driver)` -- returns `nullopt` for Match; otherwise constructs a `FailPill` with `kind=VersionMismatch`, `dismissable=true`, `blocking=false` (D-20 ALWAYS), text containing both versions (Mismatch path) or noting the missing field (DriverVersionMissing path), no actionLabel / no deepLink (no canonical "reinstall MicMap" URI).
- All POD-in / std::optional-out -- no globals, no Win32, headless-testable. test_version_mismatch ctest flips from build-RED to GREEN (5 cases all pass: Match, Mismatch, DriverVersionMissing, blocking=false invariant, dismissable=true invariant).

**`apps/micmap/main.cpp` wiring (5 insertion sites):**

- `#include "src/version_mismatch.hpp"` next to the existing `#include "src/tray_glyph.hpp"` / `fail_pill.hpp` / `process_check.hpp` block.
- `static struct VersionUxState { bool firstHealthSuccessSeen{false}; std::optional<FailPill> versionMismatchPill; } g_versionUx;` global next to `g_failUx` / `g_tray`.
- `pollDriverHealth` /health envelope-fetch branch (line ~510-516 pre-edit, inside the `if (driverLoadedIndicator.load())` block, after `driverTrainingActive.store`): one-shot version-mismatch fire on `!g_versionUx.firstHealthSuccessSeen`. Computes `kClientVersion = MICMAP_VERSION_STRING` (static const, initialized once), calls `buildVersionMismatchPill(kClientVersion, h->driver_version)`, stores the result on `g_versionUx.versionMismatchPill`. On Mismatch / DriverVersionMissing: `MICMAP_LOG_WARNING(...)` with both versions for post-mortem.
- `renderUI` driver-health pane (after the existing FAIL pill render block, before the HEALTH-01 driver-loaded indicator): version-mismatch pill render block. Amber color (`ImVec4(1.0f, 0.7f, 0.3f, 1.0f)`); `ImGui::TextWrapped("%s", vpill.text.c_str())` for the text; Dismiss button gated on `vpill.dismissable` with `##version` label suffix to prevent collision with the FAIL pill's Dismiss button when both are active. Closes with `ImGui::Separator()` so subsequent HEALTH-* lines render unchanged.
- `pill.headlineText` -> `pill.text` rename in the existing FAIL pill render path (single-line edit at the FAIL pill `ImGui::TextWrapped` call) -- mirrors the FailPill struct rename.

**FailPill struct rename (`apps/micmap/src/fail_pill.hpp`):**

- Field `headlineText` -> `text`. Plan-body's `<action>` block named the field `headlineText` for the new version-mismatch pill, but the Wave 0 RED scaffold tests/test_version_mismatch.cpp accesses `pill->text`. Following the plan verbatim would have caused build-RED in test_version_mismatch.exe.
- Decision: rename `headlineText` -> `text` in the struct definition. Verified safe: tests/test_fail_pill_priority.cpp does NOT reference the field by name (only `kind`, `actionLabel`, `deepLink`, `dismissable`, `pill.has_value`); fail_pill.cpp constructs FailPill via positional brace-init (unaffected by member rename). Single render-path consumer in main.cpp (`pill.headlineText.c_str()`) updated to `pill.text.c_str()`.
- Same load-bearing-Wave-0-contract reading 10-02 (`deriveTrayGlyph` signature) + 10-03 (`FailPill::deepLink` rename + `HealthSnapshot.econnrefused` field add) plans followed -- the test scaffold contract is the single source of truth for surface naming, plan-body's draft text yields when conflicts surface.

**`HealthView` extension (`src/steamvr/include/micmap/steamvr/driver_api.hpp` + `src/steamvr/src/driver_api.cpp`):**

- Added `std::string driver_version` field to `HealthView`. Driver-side `/health` JSON emits the field via 10-03 D-19's `driverVersionGetter` callback; client-side `DriverApi::getHealth()` now extracts it via `j.value("driver_version", std::string{})` (graceful pre-INST-09 default).
- This is the linking piece that makes the `compareVersions(MICMAP_VERSION_STRING, h->driver_version)` call site at the new poll-callback hook actually have a non-empty driver-version string to compare against. Without this, the comparison would always see `driver_version=""` (DriverVersionMissing) and the pill would mis-fire on every install -- a Rule 2 "missing critical functionality" deviation per the executor's protocol.

**`apps/micmap/CMakeLists.txt`:**

- `MICMAP_SOURCES` extended with `src/version_mismatch.cpp`. No new lib link needed (version_mismatch.cpp depends only on FailPill which is already linked via fail_pill.cpp + tray_glyph.cpp).

## Verification

**Per-task automated checks (all PASS):**

- **Task 1** (installer + cmake co-versioning): `grep -q '#include "version.iss"' installer/MicMap.iss` -> hit; 3 .ico [Files] entries verified via `grep -E "tray_(armed|triggered|error)\.ico"` -> 3 hits each; root `CMakeLists.txt` no longer matches `'"/DMICMAP_VERSION='`. `cmake -DSOURCE_DIR=. -P cmake/AssertCoVersioning.cmake` -> `clean (MICMAP_VERSION=1.6.0; cmake / version.iss in sync)`. `ctest -R AssertCoVersioning` -> PASS.
- **Task 2** (version_mismatch.{hpp,cpp}): both files exist; `grep -q "VersionCompareResult" version_mismatch.hpp` + `grep -q "buildVersionMismatchPill" version_mismatch.hpp` + `grep -q "DriverVersionMissing" version_mismatch.hpp` + `grep -q "blocking *= *false" version_mismatch.cpp` -- all hit. `cmake --build build --target test_version_mismatch` builds clean; `ctest -R VersionMismatch` -> 1/1 PASS in 0.02s. All 5 cases of the Wave 0 RED scaffold turn GREEN: Match, Mismatch (text contains both versions), DriverVersionMissing (text + still-produces-pill), blocking == false (both Mismatch + missing paths), dismissable == true (both paths).
- **Task 3** (main.cpp + CMakeLists wiring): `grep -q "src/version_mismatch.hpp" main.cpp` + `grep -q "buildVersionMismatchPill" main.cpp` + `grep -q "firstHealthSuccessSeen" main.cpp` + `grep -q 'MICMAP_LOG_WARNING.*driver version' main.cpp` -- all hit. `grep -q "src/version_mismatch.cpp" apps/micmap/CMakeLists.txt` -> hit. `cmake --build build --target micmap --config Debug` builds clean (only pre-existing LNK4098 LIBCMT noise from 10-01). `cmake --build build --target driver_micmap` builds clean. `cmake --build build --target hmd_button_test mic_test` builds clean (consumers of the renamed FailPill::text field via micmap_steamvr's HealthView change -- no narrowing/conversion issues).

**Regression-free check (10/10 PASS):**

```
ctest --test-dir build -C Debug -R "VersionMismatch|FailPillPriority|TrayGlyphStateMachine|AssertCoVersioning|LogRotation|AssertNoClient|AssertNoButton|AssertHttpServer"
1/10 Test #18: AssertHttpServerLocalhostOnly ....   Passed
2/10 Test #19: AssertHttpServerNoVrApi ..........   Passed
3/10 Test #41: AssertNoClientTraining ...........   Passed
4/10 Test #42: AssertCoVersioning ...............   Passed (stays GREEN -- the lint's MICMAP_VERSION value-match across cmake/version.cmake + installer/version.iss is unaffected by removing the ISCC /D pass)
5/10 Test #43: TrayGlyphStateMachine ............   Passed (no regression from FailPill::headlineText -> text rename)
6/10 Test #44: FailPillPriority .................   Passed (no regression from FailPill::headlineText -> text rename -- this scaffold does NOT reference the renamed field by name)
7/10 Test #45: LogRotation ......................   Passed
8/10 Test #46: VersionMismatch ..................   Passed (Wave 0 RED -> GREEN flip)
9/10 Test #47: AssertNoClientDetection ..........   Passed
10/10 Test #48: AssertNoButtonRoute ..............   Passed
100% tests passed, 0 tests failed out of 10
Total Test time (real) = 3.11s
```

**Build-side sanity (informational):**

- `build/bin/Debug/micmap.exe` links cleanly with the new `version_mismatch.cpp` translation unit + the renamed FailPill::text field.
- `build/driver/micmap/bin/win64/driver_micmap.dll` builds clean (no driver-side changes; `driver_version` was already emitted by 10-03 D-19's getter callback).
- `build/bin/Debug/hmd_button_test.exe` builds clean (consumer of micmap_steamvr's HealthView -- the new `driver_version` field is positional-init-safe; the new field defaults to empty string when not set explicitly).

**Visual / hardware verification deferred:** confirming the actual amber pill renders in the driver-health pane on a real version-skew install + the Dismiss button suppresses for the session is a `human-verify` checkpoint scheduled for 10-07 D-25(11) UAT. The plan does not request it as a gate for this autonomous executor; the test_version_mismatch GREEN flip + clean micmap.exe build + AssertCoVersioning GREEN are the gating signals for 10-06.

**Installer round-trip deferred:** confirming `cmake --build build --target package` produces `MicMap-Setup-v1.6.0.exe` + the 3 .ico files install to `<SteamVR>/drivers/micmap/bin/resources/` is the canonical 10-07 D-25(11) UAT cue. The package target requires Inno Setup 6.7.1+ on PATH (fall-through MESSAGE WARNING when ISCC.exe absent); the autonomous executor does not assume Inno Setup availability.

## Verified Existing Artifacts (Plan-Required Documentation)

Per the plan's `<output>` section, document the actual code shape encountered:

- **Actual installer build artifact name**: `MicMap-Setup-v1.6.0.exe` (from `OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}` in `installer/MicMap.iss:35` + `MICMAP_VERSION="1.6.0"` from the included `installer/version.iss`). Expected at `build/installer/` per root CMakeLists.txt's `MICMAP_INSTALLER_DIR` after `cmake --build build --target package`. 10-07 D-25(11) UAT verifies the round-trip.
- **AppVersion in [Setup] section status**: ALREADY in place pre-edit. Line 21 reads `AppVersion={#MICMAP_VERSION}` (not `{#MyAppVersion}` or hardcoded). No change needed; the value flow shifts from "ISCC /D pass" to "MicMap.iss #include version.iss" but the line itself is unchanged.
- **OutputBaseFilename status**: ALREADY in place pre-edit. Line 35 reads `OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}` -- correct verbatim, no change needed.
- **STAGE_DIR layout for .ico files**: `{#STAGE_DIR}\bin\resources\tray_*.ico`. Verified by reading the 10-02 `install(FILES ... DESTINATION bin/resources)` rule in `apps/micmap/CMakeLists.txt:162-166`. The plan's hedge ("verify via grep on the existing entries -- STAGE_DIR\\bin vs STAGE_DIR\\client\\bin vs other") was answered by direct inspection: the existing `{#STAGE_DIR}\bin\micmap.exe` + `{#STAGE_DIR}\bin\app.vrmanifest` + `{#STAGE_DIR}\bin\*.dll` entries all use `\bin` directly (no client/ subpath); the .ico entries follow the same convention with the `\resources` subdir matching the install rule's DESTINATION clause.
- **Pitfall 9 acknowledgement**: install-time overwrite of user-edited `default.vrsettings` is install-scoped per CONTEXT D-02 (emergency override is per-install only -- v1.6 shape). Document below for 10-07 SUMMARY's CLAUDE.md "Hardware rig" update cue.
- **AppId GUID preservation**: line 19 `{BC6D91A7-A852-4562-8CBF-58FC4662FEDC}` preserved verbatim (NOT changed by this plan). Inno Setup upgrade-in-place identity intact.
- **Existing recursesubdirs entries preservation**: lines 51-61 (driver_micmap.dll + driver.vrdrivermanifest + drivers/micmap/resources/* with recursesubdirs createallsubdirs) preserved verbatim. Only the new client-side `\bin\resources\tray_*.ico` entries were added.
- **MICMAP_VERSION_STRING define source on the client**: `apps/micmap/CMakeLists.txt:69-72` adds `MICMAP_VERSION_STRING="${MICMAP_VERSION}"` to `target_compile_definitions(micmap PRIVATE ...)` (10-01 SSoT). The new poll-callback hook reads this directly via `static const std::string kClientVersion = MICMAP_VERSION_STRING;`.

## Pitfall 9 Acknowledgement (D-02 cross-reference for 10-07)

Install-time overwrite of user-edited `<SteamVR>/drivers/micmap/resources/settings/default.vrsettings` is documented as install-scoped per CONTEXT D-02. The Phase 10 / 10-05 cutover ships `enable_driver_audio=true` + `enable_driver_detection=true` as the v1.6 defaults; if a UAT operator hand-edited those values back to false for emergency override (per D-02 emergency-override flow), running the installer (or upgrade-in-place) overwrites the file with the shipped defaults. This is the expected v1.6 behavior -- emergency override is per-install, not persistent across reinstalls. The 10-07 SUMMARY's CLAUDE.md "Hardware rig" section will surface this as an explicit operator-facing note.

For local UAT purposes the existing rig-environment instruction stands: "always backup `default.vrsettings` as `<name>.preuat.bak` before a UAT install; restore from `driver/resources/settings/default.vrsettings` (canonical pristine source) post-UAT." The pristine source ships `enable_driver_audio=true` + `enable_driver_detection=true` post-10-05.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] FailPill::headlineText -> text rename to match Wave 0 scaffold contract**
- **Found during:** Task 2 (writing version_mismatch.cpp's pill construction against the existing test scaffold)
- **Issue:** The plan's `<action>` block specified `pill.headlineText = "..."` for the version-mismatch pill builder. The Wave 0 RED scaffold `tests/test_version_mismatch.cpp:52-53` calls `pill->text.find("1.6.0")`. Following the plan's name would have caused build-RED in `test_version_mismatch.exe` (no member named `text`).
- **Root cause:** The 10-03 plan (which originally defined FailPill) shipped with the field name `headlineText`, but the 10-06 Wave 0 RED scaffold was written with `text` -- a draft inconsistency between the two plans. Since the 10-03 fail_pill_priority scaffold does NOT reference this field by name (only `kind`, `actionLabel`, `deepLink`, `dismissable`), the rename is regression-free.
- **Fix:** Renamed `FailPill::headlineText` -> `FailPill::text` in `apps/micmap/src/fail_pill.hpp`. fail_pill.cpp uses positional brace-init (unaffected). Single render-path consumer in `apps/micmap/main.cpp` (line 756 pre-edit, `pill.headlineText.c_str()`) updated to `pill.text.c_str()`. Verified: 5/5 ctest pass post-rename (AssertCoVersioning, TrayGlyphStateMachine, FailPillPriority, LogRotation, VersionMismatch).
- **Files modified:** apps/micmap/src/fail_pill.hpp, apps/micmap/main.cpp
- **Commit:** 30cf098

**2. [Rule 2 - Missing critical functionality] HealthView extended with driver_version + parsed in getHealth()**
- **Found during:** Task 3 (writing the pollDriverHealth hook against the actual HealthView shape)
- **Issue:** The plan body's `<action>` block called `compareVersions(kClientVersion, health.driverVersion)` -- assuming `health.driverVersion` was populated. The driver-side `/health` endpoint emits `driver_version` (10-03 D-19), but inspecting `src/steamvr/include/micmap/steamvr/driver_api.hpp:262-267` showed `HealthView` had only 4 fields: `driver_loaded`, `driver_detection_active`, `driver_training_active`, `driver_audio_enabled`. The driver_version field was being parsed driver-side and dropped client-side. Without extending HealthView, the version comparison would always see `driver_version=""` -> DriverVersionMissing -> the pill would mis-fire on every install (showing "driver predates this client" even on a fresh in-version install). This is a correctness requirement, not a feature add.
- **Fix:** Added `std::string driver_version` to the HealthView struct. Updated `DriverApi::getHealth()` to extract via `v.driver_version = j.value("driver_version", std::string{})` -- the std::string{} default preserves graceful pre-INST-09 behavior for older drivers that don't emit the field. Updated the new poll-callback hook to consume `h->driver_version` (the std::optional<HealthView> deref).
- **Files modified:** src/steamvr/include/micmap/steamvr/driver_api.hpp, src/steamvr/src/driver_api.cpp, apps/micmap/main.cpp (the call site itself)
- **Commit:** 7fd249c

### Auth Gates

None. All work was offline / local build + test. No SteamVR runtime engagement (UAT is 10-07).

## Threat Model Compliance

All 6 STRIDE threats from the plan's threat register are addressed:

- **T-10-06-01** (Tampering, version.iss -> wrong installer version): mitigated. `installer/version.iss` is generated by `configure_file` from in-repo `cmake/version.cmake` (the SSoT); `AssertCoVersioning` lint asserts the values match on every configure (5 EXISTS-gated assertions). The lint is in CI; any future drift FATALs the configure step.
- **T-10-06-02** (Tampering, install-time overwrite of user-edited default.vrsettings): accepted per D-02. Emergency override is documented as install-scoped (Pitfall 9 acknowledgement above; CLAUDE.md "Hardware rig" update cued for 10-07 SUMMARY). The user-facing rationale: emergency override is for short-term troubleshooting; persistent override would require per-user state outside the installed driver tree, which is outside v1.6 scope.
- **T-10-06-03** (DoS, missing .ico files -> tray glyph runtime failure / Pitfall 8): mitigated. Three `[Files]` entries in `installer/MicMap.iss` -> three .ico files install to `{app}\bin\resources\` in lockstep with the EXE/DLL. The 10-02 CMake `install(FILES ... DESTINATION bin/resources)` rule populates the stage layout from which the installer copies. Inno Setup `[Files]` is atomic by default (all-or-nothing); the .ico files cannot be partially missing.
- **T-10-06-04** (DoS, version mismatch hard-blocks user mid-upgrade): mitigated. D-20 `blocking == false` ALWAYS in v1.6 -- pill warns, never gates detection. Verified by test_version_mismatch case 4 (tests both Mismatch and DriverVersionMissing paths return pills with `blocking == false`). Hard-block was rejected per CONTEXT D-20 rationale because in-progress upgrades briefly show driver/client version skew (one binary updates seconds before the other) and a hard block would brick the user during the transition window.
- **T-10-06-05** (Spoofing, foreign /health server returns crafted driver_version): accepted. Localhost-only binding (P8 IPC-07 + AssertHttpServerLocalhostOnly lint stays GREEN); same trust as the binary itself. A foreign localhost server is out of scope for v1.6 (would require an attacker-controlled process listening on 127.0.0.1:27015, which implies prior compromise).
- **T-10-06-06** (Information disclosure, pill text exposes both versions): accepted. Localhost-only UI surface; same machine = same trust. Per CONTEXT D-20, semver is not a security secret -- the version is also stamped into VS_VERSION_INFO (visible via Get-Item `\bin\micmap.exe`).

## Threat Flags

None -- this plan creates no new network endpoints, no new auth paths, no new file-access patterns at trust boundaries beyond the install-time copy of the 3 .ico files (which are static read-only assets shipped in the same installer as the EXE/DLL and inherit the installer's admin-elevated trust). The `HealthView.driver_version` extension consumes an already-emitted field from the existing `/health` endpoint (no new endpoint, no new bind path -- AssertHttpServerLocalhostOnly + AssertHttpServerNoVrApi stay GREEN).

## Known Stubs

None blocking the plan's goal. The version-mismatch pill is fully wired: poll-callback fires `compareVersions(kClientVersion, h->driver_version)` ONCE per session on first /health success; on Mismatch / DriverVersionMissing, populates `g_versionUx.versionMismatchPill` + logs WARNING; render path emits the amber pill below the FAIL pill in the driver-health pane with a `##version`-suffixed Dismiss button.

The pill is INFORMATIONAL -- there is no canonical "reinstall MicMap" deep-link URI (would require either a Steam-hosted page that opens the latest installer, or a packaged auto-updater -- both out of v1.6 scope). The pill's `actionLabel` and `deepLink` are intentionally empty; the user is prompted via the text "Reinstall recommended." This is documented at the `buildVersionMismatchPill` call site, not a TODO/FIXME stub.

The `HealthSnapshot.driverVersion` field in `apps/micmap/src/tray_glyph.hpp` is still NOT populated in main.cpp's poll-tick `pollDriverHealth` materialization (lines 607-610 -- the snapshot is fed to `deriveTrayGlyph` and `pickActivePill`, neither of which consumes the field). The version-mismatch check operates directly on `h->driver_version` from the `getHealth()` envelope, bypassing the snapshot. This is INTENTIONAL: the snapshot is for the per-tick derivation passes; the version check is a one-shot at the envelope-fetch level. Adding the field to the snapshot would be over-coupling without a real consumer. Documented as a plan-of-work seam, not a stub.

## TDD Gate Compliance

Plan type is `execute` (not `tdd`). The Wave 0 RED scaffold pattern (P10 D-01) is the structural test gate: tests/test_version_mismatch.cpp was committed at 10-00 with build-RED expectations; this plan's Task 2 commit (30cf098) flips the scaffold to GREEN by landing version_mismatch.{hpp,cpp}. Verified via `ctest -R VersionMismatch` -> 1/1 PASS in 0.01s. The RED -> GREEN cycle satisfies the structural gate without requiring per-task RED/GREEN/REFACTOR commits.

## Commits

| Task | Description                                                                                          | Commit  |
| ---- | ---------------------------------------------------------------------------------------------------- | ------- |
| 1    | feat(10-06): wire MicMap.iss #include version.iss + .ico [Files] entries; remove redundant ISCC /DMICMAP_VERSION pass | c90b55f |
| 2    | feat(10-06): add version_mismatch -- compareVersions + buildVersionMismatchPill (D-20)               | 30cf098 |
| 3    | feat(10-06): wire version-mismatch into poll callback + driver-health pane render                    | 7fd249c |

## Self-Check

- installer/MicMap.iss -- FOUND (#include "version.iss" at top + 3 .ico [Files] entries; AppVersion={#MICMAP_VERSION} + AppId GUID preserved)
- CMakeLists.txt -- FOUND (no '"/DMICMAP_VERSION=' line; SSoT comment block above the package target)
- apps/micmap/src/version_mismatch.hpp -- FOUND (VersionCompareResult enum + compareVersions + buildVersionMismatchPill declarations)
- apps/micmap/src/version_mismatch.cpp -- FOUND (impl: empty -> DriverVersionMissing, exact-string compare, blocking=false ALWAYS)
- apps/micmap/src/fail_pill.hpp -- FOUND (FailPill::text field; rename from headlineText)
- apps/micmap/main.cpp -- FOUND (#include src/version_mismatch.hpp + g_versionUx + pollDriverHealth hook + amber pill render block + pill.text consumer)
- apps/micmap/CMakeLists.txt -- FOUND (src/version_mismatch.cpp in MICMAP_SOURCES)
- src/steamvr/include/micmap/steamvr/driver_api.hpp -- FOUND (HealthView.driver_version field added)
- src/steamvr/src/driver_api.cpp -- FOUND (DriverApi::getHealth parses driver_version via j.value)
- ctest VersionMismatch -- PASS (Wave 0 RED -> GREEN flip; 5/5 cases pass)
- ctest AssertCoVersioning -- PASS (stays GREEN; cmake-side MICMAP_VERSION = installer/version.iss MICMAP_VERSION = 1.6.0)
- ctest FailPillPriority -- PASS (no regression from FailPill::headlineText -> text rename)
- ctest TrayGlyphStateMachine -- PASS
- ctest LogRotation -- PASS
- ctest AssertNoClientDetection + AssertNoButtonRoute + AssertNoClientTraining -- PASS
- ctest AssertHttpServerLocalhostOnly + AssertHttpServerNoVrApi -- PASS
- micmap.exe Debug build -- clean (only pre-existing LNK4098 LIBCMT noise from 10-01)
- driver_micmap.dll Debug build -- clean
- hmd_button_test.exe Debug build -- clean (consumer of micmap_steamvr's HealthView; positional-init-safe with the new driver_version field)
- mic_test.exe Debug build -- clean
- c90b55f -- FOUND (`git log --oneline` confirms; Task 1 commit)
- 30cf098 -- FOUND (`git log --oneline` confirms; Task 2 commit)
- 7fd249c -- FOUND (`git log --oneline` confirms; Task 3 commit)

## Self-Check: PASSED

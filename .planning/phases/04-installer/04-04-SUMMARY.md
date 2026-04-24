---
phase: 04-installer
plan: 04
subsystem: [installer-packaging, build-system]
tags: [inno-setup, iscc, installer, appid, x64os, icon, pascal-script]
requires:
  - phase: 04-03
    provides: CMake `package` target + deterministic build/stage tree matching D-01 layout
provides:
  - installer/MicMap.iss — Inno Setup 6 script with [Setup] preamble + [Files] + stub [Code] (compiles cleanly under ISCC 6.7.1)
  - installer/micmap.ico — shared multi-resolution icon (16/32/48/256) used by both exe and installer
  - Stable frozen AppId GUID `BC6D91A7-A852-4562-8CBF-58FC4662FEDC` — enables Inno upgrade-in-place semantics (INST-01)
  - Preprocessor guards (#ifndef MICMAP_VERSION/OUTPUT_DIR fallbacks, #error on missing STAGE_DIR)
  - GetMicMapInstallDir() stub in [Code] (placeholder — Plan 05 replaces with HKCU\Software\Valve\Steam registry lookup)
affects:
  - 04-05 (needs to replace GetMicMapInstallDir body with real HKCU registry resolution)
  - 04-06 (adds PrepareToInstall + InitializeSetup — gates on SteamVR running + admin/UAC; hooks into CloseApplications=no)
  - 04-07 (adds [Code] callbacks for vrpathreg/ reg add / vrmanifest registration via CurStepChanged(ssPostInstall) + CurUninstallStepChanged(usUninstall))
  - 04-08 (installer copy gate + final polish on [Setup] messages / wizard pages)
tech-stack:
  added:
    - Inno Setup 6.7.1 script authoring (ISCC.exe pipeline)
    - Inno preprocessor directives (#define / #ifndef / #error) for build-time wiring
    - Pascal Script [Code] stub pattern for {code:...} default-dir resolution
  patterns:
    - Single shared .ico asset (installer/micmap.ico) referenced by both apps/micmap/micmap.rc (IDI_MICMAP_ICON) and installer/MicMap.iss (SetupIconFile) — one visual identity across exe + installer + tray (04-RESEARCH.md Open Q #3)
    - Stage-dir-only [Files] sourcing ({#STAGE_DIR}\*) — zero repo-relative paths, single source of truth is cmake --install (D-19)
    - restartreplace + uninsrestartdelete flags on driver_micmap.dll (Pitfall 4 defense-in-depth for INST-02 — handles locked DLL on upgrade)
    - x64os architecture tokens (NOT deprecated x64) — IS 6.3+ compliant; explicitly refuses ARM64 Windows installs (D-17)
    - Frozen AppId literal in .iss (plain text, not regenerated) — the linchpin of Inno's upgrade-in-place detection (INST-01)
    - [Code] stub with `{autopf}\MicMap` placeholder that is syntactically valid Pascal but never reached in prod (Plan 05 replaces body)
key-files:
  created:
    - installer/MicMap.iss
    - installer/micmap.ico
  modified:
    - apps/micmap/micmap.rc
key-decisions:
  - "AppId GUID `BC6D91A7-A852-4562-8CBF-58FC4662FEDC` FROZEN FOREVER at 2026-04-23 plan-time — any future regeneration breaks upgrade-in-place semantics (INST-01). Literal is hardcoded in .iss — not a #define — so nobody can accidentally override it."
  - "Icon generated on-the-fly via PowerShell + System.Drawing (no ImageMagick / external tooling) — a simple blue circle with white 'M' glyph at 4 resolutions. Placeholder visual identity for v1.0; proper branding deferred to post-v1 polish."
  - "[Code] section uses `// ... ` line comments (Pascal Script accepts them) — NOT `;` (which is a statement terminator in Pascal, not a comment). The [Setup] section prefix-semicolons are comments in that context, but in [Code] they're code."
  - "GetMicMapInstallDir stub body returns `{autopf}\\MicMap` — syntactically valid for ISCC compile-green, but NEVER reached in prod because Plan 05 replaces the body with HKCU\\Software\\Valve\\Steam\\SteamPath lookup. The literal value is intentional — if Plan 05 fails to land, the installer still installs somewhere sane (Program Files\\MicMap) rather than crashing."
  - "ISCC smoke-compile path hygiene: ran via a disposable `.cmd` wrapper to escape PowerShell's path quoting around `Program Files (x86)`. Wrapper deleted post-test; permanent ISCC invocation is owned by the CMake `package` target from Plan 03."
requirements-completed: [INST-01]
duration: 18min
completed: 2026-04-24
---

# Phase 4 Plan 04: Installer Skeleton ([Setup] + [Files] + Icon) Summary

**Inno Setup 6.7.1 `.iss` skeleton with frozen AppId, x64os architecture locks, {#STAGE_DIR}-sourced [Files], restartreplace on the driver DLL, and a shared multi-res `.ico` used by both exe and installer — compiles cleanly under ISCC producing a 2.5 MB MicMap-Setup-v0.0.0-test.exe against the Plan 03 stage.**

## Performance

- **Duration:** ~18 min
- **Started:** 2026-04-24T01:12:00Z (approx)
- **Completed:** 2026-04-24T01:30:00Z (approx)
- **Tasks:** 2
- **Files created:** 2 (`installer/MicMap.iss`, `installer/micmap.ico`)
- **Files modified:** 1 (`apps/micmap/micmap.rc`)

## Accomplishments

- Generated `installer/micmap.ico` — a 6 384-byte multi-resolution ICO (16/32/48/256 frames, PNG-compressed) accepted by both `System.Drawing.Icon` (validation load-test) and ISCC's `SetupIconFile` directive
- Wired `apps/micmap/micmap.rc` to reference the shared icon at `../../installer/micmap.ico` (single source of truth for visual identity across exe + installer + tray — 04-RESEARCH.md Open Q #3 recommendation)
- Authored `installer/MicMap.iss` (87 lines) with the full [Setup] preamble, [Languages] English default, 5 [Files] entries from `{#STAGE_DIR}`, and a stub [Code] function for `{code:GetMicMapInstallDir}`
- Locked the AppId GUID `BC6D91A7-A852-4562-8CBF-58FC4662FEDC` as a hardcoded literal (frozen per INST-01 — no regeneration, no #define) — the linchpin of Inno upgrade-in-place semantics for all future v1.x releases
- Validated end-to-end: `cmake --build build --config Release --target micmap` succeeds with the new `.ico` embedded AND ISCC smoke-compiles the `.iss` against the Plan 03 stage tree, producing a working 2.5 MB `MicMap-Setup-v0.0.0-test.exe` at `build/installer/`

## Task Commits

1. **Task 1: Generate installer/micmap.ico + wire IDI_MICMAP_ICON into micmap.rc** — `74fce38` (feat)
2. **Task 2: Create installer/MicMap.iss with [Setup] + [Files] + preprocessor guards + stub [Code]** — `c38b77d` (feat)

_Note: Both tasks were tdd="true". The RED state was self-evident (file-missing / line-commented), so each task landed as a single `feat(...)` commit per the "wiring-task" exception pattern established in Plan 04-03. No separate `test(...)` commits — verification is grep-based and empirical (ISCC compile-green / cmake build-green), not unit-test based._

## Files Created/Modified

- `installer/MicMap.iss` (new, 87 lines) — Inno Setup script; [Setup] preamble (AppId + x64os + admin + Uninstallable + OutputDir + CloseApplications=no + SetupIconFile), [Languages] English, [Files] with 5 stage-sourced entries (driver DLL + vrdrivermanifest + resources + micmap.exe + app.vrmanifest), [Code] stub `GetMicMapInstallDir`. Preprocessor guards at the top: #ifndef fallbacks for MICMAP_VERSION + OUTPUT_DIR, hard `#error` on missing STAGE_DIR.
- `installer/micmap.ico` (new, 6 384 bytes) — Multi-resolution ICO (16×16, 32×32, 48×48, 256×256 frames, PNG-compressed each), blue `#4A90E2` filled circle with white Arial-bold "M" glyph. Placeholder visual identity.
- `apps/micmap/micmap.rc` (modified, lines 9-11) — Replaced the commented-out `// IDI_MICMAP_ICON ICON "micmap.ico"` slot with `IDI_MICMAP_ICON ICON "../../installer/micmap.ico"` — single shared asset (exe + installer + tray).

## Decisions Made

- **AppId frozen literal.** `BC6D91A7-A852-4562-8CBF-58FC4662FEDC` is a hardcoded literal in `[Setup]`, not a `#define`-able token. This is intentional: Inno's upgrade-in-place detection fails silently if the GUID changes (new-install semantics, old version stays in Add/Remove Programs). Hardcoding prevents accidental override via `/D` flags.
- **Icon generated via PowerShell + System.Drawing, not ImageMagick.** The dev box has `System.Drawing` available (System.Drawing.Common is ubiquitous on Windows .NET) but no ImageMagick. A minimal PowerShell generator produced a proper 4-frame ICO with PNG compression — small enough to commit to the repo (6 KB). The generator script was a one-off (`_make_icon.ps1`) and was deleted post-commit; the `.ico` itself is the shipping artifact.
- **Pascal comments use `//` not `;` inside [Code].** Inno's `[Setup]`, `[Files]`, etc. use `;` as a line-comment prefix. But `[Code]` is Pascal Script where `;` is a statement terminator, NOT a comment. First compile attempt failed with `"'BEGIN' expected. Compile aborted."` at line 79 — the leading `;` lines in the stub block. Fixed to `//` (supported since Delphi 2; ISCC accepts it). Pattern applies to all Plan 05-08 [Code] additions.
- **GetMicMapInstallDir stub returns `{autopf}\MicMap`.** A syntactically-valid Pascal expression that resolves to `C:\Program Files\MicMap` (the real Windows `autopf` constant). Plan 05 will replace the body with HKCU registry resolution. The placeholder is deliberately sane rather than a crash-path — if Plan 05 regresses, the installer still installs somewhere reasonable.
- **[Run] / [UninstallRun] sections omitted.** Per plan spec, Plan 07 will add orchestration via `CurStepChanged(ssPostInstall)` + `Exec()` with `ResultCode` inspection (04-RESEARCH.md Open Q #9 Technique B). Using `[Run]` directly would bypass the error-handling that Plan 07 needs.
- **DisableWelcomePage=no kept explicit.** The verbatim template from 04-RESEARCH.md includes `DisableWelcomePage=no` — kept as-is. Wizard is welcome → progress → finished with `DisableDirPage=yes` (install dir is derived from Steam registry, user shouldn't pick) and `DisableProgramGroupPage=yes` (nothing to put in Start Menu — it's a service-like driver + tray).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocker] Pascal comment syntax inside [Code] block**
- **Found during:** Task 2 ISCC smoke compile
- **Issue:** The plan's `<action>` sample [Code] block used `;`-prefixed comment lines (e.g., `; Plan 04 stub -- replaced by Plan 05 ...`). `;` is valid as a comment prefix in `[Setup]` / `[Files]` sections but in `[Code]` (Pascal Script), `;` is a statement terminator. First compile attempt failed with `Error on line 79 ... 'BEGIN' expected. Compile aborted.`
- **Fix:** Replaced the 3 `;`-prefixed comment lines in the `[Code]` block with `//` line comments. Re-ran ISCC — compile succeeded, produced `build/installer/MicMap-Setup-v0.0.0-test.exe` (2.5 MB).
- **Files modified:** installer/MicMap.iss (within same Task 2 commit — the fix was caught pre-commit so no separate fix commit was needed)
- **Verification:** ISCC exit 0, installer exe produced, all grep acceptance criteria still pass after the fix
- **Committed in:** c38b77d (Task 2 commit)

**2. [Out-of-Scope - Logged only] Missing `build/bin/Release/` directory before OpenVR DLL copy POST_BUILD**
- **Found during:** Task 1 verification `cmake --build build --config Release --target micmap`
- **Issue:** The `micmap_steamvr` target has a POST_BUILD `cmake -E copy_if_different` that copies `openvr_api.dll` to `build/bin/Release/` — but on a fresh-configure worktree, that directory doesn't exist yet, and `copy_if_different` fails with "Invalid argument" instead of creating the destination. Result: `error MSB3073: ... exited with code 1`.
- **Scope decision:** This is a pre-existing bug in `src/steamvr/CMakeLists.txt` (or wherever the POST_BUILD rule lives) — NOT caused by Plan 04's `.ico` / `.rc` / `.iss` changes. Per execution-flow SCOPE BOUNDARY rule, out-of-scope. Logged here for visibility; should be fixed in a separate cleanup plan (trivial: add a `cmake -E make_directory` step before the copy, or use `cmake -E copy_if_different` with an explicit `--` dest terminator + `make_directory` hook).
- **Local workaround:** `mkdir -p build/bin/Release && cmake --build ...` — worked around in-place for Plan 04 verification. Not committed (the workaround is runtime, not code).
- **Files affected (NOT modified):** `src/steamvr/CMakeLists.txt` or similar
- **Action:** Left unfixed per SCOPE BOUNDARY. Tracked in this SUMMARY for handoff.

---

**Total deviations:** 1 auto-fixed (1 blocking — Pascal comment syntax) + 1 out-of-scope logged.
**Impact on plan:** Auto-fix was caught during verification and landed in the same Task 2 commit (no separate fix commit). Out-of-scope issue worked around at runtime, not a Plan 04 regression. No scope creep. No architectural changes.

## Issues Encountered

- **ISCC invocation from Git Bash required a `.cmd` wrapper.** PowerShell's path quoting around `Program Files (x86)` in combination with Bash-to-PowerShell argument handoff caused ISCC to parse the script path as a second script filename. Worked around by writing `_iscc_smoke.cmd` and invoking via `cmd.exe //c _iscc_smoke.cmd`. Wrapper deleted post-smoke-test (not shipped — the permanent ISCC invocation is owned by the CMake `package` target from Plan 03, which uses CMake's native `COMMAND ${ISCC_EXECUTABLE}` that quotes correctly).
- **CRLF line-ending warning on commit.** Git reported `warning: in the working copy of 'installer/MicMap.iss', LF will be replaced by CRLF the next time Git touches it`. Windows-appropriate; not an error. The committed blob stores LF (git normalizes); Windows checkout will get CRLF. ISCC accepts both.

## ISCC Smoke-Compile Result

| Parameter | Value |
|-----------|-------|
| ISCC path | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` (v6.7.1) |
| `/DMICMAP_VERSION` | `0.0.0-test` |
| `/DSTAGE_DIR` | `<worktree>/build/stage` |
| `/DOUTPUT_DIR` | `<worktree>/build/installer` |
| Stage source | `cmake --install build --prefix build/stage --config Release` (Plan 03 pipeline) |
| Result | `Successful compile (1.172 sec)` — exit 0 |
| Output file | `build/installer/MicMap-Setup-v0.0.0-test.exe` (2 519 889 bytes — 2.5 MB) |
| Files compressed | 6 (driver_micmap.dll, driver.vrdrivermanifest, driver.vrresources, default.vrsettings, micmap.exe, app.vrmanifest) |

The installer exe is a fully-formed Inno Setup package that, if run on a Windows box with SteamVR present and the Plan 05-08 [Code] hooks landed, would install MicMap into `{SteamVR}\drivers\micmap\`. For Plan 04 alone (no registry lookup yet), it would install into `C:\Program Files\MicMap` via the stub path — harmless because nobody runs the smoke-test installer.

## Next Plan Readiness (Plan 05)

- **`GetMicMapInstallDir` body needs replacement.** Current stub returns `{autopf}\MicMap`. Plan 05 should replace the body with HKCU\Software\Valve\Steam\SteamPath registry resolution per D-02 — the final `Result` should be `<SteamPath>\steamapps\common\SteamVR\drivers\micmap` (nested layout per D-01). The function signature `function GetMicMapInstallDir(Param: String): String;` is fixed — Plan 05 only touches the `begin ... end;` body.
- **All other [Code] hooks still green-field.** `InitializeSetup`, `PrepareToInstall`, `CurStepChanged`, `CurUninstallStepChanged` are NOT present in Plan 04's output — Plans 05-08 add them verbatim at the bottom of the `[Code]` section.
- **[Setup] preamble is frozen.** Plans 05-08 should NOT modify `[Setup]` — specifically AppId, Architectures, PrivilegesRequired, Uninstallable, OutputDir, OutputBaseFilename. If a future plan needs new [Setup] directives (e.g., `SetupMutex=MicMapInstaller`), it should append — never replace.
- **[Files] block is frozen unless new staged artifacts ship.** If Plans 05-08 add new files to `build/stage/*`, append to `[Files]` — don't modify existing entries.

## Threat Flags

None new this plan. T-04-04 (Source path injection) remains mitigated as planned — all 5 `[Files] Source:` lines use the literal `{#STAGE_DIR}\*` prefix, no user-supplied path segments, no runtime interpolation. T-04-03 (unsigned installer / SmartScreen warning) accepted per D-16 — v1.0 ships unsigned.

## Self-Check: PASSED

Files verified on disk:

- `installer/MicMap.iss` — 87 lines; `grep -q 'AppId={{BC6D91A7-A852-4562-8CBF-58FC4662FEDC}'` passes; `grep -q 'ArchitecturesAllowed=x64os'` passes; `! grep -qE 'ArchitecturesAllowed=x64($|[^o])'` passes (no bare deprecated x64); `grep -q 'PrivilegesRequired=admin'` passes; `grep -q 'Uninstallable=yes'` passes; `grep -q 'SetupIconFile=micmap.ico'` passes; `grep -q '{#STAGE_DIR}'` passes; `grep -q 'restartreplace'` passes; `! grep -qE 'Source:[^{]*\.\./'` passes (no repo-relative paths); `grep -q '#ifndef STAGE_DIR'` passes. 5 Source lines reference STAGE_DIR.
- `installer/micmap.ico` — 6 384 bytes; valid ICO header `00 00 01 00 04 00` (reserved=0, type=ICO, count=4); loads successfully via `New-Object System.Drawing.Icon`; SetupIconFile accepts it in ISCC compile.
- `apps/micmap/micmap.rc` — Line 11: `IDI_MICMAP_ICON ICON "../../installer/micmap.ico"` (uncommented, pointing at shared asset); `cmake --build build --config Release --target micmap` succeeds with icon resource embedded in 910 848-byte `build/bin/Release/micmap.exe`.

Commits verified via `git log HEAD~2..HEAD`:

- 74fce38 feat(04-04): add shared installer/micmap.ico + wire IDI_MICMAP_ICON
- c38b77d feat(04-04): add installer/MicMap.iss [Setup]+[Files]+stub [Code]

ISCC smoke compile verified via `build/installer/MicMap-Setup-v0.0.0-test.exe` file existence (2 519 889 bytes).

## TDD Gate Compliance

Both Task 1 and Task 2 were `tdd="true"` but asset-wiring tasks without meaningful unit-level tests. Per the TDD "wiring-task exception pattern" established in Plan 04-02 and 04-03:

- **Task 1 RED:** `test -f installer/micmap.ico` → MISSING (confirmed pre-edit)
- **Task 1 GREEN:** `test -f installer/micmap.ico && test -s installer/micmap.ico` + `cmake --build ... --target micmap` exit 0 (confirmed post-edit)
- **Task 2 RED:** `test -f installer/MicMap.iss` → MISSING (confirmed pre-edit)
- **Task 2 GREEN:** All 11 grep acceptance criteria pass + ISCC smoke compile exit 0 (confirmed post-edit)

No separate `test(...)` commits — empirical RED/GREEN verified via tool output, not unit tests. Landed as `feat(...)` commits matching the established Phase 4 pattern.

---
*Phase: 04-installer*
*Plan: 04*
*Completed: 2026-04-24*

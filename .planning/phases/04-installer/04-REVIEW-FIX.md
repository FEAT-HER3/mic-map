---
phase: 04-installer
fixed_at: 2026-04-24T10:45:00Z
review_path: .planning/phases/04-installer/04-REVIEW.md
iteration: 1
findings_in_scope: 6
fixed: 5
skipped: 1
status: partial
---

# Phase 04: Code Review Fix Report

**Fixed at:** 2026-04-24T10:45:00Z
**Source review:** `.planning/phases/04-installer/04-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 6 (HR-01, MR-01, MR-02, LR-01, IN-01, IN-02)
- Fixed: 5
- Skipped: 1 (IN-01, out of scope)

All fixes land in `installer/MicMap.iss`. No syntax checker exists for Inno
Setup Pascal Script, so verification was Tier 1 (re-read + context inspection)
per the 3-tier verification strategy (Tier 3 fallback). The installer `.iss`
script will be compiled end-to-end by the existing CMake `package` target; no
syntax regression is possible short of that full build, which is out of scope
for per-fix verification.

## Fixed Issues

### HR-01: PromptAndMaybeRemoveUserData blocks silent uninstall

**Files modified:** `installer/MicMap.iss`
**Commit:** `118730d`
**Applied fix:** Added `WizardSilent()` guard ahead of the `MsgBox` call in
`PromptAndMaybeRemoveUserData`. In `/SILENT` or `/VERYSILENT` mode the procedure
now logs and exits without prompting, defaulting to "keep user data" per D-13.
This unblocks headless CI / scripted uninstall paths that would otherwise have
hung on a dialog with no operator.

### MR-01: g_SteamVRDir empty during uninstall — vrpathreg removedriver silently skipped

**Files modified:** `installer/MicMap.iss`
**Commit:** `29e483b`
**Applied fix:** Re-derive `g_SteamVRDir` from `{app}` at the top of
`CurUninstallStepChanged` when the global is empty (which it always is at
uninstall time — `InitializeSetup` only runs during install). Two
`ExtractFilePath` applications back up from `{SteamVR}\drivers\micmap` to
`{SteamVR}`, with `RemoveBackslashUnlessRoot` to strip the trailing backslash.
Reuses existing `GetVrpathreg` / `VrpathregExists` helpers unchanged, so
`vrpathreg removedriver` now actually executes and the driver is properly
deregistered from `openvrpaths.vrpath` on uninstall.

**Human verification recommended:** This fix affects runtime logic (path
derivation from `{app}`); a compile-and-install-then-uninstall cycle on a real
SteamVR layout is the only end-to-end proof the derivation is correct. The
math (`{SteamVR}\drivers\micmap` → two parents up → `{SteamVR}`) is
straightforward, but the D-01 invariant is load-bearing.

### MR-02: Double FileExists check creates TOCTOU window between uninstall steps 1 and 2

**Files modified:** `installer/MicMap.iss`
**Commit:** `d47cb27`
**Applied fix:** Collapsed the two independent `FileExists(MicMapExe)` guards
into a single guard that brackets both `--unpatch-bindings` and
`--unregister-vrmanifest` invocations. Added a comment explaining why this is
safe (Inno deletes files at `usDeleteAppFiles`/`usPostUninstall`, not
`usUninstall`) and why the previous pattern was latent-TOCTOU (step 2 would
silently skip with no `Failed.Add` if the exe disappeared mid-teardown).

### LR-01: SweepLegacyBindings — no directory-attribute guard in FindFirst loop

**Files modified:** `installer/MicMap.iss`
**Commit:** `12d2157`
**Applied fix:** Added `(FindRec.Attributes and 16) = 0` check (16 =
`FILE_ATTRIBUTE_DIRECTORY`) so that directories whose name ends in `.json`
are skipped rather than producing misleading `FAILED to remove` log entries
from `DeleteFile`.

### IN-02: PromptAndMaybeRemoveUserData silent early-exit produces no log

**Files modified:** `installer/MicMap.iss`
**Commit:** `38f0325`
**Applied fix:** Added `Log(...)` call before the early `Exit` when
`AppDataDir` does not exist, so uninstall troubleshooting can unambiguously
distinguish "no user data present" from "data present and retained/removed".

## Skipped Issues

### IN-01: README.md references to deleted batch scripts

**File:** `README.md:23,38,119`
**Reason:** Out of scope. The review itself classifies this as known accepted
debt and explicitly defers the README rewrite to Phase 5 DOC-01. No change
needed in Phase 4. Surfaced here for traceability into Phase 5.
**Original issue:** README still references the four deleted batch scripts
(`install_driver.bat`, `uninstall_driver.bat`, `install_driver_test.bat`,
`test_driver.bat`). Phase 5 DOC-01 owns the README rewrite.

---

_Fixed: 2026-04-24T10:45:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

---
phase: 04-installer
plan: 05
subsystem: [installer-packaging]
tags: [inno-setup, pascal-script, registry, hkcu, steamvr, d-01, d-02, d-04]
requires:
  - phase: 04-04
    provides: installer/MicMap.iss skeleton with [Setup] + [Files] + stub [Code] GetMicMapInstallDir
provides:
  - installer/MicMap.iss [Code] block with module-level g_SteamVRDir, GetSteamPath, InitializeSetup, and real GetMicMapInstallDir body
  - HKCU\Software\Valve\Steam\SteamPath -> {SteamVR}\drivers\micmap resolution (D-02 + D-01 wired)
  - D-04 fail-closed abort with two distinct MsgBox bodies for "Steam missing" vs "SteamVR missing"
  - Shared module state (g_SteamVRDir) for Plans 06/07/08 downstream callbacks
affects:
  - 04-06 (PrepareToInstall WMI gate — can reference g_SteamVRDir if needed)
  - 04-07 (CurStepChanged(ssPostInstall) vrpathreg orchestration — reads g_SteamVRDir for vrpathreg path + {app})
  - 04-08 (CurUninstallStepChanged(usUninstall) symmetric teardown — reads g_SteamVRDir)
tech-stack:
  added:
    - Pascal Script module-level var declaration pattern (Inno [Code])
    - RegQueryStringValue + HKEY_CURRENT_USER registry read (no elevation required)
    - StringChangeEx slash normalization (HKCU SteamPath ships with `/`, [Files] needs `\`)
    - DirExists fail-closed gate (T-04-05 Tampering mitigation)
  patterns:
    - Module-scope `var` declaration between [Code] header and first function, so all callbacks share state
    - Chr(13) + Chr(10) string literal concatenation instead of Pascal `#13#10` char escape (ISPP-safe — Inno's preprocessor eats lines starting with `#`)
    - MsgBox with named constants (mbError, MB_OK) — per Pitfall 16 point 5
    - Two-stage fail-closed: (1) empty registry value, (2) directory does not exist — each with a distinct actionable MsgBox body
key-files:
  created: []
  modified:
    - installer/MicMap.iss
key-decisions:
  - "Used Chr(13) + Chr(10) instead of the documented `#13#10` Pascal char escape. The plan's verbatim snippet from 04-RESEARCH.md §Open Question 1f uses `#13#10`, but Inno's ISPP preprocessor reads every line of the .iss including the [Code] block and interprets any line beginning with `#` as a preprocessor directive (`#define`, `#include`, etc.). ISCC aborts with `Unknown preprocessor directive.` at the first `#13#10` line. Workaround is standard per community convention — concatenate Chr() calls. Semantically identical, ISPP-safe."
  - "g_SteamVRDir resolved ONCE in InitializeSetup (wizard-init callback, first [Code] hook to run). Downstream callbacks (PrepareToInstall, CurStepChanged, CurUninstallStepChanged) read — never write — the variable. No re-entrancy, no registry re-reads, one source of truth per installer run."
  - "GetMicMapInstallDir body intentionally does NOT guard against empty g_SteamVRDir. Control flow guarantees InitializeSetup returns True before GetMicMapInstallDir is ever evaluated ({code:GetMicMapInstallDir} is resolved on the Welcome → Ready transition, AFTER InitializeSetup has gated success). If InitializeSetup returns False the installer exits with no wizard pages shown, so GetMicMapInstallDir is never called. Defensive nil-check would add noise without protecting any real path."
  - "MsgBox strings hardcoded (no [CustomMessages] i18n). v1.0 is English-only — matches [Languages] `english; MessagesFile: compiler:Default.isl`. Future localization would lift these into CustomMessages but is out of scope for v1."
  - "ISPP `#13#10` gotcha is a recurring Plan 4 theme (Plan 04 hit `;`-vs-`//` comment-syntax; Plan 05 hit `#`-prefix preprocessor directives). Both are cases where a Pascal-valid construct clashes with ISPP's line-granular preprocessor. Future [Code] additions in Plans 06/07/08 should check char-literal usage at column 0 and comment prefixes."
requirements-completed: [INST-01]
duration: 12min
completed: 2026-04-24
---

# Phase 4 Plan 05: Registry-Driven SteamVR Install Path Resolution

**HKCU\\Software\\Valve\\Steam\\SteamPath registry lookup wired into installer/MicMap.iss with module-level g_SteamVRDir shared state, D-04 fail-closed abort (two distinct MsgBox bodies), and D-01 nested layout (`{SteamVR}\\drivers\\micmap`) — ISCC smoke-compile exits 0 producing a 2.5 MB Setup exe.**

## Performance

- **Duration:** ~12 min
- **Tasks:** 1
- **Files modified:** 1 (installer/MicMap.iss)
- **Commits:** 1

## Accomplishments

- Replaced the Plan 04 placeholder `GetMicMapInstallDir` body (`{autopf}\\MicMap`) with the real `g_SteamVRDir + '\\drivers\\micmap'` implementation
- Added module-level `var g_SteamVRDir: String;` declaration between the `[Code]` header and the first function — visible to all future callbacks without re-reading the registry
- Implemented `GetSteamPath()` with `RegQueryStringValue(HKEY_CURRENT_USER, 'Software\\Valve\\Steam', 'SteamPath', SteamPath)` and `StringChangeEx` slash-normalization (HKCU ships the path with forward slashes; [Files] composition needs backslashes)
- Implemented `InitializeSetup()` with two fail-closed paths:
  1. Empty registry value → MsgBox "SteamVR was not detected via HKCU\\Software\\Valve\\Steam\\SteamPath." (matches 04-VALIDATION 04-05-02 grep)
  2. Registry present but `{SteamPath}\\steamapps\\common\\SteamVR` does not exist → MsgBox "Steam is installed, but SteamVR was not found at: {path}"
- Verified with ISCC 6.7.1 smoke compile: exit 0 (0.750 sec), produced `build/installer/MicMap-Setup-v0.0.0-test.exe` (2.5 MB) — Pascal syntax clean, [Setup] frozen, [Files] still sourced from STAGE_DIR

## Task Commits

1. **Task 1: Replace Plan 04 stub with real registry resolution + D-04 abort** — `cba5bcf` (feat)

_TDD wiring-task exception pattern (consistent with Plan 04): verification is grep-based + empirical (ISCC compile-green), not unit-test-based. RED state was self-evident (pre-edit greps for `g_SteamVRDir` and `function GetSteamPath` both empty; `autopf..MicMap` present). GREEN confirmed by 14-check acceptance grep suite + ISCC exit 0. No separate `test(...)` commit._

## Actual Pascal Body Committed

```pascal
[Code]
// ---------------------------------------------------------------
// Plan 05: SteamVR registry resolution (D-02) + D-04 no-Steam abort
// Module-level g_SteamVRDir is populated by InitializeSetup and
// reused by Plans 06 (WMI gate), 07 (vrpathreg), and 08 (teardown).
// ---------------------------------------------------------------
var
  g_SteamVRDir: String;  // Resolved once in InitializeSetup. Reused by Plans 06/07/08.

function GetSteamPath(): String;
var
  SteamPath: String;
begin
  Result := '';
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Valve\Steam',
                         'SteamPath', SteamPath) then
  begin
    StringChangeEx(SteamPath, '/', '\', True);
    Result := SteamPath;
  end;
end;

function InitializeSetup(): Boolean;
var
  SteamPath: String;
begin
  Result := False;
  SteamPath := GetSteamPath();
  if SteamPath = '' then
  begin
    MsgBox('SteamVR was not detected via HKCU\Software\Valve\Steam\SteamPath.' +
           Chr(13) + Chr(10) + Chr(13) + Chr(10) +
           'Please install Steam and SteamVR, then re-run this installer.',
           mbError, MB_OK);
    Exit;
  end;
  g_SteamVRDir := SteamPath + '\steamapps\common\SteamVR';
  if not DirExists(g_SteamVRDir) then
  begin
    MsgBox('Steam is installed, but SteamVR was not found at:' + Chr(13) + Chr(10) +
           g_SteamVRDir + Chr(13) + Chr(10) + Chr(13) + Chr(10) +
           'Please install SteamVR via Steam, then re-run this installer.',
           mbError, MB_OK);
    Exit;
  end;
  Result := True;
end;

function GetMicMapInstallDir(Param: String): String;
begin
  // D-01: nested layout {SteamVR}\drivers\micmap.
  // Relies on g_SteamVRDir being populated by InitializeSetup.
  Result := g_SteamVRDir + '\drivers\micmap';
end;
```

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocker] ISPP preprocessor interprets `#13#10` Pascal char-literals as preprocessor directives**

- **Found during:** Task 1 ISCC smoke compile (first invocation)
- **Issue:** The plan's verbatim snippet from 04-RESEARCH.md §Open Question 1f used Pascal's `#13#10` char-literal syntax for CRLF inside MsgBox strings. ISCC's ISPP preprocessor runs line-by-line across the entire .iss file (including [Code] blocks) and interprets any line beginning with `#` as a preprocessor directive. Error: `Error on line 111 in ... Unknown preprocessor directive. Compile aborted.`
- **Fix:** Replaced both `#13#10` char-literals (one per MsgBox body) with `Chr(13) + Chr(10)` string concatenation. Semantically identical output (CRLF between MsgBox message lines), ISPP-safe (no `#` at column 0). Community-standard workaround; noted in Inno Setup FAQ.
- **Files modified:** `installer/MicMap.iss` (within same Task 1 commit — fix was caught during pre-commit verification)
- **Verification:** ISCC re-run exit 0 (0.750 sec), 2 519 889-byte Setup exe produced. All 14 grep acceptance criteria still green post-fix.
- **Committed in:** `cba5bcf` (Task 1 commit)

**Total deviations:** 1 auto-fixed (1 Rule 3 blocker — ISPP/`#13#10` clash).
**Impact on plan:** No scope creep. No architectural change. The plan's intended behavior (CRLF-separated MsgBox bodies) is preserved — only the Pascal syntax for expressing it changed. Noted as a key-decision for Plans 06-08 authors.

## Issues Encountered

- **ISCC STAGE_DIR interpreted relative to .iss directory, not CWD.** First smoke-compile attempt passed `/DSTAGE_DIR=build/stage` as seen from the worktree root; ISCC resolved Source paths against `installer/build/stage/...` instead. Worked around by passing absolute paths (`/DSTAGE_DIR=<worktree>\build\stage`). Noted — the CMake `package` target from Plan 03 passes absolute paths natively, so this is only a smoke-test-from-Bash concern.
- **Worktree had no `build/` tree.** Plan 04's smoke test relied on a pre-existing `build/stage/` from Plan 03's `cmake --install`. This executor worktree is a fresh checkout with no build artifacts. Created a minimal dummy stage (5 touch-ed empty files matching the [Files] Source entries) solely for the ISCC smoke — cleaned up post-compile. The smoke test exercises Pascal syntax + [Files] path resolution + ISPP preprocessor; it does NOT exercise real artifact packaging (that's owned by the Plan 03 `package` target in CI).

## ISCC Smoke-Compile Result

| Parameter | Value |
|-----------|-------|
| ISCC path | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` (v6.7.1) |
| `/DMICMAP_VERSION` | `0.0.0-test` |
| `/DSTAGE_DIR` | `<worktree-absolute>\build\stage` (dummy 5-file tree) |
| `/DOUTPUT_DIR` | `<worktree-absolute>\build\installer` |
| Result | `Successful compile (0.750 sec)` — exit 0 |
| Output file | `build/installer/MicMap-Setup-v0.0.0-test.exe` (2 519 889 bytes) |
| Files compressed | 5 (driver_micmap.dll, driver.vrdrivermanifest, resources\.keep, micmap.exe, app.vrmanifest) |

Dummy stage + installer output + smoke wrapper were deleted post-compile; only `installer/MicMap.iss` is staged for commit.

## g_SteamVRDir Module-Scope Confirmation

Declared on line 85 of `installer/MicMap.iss` (immediately after the [Code] section header comment, BEFORE any function definition):

```pascal
[Code]
// ---------------------------------------------------------------
// Plan 05: SteamVR registry resolution (D-02) + D-04 no-Steam abort
// ...
// ---------------------------------------------------------------
var
  g_SteamVRDir: String;  // Resolved once in InitializeSetup. Reused by Plans 06/07/08.

function GetSteamPath(): String;
...
```

Scope verified: Pascal Script's `var` at the top of a section (outside any `begin...end` block) is module-level. Any function added to the same `[Code]` block — including Plans 06/07/08's `PrepareToInstall`, `CurStepChanged`, `CurUninstallStepChanged` — will see `g_SteamVRDir` as a bare identifier with no qualification needed. Read-only from downstream callbacks (only `InitializeSetup` writes it).

## Hand-off Notes for Plans 06/07/08

- **`g_SteamVRDir` lifecycle:** populated by `InitializeSetup` (runs at wizard init, BEFORE any page). If `InitializeSetup` returns False, no downstream callback fires (installer aborts). Therefore Plans 06/07/08 can assume `g_SteamVRDir` is non-empty and points to an existing directory whenever their code runs — no defensive nil-check needed.
- **Available in:** `PrepareToInstall` (Plan 06 WMI gate), `CurStepChanged(ssPostInstall)` (Plan 07 vrpathreg adddriver), `CurUninstallStepChanged(usUninstall)` (Plan 08 vrpathreg removedriver). All three run AFTER `InitializeSetup` per Inno's documented event order.
- **Do NOT re-declare `g_SteamVRDir`** in a nested scope. Doing so shadows the module-level var and breaks the shared-state contract. Plans 06/07/08 should only READ it.
- **Value format:** `{SteamPath}\steamapps\common\SteamVR` — backslash-separated, no trailing slash. For Plan 07 `vrpathreg` adddriver, pass `{app}` (which resolves via `GetMicMapInstallDir` → `g_SteamVRDir + '\drivers\micmap'`). For Plan 07 `vrpathreg.exe` locator, look under `g_SteamVRDir + '\bin\win64\vrpathreg.exe'`.
- **ISPP caveat for future [Code] additions:** Do NOT start a line with `#` inside Pascal string literals. Use `Chr(13) + Chr(10)` for CRLF instead of `#13#10`. Use `Chr(9)` for tab. Use `'foo' + Chr(34) + 'bar'` for embedded quotes. This is a recurring Plan 4 gotcha (Plan 04 hit `;`-vs-`//` comments; Plan 05 hit `#`-prefix preprocessor).

## Threat Model Review

| Threat | Disposition | Status |
|--------|-------------|--------|
| T-04-05 Tampering (malicious HKCU SteamPath pointing at e.g. C:\Windows) | mitigate | Wired: `DirExists(g_SteamVRDir)` rejects any path where `{value}\steamapps\common\SteamVR` is not a real directory. Fail-closed abort. |
| T-04-05 Information Disclosure (SteamPath in MsgBox) | accept | Path is user-scope, already known to user. Present in MsgBox #2 body. |
| T-04-05 Denial of Service (installer refuses when SteamVR missing) | accept | This IS the requested D-04 behavior — fail-closed, clear recovery action. |

All three dispositions honored as planned. No new threats introduced.

## Threat Flags

None new this plan. T-04-05 Tampering is mitigated via DirExists gate (read-only registry access, no writes). No new network endpoints, no new trust boundaries, no new file access patterns beyond the read-only HKCU registry lookup covered by the existing threat model.

## Self-Check: PASSED

Files verified on disk:

- `installer/MicMap.iss` — 131 lines; all 14 grep acceptance criteria pass:
  - `grep -q 'var' installer/MicMap.iss` — PASS
  - `grep -q 'g_SteamVRDir' installer/MicMap.iss` — PASS (4 occurrences: decl + InitializeSetup write + DirExists check + GetMicMapInstallDir read)
  - `grep -q 'function GetSteamPath' installer/MicMap.iss` — PASS
  - `grep -q 'RegQueryStringValue' installer/MicMap.iss` — PASS
  - `grep -q 'HKEY_CURRENT_USER' installer/MicMap.iss` — PASS
  - `grep -q 'Software.Valve.Steam' installer/MicMap.iss` — PASS
  - `grep -q 'function InitializeSetup' installer/MicMap.iss` — PASS
  - `grep -q 'SteamVR was not detected' installer/MicMap.iss` — PASS (04-VALIDATION 04-05-02 satisfied)
  - `grep -q 'DirExists' installer/MicMap.iss` — PASS
  - `grep -q 'function GetMicMapInstallDir' installer/MicMap.iss` — PASS
  - `! grep -q 'autopf..MicMap' installer/MicMap.iss` — PASS (Plan 04 placeholder body removed)
  - `! grep -q 'Placeholder' installer/MicMap.iss` — PASS (Plan 04 placeholder comment removed)
  - `grep -q 'g_SteamVRDir.*drivers.micmap' installer/MicMap.iss` — PASS (D-01 nested layout)
  - Plus: `! grep -q '#13#10' installer/MicMap.iss` — PASS (ISPP-safe Chr() replacement)

Commit verified via `git log`:

- `cba5bcf feat(04-05): wire HKCU SteamVR resolution + D-04 no-Steam abort`

ISCC smoke compile verified: exit 0, 2 519 889-byte Setup exe produced (artifact deleted post-test; only the .iss change is persisted).

## TDD Gate Compliance

Task 1 was `tdd="true"` but a single-file [Code] replacement without meaningful unit-testable behavior. Per the TDD "wiring-task exception pattern" established in Plan 04-03 and 04-04:

- **RED state confirmed pre-edit:** `grep -q 'g_SteamVRDir' installer/MicMap.iss` → no match; `grep -q 'autopf..MicMap' installer/MicMap.iss` → match (placeholder present).
- **GREEN state confirmed post-edit:** 14 grep acceptance criteria all pass + ISCC smoke compile exit 0.
- No separate `test(...)` commit — empirical RED/GREEN verified via tool output, not unit tests. Landed as `feat(...)` commit matching established Phase 4 pattern.

---
*Phase: 04-installer*
*Plan: 05*
*Completed: 2026-04-24*

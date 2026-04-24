---
phase: 04-installer
plan: 09
subsystem: installer
tags: [installer, inno-setup, cmake, packaging, close-out, end-to-end]
one_liner: "End-to-end close-out: batch scripts deleted, `cmake --build --target package` produces MicMap-Setup-v0.1.0.exe (2.4MB) — Phase 4 Success Criterion #5 satisfied."
requires:
  - plan: 04-08
    provides: "CurUninstallStepChanged(usUninstall) teardown orchestrator; installer/MicMap.iss fully composed with Plans 05-08 Pascal callbacks"
  - plan: 04-03
    provides: "add_custom_target(package) machinery; ISCC_EXECUTABLE find_program; build/stage layout"
  - plan: 04-04
    provides: "installer/MicMap.iss [Setup] + [Files] + [Code] skeleton"
provides:
  - artifact: "build/installer/MicMap-Setup-v0.1.0.exe"
    description: "Shippable single-click MicMap installer — 2,522,747 bytes, SHA256 7150bde1472ce57ab2f8c5e77f1b3de600abf4a400dcfc601c937a82fe35b2c7"
    contains: "driver_micmap.dll + micmap.exe + app.vrmanifest + Pascal Script (Plans 05-08) + icon"
  - capability: "One build command (`cmake --build build --target package --config Release`) yields one shippable installer — ROADMAP Phase 4 Success Criterion #5 satisfied at file-exists level"
affects:
  - "scripts/ directory removed (batch-script installer no longer exists in repo — CONTEXT.md <domain> deliverable closed)"
  - "INST-07 closed at end-to-end level (Plan 03 closed at build-machinery level; this plan produces an actual .exe)"
tech_stack:
  added: []
  patterns:
    - "CMake add_custom_target(package) wrapping ISCC.exe with /D defines (D-18/D-19/D-20/D-21)"
    - "Stage-tree packaging — cmake --install --prefix into build/stage, then ISCC.exe compiles against that tree"
key_files:
  created: []
  modified: []
  deleted:
    - "scripts/install_driver.bat (248 lines)"
    - "scripts/uninstall_driver.bat (112 lines)"
    - "scripts/install_driver_test.bat (193 lines)"
    - "scripts/test_driver.bat (169 lines)"
decisions:
  - "ISCC invocation via CMake wrapper (Task B) satisfies Success Criterion #5; standalone ISCC re-run (Task C) is belt-and-braces"
  - "OPENVR_SDK_PATH pointed at sibling project `bey-closer-t1/extern/openvr` for local build — documented here; not checked into repo per STACK.md (SDK is a per-dev-box dependency)"
  - "README.md references to deleted batch scripts intentionally NOT updated — Phase 5 DOC-01 owns docs; transient mismatch accepted per CONTEXT.md <domain>"
metrics:
  duration_sec: 660
  completed_at: "2026-04-24T09:43:25Z"
  tasks_completed: 3
  files_created: 0
  files_deleted: 4
  files_modified: 0
requirements_closed:
  - "INST-07 (end-to-end): `cmake --build --target package` produces MicMap-Setup-vX.Y.Z.exe via ISCC.exe with correct /D defines"
---

# Phase 04 Plan 09: End-to-end close-out Summary

## Task Execution

### Task A — Delete obsolete batch scripts (CONTEXT.md `<domain>` deliverable)

Removed the four legacy batch scripts that the Inno Setup installer replaces:

- `scripts/install_driver.bat` — 248 lines; legacy `vrpathreg` driver installer
- `scripts/uninstall_driver.bat` — 112 lines; legacy driver uninstaller
- `scripts/install_driver_test.bat` — 193 lines; test-mode driver installer (auto-launch off)
- `scripts/test_driver.bat` — 169 lines; hmd_button_test launcher

Execution was a single `git rm` call. All four deletions cleanly staged; the `scripts/` directory is now empty (git removed the last file in it — the directory does not exist in the working tree). `grep -rE 'install_driver\.bat|uninstall_driver\.bat' CMakeLists.txt driver/CMakeLists.txt` returns exit 1 (no match) — Plan 03 already deleted the `copy_distributable_files` custom target that was the only CMake rule referencing the batch scripts, so no lingering build-system references remain.

`.github/` directory does not exist in this repo (no CI workflows) — the `.github` clause of the 04-VALIDATION 04-09-01 grep is vacuously satisfied.

**Commit:** `07f973c chore(04-09): delete obsolete batch scripts`

**Verify block output:**
```
=== git status ===
D  scripts/install_driver.bat
D  scripts/install_driver_test.bat
D  scripts/test_driver.bat
D  scripts/uninstall_driver.bat
=== file checks ===
OK: install_driver.bat absent
OK: uninstall_driver.bat absent
OK: install_driver_test.bat absent
OK: test_driver.bat absent
=== final grep ===
build-system grep exit: 1  (no match — clean)
```

README.md still contains three references to the deleted scripts (lines 23, 38, 119) — intentionally left per plan; Phase 5 DOC-01 owns the README rewrite. The transient doc/reality mismatch between Phase 4 close and Phase 5 open is explicitly accepted per CONTEXT.md `<domain>` out-of-scope item.

### Task B — End-to-end package build

Executed the three-step build chain:

1. **Clean configure** — `cmake -B build -DCMAKE_BUILD_TYPE=Release` with `OPENVR_SDK_PATH=C:/Users/decid/Documents/projects/bey-closer-t1/extern/openvr` (sibling project's SDK — the milestone's canonical OpenVR source). Output: `Configuring done (27.7s); Generating done (0.4s)`. `ISCC_EXECUTABLE` in CMakeCache.txt resolves to `C:/Program Files (x86)/Inno Setup 6/ISCC.exe`. `OpenVR_FOUND=TRUE`, so `driver/` subdirectory builds.

2. **Build all normal targets** — `cmake --build build --config Release` succeeded. Produces:
   - `build/bin/Release/micmap.exe` (main application)
   - `build/bin/Release/app.vrmanifest` (staged beside micmap.exe by Plan 03's custom step)
   - `build/driver/micmap/bin/win64/driver_micmap.dll` (sidecar driver)
   - Plus all test executables (hmd_button_test, test_*, etc.)

3. **Package target** — `cmake --build build --target package --config Release` succeeded. The custom target (per Plan 03) executed its four steps cleanly:
   - Step 1: `build/stage/` cleared and recreated
   - Step 2: `cmake --install` populated `build/stage/`
   - Step 3: ISCC.exe invoked with `/DMICMAP_VERSION=0.1.0 /DSTAGE_DIR=... /DOUTPUT_DIR=... installer/MicMap.iss`
   - Step 4: `Installer built: .../build/installer/MicMap-Setup-v0.1.0.exe`

**Produced artifact:**

| Field | Value |
| --- | --- |
| Path | `C:/Users/decid/Documents/projects/mic-map/.claude/worktrees/agent-a6a192a77cdf0c69f/build/installer/MicMap-Setup-v0.1.0.exe` |
| Size | 2,522,747 bytes (~2.4 MB) |
| Sanity floor | > 1 MB ✓ |
| SHA256 | `7150bde1472ce57ab2f8c5e77f1b3de600abf4a400dcfc601c937a82fe35b2c7` |
| ISCC.exe | `C:/Program Files (x86)/Inno Setup 6/ISCC.exe` (Inno Setup 6.7.1) |
| CMake version | 4.3 |
| Build config | Release |

**Final stage tree** (`find build/stage -type f | sort`):

```
build/stage/bin/app.vrmanifest
build/stage/bin/micmap.exe
build/stage/drivers/micmap/bin/win64/driver_micmap.dll
build/stage/drivers/micmap/driver.vrdrivermanifest
build/stage/drivers/micmap/resources/driver.vrresources
build/stage/drivers/micmap/resources/settings/default.vrsettings
build/stage/share/micmap/config.json
```

Matches the D-01 layout exactly: driver files under `drivers/micmap/`, app + vrmanifest under `bin/`. The `share/micmap/config.json` is installed by the root CMakeLists.txt `install(FILES config/default_config.json ... RENAME config.json)` rule (not packaged into the installer — MicMap's installer emits `%APPDATA%\MicMap\config.json` on first run rather than shipping it). No deviations from the expected layout; no mystery files appearing in stage from an unknown install() rule.

ISCC's `Compressing:` lines in the log confirm the six files actually embedded in the installer are: driver_micmap.dll, driver.vrdrivermanifest, driver.vrresources, default.vrsettings, micmap.exe, app.vrmanifest. That's the full Plan 03 stage minus `share/micmap/config.json` (correctly excluded from the .iss `[Files]` section).

**No commit for Task B** — the task produces only gitignored build outputs (`build/` is gitignored); there are no tracked-source changes to commit. The signal is "artifact exists on disk + SHA recorded in this SUMMARY."

### Task C — Standalone ISCC smoke re-run

Executed `"C:/Program Files (x86)/Inno Setup 6/ISCC.exe" /Qp "/DMICMAP_VERSION=0.1.0" "/DSTAGE_DIR=.../build/stage" "/DOUTPUT_DIR=.../build/installer" installer/MicMap.iss` with `MSYS_NO_PATHCONV=1` to prevent Git Bash from mangling the Windows-style paths in the /D defines.

- **Exit code:** 0
- **Artifact re-produced:** mtime advanced from 1777023743 → 1777023790 (47 seconds after Task B's build)
- **Size:** 2,522,747 bytes (identical — deterministic build)
- **SHA256:** `7150bde1472ce57ab2f8c5e77f1b3de600abf4a400dcfc601c937a82fe35b2c7` (identical — ISCC produces bit-identical output when inputs unchanged)

Belt-and-braces check passed: ISCC exits 0 on the full composed `installer/MicMap.iss` standalone (not just via CMake wrapper). Plan 05-08 Pascal callbacks (`InitializeSetup`, `PrepareToInstall`, `CurStepChanged`, `CurUninstallStepChanged`, `SweepLegacyBindings`, `PromptAndMaybeRemoveUserData` — 19 grep hits across the .iss file) compile cleanly.

**No commit for Task C** — same rationale as Task B (no tracked source changes).

## Deviations from Plan

**Environment deviation (not plan-driven):**

1. **`OPENVR_SDK_PATH` required for configure** — Plan 03's `find_package(OpenVR)` was silently skipping the driver build (OpenVR SDK not bundled in mic-map repo per STACK.md). First configure attempt reported `Skipping driver build - OpenVR SDK not found`, which would have produced an empty driver stage. Reconfigured with `OPENVR_SDK_PATH=C:/Users/decid/Documents/projects/bey-closer-t1/extern/openvr` (sibling milestone's SDK — same one validated in bey-closer-t1 per CONTEXT.md). After this, `OpenVR_FOUND=TRUE` and `driver_micmap.dll` built. This is a dev-box setup concern, not a plan gap — documented here for UAT-machine provisioning. **Action for downstream plans:** Phase 5 DOC-01 should document the `OPENVR_SDK_PATH` env var for fresh clones; current installer README does not mention it.

2. **Task C Git Bash path-mangling** — Bash in this environment auto-converted `/DSTAGE_DIR=C:/...` to `/DSTAGE_DIR=C:\Users\...\Git\...` (MSYS path conversion), which confused ISCC into interpreting the pieces as multiple script filenames. Fixed by setting `MSYS_NO_PATHCONV=1` before the ISCC invocation. Plan 03 Task 2's CMake wrapper does NOT have this issue because `add_custom_target(... VERBATIM)` bypasses shell path mangling. **Action for downstream plans:** if a contributor re-runs Task C's invocation verbatim from Git Bash, they'll need `MSYS_NO_PATHCONV=1` or PowerShell. Documenting here as a known pitfall.

**No plan-logic deviations** — plan executed exactly as written; acceptance criteria met as specified.

## Auto-fixed Issues

None — no Rule 1/2/3 deviations triggered.

## Known Stubs

None introduced by this plan. (The plan only deletes files and runs builds.)

## Threat Flags

None. No new security surface. The `build/installer/*.exe` artifact boundary is identical to Plan 03's (dev-machine trust model, code-signing deferred to v1.x per D-16). The batch-script deletion *reduces* surface (four files of legacy install logic no longer in the repo).

## Self-Check

- [x] `scripts/install_driver.bat` absent — FOUND ABSENT
- [x] `scripts/uninstall_driver.bat` absent — FOUND ABSENT
- [x] `scripts/install_driver_test.bat` absent — FOUND ABSENT
- [x] `scripts/test_driver.bat` absent — FOUND ABSENT
- [x] Commit `07f973c chore(04-09): delete obsolete batch scripts` — FOUND in git log
- [x] `build/installer/MicMap-Setup-v0.1.0.exe` exists, 2,522,747 bytes, > 1MB — FOUND
- [x] Stage tree at `build/stage/` contains driver_micmap.dll, micmap.exe, app.vrmanifest at D-01 paths — FOUND
- [x] ISCC exit code 0 on standalone re-run — VERIFIED

## Self-Check: PASSED

## Phase 4 Close-out Signal

With Plan 09 complete, all 7 INST requirements are now closed at the code + package-build level:

| Requirement | Closed by | Level |
| --- | --- | --- |
| INST-01 (single-click installer) | Plan 04 | code |
| INST-02 (vrpathreg adddriver during install) | Plan 05 | code |
| INST-03 (register app.vrmanifest + auto-launch) | Plan 06 | code |
| INST-04 (Pascal Script orchestrator) | Plans 07-08 | code |
| INST-05 (uninstall teardown) | Plan 08 | code |
| INST-06 (upgrade ghost-binding sweep) | Plans 05 (implemented) + D-07 disposition (documented-as-defense-in-depth) | code |
| INST-07 (one build command → one shippable .exe) | Plan 03 (machinery) + Plan 09 (end-to-end) | build |

**Runtime validation (VM UAT) is out of scope for this plan** — it's the 04-VALIDATION.md §"Manual-Only Verifications" checklist that runs after `/gsd-verify-work 4`. The produced `MicMap-Setup-v0.1.0.exe` at SHA `7150bde14...` is the "ready-for-UAT" artifact.

## Hand-off to `/gsd-verify-work 4`

- **Artifact path:** `C:/Users/decid/Documents/projects/mic-map/.claude/worktrees/agent-a6a192a77cdf0c69f/build/installer/MicMap-Setup-v0.1.0.exe`
  - When the wave's worktree merges back, this relative path becomes `build/installer/MicMap-Setup-v0.1.0.exe` in the main checkout. The build must be re-run after merge — `build/` is gitignored.
- **SHA256 reference:** `7150bde1472ce57ab2f8c5e77f1b3de600abf4a400dcfc601c937a82fe35b2c7` (0.1.0, deterministic given identical inputs)
- **Verification marker:** ready-for-UAT
- **Required for UAT machine:** Inno Setup 6.7.1+ installed, OpenVR SDK available (e.g., via `OPENVR_SDK_PATH` env var pointing at a checkout of the OpenVR headers+lib — same SDK used by bey-closer-t1).
- **VM UAT procedure reference:** `.planning/phases/04-installer/04-VALIDATION.md` §Manual-Only Verifications (clean VM install → driver registered via vrpathreg → SteamVR starts → detect → toggle → uninstall → teardown confirmed).

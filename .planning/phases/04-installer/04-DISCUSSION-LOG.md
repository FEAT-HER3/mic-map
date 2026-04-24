# Phase 4: Installer - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-23
**Phase:** 04-installer
**Areas discussed:** Install layout + Steam path, Running-SteamVR gate UX, Upgrade-from-0.x cleanup, INST-08 backup policy, Uninstall data retention, Installer UI + signing, CMake package target shape

---

## Install layout + Steam path

### Q1: Where do MicMap's files physically install on disk?

| Option | Description | Selected |
|--------|-------------|----------|
| Own dir: `{autopf}\MicMap\` (Recommended) | Program Files\MicMap\ holds micmap.exe, app.vrmanifest, driver\ subfolder. vrpathreg adddriver {app}\driver. Clean uninstall. Doesn't depend on Steam path resolvable. | |
| Split: app in Program Files, driver in SteamVR dir | Driver files into {SteamVR}\drivers\micmap\ (matches current batch script). App in {autopf}\MicMap\. Needs Steam path at install. Two uninstall paths. | |
| Nested under `{SteamVR}\drivers\micmap\` | Everything under {SteamVR}\drivers\micmap\ (bey-closer-t1 pattern). Requires Steam path resolution. | ✓ |

**User's choice:** Nested under {SteamVR}\drivers\micmap\

### Q2: How does the installer locate vrpathreg.exe?

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcoded `{autopf}\Steam\...` + FileExists guard (Recommended) | Try {autopf}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe. Missing = clean skip. Matches research default; DIST-03 registry stays v2. | |
| Registry lookup: `HKCU\Software\Valve\Steam` | Query HKCU\Software\Valve\Steam\SteamPath. Handles non-default Steam install drive. Pulls DIST-03 forward into v1. | ✓ |
| Parse `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` | Parse openvrpaths JSON to find SteamVR runtime[0]. Most canonical but needs JSON parser in Pascal (painful). | |

**User's choice:** Registry lookup (pulls DIST-03 forward into v1)

### Q3: Where does micmap.exe + app.vrmanifest live within the nested layout?

| Option | Description | Selected |
|--------|-------------|----------|
| `{SteamVR}\drivers\micmap\apps\` (Recommended) | Matches current install_driver.bat APP_DEST convention. | |
| `{SteamVR}\drivers\micmap\bin\` | Clearer intent (bin = executables, root = driver manifest + resources). | ✓ |
| `{SteamVR}\drivers\micmap\` (root, flat) | Everything at driver root. Mixes app binary with driver.vrdrivermanifest + resources\. | |

**User's choice:** `{SteamVR}\drivers\micmap\bin\`

### Q4: Registry lookup fails (Steam not installed / non-standard install). What happens?

| Option | Description | Selected |
|--------|-------------|----------|
| Abort install with clear error (Recommended) | MsgBox + Abort. No partial install. Simple Pascal. | ✓ |
| Fall back to `{autopf}\Steam` hardcoded + FileExists guard | Two-layer lookup; abort only if both miss. More tolerant; more Pascal. | |
| Install to `{autopf}\MicMap\` standalone, skip vrpathreg | Degrade gracefully: install files, skip registration. Splits layout (two code paths). | |

**User's choice:** Abort install with clear error

---

## Running-SteamVR gate UX

### Q1: WMI detects SteamVR running. What does the installer do?

| Option | Description | Selected |
|--------|-------------|----------|
| Prompt-and-recheck loop (Recommended) | Dialog naming processes, [Retry]/[Cancel], loop until clean. Matches bey-closer-t1 pattern. | ✓ |
| Hard-block: abort installer | Error + abort. User must close SteamVR and relaunch installer. Simpler but more friction. | |
| Offer TerminateProcess | Dialog: "Close SteamVR now?" [Yes, force-close/Cancel]. Risky — user might lose VR state. | |

**User's choice:** Prompt-and-recheck loop

### Q2: When does the WMI check run?

| Option | Description | Selected |
|--------|-------------|----------|
| PrepareToInstall (before file write) (Recommended) | Pascal callback between wizard finish and first file copy. Clean abort, no partial. | ✓ |
| InitializeSetup (very first) | Earliest bail-out. Wizard hasn't shown yet — abrupt. | |
| Both: quick InitializeSetup + final PrepareToInstall | Defense-in-depth. Extra Pascal. | |

**User's choice:** PrepareToInstall

---

## Upgrade-from-0.x cleanup

### Q1: How aggressively does installer clean ghost controller bindings from 0.x?

| Option | Description | Selected |
|--------|-------------|----------|
| Surgical: delete specific 0.x filenames (Recommended) | Known artifacts: micmap_controller_profile*.json, old binding JSONs. Explicit list. Needs spike. | |
| Nuclear: glob-delete `%LOCALAPPDATA%\openvr\input\*micmap*` | Simple. Risk: false positives if future MicMap features legitimately write there. | |
| Leave-orphaned | vrpathreg removedriver + fresh adddriver enough. Stale JSONs harmless. Zero cleanup. | |
| **User free-text: "no cleanup necessary, as that version was never shipped."** | | ✓ |

**User's choice:** (free-text) no cleanup necessary, 0.x never shipped.

### Q2: How does installer know a 0.x install is present?

| Option | Description | Selected |
|--------|-------------|----------|
| Registry AppId match (Recommended) | Inno auto-detects prior install via stable AppId GUID. Run cleanup on upgrade only. | ✓ |
| Always run cleanup unconditionally | Defense-in-depth for manually-deleted 0.x. | |
| Opt-in checkbox on wizard | User decides. Adds UI surface; most users won't understand. | |

**User's choice:** Registry AppId match (moot since no cleanup needed per Q1)

### Q3: INST-06 disposition — 0.x never shipped, no real-world upgrade path to handle.

| Option | Description | Selected |
|--------|-------------|----------|
| Mark INST-06 N/A; drop from Phase 4 scope (Recommended) | Update REQUIREMENTS.md: "N/A — no 0.x install exists in the wild". Phase 4 skips cleanup + Pitfall 8 spike. | ✓ |
| Keep INST-06 as minimal defensive no-op | Scan dir, log if found, skip delete. 5 LOC insurance for dev dogfood. | |
| Replace INST-06 with "clean previous same-version install" | Repurpose slot. Redundant with INST-03. | |

**User's choice:** Mark INST-06 N/A

---

## INST-08 backup policy

### Q1: Installer's INST-08 `.micmap_backup` collision policy?

| Option | Description | Selected |
|--------|-------------|----------|
| Write-once: skip if `.micmap_backup` exists (Recommended) | Matches driver/src/bindings_patcher.cpp:198. Preserves TRUE pristine original forever. One policy shared. | ✓ |
| Always overwrite backup on every install | Backup reflects "state before THIS install". Diverges from driver-side. | |
| Rotate: `.micmap_backup.1`, `.2`, ... | Never lose state. Directory bloat. Overengineered. | |

**User's choice:** Write-once

### Q2: Installer patches vrcompositor_bindings_generic_hmd.json; driver-side does MORE (EnsureControllerTypeFiles). Installer scope?

| Option | Description | Selected |
|--------|-------------|----------|
| Installer patches generic_hmd only; driver continues extras at runtime (Recommended) | Installer handles the one file guaranteed to exist. Driver-side stays as fallback for controller-type-dependent files. Clean separation. | |
| Installer replicates full bindings_patcher logic in Pascal | Port C++ to Pascal. One-shot do-everything. Lots of painful Pascal JSON. Duplicated source of truth. | |
| Installer calls `micmap.exe --patch-bindings` | New CLI mode runs existing C++ patcher. Installer does [Run] post-install. Reuses proven code. Adds one CLI flag; symmetric `--unpatch-bindings`. | ✓ |

**User's choice:** `micmap.exe --patch-bindings`

---

## Uninstall data retention

### Q1: What happens to `%APPDATA%\MicMap\` on uninstall?

| Option | Description | Selected |
|--------|-------------|----------|
| Leave everything (Recommended) | User data is user-owned. Reinstall = settings + training persist. Standard Windows app behavior. | |
| Prompt: "Remove settings and training data?" [Yes/No] | Explicit user choice. Risk: user clicks Yes thinking it means "clean uninstall" and loses retrained profile. | ✓ |
| Always remove | Fully reversible uninstall. Reinstall retrains from scratch (~150 samples). | |
| Split: remove logs, keep config + training | Nuanced but defensible. Extra Pascal. | |

**User's choice:** Prompt Yes/No (default No per CONTEXT D-13)

---

## Installer UI + signing

### Q1: Installer wizard chrome (WizardStyle)?

| Option | Description | Selected |
|--------|-------------|----------|
| Modern + minimal pages (Recommended) | welcome → install dir → progress → finished. 4 clicks. DisableDirPage since path is SteamVR-derived. | ✓ |
| Classic style | Older look. No reason. | |
| Modern + license page | Requires LICENSE.md. Pulls license-choice into Phase 4. | |

**User's choice:** Modern + minimal

### Q2: Repo has no LICENSE file. For the installer?

| Option | Description | Selected |
|--------|-------------|----------|
| Skip license page for v1.0; add LICENSE later (Recommended) | No license gate. Matches repo state. Defers LICENSE choice to future phase. | ✓ |
| Add LICENSE.md in Phase 4 + wire LicenseFile= | Pull license-choice into Phase 4 scope. | |
| Bundle third-party NOTICES only | OpenVR + nlohmann + ImGui notices as LicenseFile (no MicMap license). | |

**User's choice:** Skip license page for v1.0

### Q3: Code-sign installer + binaries for v1.0?

| Option | Description | Selected |
|--------|-------------|----------|
| Defer signing to v1.x (Recommended) | Unsigned — SmartScreen warning on first run. Standard indie pattern. Zero Phase 4 cost. | ✓ |
| Sign with existing cert | Requires EV / OV cert. signtool.exe hook in CMake. | |
| Stub signtool plumbing, leave cert pluggable | Wire hook, default off. Future cert slots in. | |

**User's choice:** Defer signing to v1.x

### Q4: Install architecture scope?

| Option | Description | Selected |
|--------|-------------|----------|
| 64-bit only, x64 enforced (Recommended) | ArchitecturesAllowed=x64. SteamVR is x64-only. Refuses x86. | ✓ |
| Also allow ARM64 | Windows-on-ARM SteamVR limited/emulated. Defer. | |

**User's choice:** x64 only

---

## CMake package target shape

### Q1: How is ISCC wired into CMake for `cmake --build --target package`?

| Option | Description | Selected |
|--------|-------------|----------|
| `add_custom_target(package)` invoking ISCC directly (Recommended) | Simple, explicit. Matches existing copy_distributable_files pattern. No CPack. | ✓ |
| CPack external generator | More "idiomatic" but fragile. CPack-to-ISCC not a paved path. | |
| Standalone script + thin CMake target | scripts/build_installer.ps1 does real work. Easier to debug. Adds non-CMake dep. | |

**User's choice:** add_custom_target(package) invoking ISCC directly

### Q2: How is the file tree staged for ISCC input?

| Option | Description | Selected |
|--------|-------------|----------|
| `cmake --install` to stage dir; ISCC reads from there (Recommended) | Reuses existing install(TARGETS) rules. Single source of truth. | ✓ |
| ISCC reads build outputs directly | Skip staging. Brittle with multi-config generators. | |
| Hybrid: stage + keep install(TARGETS) | Two outputs, both consume install rules. | |

**User's choice:** cmake --install to stage dir

### Q3: Where does MicMap-Setup-vX.Y.Z.exe land?

| Option | Description | Selected |
|--------|-------------|----------|
| `build/installer/` (Recommended) | Sibling to build/bin, build/driver. Gitignore covered. | ✓ |
| `dist/` at repo root | Survives cmake clean. Needs gitignore. | |
| `build/package/` | Slightly CPack-idiomatic. No functional difference. | |

**User's choice:** build/installer/

---

## Claude's Discretion

Deferred to planner per CONTEXT.md §"Claude's Discretion":
- Exact AppId GUID value
- Exact dialog body wording (WMI prompt, no-Steam abort, uninstall retention prompt)
- Start Menu / Desktop shortcut presence
- Installer icon sourcing (reuse apps/micmap/micmap.rc vs new .ico)
- `[Files]` ordering, Flags: modifiers beyond restartreplace
- Refactor destination for bindings_patcher (src/bindings/ vs src/common/ vs src/steamvr/)
- Logger-injection shape (function pointer vs std::function vs interface)
- ISCC_EXECUTABLE cache-var fallback for portable Inno installs

## Deferred Ideas

Per CONTEXT.md §Deferred:
- Code-signing → v1.x
- LICENSE.md + installer license wiring → future phase
- DIST-01 finished-page "Launch SteamVR" → v1.x
- DIST-02 silent-install flags documentation → v1.x
- ARM64 Windows support → not scoped
- Start Menu / Desktop shortcuts → Claude's discretion, likely skip
- Portable zip distribution → not scoped
- Per-user (non-admin) install → not possible
- Chocolatey / winget / scoop packaging → v2+
- Auto-update / in-app updater → v2+
- CPack integration → rejected

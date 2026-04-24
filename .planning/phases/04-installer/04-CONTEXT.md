# Phase 4: Installer - Context

**Gathered:** 2026-04-23
**Status:** Ready for planning

<domain>
## Phase Boundary

A single admin-elevated `MicMap-Setup-vX.Y.Z.exe` installs the driver + app + `app.vrmanifest` + patches the HMD bindings file in one step. Its uninstaller reverses every one of those actions. CMake `package` target drives ISCC.exe.

Deliverables this phase:
- `installer/MicMap.iss` (Inno Setup 6.7.1 script) producing `MicMap-Setup-vX.Y.Z.exe`.
- CMake `add_custom_target(package)` that stages via `cmake --install` and invokes ISCC.exe with `/D` defines.
- New app CLI modes `micmap.exe --patch-bindings` / `--unpatch-bindings` (mirroring Phase 3's `--register-vrmanifest` pattern) for symmetric HMD-bindings patch/restore.
- Refactor: lift `driver/src/bindings_patcher.{hpp,cpp}` into a shared library so both `driver_micmap.dll` and `micmap.exe` can link it.
- Deletion of `scripts/install_driver.bat`, `scripts/uninstall_driver.bat`, `scripts/install_driver_test.bat`, `scripts/test_driver.bat` (replaced by the single `.exe` installer).
- Update `CMakeLists.txt:113` — drop `copy_distributable_files` batch-script copy once installer replaces them.

Out of scope (other phases / future milestones):
- README / architecture docs referring to the new installer flow → Phase 5 (DOC-01).
- LICENSE choice / `LICENSE.md` addition → future phase (deferred, see below).
- Code-signing certificate integration → v1.x.
- DIST-01 installer "Launch SteamVR now" finished-page action → v1.x.
- DIST-02 silent-install CLI flags documentation → v1.x (Inno supports `/SILENT` natively; documentation work only).
- INST-06 legacy 0.x ghost-controller cleanup → **voided** (0.x never shipped; REQUIREMENTS.md updated).

</domain>

<decisions>
## Implementation Decisions

### Install location + Steam path discovery (INST-01, INST-03, pulls DIST-03 forward)

- **D-01:** Install layout is **nested under `{SteamVR}\drivers\micmap\`**. Driver files (`driver_micmap.dll`, `driver.vrdrivermanifest`, `resources/`) live at the driver root; `micmap.exe` + `app.vrmanifest` live at `{SteamVR}\drivers\micmap\bin\`. `vrpathreg adddriver {SteamVR}\drivers\micmap` registers the driver. This differs from the research-default "own dir under `{autopf}\MicMap\`" — user-chosen layout keeps driver files co-located with SteamVR's driver tree while MicMap still owns its subdirectory (matches the letter of "MicMap owns its own driver directory" from PROJECT.md Key Decisions; nested-under-another-vendor only applied to bey-closer-t1 placing itself under Bigscreen Beyond).
- **D-02:** Installer resolves SteamVR path via **registry lookup** of `HKCU\Software\Valve\Steam\SteamPath`, then composes `{steam}\steamapps\common\SteamVR\`. This **pulls DIST-03 forward from v2 into v1 scope**. Lookup covers non-default Steam install drives (the common "Steam on D:/E:" case). Implementation is a `RegQueryValueEx` call in Inno Setup Pascal Script (standard pattern).
- **D-03:** App binaries at `{SteamVR}\drivers\micmap\bin\`. `app.vrmanifest`'s `binary_path_windows: "micmap.exe"` is already relative (next to the manifest per Phase 3 D-19) — bin/ placement works without Phase 3 changes. `GetModuleFileNameW` at runtime resolves to the absolute path regardless.
- **D-04:** **No-Steam fallback = abort install with clear error.** MsgBox: "SteamVR not detected via `HKCU\Software\Valve\Steam\SteamPath`. Please install Steam + SteamVR, then re-run this installer." No partial install, no degraded mode. `InitializeSetup` returns False.

### Running-SteamVR gate UX (INST-02)

- **D-05:** **Prompt-and-recheck loop.** WMI detects any of `vrserver.exe`, `vrmonitor.exe`, `vrcompositor.exe`, `vrdashboard.exe`, `vrwebhelper.exe`. Dialog names the specific process(es) found: "SteamVR is running ({process list}). Please close SteamVR, then click Retry." Buttons: **[Retry] [Cancel]**. Retry re-runs WMI; loop until clean or user cancels. No `TerminateProcess` — users keep control of their VR session (possible active game save, etc).
- **D-06:** WMI check runs in **`PrepareToInstall` Pascal callback** (between wizard finish and first file copy). Guarantees clean abort with no partial install. Defense-in-depth: `restartreplace` flag on `.dll` copies (already locked by INST-02) covers a user launching SteamVR mid-wizard.

### INST-06 legacy cleanup — VOIDED

- **D-07:** **INST-06 is N/A — 0.x virtual-controller version was never shipped.** No real-world upgrade path exists. Phase 4 ships zero ghost-cleanup code. Pitfall 8 research spike (ghost controller bindings under `%LOCALAPPDATA%\openvr\input\`) is dropped from Phase 4 blocker list. **REQUIREMENTS.md must be updated** to mark INST-06 as "N/A — no 0.x install exists in the wild; migration not required" at Phase 4 execute-phase close. Same-version reinstall safety is covered by INST-03 (unconditional `vrpathreg removedriver` before `adddriver`) + Inno Setup's auto-detected upgrade path.

### INST-08 HMD-bindings patch + backup (INST-08)

- **D-08:** `.micmap_backup` collision policy = **write-once (skip if backup exists)**. Matches `driver/src/bindings_patcher.cpp:198` behavior verbatim. Preserves the TRUE pristine original across every reinstall forever. Installer and driver share one policy, one source of truth.
- **D-09:** **Installer invokes `micmap.exe --patch-bindings` as a `[Run]` step and `micmap.exe --unpatch-bindings` as an `[UninstallRun]` step** (mirror of Phase 3 `--register-vrmanifest` / `--unregister-vrmanifest` pattern). Symmetric install/uninstall. Reuses proven C++ patcher logic. Installer itself does not link nlohmann/json or parse JSON in Pascal.
- **D-10:** **Refactor: lift `driver/src/bindings_patcher.{hpp,cpp}` into a shared library** (recommended location: `src/bindings/` — new sibling to `src/steamvr/`, or into `src/common/` if simpler) so both `driver_micmap.dll` and `micmap.exe` can link it. `DriverLog` dependency replaced with an injected logger sink (app side uses `common::Logger` / `MICMAP_LOG_*`, driver side keeps `DriverLog`). No behavioral change to the patcher itself — marker-key idempotency (`micmap_patched_v2`), atomic tmp-then-rename (`AtomicWriteJson`), and write-once backup semantics all preserved.
- **D-11:** **`--unpatch-bindings` restores from `.micmap_backup` if present, else clears the marker key in place.** Symmetric with the patch path. Skip (log + return 0) if target file missing or marker absent.
- **D-12:** SteamVR runtime path for the patch target (`{SteamVR}\resources\config\vrcompositor_bindings_generic_hmd.json`) is resolved **inside `micmap.exe`** via the same `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` parse the driver already does (`driver/src/bindings_patcher.cpp:29`). The installer does **not** pass the path; the patcher discovers it itself. Keeps the installer simple and matches driver-side behavior.

### Uninstall data retention

- **D-13:** On uninstall, **prompt "Remove MicMap settings and training data?" [Yes / No]** (Inno Setup `TaskDialog` or `MsgBox` with `MB_YESNO`). **Default = No** (preserve user data — training_data.bin represents ~150 mic samples, expensive to regenerate). "Yes" removes `%APPDATA%\MicMap\` recursively (config.json + training_data.bin + micmap.log). "No" leaves `%APPDATA%\MicMap\` intact for future reinstall. Program-files-level uninstall is unconditional (both branches remove installed binaries + vrpathreg registration + vrmanifest + bindings restore).

### Installer UI + arch

- **D-14:** `WizardStyle=modern`. Pages: **welcome → install-dir → progress → finished**. No license page (no LICENSE.md in repo yet), no component-select, no ready-to-install recap page. InstallDir derived from D-01/D-02 (read-only to the user — `DisableDirPage=yes` since path is SteamVR-derived, not user-choosable).
- **D-15:** **No license page in v1.0.** Repo has no LICENSE file at time of writing. Adding LICENSE.md + wiring `LicenseFile=` deferred to a later phase (candidate: Phase 5 Documentation, or a dedicated licensing micro-phase).
- **D-16:** **Unsigned v1.0 — code-signing deferred to v1.x.** SmartScreen warning on first run is acceptable for indie launch ("More info → Run anyway"). No signtool.exe plumbing in CMake. Revisit once project reputation/cert budget justifies.
- **D-17:** **x64-only.** `ArchitecturesAllowed=x64`, `ArchitecturesInstallIn64BitMode=x64`. Installer refuses to run on x86 Windows or ARM64. SteamVR is x64-only natively. Matches reality.

### CMake `package` target (INST-07)

- **D-18:** **`add_custom_target(package)` invoking ISCC.exe directly** with `/D` defines. Finds ISCC via `find_program(ISCC_EXECUTABLE ISCC PATHS "$ENV{ProgramFiles\(x86\)}/Inno Setup 6" "$ENV{ProgramFiles}/Inno Setup 6")`. Fails configure-time if absent with actionable message. Follows existing `add_custom_target(copy_distributable_files)` precedent (`CMakeLists.txt:113`). No CPack.
- **D-19:** **Stage via `cmake --install . --prefix build/stage --config Release`** reusing the existing `install(TARGETS ...)` rules at `CMakeLists.txt:143` and `driver/CMakeLists.txt:138`. ISCC reads from `build/stage` via `/DSTAGE_DIR=...`. The `install()` rules remain the single source of truth for "what files ship"; they also keep working for local `cmake --install` dev workflow. Phase 4 may need to add `install(FILES app.vrmanifest DESTINATION bin)` if not already present.
- **D-20:** **Artifact lands at `build/installer/MicMap-Setup-vX.Y.Z.exe`** (Inno `OutputDir`). Already git-ignored via `build/`. `cmake --build --target package` prints the absolute path at the end of the ISCC run.
- **D-21:** `/D` defines passed to ISCC: `MICMAP_VERSION=${PROJECT_VERSION}`, `STAGE_DIR=${CMAKE_BINARY_DIR}/stage`, `OUTPUT_DIR=${CMAKE_BINARY_DIR}/installer`. `app_key` ("bigscreen.micmap") is hardcoded inside the .iss to stay in lockstep with Phase 3 D-19 `configure_file` substitution.

### Claude's Discretion

- Exact AppId GUID value (stable forever per INST-01). Pick once, freeze in the .iss. Planner can generate via `uuidgen` at plan time.
- Exact wording of WMI-prompt dialog body, no-Steam abort message, uninstall data-retention prompt.
- Start Menu shortcut (yes/no), Desktop shortcut (probably no — auto-start makes it unnecessary).
- Installer icon + uninstall icon (reuse `apps/micmap/micmap.rc` icon or new `.ico` file in `installer/`).
- `[Files]` ordering, `Flags:` modifiers beyond `restartreplace` on DLLs.
- Refactor destination for `bindings_patcher.{hpp,cpp}` — `src/bindings/`, `src/common/`, or `src/steamvr/` are all defensible. Planner picks.
- Logger-injection shape for the patcher lift (function pointer, std::function, interface) — planner discretion.
- `ISCC_EXECUTABLE` cache-var handling if the auto-find misses (e.g., portable Inno install).
- Whether to gate the uninstall data-retention prompt behind `Setup.ShouldUninstallRunPrompt` vs. a wizard page — either works.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 4 requirements + roadmap

- `.planning/REQUIREMENTS.md` §"Installer (INST)" — INST-01 through INST-08 falsifiable acceptance criteria. Note: INST-06 is being retired (D-07); research spike for Pitfall 8 no longer required.
- `.planning/REQUIREMENTS.md` §"v2 Requirements > Distribution" — DIST-03 (non-default Steam path) is **pulled forward into v1** via D-02; update requirements.md footnote at phase close.
- `.planning/ROADMAP.md` §"Phase 4: Installer" — goal, dependencies, success criteria, research-spike notes (Pitfall 8 now voided per D-07; Pitfall 16 Pascal-Script half-day buffer retained).
- `.planning/PROJECT.md` §"Key Decisions" — Inno Setup installer pattern, MicMap-owns-own-driver-dir, single artifact covers driver + app + auto-start.

### Research

- `.planning/research/SUMMARY.md` §"Phase 4: Installer (INST-01, INST-02)" — implementation outline, delivered checklist.
- `.planning/research/SUMMARY.md` §"Critical Pitfalls" — Pitfall 3 (vrpathreg double-registration → removedriver before adddriver), Pitfall 4 (process-kill list + `restartreplace` defense-in-depth), Pitfall 10 (FileExists guard on vrpathreg.exe for defensive uninstall), Pitfall 15 (driver dir structure), Pitfall 16 (Pascal Script gotchas — `#N` preprocessor, `AnsiString` casts for file I/O, `IsProcessRunning` verbatim).
- `.planning/research/SUMMARY.md` §"Research flags" — Pitfall 8 voided per D-07.

### Prior phase context (decisions that flow into Phase 4)

- `.planning/phases/01-driver-sidecar-migration/01-CONTEXT.md` — Final driver file layout, bindings_patcher origin (`driver/src/bindings_patcher.{hpp,cpp}`), driver directory contents.
- `.planning/phases/01-driver-sidecar-migration/01-06-SUMMARY.md` — INST-08 requirement origin (bindings patch on Bigscreen Beyond / lighthouse-non-Index HMDs).
- `.planning/phases/02-config-read-back/02-CONTEXT.md` — Config schema stable; installer does NOT touch `%APPDATA%\MicMap\config.json` at install time.
- `.planning/phases/03-auto-start/03-CONTEXT.md` — `--register-vrmanifest` / `--unregister-vrmanifest` CLI contract (INST-04 target). `app.vrmanifest` built via `configure_file` at `apps/micmap/app.vrmanifest.in`. Exit-code contract 0/1 (D-03 from Phase 3) applies to `--patch-bindings` / `--unpatch-bindings` too.

### Codebase intel

- `.planning/codebase/ARCHITECTURE.md` — two-process model; installer packages both.
- `.planning/codebase/STRUCTURE.md` — directory layout (note: `scripts/install_driver.bat` etc. being deleted).
- `.planning/codebase/INTEGRATIONS.md` §"SteamVR driver installation via batch script" — pointer to the install paths Phase 4 replaces.
- `.planning/codebase/STACK.md` — confirms Inno Setup 6.7.1 locked for this milestone.
- `.planning/codebase/CONVENTIONS.md` — factory + interface pattern (logger injection for bindings_patcher lift).

### Existing code (integration points)

- `CMakeLists.txt:113-130` — `add_custom_target(copy_distributable_files ALL ...)` precedent; the new `package` target follows the same shape. This custom target itself will likely be deleted (D-18 replaces batch-script distribution).
- `CMakeLists.txt:143-151` — existing `install(TARGETS micmap ...)` + `install(FILES config/default_config.json ...)` rules. Staged destination for D-19.
- `driver/CMakeLists.txt:138-151` — existing `install(TARGETS driver_micmap ...)` + driver manifest / vrresources / settings install rules. Staged destination for D-19.
- `apps/micmap/CMakeLists.txt:22` — `set(MICMAP_VERSION ${PROJECT_VERSION})` — single source of truth for the version passed to `/DMICMAP_VERSION` in D-21.
- `apps/micmap/app.vrmanifest.in` — build-time-generated manifest; stage dir must include the generated (not the `.in`) file.
- `driver/src/bindings_patcher.{hpp,cpp}` — code to refactor per D-10. Current public surface: `PatchGenericHmdBindingsFile(configDir)`, `EnsureControllerTypeFiles(configDir, controllerType)`, `AtomicWriteJson(target, j)`. `ResolveSteamVrConfigDir()` (static) — lift alongside or expose publicly.
- `driver/src/bindings_patcher.cpp:198` — **write-once backup behavior** (authoritative reference for D-08).
- `driver/src/bindings_patcher.cpp:23-24` — marker keys `micmap_patched_v2` / `micmap_patched_v1` — used by D-11 unpatch to detect ownership.
- `scripts/install_driver.bat`, `scripts/uninstall_driver.bat`, `scripts/install_driver_test.bat`, `scripts/test_driver.bat` — **to be deleted in Phase 4**.
- `README.md:23,38` — references to the batch scripts; Phase 5 updates these.

### External references (not canonical, for context)

- `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` — Inno Setup reference pattern (admin + `Uninstallable`, WMI process list, `vrpathreg removedriver`/`adddriver`, Pascal Script idioms). **Path not resolvable from this shell session; planner/executor must Read directly at plan/exec time.** Differences from MicMap: MicMap does NOT nest under another vendor's driver, MicMap uses its own AppId, MicMap includes INST-08 bindings-patch step that bey-closer-t1 did not ship.
- `D:\Documents\Projects\bey-closer-t1\.planning\RETROSPECTIVE.md` — Inno Setup 6 Pascal Script gotchas corpus (Pitfall 16 source).
- Inno Setup documentation: [Pascal Scripting Introduction](https://jrsoftware.org/ishelp/index.php?topic=scriptintro), [WizardStyle](https://jrsoftware.org/ishelp/index.php?topic=setup_wizardstyle), [ArchitecturesAllowed](https://jrsoftware.org/ishelp/index.php?topic=setup_architecturesallowed).
- OpenVR issues: #1653 (vrpathreg duplicate entries → covered by INST-03), #1378 / #1547 (manifest registration — Phase 3 scope, not re-opened here).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **Existing `install(TARGETS …)` rules** (`CMakeLists.txt:143`, `driver/CMakeLists.txt:138`, `driver/CMakeLists.txt:143-151`) — reused verbatim by D-19's `cmake --install . --prefix build/stage` staging step. Single source of truth for what files ship.
- **`add_custom_target(copy_distributable_files ALL …)` at `CMakeLists.txt:113`** — template for D-18's `add_custom_target(package)` shape. After Phase 4, this existing target is likely deleted (D-18 replaces batch-script distribution).
- **`driver/src/bindings_patcher.{hpp,cpp}`** — proven C++ patcher with marker-key idempotency, atomic writes, write-once backup, and SteamVR config-dir resolution via `openvrpaths.vrpath`. D-09 calls this from the app side; D-10 lifts it to a shared library.
- **Phase 3 `manifest_registrar` + `--register-vrmanifest` / `--unregister-vrmanifest` CLI pattern** — direct template for the new `--patch-bindings` / `--unpatch-bindings` modes. Same `WinMain` CLI fork (Phase 3 D-02), same `VR_Init(VRApplication_Utility)` → work → `VR_Shutdown` → exit-code flow, same 0/1 exit-code contract.
- **`apps/micmap/app.vrmanifest.in` + existing `configure_file` rule** — already produces the final manifest with `@MICMAP_VERSION@` + `@MICMAP_APP_KEY@` substituted. Phase 4 installer picks it up from the stage dir.
- **`common::Logger` sink** — pre-existing logging used by CLI modes (Phase 3 D-04). Plugs into the patcher lift's injected logger slot.

### Established Patterns

- **CMake `install(TARGETS …)` + `install(FILES …)`** — canonical way to declare what ships. Phase 4 adds only what's missing (likely `install(FILES app.vrmanifest DESTINATION bin)`), reuses the rest.
- **Factory + interface pattern** (CONVENTIONS.md) — if bindings_patcher lift grows a C++ interface (e.g., `IBindingsPatcher`), it follows `createManifestRegistrar()` precedent.
- **CLI mode fork at `WinMain` top** (Phase 3 D-02) — early-exit before any GUI init; `--patch-bindings` / `--unpatch-bindings` slot in as sibling cases.
- **`%LOCALAPPDATA%\openvr\openvrpaths.vrpath` parse** (`driver/src/bindings_patcher.cpp:29`) — canonical way to discover SteamVR runtime from both driver and app side. D-12 reuses the same logic app-side after the lift.

### Integration Points

- **CMake root `CMakeLists.txt`** — new `find_program(ISCC_EXECUTABLE ISCC …)` + `add_custom_target(package)` + (if not present) `install(FILES app.vrmanifest DESTINATION bin)` for the staged manifest. Deletion of `add_custom_target(copy_distributable_files …)` once batch scripts are retired.
- **New `installer/MicMap.iss`** — Inno Setup 6.7.1 script. Pascal Script sections: `InitializeSetup` (abort if registry lookup fails per D-04), `PrepareToInstall` (WMI-check-and-retry-loop per D-05/D-06), `[Files]` (stage-dir-sourced with `restartreplace` on DLLs), `[Run]` (`micmap.exe --register-vrmanifest` per INST-04 + `micmap.exe --patch-bindings` per D-09), `[UninstallRun]` (symmetric `--unregister-vrmanifest` + `--unpatch-bindings`), `[Code]` procedure `UninstallPromptDataRetention` (D-13), `[Registry]` reads `HKCU\Software\Valve\Steam\SteamPath` (D-02).
- **`apps/micmap/main.cpp` `WinMain` CLI fork** (Phase 3 D-02 site) — extend the `wcscmp` loop from Phase 3 to also recognize `--patch-bindings` / `--unpatch-bindings`. Dispatch to the lifted patcher library.
- **Refactor sites for bindings_patcher** (D-10) — `driver/CMakeLists.txt` (remove `src/bindings_patcher.cpp` from driver sources, add link to the new shared lib), new `src/bindings/CMakeLists.txt` (or wherever the lift lands), `apps/micmap/CMakeLists.txt` (add link), `driver/src/device_provider.cpp:15` (update include path).
- **`scripts/` directory** — `install_driver.bat`, `uninstall_driver.bat`, `install_driver_test.bat`, `test_driver.bat` deleted in Phase 4. Confirm no CI / dev-doc still references them before deletion.
- **`README.md:23,38`** — references to batch scripts; Phase 5 (DOC-01) updates.

</code_context>

<specifics>
## Specific Ideas

- **Nested layout (D-01) + registry lookup (D-02) is a deliberate architectural push.** User chose the nested-under-SteamVR placement over the research-default standalone dir, and pulled DIST-03 forward from v2 into v1 scope. Consequence: Phase 4 installer cannot install on a machine where Steam exists but SteamVR is on a weird drive unless the registry value points correctly. The hard-abort fallback (D-04) owns this failure mode explicitly.
- **`micmap.exe --patch-bindings` (D-09) is the load-bearing reuse decision.** Reuses the already-proven C++ patcher instead of porting JSON logic to Pascal Script. Cost is the D-10 refactor lift (bindings_patcher out of `driver/src/` into a shared library). Benefit: single source of truth for patch logic; installer doesn't link nlohmann/json; driver-side fallback patcher stays identical.
- **INST-06 disposition (D-07) shrinks Phase 4 scope meaningfully.** No ghost-cleanup code, no `%LOCALAPPDATA%\openvr\input\` enumeration, no Pitfall 8 validation machine required. REQUIREMENTS.md edit at phase close.
- **Uninstall data-retention prompt default = No (D-13).** Rationale: training_data.bin = ~150 live mic samples; losing it is worse than leaving an orphan file. "No" preserves user work; power users who want a clean wipe can still explicitly opt in.
- **Write-once backup semantics (D-08) inherited from driver.** The intent is that `.micmap_backup` always reflects the pre-MicMap original, even after 10 reinstalls — never drifts. Code comment at the installer patch site should cite `driver/src/bindings_patcher.cpp:198` as the authoritative reference so the invariant survives future edits.
- **Single `install(TARGETS)` source of truth (D-19).** If the stage reveals a file Phase 4 needs but no install rule exists, fix the install rule (not the .iss `[Files]` block). Keeps local `cmake --install` dev workflow consistent with the packaged installer.
- **User intentionally skipped running Pitfall 8 spike (D-07) and signing (D-16) from v1.0 scope.** Both deferrals are explicit, not oversights.

</specifics>

<deferred>
## Deferred Ideas

- **Code-signing + signtool.exe plumbing** → v1.x. SmartScreen warning accepted for v1.0. Revisit when cert budget justifies.
- **LICENSE.md + installer LicenseFile= wiring** → future phase (candidate: Phase 5 Documentation or dedicated licensing micro-phase). Requires a license-choice decision (MIT / Apache-2.0 / proprietary / etc).
- **DIST-01 finished-page "Launch SteamVR now" checkbox** → v1.x (already deferred in REQUIREMENTS.md).
- **DIST-02 silent-install CLI flags documentation** → v1.x. Inno Setup natively supports `/SILENT`, `/VERYSILENT`, `/SUPPRESSMSGBOXES`; no installer code change, just README additions.
- **ARM64 Windows support** → not scoped. `ArchitecturesAllowed=x64` refuses ARM64 at install time per D-17.
- **Start Menu / Desktop shortcuts** → Claude's discretion (likely skip — auto-start makes them redundant).
- **Portable / no-install mode (zip distribution)** → not in scope. Milestone is about one-click installer, not distribution variety.
- **Per-user install (non-admin)** → not possible. INST-01 locks admin-elevated for `vrpathreg` + writes under `{SteamVR}\drivers\`.
- **Chocolatey / winget / scoop packaging** → v2+. Requires stable v1.0 installer to wrap first.
- **Auto-update / in-app updater** → v2+. Out of scope for single-click-installer milestone.
- **CPack integration** → rejected in D-18 (custom target simpler and more debuggable).

</deferred>

---

*Phase: 04-installer*
*Context gathered: 2026-04-23*

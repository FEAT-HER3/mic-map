---
phase: 4
slug: installer
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-23
---

# Phase 4 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake) + static lint (grep/PowerShell) + VM UAT (manual) |
| **Config file** | `CMakeLists.txt` (enable_testing already present at root); new `tests/installer/CMakeLists.txt` for unit-level coverage of lifted `bindings_patcher` |
| **Quick run command** | `cmake --build build --config Release --target bindings_patcher_tests && ctest --test-dir build -R bindings_patcher -C Release --output-on-failure` |
| **Full suite command** | `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure && cmake --build build --config Release --target package` |
| **Estimated runtime** | ~90 seconds (configure + build + ctest + ISCC package) |

---

## Sampling Rate

- **After every task commit:** Run quick command (unit tests for lifted patcher + static lint grep checks on `.iss`)
- **After every plan wave:** Run full suite command (build + ctest + package target produces `build/installer/MicMap-Setup-vX.Y.Z.exe`)
- **Before `/gsd-verify-work`:** Full suite green + VM UAT checklist executed (Success Criteria #1)
- **Max feedback latency:** 90 seconds for automated path; VM UAT is out-of-band (manual)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 04-01-01 | 01 | 0 | INST-08 | T-04-02 | Quoted paths, no arbitrary write outside `{SteamVR}\resources\config\` | infra | `test -d src/bindings && test -f src/bindings/CMakeLists.txt && test -f src/bindings/bindings_patcher.hpp && test -f src/bindings/bindings_patcher.cpp` | ❌ W0 | ⬜ pending |
| 04-01-02 | 01 | 0 | INST-08 | — | N/A | unit | `ctest --test-dir build -R bindings_patcher -C Release --output-on-failure` | ❌ W0 | ⬜ pending |
| 04-01-03 | 01 | 1 | INST-08 | T-04-02 | Marker-key idempotency preserved across lift | unit | `ctest --test-dir build -R bindings_patcher_idempotent -C Release` | ❌ W0 | ⬜ pending |
| 04-02-01 | 02 | 1 | INST-08 | T-04-02 | `--patch-bindings` exits 0/1, stdout free of secrets | cli | `build/Release/micmap.exe --patch-bindings; echo $?` | ❌ W0 | ⬜ pending |
| 04-02-02 | 02 | 1 | INST-08 | T-04-02 | `--unpatch-bindings` restores from `.micmap_backup` if present, else clears marker | cli | `build/Release/micmap.exe --unpatch-bindings; echo $?` | ❌ W0 | ⬜ pending |
| 04-03-01 | 03 | 2 | INST-07 | T-04-01 | `find_program(ISCC)` fails configure cleanly if missing (no silent fallback) | build | `cmake -B build && grep -q 'ISCC_EXECUTABLE' build/CMakeCache.txt` | ✅ | ⬜ pending |
| 04-03-02 | 03 | 2 | INST-07 | T-04-01 | `cmake --install` stage is deterministic (same files every run) | build | `cmake --install build --prefix build/stage --config Release && test -f build/stage/drivers/micmap/driver_micmap.dll && test -f build/stage/drivers/micmap/bin/micmap.exe && test -f build/stage/drivers/micmap/bin/app.vrmanifest` | ✅ | ⬜ pending |
| 04-03-03 | 03 | 2 | INST-07 | T-04-01 | `add_custom_target(package)` produces artifact at expected path | build | `cmake --build build --target package --config Release && test -f build/installer/MicMap-Setup-v*.exe` | ✅ | ⬜ pending |
| 04-04-01 | 04 | 3 | INST-01 | T-04-03 | `ArchitecturesAllowed=x64os` (NOT deprecated `x64`); no install on ARM64 | static | `grep -q 'ArchitecturesAllowed=x64os' installer/MicMap.iss && ! grep -qE 'ArchitecturesAllowed=x64($\|[^o])' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-04-02 | 04 | 3 | INST-01 | T-04-03 | `PrivilegesRequired=admin`, `Uninstallable=yes`, fixed AppId GUID | static | `grep -q 'PrivilegesRequired=admin' installer/MicMap.iss && grep -qE 'AppId=\{\{[A-F0-9-]+\}' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-04-03 | 04 | 3 | INST-01 | T-04-04 | All `[Files]` use `{#STAGE_DIR}\*` — zero repo-relative paths | static | `! grep -E 'Source:[^{]*\.\./' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-05-01 | 05 | 3 | INST-01, DIST-03 | T-04-05 | `RegQueryStringValue(HKCU, Software\Valve\Steam, SteamPath, ...)` present; abort on failure | static | `grep -q 'RegQueryStringValue' installer/MicMap.iss && grep -q 'InitializeSetup' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-05-02 | 05 | 3 | INST-01 | T-04-05 | No-Steam fallback is abort-with-MsgBox (no partial install) | static | `grep -q 'SteamVR not detected' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-06-01 | 06 | 3 | INST-02 | T-04-06 | WMI gate via `CreateOleObject('WbemScripting.SWbemLocator')` + prompt-retry loop in `PrepareToInstall` | static | `grep -q 'WbemScripting' installer/MicMap.iss && grep -q 'PrepareToInstall' installer/MicMap.iss && grep -qE '(vrserver\.exe\|vrmonitor\.exe\|vrcompositor\.exe\|vrdashboard\.exe\|vrwebhelper\.exe)' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-06-02 | 06 | 3 | INST-02 | T-04-06 | `restartreplace` flag on all `.dll` entries (defense-in-depth) | static | `awk '/^\[Files\]/,/^\[/' installer/MicMap.iss \| grep -E '\.dll.*Flags:' \| grep -vq 'restartreplace'; [ $? -eq 1 ]` | ❌ W0 | ⬜ pending |
| 04-07-01 | 07 | 3 | INST-03 | T-04-07 | `vrpathreg removedriver` runs BEFORE `adddriver` unconditionally; `FileExists` guard on `vrpathreg.exe` (Pitfall 10) | static | `grep -q 'vrpathreg.exe' installer/MicMap.iss && grep -q 'removedriver' installer/MicMap.iss && grep -q 'FileExists' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-07-02 | 07 | 3 | INST-04, INST-08 | T-04-04 | `--register-vrmanifest` + `--patch-bindings` invoked via `Exec()` in `CurStepChanged(ssPostInstall)` (NOT `[Run]` — `[Run]` does not block on non-zero exit per research §9) | static | `grep -q 'CurStepChanged' installer/MicMap.iss && grep -q 'ssPostInstall' installer/MicMap.iss && grep -qE 'Exec\\(.*register-vrmanifest' installer/MicMap.iss && grep -qE 'Exec\\(.*patch-bindings' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-07-03 | 07 | 3 | INST-05 | T-04-04 | Uninstall order: `--unpatch-bindings` → `--unregister-vrmanifest` → `vrpathreg removedriver` | static | `python -c "import re,sys;s=open('installer/MicMap.iss').read();o=[s.find(x) for x in ['unpatch-bindings','unregister-vrmanifest','removedriver']];sys.exit(0 if o==sorted(o) and all(x>=0 for x in o) else 1)"` | ❌ W0 | ⬜ pending |
| 04-08-01 | 08 | 3 | INST-05 | T-04-08 | Uninstall data-retention prompt default = **No** (preserve `%APPDATA%\MicMap\`) | static | `grep -q 'CurUninstallStepChanged' installer/MicMap.iss && grep -qE '(TaskDialogMsgBox\|MsgBox).*MB_YESNO' installer/MicMap.iss` | ❌ W0 | ⬜ pending |
| 04-09-01 | 09 | 4 | — | — | Removed batch scripts no longer referenced by build system or CI | infra | `! test -f scripts/install_driver.bat && ! test -f scripts/uninstall_driver.bat && ! grep -rE 'install_driver\.bat\|uninstall_driver\.bat' CMakeLists.txt driver/CMakeLists.txt .github 2>/dev/null` | ✅ | ⬜ pending |
| 04-09-02 | 09 | 4 | — | — | `copy_distributable_files` custom target deleted (duplicates `cmake --install` — Pitfall 17 per research) | infra | `! grep -q 'copy_distributable_files' CMakeLists.txt` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `src/bindings/CMakeLists.txt` — new shared library target (OBJECT or STATIC) lifting `bindings_patcher.{hpp,cpp}` with injected logger sink
- [ ] `src/bindings/bindings_patcher.hpp` + `.cpp` — moved from `driver/src/`, `DriverLog` replaced with function-pointer / `std::function` logger injection
- [ ] `tests/installer/CMakeLists.txt` + `tests/installer/test_bindings_patcher.cpp` — unit tests covering marker-key idempotency (REQ-INST-08), write-once backup semantics, atomic tmp-then-rename
- [ ] `installer/MicMap.iss` — new file (doesn't exist yet)
- [ ] `installer/micmap.ico` — installer icon (reuse `apps/micmap/micmap.rc` icon source)

*Existing infrastructure: `enable_testing()` already present at CMake root; GoogleTest or similar if already vendored — planner picks; otherwise single-exe `assert()` harness acceptable for Phase 4 scope.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Clean VM → install → SteamVR auto-detect → MicMap auto-launch → mic-cover triggers dashboard → uninstall leaves `vrpathreg show` + `openvrpaths.vrpath` clean | INST-01, INST-03, INST-05 (Success Criteria #1) | Requires fresh Win11 VM + HMD + trained mic model; cannot be automated | 1. Snapshot clean Win11 VM with Steam + SteamVR installed. 2. Run `MicMap-Setup-vX.Y.Z.exe` with admin. 3. Launch SteamVR; verify `vrpathreg show` lists `{SteamVR}\drivers\micmap`. 4. Open MicMap tray → run Calibration. 5. Cover mic in VR → dashboard toggles. 6. Uninstall via Add/Remove Programs. 7. `vrpathreg show` no longer lists MicMap; `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` free of MicMap entries; `{SteamVR}\drivers\micmap\` removed. |
| WMI running-SteamVR gate UX with named processes in dialog body | INST-02 | Requires live `vrserver.exe` / `vrmonitor.exe` to validate dialog text | 1. Launch SteamVR. 2. Double-click installer. 3. Verify dialog body includes the exact process names found (e.g., `vrserver.exe, vrmonitor.exe`). 4. Click Retry without closing SteamVR → same dialog. 5. Close SteamVR → Retry → installer proceeds. 6. Cancel at dialog → no partial install. |
| `restartreplace` defense-in-depth: user launches SteamVR between wizard accept and first file copy | INST-02 | Race window hard to script reliably | 1. Walk wizard to Install button. 2. Before clicking, launch SteamVR. 3. Click Install. 4. Verify `.dll` files queued with `restartreplace` → reboot prompt at end of install (not mid-install crash). |
| Upgrade-in-place 1.0 → 1.1 | INST-03 (same-version-reinstall + future upgrade) | Requires prior installed version | 1. Install v1.0. 2. Launch SteamVR; exit. 3. Install v1.1 over v1.0. 4. `vrpathreg show` lists MicMap exactly once. 5. `%APPDATA%\MicMap\config.json` unmodified (user data preserved). 6. No ghost driver entries. |
| Uninstall data-retention prompt — both branches (Yes / No) | INST-05 (D-13) | Requires user interaction with uninstall dialog | Run uninstaller twice: once with Yes (`%APPDATA%\MicMap\` removed recursively), once with No (directory intact for reinstall). |
| No-Steam abort UX | INST-01 (D-04) | Requires VM without Steam registry key | 1. VM without Steam installed (or delete `HKCU\Software\Valve\Steam\SteamPath`). 2. Run installer. 3. Verify MsgBox text matches D-04 wording. 4. Click OK → installer exits with no files written. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (`src/bindings/`, `installer/`, `tests/installer/`)
- [ ] No watch-mode flags (all commands terminate)
- [ ] Feedback latency < 90s (quick command) / < 5min (full suite)
- [ ] `nyquist_compliant: true` set in frontmatter after planner populates final task IDs
- [ ] Manual-only VM UAT checklist attached to `/gsd-verify-work` gate

**Approval:** pending

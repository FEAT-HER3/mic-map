---
phase: 10-cutover-cleanup
type: uat
created: 2026-05-10
tested: <PLACEHOLDER>
driver_sha: <PLACEHOLDER — populate via `git rev-parse --short HEAD` at UAT start>
operator: <PLACEHOLDER — name/email>
rig:
  hmd: Bigscreen Beyond
  os: Windows 11 Pro
  steamvr_version: <PLACEHOLDER — e.g. 2.5.x or 2.16.x>
  build_flavor: Debug + RelWithDebInfo (both required for D-25(8) and D-25(9))
status: in-progress
---

# Phase 10 — Cutover & Cleanup UAT

**Regimen** per `.planning/phases/10-cutover-cleanup/10-CONTEXT.md` D-25 (15 cases). All 15 must reach a PASS or N/A disposition before phase-complete; FAIL on any case blocks phase sign-off and may require returning to a prior plan.

This is the largest UAT in the milestone (P7=6 cases, P8=7, P9=10, P10=15) — coverage scope for the cutover demands it.

**Pre-UAT setup**:
- Build current HEAD: `cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
- Build a parallel Release config for D-25(8) `--debug-trigger` Release-build comparison: `cmake -B build-rel -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-rel`
- Build the installer: `cmake --build build --target package` (produces `MicMap-Setup-v1.6.0.exe`)
- Backup existing installs (Bigscreen Beyond rig convention):
  - `cp '/c/Program Files (x86)/Steam/steamapps/common/SteamVR/drivers/micmap/bin/win64/driver_micmap.dll' '/c/.../driver_micmap.dll.preuat.bak'`
  - `cp '/c/Program Files (x86)/Steam/steamapps/common/SteamVR/drivers/micmap/bin/micmap.exe' '/c/.../micmap.exe.preuat.bak'`
  - `cp "$APPDATA/MicMap/training_data.bin" "$APPDATA/MicMap/training_data.bin.preuat.bak"`
- Capture driver SHA: `git rev-parse --short HEAD` — record in frontmatter above.
- Verify SteamVR sees the driver after install: `vrpathreg show` lists MicMap entry.

**Post-Phase-10 default state note**: per CONTEXT D-25, P10 OWNS the flip — `enable_driver_audio: true` AND `enable_driver_detection: true` are the new shipped defaults. There is **no post-UAT default-OFF restore** (in contrast to P6/P7/P8/P9 D-40 discipline). Restore the `.preuat.bak` files only if a FAIL needs investigation.

**Pre-UAT hash record**:
```
sha256sum "$APPDATA/MicMap/training_data.bin" > training_data_pre_uat.sha256
```

---

## D-25(1) — Cutover smoke

**Spec:** Install fresh build, launch SteamVR, cover mic → dashboard toggles. Driver-side detection is the trigger path (verify ZERO HTTP `POST /button` traffic via Wireshark or a synthetic /button 404 check).

**Steps:**
1. Build the driver+client from current HEAD; install via `MicMap-Setup-v1.6.0.exe`.
2. Launch SteamVR; verify driver loads (`vrserver.txt` shows `driver_micmap` Init OK).
3. Cover the mic with hand; observe SteamVR dashboard toggles.
4. Run `curl -X POST http://127.0.0.1:27015/button` (synthetic) — expect HTTP 404 (route deleted in 10-05).
5. Re-cover mic to verify driver-resident detection still toggles.

**Expected:**
- Dashboard toggles via driver path (no client-side detection involvement)
- `curl -X POST .../button` returns 404 (route removed)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — vrserver.txt fragment, curl output, screencap of dashboard toggle>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(2) — Tray glyph state transitions

**Spec:** Observe armed-green at idle → triggered-pulse on cover-mic → back to armed-green within 300ms + cooldown. Inject error: kill SteamVR → tray turns red within 1 health-poll cycle (≤1s). Sub-test (Pitfall 1): kill explorer.exe via Task Manager, observe tray icon reappears (WM_TASKBAR_CREATED handler).

**Steps:**
1. Launch micmap.exe; observe tray icon = armed-green.
2. Cover mic; observe tray flashes triggered-pulse for ~300ms then reverts to armed.
3. Stop SteamVR via SteamVR menu; within ≤1s observe tray = error-red.
4. Restart SteamVR; observe tray returns to armed-green within ≤1s.
5. (Pitfall 1 sub-test) With micmap.exe running, kill explorer.exe via Task Manager; explorer auto-restarts; observe tray icon reappears (does NOT vanish permanently).
6. Open Process Explorer; observe GDI handle count for micmap.exe stays bounded across 10 state transitions (Pitfall 2 verification).

**Expected:**
- All 3 glyphs render correctly (armed-green / triggered-pulse / error-red)
- Pulse window ~300ms
- Error-state arrival within poll cycle (≤1s)
- Explorer-restart recovery via WM_TASKBAR_CREATED
- No GDI handle leak after 10 state transitions

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — Process Explorer GDI count screencap pre/post; tray screencaps for each state>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(3) — FAIL-01 mic-permission

**Spec:** Revoke mic permission in Win11 Privacy settings while client is running. Pill surfaces "Mic access blocked"; click button → Windows Settings opens at `ms-settings:privacy-microphone`. Re-grant → pill clears within one poll.

**Steps:**
1. Launch micmap.exe; verify driver-health pane shows no FAIL pill.
2. Open Win11 Settings → Privacy & Security → Microphone; toggle "Microphone access" off.
3. Within one poll cycle (~1s), observe pill "Mic access blocked" with "Open Windows mic settings" button.
4. Click the button; verify Windows Settings opens at the microphone privacy page.
5. Re-enable microphone access; within one poll cycle, pill clears.

**Expected:**
- Pill surfaces correctly with deep-link button
- `ms-settings:privacy-microphone` opens at the right page
- Auto-clear within one poll on re-grant

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — driver-health pane screencap with pill; Win11 Settings screencap>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(4) — FAIL-02 driver-missing

**Spec:** Stop SteamVR, rename `driver_micmap.dll`, restart SteamVR. Client pill: "Driver not installed". Tray red.

**Steps:**
1. Stop SteamVR.
2. Rename `<Steam>\steamapps\common\SteamVR\drivers\micmap\bin\win64\driver_micmap.dll` to `.bak`.
3. Restart SteamVR.
4. Launch micmap.exe (or observe if already running); within one poll cycle, observe pill "Driver not installed — run installer or enable in SteamVR" + "Open SteamVR" button; tray red.
5. Click "Open SteamVR" button; verify `steam://rungameid/250820` launches SteamVR client (or no-op if already up).
6. Restore the DLL; restart SteamVR; pill clears.

**Expected:**
- Pill text matches; deep-link launches SteamVR
- Tray red
- Pill clears on restore

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — pill screencap; tray screencap>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(5) — FAIL-03 SteamVR-not-running

**Spec:** Stop SteamVR while client is running. Pill: "SteamVR not running"; 1Hz poll cadence (verify via netstat / packet capture — no retry storm). Restart SteamVR → pill clears, tray green.

**Steps:**
1. Launch micmap.exe with SteamVR running; healthy state.
2. Stop SteamVR via SteamVR menu (NOT just close window — full quit so vrserver.exe exits).
3. Within 5s, observe pill "SteamVR not running"; tray red.
4. Open `netstat -an | findstr 27015` repeatedly over 30s; verify connection attempts to 127.0.0.1:27015 happen at ~1Hz cadence (NOT a flood).
5. Restart SteamVR; within ~2s, pill clears, tray green.

**Expected:**
- FAIL-03 disambiguation correct (SteamVR not running, NOT FAIL-02 driver-missing)
- 1Hz poll cadence (no retry storm)
- Auto-recovery on SteamVR restart

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — netstat output capture across 30s; pill screencap>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(6) — FAIL-04 double-instance

**Spec:** Launch micmap.exe twice. Second instance foregrounds the first and exits silently (verify via Task Manager — only one process). Window is restored from minimized if applicable.

**Steps:**
1. Launch micmap.exe via desktop shortcut or Start menu.
2. Minimize the window to tray.
3. Launch micmap.exe a second time the same way.
4. Observe: second instance does NOT open a new window; first instance restores from tray and comes to foreground.
5. Open Task Manager; verify only ONE micmap.exe process exists.

**Expected:**
- Single-instance enforcement via named mutex
- Window restore from minimized
- Silent second-instance exit (no error dialog)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — Task Manager screencap showing single process>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(7) — FAIL-05 device-removed

**Spec:** Unplug USB mic mid-session (or use Device Manager to disable). Pill: "Microphone disconnected"; tray red. Reconnect → driver IMMNotificationClient callback rebinds → pill auto-clears, tray green.

**Steps:**
1. With SteamVR running and driver-resident detection active, verify mic capture is working (cover mic → dashboard toggles).
2. Unplug USB mic (or Device Manager → Disable).
3. Within one poll cycle, observe pill "Microphone disconnected — reconnect to resume detection"; tray red.
4. Plug mic back in (or Enable in Device Manager).
5. Within ~2s (driver IMMNotificationClient rebind window), pill auto-clears; tray green.

**Expected:**
- Pill surfaces correctly within one poll
- Auto-recovery via IMMNotificationClient (no manual action required)
- Tray returns to armed-green

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — pill screencap; reconnect timing observation>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(8) — `--debug-trigger` end-to-end (Debug + Release builds)

**Spec:** Debug build `micmap.exe --debug-trigger` exits 0; HMD dashboard toggles. Release build: same command exits non-zero (route 404).

**Steps:**
1. Build a Debug `micmap.exe`; install over the existing one.
2. With SteamVR running, run `micmap.exe --debug-trigger` from a terminal; verify exit code 0 (`echo %ERRORLEVEL%` == 0); verify HMD dashboard toggles.
3. Build a Release (RelWithDebInfo) `micmap.exe`; install over the existing one.
4. Run `micmap.exe --debug-trigger`; verify exit code 1 (HTTP 404 — `/debug/trigger` route is `#if MICMAP_DEBUG_BUILD`-gated and not registered in Release).

**Expected:**
- Debug = exit 0 + dashboard toggle
- Release = exit 1 (route 404)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — terminal output for both builds with %ERRORLEVEL%; dashboard-toggle observation>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(9) — Log rotation

**Spec:** Synthesize 6MB of log writes (e.g., temporary debug-build with verbose logging). Verify rotation: `micmap-driver.log` < 5MB after rotation; `micmap-driver.log.{1..5}` exist; `.6` does not.

**Steps:**
1. Build a Debug driver with verbose logging enabled (e.g., set DETECTION_RMS_LOG / verbose flags).
2. Install + run; let detection run for ~10 minutes (or use --debug-trigger in a tight loop) to generate >5MB of log lines.
3. After log file size exceeds 5MB, observe rotation: `dir %APPDATA%\MicMap\micmap-driver.log*` shows .log + .log.1.
4. Continue logging until 5+ rotations have occurred.
5. Verify: `micmap-driver.log` < 5MB; `.log.1` through `.log.5` exist; `.log.6` does NOT exist (oldest dropped per D-14).
6. Verify no `*.tmp` files leftover.

**Expected:**
- 5MB cap honored (active log < 5MB after rotation)
- 5-generation cap enforced (.log.1..5 max; .log.6 absent)
- Atomic move (no .tmp leftovers)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — `dir` listing of `%APPDATA%\MicMap\micmap-driver.log*` post-rotation; size measurements>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(10) — Version mismatch warning

**Spec:** Install matched set (driver + client both `vX.Y.Z`). Replace one binary with `vX.Y.Z-prev` (manually). Restart client. WARNING in `micmap.log`; pill "Version mismatch" surfaces; detection still works.

**Steps:**
1. Install `MicMap-Setup-v1.6.0.exe` (matched set).
2. Build a v1.5.0 (or any prior) `micmap.exe`; manually copy over the installed `micmap.exe`.
3. Restart SteamVR (driver still v1.6.0); launch the v1.5.0 client.
4. Verify `micmap.log` contains a WARNING line: `driver version '1.6.0' does not match client '1.5.0'`.
5. Verify driver-health pane shows the version-mismatch pill (amber, "Version mismatch — driver v1.6.0 vs client v1.5.0 — reinstall recommended", with Dismiss button).
6. Cover mic; verify dashboard STILL toggles (D-20: warn-only, never blocks).
7. Click Dismiss; pill clears for the rest of the session.

**Expected:**
- WARNING line in micmap.log with both versions
- Amber pill visible with Dismiss button
- Detection still works (warn-only, never blocking)
- Dismiss clears pill for session

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — micmap.log WARNING line; pill screencap pre/post Dismiss>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(11) — Installer round-trip on clean Win11 VM

**Spec:** Install `vX.Y.Z` → upgrade to `vX.Y.Z+1` → uninstall. After uninstall, `vrpathreg show` does not list MicMap; `<Steam>/drivers/micmap` removed; `%APPDATA%/MicMap` left in place per CFG-04 user-data preservation.

**Steps:**
1. On a clean Win11 VM (no prior MicMap install): run `MicMap-Setup-v1.6.0.exe`. Verify install succeeds elevated; SteamVR registration via vrpathreg.
2. Verify the 3 tray .ico files are present at `<Steam>\steamapps\common\SteamVR\drivers\micmap\bin\resources\` (Pitfall 8 mitigation).
3. Build a `MicMap-Setup-v1.6.1.exe` (bump `cmake/version.cmake`); run upgrade; verify install succeeds atomically (Inno Setup [Files] all-or-nothing).
4. Run "Uninstall" via Windows Apps; verify `vrpathreg show` no longer lists MicMap; verify `<Steam>/drivers/micmap/` is removed.
5. Verify `%APPDATA%/MicMap/` is preserved (training_data.bin + config.json remain — CFG-04).

**Expected:**
- Atomic install/upgrade/uninstall on a clean VM
- 3 tray .ico files present in install dir post-install
- vrpathreg cleared on uninstall; install dir removed
- User data (`%APPDATA%/MicMap/`) preserved across uninstall

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — VM screencaps pre/post each step; vrpathreg show output before/after uninstall; %APPDATA% directory listing post-uninstall>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(12) — Binary size regression

**Spec:** `micmap.exe` size dropped noticeably vs pre-cutover commit. Document delta.

**Steps:**
1. Reference 10-05-SUMMARY for PRE-CUTOVER and POST-CUTOVER size measurements.
2. On the rig, build the current HEAD `micmap.exe` (Release config for fair comparison if 10-05 measured Debug, or Debug if 10-05 measured Debug — match).
3. Compare; document delta in this case row + cite 10-05-SUMMARY.
4. Per Pitfall 7: delta may be modest (~10-30KB) because KissFFT remains transitively linked. Acceptable per D-03 SOFT check.

**Expected:**
- Delta documented (size pre vs post)
- Pitfall 7 caveat acknowledged (KissFFT transitively linked → modest delta is acceptable)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — `dir` size of micmap.exe pre vs post; cite 10-05-SUMMARY measurements>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(13) — SVR-05 grep audit

**Spec:** `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost' driver/src/` returns hits ONLY in `device_provider.cpp` and `manifest_registrar.cpp`.

**Steps:**
1. Run the grep:
   ```bash
   grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost' driver/src/
   ```
2. Inspect output; verify ALL hits are in `device_provider.cpp` or `manifest_registrar.cpp`; NO other TU touches `vr::*`.
3. Specifically verify: `detection_runner.cpp`, `audio_worker.cpp`, `http_server.cpp`, `training_session.cpp`, `training_io.cpp`, `config_io.cpp`, `settings_validator.cpp`, `command_queue.hpp` are all clean.

**Expected:**
- Grep output confines vr::* references to `device_provider.cpp` + `manifest_registrar.cpp` only
- SVR-05 invariant intact post-cutover

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — grep output verbatim>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(14) — HMD sleep/wake stress

**Spec:** 50 cycles of HMD wake/sleep with detection running. No leaked handles in Process Explorer; vrserver.txt clean.

**Steps:**
1. With detection active (cover mic → dashboard toggles working), open Process Explorer → micmap.exe + vrserver.exe → record initial Handle count + GDI count.
2. Cycle HMD: take HMD off (proximity sensor triggers Standby) → wait 5s → put HMD on → wait 5s. Repeat 50 times.
3. After 50 cycles, observe Handle counts in Process Explorer; verify NO unbounded growth (small fluctuations OK; 1000+ handle increase = leak).
4. Inspect `vrserver.txt` for the test window; verify no error / leak warnings related to micmap.
5. Verify detection still works after the 50 cycles (cover mic → dashboard toggles).

**Expected:**
- Handle counts bounded (small fluctuations OK; 1000+ increase = leak)
- vrserver.txt clean (no micmap-related errors/leak warnings)
- Detection still functional post-50-cycles

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — Process Explorer Handle/GDI counts pre/post 50 cycles; relevant vrserver.txt fragment>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## D-25(15) — Cutover lint go-live verification

**Spec:** All 3 new lints fire on a synthetic regression (deliberately reintroduce a `POST /button` registration / a `detector->analyze()` in `apps/micmap/`); build fails. Revert; build passes.

**Steps:**
1. Synthetic regression A — re-add `POST /button` to `driver/src/http_server.cpp`:
   ```cpp
   srv.Post("/button", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });
   ```
   Run `ctest --test-dir build -R AssertNoButtonRoute --output-on-failure`. Expect FATAL.
2. Revert; verify ctest passes.
3. Synthetic regression B — re-add `detector->analyze()` in `apps/micmap/main.cpp` (anywhere; add a stub):
   ```cpp
   // synthetic
   void __reg() { INoiseDetector* d = nullptr; if (d) d->analyze(nullptr, 0); }
   ```
   Run `ctest --test-dir build -R AssertNoClientDetection --output-on-failure`. Expect FATAL.
4. Revert; verify ctest passes.
5. Synthetic regression C — corrupt `cmake/version.cmake` to set MICMAP_VERSION="1.7.0" but leave `installer/version.iss` showing "1.6.0":
   ```bash
   sed -i 's/MICMAP_VERSION "1.6.0"/MICMAP_VERSION "1.7.0"/' cmake/version.cmake
   cmake -B build -S .   # regenerates version.iss as 1.7.0 — this matches; force a stale state by hand-editing version.iss back to 1.6.0
   ```
   Run `ctest --test-dir build -R AssertCoVersioning --output-on-failure`. Expect FATAL (drift detected).
6. Revert all changes; verify all 3 lints PASS.

**Expected:**
- All 3 lints catch their respective regressions (FATAL on synthetic break)
- Revert clears (lints PASS again)

**Actual:** <PLACEHOLDER — operator fills>

**Evidence:** <PLACEHOLDER — ctest output for each lint pre/post synthetic break + revert>

**Disposition:** <PLACEHOLDER — PASS / FAIL / N/A with reason>

---

## Sign-off

**Operator:** _______________________
**Date:** _______________________
**Status:** All 15 cases PASS (or document FAIL/N/A reasoning above per case)

By signing, operator confirms Phase 10 cutover meets all 6 success criteria from ROADMAP §"Phase 10: Cutover & Cleanup".

**Post-UAT cleanup**: per CONTEXT D-25 — NO post-UAT default-OFF restore (P10 OWNS the flip; the on-rig install IS the new shipped default). The `.preuat.bak` files may be deleted on a clean PASS, or restored only if a FAIL needs investigation.

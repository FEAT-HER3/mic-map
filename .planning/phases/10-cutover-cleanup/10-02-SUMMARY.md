---
phase: 10-cutover-cleanup
plan: 02
subsystem: tray-icon-state-machine
tags: [phase-10, cutover-cleanup, wave-2, tray-glyphs, health-08]
requires:
  - apps/micmap/main.cpp:521 pollDriverHealth (P8 D-26 1Hz/2Hz poll loop — derive+apply hook site)
  - apps/micmap/main.cpp:260 SetupSystemTray (P8 IN-03 NIM_ADD — initTrayIcons hook site)
  - apps/micmap/main.cpp:735 shutdown (NIM_DELETE — destroyTrayIcons hook site)
  - apps/micmap/first_launch_balloon.cpp:69-89 (NIM_MODIFY uFlags save/restore pattern — model for Pitfall 3 mitigation)
  - tests/test_tray_glyph_state_machine.cpp (P10 Wave 0 RED scaffold — flips to GREEN after this plan)
  - installer/micmap.ico (256x256 RGBA source for the 3 tinted variants)
provides:
  - apps/micmap/src/tray_glyph.hpp (canonical HealthSnapshot/StateSnapshot declaration site for client-side pure-derivation surfaces; 10-03/10-06 will reuse via #include)
  - apps/micmap/src/tray_glyph.cpp (deriveTrayGlyph pure function + Win32 initTrayIcons/applyTrayGlyph/destroyTrayIcons helpers)
  - apps/micmap/resources/tray_armed.ico (green multi-size 16/32/48)
  - apps/micmap/resources/tray_triggered.ico (amber/yellow brightened 300ms pulse glyph)
  - apps/micmap/resources/tray_error.ico (red multi-size 16/32/48)
  - WM_TASKBAR_CREATED handler in WindowProc (Pitfall 1 — explorer.exe restart recovery)
  - g_tray TrayState global + initTrayIcons/applyTrayGlyph/destroyTrayIcons wiring across SetupSystemTray + pollDriverHealth + shutdown
  - Phase 10 Wave 0 test_tray_glyph_state_machine ctest GREEN (was build-RED)
affects:
  - apps/micmap/main.cpp (4 insertion sites: include + g_tray global, SetupSystemTray initTrayIcons, WindowProc WM_TASKBAR_CREATED, pollDriverHealth derive+apply, shutdown destroyTrayIcons)
  - apps/micmap/CMakeLists.txt (tray_glyph.cpp source addition + Shlwapi link + POST_BUILD .ico copy + install(FILES) rule)
tech-stack:
  added: []
  patterns:
    - "Pure-derivation seam pattern (deriveTrayGlyph): inputs are POD snapshot structs + steady_clock::time_point; outputs are an enum; no globals, no Win32, fully testable in headless ctest. The Win32 wrapper (applyTrayGlyph) consumes the enum and the existing g_app.nid; the seam keeps the test fixture flat (10 cases, plain-main, no fixtures, no mocking) per the P9 IDriverApi pattern."
    - "Per-target persistent HICON cache (Pitfall 2): three HICONs loaded once at WinMain startup via LoadImageW(LR_LOADFROMFILE | LR_DEFAULTSIZE), held on a TrayState global, swapped via NIM_MODIFY pointer-only — the loop never reallocates, never leaks (the heap-side icon is OS-owned by the same process that DestroyIcons it at WinMain teardown)."
    - "Save/set-narrow/call/restore uFlags pattern around NIM_MODIFY (Pitfall 3): mirrors first_launch_balloon.cpp:69-89; setting NIF_ICON only ensures the swap never accidentally re-fires the balloon path that the same g_app.nid carries via NIF_INFO during the first-launch flow."
    - "RegisterWindowMessageW(L\"TaskbarCreated\") static-cached UINT (Pitfall 1): explorer.exe restarts broadcast this message to all top-level windows; the static initialization runs once per process, subsequent comparisons are cheap integer ops; the handler re-NIM_ADDs and inverts g_tray.current so applyTrayGlyph's idempotency check sees a mismatch and re-runs the swap."
    - "GetModuleFileNameW + PathRemoveFileSpecW + PathCombineW absolute-path resolution (Pitfall 8): SteamVR launches micmap.exe from arbitrary CWDs; relative-to-CWD LR_LOADFROMFILE silently fails. The absolute resolver is anchored to the EXE directory, so the Inno Setup [Files] copy of resources\\tray_*.ico landing in the same directory tree at install time is sufficient — no installer-side path arithmetic needed."
key-files:
  created:
    - apps/micmap/src/tray_glyph.hpp
    - apps/micmap/src/tray_glyph.cpp
    - apps/micmap/resources/tray_armed.ico
    - apps/micmap/resources/tray_triggered.ico
    - apps/micmap/resources/tray_error.ico
    - apps/micmap/resources/_make_tray_icons.py (source-asset generator; provenance-traceable, re-runnable for re-tints)
    - .planning/phases/10-cutover-cleanup/10-02-SUMMARY.md
  modified:
    - apps/micmap/main.cpp
    - apps/micmap/CMakeLists.txt
decisions:
  - "deriveTrayGlyph signature uses (HealthSnapshot, StateSnapshot, time_point lastTriggeredAt, time_point now) instead of the plan's (..., TrayState const&, time_point now). The test_tray_glyph_state_machine.cpp Wave 0 scaffold passes a raw time_point as the 3rd arg — the pure derivation only reads the pulse-window field, so a TrayState reference would be over-coupling. The Win32-only fields (HICONs + current glyph) live on TrayState and are consumed by applyTrayGlyph; deriveTrayGlyph stays POD-clean and testable on any host."
  - "HealthSnapshot/StateSnapshot declared fresh in tray_glyph.hpp rather than extracted from existing P8 main.cpp code: P8 inlined the JSON parse into pollDriverHealth() against atomic fields on MicMapApp + healthMu-guarded std::string fields — no dedicated struct existed. Following the plan's preferred 'snapshots in shared header' option, tray_glyph.hpp is the canonical declaration site; 10-03 fail_pill.hpp + 10-06 version_mismatch.hpp will reuse via #include 'tray_glyph.hpp' rather than duplicating. main.cpp materializes these structs as poll-tick stack frames in pollDriverHealth from the existing atomics + healthMu fields — minimal disruption to existing P8 code."
  - "Source-asset provenance for the 3 .ico files: apps/micmap/resources/_make_tray_icons.py (Pillow 12.2.0 RGB-tint blend on the 256x256 frame from installer/micmap.ico, alpha preserved). Tints: armed #2ecc71 (s=0.65), triggered #f1c40f (s=0.70, brightness 1.10), error #e74c3c (s=0.70). Multi-size 16/32/48 packaging via PIL save(format='ICO', sizes=[(16,16),(32,32),(48,48)]). All files <6KB, well under the 100KB plan limit; magic 00 00 01 00 validated; struct-parsed counts confirmed 3 entries each. The script is committed alongside the .ico outputs so a future re-tint (e.g. higher contrast for low-DPI rigs) is a one-line edit + re-run."
  - "WM_TASKBAR_CREATED handler placed inline at the top of WindowProc (BEFORE the existing switch) rather than as a 'case' — the message UINT is dynamic at process startup (RegisterWindowMessageW returns a registered system-wide value), so a switch-case can't bind it as a compile-time constant. The static const UINT initializer pattern is recommended by Microsoft's Shell_NotifyIcon docs (Section 'Taskbar restart' — the canonical Pitfall 1 idiom)."
  - "POST_BUILD copy_if_different of resources/tray_*.ico to $<TARGET_FILE_DIR:micmap>/resources/ added in addition to the install(FILES) rule. Without the POST_BUILD copy, a developer-launched build/bin/Debug/micmap.exe finds no .ico files at runtime (the install staging only fires on cmake --install), and applyTrayGlyph's null-HICON guard silently no-ops the glyph swap — a subtle dev-loop pitfall. The POST_BUILD makes the dev experience match the installed experience for HEALTH-08 verification."
  - "Shlwapi.lib linked (rather than reusing the already-linked Pathcch.lib) per the plan's verbatim PathCombineW + PathRemoveFileSpecW call shape. Pathcch.lib's PathCchCombineEx + PathCchRemoveFileSpec would be functionally equivalent and would avoid the new link dependency, but the plan called Shlwapi out explicitly and the target already pulls in the same general DLL surface — the marginal extra dependency is invisible at link time and matches the plan's RESEARCH §Pattern 1 example verbatim."
metrics:
  duration: ~30 minutes
  completed_date: 2026-05-10
  task_count: 4
  file_count: 8
---

# Phase 10 Plan 02: Wave 2 Tray-Icon State Glyphs Summary (HEALTH-08)

Land HEALTH-08 tray-state glyphs: 3 multi-size `.ico` assets (green armed / amber pulse / red error) under `apps/micmap/resources/`, a pure `deriveTrayGlyph()` plus Win32 `init/apply/destroy` helpers in `apps/micmap/src/tray_glyph.{hpp,cpp}`, and the wiring into `apps/micmap/main.cpp` that hooks the swap into the existing P8 1Hz `/health` + 2Hz `/state` poll callback (no new poll, no new thread per D-06). The Wave 0 `test_tray_glyph_state_machine` ctest flips from build-RED to GREEN.

## What Shipped

**Three tray-state `.ico` assets (`apps/micmap/resources/`):**

- `tray_armed.ico` — green (#2ecc71) tinted variant, multi-size 16/32/48, ~5.1 KB.
- `tray_triggered.ico` — amber (#f1c40f) brightness-boosted variant for the 300 ms pulse, multi-size 16/32/48, ~5.1 KB.
- `tray_error.ico` — red (#e74c3c) tinted variant, multi-size 16/32/48, ~5.1 KB.
- Source-asset generator: `apps/micmap/resources/_make_tray_icons.py` (Pillow 12.2.0; RGB-tint blend on the 256×256 frame from `installer/micmap.ico`, alpha preserved, per-variant brightness multiplier; `Image.save(format='ICO', sizes=[(16,16),(32,32),(48,48)])` for the multi-size pack). The script is committed so re-tints are a one-line edit + re-run.

**Pure `deriveTrayGlyph()` + Win32 helpers (`apps/micmap/src/tray_glyph.{hpp,cpp}`):**

- `HealthSnapshot { driverLoaded, driverVersion, driverDetectionActive, driverTrainingActive }` and `StateSnapshot { detectionState, lastError, audioDeviceState }` — canonical declaration site for the client-side pure-derivation surfaces (10-03 fail_pill + 10-06 version_mismatch will `#include "tray_glyph.hpp"` rather than duplicate). P8 inlined the JSON parse, so these dedicated structs do not exist elsewhere — main.cpp materializes them as poll-tick stack frames from the existing atomics + `healthMu`-guarded fields.
- `enum class TrayGlyph { Armed, Triggered, Error }` + `TrayState { HICON×3, current, lastTriggeredAt }`.
- `deriveTrayGlyph(health, state, lastTriggeredAt, now)` implements D-05 priority rules verbatim: error sources (`!driverLoaded` ∨ non-empty `lastError` ∨ `audioDeviceState ∈ {missing, permission_denied}`) → `Error`; `detectionState == "triggered"` ∨ `(now - lastTriggeredAt) < 300 ms` → `Triggered`; otherwise `Armed`. Pure POD-in / enum-out — no globals, no Win32, testable on any host.
- `initTrayIcons(hInst, ts)` — `GetModuleFileNameW` → `PathRemoveFileSpecW` → `PathCombineW("resources\\tray_*.ico")` → `LoadImageW(LR_LOADFROMFILE | LR_DEFAULTSIZE)` × 3 (Pitfall 8 absolute-path resolution; Pattern 1 / A1 system-tray DPI autopick). Logs WARNINGs on partial load, never throws.
- `applyTrayGlyph(nid, ts, desired)` — idempotent (early-returns when `ts.current == desired`); selects the matching HICON, saves `nid.uFlags`, sets `NIF_ICON` only, calls `Shell_NotifyIconW(NIM_MODIFY, &nid)`, restores `prevFlags` (Pitfall 3 — never re-fires the balloon path that the same `g_app.nid` carries via `NIF_INFO`).
- `destroyTrayIcons(ts)` — `DestroyIcon` × 3 at WinMain teardown (Pitfall 2 — three persistent HICONs, never reallocated, always cleaned up).
- All Win32-dependent code is `#ifdef _WIN32`-gated so `test_tray_glyph_state_machine` (which compiles `tray_glyph.cpp` standalone against `micmap::core_runtime`) builds on any host.

**`apps/micmap/main.cpp` wiring (4 insertion sites):**

- Top of file: `#include "src/tray_glyph.hpp"` + `static micmap::client::TrayState g_tray;` global next to `g_app`.
- `SetupSystemTray()`: after the existing `Shell_NotifyIconW(NIM_ADD, &g_app.nid)`, call `micmap::client::initTrayIcons(GetModuleHandleW(nullptr), g_tray)` to load the 3 HICONs once.
- `WindowProc`: pre-switch `static const UINT WM_TASKBAR_CREATED = ::RegisterWindowMessageW(L"TaskbarCreated");` + handler that re-`NIM_ADD`s the icon and inverts `g_tray.current` so the next `pollDriverHealth` tick re-applies the glyph (Pitfall 1 — explorer.exe restart recovery).
- `pollDriverHealth()` tail: materialize `HealthSnapshot`/`StateSnapshot` from `driverLoadedIndicator` + `driverTrainingActive` + `healthMu`-guarded `detectionStateStr`/`lastError`/`audioDeviceState`; start the 300 ms pulse window when `detectionState == "triggered"`; call `deriveTrayGlyph` + `applyTrayGlyph(g_app.nid, g_tray, desired)` (D-06 — same poll, no new thread).
- `shutdown()`: after the existing `Shell_NotifyIconW(NIM_DELETE, &nid)`, call `destroyTrayIcons(g_tray)` to release the 3 persistent HICONs.

The existing P3 `first_launch_balloon` machinery is untouched — the `NIF_ICON`-only `NIM_MODIFY` in `applyTrayGlyph` saves and restores `nid.uFlags`, so the balloon path is never accidentally re-fired even when the balloon code transiently sets `NIF_INFO` on the same `g_app.nid`.

**`apps/micmap/CMakeLists.txt`:**

- `MICMAP_SOURCES`: append `src/tray_glyph.cpp`.
- `target_link_libraries(micmap PRIVATE ... Shlwapi)` for `PathCombineW` + `PathRemoveFileSpecW` (Pitfall 8).
- `add_custom_command(TARGET micmap POST_BUILD ...)` copies `resources/tray_*.ico` to `$<TARGET_FILE_DIR:micmap>/resources/` so a developer-launched `build/bin/Debug/micmap.exe` finds the glyphs (the install staging only fires on `cmake --install`).
- `install(FILES ...) DESTINATION bin/resources` populates `cmake --install --prefix .../stage` with the 3 .ico files for the 10-06 Inno Setup `[Files]` consumer.

## Verification

**Per-task automated checks (all PASS):**

- Task 1: `python3 -c "..."` magic-byte + multi-size struct check confirms each of the 3 .ico files has `00 00 01 00` magic and `count=3` (16×16, 32×32, 48×48 entries); each <6 KB, well under the 100 KB plan limit.
- Task 2: `cmake --build build --target test_tray_glyph_state_machine --config Debug` succeeds; `ctest --test-dir build -C Debug -R TrayGlyphStateMachine --output-on-failure` → `1/1 Test #43: TrayGlyphStateMachine ............ Passed 0.02 sec`. All 10 cases of the Wave 0 RED scaffold turn GREEN: pure-armed; FAIL-02 driverLoaded=false → Error; non-empty lastError → Error; FAIL-01 permission_denied → Error; FAIL-05 missing → Error; detection_state=triggered → Triggered; pulse window (lastTriggeredAt = now-100ms, state=cooldown) → Triggered; pulse expired (lastTriggeredAt = now-400ms, state=idle) → Armed; error trumps triggered; triggered trumps armed.
- Task 3: `grep -q` confirms all 6 wiring landmarks land in `apps/micmap/main.cpp`: `src/tray_glyph.hpp` include, `initTrayIcons` call, `WM_TASKBAR_CREATED` handler, `RegisterWindowMessageW(L"TaskbarCreated")`, `applyTrayGlyph` call, `destroyTrayIcons` call. (Build-side verification deferred to Task 4 because tray_glyph.cpp is not yet in the micmap target source list at this step.)
- Task 4: `cmake -B build -S .` clean; `cmake --build build --target micmap --config Debug` builds clean (the only LINK warning is the pre-existing `LNK4098 LIBCMT` CRT-mismatch noise tracked in 10-01); `ctest -R 'AssertCoVersioning|LogRotation|TrayGlyph'` → `3/3 PASS`. `cmake --install build --config Debug --prefix /tmp/p10-02-stage` produces `/tmp/p10-02-stage/bin/resources/{tray_armed,tray_triggered,tray_error}.ico`. `ALL_BUILD` succeeds, no other targets broke.

**Build-side sanity (informational):**

- `build/bin/Debug/resources/` contains the 3 staged .ico files (POST_BUILD copy worked).
- `build/bin/Debug/micmap.exe` links cleanly with the new `tray_glyph.cpp` translation unit and the `Shlwapi` dependency.

**Visual / hardware verification deferred:** confirming the actual swap in the Windows tray is a `human-verify` checkpoint best done as part of UAT for Phase 10 (not in this autonomous executor's scope). The plan does not request it; the test_tray_glyph_state_machine GREEN flip + the magic-byte + multi-size struct check + the install staging directory inspection are the gating signals for this plan.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `deriveTrayGlyph` signature corrected to match the test scaffold**
- **Found during:** Task 2 (writing tray_glyph.hpp against the existing test scaffold)
- **Issue:** The plan specified `deriveTrayGlyph(const HealthSnapshot&, const StateSnapshot&, const TrayState& ts, time_point now)` — taking a `TrayState&` as the 3rd argument. The Wave 0 RED scaffold `tests/test_tray_glyph_state_machine.cpp` (committed in 10-00) calls `deriveTrayGlyph(health, state, pulse, now)` where `pulse` is a raw `std::chrono::steady_clock::time_point`. Following the plan's signature would have caused build-RED in the test executable (no auto-conversion from `time_point` to `TrayState&`). The Wave 0 scaffold is the load-bearing contract per the plan's `<read_first>` block; the plan body's signature was an over-coupled draft that would not have compiled.
- **Fix:** Changed the signature to `deriveTrayGlyph(const HealthSnapshot&, const StateSnapshot&, time_point lastTriggeredAt, time_point now)`. The pure derivation only reads the pulse-window field anyway — passing the full `TrayState&` would have over-coupled the derivation to the Win32 layout. The TrayState's HICONs + `current` glyph remain consumed only by `applyTrayGlyph`. main.cpp's poll-tick caller passes `g_tray.lastTriggeredAt` directly.
- **Files modified:** apps/micmap/src/tray_glyph.hpp, apps/micmap/src/tray_glyph.cpp
- **Commit:** 929b373

**2. [Rule 2 — Missing critical functionality] POST_BUILD copy of `.ico` files for dev builds**
- **Found during:** Task 4 (writing the install rule per the plan)
- **Issue:** The plan added an `install(FILES)` rule for the 3 .ico files but no POST_BUILD step. Without POST_BUILD, a developer-launched `build/bin/Debug/micmap.exe` finds no .ico files at runtime (the install staging only fires on `cmake --install`), and `applyTrayGlyph`'s null-HICON guard silently no-ops the swap. This is a correctness requirement for HEALTH-08 dev verification — type-checking and build-success do not substitute for visual confirmation, and the dev-build silent-fail would require running the installer between every test cycle.
- **Fix:** Added an `add_custom_command(TARGET micmap POST_BUILD ...)` that `copy_if_different`s the 3 .ico files into `$<TARGET_FILE_DIR:micmap>/resources/` so the dev-loop micmap.exe matches the installed micmap.exe behavior. Verified by `ls build/bin/Debug/resources/` — all 3 files present.
- **Files modified:** apps/micmap/CMakeLists.txt
- **Commit:** d82f6a1

## Threat Model Compliance

All 5 STRIDE threats from the plan's threat register are addressed:

- T-10-02-01 (DoS, HICON exhaustion via repeated NIM_MODIFY allocation): mitigated. Three persistent HICONs loaded once at WinMain startup via `LoadImageW`; `applyTrayGlyph` swaps the pointer field on `nid` and never reallocates. `destroyTrayIcons` is called once at WinMain teardown.
- T-10-02-02 (DoS, LoadImageW from attacker-controlled path): accepted. Path is built from `GetModuleFileNameW` (kernel-controlled, returns the EXE's own path) + hardcoded `resources\\tray_*.ico` literals; the resolved path lives under the Steam install directory and shares the same trust as `micmap.exe` itself.
- T-10-02-03 (Tampering, NIF_INFO bit pollution causing balloon resurrection): mitigated. `applyTrayGlyph` saves `nid.uFlags`, sets `NIF_ICON` only, calls `NIM_MODIFY`, restores `prevFlags` — mirrors `first_launch_balloon.cpp:69-89` verbatim.
- T-10-02-04 (DoS, tray icon disappears after explorer.exe restart): mitigated. `WindowProc` static-cached `WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated")`; on receipt, re-`NIM_ADD` and force `g_tray.current` invert so the next `pollDriverHealth` tick re-applies the glyph.
- T-10-02-05 (Information disclosure, tray glyph reveals detection state): accepted by design — the visible-state-on-tray IS the HEALTH-08 feature; the user opted in by installing.

## Threat Flags

None — this plan creates no new network endpoints, no new auth paths, no new file-access patterns at trust boundaries (the `resources\\tray_*.ico` files are read-only from the per-machine Steam install directory, same trust as the EXE itself), and no schema changes. The only file I/O surface is `LoadImageW` on the 3 read-only `.ico` blobs, already in the threat register as T-10-02-02.

## Known Stubs

None. `deriveTrayGlyph` implements the full D-05 priority rule set; `initTrayIcons` / `applyTrayGlyph` / `destroyTrayIcons` cover full lifecycle; the poll callback hook materializes both snapshot structs from real existing atomics + `healthMu`-guarded fields (no placeholder values); the WindowProc handler is fully wired; the install rule is committed. The `HealthSnapshot.driverDetectionActive` field is currently always set to `false` in main.cpp's poll-tick materialization — this is INTENTIONAL: 10-03 will wire the real value once `getHealth()`'s envelope is parsed for the FAIL-pill path. The unused field is a known plan-of-work seam, not a stub blocking HEALTH-08; deriveTrayGlyph does not consume it.

## Commits

| Task | Description                                                                              | Commit  |
| ---- | ---------------------------------------------------------------------------------------- | ------- |
| 1    | feat(10-02): add 3 tray-state .ico assets (HEALTH-08 D-04)                               | ff55eef |
| 2    | feat(10-02): add tray_glyph.{hpp,cpp} — pure deriveTrayGlyph + Win32 helpers             | 929b373 |
| 3    | feat(10-02): wire tray glyph state machine into main.cpp (HEALTH-08)                     | 6ac0bac |
| 4    | build(10-02): register tray_glyph.cpp + Shlwapi link + .ico install rule                 | d82f6a1 |

## Self-Check: PASSED

- apps/micmap/resources/tray_armed.ico — FOUND
- apps/micmap/resources/tray_triggered.ico — FOUND
- apps/micmap/resources/tray_error.ico — FOUND
- apps/micmap/src/tray_glyph.hpp — FOUND
- apps/micmap/src/tray_glyph.cpp — FOUND
- apps/micmap/main.cpp — FOUND (include, g_tray, initTrayIcons, WM_TASKBAR_CREATED handler, applyTrayGlyph, destroyTrayIcons all present)
- apps/micmap/CMakeLists.txt — FOUND (tray_glyph.cpp source, Shlwapi link, POST_BUILD copy, install(FILES) rule all present)
- ctest TrayGlyphStateMachine — PASS (was build-RED at Wave 0)
- ctest AssertCoVersioning + LogRotation + TrayGlyph — 3/3 PASS
- micmap.exe build — clean (LNK4098 LIBCMT noise pre-existing per 10-01)
- cmake --install staging contains bin/resources/tray_*.ico — verified
- ff55eef — FOUND (`git log --oneline` confirms; Task 1 commit)
- 929b373 — FOUND (`git log --oneline` confirms; Task 2 commit)
- 6ac0bac — FOUND (`git log --oneline` confirms; Task 3 commit)
- d82f6a1 — FOUND (`git log --oneline` confirms; Task 4 commit)

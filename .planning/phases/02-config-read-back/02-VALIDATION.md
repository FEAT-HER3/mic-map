---
phase: 2
slug: config-read-back
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-22
revised: 2026-04-23
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest standalone (C++ `main()` returning int) — matches existing `tests/test_placeholder.cpp` pattern |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build -R test_config_manager --output-on-failure -C Debug` |
| **Full suite command** | `ctest --test-dir build --output-on-failure -C Debug` |
| **Estimated runtime — test execution** | ~1 second (test binary itself, post-build) |
| **Estimated runtime — incremental rebuild + test** | < 5 seconds (assumes warm `build/` tree from Plan 01 Task 3 — first cold configure adds 30-60s for CMake configure step) |
| **Estimated runtime — full audit (Plan 03 Task 1)** | 30-90 seconds intentionally (one-shot full Debug build + ctest at end of phase; not a per-task latency target) |

---

## Sampling Rate

- **After every task commit (Plan 02 incremental work):** Run `ctest --test-dir build -R test_config_manager --output-on-failure -C Debug` — incremental compile of `config_manager.cpp` + relink + test exec, < 5s on a warm build tree.
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure -C Debug` (full suite — placeholder + config_manager).
- **Before `/gsd-verify-work`:** Full suite must be green + M-1 manual check.
- **Max per-task feedback latency:** < 5 seconds for Plan 02's incremental rebuild loop. Plan 01 Task 3 and Plan 03 Task 1 are one-shot full-build tasks (RED-confirm and end-of-phase audit respectively); their longer build latency is by design and does not bind the per-task promise.

**Latency dependency on warm build tree:** The < 5s expectation assumes `build/` exists and the previous task's incremental build artifacts are present (CMake's dependency cache + object files for unchanged TUs). Plan 01 Task 3 establishes this warm tree by running the first `cmake --build build --target test_config_manager` invocation, optionally configuring `cmake -S . -B build -G "Visual Studio 17 2022"` if `build/` is absent. Plan 02's two tasks then enjoy incremental-only rebuild costs (only `config_manager.cpp` + `test_config_manager.cpp` change between commits).

**Nyquist rationale:** Phase observable surface is "settings persist" + "corruption is survived". Round-trip identity (T-1) samples every field. Corruption backup (T-2) samples the failure mode the phase exists to prevent. Clamp (T-3) + first-run (T-4) + retention (T-5) cover the remaining acceptance bands. Together these five scenarios form a sampling rate strictly above the phase's feature frequency.

---

## Per-Task Verification Map

*Populated 2026-04-23 from Plans 01-03. Manual checkpoint Task 2 of Plan 03 is excluded (verified by user resume-signal, not automated).*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 2-01-T1 | 01 | 1 | CFG-01..CFG-05 | T-2-01-04 | nlohmann_json target wired PRIVATE into `micmap_core` (no namespaced-alias typo) | unit / file shape | `grep -n "nlohmann_json" src/core/CMakeLists.txt && grep -n "shell32" src/core/CMakeLists.txt && grep -n "ALIAS micmap_core" src/core/CMakeLists.txt` | ✅ existing file modified | ⬜ pending |
| 2-01-T2 | 01 | 1 | CFG-01..CFG-05 | T-2-01-01, T-2-01-02 | Test binary exists with 5 RED scenarios + temp-dir isolation; user's real config is unreachable | unit / file shape | `test -f tests/test_config_manager.cpp && grep -c "MM_CHECK" tests/test_config_manager.cpp` | ❌ Wave 0 — created here | ⬜ pending |
| 2-01-T3 | 01 | 1 | CFG-01..CFG-05 | T-2-01-04 | CTest registration links `micmap::core` PRIVATE; placeholder canary preserved; RED confirmed | integration (RED build) | `grep -n "add_test(NAME test_config_manager" tests/CMakeLists.txt && grep -n "target_link_libraries(test_config_manager PRIVATE micmap::core)" tests/CMakeLists.txt && grep -n "add_test(NAME test_placeholder" tests/CMakeLists.txt` (plus `cmake --build build --target test_config_manager` + `ctest --test-dir build -R test_config_manager --output-on-failure -C Debug` — must FAIL/non-zero, RED) | ✅ existing file modified | ⬜ pending |
| 2-02-T1 | 02 | 2 | CFG-01..CFG-05 | T-2-02-01..09 | nlohmann/json infrastructure compiles; old hand-rolled `JsonValue`/`toJson` removed; `save()` body stubbed to `return false` (Warning-4 fix — interrupted-state safety) | unit / build | `cmake --build build --target micmap_core 2>&1 \| tail -40` (must exit 0; build green) | ✅ existing file modified | ⬜ pending |
| 2-02-T2 | 02 | 2 | CFG-01..CFG-05 | T-2-02-01..09 | `load()` defensive parse + `save()` atomic write — all 5 RED scenarios flip to GREEN | integration (GREEN test pass) | `cmake --build build --target test_config_manager 2>&1 \| tail -10 && ctest --test-dir build -R test_config_manager --output-on-failure -C Debug 2>&1 \| tail -10` (must exit 0 and stdout contains `all tests passed`) | ✅ existing file modified | ⬜ pending |
| 2-03-T1 | 03 | 3 | CFG-01..CFG-05 | T-2-03-03 | Full-suite green; zero warnings on touched files; canary `test_placeholder` still passes | integration (full suite) | `cmake --build build --config Debug 2>&1 \| tee /tmp/phase2-build.log \| tail -10 && ctest --test-dir build --output-on-failure -C Debug 2>&1 \| tail -10` (one-shot full Debug build + full suite — intentionally 30-90s, end-of-phase audit) | ✅ existing files unchanged | ⬜ pending |
| 2-03-T2 | 03 | 3 | CFG-01, CFG-05 | T-2-03-01, T-2-03-02 | Live UI → quit → relaunch round-trip validates wiring at `apps/micmap/main.cpp:172,337` | manual (checkpoint:human-verify) | EXCLUDED — verified by user resume-signal `M-1 PASSED: ...` captured in transcript and Plan 03 SUMMARY | n/a | ⬜ pending (manual gate) |
| 2-03-T3 | 03 | 3 | CFG-01..CFG-05 | T-2-03-04 | Phase close artifact: SUMMARY records audit + manual verdict; VALIDATION sign-off flipped | unit / file shape | `test -f .planning/phases/02-config-read-back/02-03-SUMMARY.md && grep -E "M-1 (PASSED\|FAILED)" .planning/phases/02-config-read-back/02-03-SUMMARY.md && grep -c "nyquist_compliant: true" .planning/phases/02-config-read-back/02-VALIDATION.md` | ❌ Wave 3 — created here | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_config_manager.cpp` — covers CFG-01, CFG-02, CFG-03, CFG-04, CFG-05 (all five requirements) via scenarios T-1 round-trip, T-2 corruption backup, T-3 clamp, T-4 first-run, T-5 retention
- [ ] `tests/CMakeLists.txt` — add `add_executable(test_config_manager ...)` + `add_test(NAME test_config_manager COMMAND ...)` registration, link `micmap_core` + `nlohmann_json`
- [ ] No framework install needed (CTest is built into CMake)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions | Result |
|----------|-------------|------------|-------------------|--------|
| End-to-end user cycle: change a setting in the MicMap UI → quit → relaunch → setting preserved | CFG-01, CFG-05 (success criterion #1) | Requires live GUI interaction + real `%APPDATA%` path + real SteamVR/WASAPI context; cannot be scripted cleanly | 1. Launch `mic_map.exe`. 2. Change audio device, sensitivity slider, detection duration, SteamVR dashboard toggle. 3. Quit via normal shutdown path (graceful — saveDefault must run). 4. Relaunch. 5. Assert the changed values are displayed in the UI (not defaults). 6. Confirm no `config.json.tmp` left behind. See Plan 03 Task 2 for full step-by-step. | **DEFERRED** — `micmap.exe` hangs with all-white frozen window on launch (Debug + Release, fresh and stored `%APPDATA%`). `mic_test.exe` launches normally, so WASAPI path is fine. Hang is in micmap.exe VR/UI init — Phase 02 did NOT modify `apps/micmap/main.cpp`; last touches were Phase 01 `9545811` (driverClient rewire) and `10112ba` (single-tap amend). M-1 cannot run until Phase 01 startup regression is fixed. See `02-03-SUMMARY.md` for full diagnostic trail. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies (Plan 03 Task 2 is pure checkpoint:human-verify per checker Blocker 2 — manual gate is the documented exception)
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify (verified — every wave has at least one automated `<verify><automated>`)
- [ ] Wave 0 covers all MISSING references (`tests/test_config_manager.cpp` created in Plan 01 Task 2; `tests/CMakeLists.txt` registration in Plan 01 Task 3)
- [ ] No watch-mode flags (none used)
- [ ] Feedback latency < 5s (Plan 02 incremental rebuilds; Plan 01 Task 3 + Plan 03 Task 1 are one-shot full builds intentionally — see Test Infrastructure latency table)
- [ ] `nyquist_compliant: true` set in frontmatter (will be flipped by Plan 03 Task 3 Phase B at phase close)

**Approval:** pending

---
phase: 2
slug: config-read-back
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-22
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest standalone (C++ `main()` returning int) — matches existing `tests/test_placeholder.cpp` pattern |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `ctest -R test_config_manager --output-on-failure` |
| **Full suite command** | `ctest --output-on-failure` |
| **Estimated runtime** | ~1 second |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R test_config_manager --output-on-failure`
- **After every plan wave:** Run `ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green + M-1 manual check
- **Max feedback latency:** < 5 seconds (test executable is trivial)

**Nyquist rationale:** Phase observable surface is "settings persist" + "corruption is survived". Round-trip identity (T-1) samples every field. Corruption backup (T-2) samples the failure mode the phase exists to prevent. Clamp (T-3) + first-run (T-4) + retention (T-5) cover the remaining acceptance bands. Together these five scenarios form a sampling rate strictly above the phase's feature frequency.

---

## Per-Task Verification Map

*Populated by planner — each task in a PLAN.md gets a row here linking its ID to the test command and requirement it satisfies.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| {2-NN-NN} | {NN} | {N} | {CFG-XX} | {T-2-NN / —} | {behavior} | unit / integration | `{command}` | ✅ / ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_config_manager.cpp` — covers CFG-01, CFG-02, CFG-03, CFG-04, CFG-05 (all five requirements) via scenarios T-1 round-trip, T-2 corruption backup, T-3 clamp, T-4 first-run, T-5 retention
- [ ] `tests/CMakeLists.txt` — add `add_executable(test_config_manager ...)` + `add_test(NAME test_config_manager COMMAND ...)` registration, link `micmap_core` + `nlohmann_json`
- [ ] No framework install needed (CTest is built into CMake)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| End-to-end user cycle: change a setting in the MicMap UI → quit → relaunch → setting preserved | CFG-01, CFG-05 (success criterion #1) | Requires live GUI interaction + real `%APPDATA%` path + real SteamVR/WASAPI context; cannot be scripted cleanly | 1. Launch `micmap.exe`. 2. Change audio device, sensitivity slider, detection duration. 3. Quit via normal shutdown path. 4. Relaunch. 5. Assert the changed values are displayed in the UI (not defaults). |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending

---
phase: 5
slug: shared-library-extraction
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-01
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake/CTest + native targets (`mic_test.exe`, `hmd_button_test.exe`) |
| **Config file** | `CMakeLists.txt` (root) |
| **Quick run command** | `cmake --build build --target micmap_core_runtime` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~60 s clean configure+build; ~10 s incremental + CTest |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target micmap_core_runtime` (or relevant subtarget) — must compile clean
- **After every plan wave:** Run full configure + build matrix (driver ON and `-DMICMAP_BUILD_DRIVER=OFF`) + `ctest`
- **Before `/gsd-verify-work`:** Full suite green; matrix builds green; manual UAT (SC5) signed off on Bigscreen Beyond rig
- **Max feedback latency:** ~60 s (clean) / ~10 s (incremental)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD by planner | TBD | TBD | LIB-01 / LIB-02 / LIB-03 | — | N/A (build-system metadata) | configure/build/ctest | see Validation Architecture in 05-RESEARCH.md | ✅ existing | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `cmake/AssertNoOpenVRInCore.cmake` — guard script (LIB-02 / SC2) — must exist before any consumer relinks to `micmap_core_runtime`
- [ ] CTest registration for source-grep lints (`MICMAP_DRIVER_BUILD` zero-hit; optional driver-TU runtime-include lint) — installed before plans that wire consumers

*Existing CMake/CTest infra covers everything else; no framework install needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Driver behavior byte-for-byte identical to v1.5 on Bigscreen Beyond rig (SC5) | LIB-01 | Requires real HMD + SteamVR runtime; per CLAUDE.md visual VR validation is mandatory and cannot be substituted by build/type checks | 1) Install built `driver_micmap.dll` to SteamVR drivers dir; 2) Launch SteamVR with Bigscreen Beyond connected; 3) Re-run full v1.5 UAT scenarios (mic detection → SteamVR system button); 4) Confirm no laser beam, no controller phantom, no behavior delta vs v1.5 baseline |
| `dumpbin /exports driver_micmap.dll` shows exactly one symbol `HmdDriverFactory` (SC3) | LIB-03 | Requires Windows MSVC toolchain `dumpbin.exe` from Developer Command Prompt; ok to wire as CTest if MSVC env is reliably available | `dumpbin /exports build/driver/driver_micmap.dll` → assert single export `HmdDriverFactory` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (AssertNoOpenVRInCore.cmake + lint CTests)
- [ ] No watch-mode flags
- [ ] Feedback latency < 60 s (clean) / 10 s (incremental)
- [ ] `nyquist_compliant: true` set in frontmatter after planner finalizes per-task map

**Approval:** pending

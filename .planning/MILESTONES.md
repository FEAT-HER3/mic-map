# MicMap Milestones

Living ledger of shipped milestones. One entry per release. Most recent at the top.

---

## v1.5 — Seamless SteamVR Integration

**Shipped:** 2026-04-24 (closed: 2026-04-29)
**Tag:** `v1.5` (also tagged as `v1.0`; pre-GSD baseline was `v1.0.0`)
**Phases:** 4 shipped (1–4) + 1 deferred (5) | **Plans:** 24 / 24
**Audit:** [`milestones/v1.5-MILESTONE-AUDIT.md`](milestones/v1.5-MILESTONE-AUDIT.md) — `tech_debt` (no behavioral blockers; 29/31 reqs satisfied, 2 deferred)
**Archive:** [`milestones/v1.5-ROADMAP.md`](milestones/v1.5-ROADMAP.md) · [`milestones/v1.5-REQUIREMENTS.md`](milestones/v1.5-REQUIREMENTS.md)

### Delivered

Single-click MicMap installer that drops a pure-sidecar SteamVR driver next to the user's existing drivers, registers itself for SteamVR-native auto-launch, and toggles the SteamVR dashboard hands-free when the user covers the microphone — no virtual controller, no laser beam, no console window, no batch scripts.

### Stats

- Phases: 4 shipped (Driver Sidecar Migration, Config Read-Back, Auto-Start, Installer); Phase 5 (Documentation) deferred
- Plans: 24 / 24 complete
- Commits in milestone: ~170 (32 `feat`, 37 `fix`, 101 `chore`/`docs`)
- Timeline: 2025-11-25 → 2026-04-24 (≈ 5 months wall-clock; final 7 days were Phase 4 + UAT closeout)
- Installer artifact: `MicMap-Setup-v0.1.0.exe` SHA256 `f2a62d662b833264e588ddb1544a8af3461597ca0c2c766f65dab55917451651` (published as GitHub release)
- Live UAT hardware: Bigscreen Beyond + Win11 Pro rig

### Key Accomplishments

1. **Driver sidecar replaces virtual controller** — `driver_micmap.dll` no longer registers any tracked device; instead it polls `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` in `RunFrame`, creates its own `/input/system/click` boolean component on the HMD container, and updates that handle when an HTTP `/button {"kind":"tap"}` arrives. Laser beam fully eliminated. Validated on Bigscreen Beyond.
2. **PascalCase bindings patcher (Phase 1 amendment)** — Plan 01-06 added `bindings_patcher.{hpp,cpp}` after the Plan 01-05 spike falsified the bare-sidecar assumption: `/input/system/click` alone didn't toggle dashboard on lighthouse-non-Index HMDs. Patcher writes `/user/head/input/system → ToggleDashboard / ToggleRoomView` and lasermouse routes into SteamVR's `vrcompositor_bindings_generic_hmd.json`, with `.micmap_backup` for clean uninstall.
3. **Config persistence works** — defensive `nlohmann/json` load path replaces the stub at `config_manager.cpp:142`. UTF-8 wstring boundary, atomic `ReplaceFileW` save, corruption backup with 5-file retention, clamp/pow2-snap. M-1 manual cycle PASSED on real GUI after fixing the unrelated startup hang.
4. **SteamVR-native auto-launch** — `app.vrmanifest` shipped beside `micmap.exe`, `--register-vrmanifest` / `--unregister-vrmanifest` CLI modes. Manifest registrar polls `IsApplicationInstalled` for up to 2 s before calling `SetApplicationAutoLaunch` (OpenVR #1378 guard). Ack-first `AcknowledgeQuit_Exiting` ordering prevents OpenVR #1425 respawn loop. Silent boot: no console, no focus steal, one-shot tray balloon. UAT A/B/C/D/E PASS on real HMD.
5. **Single-click Inno Setup installer** — `MicMap-Setup-v0.1.0.exe` packages driver + app + auto-start registration. WMI-based SteamVR-running gate (with WQL single-quote + tasklist fallback). `vrpathreg removedriver` unconditionally before `adddriver` (OpenVR #1653). Symmetric uninstall: `--unpatch-bindings` → `--unregister-vrmanifest` → `vrpathreg removedriver` → D-13 data-retention prompt (default Keep, silent-uninstall guarded). UAT 10/10 PASS including upgrade-in-place.
6. **Double-`VR_Init` SEGV root-caused** — Phase 04 UAT exposed a startup crash where the manifest retry thread issued `VR_Init(VRApplication_Utility)` while `vrInput`'s `VRApplication_Background` session was still active. OpenVR rejected the second init and SEGV'd ~150 ms later. Fixed in commit `3187fbb` by reusing the in-process Background session via `vr::VRApplications()` — no second `VR_Init` from the retry thread.

### Known Deferred Items (acknowledged at close per `audit-open` report)

| Category | Item | Status | Reason |
|----------|------|--------|--------|
| UAT | `02-HUMAN-UAT.md` (1 pending scenario) | partial | Pending scenario covered by Phase 04 UAT 6+7 (auto-launch + persistence across sessions) |
| UAT | `03-07-UAT.md` (status unknown to audit-open) | unknown | All 5 UAT procedures (A/B/C/D/E) PASS per phase artifact + STATE.md 2026-04-23 closure |
| Verification | `02-VERIFICATION.md` frontmatter still `human_needed` | stale | M-1 PASSED 2026-04-23 per `02-03-SUMMARY.md` `m1_status: PASSED`; frontmatter never refreshed |
| Verification | `04-VERIFICATION.md` frontmatter still `human_needed` | stale | Written before HR-01/MR-01/UninstallSilent fixes; UAT 10/10 PASS post-fix |

### Tech Debt Carried Forward

- Phase 01 missing `01-VERIFICATION.md` outright (procedural — SVR reqs verified via Phase 03/04 live UAT)
- Phases 01/03/04 `*-VALIDATION.md` never flipped to `nyquist_compliant: true` (auto-tests + UAT pass; bookkeeping only)
- File-sink logger (`%APPDATA%\MicMap\micmap.log`) — currently stdout-only under `/SUBSYSTEM:WINDOWS`; queued as observability micro-plan or Phase 05 follow-up
- INST-08 absent from ROADMAP Phase 4 requirements row (REQUIREMENTS.md correctly maps it) — fixed in next milestone DOC-01 rewrite

### Next Milestone

Phase 05 (Documentation: README sync, `docs/architecture.md`) carried forward as the lead-in for the next milestone. Backlog candidates: file-sink logger, in-app auto-start toggle (UX-01), in-VR settings overlay (UX-02), finished-page launch checkbox (DIST-01), silent-install CLI docs (DIST-02), non-default Steam path support (DIST-03).

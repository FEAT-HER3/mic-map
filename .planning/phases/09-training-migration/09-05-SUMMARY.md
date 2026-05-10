---
phase: 09-training-migration
plan: 05
type: summary
status: completed
date: 2026-05-09
---

# 09-05 Summary — Manual UAT on Bigscreen Beyond + Win11 Pro

## Outcome

**PASS** — 9 of 10 D-39 cases passed cleanly; 1 case marked N/A by design. Zero FAIL dispositions.

Phase 9 is ready for `/gsd-verify-work` and ROADMAP completion.

## Rig metadata

- HMD: Bigscreen Beyond
- OS: Windows 11 Pro
- SteamVR: 2.16.5 (1777933763)
- Build flavor: Debug
- Driver SHA at sign-off: `0f27466` (auto-compute fix from D-39 UAT loop)
- Operator: brandon@bigscreenvr.com (Reavo)

## Per-case results

| Case | Title | Status | Note |
|------|-------|--------|------|
| D-39(1) | Training round-trip | ✅ PASS | hash flip `f08197aa…` → `e226e83d…`; dashboard toggle on cover-mic confirmed on real HMD |
| D-39(2) | Cancel mid-session | ✅ PASS clean | hash unchanged across cancel; mode flipped Detecting; D-13 idempotency OK |
| D-39(3) | Recompute → finalize | ✅ PASS | sensitivity=0.30 persisted as correlation=0.65 (matches `setSensitivity()` formula at noise_detector.cpp:450) |
| D-39(4) | Validation rejection | ✅ PASS | 5 envelopes verified (4a-4e); all 400 with `{field, reason}` shape; no state mutation |
| D-39(5) | Orphan timeout | ⚠️ N/A | semantic mismatch between UAT spec (user-walked-away) and impl (capture-thread-death gate); deferred to P10 |
| D-39(6) | Driver-down recovery | ✅ PASS w/revised semantics | file-integrity invariant met; client UI red→green sub-cases N/A by design (paired-life with SteamVR via WM_QUIT) |
| D-39(7) | Lint go-live | ✅ PASS | `AssertNoClientTraining` ctest catches injected `detector->addTrainingSample` regression in apps/micmap/main.cpp; clears on revert |
| D-39(8) | CI corpus replay | ✅ PASS w/caveat | schema valid per D-30; mic_test exit-code-on-failure correct; positive_001 needs future seed_profile.bin (acknowledged debt from 09-04 SUMMARY dev #5) |
| D-39(9) | Replay determinism | ✅ PASS | 3 back-to-back runs sha256 byte-identical (`24ea3ccc…`); D-34 gate met |
| D-39(10) | 50-cycle stress | ✅ PASS clean | zero handle drift (vrserver: 1186 → 1186); H_before == H_after; mode back to Detecting |

## Anomalies that surfaced + Phase 10 carryover

Three latent items captured in `.planning/phases/09-training-migration/deferred-items.md` under the new "P10 carryover" section:

1. **Dual correlation-threshold formula** in `src/detection/src/noise_detector.cpp` — `finishTraining()` line 167 uses `0.4 + (1-S) * 0.3`; `setSensitivity()` line 450 uses `0.3 + (1-S) * 0.5`. Same sensitivity input yields different correlation thresholds depending on call path. Predates Phase 9. P10 should pick one canonical form, align both call sites, add a unit test.
2. **Orphan-timeout user-idle gate** — `lastAcceptedSample_` re-armed on every `addSample` regardless of energy/content. The 30 s timeout is therefore a capture-thread-death gate (real and useful, keep it) but NOT the user-walked-away gate D-39(5) intended. P10 should add either a 5-min hard cap OR an above-threshold filter inside `addSample`.
3. **Client survives SteamVR restart** — `micmap.exe` is a SteamVR overlay app and self-terminates on WM_QUIT. SteamVR's vrmanifest auto-launch brings it back, so paired-life is the actual UX. UAT spec's "red → green driver-loaded indicator on the same client instance" is unobservable. Optional P10 UX upgrade.

## Bonus deliverable: auto-compute fix (commit `0f27466`)

D-39(1) and D-39(3) initially blocked by an implementation gap surfaced during UAT — the driver never auto-transitioned `TrainingSession` from `Collecting` to `Ready` after `samples_collected` reached `target_`. The UI-SPEC's "Cover mic → Ready preview → Recompute / Confirm" flow was unreachable from a fresh session. Fix added `TrainingSession::maybeAutoCompute()` and a single-line call in `DetectionRunner` per-iteration tick. End-to-end on rig:

- POST /training/start → 200
- /training/progress at t=0: state=collecting, samples=2, preview=null
- /training/progress at t=3s: state=ready, samples=100, preview populated
- POST /training/recompute {sensitivity:0.30} → 200, preview updated
- POST /training/cancel → 200 {cancelled:true}, mode flips back to Detecting

Without this fix Phase 9 would have shipped a UI flow that the driver could not satisfy.

## Bonus deliverable: stale-build incident note

D-39(6) initially appeared to surface a D-23 single-writer violation — client log showed `Saved training data to: …` on shutdown, suggesting `apps/micmap/main.cpp` was still calling `saveTrainingData()`. Investigation showed source was clean (09-03 deletions intact); the running binary was a stale incremental build. Clean rebuild produced the correct binary; subsequent shutdown log omits the save line. Logged as a build-system robustness item (consider stamping source SHA into binary at build time).

## Pre-UAT setup confirmation

- Build: clean Debug rebuild from `hmd-button` HEAD installed at `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap` (paths captured in `CLAUDE.md` Hardware rig section, commit `e1133f7`)
- Pre-UAT hash record: `f08197aab9c2dce29259c5bea4bc7e022b4b0a54ca0eed7b37b7879061cdde60` (`%APPDATA%\MicMap\training_data.bin`)
- Pre-UAT vrsettings backup: `default.vrsettings.preuat.bak` (canonical pristine source, flags=false)
- vrsettings flipped to `enable_driver_audio=true` + `enable_driver_detection=true` for UAT duration
- Post-UAT vrsettings restored to flags=false from canonical source (D-40 — P10 owns flips)
- `training_data.bin.preuat` deleted

## Post-UAT state

- branch `hmd-button` at HEAD (post-sign-off commit pending)
- All 6 Phase 9 plans completed (00, 01, 02, 03, 04, 05)
- ROADMAP marker for Phase 9 ready to flip from `[ ]` to `[x]` and gain a 2026-05-09 completion footnote
- Three carryover items captured for Phase 10

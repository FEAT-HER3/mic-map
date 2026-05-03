---
phase: 06-driver-side-audio-capture-spike
plan: 04
subsystem: uat
tags: [uat, real-hardware, bigscreen-beyond, win11, sc1, sc2, sc3, sc4, sc5, d-17, d-18, d-19, d-20, mig-01, pitfall-1, pitfall-3, pitfall-4, pitfall-13]
status: complete
completed: 2026-05-02

# Dependency graph
requires:
  - phase: 06-driver-side-audio-capture-spike
    plan: 01
    provides: AssertAudioWorkerNoVrApi + AudioWorkerLifecycleHeadless ctest invariants (Wave 0 RED scaffold)
  - phase: 06-driver-side-audio-capture-spike
    plan: 02
    provides: AudioWorker class implementation (Pitfalls 1/4/13 mitigations) + enable_driver_audio flag default false + driver target source registration
  - phase: 06-driver-side-audio-capture-spike
    plan: 03
    provides: DeviceProvider Init flag-read + conditional AudioWorker construction LAST + Cleanup audioWorker_.reset() FIRST
provides:
  - .planning/phases/06-driver-side-audio-capture-spike/06-UAT.md — D-17(1)-(4) observations + vrserver.txt evidence excerpts + APPROVED sign-off
  - driver/resources/settings/default.vrsettings — restored to enable_driver_audio: false (D-19 / D-20 shipped default)
affects: [07-driver-side-detection-thread]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Real-hardware UAT regimen (D-17): four sub-steps in a single continuous SteamVR session (1)+(2)+(3) followed by a cold-boot regression (4); evidence captured as vrserver.txt log excerpts pasted into 06-UAT.md per D-18"
    - "Fix-forward inline during UAT: device-selection gap in Plan 06-02 surfaced as `startCapture() failed` on D-17(1) attempt 1; patched audio_worker.cpp to enumerate + selectDeviceById between WASAPIAudioCapture construction and startCapture() (prefer Beyond, fallback isDefault, fallback first); rebuilt + reinstalled to SteamVR drivers folder; second attempt PASS"
    - "Threat-model-driven UAT design (T1-T6): each STRIDE threat carried from Plans 02/03 mapped to a specific D-17 sub-step observation; T-06-04-01 (UAT log fabrication) explicitly mitigated by requiring real grep output / counts / paste-in evidence"

# Verification matrix (acceptance criteria → UAT outcomes)
verification:
  D-17(1) flag-ON capture (SC1):
    outcome: PASS
    evidence:
      - vrserver.txt 20:22:40.305-41.516 window
      - "MicMap: enable_driver_audio = true present"
      - "MicMap: AudioWorker created + thread spawned"
      - "MicMap: audio worker thread COM apartment = MTA (hr=0x00000000)"
      - "MicMap: audio worker selected device index=6 of 7 (isDefault=1, beyond=1)"
      - "MicMap: audio worker capture started"
      - "100 RMS lines spanning 989 ms (rms[0]@40.527 → rms[99]@41.516) — kRmsBudget exhausted within ~1 s budget per D-08"
  D-17(2) HMD wake/sleep × 2 cycles:
    outcome: PASS
    evidence:
      - "vrserver.exe PID 62972 stable across both cycles"
      - "Get-Process vrserver: 1218 handles / 136.9 MB WS"
      - "zero MicMap: audio worker thread exiting lines"
      - "zero audio.*error / audio.*fail lines"
  D-17(3) SteamVR-restart-without-quit (SC2/SC5):
    outcome: PASS (with plan-vs-reality note)
    evidence:
      - "Cleanup→Init transition logs: cleaning up... → audio worker thread exiting cleanly (+4 ms) → AudioWorker thread joined cleanly (+25 ms total << 2 s watchdog) → Stopping HttpServer (D-13 reverse-order observed: AudioWorker before HttpServer)"
      - "vrserver.exe respawned (PID 62972 → 35504) — actual SteamVR Restart UI behavior; plan's 'in-process Cleanup→Init' wording was aspirational; safety contract upheld"
      - "second Init: port 27015 rebound cleanly + AudioWorker re-created + WASAPI capture re-acquired"
      - "grep -c AUDCLNT_E_DEVICE_IN_USE = 0 (no device-in-use error on second Init)"
      - "second AudioWorker fresh kRmsBudget=100 (rms[0..99] spans 28:04.768→05.242, ~474 ms)"
  D-17(4) flag-OFF regression (SC4):
    outcome: PASS
    evidence:
      - "cold boot with enable_driver_audio: false in installed vrsettings"
      - "vrserver.txt: 'MicMap: enable_driver_audio = false' present at 20:32:42.606"
      - "grep -c MicMap: AudioWorker = 0 / grep -c MicMap audio: rms = 0 / grep -c MicMap: audio worker = 0 (D-03: AudioWorker never constructed when flag false)"
      - "POST /button × 3 via curl returned {\"status\":\"ok\"}; vrserver.txt shows three UpdateBooleanComponent(down) + three UpdateBooleanComponent(up) on handle=7 — v1.5 trigger path byte-identical"
      - "single new log line vs Phase 5 baseline: enable_driver_audio = false (unconditional driver-state diagnostic, identical line shape under flag=ON)"

# Phase 6 success criteria — final mapping
phase-success-criteria:
  SC1: PASS (D-17(1) — flag-ON real-hardware capture, ~1 s RMS log)
  SC2: PASS (D-17(1) RPC_E_CHANGED_MODE absent + D-17(3) AUDCLNT_E_DEVICE_IN_USE absent on second Init)
  SC3: PASS (CoInitializeEx exclusively on worker thread per AssertAudioWorkerNoVrApi lint + grep -c device_provider.cpp = 0)
  SC4: PASS (D-17(4) flag-OFF byte-identical to Phase 5 baseline)
  SC5: PASS (D-17(3) IMMNotificationClient register/unregister/re-register cycle; alive-flag mitigation in driver-only code per D-15/D-16)

# Spike outcome
spike-outcome: GO
spike-rationale: |
  WASAPI capture inside vrserver.exe DLL host on Bigscreen Beyond + Win11 Pro is feasible.
  All five Phase 6 Success Criteria validated on real hardware. Pitfalls 1, 3, 4, 13 mitigations
  hold under realistic SteamVR lifecycle stress (HMD wake/sleep, restart-without-quit). The
  highest-risk unknown of the v1.6 milestone is resolved.

  Phase 7 (Driver-Side Detection Thread) is unblocked — the same AudioWorker pattern can be
  extended with the lock-free SampleRing producer + DetectionRunner consumer without
  re-architecting the apartment / lifecycle / alive-flag scaffolding.

# Fix-forward applied during UAT
fix-forward:
  - commit: 1c6407b
    plan: 06-02
    file: driver/src/audio_worker.cpp
    issue: "AudioWorker called WASAPIAudioCapture::startCapture() without first selecting a device, hitting currentDevice_ == null guard"
    fix: "added enumerateDevices() + selectDeviceById() between createWASAPICapture() and startCapture(); selection prefers Beyond mic (production target), fallback isDefault, fallback first"
    notes: "Both client apps (apps/micmap, apps/mic_test) work around the same gap; the driver-side worker missed that step. Stays in D-11 spike scope (default capture device) and D-12 (no config.json reading — Phase 8 work)."
    re-verified:
      - "ctest --test-dir build -C Release -R 'AssertAudioWorkerNoVrApi|AudioWorkerLifecycleHeadless|lint_no_openvr_in_core|lint_no_driver_macro' — 4/4 PASS"
      - "dumpbin /exports — only HmdDriverFactory"
      - "D-17(1) attempt 2 PASS"

# Final state
final-state:
  default.vrsettings:
    enable_driver_audio: false
    note: "shipped default per D-19/D-20 — flag stays OFF on main until Phase 7 closes its own SC"
  ctest-battery: "14/14 PASS"
  driver-exports: "HmdDriverFactory only"
  next: Phase 7 (Driver-Side Detection Thread) per ROADMAP

# Commits
commits:
  - 5c65c71: docs(06-04): scaffold UAT log + toggle flag for D-17 run
  - 1c6407b: fix(06-02): select default capture device before startCapture()
  - e4640f8: docs(06): UAT sign-off — D-17(1)-(4) all PASS on Bigscreen Beyond + Win11 Pro
---

# Plan 06-04 Summary — Real-Hardware UAT (D-17)

Phase 6's go/no-go gate. Operator-driven real-hardware exercise of the AudioWorker
on Bigscreen Beyond + Win11 Pro across four scenarios. All four PASS.

See frontmatter `verification` matrix for per-sub-step evidence and `phase-success-criteria`
for the SC1..SC5 → outcome mapping. Full UAT log + log-excerpt evidence at
`06-UAT.md`. Fix-forward to Plan 06-02 (device selection) committed inline as
`1c6407b`.

**Spike outcome: GO.** Phase 7 (Driver-Side Detection Thread) is unblocked.

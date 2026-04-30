# MicMap

## Current State

**Shipped:** v1.5 (2026-04-24) — "Seamless SteamVR Integration". See [`MILESTONES.md`](MILESTONES.md) and [`milestones/v1.5-ROADMAP.md`](milestones/v1.5-ROADMAP.md).

What runs today: a single-click installer (`MicMap-Setup-v0.1.0.exe`) drops a pure-sidecar SteamVR driver next to the user's other drivers, registers `app.vrmanifest` for SteamVR-native auto-launch, and patches the generic_hmd bindings so covering the microphone toggles the SteamVR dashboard hands-free — no virtual controller, no laser beam, no console window. UAT 10/10 PASS on Bigscreen Beyond + Win11 Pro rig.

## Current Milestone: v1.6 Feature Migration

**Goal:** Relocate all non-UI features from `micmap.exe` client into `driver_micmap.dll` SteamVR driver so the driver runs end-to-end without the client. Client demoted to settings + driver-health UI.

**Target features:**
- Driver hosts WASAPI audio capture, FFT detection, state machine, config read/write, training-data persistence, and the trigger pipeline directly (no IPC hop for trigger)
- Audio + detection code lives in a shared static library so the same source is buildable into the driver DLL, the client EXE, and headless test harnesses (`mic_test.exe` runs without SteamVR; changes flow into the driver build with no port)
- Client retains: audio device picker, detection-time/sensitivity controls, pattern-training UI (sample collection + threshold compute), driver-health/connection indicator
- IPC reshaped: client → driver pushes settings + training samples; client ← driver pulls health/state. Trigger no longer crosses the wire.
- Phase 5 carryover from v1.5 rolled in: DOC-01 (README sync) + DOC-02 (`docs/architecture.md`) ship as part of v1.6 closeout, updated to reflect the post-migration architecture

**Backlog candidates not in v1.6 scope:** file-sink logger at `%APPDATA%\MicMap\micmap.log` (UAT C3 follow-up; logger currently stdout-only under `/SUBSYSTEM:WINDOWS`), in-app auto-start toggle (UX-01), in-VR settings overlay (UX-02), installer finished-page launch checkbox (DIST-01), silent-install CLI documentation (DIST-02), non-default Steam path support via `HKCU\Software\Valve\Steam\SteamPath` (DIST-03), detection-accuracy work (DET-01/02).

---

## What This Is

MicMap is a Windows SteamVR addon that listens to microphone input, detects a trained noise pattern (e.g. covering the mic), and triggers the SteamVR system button — opening or selecting in the dashboard hands-free. It's for VR users who want a quiet, always-available input action without reaching for a controller. The v1.5 release ships the seamless integration: pure-sidecar driver, SteamVR-native auto-launch, single-click installer.

## Core Value

Covering the microphone reliably toggles the SteamVR dashboard, invisibly to the rest of VR — no controller beam, no extra hardware, no focus loss.

## Requirements

### Validated

<!-- Shipped and confirmed valuable — inferred from existing code. -->

- ✓ WASAPI-based microphone capture with device enumeration and hot-swap — existing (`src/audio/`)
- ✓ FFT-based white-noise detection with trainable spectral profile — existing (`src/detection/`)
- ✓ Training flow: collect ~150 samples, compute thresholds, persist to `%APPDATA%/MicMap/training_data.bin` — existing
- ✓ State machine (Idle → Training → Detecting → Triggered → Cooldown) with configurable min-duration + cooldown — existing (`src/core/state_machine`)
- ✓ ImGui + D3D11 desktop UI with tray icon, device picker, sensitivity / duration controls, training button — existing (`apps/micmap/`)
- ✓ SteamVR driver plugin (`driver_micmap.dll`) that currently registers a virtual controller and injects button events — existing (to be replaced this milestone)
- ✓ App ↔ driver IPC via localhost HTTP bridge (cpp-httplib) — existing (`src/steamvr/driver_client`)
- ✓ Batch-script installer (`scripts/install_driver.bat`) using `vrpathreg adddriver` — existing (to be replaced this milestone)
- ✓ Config file write path (JSON emitted to `%APPDATA%/MicMap/config.json`) — existing but read-back is stubbed
- ✓ Driver-side pure HMD sidecar: own `/input/system/click` boolean component on HMD property container — v1.5 (no virtual controller, no laser beam; SVR-01..11)
- ✓ Bindings patcher writing PascalCase dashboard + lasermouse routes into SteamVR's generic_hmd config with `.micmap_backup` for clean uninstall — v1.5 (Phase 1 amendment 01-06; INST-08)
- ✓ Defensive `nlohmann/json` config read-back: UTF-8 wstring boundary, atomic `ReplaceFileW` save, corruption backup with 5-file retention, clamp/pow2-snap — v1.5 (CFG-01..05)
- ✓ SteamVR-native auto-launch via `app.vrmanifest` + `--register-vrmanifest` / `--unregister-vrmanifest` CLI; idempotent re-registration with `IsApplicationInstalled` poll guard; `AcknowledgeQuit_Exiting` ack-first ordering for OpenVR #1425; silent boot (no console, no focus steal, one-shot tray balloon) — v1.5 (AUTO-01..06)
- ✓ Single-click Inno Setup installer with WMI SteamVR-running gate, `vrpathreg removedriver`-before-`adddriver`, symmetric uninstall with D-13 data-retention prompt, frozen AppId for upgrade-in-place — v1.5 (INST-01..08)

### Active

<!-- v1.6 Feature Migration — requirements defined during this milestone's REQUIREMENTS.md phase. DOC-01/02 carry over from v1.5 and will be re-scoped against post-migration architecture. -->

- [ ] **DOC-01**: README reflects post-v1.6 reality (driver runs detection end-to-end; client is settings/health-only); crossed-out auto-start sections become current
- [ ] **DOC-02**: New `docs/architecture.md` documents driver-resident detection pipeline, shared audio/detection library, sidecar-on-HMD technique, CommandQueue boundary, and HMD reactivation lifecycle
- [ ] Remaining v1.6 requirements (MIG-*, LIB-*, IPC-*, etc.) defined in `.planning/REQUIREMENTS.md` after research

### Out of Scope

<!-- Explicit milestone boundaries. -->

- SteamVR overlay stubs (`createSettingsOverlay`, `showOverlay`, `hideOverlay` in `dashboard_manager.cpp`) — tracked tech debt, not required for the core "cover mic → toggle dashboard" value; defer to a later settings-in-VR milestone
- Detection accuracy in loud / noisy environments — real problem, but orthogonal to the architecture migration; retrain-in-environment is the current workaround and will remain so this milestone
- Non-Windows platforms (Linux/macOS audio stubs) — MicMap is Windows-only by design of the SteamVR driver story; no platform expansion this milestone
- Fallback path that keeps the virtual-controller driver alongside the sidecar — ripped out entirely (worse UX, no upside per user)
- New detection features (beyond mic-cover pattern) — architecture-only milestone
- In-dashboard "select" as a distinct action — unnecessary because `/input/system/click` natively handles both open and in-dashboard select; the old "open vs. select" split was an artifact of the virtual-controller architecture

## Context

**Prior art from a sibling GSD project:** The sidecar-driver-creating-input-on-HMD technique was discovered and validated in `D:\Documents\Projects\bey-closer-t1`. Key references:

- `bey-closer-t1/HMD Button Stub.md` — full technique writeup: cross-driver `UpdateBooleanComponent` is blocked (`VRInputError_WrongType`), but cross-driver `CreateBooleanComponent` on a duplicate path is allowed and SteamVR propagates updates from the new component. Tested on SteamVR March 2026 + OpenVR SDK v2.5.1 + Windows 11.
- `bey-closer-t1/.planning/todos/pending/2026-03-26-explore-input-system-click-handle-probing-on-hmd.md` — the exact "integrate `/input/system/click` into sidecar driver's command set" todo that this milestone fulfills.
- `bey-closer-t1/installer/BeyondProximity.iss` — Inno Setup reference installer (admin, process-aware via WMI, `vrpathreg adddriver`, optional post-install launch). **Note:** bey-closer-t1 nested itself under the Bigscreen Beyond driver folder; MicMap owns its own driver directory and will install accordingly.

**Current architecture summary** (from `.planning/codebase/`):
- Two-process model: `micmap.exe` (audio/detection/UI) + `driver_micmap.dll` (SteamVR plugin), connected by localhost HTTP
- Layered libraries: audio → detection → core (state machine, config) → steamvr; common utilities underneath
- Config file exists at `%APPDATA%/MicMap/config.json` but is write-only (read path stubbed at `src/core/src/config_manager.cpp:142`)
- Detection flow: WASAPI callback → RMS/dB → FFT analyzer → confidence score → state machine → HTTP trigger to driver → virtual controller button injection
- After milestone: last step becomes HMD-container `/input/system/click` update

**Known open issues documented but out of scope this milestone:** overlay UI stubs, loud-environment detection accuracy, unencrypted localhost HTTP (mitigated by localhost-only binding).

**v1.5 closure note (2026-04-29):** All 4 shipped phases verified end-to-end on real hardware (Bigscreen Beyond + Win11 Pro). Phase 5 Documentation deferred to next milestone. Audit verdict `tech_debt` — bookkeeping drift only (stale VERIFICATION.md frontmatter, never-flipped VALIDATION.md sign-offs); zero behavioral blockers. Installer artifact `MicMap-Setup-v0.1.0.exe` SHA256 `f2a62d662b833264e588ddb1544a8af3461597ca0c2c766f65dab55917451651` published as GitHub release at tag `v1.5` (also tagged `v1.0`). See `MILESTONES.md` for full delivery summary.

## Constraints

- **Platform**: Windows-only — WASAPI for audio, OpenVR driver DLL, Inno Setup for installer. Non-Windows stubs remain as-is.
- **SteamVR**: Driver must remain compatible with OpenVR SDK ≥ v2.5.1 (validated in bey-closer-t1 on March 2026 SteamVR). Sidecar pattern requires SteamVR's acceptance of duplicate-path components on the HMD container — this is the project's core external dependency.
- **Tech stack (locked)**: C++, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. No framework changes this milestone.
- **Privileges**: Installer runs elevated (admin) — required for `vrpathreg` and writing into Steam install directories. Runtime does not require admin.
- **IPC**: Localhost HTTP between app and driver. Not replacing IPC this milestone; driver just swaps its end-of-pipeline action.
- **Distribution**: Driver + app + auto-start registration all ship in a single installer artifact — one click, one uninstall.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Rip out virtual-controller driver entirely (no fallback) | User-stated: worse UX with no benefits; HMD `/input/system/click` behaves the same way natively without the laser beam | ✓ Validated v1.5 — laser beam fully eliminated; native ToggleDashboard semantics |
| Sidecar-on-HMD technique (create own `/input/system/click` on HMD container) for button injection | Validated in bey-closer-t1; documented in `HMD Button Stub.md` with known timing and error constraints | ✓ Validated v1.5 — driver creates `handle=7` against HMD container; survives 5-cycle SteamVR restart UAT |
| Phase 1 amendment 01-06 — bindings patcher writes PascalCase dashboard + lasermouse routes into SteamVR's generic_hmd config | Plan 01-05 spike falsified the bare-sidecar assumption: HMD click alone didn't toggle dashboard on lighthouse-non-Index HMDs | ✓ Validated v1.5 — surfaced INST-08 for the installer mirror at install time |
| `IDriverClient::tap()` single-tap collapse (commit `10112ba`) | Driver schedules its own min-hold release; client just enqueues `TapCommand` | ✓ Validated v1.5 — wire format is `POST /button {"kind":"tap"}`; no client-side timing concerns |
| Use SteamVR-native auto-start (`app.vrmanifest` + auto-launch) rather than Windows Run-key or Startup folder | Most SteamVR-native UX; lifecycle is tied to SteamVR, not the OS session | ✓ Validated v1.5 — appears in SteamVR "Manage Startup Overlay Apps" with `app_key=bigscreen.micmap` |
| Manifest registrar reuses vrInput's `VRApplication_Background` session — no second `VR_Init(VRApplication_Utility)` from the retry thread (commit `3187fbb`) | OpenVR rejects in-process double-init; observed SEGV ~150 ms after startup; root-caused via debug session | ✓ Validated v1.5 — tray icon now persists across SteamVR session |
| Adopt Inno Setup installer pattern from bey-closer-t1 (not nested — MicMap owns its driver dir) | Better install UX than batch script; single artifact covers driver + app + auto-start | ✓ Validated v1.5 — UAT 10/10 PASS including upgrade-in-place + clean uninstall |
| Eliminate dashboard-open-state polling and the "open → system click / open → trigger click" branch | `/input/system/click` is one native action that handles both open and in-dashboard select; old split was a virtual-controller artifact | ✓ Validated v1.5 — single trigger code path, no dashboard state machine |
| Fix JSON config read-back (was stubbed) using already-present nlohmann/json | Settings persistence is table-stakes UX; dependency already vendored | ✓ Validated v1.5 — M-1 manual cycle PASSED post startup-fix `73681c5` |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-04-30 — v1.6 Feature Migration milestone opened; v1.5 closed 2026-04-29 (Seamless SteamVR Integration shipped 2026-04-24)*

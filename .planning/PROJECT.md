# MicMap

## What This Is

MicMap is a Windows SteamVR addon that listens to microphone input, detects a trained noise pattern (e.g. covering the mic), and triggers the SteamVR system button — opening or selecting in the dashboard hands-free. It's for VR users who want a quiet, always-available input action without reaching for a controller.

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

### Active

<!-- "Seamless SteamVR Integration" milestone. All hypotheses until shipped. -->

- [ ] **SVR-01**: Driver migrates from virtual-controller architecture to a pure sidecar that creates its own `/input/system/click` boolean component on the HMD property container — no `TrackedDeviceAdded`, no virtual controller, no laser beam on trigger
- [ ] **SVR-02**: Driver defers HMD-container component creation until `TrackedDeviceToPropertyContainer(k_unTrackedDeviceIndex_Hmd)` returns a valid handle (polling in `RunFrame`), falling back gracefully if creation ever fails
- [ ] **SVR-03**: On detection trigger, app signals driver via the existing HTTP bridge and the driver calls `UpdateBooleanComponent` on its HMD-side `/input/system/click` handle — single code path, no dashboard-state branching
- [ ] **SVR-04**: All virtual-controller code (device provider registration, controller device class, dashboard-state polling, "open vs. select" branching) is removed, not feature-flagged
- [ ] **CFG-01**: JSON config is read back on startup via nlohmann/json, so user settings (device selection, detection duration, sensitivity, SteamVR options) persist across sessions
- [ ] **AUTO-01**: MicMap auto-starts with SteamVR using the SteamVR-native mechanism (`app.vrmanifest` + auto-launch registration), so users don't need to launch `micmap.exe` manually
- [ ] **INST-01**: Replace the batch-script installer with an Inno Setup single-click installer (patterned on bey-closer-t1's `BeyondProximity.iss`): admin-elevated, process-aware (detect running SteamVR), registers via `vrpathreg adddriver`, handles its own driver directory (MicMap is not nested under another vendor's driver), offers post-install SteamVR launch
- [ ] **INST-02**: Installer registers the `app.vrmanifest` for auto-start (AUTO-01) as part of a single unified install step — one install covers driver + app + auto-start
- [ ] **DOC-01**: README reflects the new architecture (sidecar HMD button, no virtual controller, auto-start default) and the new installer flow; crossed-out auto-start sections become current

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
| Rip out virtual-controller driver entirely (no fallback) | User-stated: worse UX with no benefits; HMD `/input/system/click` behaves the same way natively without the laser beam | — Pending |
| Sidecar-on-HMD technique (create own `/input/system/click` on HMD container) for button injection | Validated in bey-closer-t1; documented in `HMD Button Stub.md` with known timing and error constraints | — Pending |
| Use SteamVR-native auto-start (`app.vrmanifest` + auto-launch) rather than Windows Run-key or Startup folder | Most SteamVR-native UX; lifecycle is tied to SteamVR, not the OS session | — Pending |
| Adopt Inno Setup installer pattern from bey-closer-t1 (not nested — MicMap owns its driver dir) | Better install UX than batch script; single artifact covers driver + app + auto-start | — Pending |
| Eliminate dashboard-open-state polling and the "open → system click / open → trigger click" branch | `/input/system/click` is one native action that handles both open and in-dashboard select; old split was a virtual-controller artifact | — Pending |
| Fix JSON config read-back (was stubbed) using already-present nlohmann/json | Settings persistence is table-stakes UX; dependency already vendored | — Pending |

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
*Last updated: 2026-04-22 after initialization (milestone: Seamless SteamVR Integration)*

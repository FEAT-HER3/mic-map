# MicMap — Claude Context

Windows SteamVR addon that detects a trained noise pattern on a microphone (typically "covering the mic") and triggers the SteamVR system button. Hands-free dashboard toggling, no extra hardware.

## Where to look first

- **`.planning/PROJECT.md`** — current product definition, milestone scope, key decisions, in/out of scope
- **`.planning/ROADMAP.md`** — phase breakdown for the active milestone
- **`.planning/REQUIREMENTS.md`** — falsifiable v1 requirements with REQ-IDs
- **`.planning/STATE.md`** — current position in the milestone, recent decisions, blockers
- **`.planning/research/SUMMARY.md`** — synthesized research for the current milestone (stack versions, architecture, critical pitfalls)
- **`.planning/codebase/`** — snapshot of the existing architecture, stack, conventions, and known concerns

When a question touches the codebase architecture or the active milestone, read the relevant files above before doing open-ended search.

## Active milestone

**Seamless SteamVR Integration** — rip out the legacy virtual-controller driver, replace it with a pure sidecar that creates its own `/input/system/click` boolean component on the HMD property container (technique validated in sister project `D:\Documents\Projects\bey-closer-t1`; see `HMD Button Stub.md` there). Also: fix stubbed JSON config read-back, add SteamVR-native auto-launch via `app.vrmanifest`, ship a single-click Inno Setup installer.

Phase order is load-bearing: Driver Sidecar (1) → Config Read-Back (2, parallel with 1) → Auto-Start (3) → Installer (4) → Docs (5). Do not reorder.

## GSD workflow

This project uses the GSD (Get Shit Done) workflow. Phase lifecycle:

1. **Discuss** — `/gsd-discuss-phase N` gathers context and surfaces gray areas before planning
2. **Plan** — `/gsd-plan-phase N` produces detailed PLAN.md files with task breakdown and verification loop
3. **Execute** — `/gsd-execute-phase N` runs the plans with atomic commits per task
4. **Transition** — move to the next phase with `/gsd-next` or `/gsd-transition`

Current config (`.planning/config.json`): YOLO mode, standard granularity, parallel execution, research + plan-check + verifier all enabled, quality model profile (opus for research/roadmap).

Do not skip phase artifacts. They are the project's memory across context resets.

## Environment notes

- Windows-only: WASAPI for audio, OpenVR driver DLL, Inno Setup for installer. Non-Windows audio stubs remain.
- Bash is available via Git Bash; use Unix-style paths in shell commands (forward slashes, `/dev/null`).
- Stack is locked this milestone: C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. No framework changes.
- Installer runs elevated (admin); runtime does not require admin.

## Testing

- `mic_test.exe` — audio/detection testing without VR
- `hmd_button_test.exe` — VR input testing without audio (the exit-criterion harness for Phase 1)

Visual validation on a real HMD is mandatory for the "no laser beam" exit criterion of Phase 1 — type-checking and build-success do not substitute.

## Hardware rig (this machine)

This dev machine **is** the Bigscreen Beyond + Win11 Pro UAT rig — `vrpathreg show` lists `bigscreenbeyond` + `BeyondEyetracking` + `BeyondProximity` driver entries. SteamVR can be started/stopped as needed for phase verification and UAT — no separate test station to coordinate with.

**Installed driver location** (where SteamVR loads from at runtime):
- Driver root: `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\micmap`
- Driver DLL: `…\micmap\bin\win64\driver_micmap.dll`
- Sidecar client: `…\micmap\bin\micmap.exe`
- Driver vrsettings: `…\micmap\resources\settings\default.vrsettings`
- Pre-existing rotation backups beside DLL: `driver_micmap.dll.{p5,v1.5}.bak`; `default.vrsettings.p5.bak`

**Build → install copy paths** (forward-slash forms for Git Bash):
- `build/driver/micmap/bin/win64/driver_micmap.dll` → `…/micmap/bin/win64/driver_micmap.dll`
- `build/bin/Debug/micmap.exe` → `…/micmap/bin/micmap.exe`

Always backup the installed file as `<name>.preuat.bak` before a UAT-flavored install; restore from `driver/resources/settings/default.vrsettings` (canonical pristine source) only when a FAIL needs investigation (no longer routine post-UAT cleanup — see Post-Phase-10 default state below).

**Post-Phase-10 default state** (ships as TRUE):
- `enable_driver_audio: true` AND `enable_driver_detection: true` are the new shipped defaults per Phase 10 D-25 (P10 OWNS the flip; supersedes the P6–P9 D-40 post-UAT-OFF discipline).
- The driver runs detection end-to-end by default; client is settings + driver-health UI only post-cutover.
- D-02 emergency override: a user CAN edit `default.vrsettings` to `false` for debugging, but installer upgrades will overwrite (Pitfall 9 acceptance — install-scoped, not a runtime config knob).
- The on-rig install at `<Steam>/drivers/micmap/` is the shipped reference for the post-cutover defaults; restore from `driver/resources/settings/default.vrsettings` (canonical pristine source) for clean-state debugging.

**Runtime HTTP port**: `27015` on `127.0.0.1` (per source `driver/resources/settings/default.vrsettings`). Older planning docs may reference `27115` — that was a draft value, never shipped.

**User profile config dir**: `%APPDATA%\MicMap\` — `training_data.bin` lives here. Always backup to `training_data.bin.preuat` before stress / destructive UAT cases.

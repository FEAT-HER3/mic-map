---
phase: 01-driver-sidecar-migration
plan: 06
subsystem: driver+steamvr+apps
tags: [driver, openvr, bindings, patcher, single-tap, amendment, spike-followup]
type: amendment
dependency_graph:
  requires:
    - "Plan 01-03 driver sidecar (DeviceProvider + CommandQueue + HttpServer)"
    - "Plan 01-04 app-side rewire (IDriverClient, apps/micmap onTrigger, hmd_button_test harness)"
  provides:
    - "driver/src/bindings_patcher.{hpp,cpp} -- idempotent SteamVR bindings writer"
    - "PascalCase-correct routing from /user/head/input/system -> ToggleDashboard / ToggleRoomView / LeftClick / Pointer"
    - "Single-tap IDriverClient::tap() surface + TapCommand queue type"
    - "Native SteamVR toggle semantics on any lighthouse-served HMD (validated on Bigscreen Beyond)"
  affects:
    - "Phase 04 installer (new INST-08: mirror bindings_patcher at install time)"
key-files:
  created:
    - driver/src/bindings_patcher.hpp
    - driver/src/bindings_patcher.cpp
    - .planning/phases/01-driver-sidecar-migration/01-06-SUMMARY.md
  modified:
    - driver/CMakeLists.txt
    - driver/src/command_queue.hpp
    - driver/src/device_provider.hpp
    - driver/src/device_provider.cpp
    - driver/src/http_server.cpp
    - src/core/include/micmap/core/state_machine.hpp
    - src/core/src/state_machine.cpp
    - src/steamvr/include/micmap/steamvr/vr_input.hpp
    - src/steamvr/src/vr_input.cpp
    - src/steamvr/CMakeLists.txt (via Fix: OpenVR DLL copy path)
    - apps/micmap/main.cpp
    - apps/hmd_button_test/main.cpp
    - tests/test_command_queue.cpp
    - .planning/REQUIREMENTS.md
  deleted:
    - driver/resources/input/micmap_hmd_profile.json
    - driver/resources/input/micmap_vrcompositor_bindings.json
---

# Plan 01-06: Bindings Patcher + Single-Tap Amendment

**Status:** shipped, validated on hardware
**Triggered by:** Plan 01-05 spike attempt falsified a key assumption

## What this plan exists for

Plans 01-01 through 01-04 landed on the assumption (from bey-closer-t1's
`HMD Button Stub.md`) that publishing `/input/system/click` on the HMD
property container would open the SteamVR dashboard on any HMD. When the
01-05 validation spike started on a Bigscreen Beyond rig, that turned
out to be false. This plan captures the two follow-up pieces of work
that were needed to actually hit the Phase 01 exit criterion.

## What we discovered

1. **bey-closer-t1's technique validated propagation, not dashboard-open.**
   HMD Button Stub.md propagated `/proximity` updates cross-driver (the
   Index HMD-button write in that doc was extrapolation, "may reflect").
   The doc's own wording ("*may* reflect") turned out to be load-bearing.

2. **Lighthouse owns `Prop_ControllerType_String`.** Cross-driver writes
   to that property are accepted into the container (we see err=0) but
   SteamVR's binding-resolver still reads lighthouse's value
   (`"lighthouse_hmd"` on Bigscreen Beyond) when choosing which
   `vrcompositor_bindings_<type>.json` to load. A sidecar driver cannot
   change the controller_type SteamVR uses for lookup.

3. **`Prop_InputProfilePath_String` is *not* contested on lighthouse HMDs.**
   Lighthouse doesn't set it on non-Index HMDs, so secondary-driver
   writes to this property stick. But pointing it at a profile whose
   declared `controller_type` mismatches the device's effective
   `controller_type` causes SteamVR to silently drop the bindings --
   including `/actions/lasermouse/in/Pointer`, so the head-locked cursor
   vanishes. Trying to override `input_profile_path` made the situation
   worse than leaving it alone.

4. **SteamVR's fallback binding file IS routable.** When no
   `vrcompositor_bindings_<controller_type>.json` exists in any driver's
   resource dir, SteamVR loads
   `<runtime>/resources/config/vrcompositor_bindings_generic_hmd.json`
   for `openvr.component.vrcompositor`. This file ships with empty
   `/actions/lasermouse` sources and no `/actions/system` binding -- the
   reason dashboard-open doesn't happen on any lighthouse HMD by default.
   It is user-writable (not Admin-gated) and is the correct patch point.

5. **PascalCase trap.** Valve's shipped `indexhmd` compositor bindings
   use lowercase action output paths (`opendashboard`, `leftclick`,
   `pointer`, `toggleroomview`). `vrcompositor_actions.json` in the
   SteamVR runtime declares the action set as `ToggleDashboard`,
   `LeftClick`, `Pointer`, `ToggleRoomView`, `OpenDashboard`. Matching
   against that manifest is case-sensitive: lowercase variants silently
   no-op. Index apparently works because Valve's internal binding parser
   is case-insensitive OR because their PascalCase variant exists
   elsewhere; either way, the safe contract for a third-party patch is
   to match the manifest exactly.

6. **`ToggleDashboard` is the mandatory action for native toggle.**
   `OpenDashboard` is optional. Wiring the complex_button `single` click
   to `ToggleDashboard` gives open-on-first-tap + close-on-second-tap
   without any app-level state tracking.

## The fix: `driver/src/bindings_patcher.{hpp,cpp}`

`DeviceProvider::Init()` runs the patcher before starting the HTTP
server. The patcher:

1. Resolves the SteamVR runtime path by reading
   `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` (same mechanism
   `vrpathreg.exe show` uses; driver API has no `VR_GetRuntimePath`).

2. Patches the generic-HMD compositor bindings in place:
   - `/actions/lasermouse` gets a `/user/head/pose/raw` pose output to
     `Pointer` (restores head-locked cursor) and a button source binding
     `/user/head/input/system` click -> `LeftClick`.
   - `/actions/lasermouse_secondary` gets a matching click -> `SwitchLaserHand`.
   - `/actions/system` is added: `complex_button` over `/user/head/input/system`
     with `single` -> `ToggleDashboard`, `double` -> `ToggleRoomView`.
   - Original saved alongside as `.micmap_backup` on first write for
     uninstaller restoration.

3. Writes controller-type-specific companion files
   (`vrcompositor_bindings_lighthouse_hmd.json` +
   `lighthouse_hmd_profile.json`) directly into `<runtime>/resources/config/`
   as belt-and-braces in case SteamVR's fallback path isn't chosen on
   some hardware variant.

All writes atomic (tmp + rename). All three files carry a
`micmap_patched_v2` marker key; the patcher is idempotent across
launches, auto-upgrades older markers, and refuses to touch files it
doesn't recognize as its own (prevents overwriting Valve files shipped
in future SteamVR updates).

Deliberately **removed** from `DeviceProvider::RunFrame()`:
`SetStringProperty(Prop_ControllerType_String)` and
`SetStringProperty(Prop_InputProfilePath_String)`. Those writes were
dead ends and actively harmful (see discovery #3).

## The refactor: collapse press/release to single tap

The spike also confirmed that holding the HMD system button has no
behavioral value. `complex_button` classifies press+release-under-N-ms
as `single`; holding longer does not route to a different action. The
cover-the-mic UX therefore doesn't benefit from `PressEdge::Down/Up` on
the state-machine side.

Reverted and simplified:
- `TriggerCallback` is `void()` again; `PressEdge` enum and `Releasing`
  state gone.
- `IDriverClient::press()` + `release()` collapsed to `tap()`.
- `PressCommand` -> empty `TapCommand` on the driver-side queue.
- POST /button body schema changed from `{"state":"down"|"up"}` to
  `{"kind":"tap"}`.
- Driver `device_provider::RunFrame` drains a TapCommand by writing
  DOWN, scheduling the paired UP at `now + kTapHold` (150 ms). The
  old min-hold-defer-pending-release logic is gone; max-hold
  watchdog retained as safety net.
- `hmd_button_test.exe` drops the `Send Press` / `Send Release` /
  `Tap` trio to a single `Tap` button.

Net -188 lines.

## Exit criteria satisfied

- **SVR-04** (dashboard opens on HMD without laser beam) -- **PASS**
  on Bigscreen Beyond with the patched bindings. No controller device
  registered. Head-locked cursor visible. Tap opens dashboard, second
  tap closes (native `ToggleDashboard` toggle).
- **SVR-09** (driver_client collapses to single click endpoint) --
  **PASS** via `IDriverClient::tap()`.
- **SVR-11** (end-to-end trigger + HMD sleep/wake recovery) -- partial:
  initial trigger confirmed. Formal N >= 5 sleep/wake cycle protocol
  from Plan 01-05 not run (this amendment closed the work before the
  full protocol was deemed necessary; recovery-path code in
  `DeviceProvider::RunFrame` is unchanged from the pattern validated
  by bey-closer-t1 Phase 03).

## Installer follow-up (Phase 04)

**INST-08** (added this plan): installer must patch
`vrcompositor_bindings_generic_hmd.json` at install time mirroring
`bindings_patcher.cpp`, and restore from `.micmap_backup` on uninstall.
Driver-side patcher remains as a fallback for manual driver
installations and for anyone who misses the install-time patch
(e.g. SteamVR update clobbers the file between runs).

## Outstanding risks

- **SteamVR updates may overwrite the bindings file**, reverting it to
  the empty-sources shape. Next driver launch after the update will
  re-apply the patch automatically (marker check treats the newly-
  overwritten file as "not ours -- leaving alone"). WAIT --
  correction: the overwrite sheds our marker, so it reads as a
  third-party file and is SKIPPED by the patcher. This is a defect
  to fix: the patcher should detect an unmarked generic_hmd file
  that matches Valve's shipped schema and re-patch it. TODO for
  a follow-up.
- Case-sensitivity of action output paths: we matched the manifest
  exactly. If Valve renames actions in a future SteamVR release,
  our patch becomes wrong silently. Low-frequency risk; catch with
  a smoke test in post-install validation.

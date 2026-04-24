---
phase: 03-auto-start
plan: 05
subsystem: steamvr
tags: [openvr, vr-input, quit-ack, auto-05, d-11, pitfall-2, openvr-1425, tdd, cpp17]

# Dependency graph
requires:
  - phase: 03-auto-start
    plan: 01
    provides: "tests/test_vr_input_quit_ordering.cpp RED scaffold + seam expectations (IVRSystemSeam + IEventSink pinned by stub overrides)"
provides:
  - "micmap::steamvr::IVRSystemSeam test seam (AcknowledgeQuit_Exiting)"
  - "micmap::steamvr::IEventSink test seam (notifyEvent(VREventType))"
  - "processVREventImpl(IVRSystemSeam&, IEventSink&, uint32_t) free function — ack-before-notify on VREvent_Quit, no-op on all other event types"
  - "OpenVRInput nested adapters (VRSystemAdapter, EventSinkAdapter) bridging production code to the free function"
affects:
  - "03-07 (MicMapApp shutdown orchestration) — ack is guaranteed to land BEFORE notifyEvent(Quit) fires MicMapApp::shutdown(), so Valve's 2s watchdog is already stopped when ordered teardown begins"
  - "Any future event handling on OpenVRInput::processVREvent now flows through the free function — adding a new event type means extending processVREventImpl (unit-testable), not the member"

tech-stack:
  added: []
  patterns:
    - "Free-function + thin-adapter seam pattern — production OpenVR surface (vr::IVRSystem*, typed callback) plugs into an OpenVR-free header via nested private adapter classes"
    - "uint32_t event-type parameter so the seam header carries zero OpenVR dependency — tests construct calls with literal values or vr::EVREventType casts without leaking <openvr.h> into the interface"
    - "Nested-private adapter classes (not anonymous-namespace locals) so EventSinkAdapter has access to OpenVRInput::notifyEvent without friend declarations"
    - "Distinct function name (processVREventImpl, not processVREvent) — eliminates any ambiguity between the new free function and the pre-existing OpenVRInput::processVREvent member at the delegation call site"

key-files:
  created:
    - src/steamvr/include/micmap/steamvr/vr_input_events.hpp
    - src/steamvr/src/vr_input_events.cpp
  modified:
    - src/steamvr/CMakeLists.txt
    - src/steamvr/src/vr_input.cpp
    - tests/test_vr_input_quit_ordering.cpp

key-decisions:
  - "Free function named processVREventImpl rather than processVREvent. Plan locked the rename in Task 2's 'Canonical decision' block; it keeps the delegation call-site unambiguous in the .cpp (where the OpenVRInput member and the free function would otherwise share a name at the same lookup scope). The committed RED test from Plan 01 (a12662b) used processVREvent, so this plan updated tests/test_vr_input_quit_ordering.cpp as part of Task 1 — the rename is a coordinated three-file change (.hpp/.cpp + test) captured in the same commit."
  - "Adapters are nested private classes of OpenVRInput, not anonymous-namespace free classes. Reason: EventSinkAdapter needs to call OpenVRInput::notifyEvent (protected member of the class hierarchy), and nesting grants implicit access without a friend declaration. VRSystemAdapter is nested for symmetry; it has no access requirement but co-location keeps the two adapters together."
  - "Header carries no OpenVR include. processVREventImpl takes uint32_t for eventType rather than vr::EVREventType. Reason: tests can instantiate StubVRSystemSeam/StubEventSink and call the free function without <openvr.h> in scope — and the test file uses vr::VREvent_Quit only for the cast-to-uint32_t at the call site, not inside the seam. The .cpp defines kVREventQuit from vr::VREvent_Quit when MICMAP_HAS_OPENVR is defined, and falls back to the literal 700u when building stubs."

patterns-established:
  - "Ack-first invariant for destructive lifecycle events: any operation whose failure would trigger a runtime force-kill should ack the watchdog BEFORE calling the user callback. VREvent_Quit is the canonical case; any future sibling (e.g. VREvent_ProcessForceDisconnected) would follow the same shape."
  - "Extract-to-free-function + thin-adapters as a way to unit-test logic that would otherwise require a live OpenVR runtime. Applied here; same pattern available for future Plan 03-07 (MicMapApp shutdown orchestration) if that teardown logic turns out to need its own unit-testable kernel."

requirements-completed: [AUTO-05]
# AUTO-05's ack-ordering mechanics are closed at the unit level by this plan.
# The end-to-end AUTO-05 story (ordered MicMapApp::shutdown() completing under
# the 2s watchdog ceiling, real SteamVR UAT) completes in Plan 03-07.

# Metrics
duration: ~10min (wall clock, 2 tasks with incremental rebuild)
completed: 2026-04-24
---

# Phase 03 Plan 05: processVREvent Extraction + Ack-First Ordering Summary

**AUTO-05 / D-11 ack-ordering invariant locked at the unit level: vr::IVRSystem::AcknowledgeQuit_Exiting is now called BEFORE the app's VREvent_Quit callback fires. Valve's 2-second quit watchdog is satisfied the instant the event is observed — downstream teardown latency (Plan 07 MicMapApp::shutdown) no longer races a force-kill.**

## Performance

- **Duration:** ~10 min wall clock (2 tasks, incremental MSBuild)
- **Completed:** 2026-04-24
- **Tasks:** 2 (TDD-style — Plan 01 RED test flipped GREEN by Task 1; Task 2 wires production code through the same free function)
- **Files created:** 2
- **Files modified:** 3

## Accomplishments

- Published `IVRSystemSeam` + `IEventSink` + free `processVREventImpl` as the testable kernel of VR event handling. Header carries zero OpenVR dependency (`uint32_t` event-type parameter) so test stubs construct without `<openvr.h>`.
- Locked the D-11 invariant in code: inside `processVREventImpl`, for `eventType == kVREventQuit`, the sequence is `system.AcknowledgeQuit_Exiting()` → `sink.notifyEvent(VREventType::Quit)`. No intervening statements; line 42 precedes line 43 in `vr_input_events.cpp`.
- Refactored `OpenVRInput::processVREvent(const vr::VREvent_t&)` to delegate through two nested private adapter classes:
  - `VRSystemAdapter` — forwards `AcknowledgeQuit_Exiting()` to `vr::IVRSystem*->AcknowledgeQuit_Exiting()` (null-safe — bails silently if `vrSystem_` was nulled mid-teardown).
  - `EventSinkAdapter` — forwards `notifyEvent(VREventType)` to the existing `OpenVRInput::notifyEvent` member.
  Production code now exercises the exact same branch test_vr_input_quit_ordering asserts on.
- CMake integration: added `src/vr_input_events.cpp` to `micmap_steamvr`'s `add_library(...)` source list alongside Plan 04's `manifest_registrar.cpp` — both entries preserved, order matches the `src/` file listing.
- Test rename: updated Plan 01's committed RED test (`tests/test_vr_input_quit_ordering.cpp`) from `svr::processVREvent` to `svr::processVREventImpl` — part of Task 1's coordinated rename per the plan's locked canonical decision.

## Task Commits

1. **Task 1 — vr_input_events.{hpp,cpp} + CMake wire-up + test rename:** `62fe4a5` (feat)
2. **Task 2 — OpenVRInput::processVREvent delegates via nested adapters:** `6b32b42` (refactor)

## GREEN Evidence

### test_vr_input_quit_ordering (after Task 1)

```
$ ctest --test-dir build -C Release -R test_vr_input_quit_ordering --output-on-failure
Test project C:/Users/decid/Documents/projects/mic-map/build
    Start 6: test_vr_input_quit_ordering
1/1 Test #6: test_vr_input_quit_ordering ......   Passed    0.02 sec

100% tests passed, 0 tests failed out of 1
```

Case 1 asserts `log[0] == "AcknowledgeQuit_Exiting()"` and `log[1] == "notifyEvent(Quit)"` — strict ordering. Case 2 dispatches `vr::VREvent_DashboardActivated` (`= 502`) and asserts no ack call appears in the log. Both pass.

### Full regression suite for Phase 3 (after Task 2)

```
$ ctest --test-dir build -C Release -R "vr_input|manifest" --output-on-failure
1/3 Test #5: test_manifest_registrar ..........   Passed    2.41 sec
2/3 Test #6: test_vr_input_quit_ordering ......   Passed    0.01 sec
3/3 Test #8: test_vrmanifest_schema ...........   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

`hmd_button_test` target (Phase 1 regression gate) builds warning-clean — MSBuild produced `build/bin/Release/hmd_button_test.exe` with no warnings on the changed translation units (`vr_input_events.cpp`, `vr_input.cpp`).

## Line-Number Proof of Ack-Before-Notify

`src/steamvr/src/vr_input_events.cpp`:

```
42:        system.AcknowledgeQuit_Exiting();
43:        sink.notifyEvent(VREventType::Quit);
```

Ack on line 42, notify on line 43. No branches or intervening statements. The test's call-log-ordering assertion (`log[0] == ack`, `log[1] == notify`) is a direct consequence of this source ordering plus the `return;` immediately after, which also ensures no accidental fall-through into a future `case` block added below the `if`.

`src/steamvr/src/vr_input.cpp` grep trace:

```
362:    class VRSystemAdapter : public IVRSystemSeam {
365:        void AcknowledgeQuit_Exiting() override {
369:            if (sys_) sys_->AcknowledgeQuit_Exiting();
375:    class EventSinkAdapter : public IEventSink {
377:        explicit EventSinkAdapter(OpenVRInput& self) : self_(self) {}
387:        VRSystemAdapter  sysAdapter(vrSystem_);
388:        EventSinkAdapter sinkAdapter(*this);
389:        processVREventImpl(sysAdapter, sinkAdapter,
```

Member `processVREvent` body is now four lines: construct `VRSystemAdapter`, construct `EventSinkAdapter`, call `processVREventImpl`, return. All switch-case logic lives in the unit-tested free function.

## Decisions Made

1. **Free function is `processVREventImpl`, not `processVREvent`.** The plan's Task 2 "Canonical decision" block locks this name. Benefits:
   - Eliminates any chance of unqualified-lookup ambiguity between member and free function at the delegation call site (`processVREventImpl(sysAdapter, sinkAdapter, ...)` in the member body).
   - Survives future changes: if someone adds another overload with the same argument list to `OpenVRInput`, the free function keeps its distinct identifier.
   - Cost: one coordinated rename in the committed RED test (`svr::processVREvent` → `svr::processVREventImpl`, 2 call sites). Rename done in the same commit as the .hpp/.cpp so history stays consistent.

2. **Nested private adapters over anonymous-namespace free classes.** `EventSinkAdapter::notifyEvent` forwards to `OpenVRInput::notifyEvent`, which is a `private` member of `OpenVRInput`. Nesting grants access without a `friend` declaration; an anonymous-namespace class would have needed `friend class EventSinkAdapter;` on `OpenVRInput`, which is more coupling than the pattern requires. `VRSystemAdapter` is nested for symmetry.

3. **Header carries no OpenVR dependency.** `processVREventImpl` takes `uint32_t` for `eventType` rather than `vr::EVREventType`. The `.cpp` synthesizes `kVREventQuit` from `vr::VREvent_Quit` when `MICMAP_HAS_OPENVR` is defined, falling back to the literal `700u` for stub-only builds. Test code remains OpenVR-free at the seam layer; only the test's own translation unit includes `<openvr.h>` for the `static_cast<uint32_t>(vr::VREvent_Quit)` call expression.

## Deviations from Plan

### Auto-fixed Issues

None — the plan's Task 1 and Task 2 prescriptions were followed verbatim once the `processVREventImpl` canonical name (locked by Task 2's amendment to Task 1) was applied consistently across all three files (.hpp, .cpp, test). The test rename from `svr::processVREvent` → `svr::processVREventImpl` was treated as part of Task 1 rather than Task 2 since it is a prerequisite for Task 1's GREEN criterion (`ctest -R test_vr_input_quit_ordering` exits 0); the plan's Task 2 "PREFERRED: rename ... in tests/test_vr_input_quit_ordering.cpp" instruction is honored with the same net effect.

### Not auto-fixed (intentional)

- The two remaining `notifyEvent(VREventType::SteamVRConnected)` and `notifyEvent(VREventType::SteamVRDisconnected)` calls in `OpenVRInput::initialize`/`shutdown` are NOT routed through `processVREventImpl`. They are not event-handler responses (they are synthesized app-level lifecycle callbacks), and piping them through a free function whose interface is `(seam, sink, uint32_t)` would require inventing synthetic event-type constants. Out of scope — D-11 covers `VREvent_Quit` ordering, not `SteamVRConnected/Disconnected` dispatch.
- `StubVRInput::pollEvents()` and `StubVRInput::notifyEvent()` remain unchanged. They do not exercise the free function because the stub has no OpenVR event source to process. Applicability gated on `#ifdef MICMAP_HAS_OPENVR`, matching the pre-refactor scope.

## Acceptance Criteria Status

**Task 1:**
- [x] `src/steamvr/include/micmap/steamvr/vr_input_events.hpp` exists; contains `class IVRSystemSeam`, `class IEventSink`, `void processVREventImpl(IVRSystemSeam&, IEventSink&, uint32_t)`.
- [x] `src/steamvr/src/vr_input_events.cpp` exists; line 42 contains `system.AcknowledgeQuit_Exiting();`, line 43 contains `sink.notifyEvent(VREventType::Quit);` — ack-line-number strictly less than notify-line-number.
- [x] `src/steamvr/CMakeLists.txt` lists `src/vr_input_events.cpp` in the `add_library(micmap_steamvr STATIC ...)` source list (alongside Plan 04's `src/manifest_registrar.cpp`, in source-order).
- [x] `ctest --test-dir build -C Release -R test_vr_input_quit_ordering` exits 0.

**Task 2:**
- [x] `src/steamvr/src/vr_input.cpp` member `processVREvent` delegates to the free `processVREventImpl` via adapters.
- [x] `grep -c AcknowledgeQuit_Exiting src/steamvr/src/vr_input.cpp` returns ≥ 1 (2 hits: override declaration at L365, forwarded call at L369).
- [x] `grep -c VRSystemAdapter src/steamvr/src/vr_input.cpp` returns ≥ 2 (3 hits at L362, L364, L387).
- [x] `grep -c EventSinkAdapter src/steamvr/src/vr_input.cpp` returns ≥ 2 (3 hits at L375, L377, L388).
- [x] `grep -c processVREventImpl` in vr_input_events.hpp ≥ 1, vr_input_events.cpp ≥ 1, vr_input.cpp ≥ 1.
- [x] `hmd_button_test` builds warning-clean (Phase 1 regression gate).
- [x] `ctest -R "vr_input|manifest"` exits 0 (3/3 PASS: test_manifest_registrar 2.41s, test_vr_input_quit_ordering 0.01s, test_vrmanifest_schema 0.01s).
- [x] `grep notifyEvent(VREventType::Quit) src/steamvr/src/vr_input.cpp` returns no direct match — the member `processVREvent` no longer contains the literal (delegation swallowed it into processVREventImpl). Only SteamVRConnected/Disconnected remain, at init/shutdown sites unrelated to Quit handling.

## Threat Flags

None. Threat register from PLAN.md `<threat_model>` covered:

- `T-03-05-01` (DoS — SteamVR force-kill if ack delayed, Pitfall 2): **mitigated** — ack now happens BEFORE the app callback in `processVREventImpl`. `test_vr_input_quit_ordering` locks the ordering as a call-log assertion; any regression that reverses the two lines fails the test.
- `T-03-05-02` (Tampering — malicious VREvent injection into vrserver queue): **accepted** — vrserver is the trusted producer; MicMap is a consumer. Out-of-scope (vrserver is the trust root).
- `T-03-05-03` (N/A — auth/session/access/crypto/input-validation): **N/A** — eventType is a `uint32_t` enum check; default branch no-op. No untrusted strings. Unchanged.

No new threat surface introduced.

## Issues Encountered

- Pre-existing MSBuild warnings unrelated to this plan (LIBCMT conflict on `micmap.exe` link surfaces in full-project builds); out of scope per scope-boundary rules. Already documented in Plan 04 SUMMARY § Issues Encountered.
- `processVREventImpl` canonical naming change required touching the Plan 01 RED test. Done in Task 1's commit; no orphan references in Plan 01's artifacts — the RED evidence in Plan 01 SUMMARY cites only link/compile failures, which are agnostic to the specific function name used at the call site.

## Self-Check: PASSED

- File `src/steamvr/include/micmap/steamvr/vr_input_events.hpp`: FOUND
- File `src/steamvr/src/vr_input_events.cpp`: FOUND
- File `src/steamvr/CMakeLists.txt`: FOUND (modified — vr_input_events.cpp added, Plan 04's manifest_registrar.cpp preserved)
- File `src/steamvr/src/vr_input.cpp`: FOUND (modified — delegation via nested adapters)
- File `tests/test_vr_input_quit_ordering.cpp`: FOUND (modified — rename processVREvent → processVREventImpl)
- Commit `62fe4a5`: FOUND in git log
- Commit `6b32b42`: FOUND in git log
- `ctest -R test_vr_input_quit_ordering`: PASS (0.02 sec)
- `ctest -R "vr_input|manifest"`: 3/3 PASS (test_manifest_registrar, test_vr_input_quit_ordering, test_vrmanifest_schema)
- `hmd_button_test.exe`: builds cleanly
- Ack-before-notify line ordering in vr_input_events.cpp: L42 (ack) < L43 (notify) ✓

## Next Phase Readiness

- AUTO-05 ack-ordering mechanics **closed at unit level**. The invariant is enforced both in production (`OpenVRInput::processVREvent` delegates through the same free function) and under test (`test_vr_input_quit_ordering`'s call-log assertion catches any regression).
- Plan 03-07 (MicMapApp shutdown orchestration) can proceed with the safe-by-construction assumption that Valve's 2-second quit watchdog is already stopped by the time `notifyEvent(VREventType::Quit)` propagates to `MicMapApp` and flips `running = false`. Ordered teardown (audio stop → detector reset → driverClient.disconnect → vrInput.shutdown → tray remove → ImGui/D3D destruction) can run to completion without a force-kill race.
- D-13 "no watchdog thread / no `TerminateProcess` fallback" is now correct by construction — the ack-first ordering makes an external watchdog redundant.
- Remaining Phase 3 work: Plan 03-06 (WinMain CLI fork + first-silent-launch balloon) is Wave 2. Plan 03-07 (MicMapApp shutdown + quiet retry thread) is Wave 3. Both can be planned against this SUMMARY's `provides` list without further coordination.

---
*Phase: 03-auto-start*
*Plan: 05*
*Completed: 2026-04-24*

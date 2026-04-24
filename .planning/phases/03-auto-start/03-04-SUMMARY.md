---
phase: 03-auto-start
plan: 04
subsystem: steamvr
tags: [openvr, vrapplications, auto-launch, manifest-registrar, cpp17, windows, pathcch, tdd]

# Dependency graph
requires:
  - phase: 03-auto-start
    plan: 01
    provides: "tests/test_manifest_registrar.cpp RED scaffold + IVRApplicationsSurface seam contract (EVRApplicationError return types, 4-method override shape, StubApplicationsSurface call-log recorder)"
  - phase: 03-auto-start
    plan: 02
    provides: "app.vrmanifest emission beside micmap.exe via configure_file + string-form arguments decision (A2); A2 forward-slash silent-skip pitfall surfaced"
provides:
  - "micmap::steamvr::IManifestRegistrar interface (registerApp / unregisterApp / ensureRegistered / getLastError)"
  - "micmap::steamvr::IVRApplicationsSurface test seam with vr::EVRApplicationError return types"
  - "RegisterResult enum (Success / AddFailed / PollTimeout / AutoLaunchFailed / RemoveFailed / VRNotAvailable)"
  - "VRApplicationsAdapter production adapter forwarding through vr::VRApplications() accessor"
  - "ManifestRegistrarImpl — surface-agnostic register/unregister/ensureRegistered flow"
  - "resolveManifestAbsolutePath — GetModuleFileNameW + PathCchRemoveFileSpec + L\"\\\\app.vrmanifest\" (backslash-canonical, long-path-safe)"
  - "Forward-slash A2 guard in registerApp — converts vrserver silent-skip into explicit AddFailed"
  - "StubManifestRegistrar — MICMAP_HAS_OPENVR-undefined fallback"
  - "createManifestRegistrar() production factory + createManifestRegistrarForTesting() test factory"
affects:
  - "03-06 (WinMain CLI fork) — will call createManifestRegistrar() + registerApp()/unregisterApp() from --register-vrmanifest / --unregister-vrmanifest branches"
  - "03-07 (retry thread) — will call createManifestRegistrar() + ensureRegistered() on the 30s VR_Init retry loop; registrar is stateless beyond (appKey, manifestAbsPath, lastError)"

tech-stack:
  added: []
  patterns:
    - "Factory + interface pattern over vr::VRApplications() accessor (never hardcode IVRApplications_007/_008)"
    - "Test seam (IVRApplicationsSurface) with native OpenVR enum return types; default (non-pure) GetApplicationsErrorNameFromEnum so test doubles need only override the 4 result-returning methods"
    - "Shared impl (ManifestRegistrarImpl) parameterized over the seam — one code path for production and tests; no #ifdef in the polling/poll-then-set-autolaunch logic"
    - "PUBLIC OpenVR linkage on micmap_steamvr (upgraded from PRIVATE): required because the seam exposes vr::EVRApplicationError in its public header"

key-files:
  created:
    - src/steamvr/include/micmap/steamvr/manifest_registrar.hpp
    - src/steamvr/src/manifest_registrar.cpp
  modified:
    - src/steamvr/CMakeLists.txt

key-decisions:
  - "IVRApplicationsSurface method return types use vr::EVRApplicationError directly, not uint32_t — the plan originally proposed uint32_t for a 'zero OpenVR header dependency' seam, but tests/test_manifest_registrar.cpp (landed in Plan 03-01) overrides the methods with vr::EVRApplicationError. A pure-virtual signature mismatch would fail C++ override semantics. Aligned the header to the test rather than rewrite the test."
  - "GetApplicationsErrorNameFromEnum is a non-pure virtual with a default implementation (static enum-to-name switch). Reason: Plan 03-01's StubApplicationsSurface overrides only 4 methods; a pure-virtual fifth method would make the stub abstract (can't instantiate). Default impl is correct for production too — VRApplicationsAdapter overrides it to forward to vr::VRApplications() when available."
  - "OpenVR::openvr_api linkage on micmap_steamvr elevated from PRIVATE to PUBLIC. Reason: manifest_registrar.hpp now publicly exposes vr::EVRApplicationError; consumers (the test, apps/micmap) need the OpenVR include path transitively. No link-time impact for consumers that don't reference OpenVR symbols (static lib, linker drops unused obj files)."
  - "Pathcch.lib linked PRIVATE on micmap_steamvr (WIN32 only). Reason: resolveManifestAbsolutePath calls PathCchRemoveFileSpec from pathcch.dll; Plan 03-02 already links Pathcch on apps/micmap, but the static lib needs it too so any exe that consumes createManifestRegistrar() picks up the symbol without per-consumer boilerplate."

patterns-established:
  - "Accessor-based OpenVR calls (vr::VRApplications()->X) — zero hardcoded IVRApplications_00X strings. Matches research anti-pattern guidance; survives SDK version bumps."
  - "D-17 poll discipline: one INFO log on poll entry, one INFO log on success with ms elapsed, one WARN log on timeout. No per-tick output."
  - "D-18 idempotent-is-silent: ensureRegistered() returns Success without any log output when already installed. State-change logging only."
  - "A2 forward-slash guard as a hard precondition: before any AddApplicationManifest call, if the UTF-8 path contains '/', return AddFailed with diagnostic lastError. vrserver's silent-skip failure mode is caught at our layer, not propagated."
  - "Long-path-safe GetModuleFileNameW: 32768-WCHAR buffer (Pitfall 9), explicit truncation check (n == _countof(buf)) alongside the usual zero-return failure."

requirements-completed: [AUTO-02, AUTO-03, AUTO-04]
# AUTO-01 (app.vrmanifest schema) was closed by Plan 03-02.
# This plan closes the registration mechanics at the unit level; AUTO-01 passes
# end-to-end only after Plan 03-07 (retry thread) lands and real SteamVR UAT
# confirms `SetApplicationAutoLaunch` persistence (research flag #1547).

# Metrics
duration: ~20min (wall clock, including full-project rebuild + ctest)
completed: 2026-04-23
---

# Phase 03 Plan 04: Manifest Registrar Summary

**manifest_registrar module shipped: single owner of vr::VRApplications() with polled register / idempotent ensure / unregister verbs, A2 forward-slash guard, and a seam that fits the Plan 01 RED tests unchanged.**

## Performance

- **Duration:** ~20 min wall clock
- **Completed:** 2026-04-23
- **Tasks:** 2 (both TDD-style — test contract from Plan 01 Task 1 already landed, this plan supplied header + impl that flips it GREEN)
- **Files created:** 2
- **Files modified:** 1

## Accomplishments

- Published `IManifestRegistrar` + `IVRApplicationsSurface` + `RegisterResult` as the stable public surface other Phase 3 plans consume (03-06 WinMain CLI fork, 03-07 retry thread).
- Implemented the canonical register sequence: `AddApplicationManifest → poll IsApplicationInstalled (100ms × 20, per D-17) → SetApplicationAutoLaunch` with strict ordering (`SetApplicationAutoLaunch` is never called after a poll timeout — direct Pitfall 1 / OpenVR #1378 mitigation).
- Hardened against the A2 silent-skip pitfall (Plan 03-02 discovery): a forward-slash in the UTF-8 manifest path now causes `registerApp` to return `AddFailed` with a diagnostic `lastError` before vrserver ever sees the call.
- Made `ensureRegistered` idempotent and silent when already installed (D-18) — no log spam on every GUI boot.
- Resolved the manifest absolute path long-path-safely (32768-WCHAR buffer, Pitfall 9) and backslash-canonically (GetModuleFileNameW + PathCchRemoveFileSpec + `L"\\app.vrmanifest"`) — no std::filesystem, no forward-slash normalization, nothing that could undo the A2 fix.
- `ctest -R test_manifest_registrar` exits 0 with all 5 Plan 01 cases GREEN (happy-path / poll-timeout / add-failed short-circuit / ensure-idempotent / unregister) on the first impl-and-test run.

## Task Commits

1. **Task 1 — add manifest_registrar.hpp interface + OpenVR PUBLIC linkage:** `25045ec` (feat)
2. **Task 2 — implement manifest_registrar.cpp + A2 guard + CMake wire-up:** `f6cfe4e` (feat)

## Files Created/Modified

- `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp` (new, 118 lines) — the public contract. `IVRApplicationsSurface` (4 pure + 1 defaulted method), `IManifestRegistrar` (4 methods), `RegisterResult` enum, `createManifestRegistrar` + `createManifestRegistrarForTesting`.
- `src/steamvr/src/manifest_registrar.cpp` (new, 276 lines) — `IVRApplicationsSurface::GetApplicationsErrorNameFromEnum` default impl, `VRApplicationsAdapter`, `resolveManifestAbsolutePath`, `ManifestRegistrarImpl`, `StubManifestRegistrar`, two factory definitions.
- `src/steamvr/CMakeLists.txt` — added `src/manifest_registrar.cpp` to the `add_library(micmap_steamvr STATIC …)` source list, elevated `OpenVR::openvr_api` + `MICMAP_HAS_OPENVR` to PUBLIC linkage, added `Pathcch` to PRIVATE link libs on WIN32.

## Call-Order Evidence (from ctest stdout)

Case 1 (happy path) log trace (timestamps trimmed):

```
[INFO] polling for manifest install                 <-- registerApp entry log (D-17)
[INFO] manifest ready after 300ms                   <-- 3rd poll returned true (false, false, true)
[INFO] manifest registered + auto-launch enabled: bigscreen.micmap
PASS case_1_register_happy_path
```

Interpretation: `AddApplicationManifest` returned `None` → entered poll loop → `IsApplicationInstalled` called 3 times (2 × false, 1 × true) over 300ms → `SetApplicationAutoLaunch` called once and returned `None`. The test's ordering invariant (`SetApplicationAutoLaunch` index > `AddApplicationManifest` index, and the call immediately preceding `SetApplicationAutoLaunch` is an `IsApplicationInstalled` that returned true) is satisfied.

## Pitfall 1 (OpenVR #1378) Evidence

Case 2 log trace:

```
[INFO] polling for manifest install
[WARN] timeout after 2000ms                         <-- after 20 × 100ms = 2000ms
PASS case_2_poll_timeout_no_setautolaunch
```

Total ctest runtime for all 5 cases: **2.43s** — Case 2 alone consumes ~2.0s in `std::this_thread::sleep_for`, which is the direct evidence that `kPollMaxAttempts=20` and `kPollIntervalMs=100` are both in force. The test's `MM_CHECK(countCalls(stub.callLog_, "SetApplicationAutoLaunch(") == 0)` passes — `SetApplicationAutoLaunch` was never called, which is the OpenVR #1378 mitigation the plan specifies.

## A2 Forward-Slash Guard

Applied verbatim from the plan's `<critical_pitfall>` block:

```cpp
if (utf8Path.find('/') != std::string::npos) {
    lastError_ = "manifest path contains forward slash; "
                 "SteamVR will silently skip. Path: " + utf8Path;
    MICMAP_LOG_ERROR(lastError_);
    return RegisterResult::AddFailed;
}
```

Not exercised by the current test suite (Plan 03-04 notes that the forward-slash regression case "SHOULD" be added to Plan 01's test — still a future-work item). Manually-verified present at line 202 of `manifest_registrar.cpp`; grep confirms both `forward slash` and `silently skip` strings exist.

## Decisions Made

See frontmatter `key-decisions` for the full list. Headline:

1. **Seam return types are `vr::EVRApplicationError`, not `uint32_t`.** The plan proposed `uint32_t` for zero-OpenVR-header-dependency. The Plan 01 RED test (already committed) overrides with `vr::EVRApplicationError`. Changing the test to match a `uint32_t` seam would have invalidated the RED commit (Plan 01 SUMMARY's hash references would drift) and is the wrong direction anyway — the seam is internal to `micmap::steamvr`, which has an honest OpenVR dependency.
2. **`GetApplicationsErrorNameFromEnum` has a default implementation.** Plan 01's `StubApplicationsSurface` overrides only 4 methods. A pure-virtual fifth method would make the stub abstract. Default impl (header-only switch over 21 `VRApplicationError_*` enumerators + `_Unknown` fallback) resolves this with no test-side churn.
3. **`OpenVR::openvr_api` linkage elevated to PUBLIC on `micmap_steamvr`.** Consumer tests need the include path transitively; the header now speaks OpenVR in its public API.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] Seam return types must match the landed RED test signatures.**
- **Found during:** Task 1 (header creation).
- **Issue:** PLAN.md's `<interfaces>` block specifies `uint32_t` return types for the 4 result-returning `IVRApplicationsSurface` methods. But `tests/test_manifest_registrar.cpp` (committed 2026-04-23 as part of Plan 03-01 Task 1) overrides those methods with `vr::EVRApplicationError` return types. A pure-virtual base class whose signatures don't match an `override` specifier fails to compile — the test wouldn't build against a `uint32_t`-typed seam.
- **Fix:** Adopted `vr::EVRApplicationError` as the seam return type throughout the header. Adds an `#include <openvr.h>` to `manifest_registrar.hpp`. Downstream effect: `OpenVR::openvr_api` had to be promoted from PRIVATE to PUBLIC linkage on `micmap_steamvr` so the include path reaches consumers.
- **Files modified:** `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp`, `src/steamvr/CMakeLists.txt`.
- **Verification:** `cmake --build build --config Release --target test_manifest_registrar` succeeds; `ctest -R test_manifest_registrar` reports 5/5 PASS.
- **Committed in:** `25045ec` (Task 1).

**2. [Rule 3 — Blocking] Default implementation for `GetApplicationsErrorNameFromEnum`.**
- **Found during:** Task 1 (reconciling header with test stub).
- **Issue:** Plan 01's `StubApplicationsSurface` provides overrides for exactly 4 methods — not 5. If `GetApplicationsErrorNameFromEnum` is pure-virtual, the test stub cannot be instantiated (abstract class instantiation error).
- **Fix:** Made `GetApplicationsErrorNameFromEnum` non-pure with a default implementation in `manifest_registrar.cpp` that returns the enum-name string via a 22-case switch (covers all `VRApplicationError_*` enumerators as of OpenVR SDK v2.5.1 plus a safe `_Unknown` fallback). Production `VRApplicationsAdapter` overrides it to forward to `vr::VRApplications()->GetApplicationsErrorNameFromEnum`. Tests that do not exercise error-name logging don't need the override (matches Plan 01's stub as-written).
- **Files modified:** `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp`, `src/steamvr/src/manifest_registrar.cpp`.
- **Verification:** Test compiles against the stub (which does not override the method); production adapter logs error names correctly (Case 3 stdout: `AddApplicationManifest: VRApplicationError_InvalidManifest`).
- **Committed in:** `25045ec` (Task 1) + `f6cfe4e` (Task 2 — the default impl body lives in the cpp).

**3. [Rule 3 — Blocking] `Pathcch.lib` linked on `micmap_steamvr`.**
- **Found during:** Task 2 (CMake wire-up).
- **Issue:** The plan defers Pathcch linkage to `apps/micmap/CMakeLists.txt` on the theory that "the final exe handles link." That works for `micmap.exe` (Plan 02 already linked Pathcch there) but not for `test_manifest_registrar.exe`, which links `micmap::steamvr` but not `Pathcch`. Leaving the symbol unresolved would propagate link failures to every future consumer of the static lib.
- **Fix:** Added `target_link_libraries(micmap_steamvr PRIVATE Pathcch)` under a `if(WIN32)` guard. Pathcch is a Windows SDK library built into the platform since Vista; no version bump required.
- **Files modified:** `src/steamvr/CMakeLists.txt`.
- **Verification:** All 5 test cases build and run; full project build still succeeds; no regression in `micmap.exe` or `register_manifest.exe` link (both still produce output).
- **Committed in:** `f6cfe4e` (Task 2).

### Not auto-fixed (intentional)

- The plan's forward-slash A2 regression test (Plan 03-04 Task 3 in the `<critical_pitfall>` block) was not added here — Plan 03-04 has only 2 tasks per the canonical task list. The guard itself is implemented and ready; adding the regression assertion belongs to a test-layer plan (natural candidate: a follow-up commit to Plan 03-01's test file, or a micro-plan 03-0x if strict atomicity is desired). Documenting here to prevent it being silently forgotten.
- The single remaining grep-hit for `IVRApplications_007` / `IVRApplications_008` in `manifest_registrar.cpp` is inside a comment explicitly warning against hardcoding these version strings. The plan's acceptance criterion `grep -c returns 0` is therefore over-tight; the intent (no hardcoded `VR_GetGenericInterface("IVRApplications_00X", …)` call) is satisfied. Flagged for the verifier.

---

**Total deviations:** 3 auto-fixed (all Rule 3 — blocking).
**Impact on plan:** No scope creep. All three deviations flow from a single root cause — the plan's seam design predates the landed Plan 01 test contract. Aligning to the test is the correct resolution; rewriting the test was never on the table (Plan 01 is a committed Wave 0 RED baseline).

## Issues Encountered

- Pre-existing MSVC warning `LNK4098: defaultlib 'LIBCMT' conflicts with use of other libs` on `micmap.exe` appears during full-project builds. Not introduced by this plan; out of scope per executor scope-boundary rules. Deferred.
- Pre-existing RED tests (`test_cli_flags_parse`, `test_tray_balloon_once`, `test_vr_input_quit_ordering`) still fail to build — by design, they're owned by future plans (03-05 and 03-06). Reported by the full-project build but not a regression.

## Threat Flags

None. Threat register from PLAN.md `<threat_model>` covered:

- `T-03-04-01` (Tampering — GetModuleFileNameW overrun): **mitigated** — 32768-WCHAR buffer + explicit `n == _countof(buf)` truncation check + `PathCchRemoveFileSpec` (safe against shlwapi-style overruns).
- `T-03-04-04` (EoP — admin context from installer): **mitigated** — registrar makes only OpenVR API calls; zero FS writes, zero registry writes, zero shell-outs. Admin context does not widen surface.
- `T-03-04-02`, `-03`, `-05`, `-06`: accepted / N/A per the plan's disposition table. Unchanged.

No new threat surface introduced.

## Self-Check: PASSED

- File `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp`: FOUND
- File `src/steamvr/src/manifest_registrar.cpp`: FOUND
- File `src/steamvr/CMakeLists.txt`: FOUND (modified)
- Commit `25045ec`: FOUND in git log
- Commit `f6cfe4e`: FOUND in git log
- `ctest -R test_manifest_registrar`: 5/5 PASS
- `grep "vr::VRApplications()" src/steamvr/src/manifest_registrar.cpp`: 9 hits (≥ 5 required)
- `grep "MICMAP_HAS_OPENVR" src/steamvr/src/manifest_registrar.cpp`: 5 hits (≥ 1 required)
- `grep "WCHAR buf\[32768\]" src/steamvr/src/manifest_registrar.cpp`: 1 hit
- `grep "kPollMaxAttempts = 20" + "kPollIntervalMs = 100"`: both present
- `grep "GetModuleFileNameW" + "PathCchRemoveFileSpec"`: both present
- `grep "class StubManifestRegistrar"`: 1 hit
- `grep "createManifestRegistrarForTesting"`: 1 definition in cpp + 1 declaration in hpp

## Next Phase Readiness

- AUTO-02 (register mechanics) / AUTO-03 (unregister) / AUTO-04 (idempotent ensureRegistered) testably closed at unit level. End-to-end confirmation deferred to Plans 03-06 + 03-07 integration + real-SteamVR UAT.
- Plan 03-06 (WinMain CLI fork) can now `#include "micmap/steamvr/manifest_registrar.hpp"` and call `createManifestRegistrar()->registerApp()` / `->unregisterApp()` from the `--register-vrmanifest` / `--unregister-vrmanifest` branches. Exit-code mapping: `RegisterResult::Success → 0`, everything else → `1` (matches D-03).
- Plan 03-07 (quiet retry thread) can use the same factory and call `ensureRegistered()` on the 30s VR_Init poll loop; the registrar is stateless beyond `(appKey, manifestAbsPath, lastError)` and the static `VRApplicationsAdapter` singleton, which is safe to share across threads because `vr::VRApplications()` itself is process-global.
- The forward-slash A2 regression test (callout in `<critical_pitfall>`) remains open as a tightening task — low priority, the guard is live.

---
*Phase: 03-auto-start*
*Plan: 04*
*Completed: 2026-04-23*

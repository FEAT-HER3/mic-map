# Phase 8 Deferred Items

## 08-00 Task 4 — pre-existing headless-build issue (not a regression)

**Discovered during:** cpp-httplib v0.14.3 -> v0.20.1 smoke verification

**Issue:** When configuring with `-DMICMAP_BUILD_DRIVER=OFF` and OpenVR SDK
absent, building `micmap_steamvr` (and any test that links it, e.g.
`test_vr_input_quit_ordering`) fails with `error C1083: Cannot open include
file: 'openvr.h'`. The four .cpp files under `src/steamvr/src/` and the
header `manifest_registrar.hpp` unconditionally `#include <openvr.h>`
even though `src/steamvr/CMakeLists.txt` advertises a "stub implementation
when OpenVR is absent" path.

**Status:** Pre-existing — same failure occurs in `main` repo's prior
build-headless directory (cpp-httplib v0.14.3) and in the fresh
build-headless-p8 directory (cpp-httplib v0.20.1). The bump did not
introduce this; it is unrelated to D-05.

**Out of scope for 08-00 Task 4** per the executor's scope-boundary rule
("only auto-fix issues DIRECTLY caused by the current task's changes").
The cpp-httplib bump's smoke obligation (D-06) covers the trigger-path
UAT — which lands in 08-06 — and the synchronous tests that pull cpp-httplib
into a headless TU (`test_command_queue`, `test_bindings_patcher`) PASS
under v0.20.1.

**Remediation candidate (future plan):** wrap the `#include <openvr.h>`
sites in `#ifdef MICMAP_HAS_OPENVR` so the headless config genuinely
builds. Or: gate `add_library(micmap_steamvr ...)` on `OpenVR_FOUND`
the way the test exes already are. Out of scope this phase; surface to
the planner if it blocks the 08-01 rename plan.

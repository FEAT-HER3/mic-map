# Phase 5: Shared Library Extraction - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-01
**Phase:** 05-shared-library-extraction
**Areas discussed:** CI guard strategy, micmap_lib disposition, cpp-httplib CVE bump, header facade
**Mode:** discuss (interactive); user delegated all four area decisions to Claude's best judgment

---

## CI guard strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Configure-time CMake transitive walk | Recursively crawl `INTERFACE_LINK_LIBRARIES` / `LINK_LIBRARIES` of `micmap_core_runtime`; fail at cmake configure on any `openvr_api` node or `*/openvr*` include dir | ✓ (primary) |
| Source-grep CI step | Fail CI on `<openvr.h>` / `<openvr_driver.h>` / `vr::` under `src/{audio,detection,core,common}/` | ✓ (secondary) |
| dumpbin /exports post-link assertion | Existing v1.5 driver-DLL export check (only `HmdDriverFactory` allowed) | ✓ (tertiary, downstream defense) |

**Selected:** stacked — configure-time CMake walk as primary, source-grep as secondary, existing dumpbin check stays as tertiary defense on the driver side.
**Notes:** Configure-time fail beats build-time fail beats CI-only — every developer sees it on first cmake run, not just on PR. The three layers catch different failure modes: linker walk catches transitive-link leaks, grep catches header-only / stray-include leaks, dumpbin catches accidents that survive both (e.g., a driver TU that re-exports a runtime symbol).

---

## micmap_lib disposition

| Option | Description | Selected |
|--------|-------------|----------|
| Rename + narrow | Replace `micmap_lib` with `micmap_core_runtime`, drop `micmap_steamvr` from aggregation | ✓ |
| Keep both side-by-side | Add new target alongside existing `micmap_lib`; consumers gradually migrate | |
| Deprecate `micmap_lib` | Keep as alias to new target; planned removal in later phase | |
| Split into core_runtime + app_runtime | Two INTERFACE targets, one with steamvr and one without | |

**Selected:** Replace. Delete `micmap_lib` entirely once consumers are switched.
**Notes:** Single source of truth, no parallel paths to drift. The current `micmap_lib` aggregates `micmap_steamvr` (the OpenVR boundary), which the LIB-01 spirit forbids — keeping it would actively undermine the new guard.

---

## cpp-httplib CVE bump (v0.14.3 → v0.20.1, CVE-2025-46728)

| Option | Description | Selected |
|--------|-------------|----------|
| Bundle in Phase 5 | Add as separate plan inside P5; ship lib extraction + CVE bump together | |
| Sequenced separate plan inside P5 | Lib extraction first, then a follow-up plan in the same phase for the bump | |
| Defer to Phase 8 (IPC Reshape) | Treat as Phase 8 prerequisite plan; carry in backlog until then | ✓ |
| Backlog only | No phase assignment; ship when convenient | |

**Selected:** Defer to Phase 8.
**Notes:** Phase 5 SC5 demands byte-identical v1.5 behavior. A wire-format-adjacent dep bump pollutes UAT correlation: if v1.5 trigger path regresses post-merge, was it the lib refactor or the http bump? Phase 8 reshapes IPC heavily and is the natural home. Reassign backlog item `CPP-HTTPLIB-BUMP` from "P5 standalone plan" to "P8 prerequisite plan".

---

## Header facade

| Option | Description | Selected |
|--------|-------------|----------|
| Pure transitive INTERFACE propagation | No new headers; sub-lib include dirs flow through new target | ✓ |
| Umbrella `runtime.hpp` | Single rooted entry point under `include/micmap_runtime/` | |
| Per-area facade headers (audio.hpp, detection.hpp, …) | One header per sub-lib | |

**Selected:** Pure transitive.
**Notes:** Source-movement is locked off (SUMMARY); umbrella headers imply API-surface decisions that belong to whatever phase actually consolidates the API (P7+). `micmap_bindings` precedent doesn't add a facade either — ships its own header. Keep P5 a pure plumbing change. Reconsider only if P7 finds the lack of facade hurts driver-side detection consumption.

---

## Claude's Discretion

User delegated all four areas to Claude's best judgment (see "user notes" on the multi-select question). Claude additionally locked the following downstream-consumer decisions inline (see CONTEXT.md `<decisions>`):

- mic_test linkage shape (D-08)
- micmap.exe linkage shape (D-09)
- driver linkage shape — link-only, no header consumption (D-10/D-11)
- macro hygiene assertion (D-12)
- header dllexport prohibition (D-13)
- nlohmann/json centralization deferral (D-16)

## Deferred Ideas

- cpp-httplib bump → Phase 8
- nlohmann/json serializer centralization → Phase 8
- LIB-04 logger sink wiring → Phase 8 (already roadmapped)
- Umbrella header → revisit Phase 7 if needed
- Headless canary target → Phase 7 (where the canaried path matures)

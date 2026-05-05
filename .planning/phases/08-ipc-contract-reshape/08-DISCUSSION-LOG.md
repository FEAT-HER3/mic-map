# Phase 8: IPC Contract Reshape - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-05
**Phase:** 08-ipc-contract-reshape
**Areas discussed:** JSON in driver, Config writer cutover, Client UI scope, cpp-httplib bump

User selected **"agent decides all using best judgment"** as the freeform response to the gray-area-selection question. Claude made all four calls with rationale below; each maps to one or more `D-NN` entries in CONTEXT.md.

---

## JSON in driver

| Option | Description | Selected |
|--------|-------------|----------|
| Pull full nlohmann/json | Match client; well-tested; same schema source-of-truth | ✓ |
| Hand-rolled minimal serializer | Avoid Pitfall 15 symbol bloat; minimal API surface | |

**Claude's call:** Full nlohmann/json into driver TUs (NOT into `micmap::core_runtime`). `cmake/AssertNoJsonInCore.cmake` lint enforces.
**Rationale:** Pitfall 15's three-binary cost reduces to one-binary cost when nlohmann is confined to driver TUs. mic_test stays clean. Hand-rolling a JSON parser for an HTTP-input → atomic-config-swap → ReplaceFileW codepath is a security-relevant risk that outweighs the symbol-bloat tradeoff. The deferral chain (P5 D-15/D-16, P6 D-02, P7 D-14) was always going to end here — IPC reshape is the natural lift point.
**Maps to:** D-01, D-02, D-03, D-04.

---

## Config writer cutover

| Option | Description | Selected |
|--------|-------------|----------|
| Atomic cutover (P8) | Client stops writing the moment PUT /settings lands | ✓ |
| Staged dual-writer (until P10) | Lower risk; both write until cutover phase | |

**Claude's call:** Atomic cutover. Client write path deleted in the same plan that wires `PUT /settings`.
**Rationale:** IPC-05 explicitly forbids dual-writer. Staged mode is two-test-matrix drift waiting to happen. Bootstrap race (client-before-driver) handled by gating settings UI on the driver-loaded indicator (HEALTH-01) — disabled with tooltip when red. UI control changes use optimistic in-memory client apply on PUT 200 so client-side detection (live until P10) sees the change immediately without dual-writer disk semantics.
**Maps to:** D-07, D-08, D-09, D-10.

---

## Client UI scope

| Option | Description | Selected |
|--------|-------------|----------|
| Replace controls + add health pane | Detection-config controls rewired to PUT /settings; new health pane | ✓ |
| Layer health pane only (controls untouched until P10) | Smaller diff; defers cutover risk | |

**Claude's call:** Add new driver-health pane (HEALTH-01..07) AND rewire existing detection-config controls to PUT /settings. Widgets stay; on-change handlers change. Device picker source switches to GET /devices.
**Rationale:** Layer-only mode would mean two paths — local config writes + PUT /settings — diverging client and driver behavior. Optimistic in-memory apply means widget-level UX stays atomic. The control widgets themselves don't need redesign in P8.
**Maps to:** D-11, D-12, D-13, D-26.

---

## cpp-httplib bump

| Option | Description | Selected |
|--------|-------------|----------|
| Plan 08-00 prereq (gating) | Isolated bump; smoke test before any new endpoint | ✓ |
| Roll into individual IPC plans | Avoids a "wave 0" delay | |

**Claude's call:** Plan `08-00` lands first, isolated. Smoke test = v1.5 trigger path UAT + Phase 6/7 flag-OFF regression. No new endpoints exercised at 08-00.
**Rationale:** cpp-httplib is in BOTH driver `HttpServer` AND client `IDriverApi` — bump regressions could land in either binary. CVE-2025-46728 is a deferred-twice patch; reckless to conflate "did the bump break X" with "did the new endpoint break X" during downstream debugging. One plan, one commit, one revert.
**Maps to:** D-05, D-06.

---

## Claude's Discretion

The user delegated the entire decision tree. Beyond the four headline calls above, additional decisions Claude made and recorded in CONTEXT.md `### Claude's Discretion` block:

- `FileLogSink` flush cadence (D-19)
- `ConsoleLogger` rename vs adapter strategy (D-19)
- `settings_validator.cpp` internal layout (D-15)
- `GET /devices` cache TTL (D-17 — chose 1 s as starting point)
- `MultiSinkLogger` mutex strategy (D-19)
- HEALTH-06 tray-poll floor (D-26)
- `IDriverApi` file-rename symmetry (D-22)
- `GET /settings` snapshot vs disk re-read (D-24 — chose snapshot)
- Validation error path style (JSON-Pointer vs dot-path) (D-15)

## Deferred Ideas

All deferred ideas from CONTEXT.md `<deferred>` block — these were either pre-existing deferrals from prior phase contexts or natural P10/P9 boundaries identified during analysis. None were user-introduced scope creep (the discussion was a single-turn agent-decides flow).

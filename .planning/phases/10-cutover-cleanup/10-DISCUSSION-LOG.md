# Phase 10: Cutover & Cleanup - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-10
**Phase:** 10-cutover-cleanup
**Areas discussed:** Cutover sequencing, Tray glyph language (HEALTH-08), FAIL UX surface, --debug-trigger + hmd_button_test (all delegated to Claude's best judgment)

---

## Selection turn

**Question:** Which Phase 10 gray areas to discuss for Cutover & Cleanup?

| Option | Description | Selected |
|--------|-------------|----------|
| Cutover sequencing | Single big rip-out plan vs staged | (delegated) |
| Tray glyph language (HEALTH-08) | Distinct .ico files vs overlay tint; pulse cadence; idle/cooldown color; poll wiring | (delegated) |
| FAIL UX surface (FAIL-01..05) | Pane pills vs banner vs modal vs balloon; multi-FAIL stacking; deep-link UX | (delegated) |
| --debug-trigger + hmd_button_test | TEST-02 endpoint mechanism; debug-build gating; TEST-05 retention vs collapse | (delegated) |

**User's choice:** "agent decides all using best judgment"

**Notes:** User delegated all four discussion areas plus adjacent decisions (log rotation TEST-03, installer co-versioning INST-09, lint additions, default flag flip mechanics, client-side detection death extent) to Claude. All decisions captured in CONTEXT.md `<decisions>` section as D-01 through D-27 across nine categories.

---

## Cutover sequencing — delegated

| Option | Description | Selected |
|--------|-------------|----------|
| Single atomic plan | One commit deletes /button + tap() + client audio body + flips flag + lints go live (mirrors P8 D-07 / P9 D-23) | ✓ |
| Staged across plans | Flag flip first, then route delete, then client body delete | |

**Claude's choice:** Single atomic plan (D-01).
**Rationale:** Dual-runtime mode is two-test-matrix drift waiting to happen — same logic that drove P8 single-writer cutover and P9 single-trainer cutover. Staged cutover would leave the codebase in an unshippable interim state for the duration of intermediate plans. Five distinct deletions plus the flag flip plus three lint go-lives all in one commit (Wave 5).

---

## Tray glyph language (HEALTH-08) — delegated

| Option | Description | Selected |
|--------|-------------|----------|
| Three distinct .ico resources | armed-green.ico / triggered.ico / error-red.ico, swapped via NIM_MODIFY | ✓ |
| Single icon with overlay/tint | Compose at runtime; smaller asset footprint | |
| Animated frame sequence | True per-frame pulse via a timer | |

**Claude's choice:** Three distinct `.ico` resources, single-icon-flash for pulse (D-04, D-05).
**Rationale:** Windows Shell tray API has no clean overlay primitive; per-icon-per-state is the simplest correct approach and reuses the existing tray-icon lifecycle from P3 / P9-04. Held 300ms triggered-icon flash is simpler than a frame-sequence timer; cooldown means the next poll typically shows `cooldown` or `idle` anyway. State derivation reuses the existing 1Hz `/health` + 2Hz `/state` poll wiring (no new poll).

---

## FAIL UX surface — delegated

| Option | Description | Selected |
|--------|-------------|----------|
| Inline pills in driver-health pane | Reuse P8 D-11 pane; topmost-priority pill at a time | ✓ |
| Full-window banner | Top-of-window strip independent of pane | |
| Modal dialog | Block UI on each FAIL | |
| Tray balloon | OS-level toast | |

**Claude's choice:** Inline pills, priority-stacked, in driver-health pane (D-07, D-08).
**Rationale:** The pane is already the canonical "system status at a glance" surface; modals interrupt and balloons get suppressed by Focus Assist. Single topmost pill at a time matches P8 D-16 `last_error` simplicity (no multi-error aggregation in v1.6). Priority order: FAIL-02 > FAIL-03 > FAIL-01 > FAIL-05; FAIL-04 handled at WinMain entry, never reaches the pane. Deep-links via `ShellExecuteW` (`ms-settings:privacy-microphone` for FAIL-01, `steam://rungameid/250820` for FAIL-02).

---

## --debug-trigger + hmd_button_test — delegated

| Option | Description | Selected |
|--------|-------------|----------|
| New POST /debug/trigger gated by debug build | Endpoint registered only in Debug; client CLI short-circuits via IDriverApi | ✓ |
| Reuse /button until P10 deletes it | Then need replacement | |
| CLI-only flag with separate replacement endpoint | TEST-02 lives off existing routes | |
| Collapse hmd_button_test into --debug-trigger | Single test surface | |
| Keep both (orthogonal layers) | hmd_button_test = OpenVR input; --debug-trigger = driver IPC | ✓ |

**Claude's choice:** New `POST /debug/trigger` debug-build-gated endpoint + retain `hmd_button_test.exe` (D-11, D-12, D-13).
**Rationale:** STATE.md's "open question about overlap" resolves to no-overlap — they exercise different layers. `hmd_button_test` creates its own `vr::IVRInput` context (OpenVR layer); `--debug-trigger` exercises the driver IPC + CommandQueue + RunFrame path. Different test surfaces; keep both. `MICMAP_DEBUG_BUILD` define is a new compile-time flag (cleaner than piggy-backing on `_DEBUG`/`NDEBUG`). `--debug-trigger` CLI mirrors P9-04 `tryRunReplayCli` short-circuit pattern from `apps/mic_test`.

---

## Adjacent decisions also captured (not surfaced as gray-area options but locked here)

- **Log rotation (TEST-03)** — synchronous size-check on every `FileLogSink::write()`; 5MB cap; 5 generations via `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`; applies to both driver and client log files (D-14..D-17).
- **INST-09 installer co-versioning** — `cmake/version.cmake` single source-of-truth; embedded in both binaries via `VS_VERSION_INFO` resource + compile-time `#define MICMAP_VERSION_STRING`; consumed by `installer/MicMap.iss` via generated `installer/version.iss`; `GET /health` gains `driver_version` field; mismatch is warn-only (D-18..D-22).
- **Three new CMake lints** — `AssertNoClientDetection` / `AssertNoButtonRoute` / `AssertCoVersioning` (D-23, D-24).
- **Default flag flip mechanics** — both `enable_driver_audio` and `enable_driver_detection` to `true` in same atomic cutover plan; on-rig install becomes the new shipped default; CLAUDE.md "Hardware rig" section to be updated (D-01, D-25, D-26).
- **Client-side detection death extent** — full rip including `audioCapture` / `detector` / `stateMachine` / FFT path / 3 `loadTrainingData` call sites; KissFFT no longer linked into client EXE; binary-size delta documented as part of cutover plan acceptance (D-01, D-03).
- **Wave layout** — eight waves: 10-00 RED scaffolds → 10-01 plumbing (logs + version) → 10-02 tray glyphs → 10-03 FAIL pills + driver_version → 10-04 --debug-trigger → 10-05 CUTOVER → 10-06 INST-09 installer wiring → 10-07 UAT (D-26, D-27).

---

## Claude's Discretion

Items where the planner has flexibility (documented in CONTEXT.md `<decisions>` "Claude's Discretion" subsection):

- FAIL-02 vs FAIL-03 disambiguation policy when both surface as ECONNREFUSED (recommend `tasklist` heuristic).
- Tray-icon `.ico` artwork sourcing (reuse existing `apps/micmap/micmap.ico` as base or create fresh artistic renderings).
- `IDriverApi::debugTrigger()` shape (virtual method with `#if` guards vs non-virtual free function in debug-build-only TU).
- Inno Setup `version.iss.in` template syntax (verify `configure_file` substitution at Wave 1).
- `MICMAP_DEBUG_BUILD` define mechanism (recommend new define driven by `CMAKE_BUILD_TYPE`).
- Pulse-icon implementation depth (single 300ms flash vs true frame sequence — recommend single flash).
- Version-mismatch pill placement (separate pill below FAIL-pill stack vs entry in same priority list — recommend separate).
- `--debug-trigger` parameterization (single-purpose vs `--debug-trigger=<mode>` — recommend single-purpose for v1.6).
- Disposition of `apps/micmap/main.cpp:300` startup `loadTrainingData` call (delete with body; if any UI surface still wants trained-profile name, route via `/state` field).

## Deferred Ideas

Captured in CONTEXT.md `<deferred>` section. Highlights:

- Removing `driver_detection_active` / `driver_training_active` `/health` fields post-cutover (Phase 11 or future cleanup).
- `OnDefaultDeviceChanged` follow-the-default + device pinning (Pitfall 14 — deferred indefinitely).
- `DeviceNotificationClient` ComPtr migration (CONCERNS.md backlog).
- FFT-on-every-frame perf cost (CONCERNS.md Performance Bottleneck #2 — backlog).
- Multi-error aggregation in FAIL pills; structured `last_error`; hard-block on version mismatch — all rejected for v1.6.
- HEALTH-D1/D2/D3, TRAIN-D2/D3, TEST-D2, OBS-01, DIST-01/02/03, DET-01/02, UX-01, UX-02 — all out-of-scope deferrals consistent with PROJECT.md.
- Removing `enable_driver_audio` / `enable_driver_detection` keys entirely (kept for emergency override).
- Parameterizing `--debug-trigger`.
- Inno Setup → MSIX / signed-installer migration; crash-dump capture — future distribution / observability milestones.

# Phase 2: Config Read-Back - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `02-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-04-22
**Phase:** 02-config-read-back
**Areas discussed:** Clamp ranges, Unicode / wstring, Write durability, Schema versioning

---

## Area selection

| Option | Description | Selected |
|--------|-------------|----------|
| Clamp ranges | Exact min/max for sensitivity, minDurationMs, cooldownMs, bufferSizeMs, fftSize; "sample rate" wording in CFG-03. | ✓ |
| Unicode / wstring | wstring ↔ UTF-8 round-trip; current writer's `c < 128` truncation bug. | ✓ |
| Write durability | Atomic writes; corruption-backup retention. | ✓ |
| Schema versioning | Behavior when on-disk `version` ≠ code version; unknown-key policy. | ✓ |

**User's choice:** All four.

---

## Clamp ranges

### Sensitivity range

| Option | Description | Selected |
|--------|-------------|----------|
| [0.0, 1.0] (Recommended) | Match the struct comment's documented range. Full span preserves existing slider semantics. | ✓ |
| [0.1, 0.95] | Exclude functionally-useless endpoints. | |
| [0.0, 1.0] exclusive of 0 | Forbid exactly 0.0 since that guarantees nonstop triggers. | |

**User's choice:** [0.0, 1.0].

### minDurationMs range

| Option | Description | Selected |
|--------|-------------|----------|
| [50, 5000] | Wide-open; trust the user. | |
| [100, 2000] (Recommended) | Practical sweet spot. Below 100ms = noise; above 2s = laggy. | ✓ |
| [200, 1000] | Tighter band around the 300ms default. | |

**User's choice:** [100, 2000].

### CFG-03 "sample rate" intent

| Option | Description | Selected |
|--------|-------------|----------|
| Typo — means bufferSizeMs (Recommended) | No new field; clamp bufferSizeMs instead. | ✓ |
| Add sampleRate field | Take CFG-03 literally; adds user-configurable sample rate. | |
| Drop from scope | Note as REQ text bug; don't clamp anything new. | |

**User's choice:** Typo — clamp bufferSizeMs. Fix REQUIREMENTS.md wording in Phase 5.

### fftSize out-of-range behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Snap to nearest power-of-2 in [512, 8192] (Recommended) | User 1500 → snap to 1024/2048 + warn. Honors both clamp spirit and hard pow-2 constraint. | ✓ |
| Clamp range, reject non-pow2 | Out-of-range → clamp; non-pow2 in range → default + warn. | |
| Fixed allowlist | Only {512, 1024, 2048, 4096, 8192}; anything else → default. | |

**User's choice:** Snap to nearest power-of-2 in [512, 8192].

### cooldownMs range

| Option | Description | Selected |
|--------|-------------|----------|
| [100, 2000] (Recommended) | Symmetric with minDurationMs; prevents double-fires without hanging user. | ✓ |
| [50, 5000] | Wider; power-user friendly. | |
| [150, 1000] | Tighter; no double-fires, less flexibility. | |

**User's choice:** [100, 2000].

### bufferSizeMs range

| Option | Description | Selected |
|--------|-------------|----------|
| [5, 100] (Recommended) | 5ms floor avoids thrashing audio thread; 100ms ceiling keeps latency sub-noticeable. | ✓ |
| [10, 50] | Tighter; sacrifices flexibility. | |
| [1, 500] | Effectively "anything"; 1ms may not be honored by WASAPI. | |

**User's choice:** [5, 100].

---

## Unicode / wstring

### Round-trip approach

| Option | Description | Selected |
|--------|-------------|----------|
| UTF-8 both ways (Recommended) | wstring ↔ UTF-8 at JSON boundary via WideCharToMultiByte / MultiByteToWideChar. Full fidelity, isolated Windows code. | ✓ |
| ASCII-only, document the limit | Keep lossy; warn instead of silent-truncate. Breaks real non-English device names. | |
| Store as UTF-8 std::string internally | Change AudioConfig to std::string; convert at WASAPI boundary. Bigger refactor. | |

**User's choice:** UTF-8 both ways at JSON boundary.

### Invalid UTF-8 policy on read

| Option | Description | Selected |
|--------|-------------|----------|
| Treat as field-level corruption (Recommended) | Malformed UTF-8 in a string field → warn + default that field; no whole-file backup. | ✓ |
| Whole-file corruption | Any UTF-8 error triggers CFG-02 backup-and-reset. | |
| Silent fallback to default | No warning; use default. Matches clamp philosophy but hides bugs. | |

**User's choice:** Field-level corruption.

---

## Write durability

### Atomic-save strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Temp file + ReplaceFile (Recommended) | Windows ReplaceFile atomically swaps; preserves ACLs, journals the swap. | ✓ |
| Temp file + rename | std::filesystem::rename; simpler, cross-platform. | |
| Direct write, accept risk | Keep current naive std::ofstream; rely on CFG-02 on torn writes. | |

**User's choice:** Temp file + ReplaceFile.

### Corruption-backup retention

| Option | Description | Selected |
|--------|-------------|----------|
| Keep last 5 (Recommended) | Prune oldest beyond 5 most recent. Bounded disk use, useful forensic trail. | ✓ |
| Unbounded | Never prune; user's problem. | |
| Prune by age (30 days) | Time-based self-cleaning; adds a time-based code path. | |

**User's choice:** Keep last 5.

---

## Schema versioning

### On-disk version ≠ code version

| Option | Description | Selected |
|--------|-------------|----------|
| Best-effort parse, log if mismatch (Recommended) | Ignore version for parsing; log info line if different. | ✓ |
| Strict match | Version mismatch → CFG-02 corruption path. | |
| Migration scaffold now | Add migrate(oldJson, fromVersion) switch even though v1 is the only version. | |

**User's choice:** Best-effort parse + info log on mismatch.

### Unknown keys on load

| Option | Description | Selected |
|--------|-------------|----------|
| Silently ignore on read, drop on write (Recommended) | Standard forward-compat; matches nlohmann/json natural flow. | ✓ |
| Silently ignore on read, preserve on write | Load as tree; let users stash notes. More code, niche benefit. | |
| Warn on unknown | Log per unknown field; noisy if users hand-edit. | |

**User's choice:** Silently ignore on read, drop on write.

---

## Claude's Discretion

- Exact location/signature of wchar ↔ UTF-8 helpers (anonymous namespace in config_manager.cpp vs. micmap::common utility).
- Whether the JSON reader is inline in `load()` or split into per-section helpers.
- Non-throwing `nlohmann::json::parse` + `.is_discarded()` vs. try/catch (CONVENTIONS.md prefers the former — no try/catch in the codebase).
- Exact wording of log lines for clamp / corruption / version-mismatch events.
- Whether to migrate the `saveDefault()` writer to nlohmann/json for CFG-04 parity (likely yes given UTF-8 requirement, but confirm during planning).

## Deferred Ideas

- UX-01 (auto-start toggle in UI) — already v2.
- Schema v2 migration harness — deferred until v2 schema actually exists.
- wstring → std::string refactor in AudioConfig — deferred; convert only at the JSON boundary.
- Preserving unknown user-added keys through write cycles — deferred.
- Fix CFG-03 "sample rate" wording — Phase 5 documentation.
- Enabling MICMAP_USE_GTEST globally — planner to decide.
- Config reload at runtime — no CFG requirement; not in scope.

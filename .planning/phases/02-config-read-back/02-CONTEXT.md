# Phase 2: Config Read-Back - Context

**Gathered:** 2026-04-22
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the stubbed JSON read-back at `src/core/src/config_manager.cpp:142` with a defensive `nlohmann/json` parser so user settings (audio device, detection duration, sensitivity, SteamVR options) persist across sessions. Corruption → backup the bad file + fall back to defaults + log warning. Out-of-range numeric fields → clamp + warning log. Write/read round-trip is identity across the full `AppConfig` struct.

In scope: read path, bounds validation, corruption recovery, write-path migration if needed for round-trip parity.
Out of scope (carried by other phases or milestones): settings UI changes, new config fields, the training data binary format, config schema migration tooling.

Parallel-safe with Phase 1 — no dependency either direction.

</domain>

<decisions>
## Implementation Decisions

### Clamp ranges (CFG-03)
- **D-01:** `detection.sensitivity` clamp range: **[0.0, 1.0]** inclusive. Matches the struct comment; don't narrow.
- **D-02:** `detection.minDurationMs` clamp range: **[100, 2000]**. Below 100ms false-fires on transients; above 2s feels laggy.
- **D-03:** `detection.cooldownMs` clamp range: **[100, 2000]**. Symmetric with minDurationMs; prevents double-fires from WASAPI buffer tails without hanging the user.
- **D-04:** `audio.bufferSizeMs` clamp range: **[5, 100]**. 5ms floor to avoid thrashing the audio thread; 100ms ceiling to keep detection latency sub-noticeable.
- **D-05:** `detection.fftSize` policy: **snap to nearest power-of-2 in [512, 8192]** on out-of-range or non-pow2 input, with a warning log. Honors both the CFG-03 clamp spirit and the power-of-2 hard requirement.
- **D-06:** **CFG-03's "sample rate" wording is a REQ typo — the clamped field is `audio.bufferSizeMs`.** No new `sampleRate` field is added to `AudioConfig` (WASAPI device determines sample rate; not user-configurable). Fix the REQUIREMENTS.md wording in Phase 5 (docs).

### Unicode / wstring handling (affects CFG-04)
- **D-07:** Current writer's `if (c < 128)` ASCII truncation is a **bug**, not the contract. Round-trip identity (CFG-04) requires fixing it.
- **D-08:** **UTF-8 at the JSON boundary, both directions.** `wstring ↔ UTF-8 std::string` via `WideCharToMultiByte(CP_UTF8, ...)` and `MultiByteToWideChar(CP_UTF8, ...)`. Keep `AudioConfig::deviceNamePattern` and `deviceId` as `std::wstring` in memory (no type change in the struct) — conversion is isolated to the serialize/deserialize boundary inside `config_manager.cpp`.
- **D-09:** Invalid UTF-8 in a single string field on read → **field-level default + warning log**, not a whole-file corruption trigger. Only top-level JSON parse errors engage the CFG-02 backup-and-reset flow.

### Write durability (defense-in-depth beyond CFG-02)
- **D-10:** **Atomic save via Windows `ReplaceFile`.** Write to `config.json.tmp`, then atomically swap. Preserves ACLs and journals the replace — Windows-native pattern. Non-Windows platforms remain stubbed consistent with the rest of the project.
- **D-11:** **Corruption-backup retention: keep last 5** `config.json.corrupted.YYYYMMDD-HHMMSS` files. On creating a new backup, prune any beyond the 5 most recent. Bounded disk use; useful forensic trail; self-maintaining.

### Schema versioning (contract for future drift)
- **D-12:** **Best-effort parse** — `version` field is read and compared against the code's version constant. If they differ, log an info-level line ("config written by version N, reading on version M") but proceed with normal parsing. Fields are read with `.value(key, default)` regardless of version.
- **D-13:** **No migration scaffolding this milestone.** Do not add a `migrate(oldJson, fromVersion)` switch. When v2 schema drift actually appears, add migration logic then.
- **D-14:** **Unknown keys in config.json**: silently ignored on read; dropped on write (next save emits only known fields). Standard forward-compat behavior, matches nlohmann/json's natural flow.

### Corruption handling policy (CFG-02)
- **D-15:** Whole-file corruption trigger = `nlohmann::json::parse_error` caught at the top-level parse call (syntactically invalid JSON: truncation, trailing comma, bad encoding at the top level). Recovery path: rename to `config.json.corrupted.YYYYMMDD-HHMMSS` → call `resetToDefaults()` → log warning → return `true` (load "succeeded" with defaults, so the app continues).
- **D-16:** First-run missing-file (no `config.json` exists yet) is **not corruption** — it's the expected cold-start path. Current behavior of leaving defaults in place on missing-file is correct; no backup created.

### Claude's Discretion
- Exact signature/location of the wchar↔UTF-8 helpers (free functions in the anonymous namespace vs. `micmap::common` utility — the project already uses anonymous namespaces for file-local helpers per CONVENTIONS.md).
- Exact JSON reader structure — inline in `load()` or split into per-section `readAudio(json)`, `readDetection(json)`, etc. Whatever keeps the function under the ~50-line soft ceiling the rest of the codebase uses.
- Whether to use `nlohmann::json::parse(..., nullptr, /*allow_exceptions=*/false)` + `.is_discarded()` vs. try/catch — CONVENTIONS.md flags "No try-catch blocks observed", so the non-throwing API is probably the right fit, but executor can decide.
- Specific log-message wording for the corruption / clamp / version-mismatch lines.
- Whether round-trip parity requires migrating the `saveDefault()` writer to nlohmann/json as well (CFG-04 says "if necessary"). Likely yes given D-08 (UTF-8) means the writer needs proper escaping too, but confirm during planning.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements
- `.planning/REQUIREMENTS.md` §"Config Persistence (CFG)" — CFG-01 through CFG-05, the falsifiable acceptance criteria.
- `.planning/ROADMAP.md` §"Phase 2: Config Read-Back" — success criteria and dependencies.

### Project decisions
- `.planning/PROJECT.md` §"Key Decisions" — "Fix JSON config read-back (was stubbed) using already-present nlohmann/json". §"Constraints" — tech stack is locked (nlohmann/json is vendored, no alternatives).

### Codebase context
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig`, `AudioConfig`, `DetectionConfig`, `SteamVRConfig`, `TrainingConfig`, `IConfigManager`. **Do not change struct field names or types** (other callers read these).
- `src/core/src/config_manager.cpp` — the file being modified. Current stubbed `load()` at line 126; hand-rolled lossy `toJson()` writer at line 48.
- `apps/micmap/main.cpp:171-172` and `:337` — only caller of `createConfigManager() / loadDefault() / saveDefault()`. Confirms no other entry points to worry about.
- `external/CMakeLists.txt:7-21` — nlohmann/json v3.11.2 is already vendored via FetchContent as an INTERFACE target named `nlohmann_json`. Phase 2 work is linking it, not adding it.
- `.planning/codebase/CONVENTIONS.md` — naming (camelCase methods, `_` suffix members, PascalCase types), return-code style (no exceptions, bool/Result returns), import ordering, `MICMAP_LOG_*` macros.
- `.planning/codebase/STRUCTURE.md`, `STACK.md`, `CONCERNS.md`, `TESTING.md` — general project conventions. Tests use CTest; GTest available via `MICMAP_USE_GTEST=ON` but not yet enabled.
- `src/common/include/micmap/common/logger.hpp` — `MICMAP_LOG_INFO / _WARNING / _ERROR` are the logging macros; writes to `std::cerr`.

### External reference
- `nlohmann/json` v3.11.2 docs — `parse()` with `allow_exceptions=false`, `.value(key, default)`, `.is_discarded()`, iteration over unknown keys. Standard API; no custom allocator or SAX work needed.

### Not applicable
- No external ADR or spec documents for this phase — requirements are fully captured in REQUIREMENTS.md + the decisions above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `MICMAP_LOG_WARNING`, `MICMAP_LOG_INFO`, `MICMAP_LOG_ERROR` — use these for all clamp / version-mismatch / corruption-recovery log lines. Framework already thread-safe.
- `getAppDataPath()` (anonymous namespace, `config_manager.cpp:25`) — reuse for locating corruption-backup filenames and temp-write target.
- `nlohmann_json` INTERFACE target — already available in the CMake graph; just add to `micmap_core`'s `target_link_libraries` (PRIVATE, since it's used only in the .cpp).
- `resetToDefaults()` already exists on the impl class — call this inside the corruption-recovery path.

### Established Patterns
- **Return-code error handling, no exceptions** (CONVENTIONS.md). Use `nlohmann::json::parse(..., nullptr, /*allow_exceptions=*/false)` + `.is_discarded()` check rather than try/catch. Matches project style and keeps `-Werror` clean without EH-dependent lint.
- **Interface + hidden impl** — `IConfigManager` stays stable; all changes are inside `ConfigManagerImpl`. No header changes anticipated.
- **Anonymous-namespace file-local helpers** — pattern already used for `getAppDataPath` and `toJson`; the UTF-8 helpers and new read-side helpers should go there too.
- **Doxygen `@brief/@param/@return` on public methods** — `load()` and `save()` keep their existing Doxygen blocks; update them if behavior docs change.
- **Factory returning `std::unique_ptr<IInterface>`** — unchanged; `createConfigManager()` stays.

### Integration Points
- `apps/micmap/main.cpp:172` calls `loadDefault()` at startup — the only read entry point. No branching needed; the impl owns the corruption-recovery flow.
- `apps/micmap/main.cpp:337` calls `saveDefault()` at shutdown — the only write entry point. Atomic-save behavior lands here.
- `CMakeLists.txt` for `micmap_core` — needs `target_link_libraries(... PRIVATE nlohmann_json)` added (verify during planning — may already be linked for writer's future use).
- Tests — `MICMAP_USE_GTEST=ON` is already a supported CMake option but not the default. Phase 2 is a strong candidate to flip it on and land real unit tests for the round-trip (CFG-04) and corruption (CFG-02) criteria. Planner should confirm.

</code_context>

<specifics>
## Specific Ideas

- "Defensive parser that tolerates corruption" (ROADMAP §Phase 2) — read as: every field access is guarded; no field absence crashes; no stray type mismatch crashes; only top-level syntax errors trigger the backup path.
- Backup filename format `config.json.corrupted.YYYYMMDD-HHMMSS` is exact (comes from REQUIREMENTS CFG-02 + ROADMAP success criteria #2). Use local time, zero-padded.
- CFG-04's "round-trip is identity" is the sharpest acceptance test in this phase — a unit test that writes `AppConfig{} via saveDefault()` then `loadDefault()` and asserts field-by-field equality across the full struct (including the wstring fields) is the cleanest way to prove it.
- The `std::optional<std::chrono::system_clock::time_point> lastTrainedTimestamp` round-trip needs care: `null` JSON → `std::nullopt`; an integer (current writer emits `time_t`) → `system_clock::from_time_t`. Sub-second precision is lost by design; round-trip is still identity at `time_t` resolution.

</specifics>

<deferred>
## Deferred Ideas

- **UX-01 (Auto-start toggle in the MicMap UI)** — already a v2 item in REQUIREMENTS.md, not in Phase 2 scope.
- **Schema v2 + migration harness** — punted per D-13. Will come when a real schema change appears.
- **Moving `AudioConfig` wstring → std::string** — considered and deferred. Keep the type as-is; convert at the JSON boundary only. Type surgery isn't justified for one serialization path.
- **Preserving unknown user-added keys through write cycles** — considered and deferred (D-14 drops them). If users start asking for "stash arbitrary notes in config.json", revisit.
- **Fixing the CFG-03 "sample rate" wording** — REQUIREMENTS.md text bug noted (D-06). Deferred to Phase 5 (Documentation), where REQUIREMENTS traceability already gets re-audited.
- **Enabling `MICMAP_USE_GTEST`** — strongly suggested during planning, but the framework-enablement decision is cross-phase; planner may choose to land tests with a lighter-weight placeholder harness if GTest adds scope.
- **Config-reload-while-running** — no CFG requirement. Current lifecycle is load-once at startup, save-once at shutdown. Not in scope.

</deferred>

---

*Phase: 02-config-read-back*
*Context gathered: 2026-04-22*

---
phase: 02-config-read-back
plan: 02
subsystem: config-persistence
tags: [cpp, config-persistence, json, nlohmann-json, win32, utf-8, atomic-write, green]
requirements: [CFG-01, CFG-02, CFG-03, CFG-04, CFG-05]
dependency_graph:
  requires:
    - 02-01 RED test scaffold (tests/test_config_manager.cpp, 5 scenarios)
    - nlohmann_json INTERFACE target linked PRIVATE into micmap_core (Plan 01)
    - src/core/include/micmap/core/config_manager.hpp (IConfigManager -- unchanged)
  provides:
    - Defensive nlohmann/json load path with top-level corruption detection
    - Type-safe per-field JSON readers (Pitfall R-1/R-2 safe)
    - UTF-8 wstring boundary via WideCharToMultiByte / MultiByteToWideChar
    - Clamp + pow2-snap bounds validation (CFG-03)
    - Corruption backup with YYYYMMDD-HHMMSS suffix + 5-file retention (D-11)
    - Atomic Windows save via ReplaceFileW + first-save MoveFileExW fallback (D-10)
    - GREEN ctest result flipping T-1..T-5 from RED
  affects:
    - .planning/phases/02-config-read-back/02-03-PLAN.md (hand-off: warnings audit + manual E2E)
    - apps/micmap/main.cpp (loadDefault/saveDefault semantics tightened; no API change)
tech_stack:
  added: []
  patterns:
    - Defensive parse (json::parse allow_exceptions=false + is_discarded guard)
    - Type-guarded readers (is_object/is_string/is_number_integer/is_boolean)
    - Anonymous-namespace file-local helpers (single block, no second ns)
    - Windows atomic write (temp file + ReplaceFileW, MoveFileExW first-save)
    - RAII-scoped ifstream to release share lock before rename
key_files:
  created: []
  modified:
    - src/core/src/config_manager.cpp
decisions:
  - "Inner-scoped ifstream in load() ensures Windows share lock is released before backupAndRotate calls fs::rename -- required to avoid ERROR_SHARING_VIOLATION on the Windows filesystem. Surfaced by T-2 test failure on first Task-2 run; Rule 1 auto-fix."
  - "writeAtomicWindows honors Pitfall R-3: REPLACEFILE_IGNORE_MERGE_ERRORS only; no REPLACEFILE_WRITE_THROUGH. Durability = temp file f.flush() + handle close (RAII dtor) BEFORE the swap. First-save path uses MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH which DOES support the write-through flag."
  - "Missing file in load() returns true (D-16) -- defaults already seeded by the ctor's resetToDefaults() call; changed from the previous MICMAP_LOG_WARNING + return false."
metrics:
  duration_minutes: 45
  tasks_completed: 2
  files_created: 0
  files_modified: 1
  commits: 2
  completed_date: "2026-04-23"
commits:
  - hash: ee9ca35
    message: "refactor(02-02): replace JsonValue/toJson with nlohmann/json infrastructure"
  - hash: d840277
    message: "feat(02-02): defensive JSON read + atomic Windows save -- T-1..T-5 GREEN"
---

# Phase 02 Plan 02: Defensive JSON Read + Atomic Save Summary

Stubbed JSON read-back at `src/core/src/config_manager.cpp:142` is gone. Replaced with a complete nlohmann/json load path (defensive parse + type-guarded readers + UTF-8 wstring boundary + clamp/pow2-snap bounds validation + corruption backup with `YYYYMMDD-HHMMSS` timestamp and 5-file retention pruning + atomic Windows save via `ReplaceFileW` with `MoveFileExW` first-save fallback). All five RED test scenarios from Plan 01 (T-1..T-5) are now GREEN; full suite (`test_placeholder`, `test_config_manager`, `test_command_queue`) is green.

## What Was Built

### `src/core/src/config_manager.cpp` -- 481 lines total (was 205)

**Deleted (~75 LOC):**
- `struct JsonValue` (old hand-rolled JSON value type, lines 36-45)
- `toJson()` (old lossy ASCII-truncating writer with `if (c < 128)` bug, lines 47-110)
- Stub comments (`// Simple JSON parsing/writing...`, `// In production, this would use nlohmann/json`, `// Very basic JSON writer`, `// TODO: Implement proper JSON parsing`, `// For now, keep defaults`)
- `<map>` include (no longer needed)


**Added inside the existing single anonymous namespace (~300 LOC):**

1. **`using json = nlohmann::json;`** -- file-local alias, does NOT leak into `micmap::core`.
2. **UTF-8 boundary (Recipe 4)** -- `toUtf8(std::wstring)` uses `WideCharToMultiByte(CP_UTF8, 0, ...)`; `fromUtf8(std::string, std::wstring&)` uses `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)`. Returns false on invalid UTF-8 so the caller can log + use default (D-09).
3. **Type-safe readers (Recipe 2)** -- `readInt`, `readFloat`, `readBool`, `readString`, `readWString`, `readSubObject`. Each uses `find() + is_*()` guards only; never calls `.at()` (Pitfall R-2) and never calls `value(key, default)` without prior type-check (Pitfall R-1).
4. **Bounds validators (Recipes 3 + 7)** -- `clampRange<T>` template for numeric ranges with warning log; `snapPowerOfTwo(value, lo, hi, name)` for fftSize -- first clamps into `[lo, hi]`, then snaps to nearest power of 2 (ties go lower) only if not already a valid pow2 in range.
5. **Per-section readers** -- `readAudio`, `readDetection`, `readSteamVR`, `readTraining`. Each pulls a sub-object via `readSubObject(root, "section")`, reads its fields through the typed helpers, and applies the clamp/snap ranges. `readTraining` handles the optional `int64_t` time_t with explicit `is_null` / `is_number_integer` branches.
6. **JSON writers (Recipe 5)** -- `audioToJson`, `detectionToJson`, `steamvrToJson`, `trainingToJson`, `appConfigToJson`. Use nlohmann's native `json j; j["key"] = value;` flow. wstring fields go through `toUtf8`. Empty `deviceId` and empty `customActionBinding` serialize as JSON `null`. `lastTrainedTimestamp` serializes as `int64_t` seconds-since-epoch or `null`.
7. **Corruption backup (Recipe 8)** -- `makeCorruptedSuffix()` builds the `YYYYMMDD-HHMMSS` timestamp via `localtime_s` + `std::put_time`. `backupAndRotate(configPath)` renames `config.json` to `config.json.corrupted.<timestamp>` using the `std::error_code` overload (no throws), then scans the parent directory, sorts matching backups in reverse lexicographic order, and `fs::remove` anything past the 5 newest (D-11).
8. **Atomic Windows save (Recipe 9)** -- `writeAtomicWindows(dest, utf8Content)` writes `dest.tmp` in an inner scope (RAII closes the handle on scope exit BEFORE the swap), then calls `ReplaceFileW` with `REPLACEFILE_IGNORE_MERGE_ERRORS` only (Pitfall R-3 honored -- the no-longer-supported WRITE_THROUGH flag is absent from the source). First-save branch (dest does not exist yet) falls back to `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` which DOES support write-through.

**Rewritten `ConfigManagerImpl` method bodies (~50 LOC):**

- **`load()`** -- inner-scoped `std::ifstream` (so the Windows share lock is released before `backupAndRotate` may rename the file; see Deviations). Missing file logs INFO + returns `true` (D-16 -- defaults already loaded by ctor). Reads the file into a local `content` string. Calls `json::parse(content, /*cb=*/nullptr, /*allow_exceptions=*/false)` + checks `is_discarded() || !is_object()` -- top-level corruption triggers `backupAndRotate(path) + resetToDefaults()` + returns `true` (D-15). Otherwise: reads `version` (best-effort, log only on mismatch per D-12) + calls the four per-section readers + logs `Loaded config from: ...` + returns `true`.
- **`save()`** -- Windows branch calls `writeAtomicWindows(path, appConfigToJson(config_).dump(4))`; non-Windows branch direct-writes to path using the existing log lexicon.

Header `src/core/include/micmap/core/config_manager.hpp` unchanged. Factory `createConfigManager()` unchanged. Private members (`config_`, `configDir_`) unchanged.

## Verification Results (All Acceptance Criteria)

### Task 1

| # | Check | Result |
|---|-------|--------|
| 1 | `#include <nlohmann/json.hpp>` present (1 match) | PASS (line 9) |
| 2 | `struct JsonValue` absent | PASS |
| 3 | `// In production, this would use nlohmann/json` absent | PASS |
| 4 | `// Very basic JSON writer` absent | PASS |
| 5 | `using json = nlohmann::json` present (1 match) | PASS (line 31) |
| 6 | `namespace {` single occurrence | PASS (1 match) |
| 7 | `} // anonymous namespace` single occurrence | PASS (1 match) |
| 8 | `WideCharToMultiByte` present | PASS (lines 46, 51) |
| 9 | `MultiByteToWideChar` with `MB_ERR_INVALID_CHARS` | PASS |
| 10 | `ReplaceFileW` present | PASS |
| 11 | `MoveFileExW` present | PASS |
| 12 | `REPLACEFILE_IGNORE_MERGE_ERRORS` present (1 match) | PASS |
| 13 | WRITE_THROUGH flag on ReplaceFile absent (Pitfall R-3) | PASS |
| 14 | `MOVEFILE_WRITE_THROUGH` present | PASS |
| 15 | `config.json.corrupted.` present | PASS |
| 16 | All recipe helpers present at definition | PASS (>=21 matches) |
| 17 | `wstring_convert` absent | PASS |
| 18 | `try {` / `catch (` absent (no exceptions) | PASS |
| 19 | `.at(` absent (Pitfall R-2) | PASS |
| 20 | `cmake --build build --target micmap_core` exits 0 | PASS |
| 21 | Task-1 stub marker removed at Task-2 commit (Warning-4) | PASS |

### Task 2

| # | Check | Result |
|---|-------|--------|
| 1 | `json::parse(content, ... allow_exceptions=false)` present | PASS (line 386) |
| 2 | `is_discarded` present | PASS (line 387) |
| 3 | `backupAndRotate(path)` present in load() | PASS (line 391) |
| 4 | `No config file at ` present (D-16 INFO) | PASS (line 378) |
| 5 | Old `Could not open config file: ` WARNING absent | PASS |
| 6 | `writeAtomicWindows(path, body)` present in save() | PASS (line 415) |
| 7 | Old `// TODO: Implement proper JSON parsing` absent | PASS |
| 8 | `Loaded config from: ` preserved | PASS (line 408) |
| 9 | `Saved config to: ` preserved | PASS (lines 418, 434) |
| 10 | `save() is stubbed during Plan 02 Task 1` marker removed | PASS |
| 11 | `cmake --build build --target test_config_manager` exits 0 | PASS |
| 12 | `ctest -R test_config_manager -C Debug` exits 0 | PASS |
| 13 | Full suite (`ctest --test-dir build -C Debug`) exits 0 | PASS |


### GREEN ctest Output

```
Test project C:/Users/decid/Documents/projects/mic-map/.claude/worktrees/agent-aa7a5541/build
    Start 1: test_placeholder
1/3 Test #1: test_placeholder .................   Passed    0.01 sec
    Start 2: test_config_manager
2/3 Test #2: test_config_manager ..............   Passed    0.02 sec
    Start 3: test_command_queue
3/3 Test #3: test_command_queue ...............   Passed    0.02 sec

100% tests passed, 0 tests failed out of 3
```

`test_config_manager` exercises all five RED scenarios from Plan 01:

- **T-1 round-trip identity** (CFG-04, CFG-05): Unicode `deviceNamePattern = L"Beyond(TM) Test"`, UNC `deviceId`, non-default numerics, `customActionBinding`, `lastTrainedTimestamp = from_time_t(1700000000)` all round-trip identical.
- **T-2 corruption -> backup + defaults** (CFG-02): trailing-comma JSON triggers `backupAndRotate` -> `config.json.corrupted.*` created -> defaults retained.
- **T-3 clamp + pow2-snap** (CFG-03): `bufferSizeMs=500` -> 100; `sensitivity=1.7` -> 1.0; `minDurationMs=50` -> 100; `cooldownMs=5000` -> 2000; `fftSize=3000` -> 2048 or 4096.
- **T-4 missing file != corruption** (D-16): no file -> `load()` true, defaults present, no backup created.
- **T-5 retention pruning** (D-11): 7 pre-seeded backups + corruption event -> exactly 5 remain after prune.

### File Metrics

- `src/core/src/config_manager.cpp`: 481 lines (was 205) -- +276 net, ~339 insertions, ~112 deletions across two commits.
- Zero changes to `src/core/include/micmap/core/config_manager.hpp`.
- Zero changes to `src/core/CMakeLists.txt` (Plan 01 already wired `nlohmann_json` PRIVATE).

## Commits

| Hash | Type | Task | Description |
|------|------|------|-------------|
| `ee9ca35` | refactor | 1 | Replace `JsonValue`/`toJson` with nlohmann/json infrastructure (helpers + writers + atomic-save + backup-rotate, `save()` stubbed per Warning-4) |
| `d840277` | feat     | 2 | Defensive JSON read + atomic Windows save -- T-1..T-5 GREEN |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Windows share-lock preventing corruption backup**

- **Found during:** Task 2 verification -- T-2 corruption scenario failed on first build with `ERROR_SHARING_VIOLATION` (`The process cannot access the file because it is being used by another process.`).
- **Issue:** The original Recipe 1 body for `load()` keeps the `std::ifstream file(path)` in scope through the corruption branch. On Windows `std::ifstream` holds a `GENERIC_READ | FILE_SHARE_READ` handle to the file until its destructor runs. `fs::rename(file_being_read, backup)` then fails with `std::errc::permission_denied` -- even though `fs::rename` uses the `error_code` overload, the rename physically cannot complete while the handle is open.
- **Fix:** Scoped the `std::ifstream` + buffer-slurp into an inner `{ }` block so the RAII dtor runs before `backupAndRotate(path)` is called. The local `std::string content` is declared in the outer scope and populated inside the inner block.
- **Files modified:** `src/core/src/config_manager.cpp` (load() body)
- **Commit:** `d840277` (folded into the Task 2 commit rather than a separate fix commit -- the two edits are one logical unit)
- **Rule 1 rationale:** Without this fix, the advertised D-15 recovery path (`backup + reset + return true`) silently fails on Windows -- `backupAndRotate` logs an ERROR, the backup is never created, T-2 fails. This is a correctness bug in the implementation, not a planning gap.

### Auth Gates

None -- no external service auth required.

### Architectural Changes

None -- Rule 4 did not trigger. No schema changes, no new library, no header changes, no CMake changes.

## Minor Implementation Notes

1. **Log-line wording drift from Recipe 3/7.** `clampRange` and `snapPowerOfTwo` use slightly shorter log messages than the RESEARCH recipes to keep the source tighter; every numeric clamp / snap still emits exactly one `MICMAP_LOG_WARNING` with the name, value, and chosen bound. Matches PATTERNS.md Pattern E's intent.
2. **Non-Windows `writeAtomicWindows` is stubbed `return false`** (consistent with the project's Windows-only milestone policy per CLAUDE.md). The non-Windows `save()` path uses a direct-write fallback in `ConfigManagerImpl::save()` itself. If any future platform work unstubs this, that is a separate task.
3. **LF -> CRLF warnings** during `git commit` are Git's `core.autocrlf` informational output -- not an error. The file is LF-only in the repo; git auto-handles the checkout conversion.
4. **Test binary `test_command_queue`** (from Wave 1 / SVR-05) required an explicit build before ctest could exercise it. The test file itself was already committed; only the build binary was stale. Not a regression.

## Known Stubs

None. The entire Phase 2 stub surface has been cleared. No placeholders, no TODO-for-Task-3, no hardcoded UI defaults, no mock data -- the file reads and writes real JSON and round-trips actual user settings.

## Threat Flags

None -- all surfaces introduced are covered by the plan's `<threat_model>` (T-2-02-01 through T-2-02-09) and their mitigations are in place.

## Hand-off to Plan 03

Plan 03 (if it exists -- check ROADMAP) can consume this as the GREEN baseline:

- **Build:** `cmake --build build --config Debug` -- entire `micmap_core` + all three test binaries build cleanly.
- **Tests:** `ctest --test-dir build --output-on-failure -C Debug` -- 3/3 pass.
- **Ready for:** M-1 manual end-to-end check (start app, change a setting, quit, restart, verify persisted); build-warnings audit on the new source lines (nothing emitted during our build, but `/W4` full-project audit is Plan 03 territory).
- **Pattern D (header + factory stability):** unchanged -- any downstream caller linking against `micmap_core` sees zero ABI change.

## Self-Check: PASSED

- **Files modified:** `src/core/src/config_manager.cpp` -- present at 481 lines with `#include <nlohmann/json.hpp>` and all recipe helpers.
- **Commits exist:** `ee9ca35`, `d840277` -- both in `git log --oneline`.
- **Header untouched:** `git diff src/core/include/micmap/core/config_manager.hpp` is empty.
- **CMake untouched:** `git diff src/core/CMakeLists.txt` is empty (Plan 01 did that wiring).
- **GREEN status:** `ctest -R test_config_manager -C Debug` exits 0; full suite 100% pass.
- **Forbidden patterns absent:** no `struct JsonValue`, no old TODO comments, no `.at()`, no `try/catch`, no `wstring_convert`, no unsupported `ReplaceFile` flag, no second anonymous namespace.

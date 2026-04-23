# Phase 2: Config Read-Back - Pattern Map

**Mapped:** 2026-04-22
**Files analyzed:** 4 (2 modify, 1 modify, 1 create; a second CMakeLists modify)
**Analogs found:** 4 / 4

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/core/src/config_manager.cpp` (MODIFY) | service (config persistence impl) | file-I/O (read + write, no streaming) | self (in-file `getAppDataPath` + existing `toJson`) + `src/common/src/logger.cpp` (put_time) | exact (same file) |
| `src/core/CMakeLists.txt` (MODIFY) | build config | — | `src/audio/CMakeLists.txt` lines 16-30 (existing `micmap_core` block with PUBLIC + WIN32 PRIVATE) | exact |
| `tests/test_config_manager.cpp` (CREATE) | test | file-I/O (round-trip, corruption fixture) | `tests/test_placeholder.cpp` | role-match (placeholder is the only existing test) |
| `tests/CMakeLists.txt` (MODIFY) | build config | — | `tests/CMakeLists.txt` lines 27-29 (existing `test_placeholder` registration) | exact |

**No header changes.** `src/core/include/micmap/core/config_manager.hpp` is explicitly stable per CONTEXT.md D-08 and the "Interface + private impl" pattern in CONVENTIONS.md. Do not touch.

## Pattern Assignments

---

### `src/core/src/config_manager.cpp` (MODIFY) — service, file-I/O

**Primary analog:** the same file (existing anonymous-namespace helper `getAppDataPath` + existing `toJson` function provide the style; both are being extended, one replaced).
**Secondary analog:** `src/common/src/logger.cpp` for the `localtime_s` / `std::put_time` timestamp pattern reused in the corruption-backup suffix.

#### Pattern A — Anonymous-namespace file-local helper style

Keep. Extend. Every new helper introduced this phase (`readInt`, `readFloat`, `readBool`, `readString`, `readWString`, `readSubObject`, `clampRange`, `snapPowerOfTwo`, `toUtf8`, `fromUtf8`, `makeCorruptedSuffix`, `backupAndRotate`, `writeAtomicWindows`, `audioToJson`, `detectionToJson`, `steamvrToJson`, `trainingToJson`, `appConfigToJson`, `readAudio`, `readDetection`, `readSteamVR`, `readTraining`) goes into the same anonymous namespace opened at line 23.

Existing precedent at `src/core/src/config_manager.cpp:23-34`:

```cpp
namespace {

std::filesystem::path getAppDataPath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / "MicMap";
    }
#endif
    // Fallback to current directory
    return std::filesystem::current_path() / ".micmap";
}
```

Copy this style:
- `#ifdef _WIN32` guarded Windows APIs with a stubbed/fallback `#else` branch (matches D-10's "non-Windows remains stubbed" policy).
- Plain free functions (no class wrapper), return-code style (no exceptions).
- Comments kept terse; Doxygen reserved for public-header declarations.

Helpers MUST live inside the existing `namespace { ... } // anonymous namespace` block (line 23 open, line 112 close in the current file). Do NOT open a second anonymous namespace. The closing comment `// anonymous namespace` at line 112 is the project's convention — preserve it.

#### Pattern B — Import / include ordering

Current top of `src/core/src/config_manager.cpp:6-16`:

```cpp
#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"

#include <fstream>
#include <sstream>
#include <map>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif
```

Matches CONVENTIONS.md §"Import Organization":
1. Module's own header
2. Related project headers
3. Standard library
4. Platform-specific gated by `#ifdef _WIN32`

Phase 2 additions slot in as:

```cpp
#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"

#include <nlohmann/json.hpp>          // NEW — group 2 (third-party that lives next to project headers)

#include <algorithm>                   // NEW — std::clamp, std::sort, std::greater
#include <chrono>                      // NEW — system_clock::to_time_t / from_time_t
#include <cstdint>                     // NEW — int64_t
#include <ctime>                       // NEW — std::tm, localtime_s
#include <fstream>
#include <iomanip>                     // NEW — std::put_time
#include <sstream>
#include <string>                      // NEW — std::string / wstring
#include <system_error>                // NEW — std::error_code
#include <vector>                      // NEW — retention listing
// existing <map> can be REMOVED — the JsonValue struct that used it is deleted

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>                    // existing
// No new Windows headers required; Windows.h pulls in ReplaceFileW, MoveFileExW,
// WideCharToMultiByte, MultiByteToWideChar, SHGetFolderPathW.
#endif
```

Do NOT use `<filesystem>` directly — it's already pulled through the header via `std::filesystem::path` in `IConfigManager`'s signatures. (The current file uses `std::filesystem` unqualified, confirming the transitive include.)

#### Pattern C — Delete the existing `toJson()` and `JsonValue` struct

Lines 36-110 of `src/core/src/config_manager.cpp` are the hand-rolled serializer + `JsonValue` struct. Both are being removed entirely:

- `JsonValue` struct (lines 37-45) — dead once nlohmann owns parsing.
- `toJson()` function (lines 47-110) — replaced by Recipe 5's `audioToJson` / `detectionToJson` / `steamvrToJson` / `trainingToJson` / `appConfigToJson` set.

The TODO comment at line 18-19 (`// In production, this would use nlohmann/json`) is load-bearing context — Phase 2 is literally "make this TODO happen". Delete the comment along with the struct.

The replacement lives inside the same anonymous namespace, per Pattern A. See RESEARCH.md §Recipe 5 for the exact contents.

#### Pattern D — `load()` / `save()` method body rewrite

The two method bodies at `src/core/src/config_manager.cpp:126-146` (load) and `:148-162` (save) are being replaced in-place inside `ConfigManagerImpl`. The method signatures, `override` markers, and enclosing class stay exactly as they are.

Existing `load()` skeleton to preserve:

```cpp
bool load(const std::filesystem::path& path) override {
    std::ifstream file(path);
    if (!file) {
        MICMAP_LOG_WARNING("Could not open config file: ", path.string());
        return false;
    }
    // ... parse ...
    return true;
}
```

Two behavior changes from the existing skeleton:

1. **Missing-file semantics flip.** Currently returns `false` with `MICMAP_LOG_WARNING`. Per D-16, missing file is not an error — return `true` with `MICMAP_LOG_INFO("No config file at ", path.string(), "; using defaults")`. The `configManager->loadDefault()` caller at `apps/micmap/main.cpp:172` already treats `false` as tolerable, but flipping to the new semantics matches D-16.
2. **Corruption branch returns `true`.** Per D-15, after `backupAndRotate` + `resetToDefaults`, return `true` so the app continues with defaults. This is the load-success-with-defaults contract.

Existing `save()` skeleton to preserve:

```cpp
bool save(const std::filesystem::path& path) override {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        MICMAP_LOG_ERROR("Could not open config file for writing: ", path.string());
        return false;
    }
    file << /*serialized body*/;
    MICMAP_LOG_INFO("Saved config to: ", path.string());
    return true;
}
```

Migrate body to Recipe 9 (`writeAtomicWindows`) while preserving the `MICMAP_LOG_ERROR` / `MICMAP_LOG_INFO` tone and the `create_directories` call. Non-Windows branch may keep the direct-write fallback unchanged.

#### Pattern E — Logging style (CRITICAL — re-used by every new helper)

Every new helper logs via the variadic fold-expression macros from `src/common/include/micmap/common/logger.hpp:123-128`:

```cpp
#define MICMAP_LOG_TRACE(...) ::micmap::common::Logger::trace(__VA_ARGS__)
#define MICMAP_LOG_DEBUG(...) ::micmap::common::Logger::debug(__VA_ARGS__)
#define MICMAP_LOG_INFO(...) ::micmap::common::Logger::info(__VA_ARGS__)
#define MICMAP_LOG_WARNING(...) ::micmap::common::Logger::warning(__VA_ARGS__)
#define MICMAP_LOG_ERROR(...) ::micmap::common::Logger::error(__VA_ARGS__)
#define MICMAP_LOG_FATAL(...) ::micmap::common::Logger::fatal(__VA_ARGS__)
```

Precedent usage in the current file, `src/core/src/config_manager.cpp:140, 154, 160, 182`:

```cpp
MICMAP_LOG_INFO("Loaded config from: ", path.string());
MICMAP_LOG_ERROR("Could not open config file for writing: ", path.string());
MICMAP_LOG_INFO("Saved config to: ", path.string());
MICMAP_LOG_DEBUG("Configuration reset to defaults");
```

Rules the executor must mirror:
- Pass multiple args; the logger's fold expression concatenates via `ostringstream`. Do NOT pre-concatenate with `+` or `<<`.
- Logger adds its own newline. Do not append `\n`.
- Use `WARNING` for clamp events (D-03..D-05 all require "warning log"), corruption recovery (D-15), invalid-UTF-8 field (D-09), and retention-prune failures.
- Use `INFO` for missing-file cold-start (D-16) and version-mismatch (D-12). Never `WARNING` for those.
- Use `ERROR` for save failures (temp-write, ReplaceFile/MoveFileEx).
- Use `DEBUG` only for diagnostic noise; do not emit DEBUG per-field-read or per-clamp (too noisy).

Example mapping for each CONTEXT.md decision → log macro:

| Event | Macro | Precedent call site |
|-------|-------|---------------------|
| D-12 version mismatch | `MICMAP_LOG_INFO` | — (new) |
| D-15 corruption detected | `MICMAP_LOG_WARNING` | — (new) |
| D-09 invalid UTF-8 field | `MICMAP_LOG_WARNING` | — (new) |
| D-03/D-04 clamp | `MICMAP_LOG_WARNING` | — (new) |
| D-05 pow2 snap | `MICMAP_LOG_WARNING` | — (new) |
| D-16 missing file | `MICMAP_LOG_INFO` | Replaces existing `MICMAP_LOG_WARNING` at line 129 |
| Save success | `MICMAP_LOG_INFO` | Existing line 160 — preserve verbatim |
| Save failure | `MICMAP_LOG_ERROR` | Existing line 154 — preserve tone |

#### Pattern F — Error handling: no try/catch, return-code

CONVENTIONS.md §"No Exception Handling" confirms: "No try-catch blocks observed; no exception specifications used; safe defaults returned on error."

Enforcement for Phase 2:
- Parse via `nlohmann::json::parse(content, /*cb=*/nullptr, /*allow_exceptions=*/false)` + `.is_discarded()` guard. Research §Recipe 1.
- Element access via `obj.find(key)` + `it->is_*()` type check + `it->get<T>()`. NEVER `obj.at(key)` (throws even in no-exception mode — RESEARCH Pitfall R-2) and NEVER bare `j.value(k, default)` (throws on type mismatch — RESEARCH Pitfall R-1).
- Filesystem ops via `std::error_code` overloads: `fs::rename(src, dst, ec)`, `fs::remove(p, ec)`, `fs::exists(p, ec)`, `fs::create_directories(p, ec)`, `fs::directory_iterator(p, ec)`. No filesystem function is called without the `ec` overload.

Precedent: the existing file already uses no-exception filesystem calls at line 150 (`std::filesystem::create_directories(path.parent_path())`) — note that call currently does NOT pass an `ec`. Phase 2 helpers should upgrade to the `ec` overload consistently in new code; the existing call can be left as-is or upgraded during the rewrite of `save()`.

#### Pattern G — Doxygen on public methods; none on file-local helpers

Header comment blocks in `src/core/include/micmap/core/config_manager.hpp:69-74` (for `load()`) and `:77-81` (for `save()`) are the Doxygen surface. Do not touch them unless behavior-doc changes are needed (D-16 "missing file returns true" is a behavior change — consider updating the `@return` line for `load()`).

File-local anonymous-namespace helpers do NOT get Doxygen blocks. Plain `//` comments for non-obvious bits (e.g. "D-09: field-level default on invalid UTF-8"). Precedent: `getAppDataPath` at line 25 has zero Doxygen.

#### Pattern H — Factory function and interface-impl split

`createConfigManager()` at line 202-204 and `ConfigManagerImpl : public IConfigManager` at line 117 stay completely unchanged. The factory returns `std::unique_ptr<IConfigManager>` matching CONVENTIONS.md §"Factory returning `std::unique_ptr<IInterface>`". Do not add constructors, do not add member variables beyond what's in `ConfigManagerImpl` today (lines 197-199: `AppConfig config_` + `std::filesystem::path configDir_`).

Interior of `ConfigManagerImpl` keeps these members verbatim:

```cpp
private:
    AppConfig config_;
    std::filesystem::path configDir_;
```

The member naming (`config_`, `configDir_` — camelCase + trailing underscore) matches CONVENTIONS.md.

---

### `src/core/CMakeLists.txt` (MODIFY) — build config

**Analog:** the existing file itself (exact match — just add one line to an existing `target_link_libraries` block) and `src/audio/CMakeLists.txt:16-30` for the PRIVATE-after-PUBLIC pattern.

Current `src/core/CMakeLists.txt:15-25`:

```cmake
target_link_libraries(micmap_core
    PUBLIC
        micmap_common
)

target_compile_features(micmap_core PUBLIC cxx_std_17)

# Windows-specific libraries for config paths
if(WIN32)
    target_link_libraries(micmap_core PRIVATE shell32)
endif()
```

Modify to add `nlohmann_json` as PRIVATE. Two acceptable shapes (executor picks one):

**Shape 1 (compact — merge into existing call):**

```cmake
target_link_libraries(micmap_core
    PUBLIC
        micmap_common
    PRIVATE
        nlohmann_json
)
```

**Shape 2 (separate PRIVATE call — matches `src/audio/CMakeLists.txt:25-30` platform-block style):**

```cmake
target_link_libraries(micmap_core
    PUBLIC
        micmap_common
)

target_link_libraries(micmap_core PRIVATE nlohmann_json)
```

Prefer Shape 1 for diff minimalism. No other changes to this file. The `shell32` platform block at line 23-25 stays exactly as-is.

**External target name verification:** `nlohmann_json` is the INTERFACE library name defined in `external/CMakeLists.txt:20` (`add_library(nlohmann_json INTERFACE)`). Do NOT use `nlohmann_json::nlohmann_json` or `nlohmann::json` — those namespaced aliases don't exist in this project.

---

### `tests/test_config_manager.cpp` (CREATE) — test, file-I/O

**Analog:** `tests/test_placeholder.cpp` (the only existing test).

Entire `tests/test_placeholder.cpp`:

```cpp
/**
 * @file test_placeholder.cpp
 * @brief Placeholder test file
 *
 * This is a minimal test that always passes, used to verify
 * the test infrastructure is working correctly.
 */

#include <iostream>

int main() {
    std::cout << "MicMap test placeholder - PASSED\n";
    return 0;
}
```

Patterns to copy into `test_config_manager.cpp`:

1. **File header Doxygen block** — `@file test_config_manager.cpp` + `@brief Round-trip, corruption, and clamp tests for ConfigManager`. Match the two-line style above.
2. **`int main()` returning 0 on all-pass / non-zero on failure.** No framework includes. No `TEST_F`. No `EXPECT_EQ`.
3. **`std::cout` on success, `std::cerr` on failure** (RESEARCH Recipe 10 uses `std::cerr << "FAIL: " ...`). Match the exact "PASSED" / "FAIL" token style so CTest failure output is grep-able.

The test file structure (the 5 scenarios T-1..T-5) is fully specified in RESEARCH §Recipe 10 and §"Test scenarios"; executor copies that recipe verbatim. Key cross-checks against the header:

- `AppConfig` default values referenced in T-3 / T-4 assertions must match `src/core/include/micmap/core/config_manager.hpp`:
  - `audio.bufferSizeMs = 10` (line 22)
  - `detection.sensitivity = 0.7f` (line 29)
  - `detection.minDurationMs = 300` (line 30)
  - `detection.cooldownMs = 300` (line 31)
  - `detection.fftSize = 2048` (line 32)
- Do not construct `ConfigManagerImpl` directly; use the factory `micmap::core::createConfigManager()` — it's the only exported handle (line 202 of config_manager.cpp; line 135 of the header).

**Test isolation pattern.** Every scenario uses `std::filesystem::temp_directory_path() / "micmap_test_config"` and calls `fs::remove_all(tmpDir)` at the start of each scoped block. This is essential because `ConfigManagerImpl`'s default constructor writes to `%APPDATA%\MicMap\` — tests MUST pass an explicit path to `load(path)` / `save(path)`, NOT call `loadDefault()` / `saveDefault()`, to keep the user's real config untouched.

---

### `tests/CMakeLists.txt` (MODIFY) — build config

**Analog:** the existing `test_placeholder` registration at lines 26-29 of the same file.

Current `tests/CMakeLists.txt:26-29`:

```cmake
# Placeholder test that always passes
add_executable(test_placeholder test_placeholder.cpp)
target_compile_features(test_placeholder PRIVATE cxx_std_17)
add_test(NAME test_placeholder COMMAND test_placeholder)
```

Copy pattern verbatim for `test_config_manager`, adding the `target_link_libraries` line the placeholder doesn't need (because the placeholder links nothing; ours needs `micmap::core` for `createConfigManager`):

```cmake
# Config manager round-trip, corruption, and clamp tests
add_executable(test_config_manager test_config_manager.cpp)
target_compile_features(test_config_manager PRIVATE cxx_std_17)
target_link_libraries(test_config_manager PRIVATE micmap::core)
add_test(NAME test_config_manager COMMAND test_config_manager)
```

**Alias target verification:** `micmap::core` is aliased at `src/core/CMakeLists.txt:28` (`add_library(micmap::core ALIAS micmap_core)`). Using the `::` alias matches the project's public naming convention (see `src/steamvr/CMakeLists.txt` / `src/audio/CMakeLists.txt` for similar aliases). Equivalently `micmap_core` works, but prefer the alias.

Do NOT flip `MICMAP_USE_GTEST` to ON. Leave the `option(...)` at line 8 and the inner `if(MICMAP_USE_GTEST)` block (lines 10-24) completely unchanged — CONTEXT.md's "Deferred Ideas" explicitly punts that cross-phase decision, and RESEARCH §"Testing Approach" confirms CTest-standalone is the right call for Phase 2.

Place the new block AFTER the existing `test_placeholder` registration (after line 29) to keep the placeholder as the "canary" test the whole repo can fall back on.

---

## Shared Patterns

### Anonymous-namespace file-local helpers

**Source:** `src/core/src/config_manager.cpp:23-34` (existing `getAppDataPath`)
**Apply to:** All new helpers in Phase 2 (UTF-8 converters, atomic-write, clamp, snap, read*, ...toJson, backupAndRotate, makeCorruptedSuffix).

```cpp
namespace {

std::filesystem::path getAppDataPath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / "MicMap";
    }
#endif
    return std::filesystem::current_path() / ".micmap";
}

// ... new helpers go here, in the SAME namespace block ...

} // anonymous namespace
```

### Windows-gated platform code with no-op/fallback else branch

**Source:** `src/core/src/config_manager.cpp:26-33` (Windows branch + current-path fallback in `getAppDataPath`).
**Apply to:** `writeAtomicWindows` (Recipe 9), `makeCorruptedSuffix` (Recipe 8's `localtime_s` branch).

Template:

```cpp
#ifdef _WIN32
// Windows-native implementation (ReplaceFileW, MoveFileExW, localtime_s).
#else
// Stubbed / portable fallback. Per project policy (CLAUDE.md: "Windows-only
// milestone"), non-Windows is a second-class citizen.
#endif
```

Matches CONTEXT.md D-10 ("Non-Windows platforms remain stubbed consistent with the rest of the project").

### `std::put_time` timestamp formatting

**Source:** `src/common/src/logger.cpp:38-51` (manual formatting style with `localtime_s` + `std::setw`/`std::setfill`).
**Apply to:** `makeCorruptedSuffix` in Recipe 8 — but NOT the manual-digit style; use `std::put_time` instead because it's cleaner for a `YYYYMMDD-HHMMSS` literal.

Precedent `localtime_s` pattern from `logger.cpp:38-43`:

```cpp
std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
```

Copy this exact idiom for the corruption-backup timestamp (RESEARCH Recipe 8 lines 544-549 match this structure).

### Return-code error style (no try/catch, `std::error_code` for filesystem)

**Source:** Project-wide per CONVENTIONS.md §"Error Handling". Local precedent at `src/core/src/config_manager.cpp:128-131, 153-156` (guard-return on stream-open failure).
**Apply to:** Every new function. Non-negotiable.

Template:

```cpp
bool operationName(const Input& in, Output& out) {
    if (!validate(in)) {
        MICMAP_LOG_WARNING("operationName: invalid input");
        return false;
    }
    std::error_code ec;
    some_fs_call(args, ec);
    if (ec) {
        MICMAP_LOG_ERROR("operationName: ", ec.message());
        return false;
    }
    // ...
    return true;
}
```

### CTest-standalone test harness

**Source:** `tests/test_placeholder.cpp` (entire file).
**Apply to:** `tests/test_config_manager.cpp`.

The `MM_CHECK` macro from RESEARCH Recipe 10 is new project surface, but it's test-file-local (not shared with production). Define it at the top of `test_config_manager.cpp`; do not promote it to `tests/` shared headers this phase.

```cpp
#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)
```

Matches the existing "`main()` returns 0 on pass / non-zero on fail, `std::cerr` for failure token, `std::cout` for success token" contract.

### Interface + hidden impl + factory

**Source:** `src/core/include/micmap/core/config_manager.hpp:65-135` (public `IConfigManager`, `createConfigManager()` factory) paired with `src/core/src/config_manager.cpp:117-204` (`ConfigManagerImpl`, factory impl).
**Apply to:** CONFIRM the planner preserves this contract — no header changes, no new interface methods, no renamed factory. The whole Phase 2 rewrite stays inside `ConfigManagerImpl`'s `load()` and `save()` bodies + the anonymous namespace. Every other caller (`apps/micmap/main.cpp:171-172,337`) must compile and link unchanged.

Stability check for the planner:
- `IConfigManager`'s pure-virtual surface: unchanged.
- `createConfigManager()` signature: unchanged.
- `ConfigManagerImpl`'s ctor, dtor, and public overrides: unchanged.
- `ConfigManagerImpl`'s private members `config_` / `configDir_`: unchanged.
- Only the bodies of `load()` and `save()` + the anonymous namespace content change.

## No Analog Found

None. Every file in Phase 2 has a concrete in-project analog:

- Parsing/serialization helpers — pattern drawn from the file itself (`getAppDataPath`) and `logger.cpp` (timestamp formatting).
- CMake wiring — drawn from `src/audio/CMakeLists.txt` and the file itself.
- Test — drawn from `tests/test_placeholder.cpp`.
- Test registration — drawn from the same `tests/CMakeLists.txt`.

The "atomic Windows save" helper (`writeAtomicWindows`) has no prior in-project use of `ReplaceFileW` / `MoveFileExW`, but the *style* (anonymous-namespace, `#ifdef _WIN32`, return-code, variadic-macro logging) is fully established. The executor should follow RESEARCH §Recipe 9 for the API calls and this PATTERNS.md for the surrounding conventions.

## Metadata

**Analog search scope:** `src/core/`, `src/common/`, `src/audio/`, `tests/`, `external/`, all `CMakeLists.txt` in the repo.
**Files scanned:** 12 (via Glob + Grep + direct Read).
**Files read in full:** `src/core/src/config_manager.cpp`, `src/core/include/micmap/core/config_manager.hpp`, `src/core/CMakeLists.txt`, `src/common/src/logger.cpp`, `src/common/include/micmap/common/logger.hpp`, `tests/test_placeholder.cpp`, `tests/CMakeLists.txt`, `external/CMakeLists.txt`.
**Files grep-scanned:** every `CMakeLists.txt` under the repo for `target_link_libraries` usage.
**Pattern extraction date:** 2026-04-22.

## PATTERN MAPPING COMPLETE

**Phase:** 2 — Config Read-Back
**Files classified:** 4
**Analogs found:** 4 / 4

### Coverage
- Files with exact analog: 3 (`config_manager.cpp` — self + logger.cpp; both `CMakeLists.txt` — self/siblings)
- Files with role-match analog: 1 (`test_config_manager.cpp` — matches `test_placeholder.cpp` role; scope is larger but style is identical)
- Files with no analog: 0

### Key Patterns Identified
- All new helpers live inside the existing anonymous namespace in `config_manager.cpp` (reuse the `getAppDataPath` precedent; do not open a second namespace).
- Windows-gated code uses `#ifdef _WIN32` / `#else` fallback, matching `getAppDataPath` and `logger.cpp`'s `localtime_s` pattern; non-Windows branches stay stubbed per CLAUDE.md's Windows-only milestone policy.
- Return-code error style is non-negotiable (CONVENTIONS.md §"No Exception Handling"); every filesystem call uses the `std::error_code` overload and every `nlohmann::json` call uses `allow_exceptions=false` + `.is_discarded()` / `.find()` / `.is_*()` guards.
- Variadic `MICMAP_LOG_*` macros are the only logging surface; multi-arg fold-expression concatenation matches every existing call in `config_manager.cpp` and `logger.cpp` — no `+`, no `<<` pre-concatenation.
- `IConfigManager` interface and `createConfigManager()` factory are stable; all Phase 2 changes stay inside `ConfigManagerImpl`'s method bodies and the anonymous namespace.
- Tests follow `test_placeholder.cpp`: standalone `int main()`, `std::cout` for pass token, `std::cerr` for failure token, exit code 0/non-0, registered via `add_executable` + `target_compile_features(cxx_std_17)` + `target_link_libraries(PRIVATE micmap::core)` + `add_test`. Do not flip `MICMAP_USE_GTEST`.

### File Created
`.planning/phases/02-config-read-back/02-PATTERNS.md`

### Ready for Planning
Pattern mapping complete. Planner can now reference analog patterns in PLAN.md files.

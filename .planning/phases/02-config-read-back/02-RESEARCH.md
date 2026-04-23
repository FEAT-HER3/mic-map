# Phase 2: Config Read-Back - Research

**Researched:** 2026-04-22
**Domain:** C++17 JSON persistence + Windows file atomicity + UTF-8 boundary conversion
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Clamp ranges (CFG-03)**
- D-01: `detection.sensitivity` clamp range: **[0.0, 1.0]** inclusive.
- D-02: `detection.minDurationMs` clamp range: **[100, 2000]**.
- D-03: `detection.cooldownMs` clamp range: **[100, 2000]**.
- D-04: `audio.bufferSizeMs` clamp range: **[5, 100]**.
- D-05: `detection.fftSize`: **snap to nearest power-of-2 in [512, 8192]** on out-of-range or non-pow2 input, with a warning log.
- D-06: **CFG-03's "sample rate" wording is a REQ typo — the clamped field is `audio.bufferSizeMs`.** No new `sampleRate` field. Deferred to Phase 5.

**Unicode / wstring handling (CFG-04)**
- D-07: Current writer's `if (c < 128)` ASCII truncation is a **bug**; round-trip identity requires fixing it.
- D-08: **UTF-8 at the JSON boundary, both directions.** `wstring ↔ UTF-8 std::string` via `WideCharToMultiByte(CP_UTF8, …)` and `MultiByteToWideChar(CP_UTF8, …)`. Keep `AudioConfig::deviceNamePattern` and `deviceId` as `std::wstring` in memory; convert only at serialize/deserialize boundary.
- D-09: Invalid UTF-8 in a single string field on read → **field-level default + warning log**, not a whole-file corruption trigger. Only top-level JSON parse errors engage the CFG-02 backup-and-reset flow.

**Write durability (beyond CFG-02)**
- D-10: **Atomic save via Windows `ReplaceFile`.** Write to `config.json.tmp`, then atomically swap. Non-Windows platforms remain stubbed.
- D-11: **Corruption-backup retention: keep last 5** `config.json.corrupted.YYYYMMDD-HHMMSS` files; prune beyond.

**Schema versioning**
- D-12: **Best-effort parse** — `version` field is read and compared to the code's version constant. Mismatch → info-level log line, continue parsing.
- D-13: **No migration scaffolding this milestone.**
- D-14: **Unknown keys**: silently ignored on read; dropped on write.

**Corruption handling (CFG-02)**
- D-15: Whole-file corruption trigger = top-level JSON parse failure (detected via `.is_discarded()` with `allow_exceptions=false`). Recovery: rename → `config.json.corrupted.YYYYMMDD-HHMMSS` → `resetToDefaults()` → warning log → return `true`.
- D-16: Missing file (first run) is **not corruption** — no backup created; defaults retained.

### Claude's Discretion
- Exact signature/location of the wchar↔UTF-8 helpers (anonymous namespace in `config_manager.cpp` vs `micmap::common` utility).
- Exact JSON reader structure — inline in `load()` or split into per-section helpers.
- Specific log-message wording for corruption / clamp / version-mismatch lines.
- Whether round-trip parity requires migrating `saveDefault()` writer to nlohmann/json as well (likely yes).

### Deferred Ideas (OUT OF SCOPE)
- UX-01 (Auto-start toggle in UI).
- Schema v2 + migration harness.
- Moving `AudioConfig` wstring → std::string.
- Preserving unknown user-added keys through write cycles.
- Fixing CFG-03 "sample rate" wording (Phase 5).
- Enabling `MICMAP_USE_GTEST` as a cross-phase default.
- Config-reload-while-running.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CFG-01 | `ConfigManager::loadDefault()` parses `%APPDATA%/MicMap/config.json` via nlohmann/json | Defensive-parse recipe in §API Recipes; link wiring in §CMake Integration |
| CFG-02 | Malformed/corrupt config → backup to `config.json.corrupted.YYYYMMDD-HHMMSS`, fall back to defaults, log warning, no crash | `.is_discarded()` flow, `std::put_time`/`strftime` recipe, retention pruning recipe in §API Recipes |
| CFG-03 | Each field read with `.value(key, default)` and bounds-validated (clamp + warning log) | Clamp-helper template + pow2-snap recipe in §API Recipes |
| CFG-04 | Write/read round-trip is identity across full `AppConfig` (writer migrates to nlohmann/json if needed) | nlohmann serialize recipe + `std::optional<time_point>` round-trip in §API Recipes |
| CFG-05 | Persisted settings: audio device, detection duration, sensitivity, SteamVR options. Training data path unchanged. | Field inventory in §AppConfig Field Coverage |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **Windows-only milestone.** Non-Windows branches of `config_manager.cpp` remain stubbed/fallback. Atomic-save code is gated on `_WIN32`.
- **C++17** — no `<format>`, no `std::filesystem::path::native()` peculiarities beyond what's already used. Use `std::put_time` or `std::strftime`.
- **No try/catch** per CONVENTIONS.md. Use `parse(..., nullptr, /*allow_exceptions=*/false)` + `.is_discarded()`. Do not access fields with `.at()` (throws on key-missing even in no-exception mode — see Pitfall R-2 below).
- **Return-code error style.** `load()`/`save()` return `bool`; `true` means "have a usable config" (defaults after corruption still count as success).
- **Bash uses Unix-style paths.** `std::filesystem::path` handles this fine; tests using raw literal paths must use forward slashes or `R"(...)"` raw wide literals with backslashes.
- **Logger macros do NOT append `\n` themselves** — implementation already emits `std::endl`. Callers pass a plain message; variadic args are concatenated via fold expression into an ostringstream.

## Summary

This phase is mechanical and low-risk. nlohmann/json v3.11.2 is already vendored; the only build change is adding `nlohmann_json` to `micmap_core`'s `target_link_libraries` (PRIVATE). The core technical surface is three pieces:

1. **A defensive nlohmann/json read path** using `parse(..., nullptr, false)` + `.is_discarded()` for top-level corruption, and `.value(key, default)` with explicit `is_*()` guards for every field access.
2. **A writer rewrite** using nlohmann/json `json::object()` construction and `dump(4)` — replacing the hand-rolled `toJson()` which has two bugs: ASCII truncation of wstring (D-07) and would re-emit non-conformant JSON across edge cases (Pitfall 9).
3. **A Windows atomic save path** using `ReplaceFileW` with a first-write fallback to `MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`.

**One surprise worth flagging.** `nlohmann::json::value(key, default)` is **not** type-tolerant: if a key exists but has the wrong type (e.g. JSON has `"sensitivity": "high"` but we ask for a `float`), `.value()` throws `json::type_error` — and `JSON_NOEXCEPTION` mode aborts rather than returning. The locked decision of "no try/catch" therefore requires us to guard every `.value()` call with an explicit type check: `if (obj.contains(k) && obj[k].is_number()) val = obj[k].get<float>(); else val = default;`. A small helper (see §API Recipes, recipe 2) makes this tractable. This is not a contradiction of any locked decision, but it is a constraint the planner must pass to executors.

**Primary recommendation:** Inline-rewrite the `load()` and `save()` methods, add a small set of anonymous-namespace helpers (`readFloat`, `readInt`, `readString`, `readWString`, `readBool`, `clampRange`, `snapPowerOfTwo`, `toUtf8`, `fromUtf8`, `atomicWriteWindows`, `backupAndRotate`), migrate `toJson()` to an `AppConfigToJson(const AppConfig&)` free function returning `nlohmann::json`, and land a single CTest-registered self-test binary (`test_config_manager.cpp`, exit code only — no GTest enable this phase). Total change set fits in ~300 LOC inside `src/core/src/config_manager.cpp` + a ~200 LOC test file.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| JSON parsing & serialization | `src/core` (ConfigManagerImpl) | — | Interface `IConfigManager` already owns config lifecycle; parsing is an implementation detail hidden behind it. |
| UTF-8 ↔ wstring conversion | `src/core` (file-local anonymous namespace) | `src/common` (if reused by other subsystems later) | Only used at the JSON boundary inside `config_manager.cpp` right now. CONVENTIONS.md blesses file-local helpers. If logger or audio ever needs it, promote to `micmap::common::utf8`. |
| Atomic file replace (Windows) | `src/core` (file-local) | — | Only caller is `save()`. No reuse case. Guard with `#ifdef _WIN32`. |
| Corruption backup + retention pruning | `src/core` (file-local) | — | Same reasoning. Uses `std::filesystem::directory_iterator`. |
| Bounds validation / clamping | `src/core` (file-local templates) | — | Trivial `std::clamp` wrapping + warning emission. |
| Test harness for round-trip | `tests/` (CTest standalone `main()`) | — | Follows `test_placeholder.cpp` style; no framework adoption. |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| nlohmann/json | v3.11.2 (vendored) | JSON parse + serialize | Already in `external/CMakeLists.txt:7-21` as INTERFACE target. Pinned tag; no registry check needed. `[VERIFIED: external/CMakeLists.txt:11]` |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Win32 API (Kernel32.lib) | system | `ReplaceFileW`, `MoveFileExW`, `WideCharToMultiByte`, `MultiByteToWideChar` | Atomic save + UTF-8 boundary. Already transitively available through the `shell32` link and Windows.h include. `[VERIFIED: MSDN]` |
| `<chrono>` | C++17 std | `system_clock::to_time_t` / `from_time_t` | Already used in writer for `lastTrainedTimestamp`. |
| `<iomanip>` | C++17 std | `std::put_time` for `YYYYMMDD-HHMMSS` backup filenames | Already used in `logger.cpp:46-49`. |
| `<filesystem>` | C++17 std | Path manipulation + `directory_iterator` for retention pruning | Already used throughout `config_manager.cpp`. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `ReplaceFileW` | `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` | `MoveFileEx` is atomic on same-volume NTFS but can silently fall back to non-atomic CopyFile+DeleteFile in edge cases. `ReplaceFile` is the documented atomic primitive. **Use as first-save fallback only** (when replaced file doesn't yet exist — `ReplaceFile` requires it to exist). `[CITED: MSDN ReplaceFileW Remarks]` |
| CTest-standalone test | GTest via `MICMAP_USE_GTEST=ON` | GTest is scaffolded but not wired. Flipping the default is a cross-phase decision per the CONTEXT.md discretion note. Keep this phase's tests as a `main()`-returning int to match `test_placeholder.cpp`. Planner decides final call. |
| `std::put_time` | `std::strftime` | Both work; `put_time` is the C++17-idiomatic stream manipulator (matches logger.cpp style). `strftime` is needed only if formatting outside a stream. |
| Writing own serialization | nlohmann's `json::dump(4)` | Dump handles escaping, Unicode, number formatting, and is guaranteed round-trippable. Hand-rolled is how we got Pitfall 9. |

**Installation:** No new packages. Build change:

```cmake
# src/core/CMakeLists.txt — add nlohmann_json to link line
target_link_libraries(micmap_core
    PUBLIC
        micmap_common
    PRIVATE                       # NEW
        nlohmann_json             # NEW — INTERFACE target from external/CMakeLists.txt
)
```

**Version verification:** nlohmann/json v3.11.2 confirmed at `external/CMakeLists.txt:11` via `GIT_TAG v3.11.2`. Upstream v3.11.2 released 2022-08-12; v3.11.3 (2023-11-28) is current but pin-swap is not in this phase's scope and there are no CVEs affecting us. `[CITED: external/CMakeLists.txt, github.com/nlohmann/json/releases]`

## Architecture Patterns

### System Data-Flow Diagram

```
  MicMapApp::initialize()              MicMapApp::shutdown()
         │                                    │
         │ configManager->loadDefault()       │ configManager->saveDefault()
         ▼                                    ▼
  ConfigManagerImpl::load(path)        ConfigManagerImpl::save(path)
         │                                    │
         │ ifstream entire file                │ AppConfigToJson(config_) -> json
         ▼                                    │ json.dump(4) -> utf8 string
  json::parse(str, nullptr, false)            ▼
         │                              writeAtomicWindows(path, utf8)
         ▼                                    │
  is_discarded()?  ──yes──▶ backupAndRotate(path) ▶ resetToDefaults() ▶ LOG_WARN ▶ return true
         │ no                                 ├─ write <path>.tmp
         ▼                                    ├─ ReplaceFileW(path, tmp, NULL, IGNORE_MERGE_ERRORS)
  readAudio(j["audio"])  ──fields──▶ clampRange / snapPow2 / toUtf8 ▶ config_.audio
  readDetection(j["detection"])                  │ (if target doesn't exist)
  readSteamVR(j["steamvr"])                      ▼
  readTraining(j["training"])                MoveFileExW(tmp, path,
         │                                     MOVEFILE_REPLACE_EXISTING |
         ▼                                     MOVEFILE_WRITE_THROUGH)
  return true
```

### Recommended Project Structure

No new files required. All changes inside existing `src/core/src/config_manager.cpp` plus one new test file `tests/test_config_manager.cpp`.

```
src/core/
├── include/micmap/core/config_manager.hpp   # UNCHANGED (interface stable)
├── src/config_manager.cpp                    # MODIFIED (defensive load + atomic save + UTF-8)
└── CMakeLists.txt                            # MODIFIED (add nlohmann_json PRIVATE)

tests/
├── CMakeLists.txt                            # MODIFIED (register test_config_manager)
├── test_placeholder.cpp                      # UNCHANGED
└── test_config_manager.cpp                   # NEW (round-trip + corruption + clamp)
```

### Pattern 1: Anonymous-namespace file-local helpers
**What:** File-scope helpers bundled in an anonymous namespace inside `config_manager.cpp`.
**When to use:** All of Phase 2's new helpers (UTF-8 converters, atomic-write, clamp templates, backup rotator). Matches existing `getAppDataPath()` pattern at line 25.
**Example:** See §API Recipes below — every recipe lives in this namespace.

### Pattern 2: Interface stability + private impl
**What:** `IConfigManager` interface in the public header is untouched; only `ConfigManagerImpl` internals change.
**When to use:** Mandatory. The interface is consumed by `apps/micmap/main.cpp:171-172,337` and no other caller should need recompilation beyond link.

### Anti-Patterns to Avoid

- **Use of `.at(key)` or `.at(idx)` on json.** These throw on missing key even under `allow_exceptions=false` parse. Only `.value(k, default)` + contains-guards are safe in our no-exception regime.
- **Assuming `.value("k", 0.0f)` handles type mismatches.** It does not — throws `json::type_error`. Always pre-check with `contains() && is_number()` (or the analog for the target type).
- **Hand-rolling JSON escaping in the writer.** The current `toJson()` has D-07 (ASCII truncation) bug and violates Pitfall 9 (non-conformant emit). Delete it; use nlohmann's `dump()`.
- **Writing directly to the destination path.** A crash mid-write corrupts the only saved config. Use temp + atomic swap.
- **Opening the corrupt file with `std::filesystem::rename` before creating a backup.** Use `std::filesystem::rename` AFTER computing the timestamped backup name — but note it can fail cross-volume. Since `%APPDATA%\MicMap\` is always same-volume here, `rename` is fine; the Windows API equivalent would be `MoveFileW` without flags.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing | Regex / state machine / current stub | `nlohmann::json::parse(buf, nullptr, false)` | Pitfall 9 history. Hand-rolled JSON is where this phase got into trouble. |
| JSON serialization | String concatenation with `ostringstream` | `json j; j["audio"] = ...; out << j.dump(4);` | Escaping, Unicode, number formatting, trailing-comma avoidance — all free. |
| UTF-8 / UTF-16 conversion | C++11 `std::wstring_convert<std::codecvt_utf8<wchar_t>>` | `WideCharToMultiByte(CP_UTF8, …)` / `MultiByteToWideChar(CP_UTF8, …)` | `wstring_convert` is **deprecated in C++17**. Windows API versions handle invalid sequences via `MB_ERR_INVALID_CHARS` and are the idiomatic Win32 choice. `[CITED: cppreference deprecation notice]` |
| Atomic file replace | Temp + `std::filesystem::rename` | `ReplaceFileW` (+ `MoveFileExW` fallback for first save) | `filesystem::rename` on Windows delegates to `MoveFileExW`, which per MSDN can silently fall back to non-atomic CopyFile+DeleteFile. `ReplaceFile` is guaranteed atomic, preserves ACLs, and journals the swap. `[CITED: MSDN ReplaceFileW Remarks]` |
| Backup timestamp formatting | `sprintf("%04d%02d%02d-%02d%02d%02d", …)` | `std::put_time(&tm, "%Y%m%d-%H%M%S")` in `ostringstream` | Matches existing `logger.cpp` style; locale-aware zero-padding; no `-Werror` warnings. |
| Power-of-2 snap for FFT | Lookup table | Bit-trick (see recipe 6) | Four lines of code, no table to keep in sync with [512,8192] range. |
| Unit test framework | Invent assertions | `main()` returning 1 on any failure, 0 on all-pass; `std::cerr << "FAIL: …"` | Matches `test_placeholder.cpp` style; avoids the cross-phase decision of enabling GTest. |

**Key insight:** Every C++17 "do-it-yourself" temptation in this domain has a better-supported primitive nearby. The single most important discipline is "let nlohmann own the JSON surface end-to-end."

## Runtime State Inventory

**Trigger class:** Refactor (replace stubbed read path + fix lossy writer).

| Category | Items Found | Action Required |
|----------|-------------|-----------------|
| Stored data | `%APPDATA%/MicMap/config.json` — user-installed copies from 0.x that exercised the lossy-ASCII writer. wstring device names with non-ASCII characters (e.g. "Jabra Evolve²", "Bigscreen Beyond™") will have been truncated at the `<` 128 filter. | **No migration script needed** — D-09 says invalid UTF-8 / wrong type in one field uses the per-field default, and on next save the new writer emits the in-memory value (which will default if it's been lost). User has to re-select device once. Document this in DOC phase. |
| Live service config | None — config is a file, not a service registration. | None. |
| OS-registered state | None — no registry, scheduled task, or service involves `config.json`. | None. |
| Secrets / env vars | None — config has no credentials. | None. |
| Build artifacts | None — `config.json` is produced at runtime, not by the build. | None. |

**Note:** Pitfall 9 explicitly predicts a first-run upgrade crash from non-conformant JSON in existing user files. D-15 + the defensive parser neutralize this: any unparseable legacy file triggers CFG-02 backup-and-reset instead of crash. Verified against existing writer output (lines 48-110 of current `config_manager.cpp`) — the current writer emits valid JSON except for the wstring-with-non-ASCII case where it emits `"deviceNamePattern": ""` (empty, not malformed), which nlohmann accepts cleanly.

## API Recipes

All recipes below assume:

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;
```

### Recipe 1: Defensive top-level parse

```cpp
// Source: https://json.nlohmann.me/features/parsing/parse_exceptions/
bool load(const std::filesystem::path& path) override {
    std::ifstream file(path);
    if (!file) {
        // D-16: missing file is NOT corruption. Defaults already loaded in ctor.
        MICMAP_LOG_INFO("No config file at ", path.string(), "; using defaults");
        return true;  // "load succeeded with defaults"
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    json j = json::parse(content, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        // D-15: whole-file corruption
        MICMAP_LOG_WARNING("Config file corrupted; backing up and using defaults: ", path.string());
        backupAndRotate(path);         // recipe 8
        resetToDefaults();
        return true;                    // app continues with defaults
    }

    // D-12: version best-effort
    const int onDiskVersion = readInt(j, "version", config_.version);
    if (onDiskVersion != config_.version) {
        MICMAP_LOG_INFO("Config written by version ", onDiskVersion,
                        "; reading on version ", config_.version);
    }
    config_.version = config_.version;  // keep code's version for next save

    readAudio(j, config_.audio);
    readDetection(j, config_.detection);
    readSteamVR(j, config_.steamvr);
    readTraining(j, config_.training);

    MICMAP_LOG_INFO("Loaded config from: ", path.string());
    return true;
}
```

### Recipe 2: Type-safe field readers (the core insight)

**Why this exists:** `j.value(k, default)` throws `json::type_error` when the key exists but has the wrong type. Under our no-exception style, we must pre-check. Helpers below collapse the pattern.

```cpp
// Source: https://json.nlohmann.me/features/element_access/default_value/
//         + https://github.com/nlohmann/json/issues/871 (value() type-mismatch behavior)
namespace {

int readInt(const json& obj, const char* key, int defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) return defaultVal;
    return it->get<int>();
}

float readFloat(const json& obj, const char* key, float defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) return defaultVal;
    return it->get<float>();
}

bool readBool(const json& obj, const char* key, bool defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) return defaultVal;
    return it->get<bool>();
}

std::string readString(const json& obj, const char* key, std::string defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return defaultVal;
    if (!it->is_string()) return defaultVal;
    return it->get<std::string>();
}

// wstring variant — D-09: field-level default on invalid UTF-8, not whole-file corruption.
std::wstring readWString(const json& obj, const char* key, std::wstring defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return defaultVal;
    if (!it->is_string()) return defaultVal;
    const std::string utf8 = it->get<std::string>();
    std::wstring out;
    if (!fromUtf8(utf8, out)) {
        MICMAP_LOG_WARNING("Config field '", key, "' has invalid UTF-8; using default");
        return defaultVal;
    }
    return out;
}

// For nested objects — returns the sub-object or an empty object (safe to iterate).
const json& readSubObject(const json& obj, const char* key) {
    static const json kEmpty = json::object();
    if (!obj.is_object()) return kEmpty;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_object()) return kEmpty;
    return *it;
}

} // namespace
```

Usage inside per-section readers:

```cpp
void readAudio(const json& root, AudioConfig& out) {
    const json& audio = readSubObject(root, "audio");
    out.deviceNamePattern = readWString(audio, "deviceNamePattern", out.deviceNamePattern);
    out.deviceId          = readWString(audio, "deviceId",          out.deviceId);
    out.bufferSizeMs      = clampRange(readInt(audio, "bufferSizeMs", out.bufferSizeMs),
                                       5, 100, "audio.bufferSizeMs");
}

void readDetection(const json& root, DetectionConfig& out) {
    const json& d = readSubObject(root, "detection");
    out.sensitivity   = clampRange(readFloat(d, "sensitivity",   out.sensitivity),   0.0f, 1.0f, "detection.sensitivity");
    out.minDurationMs = clampRange(readInt(d,   "minDurationMs", out.minDurationMs), 100,  2000, "detection.minDurationMs");
    out.cooldownMs    = clampRange(readInt(d,   "cooldownMs",    out.cooldownMs),    100,  2000, "detection.cooldownMs");
    out.fftSize       = snapPowerOfTwo(readInt(d, "fftSize",      out.fftSize),      512,  8192, "detection.fftSize");
}
```

### Recipe 3: Clamp with warning log

```cpp
template <typename T>
T clampRange(T value, T lo, T hi, const char* name) {
    if (value < lo) {
        MICMAP_LOG_WARNING("Config '", name, "'=", value,
                           " out of range [", lo, ",", hi, "]; clamping to ", lo);
        return lo;
    }
    if (value > hi) {
        MICMAP_LOG_WARNING("Config '", name, "'=", value,
                           " out of range [", lo, ",", hi, "]; clamping to ", hi);
        return hi;
    }
    return value;
}
```

### Recipe 4: UTF-8 ↔ wstring boundary

```cpp
// Source: https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
namespace {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     w.data(), static_cast<int>(w.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        w.data(), static_cast<int>(w.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

// Returns false on invalid UTF-8 (MB_ERR_INVALID_CHARS); caller logs + uses default.
bool fromUtf8(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     s.data(), static_cast<int>(s.size()),
                                     nullptr, 0);
    if (needed <= 0) return false;   // GetLastError() == ERROR_NO_UNICODE_TRANSLATION
    out.resize(static_cast<size_t>(needed));
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      s.data(), static_cast<int>(s.size()),
                                      out.data(), needed);
    return written == needed;
}

} // namespace
```

### Recipe 5: JSON writer (replaces `toJson()` entirely)

```cpp
namespace {

json audioToJson(const AudioConfig& c) {
    json j;
    j["deviceNamePattern"] = toUtf8(c.deviceNamePattern);
    j["deviceId"] = c.deviceId.empty() ? json(nullptr) : json(toUtf8(c.deviceId));
    j["bufferSizeMs"] = c.bufferSizeMs;
    return j;
}

json detectionToJson(const DetectionConfig& c) {
    json j;
    j["sensitivity"]   = c.sensitivity;
    j["minDurationMs"] = c.minDurationMs;
    j["cooldownMs"]    = c.cooldownMs;
    j["fftSize"]       = c.fftSize;
    return j;
}

json steamvrToJson(const SteamVRConfig& c) {
    json j;
    j["dashboardClickEnabled"] = c.dashboardClickEnabled;
    j["customActionBinding"]   = c.customActionBinding.empty()
                                 ? json(nullptr)
                                 : json(c.customActionBinding);
    return j;
}

json trainingToJson(const TrainingConfig& c) {
    json j;
    j["dataFile"] = c.dataFile;
    if (c.lastTrainedTimestamp.has_value()) {
        // Serialize as int64 seconds-since-epoch for round-trip identity at time_t precision.
        j["lastTrainedTimestamp"] = static_cast<int64_t>(
            std::chrono::system_clock::to_time_t(*c.lastTrainedTimestamp));
    } else {
        j["lastTrainedTimestamp"] = nullptr;
    }
    return j;
}

json appConfigToJson(const AppConfig& c) {
    json j;
    j["version"]   = c.version;
    j["audio"]     = audioToJson(c.audio);
    j["detection"] = detectionToJson(c.detection);
    j["steamvr"]   = steamvrToJson(c.steamvr);
    j["training"]  = trainingToJson(c.training);
    return j;
}

} // namespace
```

### Recipe 6: `std::optional<system_clock::time_point>` round-trip

Reader (in `readTraining`):

```cpp
void readTraining(const json& root, TrainingConfig& out) {
    const json& t = readSubObject(root, "training");
    out.dataFile = readString(t, "dataFile", out.dataFile);

    auto it = t.find("lastTrainedTimestamp");
    if (it == t.end() || it->is_null()) {
        out.lastTrainedTimestamp = std::nullopt;
    } else if (it->is_number_integer()) {
        // time_t is signed 32-bit on legacy MSVC default, 64-bit with _USE_32BIT_TIME_T undefined
        // (default on modern MSVC). Use int64_t intermediate to be safe.
        const auto secs = it->get<int64_t>();
        out.lastTrainedTimestamp =
            std::chrono::system_clock::from_time_t(static_cast<std::time_t>(secs));
    } else {
        // Type mismatch (e.g. a string) — treat as missing.
        MICMAP_LOG_WARNING("Config 'training.lastTrainedTimestamp' has wrong type; using default");
        // leave out.lastTrainedTimestamp at its default (std::nullopt from resetToDefaults)
    }
}
```

**Round-trip identity verification:** writer emits `int64` seconds; reader reconstructs via `from_time_t`. Sub-second precision is lost by design (CONTEXT.md §specifics). Equality tests on `time_point` must truncate to `time_t` before comparing — or the test fixture must create its `time_point` from a `time_t` to begin with. The planner's round-trip test (Recipe 10) uses a fixture time of `from_time_t(1700000000)` which round-trips exactly.

### Recipe 7: Power-of-2 snap for `fftSize`

```cpp
// Source: Hacker's Delight Ch.3; standard bit-trick.
int snapPowerOfTwo(int value, int lo, int hi, const char* name) {
    // Pre-clamp to window (D-05 says [512,8192])
    int v = value < lo ? lo : (value > hi ? hi : value);

    // Already power of two?
    if (v > 0 && (v & (v - 1)) == 0 && v == value && value >= lo && value <= hi) {
        return value;
    }

    // Find the two flanking powers of 2 within [lo, hi] and pick nearest.
    // lo and hi are themselves required to be powers of 2 in this API (512, 8192).
    int p = lo;
    int next = p * 2;
    while (next <= hi && next <= v) { p = next; next = p * 2; }
    // p is the largest pow2 <= v that is still >= lo; next is the next pow2 up (may exceed hi).
    int chosen;
    if (next > hi) {
        chosen = p;
    } else {
        chosen = (v - p <= next - v) ? p : next;
    }
    if (chosen != value) {
        MICMAP_LOG_WARNING("Config '", name, "'=", value,
                           " is not a power of 2 in [", lo, ",", hi,
                           "]; snapping to ", chosen);
    }
    return chosen;
}
```

### Recipe 8: Corruption backup with `YYYYMMDD-HHMMSS` + 5-file retention

```cpp
// Source: std::put_time on cppreference; std::filesystem::directory_iterator
namespace {

std::string makeCorruptedSuffix() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
    return oss.str();
}

void backupAndRotate(const std::filesystem::path& configPath) {
    namespace fs = std::filesystem;
    if (!fs::exists(configPath)) return;

    // 1. Rename to timestamped backup.
    const auto backup = configPath.parent_path() /
        ("config.json.corrupted." + makeCorruptedSuffix());
    std::error_code ec;
    fs::rename(configPath, backup, ec);
    if (ec) {
        MICMAP_LOG_ERROR("Could not rename corrupt config to ", backup.string(),
                         ": ", ec.message());
        return;
    }
    MICMAP_LOG_WARNING("Backed up corrupt config to: ", backup.string());

    // 2. Enforce 5-file retention (D-11). The timestamp format is lex-sortable,
    //    so newest-first sort is just reverse-lex on filename.
    std::vector<fs::path> backups;
    for (const auto& entry : fs::directory_iterator(configPath.parent_path(), ec)) {
        if (ec) break;
        const auto name = entry.path().filename().string();
        if (name.rfind("config.json.corrupted.", 0) == 0) {
            backups.push_back(entry.path());
        }
    }
    std::sort(backups.begin(), backups.end(), std::greater<>());
    for (size_t i = 5; i < backups.size(); ++i) {
        fs::remove(backups[i], ec);
        if (ec) {
            MICMAP_LOG_WARNING("Could not prune old backup ", backups[i].string(),
                               ": ", ec.message());
        }
    }
}

} // namespace
```

### Recipe 9: Atomic Windows save (ReplaceFile + first-write fallback)

```cpp
// Sources: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
//          https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
namespace {

#ifdef _WIN32
bool writeAtomicWindows(const std::filesystem::path& dest, const std::string& utf8Content) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        MICMAP_LOG_ERROR("Could not create config directory: ", ec.message());
        return false;
    }

    const auto tmp = dest.parent_path() / (dest.filename().wstring() + L".tmp");

    // 1. Write full content to temp file and flush.
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            MICMAP_LOG_ERROR("Could not open temp config file: ", tmp.string());
            return false;
        }
        f.write(utf8Content.data(), static_cast<std::streamsize>(utf8Content.size()));
        if (!f) {
            MICMAP_LOG_ERROR("Write to temp config file failed");
            return false;
        }
        f.flush();
    } // dtor closes handle before swap

    // 2. Atomic swap.
    if (fs::exists(dest, ec)) {
        // ReplaceFileW: documented atomic replace, preserves ACLs.
        // IGNORE_MERGE_ERRORS: proceed even if attribute merge fails (first-party config file
        // has no interesting ACLs to preserve; this is belt-and-suspenders).
        if (!ReplaceFileW(dest.c_str(), tmp.c_str(),
                          nullptr,                        // no backup
                          REPLACEFILE_IGNORE_MERGE_ERRORS,
                          nullptr, nullptr)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("ReplaceFileW failed (GetLastError=", err, ")");
            // Best-effort cleanup of the temp; user retries will overwrite.
            fs::remove(tmp, ec);
            return false;
        }
    } else {
        // First save: target doesn't exist, ReplaceFile would fail. Use MoveFileEx.
        if (!MoveFileExW(tmp.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("MoveFileExW failed on first save (GetLastError=", err, ")");
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}
#else
bool writeAtomicWindows(const std::filesystem::path& /*dest*/, const std::string& /*content*/) {
    return false;  // Non-Windows: stubbed per project policy.
}
#endif

} // namespace
```

Call site (new `save()`):

```cpp
bool save(const std::filesystem::path& path) override {
#ifdef _WIN32
    const std::string body = appConfigToJson(config_).dump(4);
    if (!writeAtomicWindows(path, body)) return false;
    MICMAP_LOG_INFO("Saved config to: ", path.string());
    return true;
#else
    // Preserve existing non-Windows fallback (direct write).
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f) { MICMAP_LOG_ERROR("Could not open config file for writing: ", path.string()); return false; }
    f << appConfigToJson(config_).dump(4);
    MICMAP_LOG_INFO("Saved config to: ", path.string());
    return true;
#endif
}
```

### Recipe 10: Round-trip self-test harness (CTest-standalone)

```cpp
// tests/test_config_manager.cpp
// Exit 0 on all-pass, non-zero on failure. No framework.

#include "micmap/core/config_manager.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    namespace fs = std::filesystem;
    namespace mc = micmap::core;

    // Use a temp dir so we don't clobber the user's real config.
    auto tmpDir = fs::temp_directory_path() / "micmap_test_config";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);
    auto cfgPath = tmpDir / "config.json";

    // ---- Test 1: round-trip identity (CFG-04) ----
    {
        auto mgr = mc::createConfigManager();
        auto& c = mgr->getConfig();
        c.audio.deviceNamePattern = L"Beyond™ Test 🎙";        // Unicode — D-07/D-08 check
        c.audio.deviceId          = L"\\\\?\\Global\\{abc-123}";
        c.audio.bufferSizeMs      = 25;
        c.detection.sensitivity   = 0.42f;
        c.detection.minDurationMs = 450;
        c.detection.cooldownMs    = 600;
        c.detection.fftSize       = 4096;
        c.steamvr.dashboardClickEnabled = false;
        c.steamvr.customActionBinding   = "action:/actions/micmap/in/click";
        c.training.dataFile = "training.bin";
        c.training.lastTrainedTimestamp =
            std::chrono::system_clock::from_time_t(1700000000);

        MM_CHECK(mgr->save(cfgPath));

        auto mgr2 = mc::createConfigManager();
        MM_CHECK(mgr2->load(cfgPath));
        const auto& c2 = mgr2->getConfig();
        MM_CHECK(c2.audio.deviceNamePattern == c.audio.deviceNamePattern);
        MM_CHECK(c2.audio.deviceId          == c.audio.deviceId);
        MM_CHECK(c2.audio.bufferSizeMs      == c.audio.bufferSizeMs);
        MM_CHECK(c2.detection.sensitivity   == c.detection.sensitivity);
        MM_CHECK(c2.detection.minDurationMs == c.detection.minDurationMs);
        MM_CHECK(c2.detection.cooldownMs    == c.detection.cooldownMs);
        MM_CHECK(c2.detection.fftSize       == c.detection.fftSize);
        MM_CHECK(c2.steamvr.dashboardClickEnabled == c.steamvr.dashboardClickEnabled);
        MM_CHECK(c2.steamvr.customActionBinding   == c.steamvr.customActionBinding);
        MM_CHECK(c2.training.dataFile == c.training.dataFile);
        MM_CHECK(c2.training.lastTrainedTimestamp.has_value());
        MM_CHECK(*c2.training.lastTrainedTimestamp == *c.training.lastTrainedTimestamp);
    }

    // ---- Test 2: corruption → backup + defaults (CFG-02) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::ofstream bad(cfgPath);
        bad << "{\n  \"audio\": { \"bufferSizeMs\": 20, },  // trailing comma\n}";
        bad.close();

        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));  // D-15 says returns true with defaults
        const auto& c = mgr->getConfig();
        MM_CHECK(c.audio.bufferSizeMs == 10);  // default from struct

        // Backup file should exist.
        bool foundBackup = false;
        for (const auto& e : fs::directory_iterator(tmpDir)) {
            if (e.path().filename().string().rfind("config.json.corrupted.", 0) == 0) {
                foundBackup = true; break;
            }
        }
        MM_CHECK(foundBackup);
    }

    // ---- Test 3: clamp + pow2-snap (CFG-03) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        std::ofstream f(cfgPath);
        f << R"({
            "audio":     {"bufferSizeMs": 500},
            "detection": {"sensitivity": 1.7, "minDurationMs": 50,
                          "cooldownMs": 5000, "fftSize": 3000}
        })";
        f.close();

        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));
        const auto& c = mgr->getConfig();
        MM_CHECK(c.audio.bufferSizeMs      == 100);     // clamped to hi
        MM_CHECK(c.detection.sensitivity   == 1.0f);    // clamped to hi
        MM_CHECK(c.detection.minDurationMs == 100);     // clamped to lo
        MM_CHECK(c.detection.cooldownMs    == 2000);    // clamped to hi
        MM_CHECK(c.detection.fftSize == 2048 || c.detection.fftSize == 4096);  // snapped
    }

    // ---- Test 4: missing file is not corruption (D-16) ----
    {
        fs::remove_all(tmpDir);
        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));  // doesn't exist
        const auto& c = mgr->getConfig();
        MM_CHECK(c.detection.sensitivity == 0.7f);  // still default
        MM_CHECK(!fs::exists(tmpDir / "config.json.corrupted." ));  // no backup
    }

    // ---- Test 5: retention pruning (D-11) ----
    {
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir);
        // Pre-seed 7 old backups with sortable timestamps.
        for (int i = 0; i < 7; ++i) {
            std::ofstream f(tmpDir / ("config.json.corrupted.2025010" +
                                       std::to_string(i) + "-000000"));
            f << "{}";
        }
        // Write a bad config and trigger backupAndRotate via load-fail.
        std::ofstream bad(cfgPath);
        bad << "{ not-json";
        bad.close();
        auto mgr = mc::createConfigManager();
        MM_CHECK(mgr->load(cfgPath));

        int count = 0;
        for (const auto& e : fs::directory_iterator(tmpDir)) {
            if (e.path().filename().string().rfind("config.json.corrupted.", 0) == 0) ++count;
        }
        MM_CHECK(count == 5);  // exactly 5 newest kept (1 new + 4 oldest-first pruned)
    }

    std::cout << "all tests passed\n";
    return 0;
}
```

Register in `tests/CMakeLists.txt`:

```cmake
add_executable(test_config_manager test_config_manager.cpp)
target_compile_features(test_config_manager PRIVATE cxx_std_17)
target_link_libraries(test_config_manager PRIVATE micmap::core)
add_test(NAME test_config_manager COMMAND test_config_manager)
```

## AppConfig Field Coverage (CFG-05)

| Field | Type | Persisted | Clamp Policy | Round-trip Notes |
|-------|------|-----------|--------------|------------------|
| `version` | `int` | yes | none; info-log mismatch (D-12) | Writer emits code's version; reader logs delta |
| `audio.deviceNamePattern` | `std::wstring` | yes | — | UTF-8 at boundary (D-08) |
| `audio.deviceId` | `std::wstring` | yes (null if empty) | — | UTF-8; serialize `null` on empty |
| `audio.bufferSizeMs` | `int` | yes | [5, 100] (D-04) | |
| `detection.sensitivity` | `float` | yes | [0.0, 1.0] (D-01) | |
| `detection.minDurationMs` | `int` | yes | [100, 2000] (D-02) | |
| `detection.cooldownMs` | `int` | yes | [100, 2000] (D-03) | |
| `detection.fftSize` | `int` | yes | pow2 in [512, 8192] (D-05) | |
| `steamvr.dashboardClickEnabled` | `bool` | yes | — | |
| `steamvr.customActionBinding` | `std::string` | yes (null if empty) | — | ASCII/UTF-8 directly |
| `training.dataFile` | `std::string` | yes | — | Path unchanged per CFG-05 |
| `training.lastTrainedTimestamp` | `std::optional<system_clock::time_point>` | yes | — | `null` ↔ `std::nullopt`; int64 `time_t` otherwise |

## Pitfalls

### Pitfall 9 (from research/PITFALLS.md) — re-interpreted

The original pitfall text anticipated a worst case where legacy writes produced non-conformant JSON that crashes `nlohmann::json::parse`. **Empirical check of the current writer (config_manager.cpp:48-110):** the current output *is* structurally valid JSON; the only issue is the D-07 ASCII-truncation bug which produces `"deviceNamePattern": ""` for non-ASCII device names — that's valid JSON, just semantically wrong.

So the real-world Pitfall 9 surface this phase must handle is:

1. **User manually edits `config.json`** (trailing comma, unbalanced brace). → Top-level `parse_error` → `.is_discarded()` → backup + defaults.
2. **Disk corruption / truncation** (write interrupted by crash before atomic save was added). → same.
3. **Upgrade from 0.x with non-ASCII device name** that got silently empty-stringed. → NOT a parse error; device reverts to empty; user re-selects once. Not corruption. No backup.

The defensive parser in Recipe 1 handles all three without distinction per D-15 / D-16.

### New Pitfalls Found During Research

**R-1 (HIGH): `value(key, default)` throws on type mismatch.** Documented upstream (`nlohmann/json` issue #871, #1563, #1546). Fix: use `readInt / readFloat / …` helpers in Recipe 2 which pre-check type. Do NOT propagate the naive `j.value("sensitivity", 0.7f)` pattern into the codebase.

**R-2 (MEDIUM): `.at()` still throws even under `JSON_NOEXCEPTION` or `allow_exceptions=false`.** The parse-level `allow_exceptions` flag only affects parsing; element access throws independently. Under `JSON_NOEXCEPTION` the library `std::abort`s instead of throwing — which is worse. We don't define `JSON_NOEXCEPTION`; we just avoid calls that throw. Never use `.at()`.

**R-3 (MEDIUM): `REPLACEFILE_WRITE_THROUGH` flag is documented "not supported".** Use `REPLACEFILE_IGNORE_MERGE_ERRORS` only. Durability is ensured by the temp-file `flush()` + close before calling `ReplaceFileW`. For the first-save `MoveFileExW` path, `MOVEFILE_WRITE_THROUGH` IS supported and should be set. `[CITED: MSDN ReplaceFileW param table]`

**R-4 (MEDIUM): `ReplaceFile` fails with `ERROR_FILE_NOT_FOUND` when target doesn't exist.** First-save path MUST check existence and fall back to `MoveFileExW`. Recipe 9 implements both branches.

**R-5 (LOW): Cross-volume temp file.** Both `ReplaceFile` and `MoveFileEx(REPLACE_EXISTING)` require source+destination on the same volume. `%APPDATA%\MicMap\` is always same-volume for our tmp-next-to-dest strategy, so fine. Documented for the reader.

**R-6 (LOW): `time_t` 32/64-bit collision.** On modern MSVC, `time_t` defaults to 64-bit (`_USE_32BIT_TIME_T` undefined). If anyone ever defined `_USE_32BIT_TIME_T=1`, our writer would emit a 32-bit seconds value but `from_time_t(int64)` would still reconstruct correctly. The test fixture using `from_time_t(1700000000)` (Nov 2023) is well inside 32-bit range. No action required beyond using `int64_t` intermediate.

**R-7 (LOW): `time_t(-1)` collision with `std::nullopt`.** Conceptually `time_t(-1)` is "error" from C `time()`, and JSON `null` is our `std::nullopt` marker. Since we emit the `null` JSON literal (not `-1`) for `nullopt`, and read `null` as `nullopt`, the two spaces don't collide. If a user hand-edits `lastTrainedTimestamp: -1`, we faithfully reconstruct `time_point` at `from_time_t(-1)` ≈ 1969-12-31 23:59:59 UTC — semantically odd but not a crash. No special-case needed.

**R-8 (LOW): Very large corrupt files.** `nlohmann::json::parse` allocates as it reads. A user who somehow gets a 100 MB `config.json` would cause a brief memory spike during the bad-parse path. Acceptable because (1) `%APPDATA%\MicMap\config.json` is written only by us except if the user intentionally bloats it, and (2) after one bad load, the file is backed up and replaced with a clean default (~500 bytes). **Optional mitigation:** cap the read at say 1 MB via `tellg()` check before parsing; log-and-treat-as-corrupt if over cap. Low priority.

**R-9 (LOW): AV / OneDrive / backup software holds file handle during `ReplaceFileW`.** Typical errors: `ERROR_SHARING_VIOLATION` (32), `ERROR_ACCESS_DENIED` (5). Our code logs and returns false; the next `saveDefault()` call (next app run shutdown) retries. No retry loop inside `save()` — keeps shutdown path deterministic. Flag in DOC-01 if it becomes a support issue.

## Testing Approach

### Recommendation: CTest-standalone (no GTest this phase)

**Rationale:**

- Matches `test_placeholder.cpp` style — minimal friction, no framework dependency decision.
- Flipping `MICMAP_USE_GTEST=ON` is explicitly called out in CONTEXT.md as a cross-phase decision that the planner *may* defer. Deferring it keeps Phase 2's scope tight.
- A single `test_config_manager.cpp` with ~5 scenarios (Recipe 10) covers CFG-02 (corruption + retention) and CFG-04 (round-trip identity) with well-defined pass/fail — exactly what the success criteria require.
- CTest integration is already wired (`tests/CMakeLists.txt`, `enable_testing()` in root). `add_test(NAME test_config_manager COMMAND test_config_manager)` is all that's needed.

If the planner decides to enable GTest instead, the test patterns translate directly (each `MM_CHECK` → `EXPECT_*` / `ASSERT_*`, each scoped block → `TEST(ConfigManager, RoundTrip)` etc.). But doing so pulls in GoogleTest via FetchContent at first CMake configure, lengthens clean builds by ~30-60s, and introduces a second test-style in the codebase. Not worth it for 5 scenarios.

### Test scenarios (all automated, CTest-registered)

| # | Scenario | Requirement | Type |
|---|----------|-------------|------|
| T-1 | Unicode round-trip identity across full `AppConfig` | CFG-04 | Automated |
| T-2 | Malformed JSON (trailing comma) → backup + defaults + no crash | CFG-02 | Automated |
| T-3 | Out-of-range numeric fields clamped | CFG-03 | Automated |
| T-4 | Missing file is not corruption; no backup created | D-16 | Automated |
| T-5 | Retention pruning keeps only 5 most recent `.corrupted.*` | D-11 | Automated |
| T-6 | Invalid UTF-8 in `deviceNamePattern` → field default, not whole-file backup | D-09 | Automated (optional; recipe-4 unit) |

### Manual verification (deferred from automated)

| # | Scenario | Why not automated |
|---|----------|-------------------|
| M-1 | Full app cycle: change setting in GUI, quit, relaunch, setting preserved | Success criterion #1; requires GUI + WASAPI device enumeration |
| M-2 | Atomic-save crash safety: power-cycle machine during save | Untestable without physical hardware intervention |
| M-3 | AV/OneDrive sharing violation on save | Environmental; if it becomes a field issue, flag in DOC phase |

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest standalone (C++ main() returning int) — matches existing `test_placeholder.cpp` |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `ctest -R test_config_manager --output-on-failure` |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CFG-01 | Parse `config.json` via nlohmann/json, replacing stub | Integration (load writes-then-reads) | `ctest -R test_config_manager` (T-1) | ❌ Wave 0 — create `tests/test_config_manager.cpp` |
| CFG-02 | Malformed config → backup + defaults + warning log | Integration | `ctest -R test_config_manager` (T-2, T-5) | ❌ Wave 0 |
| CFG-03 | Clamp out-of-range numerics with warning log | Integration | `ctest -R test_config_manager` (T-3) | ❌ Wave 0 |
| CFG-04 | Write/read round-trip identity across full `AppConfig` | Integration | `ctest -R test_config_manager` (T-1) | ❌ Wave 0 |
| CFG-05 | Audio device / detection duration / sensitivity / SteamVR options persist; training path unchanged | Integration (subset of T-1 covering those fields) | `ctest -R test_config_manager` (T-1) | ❌ Wave 0 |
| Manual | End-to-end user cycle: change in GUI → quit → relaunch → preserved | Manual | n/a (success-criterion #1 verification) | — |

### Sampling Rate

**Nyquist rationale:** The phase's observable surface is "settings persist" + "corruption is survived". The sharpest signal is the round-trip identity test (T-1) — if that passes, every field is both written and read correctly. The next-sharpest is the corruption-backup test (T-2) — if that passes, the failure mode the phase exists to prevent cannot recur. Together, T-1 + T-2 + T-5 cover the phase's value proposition.

- **Per task commit:** `ctest -R test_config_manager --output-on-failure` (< 1 second; runs all 5 scenarios)
- **Per wave merge:** `ctest --output-on-failure` (full suite = test_placeholder + test_config_manager)
- **Phase gate:** Full suite green + M-1 manual check before `/gsd-verify-work`

### Wave 0 Gaps

- [ ] `tests/test_config_manager.cpp` — covers CFG-01, CFG-02, CFG-03, CFG-04, CFG-05 (all five requirements)
- [ ] `tests/CMakeLists.txt` — add `add_executable(test_config_manager ...)` + `add_test(...)` registration
- [ ] No framework install needed (CTest is built into CMake)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — (no auth surface in config persistence) |
| V3 Session Management | no | — |
| V4 Access Control | no | — (config is user-owned under `%APPDATA%`) |
| V5 Input Validation | yes | Defensive-parse + `.value()` with type-guarded helpers + numeric clamping + UTF-8 validation (MB_ERR_INVALID_CHARS). Covered by Recipes 2, 3, 4. |
| V6 Cryptography | no | — (no secrets in config; no encryption needed) |

### Known Threat Patterns for JSON + Windows-FS

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed JSON → crash | Denial of Service | `parse(..., nullptr, false)` + `.is_discarded()` + backup-and-reset (Recipe 1 + 8) |
| Tampered numeric out-of-range | Tampering | `clampRange()` (Recipe 3) + `snapPowerOfTwo()` (Recipe 7) |
| Tampered invalid UTF-8 string | Tampering | `MB_ERR_INVALID_CHARS` in `MultiByteToWideChar` (Recipe 4) → field default |
| Huge config (memory exhaustion) | DoS | Optional 1 MB cap before parse (R-8). Low priority. |
| Concurrent write from another MicMap instance | Tampering | `ReplaceFileW` is atomic; last-writer-wins. Not a multi-instance product. |
| Symlink attack (`%APPDATA%/MicMap/config.json` points elsewhere) | Tampering / EoP | Out of scope — user-mode, user-owned. `%APPDATA%` is already user-trust boundary. |

## Open Questions

1. **Should `saveDefault()` also rotate the current `config.json` → `config.json.bak.1` on every save?**
   - What we know: CONTEXT.md is silent beyond the `.corrupted.*` flow. ReplaceFile's backup parameter (third arg) is exactly for this but we pass `nullptr` in Recipe 9.
   - What's unclear: Is a generational `.bak` useful, or just noise?
   - Recommendation: No generational backup this phase. One rolling `.bak` could be added if users report losing settings from the atomic-save path — but atomic save makes that nearly impossible. Defer unless requested.

2. **Does the planner want `test_config_manager` to run on non-Windows?**
   - What we know: `writeAtomicWindows` is `#ifdef _WIN32`. On other platforms it returns false, so `save()` uses direct-write fallback. Round-trip test would still pass there.
   - What's unclear: Is there value in guarding the whole test with `#ifdef _WIN32`?
   - Recommendation: Let it run everywhere — round-trip works on any fs; atomic-path specifics only matter on Windows, and are covered by the Windows-specific test config validating via M-1.

3. **Log wording conventions.** CONTEXT.md D-section 7 punts exact log strings to executor discretion. Recommendation: mirror existing `config_manager.cpp` lines 129,140,154,160,182 — `"Loaded config from: ..."`, `"Saved config to: ..."`, `"Could not open ..."`. For new lines: `"Config '{field}'={value} out of range [{lo},{hi}]; clamping to {v}"`, `"Config file corrupted; backing up and using defaults: {path}"`, `"Backed up corrupt config to: {backup}"`, `"Config written by version {N}, reading on version {M}"`.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build | ✓ | 3.20+ (project requirement) | — |
| MSVC (Visual Studio 2022) | Build | ✓ (Windows project) | 2022 | — |
| nlohmann/json source | Runtime header-only | ✓ | v3.11.2 (vendored `external/CMakeLists.txt:7-21`) | — |
| Kernel32.lib (ReplaceFileW, MoveFileExW, WideCharToMultiByte) | Windows atomic save + UTF-8 | ✓ (system lib, already linked via WinMain) | — | — |
| CTest | Test runner | ✓ (part of CMake) | 3.20+ | — |
| `%APPDATA%` writable | Runtime | ✓ (user-mode; `getAppDataPath()` falls back to CWD) | — | Existing fallback at config_manager.cpp:33 |
| Network | n/a | n/a | — | n/a |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

All dependencies are in-repo or system-provided. Phase 2 has **zero external-service dependencies** — it is fully deterministic from a clean clone + `cmake --build`.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Current `toJson()` output is structurally valid JSON (just ASCII-lossy), so upgrade-from-0.x does NOT hit Pitfall 9's parse crash | §Pitfalls (Pitfall 9 re-interpretation) | Low — the defensive parser handles parse failure regardless; the planner just doesn't need to add a migration step. I verified by reading current writer at `config_manager.cpp:48-110` [VERIFIED] |
| A2 | `time_t` is 64-bit on this project's MSVC config | Recipe 6 | Low — test fixture uses a time well within 32-bit range; recipe uses `int64_t` intermediate so even if `time_t` is 32-bit, round-trip holds |
| A3 | `%APPDATA%\MicMap\` is always same-volume as its own temp files | Recipe 9 | Low — `%APPDATA%` is `C:\Users\<u>\AppData\Roaming` on default Windows; corner case would be folder redirection to a network share, which is rare and would just fail both ReplaceFile and MoveFileEx with same error |
| A4 | Logger macros do not append `\n` (caller never passes one) | §CLAUDE.md constraints + Recipes | Verified via `src/common/src/logger.cpp:51` (`<< std::endl`) [VERIFIED] |

All other claims are verified via codebase read, MSDN, or nlohmann/json official docs.

## Sources

### Primary (HIGH confidence)
- `external/CMakeLists.txt:7-21` — nlohmann/json v3.11.2 pin [VERIFIED]
- `src/core/src/config_manager.cpp` (lines 48-146) — current writer + stubbed reader [VERIFIED]
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig` shape [VERIFIED]
- `src/common/src/logger.cpp` — logger auto-appends `std::endl` [VERIFIED]
- `src/common/include/micmap/common/logger.hpp` — variadic macro contract [VERIFIED]
- `.planning/research/PITFALLS.md` §Pitfall 9 — original threat model [VERIFIED]
- `.planning/codebase/CONVENTIONS.md` — no-exception + anonymous-namespace pattern [VERIFIED]
- [MSDN ReplaceFileW reference](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew) — API signature, flags, error codes, Remarks (first-save requirement) [CITED]
- [MSDN MoveFileExW reference](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) — REPLACE_EXISTING + WRITE_THROUGH semantics [CITED]
- [nlohmann/json parse_exceptions docs](https://json.nlohmann.me/features/parsing/parse_exceptions/) — `allow_exceptions=false` + `.is_discarded()` [CITED]
- [nlohmann/json value() default docs](https://json.nlohmann.me/features/element_access/default_value/) — key-missing vs type-mismatch [CITED]

### Secondary (MEDIUM confidence)
- [nlohmann/json issue #871](https://github.com/nlohmann/json/issues/871) — `value(k, default)` throws on type mismatch (community-confirmed behavior) [CITED]
- [nlohmann/json issue #2360](https://github.com/nlohmann/json/issues/2360) — misleading `allow_exceptions` docs (scope clarification) [CITED]
- [nlohmann/json issue #2724](https://github.com/nlohmann/json/issues/2724) — `JSON_NOEXCEPTION` aborts silently — reason we avoid it [CITED]
- [antonymale.co.uk Atomic File Writes on Windows](https://antonymale.co.uk/windows-atomic-file-writes.html) — pattern survey; confirms our recipe [CITED]

### Tertiary (LOW confidence — used only as cross-check)
- [Go golang-nuts thread on ReplaceFile vs MoveFileEx](https://groups.google.com/g/golang-nuts/c/JFvnLx246uM) — third-party confirmation of atomicity guarantees

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — nlohmann/json pinned; Win32 APIs stable since XP; all dependencies already in tree.
- Architecture: HIGH — pattern matches existing `config_manager.cpp` structure; no novel design.
- Pitfalls: HIGH for R-1..R-4 (documented upstream); MEDIUM for R-5..R-9 (edge cases, mitigations described).
- Testing: HIGH — CTest-standalone is the documented existing pattern.

**Research date:** 2026-04-22
**Valid until:** ~2026-07-22 (90 days — nlohmann/json API is extremely stable; Win32 file APIs haven't changed since Win8).

## RESEARCH COMPLETE

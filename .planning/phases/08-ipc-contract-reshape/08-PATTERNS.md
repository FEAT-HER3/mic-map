# Phase 8: IPC Contract Reshape - Pattern Map

**Mapped:** 2026-05-05
**Files analyzed:** 36 (new) + 7 (modified) = 43
**Analogs found:** 41 / 43 (95% coverage)
**Branch HEAD:** `407d8b1` (hmd-button)

---

## File Classification

### Wave 0 — Lints + Test Scaffolds (Plan `08-00-prereq`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `cmake/AssertNoJsonInCore.cmake` | config (lint) | batch (file scan) | `cmake/lint_no_openvr_in_core.cmake` | exact |
| `cmake/AssertHttpServerLocalhostOnly.cmake` | config (lint) | batch (file scan) | `cmake/AssertDetectionRunnerNoVrApi.cmake` | exact |
| `cmake/AssertHttpServerNoVrApi.cmake` | config (lint) | batch (file scan) | `cmake/AssertDetectionRunnerNoVrApi.cmake` | exact |
| `cmake/AssertNoConfigWriteInClient.cmake` | config (lint) | batch (file scan) | `cmake/lint_no_openvr_in_core.cmake` | exact |
| `tests/driver/get_state_shape_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/get_telemetry_level_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/get_devices_cache_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/get_settings_shape_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/put_settings_round_trip_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/put_settings_validation_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/put_settings_stress100_test.cpp` | test | stress | `tests/driver/device_provider_lifecycle_stress_test.cpp` | role-match |
| `tests/driver/init_config_share_violation_test.cpp` | test | file-I/O | `tests/driver/audio_worker_lifecycle_headless.cpp` | role-match |
| `tests/driver/state_clear_error_test.cpp` | test | request-response | `tests/driver/detection_settings_propagation_test.cpp` | role-match |
| `tests/driver/config_io_atomic_persist_test.cpp` | test | file-I/O | `tests/test_config_manager.cpp` | role-match |
| `tests/test_multi_sink_logger.cpp` | test | event-driven | `tests/test_command_queue.cpp` | role-match |
| `tests/test_settings_validator.cpp` | test | transform | `tests/test_command_queue.cpp` | role-match |
| `tests/test_client_driver_loaded_indicator.cpp` | test | request-response | `tests/test_vr_input_quit_ordering.cpp` | role-match |
| `tests/test_client_level_meter_cadence.cpp` | test | event-driven | `tests/test_command_queue.cpp` | role-match |

### Wave 1 — Rename + Logger Sinks (Plan `08-01`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `src/steamvr/include/micmap/steamvr/driver_api.hpp` (renamed from `vr_input.hpp`) | interface | request-response | `src/steamvr/include/micmap/steamvr/vr_input.hpp` | exact (file rename) |
| `src/steamvr/src/driver_api.cpp` (renamed from `vr_input.cpp`) | service | request-response | `src/steamvr/src/vr_input.cpp` | exact (file rename) |
| `src/common/include/micmap/common/log_sink.hpp` | interface | event-driven | `src/common/include/micmap/common/logger.hpp` (ILogger) | exact (extension) |
| `src/common/src/sinks/file_log_sink.cpp` | service (sink) | file-I/O | `src/common/src/logger.cpp` (ConsoleLogger) | role-match |
| `src/common/src/sinks/stdout_log_sink.cpp` | service (sink) | stdout-stream | `src/common/src/logger.cpp` (ConsoleLogger) | exact |
| `src/common/src/multi_sink_logger.cpp` | service (logger) | event-driven (fan-out) | `src/common/src/logger.cpp` (ConsoleLogger::log) | role-match |
| `driver/src/sinks/driver_log_sink.{hpp,cpp}` | service (sink) | event-driven | `driver/src/driver_log.hpp` (SafeDriverLog) | exact (wrap) |
| `apps/micmap/main.cpp` (modified — composition root) | controller | startup-init | (self) `WinMain` prologue near line 884 | exact |
| `driver/src/device_provider.cpp` (modified — composition root) | controller | startup-init | (self) `Init` near line 91 | exact |

### Wave 2 — JSON in Driver + Config I/O (Plan `08-02`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `driver/src/config_io.{hpp,cpp}` | service | file-I/O | `src/core/src/config_manager.cpp` (ConfigManagerImpl::load + save) | exact |
| `driver/src/sinks/` (new directory) | structural | — | `driver/src/` (as per existing layout) | exact |
| `apps/micmap/src/config_json.cpp` (NEW — option (c) from RESEARCH Open Q5) | utility | transform (JSON ADL) | `src/core/src/config_manager.cpp` (`appConfigToJson` lines 255-264) | exact |
| `external/CMakeLists.txt` (modified — cpp-httplib bump v0.14.3 → v0.20.1) | config | build | (self) lines 57-75 | exact |

### Wave 3 — GET Endpoints (Plan `08-03`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `driver/src/http_server.{hpp,cpp}` (modified — add 4 GET routes) | controller | request-response | (self) `SetupRoutes()` lines 126-184 | exact |
| `driver/src/driver_state.hpp` (NEW — DriverState struct) | model | atomic-snapshot | `driver/src/detection_runner.hpp:48-53` (DetectionConfig) | exact |
| `driver/src/device_provider.{hpp,cpp}` (modified — add atomic snapshots, getters) | controller | atomic-snapshot publish/load | (self) ctor + Init lines 109-119 (driverDetectionActiveGetter) | exact |

### Wave 4 — PUT Settings + Validator + Clear-Error (Plan `08-04`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `driver/src/settings_validator.{hpp,cpp}` | service (validator) | transform (validate) | NEW — closest is `src/core/src/config_manager.cpp` clamp paths in `readDetection`/`readAudio` | partial |
| `driver/src/http_server.{hpp,cpp}` (modified — PUT + clear-error) | controller | request-response | (self) POST /button at lines 131-154 | exact |

### Wave 5 — Client UI Health Pane + Settings Rewire (Plan `08-05`)

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|----------------|---------------|
| `apps/micmap/main.cpp` (modified — UI rewire, health pane) | view (ImGui) | request-response (poll) | (self) `MicMapApp::initialize()` + `renderUI()` near `apps/micmap/main.cpp:914` | exact |
| `apps/micmap/src/driver_health_pane.{hpp,cpp}` (optional, extracted) | component | poll | (self) extracted from main.cpp section "Audio Levels" | exact |

### Wave 6 — UAT (Plan `08-06`)

No new files; instrumentation only.

---

## Pattern Assignments

### `cmake/AssertNoJsonInCore.cmake` (config, batch)

**Analog:** `cmake/lint_no_openvr_in_core.cmake` (P5 D-02)

**Invocation pattern** (from `tests/CMakeLists.txt` lines 116-124):
```cmake
add_test(NAME lint_no_openvr_in_core
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)
```

**Body skeleton** (cmake/lint_no_openvr_in_core.cmake lines 19-57):
```cmake
set(_violations "")
set(_files_scanned 0)
foreach(_root ${SRC_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "<lintname>: SRC_ROOTS entry is not a directory: ${_root}")
    endif()
    file(GLOB_RECURSE _files
        "${_root}/*.h"   "${_root}/*.hpp" "${_root}/*.hxx"
        "${_root}/*.c"   "${_root}/*.cpp" "${_root}/*.cc"
        "${_root}/*.cxx" "${_root}/*.inc" "${_root}/*.ipp")
    foreach(_file ${_files})
        math(EXPR _files_scanned "${_files_scanned} + 1")
        file(READ "${_file}" _content)
        if(_content MATCHES "[<\"]nlohmann/json\\.hpp[>\"]")     # CHANGED regex
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()
if(_violations) ...message(FATAL_ERROR ...)... endif()
message(STATUS "<lintname>: clean (${_files_scanned} files scanned)")
```

**Adaptation:** swap the regex `[<\"]openvr[a-z_]*\\.h[>\"]` for `[<\"]nlohmann/json\\.hpp[>\"]`. Keep the SRC_ROOTS multi-directory contract (D-02 scope: `src/audio`, `src/detection`, `src/core`, `src/common`).

---

### `cmake/AssertHttpServerLocalhostOnly.cmake` (config, batch)

**Analog:** `cmake/AssertDetectionRunnerNoVrApi.cmake` (P7 D-22; **three-file explicit list shape**, narrower than GLOB_RECURSE)

**Body skeleton** (AssertDetectionRunnerNoVrApi.cmake lines 26-75):
```cmake
if(NOT DEFINED HTTP_SERVER_DIR)
    message(FATAL_ERROR "AssertHttpServerLocalhostOnly: HTTP_SERVER_DIR not provided.")
endif()
set(_targets
    "${HTTP_SERVER_DIR}/http_server.hpp"
    "${HTTP_SERVER_DIR}/http_server.cpp")
set(_violations "")
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        continue()              # Wave 0 RED-tolerant skip
    endif()
    file(READ "${_file}" _content)
    # IPC-07: any host literal that ISN'T "127.0.0.1" is a violation.
    # Match bind_to_port / listen / set_default_host calls and assert literal.
    if(_content MATCHES "0\\.0\\.0\\.0"
            OR _content MATCHES "INADDR_ANY"
            OR (_content MATCHES "bind_to_port" AND NOT _content MATCHES "127\\.0\\.0\\.1"))
        list(APPEND _violations "${_file}")
    endif()
endforeach()
if(_violations) ...FATAL_ERROR... endif()
message(STATUS "AssertHttpServerLocalhostOnly: clean")
```

**Adaptation:** narrow scope to `http_server.{hpp,cpp}`. Wave-0 RED-tolerant via skip-on-NOT-EXISTS.

---

### `cmake/AssertHttpServerNoVrApi.cmake` (config, batch)

**Analog:** `cmake/AssertDetectionRunnerNoVrApi.cmake` (one-to-one — same shape, different file list)

**Body skeleton** — copy `AssertDetectionRunnerNoVrApi.cmake` verbatim, swap the `_targets` list to `http_server.hpp`, `http_server.cpp`. Use the same regex set (lines 59-61):
```cmake
if(_content MATCHES "[<\"]openvr[a-z_]*\\.h[>\"]"
        OR _content MATCHES "[^a-zA-Z0-9_]vr::"
        OR _content MATCHES "^vr::")
    list(APPEND _violations "${_file}")
endif()
```

**Note:** `http_server.cpp` already passes (it includes only `command_queue.hpp` + `driver_log.hpp` + `httplib.h` + `nlohmann/json.hpp`). The lint locks in the discipline.

---

### `cmake/AssertNoConfigWriteInClient.cmake` (config, batch)

**Analog:** `cmake/lint_no_openvr_in_core.cmake` (uses GLOB_RECURSE; appropriate here because client write paths could leak into any source under `apps/micmap/` or `src/steamvr/`)

**Body skeleton:**
```cmake
# IPC-05: client must not write config.json. Driver is sole writer (P8 D-07).
# Allowed: client READS config.json once at startup via ConfigManager::loadDefault()
# (ConfigManagerImpl::load is allowed; ConfigManagerImpl::save is a violation in client TUs).
foreach(_root ${CLIENT_ROOTS})
    file(GLOB_RECURSE _files "${_root}/*.cpp" "${_root}/*.hpp")
    foreach(_file ${_files})
        file(READ "${_file}" _content)
        # Forbid: configManager->saveDefault() / saveDefault\(\)/save\(.*config\.json.*\)
        if(_content MATCHES "saveDefault\\(\\)"
                OR _content MATCHES "writeAtomicWindows"
                OR _content MATCHES "ReplaceFileW")
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()
```

**Scope per CONTEXT D-13 / IPC-05:** `apps/micmap/` + `src/steamvr/`. **NOT scoped:** `src/core/` (the impl is fine; only client *callers* are forbidden) and `apps/mic_test/` (headless harness keeps `saveDefault`).

---

### `tests/driver/get_state_shape_test.cpp` (test, request-response) — and 7 sibling driver tests

**Analog:** `tests/driver/detection_settings_propagation_test.cpp` (P7 Wave 0 RED-tolerant pattern)

**Header + main pattern** (detection_settings_propagation_test.cpp lines 1-35):
```cpp
/**
 * @file <test_file>.cpp
 * @brief Phase 8 Wave 0 RED test for <requirement>.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_command_queue.cpp / tests/driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-XX lands <impl>. This translation unit is intentionally
 * fail-to-build at Wave 0 — that compile failure IS the Nyquist gate.
 */

#include "<header>.hpp"   // resolved via target_include_directories tests/CMakeLists.txt

#include <chrono>
#include <iostream>
#include <thread>

namespace md = micmap::driver;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // ... case-by-case asserts, each with a PASS line on success ...
    std::cout << "all tests passed\n";
    return 0;
}
```

**CMake registration pattern** (tests/CMakeLists.txt lines 197-214 for `DetectionSettingsPropagation`):
```cmake
set(_p8_<test>_sources driver/<test>_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/<dependency>.cpp")
    list(APPEND _p8_<test>_sources
        "${CMAKE_SOURCE_DIR}/driver/src/<dependency>.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_<test> ${_p8_<test>_sources})
    target_compile_features(test_<test> PRIVATE cxx_std_17)
    target_include_directories(test_<test> PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_<test> PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api
        nlohmann_json
        httplib::httplib)
    target_compile_definitions(test_<test> PRIVATE CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME <TestName> COMMAND test_<test>)
else()
    message(STATUS "<TestName>: skipped (OpenVR SDK not found)")
endif()
```

**Specific guidance:**
- `init_config_share_violation_test.cpp` — additionally needs `<windows.h>` and the ability to open `config.json` for exclusive write before invoking the driver's `loadConfigJson` retry path. Mirror the `device_provider_lifecycle_stress_test.cpp` shape for its multi-TU source list.
- `put_settings_stress100_test.cpp` — count handles via `GetProcessHandleCount` before/after, mirror SC4 50-cycle pattern from `device_provider_lifecycle_stress_test.cpp`.

---

### `src/common/include/micmap/common/log_sink.hpp` (interface, event-driven) — NEW

**Analog:** `src/common/include/micmap/common/logger.hpp` (ILogger interface)

**Recommended shape (Pattern 3 option (a) from RESEARCH):**
```cpp
#pragma once
#include "micmap/common/logger.hpp"   // for LogLevel + logLevelToString
#include <string_view>

namespace micmap::common {

/// @brief A leaf log destination. Sinks unconditionally emit anything they
///        receive — min-level filtering is the parent logger's concern (Pitfall 10).
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void log(LogLevel level, std::string_view message) = 0;
};

} // namespace micmap::common
```

**Rationale:** the existing `ILogger` interface (logger.hpp:46-53) carries `setMinLevel`/`getMinLevel`. Sinks should NOT — those are user-facing filtering knobs. Recommended path is option (a): new `ILogSink` is the leaf interface; `MultiSinkLogger` implements `ILogger` (so `Logger::setLogger()` accepts it unchanged) and composes `ILogSink`s.

---

### `src/common/src/sinks/file_log_sink.cpp` (service-sink, file-I/O)

**Analog:** `src/common/src/logger.cpp` (ConsoleLogger::log lines 24-52) for timestamp formatting + mutex pattern; `src/core/src/config_manager.cpp:322` for atomic file ops semantics (though FileLogSink does not need ReplaceFileW — append, not replace).

**Imports + ctor pattern** (logger.cpp lines 1-22):
```cpp
#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <filesystem>

namespace micmap::common {
namespace { std::mutex fileLogMutex; }   // mirror logger.cpp:15-17

class FileLogSink : public ILogSink {
public:
    explicit FileLogSink(std::filesystem::path path) : path_(std::move(path)) {
        // Best-effort directory creation (ConsoleLogger does no I/O setup;
        // FileLogSink needs the parent dir to exist).
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
    }

    void log(LogLevel level, std::string_view message) override;
private:
    std::filesystem::path path_;
};
```

**log() body — copy timestamp formatting verbatim from ConsoleLogger** (logger.cpp lines 24-52):
```cpp
void FileLogSink::log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(fileLogMutex);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    // Append; flush per-line per Claude's Discretion default (D-19).
    std::ofstream f(path_, std::ios::app | std::ios::binary);
    if (!f) return;
    f << "[" << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
      << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
      << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "."
      << std::setfill('0') << std::setw(3) << ms.count()
      << "] [" << logLevelToString(level) << "] "
      << message << "\n";
    f.flush();
}
```

**Flush cadence (Claude's Discretion, D-19):** per-line `std::ios::app` + `f.flush()` is the simplest "no data lost on crash" semantics. P10's TEST-03 owns rotation — for P8 the file just grows.

---

### `src/common/src/sinks/stdout_log_sink.cpp` (service-sink, stdout)

**Analog:** `src/common/src/logger.cpp` ConsoleLogger (verbatim — this IS ConsoleLogger renamed/adapted).

**Adaptation choice (Claude's Discretion, D-19):**
- Recommended: keep `ConsoleLogger` as-is in `logger.cpp`, add a thin adapter `StdoutLogSink` that wraps a `ConsoleLogger` (or duplicates the body) and implements `ILogSink`. **Smallest diff in `apps/micmap/main.cpp`** because the client previously had `setLogger(make_shared<ConsoleLogger>())` only as a default — and that default stays via static init at logger.cpp:63.

```cpp
class StdoutLogSink : public ILogSink {
public:
    void log(LogLevel level, std::string_view message) override {
        // Reuse ConsoleLogger's body verbatim — same stderr stream + format.
        // (Or: hold a ConsoleLogger member and delegate.)
    }
};
```

---

### `src/common/src/multi_sink_logger.cpp` (service-logger, fan-out)

**Analog:** `src/common/src/logger.cpp` (ConsoleLogger min-level filter pattern lines 24-26)

**Recommended shape:**
```cpp
#include "micmap/common/logger.hpp"
#include "micmap/common/log_sink.hpp"
#include <vector>
#include <memory>
#include <mutex>

namespace micmap::common {

class MultiSinkLogger : public ILogger {
public:
    explicit MultiSinkLogger(std::vector<std::shared_ptr<ILogSink>> sinks)
        : sinks_(std::move(sinks)) {}

    void log(LogLevel level, std::string_view message) override {
        if (level < minLevel_) return;     // mirror ConsoleLogger filter
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& s : sinks_) {
            if (s) s->log(level, message);   // sinks each lock their own mutexes
        }
    }
    void setMinLevel(LogLevel level) override { minLevel_ = level; }
    LogLevel getMinLevel() const override { return minLevel_; }

private:
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    LogLevel minLevel_{LogLevel::Info};   // ConsoleLogger default (logger.cpp:21)
    mutable std::mutex mu_;               // single-mutex fan-out (Claude's Discretion D-19)
};

} // namespace
```

**Mutex strategy (Claude's Discretion, D-19):** single mutex around the fan-out. Volume is low (driver/client log rates are ≪ kHz); per-sink mutex adds no measurable benefit.

---

### `driver/src/sinks/driver_log_sink.{hpp,cpp}` (service-sink, event-driven)

**Analog:** `driver/src/driver_log.hpp` (SafeDriverLog Rule-3 guard lines 24-47)

**Imports + body:**
```cpp
// driver/src/sinks/driver_log_sink.hpp
#pragma once
#include "micmap/common/log_sink.hpp"
namespace micmap::driver {
class DriverLogSink : public micmap::common::ILogSink {
public:
    void log(micmap::common::LogLevel level, std::string_view message) override;
};
}

// driver/src/sinks/driver_log_sink.cpp
#include "driver_log_sink.hpp"
#include "../driver_log.hpp"   // SafeDriverLog with Rule-3 guard from P6
#include "micmap/common/logger.hpp"   // logLevelToString
#include <string>

namespace micmap::driver {
void DriverLogSink::log(micmap::common::LogLevel level, std::string_view message) {
    // SafeDriverLog already guards vr::VRDriverContext() != nullptr (driver_log.hpp:39)
    // and falls back to stderr when context is uninitialised. Add level prefix for
    // vrserver.txt readability.
    std::string line = std::string("[") + micmap::common::logLevelToString(level)
                     + "] " + std::string(message) + "\n";
    SafeDriverLog("%s", line.c_str());
}
}
```

**Why driver/-only:** SafeDriverLog includes `<openvr_driver.h>` (driver_log.hpp:11) — cannot live in shared lib (LIB-03 lint).

---

### `apps/micmap/main.cpp` (controller, startup-init) — composition root for client

**Analog:** (self) WinMain prologue starting near line 884; existing `MicMapApp::initialize()` at line 220.

**Pitfall 8 mandate (RESEARCH 08-RESEARCH.md):** wire `Logger::setLogger(MultiSinkLogger{...})` BEFORE any other code path that might call `MICMAP_LOG_*`. Insertion point = right after `WinMain` opens, before `CreateWindowW` at line 886.

**Composition root pattern (RESEARCH Pattern 3):**
```cpp
// apps/micmap/main.cpp -- WinMain prologue (insert before line 884)
{
    namespace mc = micmap::common;
    auto appdata = getAppDataPath();   // existing helper (search for SHGetKnownFolderPath in main.cpp)
    std::vector<std::shared_ptr<mc::ILogSink>> sinks;
    sinks.push_back(std::make_shared<mc::StdoutLogSink>());
    sinks.push_back(std::make_shared<mc::FileLogSink>(appdata / L"micmap.log"));
    mc::Logger::setLogger(std::make_shared<mc::MultiSinkLogger>(std::move(sinks)));
}
```

**`saveDefault()` deletion (D-07, P8 atomic cutover):** removes `apps/micmap/main.cpp:498` (`if (configManager) configManager->saveDefault();`) AND `apps/micmap/first_launch_balloon.cpp:42` (`configMgr.saveDefault();`). Replace the latter with a `PUT /settings` call once `IDriverApi::putSettings` lands (Wave 4) — until then the first-launch tray balloon flag flip is owned by the driver via PUT.

**UI rewire (D-09 / D-12):**
- on slider/picker change → call `driverApi->putSettings(currentConfig)` → handle 200/4xx/ECONNREFUSED per D-09 ladder
- new section between existing "Status" and "Audio Device" sections (per UI-SPEC §"Section order (post-P8)"): Driver Health pane

---

### `driver/src/device_provider.{hpp,cpp}` (controller, startup-init + atomic-snapshot publish/load)

**Analog:** (self) Init at lines 66-301; especially the existing `driverDetectionActiveGetter` lambda at lines 109-119.

**Logger composition root** (Pitfall 3 mandate: BEFORE `httpServer_->Start()`, IDEALLY first step in Init):
```cpp
// driver/src/device_provider.cpp - Init, BEFORE the existing
// VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext) at line 83.
// Wait — VR_INIT must run first so DriverLogSink's SafeDriverLog can publish to
// vrserver.txt. Pitfall 3 mitigation: place sink wiring immediately AFTER
// VR_INIT_SERVER_DRIVER_CONTEXT and BEFORE the existing DriverLog at line 85.
{
    namespace mc = micmap::common;
    std::vector<std::shared_ptr<mc::ILogSink>> sinks;
    sinks.push_back(std::make_shared<DriverLogSink>());
    sinks.push_back(std::make_shared<mc::FileLogSink>(getAppDataPath() / L"micmap-driver.log"));
    mc::Logger::setLogger(std::make_shared<mc::MultiSinkLogger>(std::move(sinks)));
}
```

**Atomic snapshot member shape** (RESEARCH Pattern 1, generalized from P7 `detection_runner.cpp` lines 85, 99, 294, 304):
```cpp
// driver/src/device_provider.hpp - new private members
std::shared_ptr<const core::AppConfig>          configSnapshot_;
std::shared_ptr<const DriverState>              stateSnapshot_;
std::atomic<float>                              rms_{0.0f};
std::shared_ptr<const std::string>              lastError_;   // accessed via atomic_load/store

// Lock-free getter (mirrors detection_runner.cpp:99 acquire-load):
std::shared_ptr<const core::AppConfig> getConfigSnapshot() const {
    return std::atomic_load_explicit(&configSnapshot_, std::memory_order_acquire);
}

// Validate → persist FIRST, then swap (Pitfall 2 option A from RESEARCH):
bool applyValidatedConfig(core::AppConfig candidate) {
    if (!saveConfigJson(getConfigPath(), candidate)) return false;   // ReplaceFileW
    auto next = std::make_shared<const core::AppConfig>(std::move(candidate));
    std::atomic_store_explicit(&configSnapshot_, next, std::memory_order_release);
    return true;
}
```

**Init read with 3-attempt SHARING_VIOLATION retry (D-10):** call `loadConfigJson(path)` after VR_INIT (so logging works) and before audio worker construction. Driver-only TU per D-04.

---

### `driver/src/http_server.{hpp,cpp}` (controller, request-response)

**Analog:** (self) `SetupRoutes()` at lines 126-184; especially `POST /button` handler (131-154) and `GET /health` handler (165-171).

**Ctor signature evolution pattern** (matches existing P7 D-09 evolution at http_server.hpp:50-67):
```cpp
// http_server.hpp - new ctor (additive callbacks; existing
// driverDetectionActiveGetter retained verbatim until P10):
explicit HttpServer(CommandQueue& queue,
                    int port = 27015,
                    const std::string& host = "127.0.0.1",
                    std::function<bool()> driverDetectionActiveGetter = nullptr,
                    // P8 NEW callbacks (one per route family; mirror P7 pattern):
                    std::function<std::shared_ptr<const core::AppConfig>()> configGetter = nullptr,
                    std::function<HttpResult(const core::AppConfig&)>       configMutator = nullptr,
                    std::function<std::shared_ptr<const DriverState>()>     stateGetter = nullptr,
                    std::function<void()>                                   errorClearer = nullptr,
                    std::function<float()>                                  rmsGetter = nullptr,
                    std::function<std::vector<DeviceInfo>()>                deviceLister = nullptr);
```

**GET handler pattern (lock-free atomic-snapshot read)** — copy shape from existing /health (http_server.cpp:165-171):
```cpp
server_->Get("/state", [this](const httplib::Request&, httplib::Response& res) {
    auto state = stateGetter_();   // wraps atomic_load_explicit on stateSnapshot_
    nlohmann::json body;
    body["driver_loaded"]      = true;   // we are running, by definition
    body["steamvr_running"]    = true;   // ditto (HEALTH-02 = HEALTH-01 in P8)
    body["detection_state"]    = state->detection_state;
    body["last_trigger_at"]    = state->last_trigger_at;   // ISO-8601 or null
    body["last_error"]         = state->last_error;        // string or null
    body["audio_device_id"]    = state->audio_device_id;
    body["audio_device_state"] = state->audio_device_state;
    res.set_content(body.dump(), "application/json");
});
```

**PUT handler pattern (parse → validate → persist-first per Pitfall 2)** — mirror POST /button error envelope (http_server.cpp:131-154):
```cpp
server_->Put("/settings", [this](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); }
    catch (const nlohmann::json::exception&) {
        res.status = 400;
        res.set_content(R"({"field":"(structural)","reason":"malformed JSON body"})",
                        "application/json");
        return;
    }
    core::AppConfig candidate;
    try { candidate = body.get<core::AppConfig>(); }
    catch (const nlohmann::json::exception& e) {
        res.status = 400;
        nlohmann::json err = {{"field", "(structural)"}, {"reason", e.what()}};
        res.set_content(err.dump(), "application/json");
        return;
    }
    if (auto v = validateSettings(candidate); v.has_value()) {
        res.status = 400;
        nlohmann::json err = {{"field", v->field}, {"reason", v->reason}};
        res.set_content(err.dump(), "application/json");
        return;
    }
    // Pitfall 2 — persist-first; configMutator does saveConfigJson() then atomic store.
    if (!configMutator_(candidate)) {
        res.status = 500;
        res.set_content(R"({"error":"persist failed"})", "application/json");
        return;
    }
    res.set_content(R"({"status":"ok"})", "application/json");
});
```

**Localhost-binding pattern (IPC-07):** existing ServerThread already uses `host_.c_str()` at line 196; the new lint freezes the literal in the ctor default at http_server.hpp:66. No code change beyond the new lint.

---

### `driver/src/config_io.{hpp,cpp}` (service, file-I/O)

**Analog:** `src/core/src/config_manager.cpp` — specifically `ConfigManagerImpl::load` (lines 395-433), `appConfigToJson` (lines 255-264), `backupAndRotate` (lines 280-319), and `writeAtomicWindows` (lines 322-377).

**Lift-and-modify shape** — A5 (verified): the three helpers are anonymous-namespace free functions at file scope; safe to copy-paste into a driver TU.

**`saveConfigJson` body** — direct lift from `ConfigManagerImpl::save` lines 436-460:
```cpp
// driver/src/config_io.cpp
#include "config_io.hpp"
#include "micmap/core/config_manager.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace micmap::driver {

// Lifted from src/core/src/config_manager.cpp:280-319 + 322-377 (helper
// functions). Verbatim except:
//   - namespace changed to micmap::driver
//   - logger calls retained (MICMAP_LOG_*); they now flow through the
//     driver's MultiSinkLogger composition root.
namespace {
    void backupAndRotate(const std::filesystem::path&) { /* lifted */ }
    bool writeAtomicWindows(const std::filesystem::path&, const std::string&) { /* lifted */ }
}

bool saveConfigJson(const std::filesystem::path& path, const core::AppConfig& cfg) {
    nlohmann::json j = cfg;   // requires from_json/to_json ADL hooks (D-03)
    return writeAtomicWindows(path, j.dump(4));
}
```

**`loadConfigJson` with 3-attempt SHARING_VIOLATION retry (D-10):** wrap the existing load body with a Win32 retry loop:
```cpp
bool loadConfigJson(const std::filesystem::path& path, core::AppConfig& outCfg) {
#ifdef _WIN32
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::ifstream f(path);
        if (f) {
            // Existing parse path (mirrors config_manager.cpp:395-433).
            // ... parse → assign to outCfg → return true
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION) {
            // Missing file is NOT an error — defaults are already in outCfg.
            MICMAP_LOG_INFO("No config file at ", path.string(), "; using defaults");
            return true;
        }
        MICMAP_LOG_WARNING("config.json sharing violation (attempt ", attempt + 1, "/3); retrying in 50 ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    MICMAP_LOG_WARNING("config.json read failed after 3 attempts; loading defaults");
    return true;   // fail-soft per CONTEXT D-10
#else
    return false;   // mic_test stub
#endif
}
```

**Refactor recommendation (A5):** alternatively, refactor `writeAtomicWindows` + `backupAndRotate` from anonymous namespace into a header (`src/core/include/micmap/core/config_io_helpers.hpp`) so driver and shared-lib TUs share one definition. Avoids drift; D-02 lint still passes because the helper does NOT include `<nlohmann/json.hpp>` (it takes a pre-serialized `std::string` per line 322). PLAN should pick: copy-paste vs header-extract.

---

### `apps/micmap/src/config_json.cpp` (utility, transform — JSON ADL)

**Analog:** `src/core/src/config_manager.cpp` lines 243-264 (`audioToJson`, `detectionToJson`, `steamvrToJson`, `trainingToJson`, `appConfigToJson`)

**Recommended (RESEARCH Open Q5 option (c)):** duplicated driver-only TU + client-only TU; both define hooks; mic_test never includes either. Keeps `cmake/AssertNoJsonInCore.cmake` simple (no exception list).

**Body** — lift-and-adapt the existing `appConfigToJson` builder (lines 255-264) into ADL `to_json` form:
```cpp
// apps/micmap/src/config_json.cpp
#include "micmap/core/config_manager.hpp"
#include <nlohmann/json.hpp>

namespace micmap::core {

void to_json(nlohmann::json& j, const AudioConfig& c) { /* mirror audioToJson */ }
void from_json(const nlohmann::json& j, AudioConfig& c) {
    // Default-on-missing-or-wrong-type matches v1.5 readWString/readInt
    // behavior at src/core/src/config_manager.cpp.
    if (j.contains("bufferSizeMs") && j["bufferSizeMs"].is_number_integer())
        c.bufferSizeMs = j["bufferSizeMs"].get<int>();
    // ... wstring fields use the existing UTF-8 boundary helper
}
// Repeat for DetectionConfig, SteamVRConfig, TrainingConfig, AppConfig
}
```

**UTF-8 wstring boundary:** the existing `config_manager.cpp` already handles wstring↔UTF-8 conversion (search `readWString` / `wstringFromUtf8`); reuse those helpers verbatim.

---

### `driver/src/settings_validator.{hpp,cpp}` (service-validator, transform)

**Analog:** No exact match in tree. Closest reference patterns:
- `src/core/src/config_manager.cpp` clamp paths in `readDetection`/`readAudio` (search the file for `clamp` / `pow2`) — but these CLAMP silently; the validator must REJECT first (Pitfall 1 from RESEARCH).
- `driver/src/http_server.cpp:131-154` for the `std::optional<error>` early-return pattern.

**Recommended shape (Claude's Discretion D-15 — flat free functions for ≤30 fields):**
```cpp
// driver/src/settings_validator.hpp
#pragma once
#include "micmap/core/config_manager.hpp"
#include <optional>
#include <string>

namespace micmap::driver {

struct ValidationError {
    std::string field;    // dot-path or JSON-Pointer; pick one (RESEARCH Open Q recommendation)
    std::string reason;   // human-readable
};

/// Returns the FIRST failed field; nullopt if valid (D-15 first-failed-field).
/// Validator runs BEFORE any clamp (Pitfall 1).
std::optional<ValidationError> validateSettings(const core::AppConfig& cfg);

} // namespace
```

**Body skeleton (early-return on first failure):**
```cpp
std::optional<ValidationError> validateSettings(const core::AppConfig& cfg) {
    if (cfg.detection.sensitivity < 0.0f || cfg.detection.sensitivity > 1.0f)
        return ValidationError{"detection.sensitivity",
            "must be in [0.0, 1.0]; got " + std::to_string(cfg.detection.sensitivity)};
    if (cfg.detection.minDurationMs < 0 || cfg.detection.minDurationMs > 60000)
        return ValidationError{"detection.minDurationMs", "must be in [0, 60000]"};
    if (cfg.detection.cooldownMs < 0 || cfg.detection.cooldownMs > 60000)
        return ValidationError{"detection.cooldownMs", "must be in [0, 60000]"};
    if (cfg.detection.fftSize < 64 || cfg.detection.fftSize > 16384
        || (cfg.detection.fftSize & (cfg.detection.fftSize - 1)) != 0)
        return ValidationError{"detection.fftSize", "must be power-of-2 in [64, 16384]"};
    if (cfg.audio.bufferSizeMs < 1 || cfg.audio.bufferSizeMs > 1000)
        return ValidationError{"audio.bufferSizeMs", "must be in [1, 1000]"};
    // ... ~10 more fields per AppConfig schema
    return std::nullopt;
}
```

**Field-path style (Claude's Discretion):** dot-paths (`detection.sensitivity`) match the AppConfig struct nesting more naturally than JSON-Pointer (`/detection/sensitivity`). Document the choice in PLAN.

---

### `src/steamvr/include/micmap/steamvr/driver_api.hpp` + `src/steamvr/src/driver_api.cpp` (interface + service rename)

**Analog:** (self) `vr_input.hpp` + `vr_input.cpp` — pure file rename per D-22.

**Rename mechanics:**
1. `git mv src/steamvr/include/micmap/steamvr/vr_input.hpp src/steamvr/include/micmap/steamvr/driver_api.hpp`
2. `git mv src/steamvr/src/vr_input.cpp src/steamvr/src/driver_api.cpp`
3. In `driver_api.hpp`: rename class `IDriverClient` → `IDriverApi`; rename `createDriverClient` → `createDriverApi`. Keep `IVRInput` and `createOpenVRInput` UNCHANGED in the same header (they're orthogonal).
4. In `driver_api.cpp`: rename class `DriverClient` → `DriverApi`; update factory.
5. Grep sweep across `src/`, `apps/`, `driver/`, `tests/` (Pitfall 9 from RESEARCH) for `vr_input` / `IDriverClient` / `driver_client` / `DriverClient` and update.

**Pitfall 6 mitigation in same plan (D-22 + RESEARCH Open Q6):** upgrade `connect()` from bool to a 3-state enum and use `Result::error() == httplib::Error::Connection`. Patch site = `vr_input.cpp:127-155` (post-rename: `driver_api.cpp:127-155`).

```cpp
// driver_api.hpp - new return type for connect()
enum class ConnectResult { Connected, NotFound, Timeout, OtherError };

class IDriverApi {
public:
    virtual ~IDriverApi() = default;
    virtual ConnectResult connect() = 0;   // CHANGED from bool
    // ... rest unchanged for now; new methods (getState, getSettings, etc.)
    // added incrementally per their endpoint plan (D-23).
};
```

**Connect impl (replace lines 127-155):**
```cpp
ConnectResult connect() override {
    if (connected_) return ConnectResult::Connected;
    MICMAP_LOG_INFO("Connecting to MicMap driver...");
    bool sawTimeout = false;
    for (int port = startPort_; port <= endPort_; ++port) {
        httplib::Client client(host_, port);
        client.set_connection_timeout(1);
        client.set_read_timeout(1);
        auto res = client.Get("/health");
        if (res && res->status == 200) {
            port_ = port;
            connected_ = true;
            MICMAP_LOG_INFO("Connected to MicMap driver on port ", port_);
            return ConnectResult::Connected;
        }
        if (!res) {
            using E = httplib::Error;
            switch (res.error()) {
                case E::Connection: continue;            // ECONNREFUSED — try next port
                case E::Read:
                case E::Write:      sawTimeout = true; continue;
                default:            sawTimeout = true; continue;
            }
        }
    }
    return sawTimeout ? ConnectResult::Timeout : ConnectResult::NotFound;
}
```

---

### `driver/src/driver_state.hpp` (model, atomic-snapshot)

**Analog:** `driver/src/detection_runner.hpp:48-53` (DetectionConfig POD struct shape)

**Shape:**
```cpp
// driver/src/driver_state.hpp
#pragma once
#include <string>
#include <chrono>
#include <optional>

namespace micmap::driver {

struct DriverState {
    std::string detection_state;     // "idle"|"training"|"detecting"|"triggered"|"cooldown"
    std::optional<std::chrono::system_clock::time_point> last_trigger_at;
    std::optional<std::string>       last_error;
    std::string                      audio_device_id;     // empty if none selected
    std::string                      audio_device_state;  // "ok"|"missing"|"permission_denied"
};

} // namespace
```

Held in DeviceProvider as `std::shared_ptr<const DriverState>` accessed via `std::atomic_load_explicit`/`atomic_store_explicit` (P7 D-15 mechanism — see detection_runner.cpp:85, 99, 294, 304).

---

## Shared Patterns

### Pattern A: Atomic-Snapshot Publish/Load (P7 D-15, generalized in P8)

**Source (canonical):** `driver/src/detection_runner.cpp:85, 99, 294, 304`

**Snippet** (publish — line 294):
```cpp
std::atomic_store_explicit(&activeConfig_, std::move(next),
                           std::memory_order_release);
```

**Snippet** (load — line 99/304):
```cpp
auto cfg = std::atomic_load_explicit(&activeConfig_, std::memory_order_acquire);
```

**Apply to (all atomic fields in DeviceProvider):**
- `configSnapshot_` (`std::shared_ptr<const core::AppConfig>`) — written on PUT /settings
- `stateSnapshot_` (`std::shared_ptr<const DriverState>`) — written by AudioWorker (device state) + DetectionRunner (state-pill, last_trigger_at) + DeviceProvider error channel
- `lastError_` (`std::shared_ptr<const std::string>`) — written by error producers and POST /state/clear-error

**`std::atomic<float>` for telemetry** (lock-free single-word):
- `rms_` — written by audio callback, read by GET /telemetry/level handler. Pattern: copy `audio_worker.hpp:124` (`std::atomic<uint32_t> sample_rate{0}`) + `audio_worker.cpp:354` (release-store) + `audio_worker.cpp:135` (acquire-load via `sample_rate()`).

---

### Pattern B: HTTP Route Handler (lambda + nlohmann + 200/4xx envelopes)

**Source:** `driver/src/http_server.cpp:131-184` (POST /button + GET /health + GET /port + GET /status)

**Imports header pattern** (http_server.cpp:10-18):
```cpp
#include "http_server.hpp"
#include "command_queue.hpp"
#include "driver_log.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
```

**Apply to:** all 6 new P8 routes. Always use `try`/`catch (const nlohmann::json::exception&)` around `nlohmann::json::parse(req.body)` (Pitfall 5 from POST /button impl). Always set `res.status = <code>` BEFORE `res.set_content(...)`. Always emit `application/json` mime.

---

### Pattern C: Composition-Root Logger Init (LIB-04, RESEARCH Pattern 3)

**Source:** `src/common/src/logger.cpp:63` static-init default — DELIBERATELY left in place; the composition root REPLACES it after both binaries' Logger::setLogger() runs.

**Apply to:**
- Driver: `device_provider.cpp::Init` immediately after `VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext)` (line 83), BEFORE the existing `DriverLog("MicMap driver initializing...")` at line 85.
- Client: `apps/micmap/main.cpp::WinMain` prologue, BEFORE `MicMapApp::initialize()` invocation at line 924, ideally as the very first action after WinMain opens.

**Pitfall 3 + 8 mandate:** wire the sink list FIRST so the very first log line lands in the file sink.

---

### Pattern D: P7 Getter-Callback ctor evolution (HttpServer adds N callbacks)

**Source:** `driver/src/http_server.hpp:64-67` + `device_provider.cpp:109-119`

**Snippet (ctor callback)** (http_server.hpp:67):
```cpp
std::function<bool()> driverDetectionActiveGetter = nullptr
```

**Snippet (callback wiring)** (device_provider.cpp:109-119):
```cpp
auto driverDetectionActiveGetter = [this]() {
    return driverDetectionEnabled_
        && audioWorker_
        && detectionRunner_
        && detectionRunner_->IsRunning();
};
httpServer_ = std::make_unique<HttpServer>(
    *commandQueue_, /*port=*/27015, /*host=*/"127.0.0.1",
    std::move(driverDetectionActiveGetter));
```

**Apply to:** each new P8 route gets its own callback (one-per-concern, not one-per-route — group by data source). Recommended grouping:
- `configGetter` (for GET /settings) + `configMutator` (for PUT /settings) — both touch configSnapshot_
- `stateGetter` (for GET /state) — touches stateSnapshot_
- `errorClearer` (for POST /state/clear-error) — touches lastError_ atomic
- `rmsGetter` (for GET /telemetry/level) — touches rms_ atomic
- `deviceLister` (for GET /devices) — invokes AudioWorker enumerator with a 1 s cache

---

### Pattern E: Wave 0 RED-tolerant ctest registration (test scaffolds + lints exist before impl)

**Source:** `tests/CMakeLists.txt:150-178` (P6 AudioWorker) and `tests/CMakeLists.txt:197-214` (P7 DetectionSettingsPropagation)

**Snippet — source-list gate:**
```cmake
set(_p8_<name>_sources driver/<name>_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/<dependency>.cpp")
    list(APPEND _p8_<name>_sources
        "${CMAKE_SOURCE_DIR}/driver/src/<dependency>.cpp")
endif()
```

**Snippet — lint skip-on-NOT-EXISTS** (AssertDetectionRunnerNoVrApi.cmake:48-56):
```cmake
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        continue()    # Wave 0 RED-tolerant skip
    endif()
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    # ... regex check
endforeach()
```

**Apply to:** all 16 Wave 0 scaffolds. Test exes that depend on yet-to-land impl files build only when the impl exists; lints scan only when targets exist. Keeps ctest GREEN at the start of P8 even with all scaffolds registered.

---

### Pattern F: Plain-main exit-code test convention

**Source:** `tests/driver/audio_worker_lifecycle_headless.cpp:33-87` and `tests/driver/detection_settings_propagation_test.cpp:30-76`

**Apply to:** all new `tests/driver/*_test.cpp` and `tests/test_*.cpp` files. NOT GoogleTest — the project uses a plain-main convention even when `MICMAP_USE_GTEST=ON` is available (the option exists but no test uses it; tests/CMakeLists.txt:8). Per-case PASS lines on stdout + exit 0 = green; exit 1 + FAIL line on stderr = red.

**MM_CHECK macro** (audio_worker_lifecycle_headless.cpp:33-35):
```cpp
#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)
```

---

### Pattern G: cpp-httplib bump (external/CMakeLists.txt)

**Source:** `external/CMakeLists.txt:57-75`

**Snippet (existing v0.14.3 declaration):**
```cmake
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.3              # CHANGE TO v0.20.1 in plan 08-00
)
```

**Apply to:** plan `08-00` only changes the `GIT_TAG` literal. Smoke = D-06 (existing v1.5 trigger UAT + Phase 6/7 flag-OFF regression). Revert path = single-line revert.

---

## Specific Pitfall Mitigations Anchored to Code

| Pitfall | Where to Apply | Anchor in Codebase |
|---------|---------------|--------------------|
| **Pitfall 2 (persist-first PUT)** | http_server PUT /settings handler | configMutator_ inside DeviceProvider does `saveConfigJson()` BEFORE `atomic_store_explicit`. Mirror the P7 publish-after-success ordering at `detection_runner.cpp:85`. |
| **Pitfall 3 (Logger init too late)** | device_provider.cpp::Init | Insert sink wiring BEFORE the first `DriverLog("MicMap driver initializing...")` at line 85. |
| **Pitfall 6 (ECONNREFUSED-vs-timeout)** | driver_api.cpp::connect | Replace the truthiness check at vr_input.cpp:144 with `res.error() == httplib::Error::Connection`. RESEARCH Example 6 has the full snippet. |
| **Pitfall 8 (client logger init too late)** | apps/micmap/main.cpp::WinMain | Hoist `Logger::setLogger(MultiSinkLogger{...})` to BEFORE `MicMapApp::initialize()` at line 924. |
| **Pitfall 9 (rename grep sweep)** | plan 08-01 instructions | After `git mv`, run `grep -rn 'vr_input\|IDriverClient\|driver_client\|DriverClient' src/ apps/ tests/ driver/`. |

---

## Files With No Direct Analog

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `driver/src/settings_validator.cpp` | service (validator) | transform | No existing per-field validator with `optional<error>` early-return shape; closest is the silent-clamp pattern in `config_manager.cpp readDetection`/`readAudio` which is the WRONG shape (Pitfall 1). Build fresh from RESEARCH Example 2's validator block. |
| `driver/src/sinks/` directory | structural | — | First `driver/src/<subdir>/` directory in the tree; pattern is "flat under driver/src/" historically (audio_worker, detection_runner, http_server, etc.). PLAN should decide: nest under `driver/src/sinks/` for symmetry with `src/common/src/sinks/`, or flat alongside `driver_log.hpp`. Recommend nest for symmetry. |

---

## Metadata

**Analog search scope:** `cmake/`, `src/`, `apps/micmap/`, `driver/src/`, `tests/`, `external/`
**Files scanned (Read tool):** ~28 (impl + headers + tests + lints + CMake)
**Files referenced via Grep:** ~15 additional (one-line confirmations for line numbers)
**Pattern extraction date:** 2026-05-05
**Branch HEAD:** `407d8b1` (hmd-button)

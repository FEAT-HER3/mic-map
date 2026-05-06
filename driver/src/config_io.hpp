/**
 * @file config_io.hpp
 * @brief Phase 8 D-10 / D-14: driver-side config.json I/O.
 *
 * loadConfigJson is called once at DeviceProvider::Init with a 3-attempt
 * SHARING_VIOLATION retry (50 ms backoff). saveConfigJson is called by
 * the PUT /settings handler in 08-04 with persist-first ordering (Pitfall 2).
 *
 * Driver-only TU (Pitfall 15 / D-01). Includes <nlohmann/json.hpp> via the
 * .cpp; CANNOT be moved into micmap_core_runtime (AssertNoJsonInCore lint
 * forbids).
 */

#pragma once

#include "micmap/core/config_manager.hpp"   // AppConfig schema (no JSON include)
#include <filesystem>

namespace micmap::driver {

/// @brief Resolve %APPDATA%\\MicMap\\config.json (Windows). Mirrors v1.5
///        getAppDataPath at src/core/src/config_manager.cpp:33-49.
std::filesystem::path getDriverConfigPath();

/// @brief Resolve %APPDATA%\\MicMap\\ (no filename). Exposed so DeviceProvider
///        can compute %APPDATA%\\MicMap\\micmap-driver.log without duplicating
///        the SHGetFolderPathW logic.
std::filesystem::path getDriverAppDataDir();

/// @brief Read config.json with 3-attempt SHARING_VIOLATION retry (D-10).
///        On all-attempt failure or missing-file, leaves outCfg at its
///        ctor defaults and returns true (fail-soft). Returns false only
///        on programmer error (path == "").
bool loadConfigJson(const std::filesystem::path& path, micmap::core::AppConfig& outCfg);

/// @brief Atomic save via ReplaceFileW (D-14 + v1.5 CFG-04). Returns false
///        on disk-full, ACL block, or temp-file failure. The caller (PUT
///        handler in 08-04) checks this BEFORE swapping the atomic snapshot
///        (Pitfall 2 persist-first).
bool saveConfigJson(const std::filesystem::path& path, const micmap::core::AppConfig& cfg);

} // namespace

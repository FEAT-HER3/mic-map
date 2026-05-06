/**
 * @file config_io.cpp
 * @brief Implementation of driver-side config.json I/O (P8 D-10 / D-14).
 *
 * Helper bodies (writeAtomicWindows / backupAndRotate / makeCorruptedSuffix /
 * getAppDataPath) are LIFTED VERBATIM from v1.5
 * src/core/src/config_manager.cpp (CFG-04). Refactor to a shared header
 * would also need lifting nlohmann out of core; that's a Phase 11 hardening
 * item. Lift-and-modify is the smaller diff for P8.
 */

#include "config_io.hpp"
#include "config_json.hpp"   // P8 D-03: declarations of AppConfig to_json/from_json ADL hooks
#include "micmap/common/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace micmap::driver {

namespace {

using json = nlohmann::json;

std::filesystem::path getAppDataPath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"MicMap";
    }
#endif
    return std::filesystem::current_path() / L".micmap";
}

std::string makeCorruptedSuffix() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
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
    // VERBATIM lift of src/core/src/config_manager.cpp:280-319 (v1.5 CFG-04).
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(configPath, ec) || ec) return;
    const auto backup = configPath.parent_path() /
        ("config.json.corrupted." + makeCorruptedSuffix());
    fs::rename(configPath, backup, ec);
    if (ec) {
        MICMAP_LOG_ERROR("Could not rename corrupt config to ", backup.string(), ": ", ec.message());
        return;
    }
    MICMAP_LOG_WARNING("Backed up corrupt config to: ", backup.string());

    std::vector<fs::path> backups;
    std::error_code itEc;
    for (const auto& entry : fs::directory_iterator(configPath.parent_path(), itEc)) {
        if (itEc) break;
        const auto name = entry.path().filename().string();
        if (name.rfind("config.json.corrupted.", 0) == 0) backups.push_back(entry.path());
    }
    std::sort(backups.begin(), backups.end(), std::greater<>());
    for (size_t i = 5; i < backups.size(); ++i) {
        std::error_code remEc;
        fs::remove(backups[i], remEc);
    }
}

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
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            MICMAP_LOG_ERROR("Could not open temp config file: ", tmp.string());
            return false;
        }
        f.write(utf8Content.data(), (std::streamsize)utf8Content.size());
        if (!f) {
            f.close();
            std::error_code remEc;
            fs::remove(tmp, remEc);
            return false;
        }
        f.flush();
    }
    if (fs::exists(dest, ec)) {
        if (!ReplaceFileW(dest.c_str(), tmp.c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("ReplaceFileW failed (GetLastError=", err, ")");
            std::error_code remEc;
            fs::remove(tmp, remEc);
            return false;
        }
    } else {
        if (!MoveFileExW(tmp.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("MoveFileExW failed on first save (GetLastError=", err, ")");
            std::error_code remEc;
            fs::remove(tmp, remEc);
            return false;
        }
    }
    return true;
}
#else
bool writeAtomicWindows(const std::filesystem::path&, const std::string&) { return false; }
#endif

} // anonymous namespace

std::filesystem::path getDriverAppDataDir() {
    return getAppDataPath();
}

std::filesystem::path getDriverConfigPath() {
    return getAppDataPath() / L"config.json";
}

bool loadConfigJson(const std::filesystem::path& path, core::AppConfig& outCfg) {
    if (path.empty()) return false;
#ifdef _WIN32
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::stringstream buf; buf << f.rdbuf();
            const std::string content = buf.str();
            f.close();
            json j = json::parse(content, /*cb=*/nullptr, /*allow_exceptions=*/false);
            if (j.is_discarded() || !j.is_object()) {
                MICMAP_LOG_WARNING("Config file corrupted; backing up and using defaults: ", path.string());
                backupAndRotate(path);
                return true;   // outCfg stays at defaults; fail-soft per D-10
            }
            try { j.get_to(outCfg); }
            catch (const json::exception& e) {
                MICMAP_LOG_WARNING("Config from_json threw: ", e.what(), "; backing up + defaults");
                backupAndRotate(path);
                return true;
            }
            MICMAP_LOG_INFO("Driver loaded config from: ", path.string());
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION) {
            // Missing file is NOT an error - defaults already in outCfg.
            MICMAP_LOG_INFO("No config file at ", path.string(), "; using defaults (errno=", err, ")");
            return true;
        }
        MICMAP_LOG_WARNING("config.json sharing violation (attempt ", attempt + 1, "/3); retrying in 50 ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    MICMAP_LOG_WARNING("config.json read failed after 3 SHARING_VIOLATION attempts; loading defaults");
    return true;
#else
    (void)path; (void)outCfg;
    return true;
#endif
}

bool saveConfigJson(const std::filesystem::path& path, const core::AppConfig& cfg) {
#ifdef _WIN32
    nlohmann::json j = cfg;   // requires to_json hooks (Task 1)
    return writeAtomicWindows(path, j.dump(4));
#else
    (void)path; (void)cfg;
    return false;
#endif
}

} // namespace

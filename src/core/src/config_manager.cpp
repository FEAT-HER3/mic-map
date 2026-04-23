/**
 * @file config_manager.cpp
 * @brief Configuration manager implementation
 */

#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace micmap::core {

namespace {

using json = nlohmann::json;

std::filesystem::path getAppDataPath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / "MicMap";
    }
#endif
    return std::filesystem::current_path() / ".micmap";
}

std::string toUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     w.data(), static_cast<int>(w.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), 0);
    WideCharToMultiByte(CP_UTF8, 0,
                        w.data(), static_cast<int>(w.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
#else
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        if (c < 128) out.push_back(static_cast<char>(c));
    }
    return out;
#endif
}

bool fromUtf8(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     s.data(), static_cast<int>(s.size()),
                                     nullptr, 0);
    if (needed <= 0) return false;
    out.resize(static_cast<size_t>(needed));
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      s.data(), static_cast<int>(s.size()),
                                      out.data(), needed);
    return written == needed;
#else
    out.assign(s.begin(), s.end());
    return true;
#endif
}

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
    if (it == obj.end() || it->is_null() || !it->is_string()) return defaultVal;
    return it->get<std::string>();
}

std::wstring readWString(const json& obj, const char* key, std::wstring defaultVal) {
    if (!obj.is_object()) return defaultVal;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null() || !it->is_string()) return defaultVal;
    const std::string utf8 = it->get<std::string>();
    std::wstring out;
    if (!fromUtf8(utf8, out)) {
        MICMAP_LOG_WARNING("Config field has invalid UTF-8; using default: ", key);
        return defaultVal;
    }
    return out;
}

const json& readSubObject(const json& obj, const char* key) {
    static const json kEmpty = json::object();
    if (!obj.is_object()) return kEmpty;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_object()) return kEmpty;
    return *it;
}

template <typename T>
T clampRange(T value, T lo, T hi, const char* name) {
    if (value < lo) {
        MICMAP_LOG_WARNING("Config ", name, "=", value, " out of range; clamping to ", lo);
        return lo;
    }
    if (value > hi) {
        MICMAP_LOG_WARNING("Config ", name, "=", value, " out of range; clamping to ", hi);
        return hi;
    }
    return value;
}

int snapPowerOfTwo(int value, int lo, int hi, const char* name) {
    int v = value < lo ? lo : (value > hi ? hi : value);
    if (v > 0 && (v & (v - 1)) == 0 && v == value && value >= lo && value <= hi) {
        return value;
    }
    int p = lo;
    int next = p * 2;
    while (next <= hi && next <= v) { p = next; next = p * 2; }
    int chosen;
    if (next > hi) {
        chosen = p;
    } else {
        chosen = (v - p <= next - v) ? p : next;
    }
    if (chosen != value) {
        MICMAP_LOG_WARNING("Config ", name, "=", value, " not a power of 2; snapping to ", chosen);
    }
    return chosen;
}

void readAudio(const json& root, AudioConfig& out) {
    const json& a = readSubObject(root, "audio");
    out.deviceNamePattern = readWString(a, "deviceNamePattern", out.deviceNamePattern);
    out.deviceId          = readWString(a, "deviceId",          out.deviceId);
    out.bufferSizeMs      = clampRange(readInt(a, "bufferSizeMs", out.bufferSizeMs),
                                       5, 100, "audio.bufferSizeMs");
}

void readDetection(const json& root, DetectionConfig& out) {
    const json& d = readSubObject(root, "detection");
    out.sensitivity   = clampRange(readFloat(d, "sensitivity",   out.sensitivity),
                                   0.0f, 1.0f, "detection.sensitivity");
    out.minDurationMs = clampRange(readInt(d,   "minDurationMs", out.minDurationMs),
                                   100, 2000, "detection.minDurationMs");
    out.cooldownMs    = clampRange(readInt(d,   "cooldownMs",    out.cooldownMs),
                                   100, 2000, "detection.cooldownMs");
    out.fftSize       = snapPowerOfTwo(readInt(d, "fftSize",      out.fftSize),
                                       512, 8192, "detection.fftSize");
}

void readSteamVR(const json& root, SteamVRConfig& out) {
    const json& s = readSubObject(root, "steamvr");
    out.dashboardClickEnabled = readBool(s, "dashboardClickEnabled", out.dashboardClickEnabled);
    out.customActionBinding   = readString(s, "customActionBinding", out.customActionBinding);
}

void readTraining(const json& root, TrainingConfig& out) {
    const json& t = readSubObject(root, "training");
    out.dataFile = readString(t, "dataFile", out.dataFile);

    if (!t.is_object()) return;
    auto it = t.find("lastTrainedTimestamp");
    if (it == t.end() || it->is_null()) {
        out.lastTrainedTimestamp = std::nullopt;
    } else if (it->is_number_integer()) {
        const auto secs = it->get<std::int64_t>();
        out.lastTrainedTimestamp =
            std::chrono::system_clock::from_time_t(static_cast<std::time_t>(secs));
    } else {
        MICMAP_LOG_WARNING("Config training.lastTrainedTimestamp has wrong type; using default");
    }
}

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
        j["lastTrainedTimestamp"] = static_cast<std::int64_t>(
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
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(configPath, ec) || ec) return;

    const auto backup = configPath.parent_path() /
        ("config.json.corrupted." + makeCorruptedSuffix());
    fs::rename(configPath, backup, ec);
    if (ec) {
        MICMAP_LOG_ERROR("Could not rename corrupt config to ", backup.string(),
                         ": ", ec.message());
        return;
    }
    MICMAP_LOG_WARNING("Backed up corrupt config to: ", backup.string());

    std::vector<fs::path> backups;
    std::error_code itEc;
    for (const auto& entry : fs::directory_iterator(configPath.parent_path(), itEc)) {
        if (itEc) break;
        const auto name = entry.path().filename().string();
        if (name.rfind("config.json.corrupted.", 0) == 0) {
            backups.push_back(entry.path());
        }
    }
    std::sort(backups.begin(), backups.end(), std::greater<>());
    for (size_t i = 5; i < backups.size(); ++i) {
        std::error_code remEc;
        fs::remove(backups[i], remEc);
        if (remEc) {
            MICMAP_LOG_WARNING("Could not prune old backup ", backups[i].string(),
                               ": ", remEc.message());
        }
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
        f.write(utf8Content.data(), static_cast<std::streamsize>(utf8Content.size()));
        if (!f) {
            MICMAP_LOG_ERROR("Write to temp config file failed");
            return false;
        }
        f.flush();
    }

    if (fs::exists(dest, ec)) {
        if (!ReplaceFileW(dest.c_str(), tmp.c_str(),
                          nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS,
                          nullptr, nullptr)) {
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
bool writeAtomicWindows(const std::filesystem::path&, const std::string&) {
    return false;
}
#endif

} // anonymous namespace

class ConfigManagerImpl : public IConfigManager {
public:
    ConfigManagerImpl() {
        configDir_ = getAppDataPath();
        resetToDefaults();
    }

    ~ConfigManagerImpl() override = default;

    bool load(const std::filesystem::path& path) override {
        std::string content;
        {
            std::ifstream file(path);
            if (!file) {
                // D-16: missing file is NOT corruption; defaults already loaded by ctor.
                MICMAP_LOG_INFO("No config file at ", path.string(), "; using defaults");
                return true;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
        } // close the file handle BEFORE backupAndRotate may try to rename it

        json j = json::parse(content, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            // D-15: top-level corruption -- back up + reset to defaults + return success.
            MICMAP_LOG_WARNING("Config file corrupted; backing up and using defaults: ",
                               path.string());
            backupAndRotate(path);
            resetToDefaults();
            return true;
        }

        // D-12: version best-effort -- log mismatch, keep parsing regardless.
        const int onDiskVersion = readInt(j, "version", config_.version);
        if (onDiskVersion != config_.version) {
            MICMAP_LOG_INFO("Config written by version ", onDiskVersion,
                            ", reading on version ", config_.version);
        }

        readAudio(j, config_.audio);
        readDetection(j, config_.detection);
        readSteamVR(j, config_.steamvr);
        readTraining(j, config_.training);

        MICMAP_LOG_INFO("Loaded config from: ", path.string());
        return true;
    }

    bool save(const std::filesystem::path& path) override {
#ifdef _WIN32
        const std::string body = appConfigToJson(config_).dump(4);
        if (!writeAtomicWindows(path, body)) {
            return false;  // writeAtomicWindows already logged the failure cause.
        }
        MICMAP_LOG_INFO("Saved config to: ", path.string());
        return true;
#else
        // Non-Windows fallback -- direct write, consistent with project Windows-only milestone.
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            MICMAP_LOG_ERROR("Could not create config directory: ", ec.message());
            return false;
        }
        std::ofstream file(path);
        if (!file) {
            MICMAP_LOG_ERROR("Could not open config file for writing: ", path.string());
            return false;
        }
        file << appConfigToJson(config_).dump(4);
        MICMAP_LOG_INFO("Saved config to: ", path.string());
        return true;
#endif
    }

    bool loadDefault() override {
        return load(getDefaultConfigPath());
    }

    bool saveDefault() override {
        return save(getDefaultConfigPath());
    }

    const AppConfig& getConfig() const override {
        return config_;
    }

    AppConfig& getConfig() override {
        return config_;
    }

    void resetToDefaults() override {
        config_ = AppConfig{};
        MICMAP_LOG_DEBUG("Configuration reset to defaults");
    }

    std::filesystem::path getConfigDirectory() const override {
        return configDir_;
    }

    std::filesystem::path getDefaultConfigPath() const override {
        return configDir_ / "config.json";
    }

    std::filesystem::path getTrainingDataPath() const override {
        return configDir_ / config_.training.dataFile;
    }

private:
    AppConfig config_;
    std::filesystem::path configDir_;
};

std::unique_ptr<IConfigManager> createConfigManager() {
    return std::make_unique<ConfigManagerImpl>();
}

} // namespace micmap::core

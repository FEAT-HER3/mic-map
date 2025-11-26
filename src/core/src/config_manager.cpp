/**
 * @file config_manager.cpp
 * @brief Configuration manager implementation
 */

#include "micmap/core/config_manager.hpp"
#include "micmap/common/logger.hpp"

#include <fstream>
#include <sstream>
#include <map>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

// Simple JSON parsing/writing without external dependencies
// In production, this would use nlohmann/json

namespace micmap::core {

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

// Simple JSON value representation
struct JsonValue {
    enum class Type { Null, Bool, Int, Float, String, Object };
    Type type = Type::Null;
    bool boolVal = false;
    int intVal = 0;
    float floatVal = 0.0f;
    std::string stringVal;
    std::map<std::string, JsonValue> objectVal;
};

// Very basic JSON writer
std::string toJson(const AppConfig& config) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "    \"version\": " << config.version << ",\n";
    
    // Audio section
    oss << "    \"audio\": {\n";
    oss << "        \"deviceNamePattern\": \"";
    // Convert wstring to string for JSON
    for (wchar_t c : config.audio.deviceNamePattern) {
        if (c < 128) oss << static_cast<char>(c);
    }
    oss << "\",\n";
    oss << "        \"deviceId\": ";
    if (config.audio.deviceId.empty()) {
        oss << "null";
    } else {
        oss << "\"";
        for (wchar_t c : config.audio.deviceId) {
            if (c < 128) oss << static_cast<char>(c);
        }
        oss << "\"";
    }
    oss << ",\n";
    oss << "        \"bufferSizeMs\": " << config.audio.bufferSizeMs << "\n";
    oss << "    },\n";
    
    // Detection section
    oss << "    \"detection\": {\n";
    oss << "        \"sensitivity\": " << config.detection.sensitivity << ",\n";
    oss << "        \"minDurationMs\": " << config.detection.minDurationMs << ",\n";
    oss << "        \"cooldownMs\": " << config.detection.cooldownMs << ",\n";
    oss << "        \"fftSize\": " << config.detection.fftSize << "\n";
    oss << "    },\n";
    
    // SteamVR section
    oss << "    \"steamvr\": {\n";
    oss << "        \"dashboardClickEnabled\": " << (config.steamvr.dashboardClickEnabled ? "true" : "false") << ",\n";
    oss << "        \"customActionBinding\": ";
    if (config.steamvr.customActionBinding.empty()) {
        oss << "null";
    } else {
        oss << "\"" << config.steamvr.customActionBinding << "\"";
    }
    oss << "\n";
    oss << "    },\n";
    
    // Training section
    oss << "    \"training\": {\n";
    oss << "        \"dataFile\": \"" << config.training.dataFile << "\",\n";
    oss << "        \"lastTrainedTimestamp\": ";
    if (config.training.lastTrainedTimestamp.has_value()) {
        auto time = std::chrono::system_clock::to_time_t(*config.training.lastTrainedTimestamp);
        oss << time;
    } else {
        oss << "null";
    }
    oss << "\n";
    oss << "    }\n";
    
    oss << "}\n";
    return oss.str();
}

} // anonymous namespace

/**
 * @brief Configuration manager implementation
 */
class ConfigManagerImpl : public IConfigManager {
public:
    ConfigManagerImpl() {
        configDir_ = getAppDataPath();
        resetToDefaults();
    }
    
    ~ConfigManagerImpl() override = default;
    
    bool load(const std::filesystem::path& path) override {
        std::ifstream file(path);
        if (!file) {
            MICMAP_LOG_WARNING("Could not open config file: ", path.string());
            return false;
        }
        
        // Read entire file
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // Very basic JSON parsing - in production use nlohmann/json
        // For now, just log that we would parse it
        MICMAP_LOG_INFO("Loaded config from: ", path.string());
        
        // TODO: Implement proper JSON parsing
        // For now, keep defaults
        
        return true;
    }
    
    bool save(const std::filesystem::path& path) override {
        // Ensure directory exists
        std::filesystem::create_directories(path.parent_path());
        
        std::ofstream file(path);
        if (!file) {
            MICMAP_LOG_ERROR("Could not open config file for writing: ", path.string());
            return false;
        }
        
        file << toJson(config_);
        
        MICMAP_LOG_INFO("Saved config to: ", path.string());
        return true;
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
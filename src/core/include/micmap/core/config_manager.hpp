#pragma once

/**
 * @file config_manager.hpp
 * @brief Configuration management for MicMap
 */

#include <string>
#include <filesystem>
#include <memory>
#include <optional>
#include <chrono>

namespace micmap::core {

/**
 * @brief Audio configuration
 */
struct AudioConfig {
    std::wstring deviceNamePattern = L"Beyond";  ///< Device name pattern to match
    std::wstring deviceId;                        ///< Specific device ID (overrides pattern)
    int bufferSizeMs = 10;                        ///< Audio buffer size in milliseconds
};

/**
 * @brief Detection configuration
 */
struct DetectionConfig {
    float sensitivity = 0.7f;           ///< Detection sensitivity (0.0 to 1.0)
    int minDurationMs = 300;            ///< Minimum detection duration in ms
    int cooldownMs = 300;               ///< Cooldown after trigger in ms
    int fftSize = 2048;                 ///< FFT window size
};

/**
 * @brief SteamVR configuration
 */
struct SteamVRConfig {
    bool dashboardClickEnabled = true;  ///< Enable dashboard click when open
    std::string customActionBinding;    ///< Custom action binding (optional)
};

/**
 * @brief Training configuration
 */
struct TrainingConfig {
    std::string dataFile = "training_data.bin";  ///< Training data filename
    std::optional<std::chrono::system_clock::time_point> lastTrainedTimestamp;
};

/**
 * @brief Complete application configuration
 */
struct AppConfig {
    int version = 1;                    ///< Configuration version
    AudioConfig audio;                  ///< Audio settings
    DetectionConfig detection;          ///< Detection settings
    SteamVRConfig steamvr;              ///< SteamVR settings
    TrainingConfig training;            ///< Training settings
};

/**
 * @brief Interface for configuration management
 */
class IConfigManager {
public:
    virtual ~IConfigManager() = default;
    
    /**
     * @brief Load configuration from file
     * @param path Path to configuration file
     * @return True if load was successful
     */
    virtual bool load(const std::filesystem::path& path) = 0;
    
    /**
     * @brief Save configuration to file
     * @param path Path to configuration file
     * @return True if save was successful
     */
    virtual bool save(const std::filesystem::path& path) = 0;
    
    /**
     * @brief Load configuration from default location
     * @return True if load was successful
     */
    virtual bool loadDefault() = 0;
    
    /**
     * @brief Save configuration to default location
     * @return True if save was successful
     */
    virtual bool saveDefault() = 0;
    
    /**
     * @brief Get the current configuration
     * @return Current configuration
     */
    virtual const AppConfig& getConfig() const = 0;
    
    /**
     * @brief Get mutable configuration reference
     * @return Mutable configuration reference
     */
    virtual AppConfig& getConfig() = 0;
    
    /**
     * @brief Reset to default configuration
     */
    virtual void resetToDefaults() = 0;
    
    /**
     * @brief Get the default configuration directory
     * @return Path to configuration directory
     */
    virtual std::filesystem::path getConfigDirectory() const = 0;
    
    /**
     * @brief Get the default configuration file path
     * @return Path to configuration file
     */
    virtual std::filesystem::path getDefaultConfigPath() const = 0;
    
    /**
     * @brief Get the training data file path
     * @return Path to training data file
     */
    virtual std::filesystem::path getTrainingDataPath() const = 0;
};

/**
 * @brief Create a configuration manager
 * @return Unique pointer to configuration manager
 */
std::unique_ptr<IConfigManager> createConfigManager();

} // namespace micmap::core
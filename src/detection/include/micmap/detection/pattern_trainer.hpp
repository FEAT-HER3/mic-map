#pragma once

/**
 * @file pattern_trainer.hpp
 * @brief Pattern training utilities for white noise detection
 */

#include "spectral_analyzer.hpp"

#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace micmap::detection {

/**
 * @brief Training progress callback
 */
using TrainingProgressCallback = std::function<void(float progress, const std::string& status)>;

/**
 * @brief Training configuration
 */
struct TrainingConfig {
    size_t minSamples = 10;                 ///< Minimum number of training samples
    size_t maxSamples = 100;                ///< Maximum number of training samples
    float minEnergy = 0.01f;                ///< Minimum energy threshold for valid sample
    float maxEnergy = 1.0f;                 ///< Maximum energy threshold
    std::chrono::milliseconds sampleInterval{100};  ///< Interval between samples
};

/**
 * @brief Training statistics
 */
struct TrainingStats {
    size_t samplesCollected = 0;            ///< Number of samples collected
    size_t samplesAccepted = 0;             ///< Number of valid samples
    size_t samplesRejected = 0;             ///< Number of rejected samples
    float averageEnergy = 0.0f;             ///< Average energy of accepted samples
    float averageSpectralFlatness = 0.0f;   ///< Average spectral flatness
    std::chrono::milliseconds duration{0};  ///< Total training duration
};

/**
 * @brief Pattern trainer for building noise detection profiles
 */
class PatternTrainer {
public:
    /**
     * @brief Construct a pattern trainer
     * @param analyzer Spectral analyzer to use
     * @param config Training configuration
     */
    explicit PatternTrainer(
        std::shared_ptr<ISpectralAnalyzer> analyzer,
        const TrainingConfig& config = TrainingConfig{}
    );
    
    ~PatternTrainer();
    
    /**
     * @brief Start a new training session
     */
    void startTraining();
    
    /**
     * @brief Add audio samples for training
     * @param samples Audio samples
     * @param count Number of samples
     * @return True if sample was accepted
     */
    bool addSample(const float* samples, size_t count);
    
    /**
     * @brief Finish training and compute final profile
     * @return True if training was successful
     */
    bool finishTraining();
    
    /**
     * @brief Cancel current training session
     */
    void cancelTraining();
    
    /**
     * @brief Check if training is in progress
     * @return True if training is active
     */
    bool isTraining() const;
    
    /**
     * @brief Check if training is complete
     * @return True if training finished successfully
     */
    bool isComplete() const;
    
    /**
     * @brief Get the computed spectral profile
     * @return Spectral profile vector
     */
    const std::vector<float>& getSpectralProfile() const;
    
    /**
     * @brief Get the computed energy threshold
     * @return Energy threshold
     */
    float getEnergyThreshold() const;
    
    /**
     * @brief Get training statistics
     * @return Training statistics
     */
    const TrainingStats& getStats() const;
    
    /**
     * @brief Set progress callback
     * @param callback Callback function
     */
    void setProgressCallback(TrainingProgressCallback callback);
    
    /**
     * @brief Get training configuration
     * @return Current configuration
     */
    const TrainingConfig& getConfig() const;
    
    /**
     * @brief Set training configuration
     * @param config New configuration
     */
    void setConfig(const TrainingConfig& config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace micmap::detection
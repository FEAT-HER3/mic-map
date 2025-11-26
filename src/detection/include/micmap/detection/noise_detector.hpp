#pragma once

/**
 * @file noise_detector.hpp
 * @brief White noise detection interface
 */

#include "spectral_analyzer.hpp"

#include <memory>
#include <filesystem>
#include <chrono>
#include <vector>

namespace micmap::detection {

/**
 * @brief Training data for white noise detection
 */
struct TrainingData {
    std::vector<float> spectralProfile;     ///< Average magnitude spectrum
    float energyThreshold;                   ///< Minimum energy level
    float correlationThreshold;              ///< Minimum correlation for match
    uint32_t sampleRate;                     ///< Audio sample rate
    std::chrono::system_clock::time_point trainedAt;  ///< Training timestamp
};

/**
 * @brief Result of noise detection analysis
 */
struct DetectionResult {
    float confidence;       ///< Detection confidence (0.0 to 1.0)
    float energy;           ///< Current signal energy
    float spectralFlatness; ///< Spectral flatness measure
    float correlation;      ///< Correlation with trained profile
    bool isWhiteNoise;      ///< True if above detection threshold
};

/**
 * @brief Interface for white noise detection
 */
class INoiseDetector {
public:
    virtual ~INoiseDetector() = default;
    
    // Training
    
    /**
     * @brief Start training mode
     */
    virtual void startTraining() = 0;
    
    /**
     * @brief Add training samples
     * @param samples Audio samples
     * @param count Number of samples
     */
    virtual void addTrainingSample(const float* samples, size_t count) = 0;
    
    /**
     * @brief Finish training and compute profile
     * @return True if training was successful
     */
    virtual bool finishTraining() = 0;
    
    /**
     * @brief Check if currently in training mode
     * @return True if training is in progress
     */
    virtual bool isTraining() const = 0;
    
    // Detection
    
    /**
     * @brief Analyze audio samples for white noise
     * @param samples Audio samples
     * @param count Number of samples
     * @return Detection result
     */
    virtual DetectionResult analyze(const float* samples, size_t count) = 0;
    
    // Persistence
    
    /**
     * @brief Save training data to file
     * @param path File path
     * @return True if save was successful
     */
    virtual bool saveTrainingData(const std::filesystem::path& path) = 0;
    
    /**
     * @brief Load training data from file
     * @param path File path
     * @return True if load was successful
     */
    virtual bool loadTrainingData(const std::filesystem::path& path) = 0;
    
    /**
     * @brief Check if training data is loaded
     * @return True if training data is available
     */
    virtual bool hasTrainingData() const = 0;
    
    // Configuration
    
    /**
     * @brief Set detection sensitivity
     * @param sensitivity Sensitivity value (0.0 to 1.0)
     */
    virtual void setSensitivity(float sensitivity) = 0;
    
    /**
     * @brief Get current sensitivity
     * @return Current sensitivity value
     */
    virtual float getSensitivity() const = 0;
    
    /**
     * @brief Set minimum detection duration
     * @param durationMs Minimum duration in milliseconds for confirmed detection
     *
     * White noise must be detected continuously for at least this duration
     * before isWhiteNoise returns true. This reduces false positives from
     * transient sounds. Default is 500ms as specified in config.
     */
    virtual void setMinDetectionDuration(int durationMs) = 0;
    
    /**
     * @brief Get minimum detection duration
     * @return Minimum duration in milliseconds
     */
    virtual int getMinDetectionDuration() const = 0;
    
    /**
     * @brief Get the training data
     * @return Current training data
     */
    virtual const TrainingData& getTrainingData() const = 0;
};

/**
 * @brief Create an FFT-based noise detector
 * @param sampleRate Audio sample rate in Hz
 * @param fftSize FFT window size
 * @return Unique pointer to noise detector
 */
std::unique_ptr<INoiseDetector> createFFTDetector(uint32_t sampleRate, size_t fftSize = 2048);

} // namespace micmap::detection
/**
 * @file pattern_trainer.cpp
 * @brief Pattern trainer implementation for white noise detection
 */

#include "micmap/detection/pattern_trainer.hpp"
#include "micmap/common/logger.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>

namespace micmap::detection {

namespace {
    constexpr float EPSILON = 1e-10f;
}

/**
 * @brief Internal implementation of PatternTrainer
 */
struct PatternTrainer::Impl {
    std::shared_ptr<ISpectralAnalyzer> analyzer;
    TrainingConfig config;
    TrainingStats stats;
    TrainingProgressCallback progressCallback;
    
    bool training = false;
    bool complete = false;
    
    // Collected training data
    std::vector<std::vector<float>> spectra;
    std::vector<float> energies;
    std::vector<float> spectralFlatnesses;
    
    // Computed profile
    std::vector<float> spectralProfile;
    float energyThreshold = 0.0f;
    float spectralFlatnessThreshold = 0.0f;
    
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastSampleTime;
    
    Impl(std::shared_ptr<ISpectralAnalyzer> analyzer_, const TrainingConfig& config_)
        : analyzer(std::move(analyzer_))
        , config(config_) {
    }
    
    void reportProgress(const std::string& status) {
        if (progressCallback) {
            float progress = 0.0f;
            if (config.minSamples > 0) {
                progress = static_cast<float>(stats.samplesAccepted) / 
                           static_cast<float>(config.minSamples);
                progress = std::min(progress, 1.0f);
            }
            progressCallback(progress, status);
        }
    }
    
    /**
     * @brief Compute standard deviation of a vector
     */
    float computeStdDev(const std::vector<float>& values, float mean) {
        if (values.empty()) return 0.0f;
        
        float sumSq = 0.0f;
        for (float v : values) {
            float diff = v - mean;
            sumSq += diff * diff;
        }
        return std::sqrt(sumSq / static_cast<float>(values.size()));
    }
    
    /**
     * @brief Normalize a spectral profile
     */
    void normalizeProfile(std::vector<float>& profile) {
        if (profile.empty()) return;
        
        // L2 normalization
        float sumSq = 0.0f;
        for (float v : profile) {
            sumSq += v * v;
        }
        
        float norm = std::sqrt(sumSq);
        if (norm > EPSILON) {
            for (float& v : profile) {
                v /= norm;
            }
        }
    }
};

PatternTrainer::PatternTrainer(
    std::shared_ptr<ISpectralAnalyzer> analyzer,
    const TrainingConfig& config
) : impl_(std::make_unique<Impl>(std::move(analyzer), config)) {
}

PatternTrainer::~PatternTrainer() = default;

void PatternTrainer::startTraining() {
    // Reset all state
    impl_->training = true;
    impl_->complete = false;
    impl_->spectra.clear();
    impl_->energies.clear();
    impl_->spectralFlatnesses.clear();
    impl_->spectralProfile.clear();
    impl_->energyThreshold = 0.0f;
    impl_->spectralFlatnessThreshold = 0.0f;
    impl_->stats = TrainingStats{};
    impl_->startTime = std::chrono::steady_clock::now();
    impl_->lastSampleTime = impl_->startTime;
    
    impl_->reportProgress("Training started - cover the microphone with your finger");
    MICMAP_LOG_INFO("Pattern training started");
}

bool PatternTrainer::addSample(const float* samples, size_t count) {
    if (!impl_->training || !samples || count == 0) {
        return false;
    }
    
    // Check if we've reached max samples
    if (impl_->stats.samplesAccepted >= impl_->config.maxSamples) {
        impl_->reportProgress("Maximum samples reached");
        return false;
    }
    
    // Check sample interval (rate limiting)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->lastSampleTime
    );
    if (elapsed < impl_->config.sampleInterval) {
        // Too soon since last sample, skip
        return false;
    }
    
    ++impl_->stats.samplesCollected;
    
    // Analyze the audio
    auto result = impl_->analyzer->analyze(samples, count);
    
    // Validate energy bounds
    if (result.energy < impl_->config.minEnergy) {
        ++impl_->stats.samplesRejected;
        impl_->reportProgress("Sample rejected: energy too low (not covering mic?)");
        MICMAP_LOG_DEBUG("Training sample rejected: energy ", result.energy, 
                        " < ", impl_->config.minEnergy);
        return false;
    }
    
    if (result.energy > impl_->config.maxEnergy) {
        ++impl_->stats.samplesRejected;
        impl_->reportProgress("Sample rejected: energy too high (clipping?)");
        MICMAP_LOG_DEBUG("Training sample rejected: energy ", result.energy, 
                        " > ", impl_->config.maxEnergy);
        return false;
    }
    
    // Check spectral flatness - white noise should have high flatness
    if (result.spectralFlatness < 0.1f) {
        ++impl_->stats.samplesRejected;
        impl_->reportProgress("Sample rejected: not white noise (too tonal)");
        MICMAP_LOG_DEBUG("Training sample rejected: flatness ", result.spectralFlatness, 
                        " too low");
        return false;
    }
    
    // Accept sample
    impl_->spectra.push_back(result.magnitudes);
    impl_->energies.push_back(result.energy);
    impl_->spectralFlatnesses.push_back(result.spectralFlatness);
    ++impl_->stats.samplesAccepted;
    impl_->lastSampleTime = now;
    
    // Update running statistics
    impl_->stats.averageEnergy = std::accumulate(
        impl_->energies.begin(), impl_->energies.end(), 0.0f
    ) / static_cast<float>(impl_->energies.size());
    
    impl_->stats.averageSpectralFlatness = std::accumulate(
        impl_->spectralFlatnesses.begin(), impl_->spectralFlatnesses.end(), 0.0f
    ) / static_cast<float>(impl_->spectralFlatnesses.size());
    
    impl_->stats.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->startTime
    );
    
    // Report progress
    std::string status = "Sample " + std::to_string(impl_->stats.samplesAccepted) + 
                        "/" + std::to_string(impl_->config.minSamples) + " accepted";
    impl_->reportProgress(status);
    
    MICMAP_LOG_DEBUG("Training sample accepted: ", impl_->stats.samplesAccepted, 
                   "/", impl_->config.minSamples,
                   " energy=", result.energy,
                   " flatness=", result.spectralFlatness);
    
    return true;
}

bool PatternTrainer::finishTraining() {
    if (!impl_->training) {
        MICMAP_LOG_ERROR("Cannot finish training: not in training mode");
        return false;
    }
    
    impl_->training = false;
    
    // Check minimum samples
    if (impl_->stats.samplesAccepted < impl_->config.minSamples) {
        MICMAP_LOG_ERROR("Not enough training samples: ", impl_->stats.samplesAccepted,
                        " < ", impl_->config.minSamples);
        impl_->reportProgress("Training failed: not enough valid samples");
        return false;
    }
    
    // Validate collected data
    if (impl_->spectra.empty() || impl_->spectra[0].empty()) {
        MICMAP_LOG_ERROR("No valid spectra collected");
        impl_->reportProgress("Training failed: no valid spectra");
        return false;
    }
    
    // Compute average spectral profile
    size_t profileSize = impl_->spectra[0].size();
    impl_->spectralProfile.resize(profileSize, 0.0f);
    
    for (const auto& spectrum : impl_->spectra) {
        for (size_t i = 0; i < profileSize && i < spectrum.size(); ++i) {
            impl_->spectralProfile[i] += spectrum[i];
        }
    }
    
    float numSamples = static_cast<float>(impl_->spectra.size());
    for (float& val : impl_->spectralProfile) {
        val /= numSamples;
    }
    
    // Normalize the profile for correlation comparison
    impl_->normalizeProfile(impl_->spectralProfile);
    
    // Compute energy threshold
    // Use mean - 2*stddev as lower bound (captures ~95% of training data)
    float energyMean = impl_->stats.averageEnergy;
    float energyStdDev = impl_->computeStdDev(impl_->energies, energyMean);
    impl_->energyThreshold = std::max(
        impl_->config.minEnergy,
        energyMean - 2.0f * energyStdDev
    );
    
    // Compute spectral flatness threshold
    float flatnessMean = impl_->stats.averageSpectralFlatness;
    float flatnessStdDev = impl_->computeStdDev(impl_->spectralFlatnesses, flatnessMean);
    impl_->spectralFlatnessThreshold = std::max(
        0.1f,
        flatnessMean - 2.0f * flatnessStdDev
    );
    
    impl_->complete = true;
    impl_->reportProgress("Training complete!");
    
    MICMAP_LOG_INFO("Pattern training complete:");
    MICMAP_LOG_INFO("  Samples: ", impl_->stats.samplesAccepted);
    MICMAP_LOG_INFO("  Average energy: ", impl_->stats.averageEnergy);
    MICMAP_LOG_INFO("  Energy threshold: ", impl_->energyThreshold);
    MICMAP_LOG_INFO("  Average flatness: ", impl_->stats.averageSpectralFlatness);
    MICMAP_LOG_INFO("  Flatness threshold: ", impl_->spectralFlatnessThreshold);
    
    return true;
}

void PatternTrainer::cancelTraining() {
    impl_->training = false;
    impl_->complete = false;
    impl_->spectra.clear();
    impl_->energies.clear();
    impl_->spectralFlatnesses.clear();
    impl_->spectralProfile.clear();
    impl_->energyThreshold = 0.0f;
    impl_->spectralFlatnessThreshold = 0.0f;
    
    impl_->reportProgress("Training cancelled");
    MICMAP_LOG_INFO("Pattern training cancelled");
}

bool PatternTrainer::isTraining() const {
    return impl_->training;
}

bool PatternTrainer::isComplete() const {
    return impl_->complete;
}

const std::vector<float>& PatternTrainer::getSpectralProfile() const {
    return impl_->spectralProfile;
}

float PatternTrainer::getEnergyThreshold() const {
    return impl_->energyThreshold;
}

const TrainingStats& PatternTrainer::getStats() const {
    return impl_->stats;
}

void PatternTrainer::setProgressCallback(TrainingProgressCallback callback) {
    impl_->progressCallback = std::move(callback);
}

const TrainingConfig& PatternTrainer::getConfig() const {
    return impl_->config;
}

void PatternTrainer::setConfig(const TrainingConfig& config) {
    if (!impl_->training) {
        impl_->config = config;
    } else {
        MICMAP_LOG_WARNING("Cannot change config while training is in progress");
    }
}

} // namespace micmap::detection
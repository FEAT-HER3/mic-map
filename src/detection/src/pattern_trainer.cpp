/**
 * @file pattern_trainer.cpp
 * @brief Pattern trainer implementation
 */

#include "micmap/detection/pattern_trainer.hpp"
#include "micmap/common/logger.hpp"

#include <algorithm>
#include <numeric>

namespace micmap::detection {

struct PatternTrainer::Impl {
    std::shared_ptr<ISpectralAnalyzer> analyzer;
    TrainingConfig config;
    TrainingStats stats;
    TrainingProgressCallback progressCallback;
    
    bool training = false;
    bool complete = false;
    
    std::vector<std::vector<float>> spectra;
    std::vector<float> energies;
    std::vector<float> spectralProfile;
    float energyThreshold = 0.0f;
    
    std::chrono::steady_clock::time_point startTime;
    
    Impl(std::shared_ptr<ISpectralAnalyzer> analyzer_, const TrainingConfig& config_)
        : analyzer(std::move(analyzer_))
        , config(config_) {
    }
    
    void reportProgress(const std::string& status) {
        if (progressCallback) {
            float progress = static_cast<float>(stats.samplesAccepted) / 
                           static_cast<float>(config.minSamples);
            progress = std::min(progress, 1.0f);
            progressCallback(progress, status);
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
    impl_->training = true;
    impl_->complete = false;
    impl_->spectra.clear();
    impl_->energies.clear();
    impl_->spectralProfile.clear();
    impl_->energyThreshold = 0.0f;
    impl_->stats = TrainingStats{};
    impl_->startTime = std::chrono::steady_clock::now();
    
    impl_->reportProgress("Training started");
    MICMAP_LOG_INFO("Pattern training started");
}

bool PatternTrainer::addSample(const float* samples, size_t count) {
    if (!impl_->training || !samples || count == 0) {
        return false;
    }
    
    if (impl_->stats.samplesAccepted >= impl_->config.maxSamples) {
        return false;
    }
    
    ++impl_->stats.samplesCollected;
    
    auto result = impl_->analyzer->analyze(samples, count);
    
    // Check energy bounds
    if (result.energy < impl_->config.minEnergy) {
        ++impl_->stats.samplesRejected;
        impl_->reportProgress("Sample rejected: energy too low");
        return false;
    }
    
    if (result.energy > impl_->config.maxEnergy) {
        ++impl_->stats.samplesRejected;
        impl_->reportProgress("Sample rejected: energy too high");
        return false;
    }
    
    // Accept sample
    impl_->spectra.push_back(result.magnitudes);
    impl_->energies.push_back(result.energy);
    ++impl_->stats.samplesAccepted;
    
    // Update running averages
    impl_->stats.averageEnergy = std::accumulate(
        impl_->energies.begin(), impl_->energies.end(), 0.0f
    ) / impl_->energies.size();
    
    // Compute average spectral flatness
    float totalFlatness = 0.0f;
    for (const auto& spectrum : impl_->spectra) {
        // Simple flatness approximation
        if (!spectrum.empty()) {
            float sum = std::accumulate(spectrum.begin(), spectrum.end(), 0.0f);
            float mean = sum / spectrum.size();
            float variance = 0.0f;
            for (float v : spectrum) {
                variance += (v - mean) * (v - mean);
            }
            variance /= spectrum.size();
            // Lower variance = flatter spectrum
            totalFlatness += 1.0f / (1.0f + variance);
        }
    }
    impl_->stats.averageSpectralFlatness = totalFlatness / impl_->spectra.size();
    
    impl_->stats.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - impl_->startTime
    );
    
    impl_->reportProgress("Sample accepted");
    
    MICMAP_LOG_DEBUG("Training sample accepted: ", impl_->stats.samplesAccepted, 
                   "/", impl_->config.minSamples);
    
    return true;
}

bool PatternTrainer::finishTraining() {
    if (!impl_->training) {
        return false;
    }
    
    impl_->training = false;
    
    if (impl_->stats.samplesAccepted < impl_->config.minSamples) {
        MICMAP_LOG_ERROR("Not enough training samples: ", impl_->stats.samplesAccepted,
                        " < ", impl_->config.minSamples);
        impl_->reportProgress("Training failed: not enough samples");
        return false;
    }
    
    // Compute average spectral profile
    if (impl_->spectra.empty() || impl_->spectra[0].empty()) {
        MICMAP_LOG_ERROR("No valid spectra collected");
        return false;
    }
    
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
    
    // Normalize profile
    float maxVal = *std::max_element(
        impl_->spectralProfile.begin(),
        impl_->spectralProfile.end()
    );
    if (maxVal > 0.0f) {
        for (float& val : impl_->spectralProfile) {
            val /= maxVal;
        }
    }
    
    // Compute energy threshold
    float minEnergy = *std::min_element(impl_->energies.begin(), impl_->energies.end());
    impl_->energyThreshold = minEnergy * 0.5f;
    
    impl_->complete = true;
    impl_->reportProgress("Training complete");
    
    MICMAP_LOG_INFO("Pattern training complete: ", impl_->stats.samplesAccepted, " samples");
    
    return true;
}

void PatternTrainer::cancelTraining() {
    impl_->training = false;
    impl_->complete = false;
    impl_->spectra.clear();
    impl_->energies.clear();
    
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
    impl_->config = config;
}

} // namespace micmap::detection
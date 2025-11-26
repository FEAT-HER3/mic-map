/**
 * @file noise_detector.cpp
 * @brief FFT-based white noise detector implementation
 */

#include "micmap/detection/noise_detector.hpp"
#include "micmap/common/logger.hpp"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <numeric>

namespace micmap::detection {

// Training data file format
struct TrainingDataHeader {
    char magic[4];              // "MMAP"
    uint32_t version;           // Format version
    uint32_t sampleRate;        // Audio sample rate
    uint32_t fftSize;           // FFT window size
    uint32_t profileSize;       // Number of frequency bins
    float energyThreshold;      // Minimum energy
    float correlationThreshold; // Minimum correlation
    int64_t timestamp;          // Training timestamp (Unix time)
};

/**
 * @brief FFT-based noise detector implementation
 */
class FFTNoiseDetector : public INoiseDetector {
public:
    FFTNoiseDetector(uint32_t sampleRate, size_t fftSize)
        : sampleRate_(sampleRate)
        , fftSize_(fftSize)
        , sensitivity_(0.7f)
        , training_(false)
        , hasTrainingData_(false) {
        
        analyzer_ = createKissFFTAnalyzer(sampleRate, fftSize);
        
        MICMAP_LOG_DEBUG("Created noise detector: ", fftSize_, " point FFT at ", sampleRate_, " Hz");
    }
    
    ~FFTNoiseDetector() override = default;
    
    // Training
    
    void startTraining() override {
        training_ = true;
        trainingSpectra_.clear();
        trainingEnergies_.clear();
        
        MICMAP_LOG_INFO("Started noise detection training");
    }
    
    void addTrainingSample(const float* samples, size_t count) override {
        if (!training_ || !samples || count == 0) {
            return;
        }
        
        auto result = analyzer_->analyze(samples, count);
        
        // Only accept samples with sufficient energy
        if (result.energy > 0.001f) {
            trainingSpectra_.push_back(result.magnitudes);
            trainingEnergies_.push_back(result.energy);
            
            MICMAP_LOG_DEBUG("Added training sample: energy=", result.energy, 
                           ", flatness=", result.spectralFlatness);
        }
    }
    
    bool finishTraining() override {
        if (!training_) {
            return false;
        }
        
        training_ = false;
        
        if (trainingSpectra_.empty()) {
            MICMAP_LOG_ERROR("No valid training samples collected");
            return false;
        }
        
        // Compute average spectral profile
        size_t profileSize = trainingSpectra_[0].size();
        trainingData_.spectralProfile.resize(profileSize, 0.0f);
        
        for (const auto& spectrum : trainingSpectra_) {
            for (size_t i = 0; i < profileSize && i < spectrum.size(); ++i) {
                trainingData_.spectralProfile[i] += spectrum[i];
            }
        }
        
        float numSamples = static_cast<float>(trainingSpectra_.size());
        for (float& val : trainingData_.spectralProfile) {
            val /= numSamples;
        }
        
        // Normalize profile
        float maxVal = *std::max_element(
            trainingData_.spectralProfile.begin(),
            trainingData_.spectralProfile.end()
        );
        if (maxVal > 0.0f) {
            for (float& val : trainingData_.spectralProfile) {
                val /= maxVal;
            }
        }
        
        // Compute energy threshold (use minimum observed energy * 0.5)
        float minEnergy = *std::min_element(trainingEnergies_.begin(), trainingEnergies_.end());
        trainingData_.energyThreshold = minEnergy * 0.5f;
        
        // Set correlation threshold based on sensitivity
        trainingData_.correlationThreshold = 0.5f + (1.0f - sensitivity_) * 0.4f;
        
        trainingData_.sampleRate = sampleRate_;
        trainingData_.trainedAt = std::chrono::system_clock::now();
        
        hasTrainingData_ = true;
        
        MICMAP_LOG_INFO("Training complete: ", trainingSpectra_.size(), " samples, ",
                       "energy threshold=", trainingData_.energyThreshold);
        
        // Clear training buffers
        trainingSpectra_.clear();
        trainingEnergies_.clear();
        
        return true;
    }
    
    bool isTraining() const override {
        return training_;
    }
    
    // Detection
    
    DetectionResult analyze(const float* samples, size_t count) override {
        DetectionResult result{};
        
        if (!samples || count == 0) {
            return result;
        }
        
        auto spectral = analyzer_->analyze(samples, count);
        
        result.energy = spectral.energy;
        result.spectralFlatness = spectral.spectralFlatness;
        
        if (!hasTrainingData_) {
            // Without training data, use spectral flatness as a rough indicator
            result.confidence = spectral.spectralFlatness;
            result.correlation = 0.0f;
            result.isWhiteNoise = spectral.spectralFlatness > 0.5f && spectral.energy > 0.01f;
            return result;
        }
        
        // Check energy threshold
        if (spectral.energy < trainingData_.energyThreshold) {
            result.confidence = 0.0f;
            result.correlation = 0.0f;
            result.isWhiteNoise = false;
            return result;
        }
        
        // Compute correlation with trained profile
        result.correlation = computeCorrelation(spectral.magnitudes, trainingData_.spectralProfile);
        
        // Combine factors for confidence
        float energyFactor = std::min(1.0f, spectral.energy / (trainingData_.energyThreshold * 2.0f));
        float flatnessFactor = spectral.spectralFlatness;
        float correlationFactor = result.correlation;
        
        // Weighted combination
        result.confidence = 0.4f * correlationFactor + 0.3f * flatnessFactor + 0.3f * energyFactor;
        
        // Apply sensitivity
        float threshold = 1.0f - sensitivity_;
        result.isWhiteNoise = result.confidence > threshold && 
                              result.correlation > trainingData_.correlationThreshold;
        
        return result;
    }
    
    // Persistence
    
    bool saveTrainingData(const std::filesystem::path& path) override {
        if (!hasTrainingData_) {
            MICMAP_LOG_ERROR("No training data to save");
            return false;
        }
        
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            MICMAP_LOG_ERROR("Failed to open file for writing: ", path.string());
            return false;
        }
        
        TrainingDataHeader header{};
        std::memcpy(header.magic, "MMAP", 4);
        header.version = 1;
        header.sampleRate = trainingData_.sampleRate;
        header.fftSize = static_cast<uint32_t>(fftSize_);
        header.profileSize = static_cast<uint32_t>(trainingData_.spectralProfile.size());
        header.energyThreshold = trainingData_.energyThreshold;
        header.correlationThreshold = trainingData_.correlationThreshold;
        header.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            trainingData_.trainedAt.time_since_epoch()
        ).count();
        
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(
            reinterpret_cast<const char*>(trainingData_.spectralProfile.data()),
            trainingData_.spectralProfile.size() * sizeof(float)
        );
        
        MICMAP_LOG_INFO("Saved training data to: ", path.string());
        return true;
    }
    
    bool loadTrainingData(const std::filesystem::path& path) override {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            MICMAP_LOG_ERROR("Failed to open file for reading: ", path.string());
            return false;
        }
        
        TrainingDataHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (std::memcmp(header.magic, "MMAP", 4) != 0) {
            MICMAP_LOG_ERROR("Invalid training data file format");
            return false;
        }
        
        if (header.version != 1) {
            MICMAP_LOG_ERROR("Unsupported training data version: ", header.version);
            return false;
        }
        
        trainingData_.spectralProfile.resize(header.profileSize);
        file.read(
            reinterpret_cast<char*>(trainingData_.spectralProfile.data()),
            header.profileSize * sizeof(float)
        );
        
        trainingData_.sampleRate = header.sampleRate;
        trainingData_.energyThreshold = header.energyThreshold;
        trainingData_.correlationThreshold = header.correlationThreshold;
        trainingData_.trainedAt = std::chrono::system_clock::time_point(
            std::chrono::seconds(header.timestamp)
        );
        
        hasTrainingData_ = true;
        
        MICMAP_LOG_INFO("Loaded training data from: ", path.string());
        return true;
    }
    
    bool hasTrainingData() const override {
        return hasTrainingData_;
    }
    
    // Configuration
    
    void setSensitivity(float sensitivity) override {
        sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
    }
    
    float getSensitivity() const override {
        return sensitivity_;
    }
    
    const TrainingData& getTrainingData() const override {
        return trainingData_;
    }
    
private:
    float computeCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.empty() || b.empty() || a.size() != b.size()) {
            return 0.0f;
        }
        
        // Normalize vectors
        auto normalize = [](const std::vector<float>& v) -> std::vector<float> {
            std::vector<float> result = v;
            float sumSq = 0.0f;
            for (float x : result) {
                sumSq += x * x;
            }
            float norm = std::sqrt(sumSq);
            if (norm > 0.0f) {
                for (float& x : result) {
                    x /= norm;
                }
            }
            return result;
        };
        
        auto normA = normalize(a);
        auto normB = normalize(b);
        
        // Dot product
        float correlation = 0.0f;
        for (size_t i = 0; i < normA.size(); ++i) {
            correlation += normA[i] * normB[i];
        }
        
        return std::clamp(correlation, 0.0f, 1.0f);
    }
    
    uint32_t sampleRate_;
    size_t fftSize_;
    float sensitivity_;
    
    std::unique_ptr<ISpectralAnalyzer> analyzer_;
    
    bool training_;
    std::vector<std::vector<float>> trainingSpectra_;
    std::vector<float> trainingEnergies_;
    
    TrainingData trainingData_;
    bool hasTrainingData_;
};

std::unique_ptr<INoiseDetector> createFFTDetector(uint32_t sampleRate, size_t fftSize) {
    return std::make_unique<FFTNoiseDetector>(sampleRate, fftSize);
}

} // namespace micmap::detection
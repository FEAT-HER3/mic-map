/**
 * @file noise_detector.cpp
 * @brief FFT-based white noise detector implementation with temporal consistency
 */

#include "micmap/detection/noise_detector.hpp"
#include "micmap/common/logger.hpp"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <mutex>

namespace micmap::detection {

namespace {
    constexpr float EPSILON = 1e-10f;
    constexpr char MAGIC[4] = {'M', 'M', 'A', 'P'};
    constexpr uint32_t FORMAT_VERSION = 1;
    constexpr int DEFAULT_MIN_DETECTION_DURATION_MS = 300;
}

/**
 * @brief Training data file header format
 */
#pragma pack(push, 1)
struct TrainingDataHeader {
    char magic[4];              // "MMAP"
    uint32_t version;           // Format version
    uint32_t sampleRate;        // Audio sample rate
    uint32_t fftSize;           // FFT window size
    uint32_t profileSize;       // Number of frequency bins
    float energyThreshold;      // Minimum energy
    float correlationThreshold; // Minimum correlation
    float spectralFlatnessThreshold; // Minimum spectral flatness
    int64_t timestamp;          // Training timestamp (Unix time)
    uint32_t reserved[4];       // Reserved for future use
};
#pragma pack(pop)

/**
 * @brief FFT-based noise detector implementation
 * 
 * Implements multi-factor detection with temporal consistency:
 * - Spectral flatness (white noise has flat spectrum)
 * - Energy level (must be within expected range)
 * - Cross-correlation with trained profile
 * - Temporal consistency (configurable duration, default 500ms from config)
 */
class FFTNoiseDetector : public INoiseDetector {
public:
    FFTNoiseDetector(uint32_t sampleRate, size_t fftSize)
        : sampleRate_(sampleRate)
        , fftSize_(fftSize)
        , sensitivity_(0.7f)
        , training_(false)
        , hasTrainingData_(false)
        , minDetectionDurationMs_(DEFAULT_MIN_DETECTION_DURATION_MS)
        , detectionStartTime_()
        , isCurrentlyDetecting_(false) {
        
        analyzer_ = createKissFFTAnalyzer(sampleRate, fftSize);
        
        MICMAP_LOG_DEBUG("Created FFT noise detector: ", fftSize_, " point FFT at ", sampleRate_, " Hz");
    }
    
    ~FFTNoiseDetector() override = default;
    
    // ========== Training ==========
    
    void startTraining() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        training_ = true;
        trainingSpectra_.clear();
        trainingEnergies_.clear();
        trainingFlatnesses_.clear();
        
        MICMAP_LOG_INFO("Started noise detection training");
    }
    
    void addTrainingSample(const float* samples, size_t count) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!training_ || !samples || count == 0) {
            return;
        }
        
        auto result = analyzer_->analyze(samples, count);
        
        // Accept samples with any detectable energy (very low threshold)
        // The "microphone covered" sound may be quiet and not spectrally flat
        // We'll learn whatever pattern the user provides during training
        if (result.energy > 0.00001f) {  // Very low threshold - just needs some signal
            trainingSpectra_.push_back(result.magnitudes);
            trainingEnergies_.push_back(result.energy);
            trainingFlatnesses_.push_back(result.spectralFlatness);
            
            MICMAP_LOG_DEBUG("Added training sample: energy=", result.energy,
                           ", flatness=", result.spectralFlatness);
        } else {
            MICMAP_LOG_DEBUG("Rejected training sample (no signal): energy=", result.energy);
        }
    }
    
    bool finishTraining() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!training_) {
            return false;
        }
        
        training_ = false;
        
        if (trainingSpectra_.empty()) {
            MICMAP_LOG_ERROR("No valid training samples collected");
            return false;
        }
        
        if (trainingSpectra_.size() < 5) {
            MICMAP_LOG_ERROR("Not enough training samples: ", trainingSpectra_.size(), " < 5");
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
        
        // Normalize profile (L2 normalization)
        normalizeVector(trainingData_.spectralProfile);
        
        // Compute energy statistics
        float energyMean = std::accumulate(trainingEnergies_.begin(), trainingEnergies_.end(), 0.0f)
                          / static_cast<float>(trainingEnergies_.size());
        float energyStdDev = computeStdDev(trainingEnergies_, energyMean);
        
        // Store energy statistics for detection
        trainingData_.energyThreshold = std::max(0.000001f, energyMean);
        
        // Store minimum energy (with margin) - energy must be at least this high
        float minEnergy = *std::min_element(trainingEnergies_.begin(), trainingEnergies_.end());
        energyMinThreshold_ = minEnergy * 0.3f;  // 30% of minimum training energy
        
        // Store energy variance - covered mic has CONSISTENT energy (low variance)
        // This helps distinguish from speech which has high variance
        energyVarianceThreshold_ = energyStdDev / energyMean;  // Coefficient of variation
        
        // Compute flatness statistics
        float flatnessMean = std::accumulate(trainingFlatnesses_.begin(), trainingFlatnesses_.end(), 0.0f)
                            / static_cast<float>(trainingFlatnesses_.size());
        spectralFlatnessThreshold_ = flatnessMean;
        
        // Correlation threshold
        trainingData_.correlationThreshold = 0.4f + (1.0f - sensitivity_) * 0.3f;
        
        MICMAP_LOG_INFO("  Energy min threshold: ", energyMinThreshold_);
        MICMAP_LOG_INFO("  Energy CV threshold: ", energyVarianceThreshold_);
        
        trainingData_.sampleRate = sampleRate_;
        trainingData_.trainedAt = std::chrono::system_clock::now();
        
        hasTrainingData_ = true;
        
        MICMAP_LOG_INFO("Training complete: ", trainingSpectra_.size(), " samples");
        MICMAP_LOG_INFO("  Energy threshold: ", trainingData_.energyThreshold);
        MICMAP_LOG_INFO("  Correlation threshold: ", trainingData_.correlationThreshold);
        MICMAP_LOG_INFO("  Flatness threshold: ", spectralFlatnessThreshold_);
        
        // Clear training buffers
        trainingSpectra_.clear();
        trainingEnergies_.clear();
        trainingFlatnesses_.clear();
        
        return true;
    }
    
    bool isTraining() const override {
        return training_;
    }
    
    // ========== Detection ==========
    
    DetectionResult analyze(const float* samples, size_t count) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        DetectionResult result{};
        result.confidence = 0.0f;
        result.energy = 0.0f;
        result.spectralFlatness = 0.0f;
        result.correlation = 0.0f;
        result.isWhiteNoise = false;
        
        if (!samples || count == 0) {
            updateTemporalState(false);
            return result;
        }
        
        // Perform spectral analysis
        auto spectral = analyzer_->analyze(samples, count);
        
        result.energy = spectral.energy;
        result.spectralFlatness = spectral.spectralFlatness;
        
        // Without training data, cannot detect
        if (!hasTrainingData_) {
            result.confidence = 0.0f;
            result.correlation = 0.0f;
            result.isWhiteNoise = false;
            return result;
        }
        
        // SPIKE-GATED FREQUENCY-BASED DETECTION
        //
        // Key insights:
        // 1. Touching mic creates a SPIKE to 0dB (normal audio is -60 to -25dB)
        // 2. Covered mic has frequent high confidence readings
        // 3. Music can also have high confidence but NO initial spike
        //
        // Solution: Require spike to arm detection, then use frequency-based detection
        
        // Convert energy to dB for spike detection
        float energyDb = (spectral.energy > EPSILON) ?
            10.0f * std::log10(spectral.energy) : -60.0f;
        
        // Track energy history for consistency
        updateEnergyHistory(spectral.energy);
        
        // SPIKE DETECTION: Look for energy near 0dB (very loud)
        // Normal audio: -60 to -25 dB
        // Spike when touching: > -10 dB (approaching 0dB)
        bool spikeDetected = energyDb > -10.0f;
        
        if (spikeDetected && !spikeTriggered_) {
            spikeTriggered_ = true;
            spikeTime_ = std::chrono::steady_clock::now();
            MICMAP_LOG_DEBUG("SPIKE detected! Energy: ", energyDb, " dB");
        }
        
        // Check if spike is still valid (within time window)
        bool spikeValid = false;
        if (spikeTriggered_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - spikeTime_).count();
            // Spike arms detection for 3 seconds
            spikeValid = elapsed < 500;
            if (!spikeValid && !isCurrentlyDetecting_) {
                spikeTriggered_ = false;  // Spike expired and not detecting
                MICMAP_LOG_DEBUG("Spike expired");
            }
        }
        
        // Compute confidence factors
        float energyConsistency = computeEnergyConsistency();
        
        float energyRatio = 0.0f;
        if (trainingData_.energyThreshold > EPSILON) {
            float ratio = spectral.energy / trainingData_.energyThreshold;
            if (ratio >= 0.3f && ratio <= 5.0f) {
                if (ratio < 1.0f) {
                    energyRatio = (ratio - 0.3f) / 0.7f;
                } else {
                    energyRatio = std::min(1.0f, 0.8f + ratio * 0.04f);
                }
            }
        }
        
        float pearsonCorr = computeCorrelation(spectral.magnitudes, trainingData_.spectralProfile);
        float shapeSimilarity = computeSpectralShapeDistance(spectral.magnitudes, trainingData_.spectralProfile);
        result.correlation = std::sqrt(pearsonCorr * shapeSimilarity);
        
        // Confidence combines all factors
        result.confidence = 0.35f * energyRatio +
                           0.35f * energyConsistency +
                           0.30f * result.correlation;
        
        // Track high-confidence hits in sliding window
        bool isHighConfidence = result.confidence >= 0.60f;  // 60% threshold for "high"
        updateConfidenceHistory(isHighConfidence);
        
        // Count high-confidence hits in recent history
        int highHits = countHighConfidenceHits();
        
        // FREQUENCY-BASED DETECTION with SPIKE GATE:
        // - Must have spike to start (or already detecting)
        // - Start: Need 4+ high hits out of last 12 samples
        // - Continue: Need 2+ high hits out of last 12
        // - Stop: Less than 2 high hits
        
        int startThreshold = 4;
        int stopThreshold = 2;
        
        bool instantDetection;
        if (isCurrentlyDetecting_) {
            // Already detecting - use lower threshold to maintain
            // Don't need spike anymore once detecting
            instantDetection = highHits >= stopThreshold;
        } else {
            // Not detecting - need spike AND high frequency to start
            bool canStart = spikeValid;  // Must have recent spike
            instantDetection = canStart && highHits >= startThreshold;
        }
        
        // Reset spike if detection is lost
        if (!instantDetection && !isCurrentlyDetecting_ && !spikeValid) {
            spikeTriggered_ = false;
        }
        
        // Apply temporal consistency (configurable duration from config)
        result.isWhiteNoise = updateTemporalState(instantDetection);
        
        return result;
    }
    
    // ========== Persistence ==========
    
    bool saveTrainingData(const std::filesystem::path& path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!hasTrainingData_) {
            MICMAP_LOG_ERROR("No training data to save");
            return false;
        }
        
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            MICMAP_LOG_ERROR("Failed to open file for writing: ", path.string());
            return false;
        }
        
        // Prepare header
        TrainingDataHeader header{};
        std::memcpy(header.magic, MAGIC, 4);
        header.version = FORMAT_VERSION;
        header.sampleRate = trainingData_.sampleRate;
        header.fftSize = static_cast<uint32_t>(fftSize_);
        header.profileSize = static_cast<uint32_t>(trainingData_.spectralProfile.size());
        header.energyThreshold = trainingData_.energyThreshold;
        header.correlationThreshold = trainingData_.correlationThreshold;
        header.spectralFlatnessThreshold = spectralFlatnessThreshold_;
        header.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            trainingData_.trainedAt.time_since_epoch()
        ).count();
        std::memset(header.reserved, 0, sizeof(header.reserved));
        
        // Write header
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        
        // Write spectral profile
        file.write(
            reinterpret_cast<const char*>(trainingData_.spectralProfile.data()),
            trainingData_.spectralProfile.size() * sizeof(float)
        );
        
        if (!file) {
            MICMAP_LOG_ERROR("Failed to write training data to: ", path.string());
            return false;
        }
        
        MICMAP_LOG_INFO("Saved training data to: ", path.string());
        return true;
    }
    
    bool loadTrainingData(const std::filesystem::path& path) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            MICMAP_LOG_ERROR("Failed to open file for reading: ", path.string());
            return false;
        }
        
        // Read header
        TrainingDataHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        // Validate magic
        if (std::memcmp(header.magic, MAGIC, 4) != 0) {
            MICMAP_LOG_ERROR("Invalid training data file format (bad magic)");
            return false;
        }
        
        // Validate version
        if (header.version != FORMAT_VERSION) {
            MICMAP_LOG_ERROR("Unsupported training data version: ", header.version);
            return false;
        }
        
        // Validate profile size
        if (header.profileSize == 0 || header.profileSize > 100000) {
            MICMAP_LOG_ERROR("Invalid profile size: ", header.profileSize);
            return false;
        }
        
        // Read spectral profile
        trainingData_.spectralProfile.resize(header.profileSize);
        file.read(
            reinterpret_cast<char*>(trainingData_.spectralProfile.data()),
            header.profileSize * sizeof(float)
        );
        
        if (!file) {
            MICMAP_LOG_ERROR("Failed to read training data from: ", path.string());
            return false;
        }
        
        // Populate training data
        trainingData_.sampleRate = header.sampleRate;
        trainingData_.energyThreshold = header.energyThreshold;
        trainingData_.correlationThreshold = header.correlationThreshold;
        spectralFlatnessThreshold_ = header.spectralFlatnessThreshold;
        trainingData_.trainedAt = std::chrono::system_clock::time_point(
            std::chrono::seconds(header.timestamp)
        );
        
        hasTrainingData_ = true;
        
        MICMAP_LOG_INFO("Loaded training data from: ", path.string());
        MICMAP_LOG_INFO("  Sample rate: ", trainingData_.sampleRate, " Hz");
        MICMAP_LOG_INFO("  Profile size: ", trainingData_.spectralProfile.size(), " bins");
        MICMAP_LOG_INFO("  Energy threshold: ", trainingData_.energyThreshold);
        MICMAP_LOG_INFO("  Correlation threshold: ", trainingData_.correlationThreshold);
        
        return true;
    }
    
    bool hasTrainingData() const override {
        return hasTrainingData_;
    }
    
    // ========== Configuration ==========
    
    void setSensitivity(float sensitivity) override {
        sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
        
        // Update correlation threshold if we have training data
        if (hasTrainingData_) {
            trainingData_.correlationThreshold = 0.3f + (1.0f - sensitivity_) * 0.5f;
        }
    }
    
    float getSensitivity() const override {
        return sensitivity_;
    }
    
    void setMinDetectionDuration(int durationMs) override {
        minDetectionDurationMs_ = std::max(0, durationMs);
        MICMAP_LOG_DEBUG("Set minimum detection duration to ", minDetectionDurationMs_, " ms");
    }
    
    int getMinDetectionDuration() const override {
        return minDetectionDurationMs_;
    }
    
    const TrainingData& getTrainingData() const override {
        return trainingData_;
    }
    
private:
    /**
     * @brief Compute Pearson correlation coefficient between two vectors
     *
     * This is more discriminative than simple cosine similarity because it
     * subtracts the mean first, measuring how well the shapes correlate
     * rather than just the overall magnitude distribution.
     */
    float computeCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.empty() || b.empty()) {
            return 0.0f;
        }
        
        size_t minSize = std::min(a.size(), b.size());
        
        // Compute means
        float meanA = 0.0f, meanB = 0.0f;
        for (size_t i = 0; i < minSize; ++i) {
            meanA += a[i];
            meanB += b[i];
        }
        meanA /= static_cast<float>(minSize);
        meanB /= static_cast<float>(minSize);
        
        // Compute Pearson correlation: sum((a-meanA)*(b-meanB)) / (stdA * stdB * n)
        float sumAB = 0.0f;
        float sumA2 = 0.0f;
        float sumB2 = 0.0f;
        
        for (size_t i = 0; i < minSize; ++i) {
            float devA = a[i] - meanA;
            float devB = b[i] - meanB;
            sumAB += devA * devB;
            sumA2 += devA * devA;
            sumB2 += devB * devB;
        }
        
        float denominator = std::sqrt(sumA2 * sumB2);
        if (denominator < EPSILON) {
            return 0.0f;
        }
        
        float correlation = sumAB / denominator;
        
        // Pearson correlation ranges from -1 to 1
        // Map to 0-1 range: negative correlation = 0, positive = correlation value
        return std::max(0.0f, correlation);
    }
    
    /**
     * @brief Compute spectral shape distance using log-magnitude comparison
     *
     * This compares the relative shape of spectra in log domain,
     * which is more perceptually meaningful and discriminative.
     */
    float computeSpectralShapeDistance(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.empty() || b.empty()) {
            return 1.0f;  // Maximum distance
        }
        
        size_t minSize = std::min(a.size(), b.size());
        
        // Convert to log domain and normalize
        std::vector<float> logA(minSize), logB(minSize);
        float sumLogA = 0.0f, sumLogB = 0.0f;
        
        for (size_t i = 0; i < minSize; ++i) {
            logA[i] = std::log(a[i] + EPSILON);
            logB[i] = std::log(b[i] + EPSILON);
            sumLogA += logA[i];
            sumLogB += logB[i];
        }
        
        // Normalize to zero mean (removes overall level difference)
        float meanLogA = sumLogA / static_cast<float>(minSize);
        float meanLogB = sumLogB / static_cast<float>(minSize);
        
        // Compute mean squared error of normalized log spectra
        float mse = 0.0f;
        for (size_t i = 0; i < minSize; ++i) {
            float diff = (logA[i] - meanLogA) - (logB[i] - meanLogB);
            mse += diff * diff;
        }
        mse /= static_cast<float>(minSize);
        
        // Convert MSE to similarity (0 = different, 1 = identical)
        // Use exponential decay: similarity = exp(-mse / scale)
        float similarity = std::exp(-mse / 2.0f);
        return similarity;
    }
    
    /**
     * @brief L2 normalize a vector in place
     */
    void normalizeVector(std::vector<float>& v) {
        float sumSq = 0.0f;
        for (float x : v) {
            sumSq += x * x;
        }
        
        float norm = std::sqrt(sumSq);
        if (norm > EPSILON) {
            for (float& x : v) {
                x /= norm;
            }
        }
    }
    
    /**
     * @brief Compute standard deviation
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
     * @brief Update temporal state and check if detection duration threshold is met
     * 
     * Implements the configurable continuous detection requirement.
     * Returns true only if white noise has been detected continuously for >= minDetectionDurationMs_.
     * The duration is configured via setMinDetectionDuration() which should be called
     * with the value from config (detection.minDurationMs).
     */
    bool updateTemporalState(bool instantDetection) {
        auto now = std::chrono::steady_clock::now();
        
        if (instantDetection) {
            if (!isCurrentlyDetecting_) {
                // Start of new detection period
                detectionStartTime_ = now;
                isCurrentlyDetecting_ = true;
                MICMAP_LOG_DEBUG("Detection started");
            }
            
            // Check if we've been detecting long enough
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - detectionStartTime_
            );
            
            if (duration.count() >= minDetectionDurationMs_) {
                return true;  // Confirmed detection
            }
        } else {
            if (isCurrentlyDetecting_) {
                // Detection lost
                isCurrentlyDetecting_ = false;
                MICMAP_LOG_DEBUG("Detection lost");
            }
        }
        
        return false;  // Not yet confirmed
    }
    
    // Configuration
    uint32_t sampleRate_;
    size_t fftSize_;
    float sensitivity_;
    int minDetectionDurationMs_;
    
    // Components
    std::unique_ptr<ISpectralAnalyzer> analyzer_;
    
    // Training state
    bool training_;
    std::vector<std::vector<float>> trainingSpectra_;
    std::vector<float> trainingEnergies_;
    std::vector<float> trainingFlatnesses_;
    
    // Trained data
    TrainingData trainingData_;
    float spectralFlatnessThreshold_ = 0.3f;
    float energyMinThreshold_ = 0.0f;
    float energyVarianceThreshold_ = 0.5f;
    bool hasTrainingData_;
    
    // Temporal detection state
    std::chrono::steady_clock::time_point detectionStartTime_;
    bool isCurrentlyDetecting_;
    
    // Spike detection state
    bool spikeTriggered_ = false;
    std::chrono::steady_clock::time_point spikeTime_;
    
    // Energy history for consistency tracking
    static constexpr size_t ENERGY_HISTORY_SIZE = 10;
    std::vector<float> energyHistory_;
    size_t energyHistoryIndex_ = 0;
    
    // Confidence history for frequency-based detection
    static constexpr size_t CONFIDENCE_HISTORY_SIZE = 12;
    std::vector<bool> confidenceHistory_;
    size_t confidenceHistoryIndex_ = 0;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    /**
     * @brief Update energy history buffer
     */
    void updateEnergyHistory(float energy) {
        if (energyHistory_.size() < ENERGY_HISTORY_SIZE) {
            energyHistory_.push_back(energy);
        } else {
            energyHistory_[energyHistoryIndex_] = energy;
            energyHistoryIndex_ = (energyHistoryIndex_ + 1) % ENERGY_HISTORY_SIZE;
        }
    }
    
    /**
     * @brief Update confidence history (tracks high/low hits)
     */
    void updateConfidenceHistory(bool isHigh) {
        if (confidenceHistory_.size() < CONFIDENCE_HISTORY_SIZE) {
            confidenceHistory_.push_back(isHigh);
        } else {
            confidenceHistory_[confidenceHistoryIndex_] = isHigh;
            confidenceHistoryIndex_ = (confidenceHistoryIndex_ + 1) % CONFIDENCE_HISTORY_SIZE;
        }
    }
    
    /**
     * @brief Count high-confidence hits in recent history
     */
    int countHighConfidenceHits() {
        int count = 0;
        for (bool isHigh : confidenceHistory_) {
            if (isHigh) count++;
        }
        return count;
    }
    
    /**
     * @brief Compute energy consistency (low variance = consistent = covered mic)
     *
     * Returns 0-1 where 1 = very consistent (like covered mic)
     * Speech and game audio have high variance, so this helps distinguish
     */
    float computeEnergyConsistency() {
        if (energyHistory_.size() < 3) {
            return 0.5f;  // Not enough data
        }
        
        // Compute coefficient of variation (stddev / mean)
        float mean = std::accumulate(energyHistory_.begin(), energyHistory_.end(), 0.0f)
                    / static_cast<float>(energyHistory_.size());
        
        if (mean < EPSILON) {
            return 0.0f;  // No signal
        }
        
        float variance = 0.0f;
        for (float e : energyHistory_) {
            float diff = e - mean;
            variance += diff * diff;
        }
        variance /= static_cast<float>(energyHistory_.size());
        float stddev = std::sqrt(variance);
        float cv = stddev / mean;  // Coefficient of variation
        
        // Lower CV = more consistent = higher score
        // CV of 0 = perfect consistency = score 1.0
        // CV of 1 = high variance = score 0.0
        float consistency = std::max(0.0f, 1.0f - cv);
        return consistency;
    }
};

std::unique_ptr<INoiseDetector> createFFTDetector(uint32_t sampleRate, size_t fftSize) {
    return std::make_unique<FFTNoiseDetector>(sampleRate, fftSize);
}

} // namespace micmap::detection
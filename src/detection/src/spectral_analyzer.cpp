/**
 * @file spectral_analyzer.cpp
 * @brief KissFFT-based spectral analyzer implementation
 */

#include "micmap/detection/spectral_analyzer.hpp"
#include "micmap/common/logger.hpp"

// KissFFT headers
#include "kiss_fft.h"
#include "kiss_fftr.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace micmap::detection {

namespace {
    constexpr float PI = 3.14159265358979323846f;
    constexpr float EPSILON = 1e-10f;
}

/**
 * @brief KissFFT-based spectral analyzer implementation
 * 
 * Uses KissFFT for efficient FFT computation with Hanning window
 * to reduce spectral leakage.
 */
class KissFFTAnalyzer : public ISpectralAnalyzer {
public:
    KissFFTAnalyzer(uint32_t sampleRate, size_t fftSize)
        : sampleRate_(sampleRate)
        , fftSize_(fftSize)
        , frequencyResolution_(static_cast<float>(sampleRate) / static_cast<float>(fftSize))
        , numBins_(fftSize / 2 + 1)
        , fftConfig_(nullptr) {
        
        // Validate FFT size is power of 2
        if (fftSize == 0 || (fftSize & (fftSize - 1)) != 0) {
            throw std::invalid_argument("FFT size must be a power of 2");
        }
        
        // Allocate buffers
        windowedSamples_.resize(fftSize_);
        fftOutput_.resize(numBins_);
        window_.resize(fftSize_);
        
        // Create Hanning window for reduced spectral leakage
        // Hanning: w(n) = 0.5 * (1 - cos(2*pi*n / (N-1)))
        for (size_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * PI * static_cast<float>(i) / static_cast<float>(fftSize_ - 1)));
        }
        
        // Compute window normalization factor (for energy preservation)
        windowNormFactor_ = 0.0f;
        for (float w : window_) {
            windowNormFactor_ += w * w;
        }
        windowNormFactor_ = std::sqrt(windowNormFactor_ / static_cast<float>(fftSize_));
        
        // Initialize KissFFT for real-to-complex transform
        fftConfig_ = kiss_fftr_alloc(static_cast<int>(fftSize_), 0, nullptr, nullptr);
        if (!fftConfig_) {
            throw std::runtime_error("Failed to allocate KissFFT configuration");
        }
        
        MICMAP_LOG_DEBUG("Created KissFFT spectral analyzer: ", fftSize_, " point FFT at ", sampleRate_, " Hz");
        MICMAP_LOG_DEBUG("Frequency resolution: ", frequencyResolution_, " Hz/bin, ", numBins_, " bins");
    }
    
    ~KissFFTAnalyzer() override {
        if (fftConfig_) {
            kiss_fftr_free(fftConfig_);
            fftConfig_ = nullptr;
        }
    }
    
    // Non-copyable
    KissFFTAnalyzer(const KissFFTAnalyzer&) = delete;
    KissFFTAnalyzer& operator=(const KissFFTAnalyzer&) = delete;
    
    SpectralResult analyze(const float* samples, size_t count) override {
        SpectralResult result;
        result.magnitudes.resize(numBins_, 0.0f);
        result.spectralFlatness = 0.0f;
        result.spectralCentroid = 0.0f;
        result.energy = 0.0f;
        
        if (!samples || count == 0) {
            return result;
        }
        
        // Compute signal energy first (before windowing)
        result.energy = computeEnergy(samples, count);
        
        // Prepare windowed samples
        prepareWindowedSamples(samples, count);
        
        // Perform FFT
        kiss_fftr(fftConfig_, windowedSamples_.data(), fftOutput_.data());
        
        // Extract magnitudes (normalized)
        const float normFactor = 2.0f / static_cast<float>(fftSize_);
        for (size_t i = 0; i < numBins_; ++i) {
            float real = fftOutput_[i].r;
            float imag = fftOutput_[i].i;
            result.magnitudes[i] = std::sqrt(real * real + imag * imag) * normFactor;
        }
        
        // DC and Nyquist bins should not be doubled
        result.magnitudes[0] *= 0.5f;
        if (numBins_ > 1) {
            result.magnitudes[numBins_ - 1] *= 0.5f;
        }
        
        // Compute spectral features
        result.spectralFlatness = computeSpectralFlatness(result.magnitudes);
        result.spectralCentroid = computeSpectralCentroid(result.magnitudes);
        
        return result;
    }
    
    size_t getFFTSize() const override {
        return fftSize_;
    }
    
    uint32_t getSampleRate() const override {
        return sampleRate_;
    }
    
    float getFrequencyResolution() const override {
        return frequencyResolution_;
    }
    
    float binToFrequency(size_t bin) const override {
        return static_cast<float>(bin) * frequencyResolution_;
    }
    
    size_t frequencyToBin(float frequency) const override {
        if (frequency < 0.0f) return 0;
        size_t bin = static_cast<size_t>(frequency / frequencyResolution_ + 0.5f);
        return std::min(bin, numBins_ - 1);
    }
    
private:
    /**
     * @brief Prepare windowed samples for FFT
     * 
     * Takes the last fftSize_ samples from input, applies Hanning window.
     * Zero-pads if not enough samples available.
     */
    void prepareWindowedSamples(const float* samples, size_t count) {
        // Clear buffer
        std::fill(windowedSamples_.begin(), windowedSamples_.end(), 0.0f);
        
        if (count >= fftSize_) {
            // Use the last fftSize_ samples
            const float* src = samples + (count - fftSize_);
            for (size_t i = 0; i < fftSize_; ++i) {
                windowedSamples_[i] = src[i] * window_[i];
            }
        } else {
            // Zero-pad at the beginning, place samples at the end
            size_t offset = fftSize_ - count;
            for (size_t i = 0; i < count; ++i) {
                windowedSamples_[offset + i] = samples[i] * window_[offset + i];
            }
        }
    }
    
    /**
     * @brief Compute spectral flatness (Wiener entropy)
     * 
     * Spectral flatness = geometric_mean(spectrum) / arithmetic_mean(spectrum)
     * 
     * White noise has a flat spectrum, so spectral flatness approaches 1.0
     * Tonal sounds have peaks, so spectral flatness is much lower
     */
    float computeSpectralFlatness(const std::vector<float>& magnitudes) {
        if (magnitudes.empty()) {
            return 0.0f;
        }
        
        // Use only positive frequency bins (skip DC)
        double logSum = 0.0;
        double sum = 0.0;
        size_t validCount = 0;
        
        // Start from bin 1 to skip DC component
        for (size_t i = 1; i < magnitudes.size(); ++i) {
            float mag = magnitudes[i];
            if (mag > EPSILON) {
                logSum += std::log(static_cast<double>(mag));
                sum += mag;
                ++validCount;
            }
        }
        
        if (validCount == 0 || sum < EPSILON) {
            return 0.0f;
        }
        
        // Geometric mean = exp(mean(log(x)))
        double geometricMean = std::exp(logSum / static_cast<double>(validCount));
        double arithmeticMean = sum / static_cast<double>(validCount);
        
        // Clamp result to [0, 1]
        float flatness = static_cast<float>(geometricMean / arithmeticMean);
        return std::clamp(flatness, 0.0f, 1.0f);
    }
    
    /**
     * @brief Compute spectral centroid
     * 
     * The spectral centroid is the "center of mass" of the spectrum,
     * indicating where most of the spectral energy is concentrated.
     */
    float computeSpectralCentroid(const std::vector<float>& magnitudes) {
        if (magnitudes.empty()) {
            return 0.0f;
        }
        
        float weightedSum = 0.0f;
        float sum = 0.0f;
        
        for (size_t i = 0; i < magnitudes.size(); ++i) {
            float freq = binToFrequency(i);
            float mag = magnitudes[i];
            weightedSum += freq * mag;
            sum += mag;
        }
        
        if (sum < EPSILON) {
            return 0.0f;
        }
        
        return weightedSum / sum;
    }
    
    /**
     * @brief Compute signal energy (RMS squared)
     */
    float computeEnergy(const float* samples, size_t count) {
        if (!samples || count == 0) {
            return 0.0f;
        }
        
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
        }
        
        return static_cast<float>(sum / static_cast<double>(count));
    }
    
    uint32_t sampleRate_;
    size_t fftSize_;
    float frequencyResolution_;
    size_t numBins_;
    float windowNormFactor_;
    
    std::vector<float> windowedSamples_;
    std::vector<kiss_fft_cpx> fftOutput_;
    std::vector<float> window_;
    
    kiss_fftr_cfg fftConfig_;
};

std::unique_ptr<ISpectralAnalyzer> createKissFFTAnalyzer(uint32_t sampleRate, size_t fftSize) {
    return std::make_unique<KissFFTAnalyzer>(sampleRate, fftSize);
}

} // namespace micmap::detection
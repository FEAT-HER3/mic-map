/**
 * @file spectral_analyzer.cpp
 * @brief KissFFT-based spectral analyzer implementation
 */

#include "micmap/detection/spectral_analyzer.hpp"
#include "micmap/common/logger.hpp"

#include <cmath>
#include <algorithm>
#include <numeric>

// Forward declare KissFFT types - actual implementation will use the library
// For now, we provide a stub implementation
namespace kissfft_stub {
    struct kiss_fft_cpx {
        float r;
        float i;
    };
}

namespace micmap::detection {

/**
 * @brief KissFFT-based spectral analyzer implementation
 */
class KissFFTAnalyzer : public ISpectralAnalyzer {
public:
    KissFFTAnalyzer(uint32_t sampleRate, size_t fftSize)
        : sampleRate_(sampleRate)
        , fftSize_(fftSize)
        , frequencyResolution_(static_cast<float>(sampleRate) / static_cast<float>(fftSize)) {
        
        // Allocate buffers
        windowedSamples_.resize(fftSize_);
        fftOutput_.resize(fftSize_ / 2 + 1);
        window_.resize(fftSize_);
        
        // Create Hann window
        for (size_t i = 0; i < fftSize_; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * i / (fftSize_ - 1)));
        }
        
        MICMAP_LOG_DEBUG("Created spectral analyzer: ", fftSize_, " point FFT at ", sampleRate_, " Hz");
    }
    
    ~KissFFTAnalyzer() override = default;
    
    SpectralResult analyze(const float* samples, size_t count) override {
        SpectralResult result;
        
        if (!samples || count == 0) {
            return result;
        }
        
        // Use the last fftSize_ samples, or zero-pad if not enough
        size_t offset = (count >= fftSize_) ? (count - fftSize_) : 0;
        size_t available = std::min(count, fftSize_);
        
        // Apply window and copy samples
        std::fill(windowedSamples_.begin(), windowedSamples_.end(), 0.0f);
        for (size_t i = 0; i < available; ++i) {
            size_t srcIdx = offset + i;
            size_t dstIdx = fftSize_ - available + i;
            windowedSamples_[dstIdx] = samples[srcIdx] * window_[dstIdx];
        }
        
        // Compute FFT (stub implementation - just compute magnitude from time domain)
        // In real implementation, this would use KissFFT
        computeFFT();
        
        // Extract magnitudes
        result.magnitudes.resize(fftOutput_.size());
        for (size_t i = 0; i < fftOutput_.size(); ++i) {
            result.magnitudes[i] = std::sqrt(
                fftOutput_[i].r * fftOutput_[i].r + 
                fftOutput_[i].i * fftOutput_[i].i
            );
        }
        
        // Compute spectral flatness
        result.spectralFlatness = computeSpectralFlatness(result.magnitudes);
        
        // Compute spectral centroid
        result.spectralCentroid = computeSpectralCentroid(result.magnitudes);
        
        // Compute energy
        result.energy = computeEnergy(samples, count);
        
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
        return static_cast<size_t>(frequency / frequencyResolution_);
    }
    
private:
    void computeFFT() {
        // Stub FFT implementation
        // In real implementation, this would call kiss_fft
        // For now, just compute a simple DFT for small sizes or approximate
        
        size_t numBins = fftOutput_.size();
        
        for (size_t k = 0; k < numBins; ++k) {
            float real = 0.0f;
            float imag = 0.0f;
            
            // Simple DFT (very slow, but works for stub)
            // Real implementation uses KissFFT
            for (size_t n = 0; n < fftSize_; ++n) {
                float angle = 2.0f * 3.14159265358979f * k * n / fftSize_;
                real += windowedSamples_[n] * std::cos(angle);
                imag -= windowedSamples_[n] * std::sin(angle);
            }
            
            fftOutput_[k].r = real;
            fftOutput_[k].i = imag;
        }
    }
    
    float computeSpectralFlatness(const std::vector<float>& magnitudes) {
        if (magnitudes.empty()) {
            return 0.0f;
        }
        
        // Spectral flatness = geometric mean / arithmetic mean
        double logSum = 0.0;
        double sum = 0.0;
        size_t count = 0;
        
        for (float mag : magnitudes) {
            if (mag > 1e-10f) {
                logSum += std::log(mag);
                sum += mag;
                ++count;
            }
        }
        
        if (count == 0 || sum == 0.0) {
            return 0.0f;
        }
        
        double geometricMean = std::exp(logSum / count);
        double arithmeticMean = sum / count;
        
        return static_cast<float>(geometricMean / arithmeticMean);
    }
    
    float computeSpectralCentroid(const std::vector<float>& magnitudes) {
        if (magnitudes.empty()) {
            return 0.0f;
        }
        
        float weightedSum = 0.0f;
        float sum = 0.0f;
        
        for (size_t i = 0; i < magnitudes.size(); ++i) {
            float freq = binToFrequency(i);
            weightedSum += freq * magnitudes[i];
            sum += magnitudes[i];
        }
        
        if (sum == 0.0f) {
            return 0.0f;
        }
        
        return weightedSum / sum;
    }
    
    float computeEnergy(const float* samples, size_t count) {
        if (!samples || count == 0) {
            return 0.0f;
        }
        
        float sum = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            sum += samples[i] * samples[i];
        }
        
        return sum / count;
    }
    
    uint32_t sampleRate_;
    size_t fftSize_;
    float frequencyResolution_;
    
    std::vector<float> windowedSamples_;
    std::vector<kissfft_stub::kiss_fft_cpx> fftOutput_;
    std::vector<float> window_;
};

std::unique_ptr<ISpectralAnalyzer> createKissFFTAnalyzer(uint32_t sampleRate, size_t fftSize) {
    return std::make_unique<KissFFTAnalyzer>(sampleRate, fftSize);
}

} // namespace micmap::detection
#pragma once

/**
 * @file spectral_analyzer.hpp
 * @brief FFT-based spectral analysis for audio signals
 */

#include <vector>
#include <cstddef>
#include <memory>
#include <complex>

namespace micmap::detection {

/**
 * @brief Result of spectral analysis
 */
struct SpectralResult {
    std::vector<float> magnitudes;      ///< Magnitude spectrum
    std::vector<float> phases;          ///< Phase spectrum (optional)
    float spectralFlatness;             ///< Spectral flatness measure (0-1)
    float spectralCentroid;             ///< Spectral centroid frequency
    float energy;                       ///< Total signal energy
};

/**
 * @brief Interface for spectral analysis
 */
class ISpectralAnalyzer {
public:
    virtual ~ISpectralAnalyzer() = default;
    
    /**
     * @brief Analyze audio samples and compute spectrum
     * @param samples Input audio samples
     * @param count Number of samples
     * @return Spectral analysis result
     */
    virtual SpectralResult analyze(const float* samples, size_t count) = 0;
    
    /**
     * @brief Get the FFT size
     * @return FFT size in samples
     */
    virtual size_t getFFTSize() const = 0;
    
    /**
     * @brief Get the sample rate
     * @return Sample rate in Hz
     */
    virtual uint32_t getSampleRate() const = 0;
    
    /**
     * @brief Get frequency resolution
     * @return Frequency resolution in Hz per bin
     */
    virtual float getFrequencyResolution() const = 0;
    
    /**
     * @brief Convert bin index to frequency
     * @param bin Bin index
     * @return Frequency in Hz
     */
    virtual float binToFrequency(size_t bin) const = 0;
    
    /**
     * @brief Convert frequency to bin index
     * @param frequency Frequency in Hz
     * @return Bin index
     */
    virtual size_t frequencyToBin(float frequency) const = 0;
};

/**
 * @brief Create a KissFFT-based spectral analyzer
 * @param sampleRate Audio sample rate in Hz
 * @param fftSize FFT window size (must be power of 2)
 * @return Unique pointer to spectral analyzer
 */
std::unique_ptr<ISpectralAnalyzer> createKissFFTAnalyzer(uint32_t sampleRate, size_t fftSize = 2048);

} // namespace micmap::detection
#pragma once

/**
 * @file audio_capture.hpp
 * @brief Audio capture interface and implementation
 */

#include "device_enumerator.hpp"
#include "audio_buffer.hpp"

#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>

namespace micmap::audio {

/**
 * @brief Callback type for audio data
 * @param samples Pointer to audio samples (normalized float -1.0 to 1.0)
 * @param count Number of samples
 */
using AudioCallback = std::function<void(const float*, size_t)>;

/**
 * @brief Interface for audio capture
 */
class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    
    /**
     * @brief Enumerate available audio input devices
     * @return Vector of available devices
     */
    virtual std::vector<AudioDevice> enumerateDevices() = 0;
    
    /**
     * @brief Select a device by name pattern
     * @param namePattern Substring to match in device name
     * @return True if a matching device was found and selected
     */
    virtual bool selectDevice(const std::wstring& namePattern) = 0;
    
    /**
     * @brief Select a device by its unique ID
     * @param deviceId The device ID
     * @return True if the device was found and selected
     */
    virtual bool selectDeviceById(const std::wstring& deviceId) = 0;
    
    /**
     * @brief Start capturing audio
     * @return True if capture started successfully
     */
    virtual bool startCapture() = 0;
    
    /**
     * @brief Stop capturing audio
     */
    virtual void stopCapture() = 0;
    
    /**
     * @brief Check if currently capturing
     * @return True if capture is active
     */
    virtual bool isCapturing() const = 0;
    
    /**
     * @brief Get audio data from the buffer
     * @param buffer Vector to fill with audio samples
     * @return True if data was available
     */
    virtual bool getAudioBuffer(std::vector<float>& buffer) = 0;
    
    /**
     * @brief Get the currently selected device
     * @return Current device information
     */
    virtual AudioDevice getCurrentDevice() const = 0;
    
    /**
     * @brief Set callback for audio data
     * @param callback Function to call when audio data is available
     */
    virtual void setAudioCallback(AudioCallback callback) = 0;
    
    /**
     * @brief Get the sample rate of the current device
     * @return Sample rate in Hz
     */
    virtual uint32_t getSampleRate() const = 0;
    
    /**
     * @brief Get the number of channels
     * @return Number of audio channels
     */
    virtual uint16_t getChannels() const = 0;
};

/**
 * @brief Create a WASAPI-based audio capture instance
 * @return Unique pointer to the audio capture interface
 */
std::unique_ptr<IAudioCapture> createWASAPICapture();

} // namespace micmap::audio
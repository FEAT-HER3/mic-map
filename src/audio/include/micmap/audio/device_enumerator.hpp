#pragma once

/**
 * @file device_enumerator.hpp
 * @brief Audio device enumeration
 */

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace micmap::audio {

/**
 * @brief Information about an audio device
 */
struct AudioDevice {
    std::wstring id;            ///< Unique device identifier
    std::wstring name;          ///< Human-readable device name
    uint32_t sampleRate;        ///< Default sample rate in Hz
    uint16_t channels;          ///< Number of channels
    uint16_t bitsPerSample;     ///< Bits per sample
    bool isDefault;             ///< True if this is the default device
};

/**
 * @brief Interface for audio device enumeration
 */
class IDeviceEnumerator {
public:
    virtual ~IDeviceEnumerator() = default;
    
    /**
     * @brief Enumerate all available audio input devices
     * @return Vector of available audio devices
     */
    virtual std::vector<AudioDevice> enumerateDevices() = 0;
    
    /**
     * @brief Get the default audio input device
     * @return Default device, or empty optional if none available
     */
    virtual AudioDevice getDefaultDevice() = 0;
    
    /**
     * @brief Find a device by name pattern
     * @param pattern Substring to search for in device names
     * @return First matching device, or empty optional if not found
     */
    virtual AudioDevice findDeviceByName(const std::wstring& pattern) = 0;
    
    /**
     * @brief Find a device by its unique ID
     * @param deviceId The device ID to search for
     * @return Matching device, or empty optional if not found
     */
    virtual AudioDevice findDeviceById(const std::wstring& deviceId) = 0;
    
    /**
     * @brief Refresh the device list
     */
    virtual void refresh() = 0;
};

/**
 * @brief Create a WASAPI-based device enumerator
 * @return Unique pointer to the device enumerator
 */
std::unique_ptr<IDeviceEnumerator> createWASAPIDeviceEnumerator();

} // namespace micmap::audio
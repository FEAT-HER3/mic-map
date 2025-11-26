/**
 * @file device_enumerator.cpp
 * @brief WASAPI device enumeration implementation
 */

#include "micmap/audio/device_enumerator.hpp"
#include "micmap/common/logger.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#endif

#include <algorithm>

namespace micmap::audio {

#ifdef _WIN32

using Microsoft::WRL::ComPtr;

/**
 * @brief WASAPI implementation of device enumerator
 */
class WASAPIDeviceEnumerator : public IDeviceEnumerator {
public:
    WASAPIDeviceEnumerator() {
        // Initialize COM for this thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE;
        
        if (comInitialized_) {
            hr = CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(enumerator_.GetAddressOf())
            );
            
            if (FAILED(hr)) {
                MICMAP_LOG_ERROR("Failed to create device enumerator: ", hr);
            }
        }
    }
    
    ~WASAPIDeviceEnumerator() override {
        enumerator_.Reset();
        if (comInitialized_) {
            CoUninitialize();
        }
    }
    
    std::vector<AudioDevice> enumerateDevices() override {
        std::vector<AudioDevice> devices;
        
        if (!enumerator_) {
            return devices;
        }
        
        ComPtr<IMMDeviceCollection> collection;
        HRESULT hr = enumerator_->EnumAudioEndpoints(
            eCapture,
            DEVICE_STATE_ACTIVE,
            collection.GetAddressOf()
        );
        
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to enumerate audio endpoints: ", hr);
            return devices;
        }
        
        UINT count = 0;
        hr = collection->GetCount(&count);
        if (FAILED(hr)) {
            return devices;
        }
        
        // Get default device ID for comparison
        std::wstring defaultId;
        ComPtr<IMMDevice> defaultDevice;
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, defaultDevice.GetAddressOf()))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&id))) {
                defaultId = id;
                CoTaskMemFree(id);
            }
        }
        
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            hr = collection->Item(i, device.GetAddressOf());
            if (FAILED(hr)) {
                continue;
            }
            
            AudioDevice audioDevice = getDeviceInfo(device.Get());
            audioDevice.isDefault = (audioDevice.id == defaultId);
            devices.push_back(std::move(audioDevice));
        }
        
        return devices;
    }
    
    AudioDevice getDefaultDevice() override {
        AudioDevice device{};
        
        if (!enumerator_) {
            return device;
        }
        
        ComPtr<IMMDevice> mmDevice;
        HRESULT hr = enumerator_->GetDefaultAudioEndpoint(
            eCapture,
            eConsole,
            mmDevice.GetAddressOf()
        );
        
        if (SUCCEEDED(hr)) {
            device = getDeviceInfo(mmDevice.Get());
            device.isDefault = true;
        }
        
        return device;
    }
    
    AudioDevice findDeviceByName(const std::wstring& pattern) override {
        auto devices = enumerateDevices();
        
        for (const auto& device : devices) {
            if (device.name.find(pattern) != std::wstring::npos) {
                return device;
            }
        }
        
        return AudioDevice{};
    }
    
    AudioDevice findDeviceById(const std::wstring& deviceId) override {
        AudioDevice device{};
        
        if (!enumerator_ || deviceId.empty()) {
            return device;
        }
        
        ComPtr<IMMDevice> mmDevice;
        HRESULT hr = enumerator_->GetDevice(deviceId.c_str(), mmDevice.GetAddressOf());
        
        if (SUCCEEDED(hr)) {
            device = getDeviceInfo(mmDevice.Get());
        }
        
        return device;
    }
    
    void refresh() override {
        // The enumerator automatically reflects current state
        // No explicit refresh needed for WASAPI
    }
    
private:
    AudioDevice getDeviceInfo(IMMDevice* device) {
        AudioDevice info{};
        
        if (!device) {
            return info;
        }
        
        // Get device ID
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            info.id = id;
            CoTaskMemFree(id);
        }
        
        // Get device properties
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
                if (varName.vt == VT_LPWSTR) {
                    info.name = varName.pwszVal;
                }
                PropVariantClear(&varName);
            }
        }
        
        // Get audio format info
        ComPtr<IAudioClient> audioClient;
        if (SUCCEEDED(device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(audioClient.GetAddressOf())
        ))) {
            WAVEFORMATEX* format = nullptr;
            if (SUCCEEDED(audioClient->GetMixFormat(&format))) {
                info.sampleRate = format->nSamplesPerSec;
                info.channels = static_cast<uint16_t>(format->nChannels);
                info.bitsPerSample = static_cast<uint16_t>(format->wBitsPerSample);
                CoTaskMemFree(format);
            }
        }
        
        return info;
    }
    
    ComPtr<IMMDeviceEnumerator> enumerator_;
    bool comInitialized_ = false;
};

std::unique_ptr<IDeviceEnumerator> createWASAPIDeviceEnumerator() {
    return std::make_unique<WASAPIDeviceEnumerator>();
}

#else

// Stub implementation for non-Windows platforms
class StubDeviceEnumerator : public IDeviceEnumerator {
public:
    std::vector<AudioDevice> enumerateDevices() override {
        return {};
    }
    
    AudioDevice getDefaultDevice() override {
        return {};
    }
    
    AudioDevice findDeviceByName(const std::wstring&) override {
        return {};
    }
    
    AudioDevice findDeviceById(const std::wstring&) override {
        return {};
    }
    
    void refresh() override {}
};

std::unique_ptr<IDeviceEnumerator> createWASAPIDeviceEnumerator() {
    return std::make_unique<StubDeviceEnumerator>();
}

#endif

} // namespace micmap::audio
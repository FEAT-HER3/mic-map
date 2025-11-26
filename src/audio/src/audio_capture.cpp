/**
 * @file audio_capture.cpp
 * @brief WASAPI audio capture implementation with format conversion and mono downmix
 */

#include "micmap/audio/audio_capture.hpp"
#include "micmap/common/logger.hpp"

#ifdef _WIN32
// WIN32_LEAN_AND_MEAN and NOMINMAX are defined in CMakeLists.txt
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <ksmedia.h>
#endif

#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace micmap::audio {

#ifdef _WIN32

using Microsoft::WRL::ComPtr;

/**
 * @brief Audio format types that WASAPI may return
 */
enum class AudioFormatType {
    Unknown,
    Float32,
    Int16,
    Int24,
    Int32
};

/**
 * @brief Determine the audio format type from WAVEFORMATEX
 */
static AudioFormatType getFormatType(const WAVEFORMATEX* format) {
    if (!format) {
        return AudioFormatType::Unknown;
    }
    
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return AudioFormatType::Float32;
    }
    
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        switch (format->wBitsPerSample) {
            case 16: return AudioFormatType::Int16;
            case 24: return AudioFormatType::Int24;
            case 32: return AudioFormatType::Int32;
            default: return AudioFormatType::Unknown;
        }
    }
    
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            return AudioFormatType::Float32;
        }
        
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            switch (ext->Samples.wValidBitsPerSample) {
                case 16: return AudioFormatType::Int16;
                case 24: return AudioFormatType::Int24;
                case 32: return AudioFormatType::Int32;
                default: return AudioFormatType::Unknown;
            }
        }
    }
    
    return AudioFormatType::Unknown;
}

/**
 * @brief Convert 16-bit PCM sample to float
 */
static inline float int16ToFloat(int16_t sample) {
    return static_cast<float>(sample) / 32768.0f;
}

/**
 * @brief Convert 24-bit PCM sample (stored in 3 bytes) to float
 */
static inline float int24ToFloat(const uint8_t* bytes) {
    // 24-bit samples are stored as 3 bytes, little-endian
    int32_t sample = static_cast<int32_t>(bytes[0]) |
                     (static_cast<int32_t>(bytes[1]) << 8) |
                     (static_cast<int32_t>(bytes[2]) << 16);
    // Sign extend from 24 bits to 32 bits
    if (sample & 0x800000) {
        sample |= static_cast<int32_t>(0xFF000000);
    }
    return static_cast<float>(sample) / 8388608.0f;  // 2^23
}

/**
 * @brief Convert 32-bit PCM sample to float
 */
static inline float int32ToFloat(int32_t sample) {
    return static_cast<float>(sample) / 2147483648.0f;  // 2^31
}

/**
 * @brief Device notification callback for handling disconnection
 */
class DeviceNotificationClient : public IMMNotificationClient {
public:
    explicit DeviceNotificationClient(std::function<void(const std::wstring&)> onDeviceRemoved)
        : refCount_(1)
        , onDeviceRemoved_(std::move(onDeviceRemoved)) {}
    
    // IUnknown methods
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&refCount_);
    }
    
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return count;
    }
    
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *ppvObject = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    
    // IMMNotificationClient methods
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override {
        if (dwNewState == DEVICE_STATE_UNPLUGGED || dwNewState == DEVICE_STATE_DISABLED) {
            if (onDeviceRemoved_) {
                onDeviceRemoved_(pwstrDeviceId);
            }
        }
        return S_OK;
    }
    
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR /*pwstrDeviceId*/) override {
        return S_OK;
    }
    
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
        if (onDeviceRemoved_) {
            onDeviceRemoved_(pwstrDeviceId);
        }
        return S_OK;
    }
    
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow /*flow*/, ERole /*role*/, 
                                                      LPCWSTR /*pwstrDefaultDeviceId*/) override {
        return S_OK;
    }
    
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, 
                                                      const PROPERTYKEY /*key*/) override {
        return S_OK;
    }
    
private:
    LONG refCount_;
    std::function<void(const std::wstring&)> onDeviceRemoved_;
};

/**
 * @brief WASAPI implementation of audio capture with format conversion
 */
class WASAPIAudioCapture : public IAudioCapture {
public:
    WASAPIAudioCapture()
        : capturing_(false)
        , deviceLost_(false)
        , sampleRate_(0)
        , channels_(0)
        , sourceChannels_(0)
        , bytesPerFrame_(0)
        , formatType_(AudioFormatType::Unknown)
        , notificationClient_(nullptr) {
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
            } else {
                // Register for device notifications
                notificationClient_ = new DeviceNotificationClient(
                    [this](const std::wstring& deviceId) {
                        onDeviceRemoved(deviceId);
                    }
                );
                enumerator_->RegisterEndpointNotificationCallback(notificationClient_);
            }
        }
    }
    
    ~WASAPIAudioCapture() override {
        stopCapture();
        
        if (enumerator_ && notificationClient_) {
            enumerator_->UnregisterEndpointNotificationCallback(notificationClient_);
            notificationClient_->Release();
            notificationClient_ = nullptr;
        }
        
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
            return devices;
        }
        
        UINT count = 0;
        collection->GetCount(&count);
        
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
            if (SUCCEEDED(collection->Item(i, device.GetAddressOf()))) {
                AudioDevice audioDevice = getDeviceInfo(device.Get());
                audioDevice.isDefault = (audioDevice.id == defaultId);
                devices.push_back(std::move(audioDevice));
            }
        }
        
        return devices;
    }
    
    bool selectDevice(const std::wstring& namePattern) override {
        auto devices = enumerateDevices();
        
        for (const auto& device : devices) {
            if (device.name.find(namePattern) != std::wstring::npos) {
                return selectDeviceById(device.id);
            }
        }
        
        MICMAP_LOG_WARNING("No device found matching pattern");
        return false;
    }
    
    bool selectDeviceById(const std::wstring& deviceId) override {
        if (!enumerator_) {
            return false;
        }
        
        stopCapture();
        
        ComPtr<IMMDevice> device;
        HRESULT hr = enumerator_->GetDevice(deviceId.c_str(), device.GetAddressOf());
        
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to get device: ", hr);
            return false;
        }
        
        currentDevice_ = device;
        currentDeviceInfo_ = getDeviceInfo(device.Get());
        deviceLost_ = false;
        
        // Convert wide string to narrow string for logging
        std::string deviceNameNarrow;
        deviceNameNarrow.reserve(currentDeviceInfo_.name.size());
        for (wchar_t wc : currentDeviceInfo_.name) {
            deviceNameNarrow.push_back(static_cast<char>(wc & 0x7F));
        }
        MICMAP_LOG_INFO("Selected audio device: ", deviceNameNarrow);
        
        return true;
    }
    
    bool startCapture() override {
        if (capturing_) {
            return true;
        }
        
        if (!currentDevice_) {
            MICMAP_LOG_ERROR("No device selected");
            return false;
        }
        
        if (deviceLost_) {
            MICMAP_LOG_ERROR("Device has been disconnected");
            return false;
        }
        
        // Activate audio client
        HRESULT hr = currentDevice_->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(audioClient_.GetAddressOf())
        );
        
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to activate audio client: ", hr);
            return false;
        }
        
        // Get mix format
        WAVEFORMATEX* format = nullptr;
        hr = audioClient_->GetMixFormat(&format);
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to get mix format: ", hr);
            return false;
        }
        
        // Store format information for conversion
        sampleRate_ = format->nSamplesPerSec;
        sourceChannels_ = static_cast<uint16_t>(format->nChannels);
        channels_ = 1;  // We always output mono
        bytesPerFrame_ = format->nBlockAlign;
        formatType_ = getFormatType(format);
        
        MICMAP_LOG_INFO("Audio format: ", format->nSamplesPerSec, " Hz, ", 
                        format->nChannels, " channels, ", 
                        format->wBitsPerSample, " bits");
        
        if (formatType_ == AudioFormatType::Unknown) {
            MICMAP_LOG_ERROR("Unsupported audio format");
            CoTaskMemFree(format);
            return false;
        }
        
        // Initialize in shared mode with event callback
        hr = audioClient_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            10000000,  // 1 second buffer (100ns units)
            0,         // Must be 0 for shared mode
            format,
            nullptr
        );
        
        CoTaskMemFree(format);
        
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to initialize audio client: ", hr);
            return false;
        }
        
        // Create event for audio data
        captureEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!captureEvent_) {
            MICMAP_LOG_ERROR("Failed to create capture event");
            return false;
        }
        
        hr = audioClient_->SetEventHandle(captureEvent_);
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to set event handle: ", hr);
            CloseHandle(captureEvent_);
            captureEvent_ = nullptr;
            return false;
        }
        
        // Get capture client
        hr = audioClient_->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(captureClient_.GetAddressOf())
        );
        
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to get capture client: ", hr);
            CloseHandle(captureEvent_);
            captureEvent_ = nullptr;
            return false;
        }
        
        // Start capture
        hr = audioClient_->Start();
        if (FAILED(hr)) {
            MICMAP_LOG_ERROR("Failed to start audio client: ", hr);
            CloseHandle(captureEvent_);
            captureEvent_ = nullptr;
            return false;
        }
        
        capturing_ = true;
        deviceLost_ = false;
        
        // Start capture thread
        captureThread_ = std::thread(&WASAPIAudioCapture::captureLoop, this);
        
        MICMAP_LOG_INFO("Audio capture started (output: mono float32)");
        return true;
    }
    
    void stopCapture() override {
        if (!capturing_) {
            return;
        }
        
        capturing_ = false;
        
        // Signal the event to wake up the capture thread
        if (captureEvent_) {
            SetEvent(captureEvent_);
        }
        
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        
        if (audioClient_) {
            audioClient_->Stop();
        }
        
        if (captureEvent_) {
            CloseHandle(captureEvent_);
            captureEvent_ = nullptr;
        }
        
        captureClient_.Reset();
        audioClient_.Reset();
        
        MICMAP_LOG_INFO("Audio capture stopped");
    }
    
    bool isCapturing() const override {
        return capturing_ && !deviceLost_;
    }
    
    bool getAudioBuffer(std::vector<float>& buffer) override {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        
        if (audioBuffer_.empty()) {
            return false;
        }
        
        buffer = std::move(audioBuffer_);
        audioBuffer_.clear();
        return true;
    }
    
    AudioDevice getCurrentDevice() const override {
        return currentDeviceInfo_;
    }
    
    void setAudioCallback(AudioCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        audioCallback_ = std::move(callback);
    }
    
    uint32_t getSampleRate() const override {
        return sampleRate_;
    }
    
    uint16_t getChannels() const override {
        return channels_;  // Always returns 1 (mono output)
    }
    
private:
    /**
     * @brief Handle device removal notification
     */
    void onDeviceRemoved(const std::wstring& deviceId) {
        if (currentDeviceInfo_.id == deviceId) {
            MICMAP_LOG_WARNING("Audio device disconnected");
            deviceLost_ = true;
        }
    }
    
    /**
     * @brief Main capture loop running in separate thread
     */
    void captureLoop() {
        // Initialize COM for this thread
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        
        while (capturing_ && !deviceLost_) {
            DWORD result = WaitForSingleObject(captureEvent_, 100);
            
            if (result == WAIT_OBJECT_0 && capturing_ && !deviceLost_) {
                processAudioData();
            }
        }
        
        CoUninitialize();
    }
    
    /**
     * @brief Process captured audio data with format conversion
     */
    void processAudioData() {
        UINT32 packetLength = 0;
        
        while (SUCCEEDED(captureClient_->GetNextPacketSize(&packetLength)) && packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            
            HRESULT hr = captureClient_->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                    deviceLost_ = true;
                    MICMAP_LOG_WARNING("Audio device invalidated during capture");
                }
                break;
            }
            
            // Convert to mono float samples
            std::vector<float> monoSamples(numFrames);
            
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // Fill with silence
                std::fill(monoSamples.begin(), monoSamples.end(), 0.0f);
            } else {
                // Convert based on format type
                convertToMonoFloat(data, numFrames, monoSamples);
            }
            
            // Store in buffer
            {
                std::lock_guard<std::mutex> lock(bufferMutex_);
                audioBuffer_.insert(audioBuffer_.end(), monoSamples.begin(), monoSamples.end());
                
                // Limit buffer size (keep last 1 second of mono samples)
                size_t maxSize = sampleRate_;
                if (audioBuffer_.size() > maxSize) {
                    audioBuffer_.erase(
                        audioBuffer_.begin(),
                        audioBuffer_.begin() + static_cast<ptrdiff_t>(audioBuffer_.size() - maxSize)
                    );
                }
            }
            
            // Call callback if set
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                if (audioCallback_) {
                    audioCallback_(monoSamples.data(), monoSamples.size());
                }
            }
            
            captureClient_->ReleaseBuffer(numFrames);
        }
    }
    
    /**
     * @brief Convert audio data to mono float format
     */
    void convertToMonoFloat(const BYTE* data, UINT32 numFrames, std::vector<float>& output) {
        switch (formatType_) {
            case AudioFormatType::Float32:
                convertFloat32ToMono(reinterpret_cast<const float*>(data), numFrames, output);
                break;
            case AudioFormatType::Int16:
                convertInt16ToMono(reinterpret_cast<const int16_t*>(data), numFrames, output);
                break;
            case AudioFormatType::Int24:
                convertInt24ToMono(data, numFrames, output);
                break;
            case AudioFormatType::Int32:
                convertInt32ToMono(reinterpret_cast<const int32_t*>(data), numFrames, output);
                break;
            default:
                std::fill(output.begin(), output.end(), 0.0f);
                break;
        }
    }
    
    /**
     * @brief Convert float32 multi-channel to mono
     */
    void convertFloat32ToMono(const float* data, UINT32 numFrames, std::vector<float>& output) {
        for (UINT32 i = 0; i < numFrames; ++i) {
            float sum = 0.0f;
            for (uint16_t ch = 0; ch < sourceChannels_; ++ch) {
                sum += data[i * sourceChannels_ + ch];
            }
            output[i] = sum / static_cast<float>(sourceChannels_);
        }
    }
    
    /**
     * @brief Convert int16 multi-channel to mono float
     */
    void convertInt16ToMono(const int16_t* data, UINT32 numFrames, std::vector<float>& output) {
        for (UINT32 i = 0; i < numFrames; ++i) {
            float sum = 0.0f;
            for (uint16_t ch = 0; ch < sourceChannels_; ++ch) {
                sum += int16ToFloat(data[i * sourceChannels_ + ch]);
            }
            output[i] = sum / static_cast<float>(sourceChannels_);
        }
    }
    
    /**
     * @brief Convert int24 multi-channel to mono float
     */
    void convertInt24ToMono(const BYTE* data, UINT32 numFrames, std::vector<float>& output) {
        const size_t bytesPerSample = 3;
        for (UINT32 i = 0; i < numFrames; ++i) {
            float sum = 0.0f;
            for (uint16_t ch = 0; ch < sourceChannels_; ++ch) {
                const BYTE* samplePtr = data + (i * sourceChannels_ + ch) * bytesPerSample;
                sum += int24ToFloat(samplePtr);
            }
            output[i] = sum / static_cast<float>(sourceChannels_);
        }
    }
    
    /**
     * @brief Convert int32 multi-channel to mono float
     */
    void convertInt32ToMono(const int32_t* data, UINT32 numFrames, std::vector<float>& output) {
        for (UINT32 i = 0; i < numFrames; ++i) {
            float sum = 0.0f;
            for (uint16_t ch = 0; ch < sourceChannels_; ++ch) {
                sum += int32ToFloat(data[i * sourceChannels_ + ch]);
            }
            output[i] = sum / static_cast<float>(sourceChannels_);
        }
    }
    
    /**
     * @brief Get device information from IMMDevice
     */
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
    
    // COM and device management
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> currentDevice_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    DeviceNotificationClient* notificationClient_;
    
    AudioDevice currentDeviceInfo_;
    
    // Capture state
    HANDLE captureEvent_ = nullptr;
    std::thread captureThread_;
    std::atomic<bool> capturing_;
    std::atomic<bool> deviceLost_;
    
    // Audio buffer
    std::vector<float> audioBuffer_;
    std::mutex bufferMutex_;
    
    // Callback
    AudioCallback audioCallback_;
    std::mutex callbackMutex_;
    
    // Format information
    uint32_t sampleRate_;
    uint16_t channels_;          // Output channels (always 1 for mono)
    uint16_t sourceChannels_;    // Source device channels
    uint16_t bytesPerFrame_;
    AudioFormatType formatType_;
    
    bool comInitialized_ = false;
};

std::unique_ptr<IAudioCapture> createWASAPICapture() {
    return std::make_unique<WASAPIAudioCapture>();
}

#else

// Stub implementation for non-Windows platforms
class StubAudioCapture : public IAudioCapture {
public:
    std::vector<AudioDevice> enumerateDevices() override { return {}; }
    bool selectDevice(const std::wstring&) override { return false; }
    bool selectDeviceById(const std::wstring&) override { return false; }
    bool startCapture() override { return false; }
    void stopCapture() override {}
    bool isCapturing() const override { return false; }
    bool getAudioBuffer(std::vector<float>&) override { return false; }
    AudioDevice getCurrentDevice() const override { return {}; }
    void setAudioCallback(AudioCallback) override {}
    uint32_t getSampleRate() const override { return 0; }
    uint16_t getChannels() const override { return 0; }
};

std::unique_ptr<IAudioCapture> createWASAPICapture() {
    return std::make_unique<StubAudioCapture>();
}

#endif

} // namespace micmap::audio
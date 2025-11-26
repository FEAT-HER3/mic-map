/**
 * @file audio_buffer.cpp
 * @brief Ring buffer implementation for audio samples
 */

#include "micmap/audio/audio_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace micmap::audio {

AudioBuffer::AudioBuffer(size_t capacity)
    : buffer_(capacity)
    , capacity_(capacity)
    , readPos_(0)
    , writePos_(0) {
}

AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
    : buffer_(std::move(other.buffer_))
    , capacity_(other.capacity_)
    , readPos_(other.readPos_.load())
    , writePos_(other.writePos_.load()) {
    other.capacity_ = 0;
    other.readPos_ = 0;
    other.writePos_ = 0;
}

AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept {
    if (this != &other) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_ = std::move(other.buffer_);
        capacity_ = other.capacity_;
        readPos_ = other.readPos_.load();
        writePos_ = other.writePos_.load();
        other.capacity_ = 0;
        other.readPos_ = 0;
        other.writePos_ = 0;
    }
    return *this;
}

size_t AudioBuffer::write(const float* samples, size_t count) {
    if (!samples || count == 0) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t availableSpace = space();
    size_t toWrite = std::min(count, availableSpace);
    
    if (toWrite == 0) {
        return 0;
    }
    
    size_t writeIdx = writePos_ % capacity_;
    size_t firstPart = std::min(toWrite, capacity_ - writeIdx);
    size_t secondPart = toWrite - firstPart;
    
    std::memcpy(buffer_.data() + writeIdx, samples, firstPart * sizeof(float));
    if (secondPart > 0) {
        std::memcpy(buffer_.data(), samples + firstPart, secondPart * sizeof(float));
    }
    
    writePos_ += toWrite;
    return toWrite;
}

size_t AudioBuffer::read(float* samples, size_t count) {
    if (!samples || count == 0) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t availableData = available();
    size_t toRead = std::min(count, availableData);
    
    if (toRead == 0) {
        return 0;
    }
    
    size_t readIdx = readPos_ % capacity_;
    size_t firstPart = std::min(toRead, capacity_ - readIdx);
    size_t secondPart = toRead - firstPart;
    
    std::memcpy(samples, buffer_.data() + readIdx, firstPart * sizeof(float));
    if (secondPart > 0) {
        std::memcpy(samples + firstPart, buffer_.data(), secondPart * sizeof(float));
    }
    
    readPos_ += toRead;
    return toRead;
}

size_t AudioBuffer::peek(float* samples, size_t count) const {
    if (!samples || count == 0) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t availableData = writePos_ - readPos_;
    size_t toPeek = std::min(count, availableData);
    
    if (toPeek == 0) {
        return 0;
    }
    
    size_t readIdx = readPos_ % capacity_;
    size_t firstPart = std::min(toPeek, capacity_ - readIdx);
    size_t secondPart = toPeek - firstPart;
    
    std::memcpy(samples, buffer_.data() + readIdx, firstPart * sizeof(float));
    if (secondPart > 0) {
        std::memcpy(samples + firstPart, buffer_.data(), secondPart * sizeof(float));
    }
    
    return toPeek;
}

size_t AudioBuffer::available() const {
    return writePos_ - readPos_;
}

size_t AudioBuffer::space() const {
    return capacity_ - available();
}

size_t AudioBuffer::capacity() const {
    return capacity_;
}

void AudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    readPos_ = 0;
    writePos_ = 0;
}

bool AudioBuffer::empty() const {
    return available() == 0;
}

bool AudioBuffer::full() const {
    return available() >= capacity_;
}

} // namespace micmap::audio
#pragma once

/**
 * @file audio_buffer.hpp
 * @brief Ring buffer for audio samples
 */

#include <vector>
#include <cstddef>
#include <mutex>
#include <atomic>

namespace micmap::audio {

/**
 * @brief Thread-safe ring buffer for audio samples
 */
class AudioBuffer {
public:
    /**
     * @brief Construct an audio buffer
     * @param capacity Maximum number of samples to store
     */
    explicit AudioBuffer(size_t capacity);
    
    ~AudioBuffer() = default;
    
    // Non-copyable
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
    
    // Movable
    AudioBuffer(AudioBuffer&&) noexcept;
    AudioBuffer& operator=(AudioBuffer&&) noexcept;
    
    /**
     * @brief Write samples to the buffer
     * @param samples Pointer to sample data
     * @param count Number of samples to write
     * @return Number of samples actually written
     */
    size_t write(const float* samples, size_t count);
    
    /**
     * @brief Read samples from the buffer
     * @param samples Pointer to output buffer
     * @param count Maximum number of samples to read
     * @return Number of samples actually read
     */
    size_t read(float* samples, size_t count);
    
    /**
     * @brief Peek at samples without removing them
     * @param samples Pointer to output buffer
     * @param count Maximum number of samples to peek
     * @return Number of samples actually peeked
     */
    size_t peek(float* samples, size_t count) const;
    
    /**
     * @brief Get the number of samples available for reading
     */
    size_t available() const;
    
    /**
     * @brief Get the remaining capacity for writing
     */
    size_t space() const;
    
    /**
     * @brief Get the total capacity of the buffer
     */
    size_t capacity() const;
    
    /**
     * @brief Clear all samples from the buffer
     */
    void clear();
    
    /**
     * @brief Check if the buffer is empty
     */
    bool empty() const;
    
    /**
     * @brief Check if the buffer is full
     */
    bool full() const;

private:
    std::vector<float> buffer_;
    size_t capacity_;
    std::atomic<size_t> readPos_;
    std::atomic<size_t> writePos_;
    mutable std::mutex mutex_;
};

} // namespace micmap::audio
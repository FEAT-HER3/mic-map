#pragma once

/**
 * @file types.hpp
 * @brief Common type definitions for MicMap
 */

#include <cstdint>
#include <string>
#include <chrono>
#include <functional>

namespace micmap::common {

/**
 * @brief Result type for operations that can fail
 */
enum class Result {
    Success,
    Error,
    NotInitialized,
    InvalidParameter,
    DeviceNotFound,
    Timeout,
    NotSupported
};

/**
 * @brief Convert Result to string for logging
 */
inline const char* resultToString(Result result) {
    switch (result) {
        case Result::Success: return "Success";
        case Result::Error: return "Error";
        case Result::NotInitialized: return "NotInitialized";
        case Result::InvalidParameter: return "InvalidParameter";
        case Result::DeviceNotFound: return "DeviceNotFound";
        case Result::Timeout: return "Timeout";
        case Result::NotSupported: return "NotSupported";
        default: return "Unknown";
    }
}

/**
 * @brief Audio sample type (normalized float -1.0 to 1.0)
 */
using AudioSample = float;

/**
 * @brief Timestamp type using steady clock
 */
using Timestamp = std::chrono::steady_clock::time_point;

/**
 * @brief Duration type in milliseconds
 */
using Duration = std::chrono::milliseconds;

/**
 * @brief Get current timestamp
 */
inline Timestamp now() {
    return std::chrono::steady_clock::now();
}

/**
 * @brief Calculate elapsed time in milliseconds
 */
inline Duration elapsed(Timestamp start) {
    return std::chrono::duration_cast<Duration>(now() - start);
}

} // namespace micmap::common
/**
 * @file driver_log.hpp
 * @brief Safe logging wrapper for OpenVR driver
 *
 * Provides a DriverLog function that safely handles logging before
 * the OpenVR driver context is initialized.
 */

#pragma once

#include <openvr_driver.h>
#include <cstdio>
#include <cstdarg>

namespace micmap::driver {

/**
 * @brief Safe driver logging function
 *
 * This function wraps OpenVR's DriverLog to handle cases where
 * logging is attempted before the driver context is initialized.
 * In such cases, it falls back to stderr output.
 */
inline void SafeDriverLog(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Try to use OpenVR's DriverLog if available
    // VRDriverLog() returns nullptr if context not initialized
    if (vr::VRDriverLog()) {
        vr::VRDriverLog()->Log(buffer);
    } else {
        // Fallback to stderr before context is initialized
        fprintf(stderr, "[MicMap Driver] %s", buffer);
    }
}

} // namespace micmap::driver

// Macro to replace DriverLog calls
#define DriverLog(...) micmap::driver::SafeDriverLog(__VA_ARGS__)
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

    // P6 Plan 06-02: VRDriverLog() itself dereferences VRDriverContext()
    // which is a default-null static pointer until VR_INIT_SERVER_DRIVER_CONTEXT
    // runs. Calling VRDriverLog() before that init crashes with an access
    // violation (the docstring's "safely handles logging before context init"
    // intent was not actually realised by the original implementation).
    // Guard on VRDriverContext() first so headless test environments — which
    // compile this header transitively but never call VR_INIT_SERVER_DRIVER_CONTEXT
    // — fall through cleanly to stderr instead of segfaulting.
    if (vr::VRDriverContext() != nullptr) {
        if (vr::VRDriverLog()) {
            vr::VRDriverLog()->Log(buffer);
            return;
        }
    }
    // Fallback to stderr before context is initialized
    fprintf(stderr, "[MicMap Driver] %s", buffer);
}

} // namespace micmap::driver

// Macro to replace DriverLog calls
#define DriverLog(...) micmap::driver::SafeDriverLog(__VA_ARGS__)
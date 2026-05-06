/**
 * @file driver_log_sink.cpp
 * @brief Implementation of DriverLogSink (LIB-04 / D-19).
 *
 * Adapts SafeDriverLog (driver/src/driver_log.hpp) to the ILogSink shape
 * so the same MultiSinkLogger fan-out used by the client also drives
 * vr::VRDriverLog()->Log() output into vrserver.txt. SafeDriverLog
 * already handles the pre-VR_INIT_SERVER_DRIVER_CONTEXT case by falling
 * through to stderr (driver_log.hpp:39 Rule-3 guard from P6).
 *
 * Driver-side composition root wiring (DriverLogSink + FileLogSink under
 * DeviceProvider::Init) lands in 08-02 — bundling here would force this
 * plan to also evolve device_provider.cpp ctor signature.
 */

#include "driver_log_sink.hpp"
#include "../driver_log.hpp"          // SafeDriverLog with Rule-3 guard from P6
#include "micmap/common/logger.hpp"   // logLevelToString

#include <memory>
#include <string>

namespace micmap::driver {

void DriverLogSink::log(micmap::common::LogLevel level, std::string_view message) {
    // SafeDriverLog already guards on vr::VRDriverContext() != nullptr
    // (driver_log.hpp:39) and falls back to stderr when context is
    // uninitialised. Add level prefix for vrserver.txt readability.
    std::string line = std::string("[") + micmap::common::logLevelToString(level)
                     + "] " + std::string(message) + "\n";
    SafeDriverLog("%s", line.c_str());
}

std::shared_ptr<micmap::common::ILogSink> makeDriverLogSink() {
    return std::make_shared<DriverLogSink>();
}

} // namespace

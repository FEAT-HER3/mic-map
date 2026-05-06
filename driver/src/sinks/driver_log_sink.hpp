/**
 * @file driver_log_sink.hpp
 * @brief LIB-04 driver-side log sink (P8 D-19). Wraps SafeDriverLog so
 *        driver log lines flow through vr::VRDriverLog() into vrserver.txt.
 *
 * Driver-only — depends on driver_log.hpp which includes <openvr_driver.h>.
 * Lives under driver/src/sinks/ NOT in micmap_core_runtime (LIB-03 lint
 * forbids OpenVR symbols in shared lib).
 */

#pragma once

#include "micmap/common/log_sink.hpp"

#include <memory>

namespace micmap::driver {

class DriverLogSink : public micmap::common::ILogSink {
public:
    void log(micmap::common::LogLevel level, std::string_view message) override;
};

std::shared_ptr<micmap::common::ILogSink> makeDriverLogSink();

} // namespace

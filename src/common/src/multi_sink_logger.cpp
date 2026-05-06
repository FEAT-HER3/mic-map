/**
 * @file multi_sink_logger.cpp
 * @brief LIB-04 fan-out logger composing 1+ ILogSink (P8 D-19 / D-21).
 *
 * Thread-safe log() under a single serializing mutex. Min-level filter
 * lives here (mirror of ConsoleLogger min-level pattern from
 * logger.cpp:24-26). Each sink locks its own internal mutex; the outer
 * mutex serializes the fan-out itself so a single log() call delivers
 * to all sinks atomically (no interleaving across sinks under burst load).
 *
 * Volume is bounded by the 5 Hz HEALTH-06 visible-cadence on the client
 * side and well under 1 kHz on the driver side (audio worker pushes
 * structured events at the audio frame cadence, ~50 Hz). Single-mutex
 * contention is non-issue at this scale per CONTEXT D-19.
 */

#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace micmap::common {

void MultiSinkLogger::log(LogLevel level, std::string_view message) {
    if (level < minLevel_) return;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& s : sinks_) {
        if (s) s->log(level, message);
    }
}

std::shared_ptr<ILogger> makeMultiSinkLogger(std::vector<std::shared_ptr<ILogSink>> sinks) {
    return std::make_shared<MultiSinkLogger>(std::move(sinks));
}

} // namespace

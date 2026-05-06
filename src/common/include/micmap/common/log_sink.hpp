#pragma once

/**
 * @file log_sink.hpp
 * @brief LIB-04 leaf log destination interface + MultiSinkLogger
 *        composition root (P8 D-19 / D-21 / Pitfall 10).
 *
 * Sinks unconditionally emit anything they receive. Min-level filtering is
 * the parent logger's concern (MultiSinkLogger). Multiple sinks can compose
 * via MultiSinkLogger to fan a single log() call out to driver-host /
 * file / stdout simultaneously without #ifdef branches inside
 * micmap_core_runtime (LIB-04 explicit).
 *
 * Composition root pattern: WinMain (client) and DeviceProvider::Init
 * (driver, lands in 08-02) construct the appropriate ILogSink list and
 * pass it to a MultiSinkLogger that is then registered with
 * micmap::common::Logger::setLogger BEFORE any application work begins.
 */

#include "micmap/common/logger.hpp"   // for ILogger, LogLevel + logLevelToString

#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace micmap::common {

/**
 * @brief Leaf log destination — emits one log line per call.
 *
 * Implementations MUST be thread-safe (each sink owns its own mutex; the
 * parent MultiSinkLogger does NOT lock around the per-sink fan-out beyond
 * its own serializing mutex). Implementations MUST NOT filter on min-level
 * — that is the parent logger's responsibility.
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void log(LogLevel level, std::string_view message) = 0;
};

// ---- factories for the concrete sinks (impls live in src/sinks/*.cpp) ----

/**
 * @brief Construct a FileLogSink that appends to @p path with per-line
 *        flush. Best-effort directory-create on the parent of @p path.
 */
std::shared_ptr<ILogSink> makeFileLogSink(std::filesystem::path path);

/**
 * @brief Construct a StdoutLogSink that writes to std::cerr (mirrors the
 *        existing ConsoleLogger output stream — stderr is the
 *        unbuffered Win32 console of choice).
 */
std::shared_ptr<ILogSink> makeStdoutLogSink();

/**
 * @brief Fan-out logger composing 1+ ILogSink instances under a single
 *        serializing mutex (Claude's-Discretion D-19; volume is bounded
 *        by the 5 Hz HEALTH-06 cadence so contention is non-issue).
 *
 * Min-level filtering happens here (mirrors ConsoleLogger min-level
 * pattern from logger.cpp); sinks themselves are unconditional emitters.
 *
 * Declared in the header (not just behind a factory) so test scaffolds
 * can stack-allocate a MultiSinkLogger directly without going through a
 * shared_ptr (see tests/test_multi_sink_logger.cpp).
 */
class MultiSinkLogger : public ILogger {
public:
    explicit MultiSinkLogger(std::vector<std::shared_ptr<ILogSink>> sinks)
        : sinks_(std::move(sinks)) {}

    void log(LogLevel level, std::string_view message) override;

    void setMinLevel(LogLevel level) override { minLevel_ = level; }
    LogLevel getMinLevel() const override { return minLevel_; }

private:
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    LogLevel minLevel_{LogLevel::Info};   // mirror ConsoleLogger ctor default
    mutable std::mutex mu_;
};

/**
 * @brief Convenience factory returning a MultiSinkLogger as ILogger via
 *        shared_ptr — the typical shape passed to Logger::setLogger.
 */
std::shared_ptr<ILogger> makeMultiSinkLogger(std::vector<std::shared_ptr<ILogSink>> sinks);

} // namespace micmap::common

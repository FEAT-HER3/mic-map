/**
 * @file stdout_log_sink.cpp
 * @brief LIB-04 stderr-stream sink (P8 D-19 / D-20). Mirrors ConsoleLogger
 *        body verbatim minus min-level filter (the parent MultiSinkLogger
 *        owns min-level per Pitfall 10).
 */

#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace micmap::common {

namespace { std::mutex stdoutLogMutex; }

class StdoutLogSink : public ILogSink {
public:
    void log(LogLevel level, std::string_view message) override {
        std::lock_guard<std::mutex> lock(stdoutLogMutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        std::cerr << "[" << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
                  << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
                  << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "."
                  << std::setfill('0') << std::setw(3) << ms.count()
                  << "] [" << logLevelToString(level) << "] "
                  << message << std::endl;
    }
};

std::shared_ptr<ILogSink> makeStdoutLogSink() {
    return std::make_shared<StdoutLogSink>();
}

} // namespace

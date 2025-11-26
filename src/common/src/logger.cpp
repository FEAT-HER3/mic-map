/**
 * @file logger.cpp
 * @brief Logger implementation
 */

#include "micmap/common/logger.hpp"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace micmap::common {

namespace {
    std::mutex logMutex;
}

// ConsoleLogger implementation
ConsoleLogger::ConsoleLogger()
    : minLevel_(LogLevel::Info) {
}

void ConsoleLogger::log(LogLevel level, std::string_view message) {
    if (level < minLevel_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(logMutex);
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Format: [HH:MM:SS.mmm] [LEVEL] message
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    
    std::cerr << "[" 
              << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
              << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
              << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "."
              << std::setfill('0') << std::setw(3) << ms.count()
              << "] [" << logLevelToString(level) << "] "
              << message << std::endl;
}

void ConsoleLogger::setMinLevel(LogLevel level) {
    minLevel_ = level;
}

LogLevel ConsoleLogger::getMinLevel() const {
    return minLevel_;
}

// Logger static implementation
std::shared_ptr<ILogger> Logger::logger_ = std::make_shared<ConsoleLogger>();

void Logger::setLogger(std::shared_ptr<ILogger> logger) {
    if (logger) {
        logger_ = logger;
    }
}

std::shared_ptr<ILogger> Logger::getLogger() {
    return logger_;
}

void Logger::log(LogLevel level, std::string_view message) {
    if (logger_) {
        logger_->log(level, message);
    }
}

} // namespace micmap::common
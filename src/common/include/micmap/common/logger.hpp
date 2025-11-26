#pragma once

/**
 * @file logger.hpp
 * @brief Logging utilities for MicMap
 */

#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <sstream>

namespace micmap::common {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/**
 * @brief Convert LogLevel to string
 */
inline const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Logger interface
 */
class ILogger {
public:
    virtual ~ILogger() = default;
    
    virtual void log(LogLevel level, std::string_view message) = 0;
    virtual void setMinLevel(LogLevel level) = 0;
    virtual LogLevel getMinLevel() const = 0;
};

/**
 * @brief Default console logger implementation
 */
class ConsoleLogger : public ILogger {
public:
    ConsoleLogger();
    ~ConsoleLogger() override = default;
    
    void log(LogLevel level, std::string_view message) override;
    void setMinLevel(LogLevel level) override;
    LogLevel getMinLevel() const override;
    
private:
    LogLevel minLevel_;
};

/**
 * @brief Global logger access
 */
class Logger {
public:
    static void setLogger(std::shared_ptr<ILogger> logger);
    static std::shared_ptr<ILogger> getLogger();
    
    static void log(LogLevel level, std::string_view message);
    
    template<typename... Args>
    static void trace(Args&&... args) {
        logFormatted(LogLevel::Trace, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void debug(Args&&... args) {
        logFormatted(LogLevel::Debug, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(Args&&... args) {
        logFormatted(LogLevel::Info, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warning(Args&&... args) {
        logFormatted(LogLevel::Warning, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(Args&&... args) {
        logFormatted(LogLevel::Error, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void fatal(Args&&... args) {
        logFormatted(LogLevel::Fatal, std::forward<Args>(args)...);
    }
    
private:
    template<typename... Args>
    static void logFormatted(LogLevel level, Args&&... args) {
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        log(level, oss.str());
    }
    
    static std::shared_ptr<ILogger> logger_;
};

// Convenience macros
#define MICMAP_LOG_TRACE(...) ::micmap::common::Logger::trace(__VA_ARGS__)
#define MICMAP_LOG_DEBUG(...) ::micmap::common::Logger::debug(__VA_ARGS__)
#define MICMAP_LOG_INFO(...) ::micmap::common::Logger::info(__VA_ARGS__)
#define MICMAP_LOG_WARNING(...) ::micmap::common::Logger::warning(__VA_ARGS__)
#define MICMAP_LOG_ERROR(...) ::micmap::common::Logger::error(__VA_ARGS__)
#define MICMAP_LOG_FATAL(...) ::micmap::common::Logger::fatal(__VA_ARGS__)

} // namespace micmap::common
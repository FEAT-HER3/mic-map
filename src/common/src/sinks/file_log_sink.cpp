/**
 * @file file_log_sink.cpp
 * @brief LIB-04 atomic-append file sink (P8 D-19 / D-20).
 *
 * Append-only. Per-line flush so a vrserver.exe / micmap.exe crash does
 * not lose the last log entry. No rotation in P8 — log-file rotation
 * (5 MB cap, 5 retained generations) is TEST-03 owned by Phase 10.
 * Best-effort directory creation in the ctor: %APPDATA%\\MicMap may not
 * exist on first run.
 */

#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <utility>

namespace micmap::common {

namespace { std::mutex fileLogMutex; }

class FileLogSink : public ILogSink {
public:
    explicit FileLogSink(std::filesystem::path path) : path_(std::move(path)) {
        std::error_code ec;
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path(), ec);
        }
        // Best-effort. If create fails (read-only %APPDATA% — extremely rare
        // on Windows), the first ofstream open below will silently no-op
        // because !f returns true; per-line flush is then a no-op.
    }

    void log(LogLevel level, std::string_view message) override {
        std::lock_guard<std::mutex> lock(fileLogMutex);
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
        std::ofstream f(path_, std::ios::app | std::ios::binary);
        if (!f) return;
        f << "[" << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
          << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
          << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "."
          << std::setfill('0') << std::setw(3) << ms.count()
          << "] [" << logLevelToString(level) << "] "
          << message << "\n";
        f.flush();
    }

private:
    std::filesystem::path path_;
};

// Factory exposed for composition root use.
std::shared_ptr<ILogSink> makeFileLogSink(std::filesystem::path path) {
    return std::make_shared<FileLogSink>(std::move(path));
}

} // namespace

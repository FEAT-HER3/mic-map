/**
 * @file file_log_sink.cpp
 * @brief LIB-04 atomic-append file sink (P8 D-19 / D-20) +
 *        TEST-03 5MB / 5-generation rotation (P10 D-14..D-17).
 *
 * Append-only. Per-line flush so a vrserver.exe / micmap.exe crash does
 * not lose the last log entry. Rotation: after each write, query
 * std::filesystem::file_size(); when >= 5MB, perform a synchronous
 * 5-generation rotation via serial MoveFileExW(MOVEFILE_REPLACE_EXISTING)
 * calls (oldest first), then continue logging to a fresh handle on the
 * next call. No rotation thread; the existing per-sink mutex
 * (fileLogMutex) plus the parent MultiSinkLogger mutex serialize writes
 * so rotate() executes alone. Failure mode: log a one-shot warning to
 * stderr (non-file sink to avoid recursion) and continue at oversize
 * — D-17 makes the bound soft, never lose log lines.
 *
 * Best-effort directory creation in the ctor: %APPDATA%\\MicMap may not
 * exist on first run.
 */

#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace micmap::common {

namespace {

// P10 D-14: 5 MB hard cap per active log; 5 retained generations (.1 .. .5).
constexpr std::uintmax_t kMaxLogBytes        = 5ull * 1024ull * 1024ull;
constexpr int            kRetainedGenerations = 5;

std::mutex fileLogMutex;

} // anonymous

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
        {
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
        // ofstream destroyed here — file handle closed BEFORE rotate() so
        // MoveFileExW can rename the active log on Windows (Win32 refuses
        // renames against an open handle on the source).

        // P10 D-14..D-17: synchronous rotation when size cap reached.
        // file_size on Windows is a cached MFT lookup (microseconds); cheap
        // enough to query on every write at the HEALTH-06 5 Hz cadence.
        std::error_code ec;
        const auto sz = std::filesystem::file_size(path_, ec);
        if (!ec && sz >= kMaxLogBytes) {
            rotate();
        }
    }

private:
    void rotate() noexcept;

    std::filesystem::path path_;
    bool warnedRotationFailure_ = false;
};

void FileLogSink::rotate() noexcept {
#ifdef _WIN32
    // Helper: build generation path.
    //   gen(0) -> active log path
    //   gen(N) -> path + ".N"
    auto gen = [&](int n) -> std::wstring {
        if (n == 0) return path_.wstring();
        std::wstring s = path_.wstring();
        s.append(L".");
        s.append(std::to_wstring(n));
        return s;
    };

    // Step A: drop the oldest retained generation if it exists.
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(gen(kRetainedGenerations)), ec);
        // ENOENT-equivalent is fine; any other error falls through to the
        // rotation chain below — MoveFileExW will surface the real failure.
    }

    // Step B: walk newest-to-oldest existing intermediate generations:
    //   .4 -> .5, .3 -> .4, .2 -> .3, .1 -> .2
    // (We just dropped .5; this preserves the chain and never overwrites
    // a generation the chain still needs.)
    for (int i = kRetainedGenerations - 1; i >= 1; --i) {
        const auto src = gen(i);
        const auto dst = gen(i + 1);
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(src), ec)) {
            continue;   // skip non-existent intermediate
        }
        if (!::MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            if (!warnedRotationFailure_) {
                warnedRotationFailure_ = true;
                std::fprintf(stderr,
                    "[micmap-log-rotation] MoveFileExW(%ls -> %ls) failed "
                    "(GetLastError=%lu); log will continue at oversize. "
                    "(one-shot warning)\n",
                    src.c_str(), dst.c_str(),
                    static_cast<unsigned long>(::GetLastError()));
            }
            return;   // bail out cleanly — bound is soft per D-17
        }
    }

    // Step C: rename the active log -> .1; subsequent log() calls reopen
    // the path via ofstream(app) which naturally handles the not-exists
    // case by creating a fresh file.
    const auto active = gen(0);
    const auto dotone = gen(1);
    if (!::MoveFileExW(active.c_str(), dotone.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (!warnedRotationFailure_) {
            warnedRotationFailure_ = true;
            std::fprintf(stderr,
                "[micmap-log-rotation] MoveFileExW(active -> .1) failed "
                "(GetLastError=%lu); log will continue at oversize. "
                "(one-shot warning)\n",
                static_cast<unsigned long>(::GetLastError()));
        }
    }
#endif // _WIN32
}

// Factory exposed for composition root use.
std::shared_ptr<ILogSink> makeFileLogSink(std::filesystem::path path) {
    return std::make_shared<FileLogSink>(std::move(path));
}

} // namespace

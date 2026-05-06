/**
 * @file test_multi_sink_logger.cpp
 * @brief Phase 8 Wave 0 RED scaffold for MultiSinkLogger fan-out (LIB-04 / D-19).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/test_command_queue.cpp / driver/audio_worker_lifecycle_headless.cpp.
 *
 * RED until Plan 08-01 lands src/common/include/micmap/common/log_sink.hpp +
 * src/common/src/multi_sink_logger.cpp. Build-time fail = Nyquist gate.
 */

#include "micmap/common/logger.hpp"
#include "micmap/common/log_sink.hpp"        // RED hook: lands in Plan 08-01

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mc = micmap::common;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace {

// Lambda-backed test sink that captures every log() call into a stringstream.
class CaptureSink : public mc::ILogSink {
public:
    explicit CaptureSink(std::stringstream& sink) : sink_(sink) {}
    void log(mc::LogLevel level, std::string_view message) override {
        sink_ << "[" << mc::logLevelToString(level) << "] " << message << "\n";
    }
private:
    std::stringstream& sink_;
};

} // namespace

int main() {
    std::stringstream a;
    std::stringstream b;
    std::vector<std::shared_ptr<mc::ILogSink>> sinks{
        std::make_shared<CaptureSink>(a),
        std::make_shared<CaptureSink>(b),
    };

    mc::MultiSinkLogger ml(std::move(sinks));
    ml.log(mc::LogLevel::Info, "hello");

    MM_CHECK(a.str().find("hello") != std::string::npos);
    MM_CHECK(b.str().find("hello") != std::string::npos);

    std::cout << "all tests passed\n";
    return 0;
}

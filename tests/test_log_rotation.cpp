/**
 * @file test_log_rotation.cpp
 * @brief Phase 10 Wave 0 RED scaffold for TEST-03 FileLogSink rotation
 *        (D-14..D-17): 5MB cap, 5 generations, atomic MoveFileExW.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / tests/test_settings_validator.cpp.
 *
 * Special note (PURE RUN-RED gate, not build-RED):
 *   This scaffold compiles + links cleanly at Wave 0 against the existing
 *   FileLogSink (post-P8 / src/common/src/sinks/file_log_sink.cpp). No
 *   new symbols are needed at compile time — the rotation behavior is an
 *   in-place change to FileLogSink::log() introduced in 10-01. At Wave 0
 *   this test BUILDS, RUNS, and FAILs assertions because the existing
 *   FileLogSink appends unbounded (no rotation). After 10-01 lands the
 *   rotate() helper inside FileLogSink::log(), the test PASSes. This is
 *   a deliberate departure from the build-time RED gate used by the
 *   other 3 Phase 10 Wave 0 scaffolds. Per CONTEXT D-26, the test is
 *   the RUN-RED gate that 10-01 turns GREEN.
 *
 * Test cases (CONTEXT D-14..D-17):
 *  (1) Setup: tmp dir + path.
 *  (2) Write small lines until size approaches 5MB; assert no rotation yet.
 *  (3) Force rotation: write more lines past 5MB; .1 generation appears.
 *  (4) Active log size after rotation < 5MB (rotation fires before exceeding cap).
 *  (5) 5-generation cap: drive 7 rotations; .1..5 exist, .6 does NOT.
 *  (6) Atomic move: no .tmp leftovers in tmp dir between rotations.
 *  (7) Cleanup: remove tmp dir.
 */

#include "micmap/common/log_sink.hpp"
#include "micmap/common/logger.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace common = micmap::common;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace {

// Write `count` lines of approximately `lineBytes` bytes through the sink.
// Returns 0 on success.
int writeLines(common::ILogSink& sink, std::size_t count, std::size_t lineBytes) {
    std::string payload(lineBytes, 'X');
    for (std::size_t i = 0; i < count; ++i) {
        sink.log(common::LogLevel::Info, payload);
    }
    return 0;
}

bool anyTmpInDir(const fs::path& dir) {
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (ec) return false;
        auto name = entry.path().filename().string();
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".tmp") {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    // (1) Setup tmp dir + path.
    auto tmp = fs::temp_directory_path() / "micmap_p10_logrot_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    MM_CHECK(!ec);
    auto path = tmp / "micmap-test.log";

    {
        auto sink = common::makeFileLogSink(path);
        MM_CHECK(sink != nullptr);

        // (2) Write enough to approach but not exceed 5MB.
        // Each call writes ~1KB payload + ~30 bytes of timestamp+level prefix +
        // newline; we drive ~1MB and assert no rotation yet (file_size grows but
        // .1 should NOT exist).
        constexpr std::size_t kLineBytes = 1024;
        constexpr std::size_t kLines1MB = 1024;   // ~1MB raw
        writeLines(*sink, kLines1MB, kLineBytes);

        auto sz = fs::file_size(path, ec);
        MM_CHECK(!ec);
        MM_CHECK(sz >= 900 * 1024);     // floor (some prefix overhead)
        MM_CHECK(sz < 5 * 1024 * 1024); // not yet rotated
        MM_CHECK(!fs::exists(path.string() + ".1", ec));

        // (3) Force rotation: keep writing past 5MB.
        // Drive ~6MB total to guarantee at least one rotation fires.
        constexpr std::size_t kLines5MB = 5 * 1024;
        writeLines(*sink, kLines5MB, kLineBytes);

        MM_CHECK(fs::exists(path, ec));               // active log reopened
        MM_CHECK(fs::exists(path.string() + ".1", ec)); // first rotation generation present

        // (4) Active log size after rotation strictly less than 5MB cap.
        auto active_sz = fs::file_size(path, ec);
        MM_CHECK(!ec);
        MM_CHECK(active_sz < 5 * 1024 * 1024);

        // (5) 5-generation cap: drive enough writes to produce 7 rotations total.
        // Each rotation is triggered by ~5MB of lines, so 6 more rotations need
        // ~30MB more. Use bigger lines to keep the test fast (4KB lines * 7700 ~=
        // 30MB).
        constexpr std::size_t kBigLineBytes = 4096;
        constexpr std::size_t kBigLines = 8000;
        writeLines(*sink, kBigLines, kBigLineBytes);

        // (6) Atomic move: no .tmp leftovers in tmp dir at this point.
        MM_CHECK(!anyTmpInDir(tmp));

        // .1..5 exist; .6 does NOT (oldest dropped on each rotation).
        MM_CHECK(fs::exists(path.string() + ".1", ec));
        MM_CHECK(fs::exists(path.string() + ".2", ec));
        MM_CHECK(fs::exists(path.string() + ".3", ec));
        MM_CHECK(fs::exists(path.string() + ".4", ec));
        MM_CHECK(fs::exists(path.string() + ".5", ec));
        MM_CHECK(!fs::exists(path.string() + ".6", ec));
    } // sink dtor closes any owned ofstream

    // (7) Cleanup tmp dir.
    fs::remove_all(tmp, ec);
    MM_CHECK(!ec);

    std::cout << "all tests passed\n";
    return 0;
}

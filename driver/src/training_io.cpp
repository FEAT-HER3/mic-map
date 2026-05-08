/**
 * @file training_io.cpp
 * @brief Implementation of driver-side training_data.bin I/O (P9 D-23 / D-27).
 *
 * Helper bodies (writeAtomicWindowsWithDetector / backupAndRotate /
 * makeCorruptedSuffix / getAppDataPath) mirror driver/src/config_io.cpp
 * (P8 D-10/D-14) verbatim except:
 *   - the corruption-backup literal is "training_data.bin.corrupted." rather
 *     than "config.json.corrupted.";
 *   - the atomic-write helper accepts an INoiseDetector& and asks it to
 *     serialize to the .tmp path itself (preserving v1.5 binary format
 *     byte-for-byte) instead of taking a UTF-8 string.
 *
 * Single-writer rule (IPC-06 / D-23): only the driver writes
 * training_data.bin. The cmake/AssertNoClientTraining lint enforces this
 * structurally once it goes live in 09-03.
 */

#include "training_io.hpp"
#include "micmap/common/logger.hpp"
#include "micmap/detection/noise_detector.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace micmap::driver {

namespace {

std::filesystem::path getAppDataPath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"MicMap";
    }
#endif
    return std::filesystem::current_path() / L".micmap";
}

std::string makeCorruptedSuffix() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
    return oss.str();
}

// [[maybe_unused]]: backupAndRotate is reserved for the load-side corruption
// handler (mirror of config_io.cpp where it fires from loadConfigJson on
// parse failure). 09-01 does not yet wire the load-side; defining the helper
// up front keeps the corruption-backup ring ready for 09-02 / future work
// without needing a re-edit. Without [[maybe_unused]] MSVC /W4 + /WX would
// reject this TU (C4505 unreferenced local function with internal linkage).
[[maybe_unused]] void backupAndRotate(const std::filesystem::path& trainingPath) {
    // Mirror of config_io.cpp:62-88 with the "training_data.bin.corrupted."
    // literal substituted in. 5-generation retention ring (sort + erase
    // beyond index 5).
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(trainingPath, ec) || ec) return;
    const auto backup = trainingPath.parent_path() /
        ("training_data.bin.corrupted." + makeCorruptedSuffix());
    fs::rename(trainingPath, backup, ec);
    if (ec) {
        MICMAP_LOG_ERROR("training_io: could not rename corrupt training_data to ",
                         backup.string(), ": ", ec.message());
        return;
    }
    MICMAP_LOG_WARNING("training_io: backed up corrupt training_data to: ",
                       backup.string());

    std::vector<fs::path> backups;
    std::error_code itEc;
    for (const auto& entry : fs::directory_iterator(trainingPath.parent_path(), itEc)) {
        if (itEc) break;
        const auto name = entry.path().filename().string();
        if (name.rfind("training_data.bin.corrupted.", 0) == 0) backups.push_back(entry.path());
    }
    std::sort(backups.begin(), backups.end(), std::greater<>());
    for (size_t i = 5; i < backups.size(); ++i) {
        std::error_code remEc;
        fs::remove(backups[i], remEc);
    }
}

#ifdef _WIN32
bool writeAtomicWindowsWithDetector(const std::filesystem::path& dest,
                                    micmap::detection::INoiseDetector& detector) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        MICMAP_LOG_ERROR("training_io: could not create training data directory: ",
                         ec.message());
        return false;
    }
    const auto tmp = dest.parent_path() / (dest.filename().wstring() + L".tmp");

    // 1) Ask detector to serialize binary profile to tmp via the v1.5 ofstream
    //    path at src/detection/src/noise_detector.cpp:329. Preserves binary
    //    format byte-for-byte (no re-implementation).
    if (!detector.saveTrainingData(tmp)) {
        MICMAP_LOG_ERROR("training_io: detector.saveTrainingData(tmp) failed for ",
                         tmp.string());
        std::error_code remEc;
        fs::remove(tmp, remEc);
        return false;
    }

    // 2) Atomic swap of tmp -> dest. ReplaceFileW preserves dest's ACLs and is
    //    atomic on NTFS; MoveFileExW is the fallback for the (rare) case where
    //    ReplaceFileW returns FALSE (e.g. dest on a non-NTFS filesystem).
    if (fs::exists(dest, ec)) {
        if (!ReplaceFileW(dest.c_str(), tmp.c_str(), nullptr,
                          REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS,
                          nullptr, nullptr)) {
            const DWORD err = ::GetLastError();
            MICMAP_LOG_ERROR("training_io: ReplaceFileW failed (GetLastError=", err, ")");
            // Fallback: MoveFileExW with MOVEFILE_REPLACE_EXISTING. Loses
            // atomicity (brief window where dest is gone before tmp lands)
            // but preserves data better than aborting the write entirely.
            if (!MoveFileExW(tmp.c_str(), dest.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const DWORD err2 = ::GetLastError();
                MICMAP_LOG_ERROR("training_io: MoveFileExW fallback also failed "
                                 "(GetLastError=", err2, ")");
                std::error_code remEc;
                fs::remove(tmp, remEc);
                return false;
            }
        }
    } else {
        // Fresh write — no atomic swap needed; just rename the tmp into place.
        if (!MoveFileExW(tmp.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD err = ::GetLastError();
            MICMAP_LOG_ERROR("training_io: MoveFileExW (fresh) failed "
                             "(GetLastError=", err, ")");
            std::error_code remEc;
            fs::remove(tmp, remEc);
            return false;
        }
    }

    // NOTE: do NOT call backupAndRotate(dest) here — that helper RENAMES dest
    // to "training_data.bin.corrupted.<timestamp>", which would destroy the
    // valid file we just wrote. backupAndRotate is reserved for future load-
    // time corruption handling (mirror of config_io.cpp where it is invoked
    // from loadConfigJson on parse failure, not from saveConfigJson).
    // Defining the helper here keeps the corruption-backup mechanism + the
    // 5-generation retention ring ready for the load-side corruption handler
    // that 09-02 / future work may add. Rule 1 deviation from 09-01-PLAN.md
    // Task 1 action which mistakenly called backupAndRotate on the save
    // success path.

    MICMAP_LOG_INFO("training_io: wrote training_data.bin to ", dest.string());
    return true;
}

#else
bool writeAtomicWindowsWithDetector(const std::filesystem::path&,
                                    micmap::detection::INoiseDetector&) {
    return false;
}
#endif

} // anonymous namespace

std::filesystem::path getDriverTrainingDataPath() {
    return getAppDataPath() / L"training_data.bin";
}

bool saveTrainingFile(const std::filesystem::path& path,
                      micmap::detection::INoiseDetector& detector) {
#ifdef _WIN32
    return writeAtomicWindowsWithDetector(path, detector);
#else
    (void)path; (void)detector;
    return false;
#endif
}

} // namespace micmap::driver

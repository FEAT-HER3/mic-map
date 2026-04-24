/**
 * @file test_bindings_patcher.cpp
 * @brief Unit tests for the Phase-4 lifted bindings_patcher shared library.
 *
 * Covers the six scenarios from 04-PATTERNS.md:
 *   1. Idempotency              — patch twice, second call no-op
 *   2. Write-once backup (D-08) — backup stays pristine across reinstalls
 *                                 (authoritative reference: driver/src/bindings_patcher.cpp:198)
 *   3. Unpatch-restore (D-11 primary)   — target == backup after unpatch
 *   4. Unpatch marker-erase (D-11 secondary) — no backup -> marker + keys erased
 *   5. Unpatch no-op            — unpatched target untouched
 *   6. AtomicWriteJson crash safety — leftover .micmap_tmp cleaned up on success
 *
 * No SteamVR runtime dependency — every scenario writes under
 * fs::temp_directory_path()/"micmap_test_bindings" and cleans up on entry.
 * argv[1] == "idempotent-only" runs scenario 1 alone (wired through ctest
 * as the bindings_patcher_idempotent target per 04-VALIDATION 04-01-03).
 */

#include "micmap/bindings/bindings_patcher.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace fs = std::filesystem;
using nlohmann::json;
using micmap::bindings::AtomicWriteJson;
using micmap::bindings::EnsureControllerTypeFiles;
using micmap::bindings::kMarkerKey;
using micmap::bindings::LogSink;
using micmap::bindings::PatchGenericHmdBindingsFile;
using micmap::bindings::UnpatchGenericHmdBindingsFile;

static LogSink noopLog = [](const char*){};

// IN-07: prior implementation used a fixed directory name
// ("micmap_test_bindings") which races under `ctest -jN` when
// test_bindings_patcher and bindings_patcher_idempotent (registered separately
// in tests/CMakeLists.txt) run concurrently — each resetTmpDir() call's
// remove_all would clobber the sibling test's in-progress scenario files.
// Append the PID so every concurrent process gets its own tmp tree.
static fs::path resetTmpDir() {
#ifdef _WIN32
    const unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    auto tmp = fs::temp_directory_path()
             / ("micmap_test_bindings_" + std::to_string(pid));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    return tmp;
}

// Valve's default empty-sources shape for generic_hmd (minimal, but
// structured enough that ApplyPatch won't crash and erase paths are testable).
static json valveOriginalShape() {
    return json{
        {"bindings", json::object()},
        {"controller_type", "generic_hmd"},
        {"name", "default generic_hmd bindings"}
    };
}

static void seedValveOriginal(const fs::path& configDir) {
    json j = valveOriginalShape();
    std::ofstream out(configDir / "vrcompositor_bindings_generic_hmd.json",
                      std::ios::trunc | std::ios::binary);
    out << j.dump(4);
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// --- Scenario 1: Idempotency --------------------------------------------
// Patch twice in a row on the same file. After the first patch, the file
// has the marker + bindings. The second patch must detect the marker and
// skip in place — file bytes unchanged.
static int runIdempotencyScenario() {
    auto tmp = resetTmpDir();
    seedValveOriginal(tmp);

    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    const auto target = tmp / "vrcompositor_bindings_generic_hmd.json";
    const std::string after1 = slurp(target);

    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    const std::string after2 = slurp(target);

    // D-08 idempotency: second patch is a no-op on a marker-stamped file.
    MM_CHECK(after1 == after2);

    // Sanity: the marker key is present on the top-level object.
    json j = json::parse(after2);
    MM_CHECK(j.contains(kMarkerKey));
    MM_CHECK(j[kMarkerKey].is_boolean());
    MM_CHECK(j[kMarkerKey].get<bool>() == true);

    std::cout << "PASS idempotency\n";
    return 0;
}

// --- Scenario 2: Write-once backup (D-08 / cpp:198) ---------------------
// Patch once — .micmap_backup captures the true original. Then corrupt the
// TARGET with junk, patch again — .micmap_backup must still reflect the
// pristine original, not the corrupted intermediate.
static int runWriteOnceBackupScenario() {
    auto tmp = resetTmpDir();
    seedValveOriginal(tmp);

    const auto target = tmp / "vrcompositor_bindings_generic_hmd.json";
    const auto backup = tmp / "vrcompositor_bindings_generic_hmd.json.micmap_backup";

    const std::string originalBytes = slurp(target);

    // First patch creates the backup from the pristine target.
    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    MM_CHECK(fs::exists(backup));
    const std::string backupAfterFirst = slurp(backup);
    MM_CHECK(backupAfterFirst == originalBytes);

    // Simulate a user / outside actor scribbling over the target between
    // patches (the write-once invariant protects against this consuming the
    // pristine state). Write after any marker-aware check would skip, so we
    // make it obvious "not-ours" by stripping our marker.
    {
        json corrupted = json::parse(slurp(target));
        corrupted.erase(kMarkerKey);
        corrupted["bindings"] = json::object();
        corrupted["user_scribbled"] = "yes";
        std::ofstream out(target, std::ios::trunc | std::ios::binary);
        out << corrupted.dump(4);
    }

    // Second patch — marker absent in target, so ApplyPatch runs again. The
    // critical check: because .micmap_backup already existed, the helper at
    // driver/src/bindings_patcher.cpp:198 must have SKIPPED re-backing-up
    // the corrupted intermediate, leaving the pristine original intact.
    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    const std::string backupAfterSecond = slurp(backup);
    MM_CHECK(backupAfterSecond == originalBytes);  // D-08 write-once invariant

    std::cout << "PASS write_once_backup\n";
    return 0;
}

// --- Scenario 3: Unpatch restore path (D-11 primary) --------------------
// Seed + patch so the backup exists, then unpatch. Target bytes must equal
// the original (backup) bytes byte-for-byte, AND the backup must STILL
// exist (D-08 write-once: we never consume it on restore).
static int runUnpatchRestoreScenario() {
    auto tmp = resetTmpDir();
    seedValveOriginal(tmp);

    const auto target = tmp / "vrcompositor_bindings_generic_hmd.json";
    const auto backup = tmp / "vrcompositor_bindings_generic_hmd.json.micmap_backup";
    const std::string originalBytes = slurp(target);

    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    MM_CHECK(fs::exists(backup));

    MM_CHECK(UnpatchGenericHmdBindingsFile(tmp, noopLog));
    MM_CHECK(slurp(target) == originalBytes);
    // D-08 write-once — backup must still be there after restore.
    MM_CHECK(fs::exists(backup));
    MM_CHECK(slurp(backup) == originalBytes);

    std::cout << "PASS unpatch_restore\n";
    return 0;
}

// --- Scenario 4: Unpatch marker-erase (D-11 secondary) ------------------
// Seed + patch + delete the backup. Unpatch must fall back to the
// marker-erase branch: target still parses as JSON but carries neither our
// marker nor MicMap-added action paths.
static int runUnpatchMarkerEraseScenario() {
    auto tmp = resetTmpDir();
    seedValveOriginal(tmp);

    const auto target = tmp / "vrcompositor_bindings_generic_hmd.json";
    const auto backup = tmp / "vrcompositor_bindings_generic_hmd.json.micmap_backup";

    MM_CHECK(PatchGenericHmdBindingsFile(tmp, noopLog));
    MM_CHECK(fs::exists(backup));

    std::error_code ec;
    fs::remove(backup, ec);
    MM_CHECK(!fs::exists(backup));

    MM_CHECK(UnpatchGenericHmdBindingsFile(tmp, noopLog));

    json j = json::parse(slurp(target));
    MM_CHECK(!j.contains(kMarkerKey));
    if (j.contains("bindings") && j["bindings"].is_object()) {
        MM_CHECK(!j["bindings"].contains("/actions/lasermouse"));
        MM_CHECK(!j["bindings"].contains("/actions/lasermouse_secondary"));
        MM_CHECK(!j["bindings"].contains("/actions/system"));
    }

    std::cout << "PASS unpatch_marker_erase\n";
    return 0;
}

// --- Scenario 5: Unpatch no-op -----------------------------------------
// Target has no MicMap marker — unpatch must succeed (D-11 skip contract)
// and leave the file untouched byte-for-byte.
static int runUnpatchNoOpScenario() {
    auto tmp = resetTmpDir();
    seedValveOriginal(tmp);

    const auto target = tmp / "vrcompositor_bindings_generic_hmd.json";
    const std::string originalBytes = slurp(target);

    MM_CHECK(UnpatchGenericHmdBindingsFile(tmp, noopLog));
    MM_CHECK(slurp(target) == originalBytes);

    std::cout << "PASS unpatch_noop\n";
    return 0;
}

// --- Scenario 6: AtomicWriteJson crash safety --------------------------
// Pre-populate a stale .micmap_tmp as if a prior AtomicWriteJson crashed
// mid-rename. A fresh successful AtomicWriteJson must leave only the
// target in place (the tmp is consumed by the rename).
static int runAtomicWriteJsonCrashSafetyScenario() {
    auto tmp = resetTmpDir();

    const auto target = tmp / "crashsafe_target.json";
    const auto staleTmp = fs::path(target).concat(".micmap_tmp");

    // Plant the stale tmp.
    {
        std::ofstream out(staleTmp, std::ios::trunc | std::ios::binary);
        out << "STALE_TMP_CONTENTS";
    }
    MM_CHECK(fs::exists(staleTmp));

    json j = {{"hello", "world"}, {"n", 42}};
    MM_CHECK(AtomicWriteJson(target, j, noopLog));

    MM_CHECK(fs::exists(target));
    MM_CHECK(!fs::exists(staleTmp));  // consumed by the rename

    json readBack = json::parse(slurp(target));
    MM_CHECK(readBack["hello"] == "world");
    MM_CHECK(readBack["n"].get<int>() == 42);

    std::cout << "PASS atomic_write_crash_safety\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "idempotent-only") == 0) {
        return runIdempotencyScenario();
    }
    if (int rc = runIdempotencyScenario();              rc != 0) return rc;
    if (int rc = runWriteOnceBackupScenario();          rc != 0) return rc;
    if (int rc = runUnpatchRestoreScenario();           rc != 0) return rc;
    if (int rc = runUnpatchMarkerEraseScenario();       rc != 0) return rc;
    if (int rc = runUnpatchNoOpScenario();              rc != 0) return rc;
    if (int rc = runAtomicWriteJsonCrashSafetyScenario(); rc != 0) return rc;
    std::cout << "ALL PASS\n";
    return 0;
}

/**
 * @file test_vrmanifest_schema.cpp
 * @brief Schema test for app.vrmanifest (AUTO-01, A2 LOCKED).
 *
 * Parses the generated `app.vrmanifest` (built next to micmap.exe by CMake
 * configure_file in Plan 03-02) and asserts the exact key/value contract
 * the SteamVR runtime requires for auto-launch:
 *
 *   - top-level "source"                   == "builtin"
 *   - applications[0].app_key              == "bigscreen.micmap"
 *   - applications[0].launch_type          == "binary"
 *   - applications[0].binary_path_windows  == "micmap.exe"
 *   - applications[0].is_dashboard_overlay == true
 *   - applications[0].arguments            == "--minimized" (string, A2-LOCKED)
 *
 * A2 RESOLUTION (Plan 03-02 Task 2, 2026-04-23): empirical SteamVR test on
 * Bigscreen Beyond + Windows 11 confirmed STRING form wins. SteamVR launched
 * `micmap.exe --minimized` from the string-form manifest. The 1-element
 * array fallback was deleted from the codebase. See:
 *   .planning/phases/03-auto-start/03-02-A2-RESULT.md
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail.
 */

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef MICMAP_MANIFEST_PATH
#error "MICMAP_MANIFEST_PATH must be defined by CMake target_compile_definitions"
#endif

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    namespace fs = std::filesystem;
    using nlohmann::json;

    const fs::path manifestPath = MICMAP_MANIFEST_PATH;
    if (!fs::exists(manifestPath)) {
        std::cerr << "FAIL: manifest file not found at " << manifestPath.string() << "\n";
        std::cerr << "      Plan 03-02 must emit app.vrmanifest beside micmap.exe.\n";
        return 1;
    }

    std::ifstream in(manifestPath);
    MM_CHECK(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();

    // Defensive parse — Phase 2 precedent: never throw on malformed JSON.
    json doc = json::parse(raw, /*cb=*/nullptr,
                           /*allow_exceptions=*/false,
                           /*ignore_comments=*/false);
    if (doc.is_discarded()) {
        std::cerr << "FAIL: app.vrmanifest is not valid JSON\n";
        return 1;
    }

    // Top-level "source" — required for SteamVR builtin manifests.
    MM_CHECK(doc.contains("source"));
    MM_CHECK(doc["source"].is_string());
    MM_CHECK(doc["source"].get<std::string>() == "builtin");

    // applications[0] block.
    MM_CHECK(doc.contains("applications"));
    MM_CHECK(doc["applications"].is_array());
    MM_CHECK(!doc["applications"].empty());
    const auto& app = doc["applications"][0];

    MM_CHECK(app.contains("app_key"));
    MM_CHECK(app["app_key"].get<std::string>() == "bigscreen.micmap");

    MM_CHECK(app.contains("launch_type"));
    MM_CHECK(app["launch_type"].get<std::string>() == "binary");

    MM_CHECK(app.contains("binary_path_windows"));
    MM_CHECK(app["binary_path_windows"].get<std::string>() == "micmap.exe");

    MM_CHECK(app.contains("is_dashboard_overlay"));
    MM_CHECK(app["is_dashboard_overlay"].is_boolean());
    MM_CHECK(app["is_dashboard_overlay"].get<bool>() == true);

    // arguments — A2 LOCKED to string form per empirical SteamVR test
    // (Plan 03-02 Task 2, 2026-04-23). Array form was rejected; only the
    // string form survives in the canonical template.
    MM_CHECK(app.contains("arguments"));
    MM_CHECK(app["arguments"].is_string());
    MM_CHECK(app["arguments"].get<std::string>() == "--minimized");

    std::cout << "PASS: arguments field locked to string form per A2\n";
    std::cout << "PASS: app.vrmanifest schema valid\n";
    return 0;
}

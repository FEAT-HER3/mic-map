/**
 * @file test_vrmanifest_schema.cpp
 * @brief RED schema test for app.vrmanifest (AUTO-01 + Open-item A2).
 *
 * Phase 3 Plan 01 Task 2 (Wave 0 test scaffold). Parses the generated
 * `app.vrmanifest` (built next to micmap.exe by CMake configure_file in
 * Plan 03-02) and asserts the exact key/value contract the SteamVR
 * runtime requires for auto-launch:
 *
 *   - top-level "source"                   == "builtin"
 *   - applications[0].app_key              == "bigscreen.micmap"
 *   - applications[0].launch_type          == "binary"
 *   - applications[0].binary_path_windows  == "micmap.exe"
 *   - applications[0].is_dashboard_overlay == true
 *   - applications[0].arguments            EITHER == "--minimized" (string)
 *                                          OR == ["--minimized"]   (1-elem array)
 *
 * The arguments-field branch encodes Open-item A2 (Plan 03 RESEARCH §A2):
 * SteamVR's accepted form is empirically verified during Wave 1; Plan 03-02
 * locks the working form into app.vrmanifest.in and tightens this test to
 * the single accepted shape.
 *
 * RED state: until Plan 03-02 lands the configure_file rule that emits
 * `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/<cfg>/app.vrmanifest`, this test
 * fails at runtime with "manifest file not found at <path>". CMake-side
 * `add_dependencies(test_vrmanifest_schema micmap)` enforces the build
 * ordering for downstream Plans.
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

    // arguments — Open-item A2: accept EITHER string OR 1-element array.
    MM_CHECK(app.contains("arguments"));
    if (app["arguments"].is_string()) {
        MM_CHECK(app["arguments"].get<std::string>() == "--minimized");
    } else if (app["arguments"].is_array()) {
        MM_CHECK(app["arguments"].size() == 1);
        MM_CHECK(app["arguments"][0].is_string());
        MM_CHECK(app["arguments"][0].get<std::string>() == "--minimized");
    } else {
        std::cerr << "FAIL: applications[0].arguments must be string or array\n";
        return 1;
    }

    std::cout << "PASS: app.vrmanifest schema valid\n";
    return 0;
}

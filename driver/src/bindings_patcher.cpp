/**
 * @file bindings_patcher.cpp
 * @brief See bindings_patcher.hpp.
 */

#include "bindings_patcher.hpp"
#include "driver_log.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace micmap::driver {

namespace {

constexpr const char* kMarkerKey = "micmap_patched_v2";
constexpr const char* kMarkerKeyV1 = "micmap_patched_v1";  // legacy -- force re-patch if present

// Resolve the SteamVR runtime install path via the user's openvrpaths.vrpath
// file (same mechanism vrpathreg.exe prints). Driver API (openvr_driver.h)
// does not expose VR_GetRuntimePath, so we parse this JSON ourselves.
fs::path ResolveSteamVrConfigDir() {
#ifdef _WIN32
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (!localAppData) {
        DriverLog("MicMap[patch]: LOCALAPPDATA not set; cannot resolve SteamVR path\n");
        return {};
    }
    fs::path pathsFile = fs::path(localAppData) / "openvr" / "openvrpaths.vrpath";
#else
    // Not expected to be exercised (driver is Windows-only), but keep the
    // filesystem code portable.
    const char* home = std::getenv("HOME");
    if (!home) return {};
    fs::path pathsFile = fs::path(home) / ".config" / "openvr" / "openvrpaths.vrpath";
#endif

    std::error_code ec;
    if (!fs::exists(pathsFile, ec)) {
        DriverLog("MicMap[patch]: openvrpaths.vrpath not found at %s\n",
                  pathsFile.string().c_str());
        return {};
    }

    try {
        std::ifstream in(pathsFile);
        json j;
        in >> j;
        if (!j.contains("runtime") || !j["runtime"].is_array() || j["runtime"].empty()) {
            DriverLog("MicMap[patch]: openvrpaths.vrpath has no runtime entries\n");
            return {};
        }
        const std::string runtime = j["runtime"][0].get<std::string>();
        return fs::path(runtime) / "resources" / "config";
    } catch (const std::exception& e) {
        DriverLog("MicMap[patch]: failed to parse openvrpaths.vrpath: %s\n", e.what());
        return {};
    }
}

bool AlreadyPatched(const json& j) {
    return j.contains(kMarkerKey)
        && j[kMarkerKey].is_boolean()
        && j[kMarkerKey].get<bool>();
}

// A file we wrote but with the older marker key. Still "ours" for upgrade.
bool OwnedByLegacyMicmap(const json& j) {
    return j.contains(kMarkerKeyV1)
        && j[kMarkerKeyV1].is_boolean()
        && j[kMarkerKeyV1].get<bool>();
}

// Mutates `j` in place so the result has:
//   /actions/lasermouse           pose + head/system click source
//   /actions/lasermouse_secondary head/system click source
//   /actions/system               complex_button single=opendashboard, double=toggleroomview
// Values match Valve Index's vrcompositor_bindings_indexhmd.json (this is
// the SteamVR-authored, dashboard-consumed binding shape).
// Forces the three action bindings onto `j["bindings"]`, regardless of
// existing content. Action output paths match vrcompositor_actions.json
// exactly (PascalCase — mandatory actions: ToggleDashboard, LeftClick,
// Pointer). Idempotency is enforced by the marker key on the enclosing
// object, NOT by checking field existence inline.
void ApplyPatch(json& j) {
    if (!j.contains("bindings") || !j["bindings"].is_object()) {
        j["bindings"] = json::object();
    }
    auto& bindings = j["bindings"];

    bindings["/actions/lasermouse"] = {
        {"poses", json::array({
            {
                {"output", "/actions/lasermouse/in/Pointer"},
                {"path",   "/user/head/pose/raw"}
            }
        })},
        {"sources", json::array({
            {
                {"inputs", {{"click", {{"output", "/actions/lasermouse/in/LeftClick"}}}}},
                {"mode",   "button"},
                {"path",   "/user/head/input/system"}
            }
        })}
    };

    bindings["/actions/lasermouse_secondary"] = {
        {"poses",   json::array()},
        {"sources", json::array({
            {
                {"inputs", {{"click", {{"output", "/actions/lasermouse_secondary/in/SwitchLaserHand"}}}}},
                {"mode",   "button"},
                {"path",   "/user/head/input/system"}
            }
        })}
    };

    bindings["/actions/system"] = {
        {"sources", json::array({
            {
                {"inputs", {
                    {"single", {{"output", "/actions/system/in/ToggleDashboard"}}},
                    {"double", {{"output", "/actions/system/in/ToggleRoomView"}}}
                }},
                {"mode", "complex_button"},
                {"path", "/user/head/input/system"}
            }
        })}
    };

    // Clear legacy marker and stamp current one.
    j.erase(kMarkerKeyV1);
    j[kMarkerKey] = true;
}

} // namespace

// Atomic write helper: stage as .micmap_tmp and rename over target.
bool AtomicWriteJson(const fs::path& target, const json& j) {
    fs::path tmp = target;
    tmp += ".micmap_tmp";
    std::error_code ec;
    try {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        out << j.dump(4);
        out.close();
    } catch (const std::exception& e) {
        DriverLog("MicMap[patch]: tmp write failed for %s: %s\n",
                  target.string().c_str(), e.what());
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        DriverLog("MicMap[patch]: rename to %s failed: %s\n",
                  target.string().c_str(), ec.message().c_str());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// In-place patch of the already-existing generic_hmd compositor bindings.
bool PatchGenericHmdBindingsFile(const fs::path& configDir) {
    fs::path target = configDir / "vrcompositor_bindings_generic_hmd.json";
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        DriverLog("MicMap[patch]: generic_hmd bindings missing at %s\n",
                  target.string().c_str());
        return false;
    }

    json original;
    try {
        std::ifstream in(target);
        in >> original;
    } catch (const std::exception& e) {
        DriverLog("MicMap[patch]: failed to parse %s: %s\n",
                  target.string().c_str(), e.what());
        return false;
    }

    if (AlreadyPatched(original)) {
        DriverLog("MicMap[patch]: generic_hmd bindings already patched\n");
        return true;
    }

    // One-shot backup so the installer's uninstall path can restore it.
    fs::path backup = target;
    backup += ".micmap_backup";
    if (!fs::exists(backup, ec)) {
        fs::copy_file(target, backup, ec);
        if (ec) {
            DriverLog("MicMap[patch]: backup write failed: %s\n", ec.message().c_str());
        } else {
            DriverLog("MicMap[patch]: backed up original to %s\n",
                      backup.string().c_str());
        }
    }

    ApplyPatch(original);
    if (!AtomicWriteJson(target, original)) return false;

    DriverLog("MicMap[patch]: patched %s\n", target.string().c_str());
    return true;
}

// Build a stand-alone controller-type-specific compositor bindings file
// matching Valve Index's shape (lasermouse + lasermouse_secondary + system).
json BuildControllerTypeBindings(const std::string& controllerType) {
    return {
        {"action_manifest_version", 0},
        {"alias_info", json::object()},
        {"app_key", "openvr.component.vrcompositor"},
        {"bindings", {
            {"/actions/lasermouse", {
                {"poses", json::array({
                    {{"output", "/actions/lasermouse/in/Pointer"},
                     {"path",   "/user/head/pose/raw"}}
                })},
                {"sources", json::array({
                    {{"inputs", {{"click", {{"output", "/actions/lasermouse/in/LeftClick"}}}}},
                     {"mode",   "button"},
                     {"path",   "/user/head/input/system"}}
                })}
            }},
            {"/actions/lasermouse_secondary", {
                {"poses", json::array()},
                {"sources", json::array({
                    {{"inputs", {{"click", {{"output", "/actions/lasermouse_secondary/in/SwitchLaserHand"}}}}},
                     {"mode",   "button"},
                     {"path",   "/user/head/input/system"}}
                })}
            }},
            {"/actions/system", {
                {"sources", json::array({
                    {{"inputs", {
                         {"single", {{"output", "/actions/system/in/ToggleDashboard"}}},
                         {"double", {{"output", "/actions/system/in/ToggleRoomView"}}}
                     }},
                     {"mode", "complex_button"},
                     {"path", "/user/head/input/system"}}
                })}
            }}
        }},
        {"category", "steamvr_input"},
        {"controller_type", controllerType},
        {"description", ""},
        {"name", "MicMap HMD dashboard bindings (" + controllerType + ")"},
        {"options", json::object()},
        {"simulated_actions", json::array()},
        {kMarkerKey, true}
    };
}

// Device-side input profile declaring that this HMD exposes /input/system
// (button) and /pose/raw. Mirrors indexhmd_profile.json structure.
json BuildControllerTypeProfile(const std::string& controllerType) {
    return {
        {"jsonid", "input_profile"},
        {"controller_type", controllerType},
        {"input_bindingui_mode", "hmd"},
        {"input_source", {
            {"/input/system", {
                {"type", "button"},
                {"order", 1}
            }},
            {"/pose/raw", {
                {"type", "pose"}
            }}
        }},
        {"default_bindings", json::array({
            {{"app_key", "openvr.component.vrcompositor"},
             {"binding_url", "vrcompositor_bindings_" + controllerType + ".json"}}
        })},
        {kMarkerKey, true}
    };
}

// Write-if-missing helper for a controller-type file pair in configDir.
// Never overwrites a non-micmap file; if file exists without our marker,
// assume Valve shipped one (or a third-party patched first) and skip.
bool EnsureControllerTypeFiles(const fs::path& configDir,
                               const std::string& controllerType) {
    std::error_code ec;
    bool anyWritten = false;

    const fs::path bindingsPath = configDir / ("vrcompositor_bindings_" + controllerType + ".json");
    const fs::path profilePath  = configDir / (controllerType + "_profile.json");

    // Bindings file
    if (!fs::exists(bindingsPath, ec)) {
        if (AtomicWriteJson(bindingsPath, BuildControllerTypeBindings(controllerType))) {
            DriverLog("MicMap[patch]: wrote %s (dashboard+lasermouse for %s)\n",
                      bindingsPath.string().c_str(), controllerType.c_str());
            anyWritten = true;
        }
    } else {
        // Replace a file only if marker identifies it as ours (current or
        // legacy). Leave non-micmap files alone.
        json existing;
        bool mine = false;
        bool currentMarker = false;
        try {
            std::ifstream in(bindingsPath);
            in >> existing;
            currentMarker = AlreadyPatched(existing);
            mine = currentMarker || OwnedByLegacyMicmap(existing);
        } catch (...) {
            mine = false;
        }
        if (mine && !currentMarker) {
            if (AtomicWriteJson(bindingsPath, BuildControllerTypeBindings(controllerType))) {
                DriverLog("MicMap[patch]: upgraded %s to current marker\n",
                          bindingsPath.string().c_str());
                anyWritten = true;
            }
        } else if (currentMarker) {
            DriverLog("MicMap[patch]: %s already current\n",
                      bindingsPath.string().c_str());
        } else {
            DriverLog("MicMap[patch]: %s exists and is not ours -- leaving alone\n",
                      bindingsPath.string().c_str());
        }
    }

    // Profile file
    if (!fs::exists(profilePath, ec)) {
        if (AtomicWriteJson(profilePath, BuildControllerTypeProfile(controllerType))) {
            DriverLog("MicMap[patch]: wrote %s (input profile for %s)\n",
                      profilePath.string().c_str(), controllerType.c_str());
            anyWritten = true;
        }
    } else {
        json existing;
        bool mine = false;
        bool currentMarker = false;
        try {
            std::ifstream in(profilePath);
            in >> existing;
            currentMarker = AlreadyPatched(existing);
            mine = currentMarker || OwnedByLegacyMicmap(existing);
        } catch (...) {
            mine = false;
        }
        if (mine && !currentMarker) {
            if (AtomicWriteJson(profilePath, BuildControllerTypeProfile(controllerType))) {
                DriverLog("MicMap[patch]: upgraded %s to current marker\n",
                          profilePath.string().c_str());
                anyWritten = true;
            }
        } else if (currentMarker) {
            DriverLog("MicMap[patch]: %s already current\n",
                      profilePath.string().c_str());
        } else {
            DriverLog("MicMap[patch]: %s exists and is not ours -- leaving alone\n",
                      profilePath.string().c_str());
        }
    }

    return anyWritten;
}

bool PatchGenericHmdBindings() {
    fs::path configDir = ResolveSteamVrConfigDir();
    if (configDir.empty()) return false;

    std::error_code ec;
    if (!fs::exists(configDir, ec)) {
        DriverLog("MicMap[patch]: SteamVR config dir missing at %s\n",
                  configDir.string().c_str());
        return false;
    }

    bool ok = true;
    // 1. Patch the existing generic_hmd bindings (Valve ships this one).
    ok &= PatchGenericHmdBindingsFile(configDir);

    // 2. Drop controller-type-specific files for non-Index lighthouse HMDs
    //    (Bigscreen Beyond, Vive, HP Reverb variants routed as lighthouse_hmd).
    //    SteamVR's binding resolution tries these BEFORE falling back to
    //    generic_hmd; if present, they win.
    ok &= EnsureControllerTypeFiles(configDir, "lighthouse_hmd");

    return ok;
}

} // namespace micmap::driver

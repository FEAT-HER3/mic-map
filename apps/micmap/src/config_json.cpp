/**
 * @file config_json.cpp (client TU)
 * @brief Phase 8 D-03: AppConfig nlohmann/json ADL hooks compiled into
 *        micmap.exe. Mirrored verbatim from driver/src/config_json.cpp
 *        (RESEARCH Open Q5 option (c) - duplication keeps
 *        cmake/AssertNoJsonInCore lint scope simple, avoids exception lists).
 *        Schema source-of-truth = src/core/include/micmap/core/config_manager.hpp;
 *        wire format identical to v1.5 client-written config.json on disk.
 */

#include "micmap/core/config_manager.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <ctime>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace micmap::core {

// UTF-8 wstring helpers (lifted from src/core/src/config_manager.cpp:51-90).
namespace {
std::string toUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out((size_t)needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), needed, nullptr, nullptr);
    return out;
#else
    std::string out; out.reserve(w.size());
    for (wchar_t c : w) if (c < 128) out.push_back((char)c);
    return out;
#endif
}
bool fromUtf8(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (needed <= 0) return false;
    out.resize((size_t)needed);
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), out.data(), needed);
    return written == needed;
#else
    out.assign(s.begin(), s.end());
    return true;
#endif
}
} // anonymous

void to_json(nlohmann::json& j, const AudioConfig& c) {
    j["deviceNamePattern"] = toUtf8(c.deviceNamePattern);
    j["deviceId"]          = c.deviceId.empty() ? nlohmann::json(nullptr) : nlohmann::json(toUtf8(c.deviceId));
    j["bufferSizeMs"]      = c.bufferSizeMs;
}

void from_json(const nlohmann::json& j, AudioConfig& c) {
    if (j.contains("deviceNamePattern") && j["deviceNamePattern"].is_string()) {
        std::wstring w; if (fromUtf8(j["deviceNamePattern"].get<std::string>(), w)) c.deviceNamePattern = std::move(w);
    }
    if (j.contains("deviceId") && j["deviceId"].is_string()) {
        std::wstring w; if (fromUtf8(j["deviceId"].get<std::string>(), w)) c.deviceId = std::move(w);
    }
    if (j.contains("bufferSizeMs") && j["bufferSizeMs"].is_number_integer()) {
        c.bufferSizeMs = j["bufferSizeMs"].get<int>();
    }
}

void to_json(nlohmann::json& j, const DetectionConfig& c) {
    j["sensitivity"]   = c.sensitivity;
    j["minDurationMs"] = c.minDurationMs;
    j["cooldownMs"]    = c.cooldownMs;
    j["fftSize"]       = c.fftSize;
}
void from_json(const nlohmann::json& j, DetectionConfig& c) {
    if (j.contains("sensitivity")   && j["sensitivity"].is_number())          c.sensitivity   = j["sensitivity"].get<float>();
    if (j.contains("minDurationMs") && j["minDurationMs"].is_number_integer()) c.minDurationMs = j["minDurationMs"].get<int>();
    if (j.contains("cooldownMs")    && j["cooldownMs"].is_number_integer())    c.cooldownMs    = j["cooldownMs"].get<int>();
    if (j.contains("fftSize")       && j["fftSize"].is_number_integer())       c.fftSize       = j["fftSize"].get<int>();
}

void to_json(nlohmann::json& j, const SteamVRConfig& c) {
    j["dashboardClickEnabled"] = c.dashboardClickEnabled;
    j["customActionBinding"]   = c.customActionBinding.empty() ? nlohmann::json(nullptr) : nlohmann::json(c.customActionBinding);
}
void from_json(const nlohmann::json& j, SteamVRConfig& c) {
    if (j.contains("dashboardClickEnabled") && j["dashboardClickEnabled"].is_boolean()) c.dashboardClickEnabled = j["dashboardClickEnabled"].get<bool>();
    if (j.contains("customActionBinding") && j["customActionBinding"].is_string())     c.customActionBinding = j["customActionBinding"].get<std::string>();
}

void to_json(nlohmann::json& j, const TrainingConfig& c) {
    j["dataFile"] = c.dataFile;
    if (c.lastTrainedTimestamp.has_value()) {
        j["lastTrainedTimestamp"] = static_cast<std::int64_t>(
            std::chrono::system_clock::to_time_t(*c.lastTrainedTimestamp));
    } else {
        j["lastTrainedTimestamp"] = nullptr;
    }
}
void from_json(const nlohmann::json& j, TrainingConfig& c) {
    if (j.contains("dataFile") && j["dataFile"].is_string()) c.dataFile = j["dataFile"].get<std::string>();
    if (j.contains("lastTrainedTimestamp")) {
        const auto& t = j["lastTrainedTimestamp"];
        if (t.is_null()) c.lastTrainedTimestamp = std::nullopt;
        else if (t.is_number_integer()) {
            c.lastTrainedTimestamp = std::chrono::system_clock::from_time_t(
                static_cast<std::time_t>(t.get<std::int64_t>()));
        }
    }
}

void to_json(nlohmann::json& j, const AppConfig& c) {
    j["version"]   = c.version;
    j["audio"]     = c.audio;
    j["detection"] = c.detection;
    j["steamvr"]   = c.steamvr;
    j["training"]  = c.training;
    j["shownTrayNotification"] = c.shownTrayNotification;
}
void from_json(const nlohmann::json& j, AppConfig& c) {
    if (j.contains("version") && j["version"].is_number_integer()) c.version = j["version"].get<int>();
    if (j.contains("audio")     && j["audio"].is_object())     j["audio"].get_to(c.audio);
    if (j.contains("detection") && j["detection"].is_object()) j["detection"].get_to(c.detection);
    if (j.contains("steamvr")   && j["steamvr"].is_object())   j["steamvr"].get_to(c.steamvr);
    if (j.contains("training")  && j["training"].is_object()) j["training"].get_to(c.training);
    if (j.contains("shownTrayNotification") && j["shownTrayNotification"].is_boolean()) c.shownTrayNotification = j["shownTrayNotification"].get<bool>();
}

} // namespace

/**
 * @file settings_validator.cpp
 * @brief Implementation of validateSettings (P8 D-14 / D-15).
 *
 * Ranges mirror v1.5 src/core/src/config_manager.cpp readAudio/readDetection
 * clamps -- NOT silently clamped here, but rejected. First-failed-field
 * early-return; no multi-error aggregation in P8.
 */

#include "settings_validator.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace micmap::driver {

namespace {

bool isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(v));
    return std::string(buf);
}

std::string fmtInt(int v) { return std::to_string(v); }

} // anonymous namespace

std::optional<ValidationError> validateSettings(const core::AppConfig& cfg) {
    // version: must equal 1 in P8 (single supported wire-format version per D-04).
    if (cfg.version != 1) {
        return ValidationError{"version", "must equal 1; got " + fmtInt(cfg.version)};
    }

    // audio.deviceNamePattern: must be non-empty (else AudioWorker has nothing to match).
    if (cfg.audio.deviceNamePattern.empty()) {
        return ValidationError{"audio.deviceNamePattern", "must not be empty"};
    }

    // audio.deviceId: optional -- empty is valid (no check).

    // audio.bufferSizeMs: [5, 100] per v1.5 readAudio clamp.
    if (cfg.audio.bufferSizeMs < 5 || cfg.audio.bufferSizeMs > 100) {
        return ValidationError{"audio.bufferSizeMs",
            "must be in [5, 100]; got " + fmtInt(cfg.audio.bufferSizeMs)};
    }

    // detection.sensitivity: [0.0, 1.0] per v1.5 readDetection clamp.
    if (!std::isfinite(cfg.detection.sensitivity)
            || cfg.detection.sensitivity < 0.0f
            || cfg.detection.sensitivity > 1.0f) {
        return ValidationError{"detection.sensitivity",
            "must be in [0.0, 1.0]; got " + fmtFloat(cfg.detection.sensitivity)};
    }

    // detection.minDurationMs: [100, 2000] per v1.5.
    if (cfg.detection.minDurationMs < 100 || cfg.detection.minDurationMs > 2000) {
        return ValidationError{"detection.minDurationMs",
            "must be in [100, 2000]; got " + fmtInt(cfg.detection.minDurationMs)};
    }

    // detection.cooldownMs: [100, 2000] per v1.5.
    if (cfg.detection.cooldownMs < 100 || cfg.detection.cooldownMs > 2000) {
        return ValidationError{"detection.cooldownMs",
            "must be in [100, 2000]; got " + fmtInt(cfg.detection.cooldownMs)};
    }

    // detection.fftSize: power-of-2, [512, 8192] per v1.5 snapPowerOfTwo.
    if (cfg.detection.fftSize < 512 || cfg.detection.fftSize > 8192) {
        return ValidationError{"detection.fftSize",
            "must be in [512, 8192]; got " + fmtInt(cfg.detection.fftSize)};
    }
    if (!isPowerOfTwo(cfg.detection.fftSize)) {
        return ValidationError{"detection.fftSize",
            "must be a power of 2; got " + fmtInt(cfg.detection.fftSize)};
    }

    // steamvr.dashboardClickEnabled: bool -- any value is valid.
    // steamvr.customActionBinding: any string (or empty) -- no range check.

    // training.dataFile: must be non-empty.
    if (cfg.training.dataFile.empty()) {
        return ValidationError{"training.dataFile", "must not be empty"};
    }

    // training.lastTrainedTimestamp: optional -- nullopt or any time_point is valid.
    // shownTrayNotification: bool -- any value is valid.

    return std::nullopt;
}

} // namespace micmap::driver

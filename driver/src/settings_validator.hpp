/**
 * @file settings_validator.hpp
 * @brief Phase 8 D-14 / D-15: PUT /settings validation.
 *
 * Pre-persist validation: rejects out-of-range / wrong-type fields BEFORE
 * any clamp (Pitfall 1 mitigation -- silent clamp lets the wrong value land
 * in the snapshot; we want explicit rejection so the client can surface
 * "Invalid {field}: {reason}" UX).
 *
 * First-failed-field reporting (D-14 / D-15). Multi-error aggregation
 * explicitly NOT in P8 -- adds shape complexity without UX value.
 *
 * Field-path style: dot-paths (`detection.sensitivity`) match struct
 * nesting more naturally than JSON-Pointer. Documented in CONTEXT
 * Claude's Discretion.
 *
 * Validation library is hand-rolled (no JSON Schema lib pulled in).
 * AppConfig is small (<= 30 fields); per-field validators live as flat
 * free functions in the .cpp.
 */

#pragma once

#include "micmap/core/config_manager.hpp"

#include <optional>
#include <string>

namespace micmap::driver {

struct ValidationError {
    std::string field;    ///< Dot-path, e.g. "detection.sensitivity" or "audio.bufferSizeMs"
    std::string reason;   ///< Human-readable; safe to surface in client UI
};

/// @brief Returns nullopt if all fields valid; else first-failed-field error.
///        Always runs to completion within bounded work (no I/O, no syscalls).
std::optional<ValidationError> validateSettings(const core::AppConfig& cfg);

} // namespace micmap::driver

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

#include <nlohmann/json.hpp>   // P9 09-02: training payload validators consume json bodies

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

// ---- P9 training payload validators (09-02) ----
// All return std::nullopt on success; first-failed-field {field, reason} envelope
// on rejection (P8 D-14 inheritance). No partial state mutation on rejection.

/// @brief P9 09-02 / D-15 / D-16: parsed body of POST /training/finalize.
///        Schema:
///          - {"confirm": true}                                       — accept the preview
///          - {"confirm": false, "sensitivity": s}                    — explicit sensitivity override
///          - {"confirm": false, "sensitivity": s, "threshold": t}    — explicit thresholds
///          - {"confirm": true,  "sensitivity": s, "threshold": t}    — confirm + explicit overrides
///        Reject any other shape including missing "confirm" key, unknown fields.
struct FinalizePayload {
    bool                  confirm{false};
    std::optional<float>  sensitivity;
    std::optional<float>  threshold;
};

/// @brief Parse + validate the JSON body of POST /training/finalize.
///        On success populates `out`; on failure returns first-failed-field
///        {field, reason}. Strict-shape: unknown fields rejected.
std::optional<ValidationError> validateFinalizePayload(const nlohmann::json& body,
                                                       FinalizePayload& out);

/// @brief Parse + validate the JSON body of POST /training/recompute.
///        Schema: {"sensitivity": float} — single required field;
///        range [0.0, 1.0]; std::isfinite required. Strict-shape: unknown
///        fields rejected. On success populates `outSensitivity`.
std::optional<ValidationError> validateRecomputePayload(const nlohmann::json& body,
                                                        float& outSensitivity);

/// @brief Strict empty-body policy for POST /training/start + POST /training/cancel:
///        accept "" or "{}"; reject anything else. T-09-02-10 mitigation
///        (no extra fields permitted on no-body endpoints).
std::optional<ValidationError> validateEmptyOrEmptyObjectBody(const std::string& raw);

} // namespace micmap::driver

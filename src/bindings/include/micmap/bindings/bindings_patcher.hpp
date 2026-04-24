/**
 * @file bindings_patcher.hpp
 * @brief Patches SteamVR's system-level generic-HMD vrcompositor bindings so
 *        that an HMD's /input/system/click drives the SteamVR dashboard open
 *        and the head-locked lasermouse leftclick.
 *
 * Lifted from driver/src/bindings_patcher.{hpp,cpp} in Phase 4 (D-10) so that
 * both driver_micmap.dll and micmap.exe can link a single source of truth.
 * The driver's DriverLog dependency is replaced by an injected LogSink so
 * this library carries no driver-only symbols and is unit-testable against a
 * throwaway tmp directory (no live SteamVR runtime required).
 *
 * Idempotency is enforced by a marker key (kMarkerKey = "micmap_patched_v2")
 * on the top-level JSON object. A one-time sibling backup (.micmap_backup)
 * captures the pristine pre-MicMap file so uninstall can restore it
 * (write-once semantics — D-08; see driver/src/bindings_patcher.cpp:198).
 */

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace micmap::bindings {

namespace fs = std::filesystem;
using json   = nlohmann::json;

/**
 * @brief Injected log sink. Defaults to a no-op so callers need not wire a
 *        logger when they don't care (tests, smoke paths).
 *
 * Driver side wraps DriverLog; app side wraps MICMAP_LOG_INFO.
 */
using LogSink = std::function<void(const char*)>;

/// No-op default log sink (matches research §Open Question 6 shape).
inline void NullLog(const char* /*msg*/) {}

// Marker keys — ownership of micmap_patched_v2 is shared between the
// installer and the driver; do NOT bump to v3 per 04-PATTERNS.md.
inline constexpr const char* kMarkerKey   = "micmap_patched_v2";
inline constexpr const char* kMarkerKeyV1 = "micmap_patched_v1";

// ----- Top-level entry points (D-10 / D-11) -----

/**
 * @brief Ensures vrcompositor_bindings_generic_hmd.json contains the HMD
 *        system-click -> dashboard + lasermouse leftclick bindings, and
 *        that lighthouse_hmd controller-type files exist.
 *
 * Safe to call every driver launch / every installer run.
 * @returns true if the files are in the patched state after the call.
 */
bool PatchGenericHmdBindings(LogSink log = NullLog);

/**
 * @brief Reverses PatchGenericHmdBindings — restores from .micmap_backup if
 *        present (write-once: backup is NOT removed), else erases our marker
 *        keys + MicMap-added bindings in place. Deletes the controller-type
 *        lighthouse_hmd files entirely if they carry our marker (Valve never
 *        shipped those — they are entirely ours). Per D-11: returns true and
 *        does nothing if the target file is missing or has no MicMap marker.
 */
bool UnpatchGenericHmdBindings(LogSink log = NullLog);

// ----- Lifted helpers exposed for app-side + tests -----

/**
 * @brief Resolves {SteamVR}\resources\config\ via %LOCALAPPDATA%\openvr\openvrpaths.vrpath.
 * @returns empty path on failure (LOCALAPPDATA unset, file missing, parse error).
 */
fs::path ResolveSteamVrConfigDir(LogSink log = NullLog);

/// In-place patch of the generic_hmd compositor bindings file in configDir.
bool PatchGenericHmdBindingsFile(const fs::path& configDir, LogSink log = NullLog);

/// Reverses PatchGenericHmdBindingsFile (single-file flavor for tests + app).
bool UnpatchGenericHmdBindingsFile(const fs::path& configDir, LogSink log = NullLog);

/**
 * @brief Writes controller-type-specific bindings + profile files for
 *        controllerType (e.g. "lighthouse_hmd"). Skips non-MicMap files.
 */
bool EnsureControllerTypeFiles(const fs::path& configDir,
                               const std::string& controllerType,
                               LogSink log = NullLog);

/// Atomic tmp-then-rename JSON write helper.
bool AtomicWriteJson(const fs::path& target, const json& j, LogSink log = NullLog);

}  // namespace micmap::bindings

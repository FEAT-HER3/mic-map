/**
 * @file bindings_patcher.hpp
 * @brief Patches SteamVR's system-level generic-HMD vrcompositor bindings so
 *        that an HMD's /input/system/click (when published by a driver on the
 *        HMD property container) drives the SteamVR dashboard open + the
 *        head-locked lasermouse leftclick.
 *
 * Rationale: the default SteamVR bindings file
 *   <runtime>/resources/config/vrcompositor_bindings_generic_hmd.json
 * ships with an empty `sources` array on /actions/lasermouse and no
 * /actions/system binding at all. HMDs that SteamVR resolves as
 * controller_type = "lighthouse_hmd" (e.g. Bigscreen Beyond) or
 * "generic_hmd" fall through to this file and therefore have no route
 * from HMD system click -> dashboard.
 *
 * We patch the file at driver init to add the Index-equivalent bindings.
 * Idempotent via a marker key; writes a one-time sibling backup
 * (.micmap_backup) before the first modification so uninstall can restore.
 */

#pragma once

namespace micmap::driver {

/**
 * @brief Ensures vrcompositor_bindings_generic_hmd.json contains the HMD
 *        system-click -> dashboard + lasermouse leftclick bindings.
 *
 * Safe to call every driver launch. Returns true if the file is in the
 * patched state after the call (either already was, or was patched now).
 * Returns false if the file could not be located / read / written.
 */
bool PatchGenericHmdBindings();

} // namespace micmap::driver

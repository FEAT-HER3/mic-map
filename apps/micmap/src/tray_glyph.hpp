/**
 * @file tray_glyph.hpp
 * @brief Phase 10 / HEALTH-08 / D-04..D-06: tray-icon state glyph derivation + Shell tray swap.
 *
 * Pure deriveTrayGlyph() function over (HealthSnapshot, StateSnapshot, lastTriggeredAt, now).
 * Win32 helpers initTrayIcons / applyTrayGlyph / destroyTrayIcons are Windows-only and
 * compiled only when _WIN32 is defined (so the headless ctest fixture
 * test_tray_glyph_state_machine builds on any host).
 *
 * Hooked into the existing 1Hz /health + 2Hz /state poll callback at
 * apps/micmap/main.cpp:521+ (D-06 — no new poll, no new thread).
 *
 * The third deriveTrayGlyph argument is a raw chrono::steady_clock::time_point
 * (the lastTriggeredAt instant), NOT the full TrayState&. This matches the
 * test_tray_glyph_state_machine.cpp scaffold's call signature; the 300ms pulse
 * window is the only piece of TrayState the pure derivation needs to read.
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>   // NOTIFYICONDATAW, Shell_NotifyIconW
#endif

namespace micmap::client {

// ---------------------------------------------------------------------------
// Snapshot input types — canonical declaration site for Phase 10 client-side
// pure-derivation surfaces. P8 inlined the JSON parse into pollDriverHealth()
// against atomic fields on MicMapApp, so these dedicated structs do not exist
// elsewhere; 10-03 fail_pill.hpp + 10-06 version_mismatch.hpp will reuse by
// including this header (the structs are intentionally minimal — only the
// fields the pure deriveTrayGlyph / pickActivePill / classifyVersionMismatch
// functions consume).
// ---------------------------------------------------------------------------

struct HealthSnapshot {
    bool driverLoaded{false};                  ///< /health responded 200 OK (FAIL-02 surface)
    std::string driverVersion;                 ///< populated by 10-03 D-19 wiring; empty in Wave 2
    bool driverDetectionActive{false};         ///< /health.driver_detection_active (P7 D-09)
    bool driverTrainingActive{false};          ///< /health.driver_training_active (P9 D-07)
};

struct StateSnapshot {
    std::string detectionState;                ///< "idle" | "training" | "detecting" | "triggered" | "cooldown"
    std::optional<std::string> lastError;      ///< /state.last_error
    std::string audioDeviceState;              ///< "active" | "missing" | "permission_denied"
};

// ---------------------------------------------------------------------------
// Glyph state machine
// ---------------------------------------------------------------------------

enum class TrayGlyph { Armed, Triggered, Error };

struct TrayState {
#ifdef _WIN32
    HICON iconArmed{nullptr};
    HICON iconTriggered{nullptr};
    HICON iconError{nullptr};
#endif
    TrayGlyph current{TrayGlyph::Armed};
    /// Updated by the poll callback when /state.detection_state == "triggered";
    /// drives the 300ms pulse window in deriveTrayGlyph (D-04 / D-05).
    std::chrono::steady_clock::time_point lastTriggeredAt{};
};

/**
 * Pure derivation per CONTEXT D-05 priority rules:
 *   error  > triggered (or 300ms pulse window) > armed
 *
 * Error sources (highest priority, FAIL-01 / FAIL-02 / FAIL-05 surfaces):
 *   - !health.driverLoaded
 *   - state.lastError has a non-empty value
 *   - state.audioDeviceState in {"missing", "permission_denied"}
 *
 * Triggered: state.detectionState == "triggered" OR
 *            (now - lastTriggeredAt) < 300ms.
 *
 * Armed: default healthy state.
 */
TrayGlyph deriveTrayGlyph(const HealthSnapshot& health,
                          const StateSnapshot& state,
                          std::chrono::steady_clock::time_point lastTriggeredAt,
                          std::chrono::steady_clock::time_point now);

#ifdef _WIN32
/// Load the 3 persistent HICONs once at WinMain startup (Pitfall 2 — no
/// per-swap allocation; Pitfall 8 — absolute path resolution).
void initTrayIcons(HINSTANCE hInst, TrayState& ts);

/// Idempotent NIM_MODIFY swap to the desired glyph. Saves and restores
/// nid.uFlags around the NIF_ICON-only call (Pitfall 3 mitigation —
/// no NIF_INFO bit pollution / balloon resurrection).
void applyTrayGlyph(NOTIFYICONDATAW& nid, TrayState& ts, TrayGlyph desired);

/// Release the 3 persistent HICONs at WinMain teardown.
void destroyTrayIcons(TrayState& ts);
#endif

} // namespace micmap::client

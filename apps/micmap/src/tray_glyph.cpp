/**
 * @file tray_glyph.cpp
 * @brief Phase 10 / HEALTH-08 / D-04..D-06 implementation.
 *
 * Pure deriveTrayGlyph() per D-05 priority rules + Win32 init/apply/destroy
 * helpers. The Win32 portion is _WIN32-gated so the headless ctest fixture
 * test_tray_glyph_state_machine.cpp builds on any host (the test only links
 * micmap::core_runtime + this TU and exercises deriveTrayGlyph).
 *
 * Pitfall mitigations:
 *  - Pitfall 1 (TaskbarCreated): handled in main.cpp WindowProc, not here.
 *  - Pitfall 2 (HICON leak): three persistent HICONs loaded once at init;
 *    applyTrayGlyph never reallocates.
 *  - Pitfall 3 (NIF_INFO pollution / balloon resurrection): save/restore
 *    nid.uFlags around the NIF_ICON-only NIM_MODIFY call.
 *  - Pitfall 8 (LoadImageW relative path): absolute path resolution via
 *    GetModuleFileNameW + PathRemoveFileSpecW + PathCombineW.
 */

#include "tray_glyph.hpp"

#ifdef _WIN32
#include <shlwapi.h>     // PathRemoveFileSpecW, PathCombineW (Pitfall 8 — absolute path resolution)
#pragma comment(lib, "Shlwapi.lib")
#endif

#include "micmap/common/logger.hpp"

namespace micmap::client {

TrayGlyph deriveTrayGlyph(const HealthSnapshot& health,
                          const StateSnapshot& state,
                          std::chrono::steady_clock::time_point lastTriggeredAt,
                          std::chrono::steady_clock::time_point now) {
    // D-05 derivation rules in priority order: Error > Triggered > Armed.

    // --- Error (red) — highest priority ---
    if (!health.driverLoaded) {
        return TrayGlyph::Error;                                  // FAIL-02 surface
    }
    if (state.lastError.has_value() && !state.lastError->empty()) {
        return TrayGlyph::Error;                                  // /state.last_error surface
    }
    if (state.audioDeviceState == "missing"
     || state.audioDeviceState == "permission_denied") {
        return TrayGlyph::Error;                                  // FAIL-01 / FAIL-05 surface
    }

    // --- Triggered (pulse) — 300ms window after most recent triggered observation ---
    // The pulse window catches the cooldown/idle "tail" so the user perceives
    // a held flash rather than a single-frame swap (CONTEXT Discretion §pulse-icon).
    if (state.detectionState == "triggered"
     || (lastTriggeredAt != std::chrono::steady_clock::time_point{}
         && (now - lastTriggeredAt) < std::chrono::milliseconds(300))) {
        return TrayGlyph::Triggered;
    }

    // --- Armed (green) — default healthy state ---
    return TrayGlyph::Armed;
}

#ifdef _WIN32
void initTrayIcons(HINSTANCE hInst, TrayState& ts) {
    // Pitfall 8 mitigation — resolve the .ico paths against the EXE's directory,
    // not relative-to-CWD. SteamVR launches micmap.exe from arbitrary CWDs (the
    // Steam install dir, the user profile, etc.); a bare relative path to
    // LoadImageW silently fails to load and the tray glyph stays at the
    // boot-time IDI_APPLICATION fallback.
    wchar_t exeDir[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH) == 0) {
        MICMAP_LOG_WARNING("initTrayIcons: GetModuleFileNameW failed (GLE=", GetLastError(), ")");
        return;
    }
    PathRemoveFileSpecW(exeDir);

    auto loadIcon = [&](const wchar_t* relPath) -> HICON {
        wchar_t full[MAX_PATH];
        if (!PathCombineW(full, exeDir, relPath)) {
            MICMAP_LOG_WARNING("initTrayIcons: PathCombineW failed for relPath");
            return nullptr;
        }
        // LR_DEFAULTSIZE picks the system tray's preferred resolution from the
        // multi-size .ico (RESEARCH §Pattern 1 / Assumption A1).
        HICON h = static_cast<HICON>(::LoadImageW(
            nullptr, full, IMAGE_ICON, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (!h) {
            MICMAP_LOG_WARNING("initTrayIcons: LoadImageW failed (GLE=", GetLastError(), ")");
        }
        return h;
    };

    ts.iconArmed     = loadIcon(L"resources\\tray_armed.ico");
    ts.iconTriggered = loadIcon(L"resources\\tray_triggered.ico");
    ts.iconError     = loadIcon(L"resources\\tray_error.ico");

    if (!ts.iconArmed || !ts.iconTriggered || !ts.iconError) {
        MICMAP_LOG_WARNING("initTrayIcons: partial load (armed=",
                           static_cast<void*>(ts.iconArmed),
                           ", triggered=", static_cast<void*>(ts.iconTriggered),
                           ", error=", static_cast<void*>(ts.iconError), ")");
    }
    (void)hInst;   // hInst unused with LR_LOADFROMFILE; kept in signature for future RC-based loads
}

void applyTrayGlyph(NOTIFYICONDATAW& nid, TrayState& ts, TrayGlyph desired) {
    // Idempotent — no Shell call when the glyph is already correct.
    if (ts.current == desired) return;

    HICON h = (desired == TrayGlyph::Armed)     ? ts.iconArmed
            : (desired == TrayGlyph::Triggered) ? ts.iconTriggered
            :                                     ts.iconError;
    if (!h) {
        // Partial-load failure — skip silently (a one-time WARNING was logged at init).
        return;
    }

    // Pitfall 3 mitigation: save / set NIF_ICON only / NIM_MODIFY / restore prevFlags.
    // The existing nid carries NIF_ICON | NIF_MESSAGE | NIF_TIP from SetupSystemTray;
    // first_launch_balloon.cpp may have set NIF_INFO transiently. Setting only NIF_ICON
    // for the swap means we never accidentally re-fire the balloon path.
    const UINT prevFlags = nid.uFlags;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = h;
    if (!::Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        MICMAP_LOG_WARNING("applyTrayGlyph: Shell_NotifyIconW(NIM_MODIFY) failed (GLE=",
                           GetLastError(), ")");
    }
    nid.uFlags = prevFlags;

    ts.current = desired;
}

void destroyTrayIcons(TrayState& ts) {
    if (ts.iconArmed)     { ::DestroyIcon(ts.iconArmed);     ts.iconArmed     = nullptr; }
    if (ts.iconTriggered) { ::DestroyIcon(ts.iconTriggered); ts.iconTriggered = nullptr; }
    if (ts.iconError)     { ::DestroyIcon(ts.iconError);     ts.iconError     = nullptr; }
}
#endif // _WIN32

} // namespace micmap::client

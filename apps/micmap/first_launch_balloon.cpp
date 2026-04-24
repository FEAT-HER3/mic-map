/**
 * @file first_launch_balloon.cpp
 * @brief Phase 3 D-09 first-silent-launch balloon + production Shell adapter.
 */

#include "first_launch_balloon.hpp"
#include "micmap/common/logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#endif

namespace micmap::apps {

void fireBalloonIfFirstSilentLaunch(IShellNotifySeam& shell,
                                    core::IConfigManager& configMgr,
                                    bool minimized) {
    // D-09: balloon is gated on silent auto-launch. User-clicked boots
    // (no --minimized) must not consume the one-shot regardless of flag.
    if (!minimized) return;

    core::AppConfig& cfg = configMgr.getConfig();
    if (cfg.shownTrayNotification) return;  // D-10: one-shot already consumed.

    const bool accepted = shell.notifyWithBalloon(
        L"MicMap",
        L"Running in the system tray. Click the icon to open.");

    // Persist regardless of Shell's accepted/suppressed verdict — Pitfall 7:
    // Focus Assist can silently drop the balloon, but the flag means "we
    // tried on first silent launch", not "the user saw it". Firing again
    // on the next silent launch would be more annoying than helpful.
    cfg.shownTrayNotification = true;
    configMgr.saveDefault();

    if (accepted) {
        MICMAP_LOG_INFO("tray balloon fired (first silent launch)");
    } else {
        MICMAP_LOG_WARNING("tray balloon suppressed by Shell (Focus Assist?)");
    }
}

#ifdef _WIN32

ProductionShellNotifySeam::ProductionShellNotifySeam(NOTIFYICONDATAW& nid)
    : nid_(nid) {}

bool ProductionShellNotifySeam::notifyWithBalloon(const wchar_t* title,
                                                  const wchar_t* body) {
    // Save flags so later NIM_MODIFY calls (tooltip / icon updates) don't
    // re-fire the balloon — Pitfall 11 "clear-after-fire" discipline.
    const UINT prevFlags = nid_.uFlags;

    nid_.uFlags |= NIF_INFO;
    wcscpy_s(nid_.szInfoTitle, _countof(nid_.szInfoTitle), title ? title : L"");
    wcscpy_s(nid_.szInfo,      _countof(nid_.szInfo),      body  ? body  : L"");
    nid_.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;

    const BOOL ok = Shell_NotifyIconW(NIM_MODIFY, &nid_);

    // Clear balloon-specific fields so subsequent NIM_MODIFY (tooltip /
    // icon updates elsewhere in the app) does NOT re-display this balloon.
    nid_.uFlags = prevFlags;
    nid_.szInfo[0] = L'\0';
    nid_.szInfoTitle[0] = L'\0';

    return ok == TRUE;
}

#endif  // _WIN32

} // namespace micmap::apps

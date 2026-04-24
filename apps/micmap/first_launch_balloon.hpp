#pragma once

/**
 * @file first_launch_balloon.hpp
 * @brief First-silent-launch tray balloon (Phase 3 D-09 / D-10)
 *
 * Fires a Windows Shell balloon on the tray icon the FIRST TIME MicMap is
 * silently auto-launched by SteamVR (flags.minimized == true). Persistence
 * is via AppConfig::shownTrayNotification (Phase 2 defensive reader +
 * Phase 3 Plan 03's new field). Focus Assist / Quiet Hours respected via
 * NIIF_RESPECT_QUIET_TIME (Pitfall 7).
 *
 * Namespace: micmap::apps (plural) — matches the Plan 01 RED test
 * (tests/test_tray_balloon_once.cpp line 37: `namespace ma = micmap::apps;`).
 */

#include "micmap/core/config_manager.hpp"

// Forward-declare NOTIFYICONDATAW so this header does not drag in <windows.h>
// into every TU that wants the seam. ProductionShellNotifySeam's .cpp
// includes the full Win32 SDK.
#ifdef _WIN32
struct _NOTIFYICONDATAW;
typedef struct _NOTIFYICONDATAW NOTIFYICONDATAW;
#endif

namespace micmap::apps {

/**
 * @brief Injection seam over Shell_NotifyIconW(NIM_MODIFY, NIF_INFO) — Pitfall 11.
 *
 * Tests override this with a stub that counts calls; production code uses
 * ProductionShellNotifySeam below, which forwards to the live tray icon
 * (already NIM_ADD'd by SetupSystemTray).
 */
class IShellNotifySeam {
public:
    virtual ~IShellNotifySeam() = default;
    /// Returns true if Shell accepted the balloon (Shell_NotifyIconW returned TRUE).
    virtual bool notifyWithBalloon(const wchar_t* title, const wchar_t* body) = 0;
};

/**
 * @brief Fires the balloon iff `minimized && !config.shownTrayNotification`.
 *
 * Side effects on fire: flips config.shownTrayNotification to true; calls
 * configMgr.saveDefault(). Flag is flipped even if Shell suppresses display
 * (Focus Assist / Quiet Hours) — D-09 policy: the flag is "we tried", not
 * "user saw it".
 */
void fireBalloonIfFirstSilentLaunch(IShellNotifySeam& shell,
                                    core::IConfigManager& configMgr,
                                    bool minimized);

#ifdef _WIN32
/**
 * @brief Production Shell_NotifyIconW adapter (Pitfall 11 clear-after-fire).
 *
 * Wraps the tray NOTIFYICONDATAW owned by MicMapApp::nid. Before calling,
 * sets NIF_INFO + title + body + NIIF_INFO + NIIF_RESPECT_QUIET_TIME; after
 * the call, clears szInfo / szInfoTitle / NIF_INFO so later NIM_MODIFY
 * (tooltip / icon updates) does NOT re-fire the balloon.
 */
class ProductionShellNotifySeam : public IShellNotifySeam {
public:
    explicit ProductionShellNotifySeam(NOTIFYICONDATAW& nid);
    bool notifyWithBalloon(const wchar_t* title, const wchar_t* body) override;
private:
    NOTIFYICONDATAW& nid_;
};
#endif  // _WIN32

} // namespace micmap::apps

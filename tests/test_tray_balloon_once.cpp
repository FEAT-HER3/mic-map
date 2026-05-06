/**
 * @file test_tray_balloon_once.cpp
 * @brief RED unit test for AUTO-06 first-silent-launch balloon (D-09 / D-10).
 *
 * Phase 3 Plan 01 Task 2 (Wave 0 test scaffold). Verifies the
 * fireBalloonIfFirstSilentLaunch() rule: the FIRST time the app boots with
 * --minimized AFTER install, a Shell_NotifyIcon NIM_MODIFY+NIF_INFO
 * balloon fires AND the AppConfig.shownTrayNotification flag is flipped to
 * true and persisted via saveDefault(). All subsequent boots — silent or
 * not — must be balloon-silent.
 *
 * Plan 03-06 supplies:
 *   - apps/micmap/first_launch_balloon.hpp
 *       declares IShellNotifySeam and fireBalloonIfFirstSilentLaunch().
 *   - apps/micmap/first_launch_balloon.cpp
 *       implements the gate against AppConfig.shownTrayNotification +
 *       IConfigManager::saveDefault().
 *   - core::AppConfig gains a `bool shownTrayNotification = false;` field
 *     (defensive reader uses .value("shownTrayNotification", false)).
 *
 * Until then this translation unit fails to compile with a missing
 * "first_launch_balloon.hpp" diagnostic. After that header lands but
 * before AppConfig.shownTrayNotification is added, this still fails to
 * compile on the field reference. Both are valid RED states.
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail.
 */

#include "micmap/core/config_manager.hpp"
#include "first_launch_balloon.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace mc  = micmap::core;
namespace ma  = micmap::apps;  // Plan 03-06 places the header in this ns.

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace {

/// Stub seam for Shell_NotifyIcon. Counts calls + captures the most-recent
/// (title, body) pair so the test can assert non-empty user-facing strings.
class StubShellNotifySeam : public ma::IShellNotifySeam {
public:
    int notifyCount_ = 0;
    std::wstring lastTitle_;
    std::wstring lastBody_;

    bool notifyWithBalloon(const wchar_t* title, const wchar_t* body) override {
        ++notifyCount_;
        if (title) lastTitle_ = title;
        if (body)  lastBody_  = body;
        return true;
    }
};

/// Tiny IConfigManager stub. Holds an AppConfig in-memory, counts
/// saveDefault() invocations. Filesystem-touching methods are no-ops; the
/// test never invokes them.
class StubConfigManager : public mc::IConfigManager {
public:
    mc::AppConfig cfg_;
    int saveDefaultCount_ = 0;

    bool load(const std::filesystem::path&)        override { return true; }
    bool save(const std::filesystem::path&)        override { return true; }
    bool loadDefault()                             override { return true; }
    bool saveDefault() override {
        ++saveDefaultCount_;
        return true;
    }
    const mc::AppConfig& getConfig() const override { return cfg_; }
    mc::AppConfig&       getConfig()       override { return cfg_; }
    void resetToDefaults()                 override { cfg_ = mc::AppConfig{}; }
    std::filesystem::path getConfigDirectory()  const override { return {}; }
    std::filesystem::path getDefaultConfigPath() const override { return {}; }
    std::filesystem::path getTrainingDataPath()  const override { return {}; }
};

}  // namespace

int main() {
    // P8 08-05 D-07 / IPC-05 update: fireBalloonIfFirstSilentLaunch no longer
    // calls saveDefault() inline -- persistence flows through the optional
    // IDriverApi PUT /settings parameter (defaulting to nullptr in this
    // headless test path). The test contract therefore drops the
    // saveDefaultCount assertion; the in-memory flag-flip + balloon-fired +
    // re-fire-suppression behaviors are the surviving invariants.

    // ---- Case 1: shownTrayNotification=false + minimized=true -> fires ----
    {
        StubShellNotifySeam seam;
        StubConfigManager   cfg;
        cfg.cfg_.shownTrayNotification = false;

        ma::fireBalloonIfFirstSilentLaunch(seam, cfg, /*minimized=*/true);

        MM_CHECK(seam.notifyCount_ == 1);                       // balloon fired
        MM_CHECK(cfg.cfg_.shownTrayNotification == true);       // flag flipped
        MM_CHECK(!seam.lastTitle_.empty());                     // user-facing strings
        MM_CHECK(!seam.lastBody_.empty());
        std::cout << "PASS case_1_first_silent_launch_fires\n";
    }

    // ---- Case 2: shownTrayNotification=true -> never re-fires ----
    {
        StubShellNotifySeam seam;
        StubConfigManager   cfg;
        cfg.cfg_.shownTrayNotification = true;

        ma::fireBalloonIfFirstSilentLaunch(seam, cfg, /*minimized=*/true);

        MM_CHECK(seam.notifyCount_ == 0);
        std::cout << "PASS case_2_already_shown_no_refire\n";
    }

    // ---- Case 3: minimized=false -> never fires regardless of flag ----
    // The balloon is gated on silent auto-launch (D-09). User-clicked boots
    // (no --minimized) must not consume the one-shot regardless of state.
    {
        StubShellNotifySeam seam;
        StubConfigManager   cfg;
        cfg.cfg_.shownTrayNotification = false;

        ma::fireBalloonIfFirstSilentLaunch(seam, cfg, /*minimized=*/false);

        MM_CHECK(seam.notifyCount_ == 0);
        MM_CHECK(cfg.cfg_.shownTrayNotification == false);  // unchanged
        std::cout << "PASS case_3_user_launch_does_not_consume\n";
    }

    std::cout << "all tests passed\n";
    return 0;
}

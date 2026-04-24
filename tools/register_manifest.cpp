// tools/register_manifest.cpp — ad-hoc helper for Phase 03 A2 empirical test.
//
// NOT shipped in the installer (warning 2 resolution — option (a)). Registers
// the passed-in absolute manifest path with SteamVR via
// IVRApplications::AddApplicationManifest(path, temporary=false), then enables
// auto-launch for the bigscreen.micmap app key, then exits. Throwaway target —
// delete after Phase 03 exit.
//
// Usage:
//   register_manifest.exe <absolute-path-to.vrmanifest>
//
// Exit codes:
//   0 — manifest registered + autolaunch enabled successfully
//   1 — VR_Init / AddApplicationManifest / SetApplicationAutoLaunch failed
//   2 — usage error
//
// Logging routes through micmap::common::Logger (default ConsoleLogger;
// constructed at static-init time — no init call required).

#include <openvr.h>

#include <micmap/common/logger.hpp>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr,
                     "usage: register_manifest <absolute-path-to.vrmanifest>\n");
        return 2;
    }

    vr::EVRInitError initErr = vr::VRInitError_None;
    vr::VR_Init(&initErr, vr::VRApplication_Utility);
    if (initErr != vr::VRInitError_None) {
        MICMAP_LOG_ERROR("VR_Init(Utility) failed: ",
                         vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
        return 1;
    }

    vr::EVRApplicationError appErr =
        vr::VRApplications()->AddApplicationManifest(argv[1], false);
    if (appErr != vr::VRApplicationError_None) {
        MICMAP_LOG_ERROR(
            "AddApplicationManifest failed: ",
            vr::VRApplications()->GetApplicationsErrorNameFromEnum(appErr));
        vr::VR_Shutdown();
        return 1;
    }

    MICMAP_LOG_INFO("Registered manifest: ", argv[1]);

    constexpr const char* kAppKey = "bigscreen.micmap";
    vr::EVRApplicationError alErr =
        vr::VRApplications()->SetApplicationAutoLaunch(kAppKey, true);
    if (alErr != vr::VRApplicationError_None) {
        MICMAP_LOG_ERROR(
            "SetApplicationAutoLaunch failed: ",
            vr::VRApplications()->GetApplicationsErrorNameFromEnum(alErr));
        vr::VR_Shutdown();
        return 1;
    }
    MICMAP_LOG_INFO("Auto-launch enabled for app key: ", kAppKey);

    vr::VR_Shutdown();
    return 0;
}

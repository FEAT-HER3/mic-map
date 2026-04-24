#pragma once

/**
 * @file manifest_registrar.hpp
 * @brief SteamVR app.vrmanifest registration (Phase 3 auto-start)
 *
 * Wraps the IVRApplications surface for registering / de-registering MicMap's
 * app.vrmanifest and toggling auto-launch. The CALLER owns VR_Init + VR_Shutdown
 * (registrar is invoked from VR_Init(VRApplication_Utility) context per D-02 / D-15).
 *
 * Pitfall 1 (OpenVR #1378): registerApp polls IsApplicationInstalled for up to
 *   2 seconds between AddApplicationManifest and SetApplicationAutoLaunch.
 * Pitfall 3 (OpenVR #1547): ensureRegistered is idempotent — re-running on every
 *   GUI boot self-heals across SteamVR upgrades and manual user removal.
 *
 * Forward-slash guard (Plan 03-02 A2 pitfall): registerApp refuses UTF-8 paths
 *   containing '/'; vrserver silently skips such manifests (treats the full path
 *   as working dir) and no error propagates. The guard converts that silent
 *   failure into RegisterResult::AddFailed with a diagnostic lastError.
 *
 * The IVRApplicationsSurface seam uses vr::EVRApplicationError directly so the
 * test stub (tests/test_manifest_registrar.cpp) can override the methods with
 * their native OpenVR return type. This implies micmap::steamvr must expose
 * OpenVR headers PUBLIC-ly to its consumers (see src/steamvr/CMakeLists.txt).
 */

#include <cstdint>
#include <memory>
#include <string>

#include <openvr.h>

namespace micmap::steamvr {

enum class RegisterResult {
    Success,
    AddFailed,
    PollTimeout,
    AutoLaunchFailed,
    RemoveFailed,
    VRNotAvailable,
};

/**
 * @brief Test-injection seam over vr::IVRApplications.
 *
 * The production adapter (in manifest_registrar.cpp) implements these four
 * methods by forwarding to vr::VRApplications(). The test double
 * (tests/test_manifest_registrar.cpp StubApplicationsSurface) implements them
 * by recording call strings and returning scripted results — so SteamVR need
 * not be running to exercise the register/unregister/poll sequence.
 *
 * Method signatures match vr::IVRApplications_007/_008 (ABI-compatible slice).
 * GetApplicationsErrorNameFromEnum has a default implementation so the test
 * stub need not override it (the test does not exercise error-name logging).
 */
class IVRApplicationsSurface {
public:
    virtual ~IVRApplicationsSurface() = default;

    virtual vr::EVRApplicationError AddApplicationManifest(const char* manifestAbsPath,
                                                           bool bTemporary) = 0;
    virtual bool                    IsApplicationInstalled(const char* appKey) = 0;
    virtual vr::EVRApplicationError SetApplicationAutoLaunch(const char* appKey,
                                                             bool bAutoLaunch) = 0;
    virtual vr::EVRApplicationError RemoveApplicationManifest(const char* manifestAbsPath) = 0;

    /// Default returns a minimal static string. Production adapter overrides
    /// to forward to vr::VRApplications()->GetApplicationsErrorNameFromEnum.
    /// Test stubs that do not exercise error paths can skip this override.
    virtual const char* GetApplicationsErrorNameFromEnum(vr::EVRApplicationError err);
};

class IManifestRegistrar {
public:
    virtual ~IManifestRegistrar() = default;

    /// Full register sequence: AddApplicationManifest -> poll IsApplicationInstalled ->
    /// SetApplicationAutoLaunch(true). 100ms x 20 poll ceiling per D-17. Logs once
    /// on entry and once on exit of the poll loop (D-18).
    virtual RegisterResult registerApp() = 0;

    /// RemoveApplicationManifest(manifestAbsPath). Used by the --unregister-vrmanifest
    /// CLI (Plan 03-06) and the Phase 4 installer uninstall hook.
    virtual RegisterResult unregisterApp() = 0;

    /// Idempotent: returns Success without side effects when
    /// IsApplicationInstalled(appKey) is already true; otherwise delegates to
    /// registerApp(). This is the GUI boot-time self-heal per D-15 / D-18.
    virtual RegisterResult ensureRegistered() = 0;

    virtual std::string getLastError() const = 0;
};

/**
 * @brief Production factory.
 *
 * Resolves:
 *   appKey          = "bigscreen.micmap"
 *   manifestAbsPath = GetModuleFileNameW(nullptr) + PathCchRemoveFileSpec +
 *                     L"\\app.vrmanifest" (backslash-canonical per A2 pitfall).
 *
 * Uses the vr::VRApplications() accessor internally — NEVER hardcodes
 * "IVRApplications_007" or "IVRApplications_008" strings.
 *
 * When MICMAP_HAS_OPENVR is undefined (no OpenVR SDK at build time), returns
 * a stub implementation that reports RegisterResult::VRNotAvailable for all
 * operations. Callers must not assume Success in that case.
 */
std::unique_ptr<IManifestRegistrar> createManifestRegistrar();

/**
 * @brief Test factory.
 *
 * The caller supplies the surface seam and the appKey / manifestAbsPath the
 * registrar will use. No OpenVR calls are made — the production path
 * (VRApplicationsAdapter, resolveManifestAbsolutePath) is not entered.
 */
std::unique_ptr<IManifestRegistrar> createManifestRegistrarForTesting(
    IVRApplicationsSurface& surface,
    std::string appKey,
    std::wstring manifestAbsPath);

} // namespace micmap::steamvr

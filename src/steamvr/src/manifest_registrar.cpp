/**
 * @file manifest_registrar.cpp
 * @brief SteamVR app.vrmanifest registration (implementation)
 *
 * See manifest_registrar.hpp for contract. Caller owns VR_Init / VR_Shutdown.
 *
 * Log discipline (CONTEXT.md D-17 / D-18):
 *   - INFO on state change only; no per-tick poll output.
 *   - WARNING on unexpected errors with the EVRApplicationError enum NAME
 *     (not just the numeric code) per RESEARCH Pattern 1.
 *
 * Pitfall coverage:
 *   - OpenVR #1378 (SetApplicationAutoLaunch race): the register sequence polls
 *     IsApplicationInstalled for up to 2 seconds (20 x 100ms) between Add and
 *     SetApplicationAutoLaunch. On timeout, SetApplicationAutoLaunch is NEVER
 *     called and RegisterResult::PollTimeout is returned.
 *   - OpenVR #1547 (auto-launch drift): ensureRegistered is idempotent — a
 *     no-op when IsApplicationInstalled already returns true, so GUI boot
 *     re-registration self-heals without log spam.
 *   - A2 forward-slash pitfall (.planning/phases/03-auto-start/03-02-A2-RESULT.md):
 *     vrserver silently skips manifests whose UTF-8 path contains '/' (it
 *     misinterprets the entire path as the working directory). registerApp
 *     guards against this explicitly and fails fast with AddFailed + a
 *     diagnostic lastError.
 *   - Pitfall 9 (long-path GetModuleFileNameW truncation): resolver uses a
 *     32768-WCHAR buffer and rejects both the "returned 0" and "returned
 *     _countof(buf)" (truncation) cases.
 *
 * The registrar never calls VR_Init / VR_Shutdown internally — per D-02 and
 * D-15 the caller (CLI fork in Plan 06, retry thread in Plan 07) owns that
 * lifetime.
 */

#include "micmap/steamvr/manifest_registrar.hpp"
#include "micmap/common/logger.hpp"

#include <chrono>
#include <thread>
#include <utility>

#ifdef MICMAP_HAS_OPENVR
#include <openvr.h>
#include <windows.h>
#include <pathcch.h>
#endif

namespace micmap::steamvr {

// ============================================================================
// IVRApplicationsSurface default (non-pure) method
// ============================================================================
//
// Provided so test doubles that do not exercise error-name logging (see
// tests/test_manifest_registrar.cpp StubApplicationsSurface — overrides only
// the four result-returning methods) do not need to override this one.
// The production adapter (VRApplicationsAdapter below) overrides it and
// forwards to vr::VRApplications()->GetApplicationsErrorNameFromEnum.
const char* IVRApplicationsSurface::GetApplicationsErrorNameFromEnum(vr::EVRApplicationError err) {
    // Minimal, header-only name mapping covering the errors the registrar
    // is reasonably likely to surface via lastError_. Anything else prints
    // as the numeric enumerator via the generic branch (the caller embeds
    // this in a std::string, so null would crash — never return null).
    switch (err) {
        case vr::VRApplicationError_None:                        return "VRApplicationError_None";
        case vr::VRApplicationError_AppKeyAlreadyExists:         return "VRApplicationError_AppKeyAlreadyExists";
        case vr::VRApplicationError_NoManifest:                  return "VRApplicationError_NoManifest";
        case vr::VRApplicationError_NoApplication:               return "VRApplicationError_NoApplication";
        case vr::VRApplicationError_InvalidIndex:                return "VRApplicationError_InvalidIndex";
        case vr::VRApplicationError_UnknownApplication:          return "VRApplicationError_UnknownApplication";
        case vr::VRApplicationError_IPCFailed:                   return "VRApplicationError_IPCFailed";
        case vr::VRApplicationError_ApplicationAlreadyRunning:   return "VRApplicationError_ApplicationAlreadyRunning";
        case vr::VRApplicationError_InvalidManifest:             return "VRApplicationError_InvalidManifest";
        case vr::VRApplicationError_InvalidApplication:          return "VRApplicationError_InvalidApplication";
        case vr::VRApplicationError_LaunchFailed:                return "VRApplicationError_LaunchFailed";
        case vr::VRApplicationError_ApplicationAlreadyStarting:  return "VRApplicationError_ApplicationAlreadyStarting";
        case vr::VRApplicationError_LaunchInProgress:            return "VRApplicationError_LaunchInProgress";
        case vr::VRApplicationError_OldApplicationQuitting:      return "VRApplicationError_OldApplicationQuitting";
        case vr::VRApplicationError_TransitionAborted:           return "VRApplicationError_TransitionAborted";
        case vr::VRApplicationError_IsTemplate:                  return "VRApplicationError_IsTemplate";
        case vr::VRApplicationError_SteamVRIsExiting:            return "VRApplicationError_SteamVRIsExiting";
        case vr::VRApplicationError_BufferTooSmall:              return "VRApplicationError_BufferTooSmall";
        case vr::VRApplicationError_PropertyNotSet:              return "VRApplicationError_PropertyNotSet";
        case vr::VRApplicationError_UnknownProperty:             return "VRApplicationError_UnknownProperty";
        case vr::VRApplicationError_InvalidParameter:            return "VRApplicationError_InvalidParameter";
        default:                                                 return "VRApplicationError_Unknown";
    }
}

namespace {

constexpr const char* kAppKey          = "bigscreen.micmap";
constexpr int         kPollIntervalMs  = 100;
constexpr int         kPollMaxAttempts = 20;   // 2000ms ceiling per D-17

// UTF-16 -> UTF-8 for passing wide paths to OpenVR (which accepts const char*).
// Uses WideCharToMultiByte directly — std::wstring_convert is deprecated in C++17.
std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
#else
    // Non-Windows path — registrar is Windows-only in production. Provide a
    // best-effort ASCII degrade so the header's non-Windows ABI still links.
    std::string out;
    out.reserve(w.size());
    for (wchar_t ch : w) {
        out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
    }
    return out;
#endif
}

#ifdef MICMAP_HAS_OPENVR

// ----------------------------------------------------------------------------
// Production adapter — forwards to vr::VRApplications() accessor.
//
// Uses the vr::VRApplications() accessor function. Never hardcodes
// "IVRApplications_007" / "IVRApplications_008" ABI version strings — the
// accessor resolves to whatever version the linked SDK exposes.
// ----------------------------------------------------------------------------
class VRApplicationsAdapter : public IVRApplicationsSurface {
public:
    vr::EVRApplicationError AddApplicationManifest(const char* path, bool bTemporary) override {
        auto* apps = vr::VRApplications();
        if (!apps) return vr::VRApplicationError_IPCFailed;
        return apps->AddApplicationManifest(path, bTemporary);
    }
    bool IsApplicationInstalled(const char* appKey) override {
        auto* apps = vr::VRApplications();
        if (!apps) return false;
        return apps->IsApplicationInstalled(appKey);
    }
    vr::EVRApplicationError SetApplicationAutoLaunch(const char* appKey, bool bAutoLaunch) override {
        auto* apps = vr::VRApplications();
        if (!apps) return vr::VRApplicationError_IPCFailed;
        return apps->SetApplicationAutoLaunch(appKey, bAutoLaunch);
    }
    vr::EVRApplicationError RemoveApplicationManifest(const char* path) override {
        auto* apps = vr::VRApplications();
        if (!apps) return vr::VRApplicationError_IPCFailed;
        return apps->RemoveApplicationManifest(path);
    }
    const char* GetApplicationsErrorNameFromEnum(vr::EVRApplicationError err) override {
        auto* apps = vr::VRApplications();
        if (!apps) {
            // Fall through to the base-class minimal name map.
            return IVRApplicationsSurface::GetApplicationsErrorNameFromEnum(err);
        }
        return apps->GetApplicationsErrorNameFromEnum(err);
    }
};

// ----------------------------------------------------------------------------
// Long-path-safe absolute path of app.vrmanifest beside the running exe.
//
// Backslash-canonical on Windows: GetModuleFileNameW yields native backslashes,
// PathCchRemoveFileSpec preserves them, and the manual L"\\app.vrmanifest"
// append uses escaped backslashes. DO NOT introduce std::filesystem or any
// forward-slash normalization here (vrserver will silently skip the manifest
// if the UTF-8 form contains '/' — see <critical_pitfall> in 03-04-PLAN.md).
// ----------------------------------------------------------------------------
std::wstring resolveManifestAbsolutePath() {
    // IN-04: 32768 WCHARs = 65,536 bytes on the stack. Safe on the default
    // 1 MB Windows thread stack (main thread and the manifest retry thread
    // both use the default size). If this function is ever called from a
    // thread with a reduced stack (e.g., a UI-framework worker pool), move
    // the buffer to the heap via std::unique_ptr<WCHAR[]>. PATHCCH_MAX_CCH
    // (32767) is the canonical long-path bound; stack-allocating is
    // documented as acceptable in the PathCch* reference.
    WCHAR buf[32768];  // long-path safe per Pitfall 9
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(_countof(buf)));
    if (n == 0 || n == _countof(buf)) {
        MICMAP_LOG_ERROR("GetModuleFileNameW failed or truncated");
        return {};
    }
    if (FAILED(PathCchRemoveFileSpec(buf, _countof(buf)))) {
        MICMAP_LOG_ERROR("PathCchRemoveFileSpec failed");
        return {};
    }
    std::wstring r = buf;
    r += L"\\app.vrmanifest";
    return r;
}

#endif // MICMAP_HAS_OPENVR

// ============================================================================
// Shared impl — agnostic of production-vs-test surface.
// ============================================================================
class ManifestRegistrarImpl : public IManifestRegistrar {
public:
    ManifestRegistrarImpl(IVRApplicationsSurface& surface,
                          std::string appKey,
                          std::wstring manifestAbsPath)
        : surface_(surface),
          appKey_(std::move(appKey)),
          manifestAbsPath_(std::move(manifestAbsPath)) {}

    RegisterResult registerApp() override {
        const std::string utf8Path = wideToUtf8(manifestAbsPath_);
        if (utf8Path.empty()) {
            lastError_ = "manifest path empty or invalid";
            MICMAP_LOG_ERROR(lastError_);
            return RegisterResult::AddFailed;
        }

        // A2 pitfall guard: a forward slash in the UTF-8 manifest path causes
        // vrserver to treat the entire path as the working dir, silently skip
        // the manifest, and return no error. Convert that silent failure into
        // an explicit AddFailed with a diagnostic lastError.
        if (utf8Path.find('/') != std::string::npos) {
            lastError_ = "manifest path contains forward slash; SteamVR will silently skip. Path: " + utf8Path;
            MICMAP_LOG_ERROR(lastError_);
            return RegisterResult::AddFailed;
        }

        const vr::EVRApplicationError addErr =
            surface_.AddApplicationManifest(utf8Path.c_str(), /*bTemporary=*/false);
        if (addErr != vr::VRApplicationError_None) {
            lastError_ = std::string("AddApplicationManifest: ")
                       + surface_.GetApplicationsErrorNameFromEnum(addErr);
            MICMAP_LOG_WARNING(lastError_);
            return RegisterResult::AddFailed;
        }

        MICMAP_LOG_INFO("polling for manifest install");  // D-17: one entry log
        bool installed = false;
        for (int i = 0; i < kPollMaxAttempts; ++i) {
            if (surface_.IsApplicationInstalled(appKey_.c_str())) {
                installed = true;
                MICMAP_LOG_INFO("manifest ready after ", (i + 1) * kPollIntervalMs, "ms");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        }
        if (!installed) {
            lastError_ = "IsApplicationInstalled timeout after 2000ms";
            MICMAP_LOG_WARNING("timeout after 2000ms");
            return RegisterResult::PollTimeout;
        }

        const vr::EVRApplicationError setErr =
            surface_.SetApplicationAutoLaunch(appKey_.c_str(), /*bAutoLaunch=*/true);
        if (setErr != vr::VRApplicationError_None) {
            lastError_ = std::string("SetApplicationAutoLaunch: ")
                       + surface_.GetApplicationsErrorNameFromEnum(setErr);
            MICMAP_LOG_WARNING(lastError_);
            return RegisterResult::AutoLaunchFailed;
        }

        MICMAP_LOG_INFO("manifest registered + auto-launch enabled: ", appKey_);
        lastError_.clear();
        return RegisterResult::Success;
    }

    RegisterResult unregisterApp() override {
        const std::string utf8Path = wideToUtf8(manifestAbsPath_);
        if (utf8Path.empty()) {
            lastError_ = "manifest path empty or invalid";
            MICMAP_LOG_ERROR(lastError_);
            return RegisterResult::RemoveFailed;
        }
        const vr::EVRApplicationError err = surface_.RemoveApplicationManifest(utf8Path.c_str());
        if (err != vr::VRApplicationError_None) {
            lastError_ = std::string("RemoveApplicationManifest: ")
                       + surface_.GetApplicationsErrorNameFromEnum(err);
            MICMAP_LOG_WARNING(lastError_);
            return RegisterResult::RemoveFailed;
        }
        MICMAP_LOG_INFO("manifest removed: ", appKey_);
        lastError_.clear();
        return RegisterResult::Success;
    }

    RegisterResult ensureRegistered() override {
        if (surface_.IsApplicationInstalled(appKey_.c_str())) {
            // D-18: log on state change ONLY. Already-installed is the
            // steady-state; no output here.
            lastError_.clear();
            return RegisterResult::Success;
        }
        return registerApp();  // registerApp logs INFO on state change per D-18.
    }

    std::string getLastError() const override { return lastError_; }

private:
    IVRApplicationsSurface& surface_;
    std::string             appKey_;
    std::wstring            manifestAbsPath_;
    std::string             lastError_;
};

// ============================================================================
// Stub for MICMAP_HAS_OPENVR-undefined builds.
//
// Returns VRNotAvailable for all operations. Never reached in the standard
// Windows build (OpenVR is always found); exists so the library still links
// when built against a toolchain lacking the OpenVR SDK.
// ============================================================================
class StubManifestRegistrar : public IManifestRegistrar {
public:
    RegisterResult registerApp()      override { return RegisterResult::VRNotAvailable; }
    RegisterResult unregisterApp()    override { return RegisterResult::VRNotAvailable; }
    RegisterResult ensureRegistered() override { return RegisterResult::VRNotAvailable; }
    std::string    getLastError() const override { return "OpenVR not available at build time"; }
};

} // anonymous namespace

// ============================================================================
// Factories
// ============================================================================

std::unique_ptr<IManifestRegistrar> createManifestRegistrar() {
#ifdef MICMAP_HAS_OPENVR
    // Stateless, safe as static — the adapter only forwards to
    // vr::VRApplications() which itself is a process-global accessor.
    static VRApplicationsAdapter s_adapter;

    std::wstring manifestPath = resolveManifestAbsolutePath();
    if (manifestPath.empty()) {
        MICMAP_LOG_ERROR("createManifestRegistrar: manifest path resolution failed; returning stub");
        return std::make_unique<StubManifestRegistrar>();
    }
    return std::unique_ptr<IManifestRegistrar>(
        new ManifestRegistrarImpl(s_adapter, std::string(kAppKey), std::move(manifestPath)));
#else
    MICMAP_LOG_WARNING("OpenVR not available at build time — using stub manifest registrar");
    return std::make_unique<StubManifestRegistrar>();
#endif
}

std::unique_ptr<IManifestRegistrar> createManifestRegistrarForTesting(
    IVRApplicationsSurface& surface,
    std::string appKey,
    std::wstring manifestAbsPath) {
    return std::unique_ptr<IManifestRegistrar>(
        new ManifestRegistrarImpl(surface, std::move(appKey), std::move(manifestAbsPath)));
}

} // namespace micmap::steamvr

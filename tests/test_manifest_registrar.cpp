/**
 * @file test_manifest_registrar.cpp
 * @brief RED unit tests for IManifestRegistrar (AUTO-02 / AUTO-03 / AUTO-04 — D-15, D-17).
 *
 * Phase 3 Plan 01 Task 1 (Wave 0 test scaffold). Verifies the manifest
 * registrar's call sequence against IVRApplications_007/_008 — exercised
 * through the project's IVRApplicationsSurface seam so SteamVR doesn't
 * have to be running.
 *
 * Plan 03-04 supplies:
 *   - src/steamvr/include/micmap/steamvr/manifest_registrar.hpp
 *       declares IVRApplicationsSurface, IManifestRegistrar, RegisterResult,
 *       createManifestRegistrar() (production) and
 *       createManifestRegistrarForTesting() (this test's entry point).
 *   - src/steamvr/src/manifest_registrar.cpp
 *       implements the polled register / idempotent ensureRegistered /
 *       unregister flow per CONTEXT.md D-15 + D-17.
 *
 * Until then, this translation unit fails to compile with a missing
 * "micmap/steamvr/manifest_registrar.hpp" diagnostic. That is the
 * expected Wave 0 RED state.
 *
 * Five behavioral cases (PLAN 03-01 Task 1 <behavior>):
 *   Case 1 — registerApp happy path: AddApplicationManifest=None ->
 *            IsApplicationInstalled false,false,true -> SetAutoLaunch=None.
 *            Expects RegisterResult::Success, poll count == 3,
 *            SetApplicationAutoLaunch called ONCE strictly AFTER
 *            IsApplicationInstalled returned true (Pitfall 1 / OpenVR #1378).
 *   Case 2 — Pitfall 1 guard: IsApplicationInstalled false x20 (timeout).
 *            Expects RegisterResult::PollTimeout, SetApplicationAutoLaunch
 *            never called.
 *   Case 3 — AddApplicationManifest fails (VRApplicationError_InvalidManifest).
 *            Expects RegisterResult::AddFailed, no further surface calls.
 *   Case 4 — ensureRegistered idempotent: IsApplicationInstalled returns
 *            true on first call. Expects RegisterResult::Success,
 *            AddApplicationManifest never called, SetAutoLaunch never called.
 *   Case 5 — unregisterApp: RemoveApplicationManifest called once with the
 *            manifest absolute path; returns Success on
 *            VRApplicationError_None.
 */

#include "micmap/steamvr/manifest_registrar.hpp"

#include <openvr.h>

#include <cstdint>
#include <iostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace svr = micmap::steamvr;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

namespace {

/// Stub backing the IVRApplicationsSurface seam. Records every call into
/// callLog_ as "<MethodName>(<args>)" strings and returns scripted values
/// from the *_Results_ queues (or sensible defaults if the queue is empty).
class StubApplicationsSurface : public svr::IVRApplicationsSurface {
public:
    std::vector<std::string> callLog_;

    std::queue<vr::EVRApplicationError> addResults_;
    std::queue<bool>                    isInstalledResults_;
    std::queue<vr::EVRApplicationError> setAutoLaunchResults_;
    std::queue<vr::EVRApplicationError> removeResults_;

    vr::EVRApplicationError AddApplicationManifest(const char* manifestPath,
                                                   bool bTemporary) override {
        std::string entry = "AddApplicationManifest(";
        entry += (manifestPath ? manifestPath : "(null)");
        entry += ",";
        entry += (bTemporary ? "true" : "false");
        entry += ")";
        callLog_.push_back(std::move(entry));
        if (addResults_.empty()) return vr::VRApplicationError_None;
        auto r = addResults_.front(); addResults_.pop(); return r;
    }

    bool IsApplicationInstalled(const char* appKey) override {
        std::string entry = "IsApplicationInstalled(";
        entry += (appKey ? appKey : "(null)");
        entry += ")";
        callLog_.push_back(std::move(entry));
        if (isInstalledResults_.empty()) return false;
        bool r = isInstalledResults_.front(); isInstalledResults_.pop(); return r;
    }

    vr::EVRApplicationError SetApplicationAutoLaunch(const char* appKey,
                                                     bool bAutoLaunch) override {
        std::string entry = "SetApplicationAutoLaunch(";
        entry += (appKey ? appKey : "(null)");
        entry += ",";
        entry += (bAutoLaunch ? "true" : "false");
        entry += ")";
        callLog_.push_back(std::move(entry));
        if (setAutoLaunchResults_.empty()) return vr::VRApplicationError_None;
        auto r = setAutoLaunchResults_.front(); setAutoLaunchResults_.pop(); return r;
    }

    vr::EVRApplicationError RemoveApplicationManifest(const char* manifestPath) override {
        std::string entry = "RemoveApplicationManifest(";
        entry += (manifestPath ? manifestPath : "(null)");
        entry += ")";
        callLog_.push_back(std::move(entry));
        if (removeResults_.empty()) return vr::VRApplicationError_None;
        auto r = removeResults_.front(); removeResults_.pop(); return r;
    }
};

int countCalls(const std::vector<std::string>& log, const std::string& prefix) {
    int n = 0;
    for (const auto& s : log) {
        if (s.rfind(prefix, 0) == 0) ++n;
    }
    return n;
}

int firstIndexWithPrefix(const std::vector<std::string>& log, const std::string& prefix) {
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].rfind(prefix, 0) == 0) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

int main() {
    const std::string  kAppKey       = "bigscreen.micmap";
    const std::wstring kManifestPath = L"C:\\Program Files\\MicMap\\app.vrmanifest";

    // ---- Case 1: registerApp happy path ----
    {
        StubApplicationsSurface stub;
        stub.addResults_.push(vr::VRApplicationError_None);
        stub.isInstalledResults_.push(false);
        stub.isInstalledResults_.push(false);
        stub.isInstalledResults_.push(true);
        stub.setAutoLaunchResults_.push(vr::VRApplicationError_None);

        auto reg = svr::createManifestRegistrarForTesting(stub, kAppKey, kManifestPath);
        MM_CHECK(reg != nullptr);
        svr::RegisterResult r = reg->registerApp();
        MM_CHECK(r == svr::RegisterResult::Success);

        // Three IsApplicationInstalled poll cycles (false, false, true).
        MM_CHECK(countCalls(stub.callLog_, "IsApplicationInstalled(") == 3);

        // SetApplicationAutoLaunch called exactly once.
        MM_CHECK(countCalls(stub.callLog_, "SetApplicationAutoLaunch(") == 1);

        // Ordering invariant: SetApplicationAutoLaunch must come AFTER the
        // last IsApplicationInstalled (which returned true).
        int idxSet  = firstIndexWithPrefix(stub.callLog_, "SetApplicationAutoLaunch(");
        int idxAdd  = firstIndexWithPrefix(stub.callLog_, "AddApplicationManifest(");
        MM_CHECK(idxAdd >= 0);
        MM_CHECK(idxSet > idxAdd);
        // Last IsApplicationInstalled occurs immediately before SetAutoLaunch.
        MM_CHECK(idxSet >= 1);
        MM_CHECK(stub.callLog_[idxSet - 1].rfind("IsApplicationInstalled(", 0) == 0);

        std::cout << "PASS case_1_register_happy_path\n";
    }

    // ---- Case 2: Pitfall 1 guard — poll timeout ----
    {
        StubApplicationsSurface stub;
        stub.addResults_.push(vr::VRApplicationError_None);
        for (int i = 0; i < 25; ++i) stub.isInstalledResults_.push(false);

        auto reg = svr::createManifestRegistrarForTesting(stub, kAppKey, kManifestPath);
        MM_CHECK(reg != nullptr);
        svr::RegisterResult r = reg->registerApp();
        MM_CHECK(r == svr::RegisterResult::PollTimeout);

        // SetApplicationAutoLaunch must NEVER be called when poll never
        // confirms installation — direct OpenVR #1378 mitigation.
        MM_CHECK(countCalls(stub.callLog_, "SetApplicationAutoLaunch(") == 0);

        // Should have polled the configured ceiling (D-17: 20 attempts).
        MM_CHECK(countCalls(stub.callLog_, "IsApplicationInstalled(") == 20);

        std::cout << "PASS case_2_poll_timeout_no_setautolaunch\n";
    }

    // ---- Case 3: AddApplicationManifest fails ----
    {
        StubApplicationsSurface stub;
        stub.addResults_.push(vr::VRApplicationError_InvalidManifest);

        auto reg = svr::createManifestRegistrarForTesting(stub, kAppKey, kManifestPath);
        MM_CHECK(reg != nullptr);
        svr::RegisterResult r = reg->registerApp();
        MM_CHECK(r == svr::RegisterResult::AddFailed);

        // No further surface calls should follow a failed Add.
        MM_CHECK(countCalls(stub.callLog_, "AddApplicationManifest(") == 1);
        MM_CHECK(countCalls(stub.callLog_, "IsApplicationInstalled(") == 0);
        MM_CHECK(countCalls(stub.callLog_, "SetApplicationAutoLaunch(") == 0);

        std::cout << "PASS case_3_add_failed_short_circuit\n";
    }

    // ---- Case 4: ensureRegistered idempotent (already installed) ----
    {
        StubApplicationsSurface stub;
        stub.isInstalledResults_.push(true);

        auto reg = svr::createManifestRegistrarForTesting(stub, kAppKey, kManifestPath);
        MM_CHECK(reg != nullptr);
        svr::RegisterResult r = reg->ensureRegistered();
        MM_CHECK(r == svr::RegisterResult::Success);

        // Already installed -> no Add, no SetAutoLaunch (D-15).
        MM_CHECK(countCalls(stub.callLog_, "IsApplicationInstalled(") == 1);
        MM_CHECK(countCalls(stub.callLog_, "AddApplicationManifest(") == 0);
        MM_CHECK(countCalls(stub.callLog_, "SetApplicationAutoLaunch(") == 0);

        std::cout << "PASS case_4_ensure_registered_idempotent\n";
    }

    // ---- Case 5: unregisterApp ----
    {
        StubApplicationsSurface stub;
        stub.removeResults_.push(vr::VRApplicationError_None);

        auto reg = svr::createManifestRegistrarForTesting(stub, kAppKey, kManifestPath);
        MM_CHECK(reg != nullptr);
        svr::RegisterResult r = reg->unregisterApp();
        MM_CHECK(r == svr::RegisterResult::Success);

        MM_CHECK(countCalls(stub.callLog_, "RemoveApplicationManifest(") == 1);
        // Surface receives a UTF-8 path; just assert the exe-side filename
        // appears (avoids brittleness on the wide -> UTF-8 conversion site).
        MM_CHECK(stub.callLog_[0].find("app.vrmanifest") != std::string::npos);

        std::cout << "PASS case_5_unregister\n";
    }

    std::cout << "all tests passed\n";
    return 0;
}

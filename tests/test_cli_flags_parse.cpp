/**
 * @file test_cli_flags_parse.cpp
 * @brief RED unit tests for micmap::common::parseCliArgs (AUTO-06 / D-01).
 *
 * Phase 3 Plan 01 Task 1 (Wave 0 test scaffold). Verifies CLI flag parsing
 * for --register-vrmanifest / --unregister-vrmanifest / --minimized and the
 * silent-ignore policy for unknown flags.
 *
 * Header-location decision (per Plan 03-01 Task 1 instruction):
 *   The CliFlags struct + parseCliArgs() forward-declaration live at
 *   src/common/include/micmap/common/cli_flags.hpp. Plan 03-06 provides
 *   the implementation in apps/micmap (and links it into the micmap exe
 *   target — see Plan 03-06 for the .cpp compilation unit decision).
 *
 * RED state: until Plan 03-06 supplies parseCliArgs(), this test fails to
 * link with "unresolved external symbol parseCliArgs". That failure mode
 * is the expected Wave 0 evidence.
 *
 * Convention matches tests/test_config_manager.cpp: plain-main, exit code
 * 0 on all-pass, 1 on first failure, "FAIL: <expr> at line N" on stderr.
 */

#include "micmap/common/cli_flags.hpp"

#include <iostream>

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    using micmap::common::CliFlags;
    using micmap::common::parseCliArgs;

    // ---- case_1: --register-vrmanifest sets registerManifest, leaves others false ----
    {
        const wchar_t* argv[] = { L"micmap.exe", L"--register-vrmanifest" };
        CliFlags f = parseCliArgs(2, argv);
        MM_CHECK(f.registerManifest == true);
        MM_CHECK(f.unregisterManifest == false);
        MM_CHECK(f.minimized == false);
        std::cout << "PASS case_1_register\n";
    }

    // ---- case_2: --unregister-vrmanifest sets unregisterManifest only ----
    {
        const wchar_t* argv[] = { L"micmap.exe", L"--unregister-vrmanifest" };
        CliFlags f = parseCliArgs(2, argv);
        MM_CHECK(f.unregisterManifest == true);
        MM_CHECK(f.registerManifest == false);
        MM_CHECK(f.minimized == false);
        std::cout << "PASS case_2_unregister\n";
    }

    // ---- case_3: --minimized sets minimized only ----
    {
        const wchar_t* argv[] = { L"micmap.exe", L"--minimized" };
        CliFlags f = parseCliArgs(2, argv);
        MM_CHECK(f.minimized == true);
        MM_CHECK(f.registerManifest == false);
        MM_CHECK(f.unregisterManifest == false);
        std::cout << "PASS case_3_minimized\n";
    }

    // ---- case_4: --minimized + --register-vrmanifest both set (install post-run) ----
    {
        const wchar_t* argv[] = { L"micmap.exe", L"--minimized", L"--register-vrmanifest" };
        CliFlags f = parseCliArgs(3, argv);
        MM_CHECK(f.minimized == true);
        MM_CHECK(f.registerManifest == true);
        MM_CHECK(f.unregisterManifest == false);
        std::cout << "PASS case_4_combined\n";
    }

    // ---- case_5: unknown flag silently ignored, all defaults remain ----
    {
        const wchar_t* argv[] = { L"micmap.exe", L"--unknown" };
        CliFlags f = parseCliArgs(2, argv);
        MM_CHECK(f.registerManifest == false);
        MM_CHECK(f.unregisterManifest == false);
        MM_CHECK(f.minimized == false);
        std::cout << "PASS case_5_unknown_ignored\n";
    }

    // ---- case_6: no flags (argv0 only) — all defaults ----
    {
        const wchar_t* argv[] = { L"micmap.exe" };
        CliFlags f = parseCliArgs(1, argv);
        MM_CHECK(f.registerManifest == false);
        MM_CHECK(f.unregisterManifest == false);
        MM_CHECK(f.minimized == false);
        std::cout << "PASS case_6_no_flags\n";
    }

    std::cout << "all tests passed\n";
    return 0;
}

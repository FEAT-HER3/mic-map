/**
 * @file cli_flags.cpp
 * @brief parseCliArgs implementation for micmap.exe (Phase 3 AUTO-06 / D-01).
 *
 * Pure, side-effect-free argv-to-CliFlags translator. Consumed by:
 *   - apps/micmap/main.cpp WinMain (via CommandLineToArgvW at entry).
 *   - tests/test_cli_flags_parse.cpp (6 case contract from Plan 03-01).
 *
 * D-01: "wcscmp loop to set a flags struct {bool register_manifest,
 *        bool unregister_manifest, bool minimized}". Unknown flags are
 *        silently ignored (forward-compat with installer variants).
 */

#include "micmap/common/cli_flags.hpp"

#include <cwchar>

namespace micmap::common {

CliFlags parseCliArgs(int argc, const wchar_t* const* argv) {
    CliFlags flags{};
    if (!argv) return flags;
    // argv[0] is the executable path — start at index 1.
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (std::wcscmp(argv[i], L"--register-vrmanifest") == 0) {
            flags.registerManifest = true;
        } else if (std::wcscmp(argv[i], L"--unregister-vrmanifest") == 0) {
            flags.unregisterManifest = true;
        } else if (std::wcscmp(argv[i], L"--minimized") == 0) {
            flags.minimized = true;
        }
        // Unknown flags are silently ignored (D-01).
    }
    return flags;
}

} // namespace micmap::common

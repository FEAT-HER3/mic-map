/**
 * @file cli_flags.hpp
 * @brief Parsed command-line flags for micmap.exe (Phase 3 AUTO-06).
 *
 * Public contract published in Plan 03-01 (Wave 0 test scaffold) so the
 * RED test test_cli_flags_parse can be compiled+linked before Plan 03-06
 * lands the implementation in apps/micmap/main.cpp.
 *
 * D-01 (CONTEXT.md): "CLI argument parsing uses CommandLineToArgvW once at
 * WinMain entry, followed by a wcscmp loop to set a flags struct
 * {bool register_manifest, bool unregister_manifest, bool minimized}".
 *
 * The free function parseCliArgs() is the pure, side-effect-free piece of
 * that flow — it takes the already-converted argv from CommandLineToArgvW
 * and returns the populated CliFlags. Test test_cli_flags_parse exercises
 * this function directly without invoking CommandLineToArgvW.
 *
 * Plan 03-06 implements parseCliArgs() against this header. Until then the
 * symbol is unresolved and test_cli_flags_parse fails to link — the RED
 * state expected for Wave 0.
 */

#ifndef MICMAP_COMMON_CLI_FLAGS_HPP
#define MICMAP_COMMON_CLI_FLAGS_HPP

namespace micmap::common {

struct CliFlags {
    bool registerManifest   = false;  ///< --register-vrmanifest
    bool unregisterManifest = false;  ///< --unregister-vrmanifest
    bool minimized          = false;  ///< --minimized (silent auto-launch)
    bool patchBindings      = false;  ///< --patch-bindings (Phase 4 INST-08)
    bool unpatchBindings    = false;  ///< --unpatch-bindings (Phase 4 INST-08)
};

/**
 * Parse a wide-char argv vector (as produced by CommandLineToArgvW) into a
 * CliFlags struct. Unknown flags are silently ignored (D-01). argv[0] is
 * the executable path and is not inspected.
 *
 * Pure function: no logging, no globals, no I/O. Safe to call from tests.
 *
 * @param argc  Number of elements in argv. Must be >= 0.
 * @param argv  Array of wide-char C-strings. May be nullptr iff argc == 0.
 * @return      Populated CliFlags (defaults are all false).
 */
CliFlags parseCliArgs(int argc, const wchar_t* const* argv);

}  // namespace micmap::common

#endif  // MICMAP_COMMON_CLI_FLAGS_HPP

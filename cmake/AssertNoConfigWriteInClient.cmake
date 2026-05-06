# cmake/AssertNoConfigWriteInClient.cmake
#
# Phase 8 / IPC-05 / D-07: assert client TUs do not write config.json.
# Driver is sole writer; client edits flow through PUT /settings. Pitfall 5
# (file-watching from the driver) is REJECTED — single-writer is the only
# discipline that survives a torn-read race.
#
# Allowlist: read-only patterns (std::ifstream / fstream::read /
# std::getline / loadDefault / ConfigManagerImpl::load) are NOT flagged.
# The 4th and 5th conditions only fire when a literal "config.json" string
# coexists with a write-side syscall or a std::ofstream IN THE SAME TU. If
# a future client TU legitimately must read config.json with std::ifstream,
# that is allowed (only ofstream + write-syscalls trip the lint).
#
# Scope: apps/micmap/ + src/steamvr/. NOT in scope: src/core/ (the impl is
# fine; only client *callers* are forbidden) and apps/mic_test/ (headless
# harness retains saveDefault).
#
# Excluded files (08-05): apps/micmap/src/config_manager_impl.cpp — the
# v1.5 ConfigManagerImpl was relocated from src/core into apps/micmap/src
# in Plan 08-02 (D-02) to keep micmap_core JSON-free. The impl itself MUST
# retain saveDefault / writeAtomicWindows / ReplaceFileW because:
#   (a) IConfigManager::saveDefault is part of the public interface and
#       still consumed by mic_test.exe (out of scope for this lint);
#   (b) the implementation file is not a *caller* — the lint comment above
#       reads "only client *callers* are forbidden". P10 deletes the impl
#       entirely once configManager is removed from the client.
# Excluding this single file by name is more honest than weakening the
# regex; any new client TU that calls these helpers is still flagged.
#
# RED-tolerant by GLOB_RECURSE form: lint stays clean if no client TU has
# yet been migrated; fires the moment any client TU writes the file. Wave 0
# deliberately deferred ctest registration — Plan 08-05 deletes the last
# saveDefault() callsites in apps/micmap/main.cpp and first_launch_balloon.cpp,
# then registers this lint as a ctest in tests/CMakeLists.txt.
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertNoConfigWriteInClient
#       COMMAND ${CMAKE_COMMAND}
#           -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>$<SEMICOLON>...
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoConfigWriteInClient.cmake)

if(NOT DEFINED CLIENT_ROOTS)
    message(FATAL_ERROR "AssertNoConfigWriteInClient: CLIENT_ROOTS not provided. "
        "Pass -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_violations "")
set(_files_scanned 0)

# Narrower extension set than lint_no_openvr_in_core: client production code
# only (.cpp + .hpp). External / vendored headers in third-party trees are
# not a concern because client roots scope down to apps/micmap and
# src/steamvr.
foreach(_root ${CLIENT_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "AssertNoConfigWriteInClient: CLIENT_ROOTS entry is not a directory: ${_root}")
    endif()
    file(GLOB_RECURSE _files
        "${_root}/*.cpp" "${_root}/*.hpp")
    foreach(_file ${_files})
        # P8 08-05 excluded files: the relocated ConfigManagerImpl is the impl,
        # not a caller. Skip by exact basename. Any new TU adopting the impl
        # patterns under a different filename is still flagged.
        get_filename_component(_basename "${_file}" NAME)
        if(_basename STREQUAL "config_manager_impl.cpp")
            continue()
        endif()

        math(EXPR _files_scanned "${_files_scanned} + 1")
        file(READ "${_file}" _content)
        # Five-condition disjunction (per checker fix). The first three
        # catch the named v1.5 helpers; the last two catch any TU that
        # combines a `config.json` string literal with a write-side syscall
        # or a std::ofstream. Read-only stream creation is intentionally
        # not flagged.
        if(_content MATCHES "saveDefault\\(\\)"
                OR _content MATCHES "writeAtomicWindows"
                OR _content MATCHES "ReplaceFileW"
                OR (_content MATCHES "config\\.json"
                    AND _content MATCHES "fopen|CreateFileW|MoveFileExW|WriteFile|fwrite")
                OR (_content MATCHES "std::ofstream"
                    AND _content MATCHES "config\\.json"))
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoConfigWriteInClient: ${_vcount} file(s) violate the single-writer rule (IPC-05 / D-07):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH CLIENT_ROOTS _rootcount)
message(STATUS "AssertNoConfigWriteInClient: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

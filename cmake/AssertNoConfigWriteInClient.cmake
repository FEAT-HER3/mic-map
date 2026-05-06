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
# RED-tolerant by GLOB_RECURSE form: lint stays clean if no client TU has
# yet been migrated; fires the moment any client TU writes the file. Wave 0
# deliberately does NOT register this lint as a ctest yet — it currently
# fires on apps/micmap/main.cpp:498 (configManager->saveDefault()) which
# Plan 08-04 deletes; ctest registration lands then.
#
# Invocation (from tests/CMakeLists.txt at 08-04):
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

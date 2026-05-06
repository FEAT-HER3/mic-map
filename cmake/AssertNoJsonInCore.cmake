# cmake/AssertNoJsonInCore.cmake
#
# Phase 8 / D-02: source-grep lint that fails the build (via CTest) if any
# nlohmann/json header include appears under the four shared-lib roots.
# Mirrors the shape of cmake/lint_no_openvr_in_core.cmake (P5 D-02), but
# narrows the regex to <nlohmann/json.hpp> only. Pitfall 15 mitigation:
# json lives in driver TUs only — never in the shared core/audio/detection/
# common layers, so mic_test stays free of nlohmann symbols.
#
# RED-tolerant by virtue of the GLOB_RECURSE form: when no shared-lib TU
# yet includes <nlohmann/json.hpp>, the lint stays green; the moment one
# does, the lint fires. Wave 0 deliberately does NOT register this lint as
# a ctest yet (see tests/CMakeLists.txt) — it gets registered in Plan 08-02
# alongside the JSON-in-driver lift, AFTER src/core/src/config_manager.cpp
# is migrated off the shared layer.
#
# Invocation (from tests/CMakeLists.txt at 08-02):
#   add_test(NAME AssertNoJsonInCore
#       COMMAND ${CMAKE_COMMAND}
#           -DSRC_ROOTS=<root1>$<SEMICOLON><root2>$<SEMICOLON>...
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoJsonInCore.cmake)

if(NOT DEFINED SRC_ROOTS)
    message(FATAL_ERROR "AssertNoJsonInCore: SRC_ROOTS not provided. "
        "Pass -DSRC_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_violations "")
set(_files_scanned 0)

# Single regex condition (vs. the 3 OpenVR ones in lint_no_openvr_in_core):
# the json header has exactly one canonical include form:
#   #include <nlohmann/json.hpp>     or     #include "nlohmann/json.hpp"
# Anchored to that exact path so unrelated identifiers (e.g. a hypothetical
# `nlohmann_json_helpers` symbol) do not false-positive.
foreach(_root ${SRC_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "AssertNoJsonInCore: SRC_ROOTS entry is not a directory: ${_root}")
    endif()
    file(GLOB_RECURSE _files
        "${_root}/*.h"   "${_root}/*.hpp" "${_root}/*.hxx"
        "${_root}/*.c"   "${_root}/*.cpp" "${_root}/*.cc"
        "${_root}/*.cxx" "${_root}/*.inc" "${_root}/*.ipp")
    foreach(_file ${_files})
        math(EXPR _files_scanned "${_files_scanned} + 1")
        file(READ "${_file}" _content)
        if(_content MATCHES "[<\"]nlohmann/json\\.hpp[>\"]")
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoJsonInCore: ${_vcount} file(s) violate the no-json-in-shared-layer rule:")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH SRC_ROOTS _rootcount)
message(STATUS "AssertNoJsonInCore: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

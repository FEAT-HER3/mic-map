# cmake/AssertCoVersioning.cmake
#
# Phase 10 / INST-09 / D-21: assert single-version source-of-truth.
# cmake/version.cmake defines MICMAP_VERSION (semver MAJOR.MINOR.PATCH);
# the same value must appear in the generated installer/version.iss
# (`#define MICMAP_VERSION "X.Y.Z"`). Driver and client targets receive
# MICMAP_VERSION_STRING via target_compile_definitions from the same
# SSoT — drift across these surfaces is the failure mode INST-09 closes.
#
# Wave 0 RED-tolerant via skip-on-NOT-EXISTS at each gate (mirrors
# cmake/AssertReplayNoVrApi.cmake shape). Plumbing lands in 10-01
# (cmake/version.cmake + installer/version.iss.in); full enforcement
# (installer wiring + binary-side define matching) lands in 10-06.
#
# Five gated assertions (FATAL on first hard violation; STATUS skip on
# missing prerequisite files until they land):
#   1. EXISTS cmake/version.cmake — Wave 0: skip-on-NOT-EXISTS.
#   2. After include, MICMAP_VERSION is defined and matches semver
#      ^[0-9]+\.[0-9]+\.[0-9]+$ (no pre-release suffix in v1.6).
#   3. EXISTS installer/version.iss.in — Wave 0: skip-on-NOT-EXISTS.
#   4. EXISTS generated installer/version.iss — skip-on-NOT-EXISTS until
#      cmake configure has produced it via configure_file.
#   5. The MICMAP_VERSION baked into installer/version.iss matches the
#      cmake-side MICMAP_VERSION (regex extract `#define MICMAP_VERSION
#      "([^"]+)"`, string-compare).
#
# A future refinement (deferred to backlog) could also assert the value
# is identical to the MICMAP_VERSION_STRING baked into the driver/client
# target object files — that requires running the build first and grepping
# the binaries. For Wave 0 + 10-06 enforcement, the 5 assertions above are
# sufficient to catch the most likely drift modes.
#
# T-10-00-03 mitigation: all assertions reference fixed paths under
# ${SOURCE_DIR}; no GLOB; configure-time only. An attacker who can write
# cmake/version.cmake already has repo write access (separate concern).
#
# Invocation (from tests/CMakeLists.txt — wired NOW at Wave 0):
#   add_test(NAME AssertCoVersioning
#       COMMAND ${CMAKE_COMMAND}
#           -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertCoVersioning.cmake)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "AssertCoVersioning: SOURCE_DIR not provided. "
        "Pass -DSOURCE_DIR=<repo root>")
endif()

set(_version_cmake "${SOURCE_DIR}/cmake/version.cmake")
set(_version_iss_in "${SOURCE_DIR}/installer/version.iss.in")
set(_version_iss "${SOURCE_DIR}/installer/version.iss")
set(_violations "")

# Assertion 1: cmake/version.cmake must exist (Wave 0: skip-on-NOT-EXISTS).
if(NOT EXISTS "${_version_cmake}")
    message(STATUS "AssertCoVersioning: skipped (cmake/version.cmake does not exist yet — Wave 0 RED-tolerant; 10-01 lands the SSoT)")
    return()
endif()

# Assertion 2: include and confirm MICMAP_VERSION is defined + semver-shaped.
include("${_version_cmake}")
if(NOT DEFINED MICMAP_VERSION)
    list(APPEND _violations "cmake/version.cmake does not define MICMAP_VERSION")
else()
    string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+$" _vmatch "${MICMAP_VERSION}")
    if(NOT _vmatch)
        list(APPEND _violations
            "cmake/version.cmake: MICMAP_VERSION='${MICMAP_VERSION}' does not match MAJOR.MINOR.PATCH (no pre-release suffix in v1.6)")
    endif()
endif()

# Assertion 3: installer/version.iss.in must exist (Wave 0: skip-on-NOT-EXISTS).
if(NOT EXISTS "${_version_iss_in}")
    message(STATUS "AssertCoVersioning: cmake/version.cmake OK (MICMAP_VERSION=${MICMAP_VERSION}); installer/version.iss.in does not exist yet — Wave 0 RED-tolerant; 10-01 lands the template")
    return()
endif()

# Assertion 4: generated installer/version.iss must exist (configure_file output).
if(NOT EXISTS "${_version_iss}")
    message(STATUS "AssertCoVersioning: cmake/version.cmake + version.iss.in OK; generated installer/version.iss not present — run cmake configure to produce it")
    return()
endif()

# Assertion 5: the MICMAP_VERSION baked into installer/version.iss matches cmake-side.
file(READ "${_version_iss}" _iss_content)
string(REGEX MATCH "#define[ \t]+MICMAP_VERSION[ \t]+\"([^\"]+)\"" _ver_match "${_iss_content}")
if(NOT _ver_match)
    list(APPEND _violations
        "installer/version.iss does not contain '#define MICMAP_VERSION \"...\"' line (configure_file failure?)")
else()
    set(_iss_version "${CMAKE_MATCH_1}")
    if(NOT "${_iss_version}" STREQUAL "${MICMAP_VERSION}")
        list(APPEND _violations
            "installer/version.iss MICMAP_VERSION='${_iss_version}' does not match cmake/version.cmake MICMAP_VERSION='${MICMAP_VERSION}' — drift detected")
    endif()
endif()

list(LENGTH _violations _vcount)
if(_vcount GREATER 0)
    set(_msg "AssertCoVersioning: ${_vcount} co-versioning violation(s) (P10 D-21 / INST-09):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertCoVersioning: clean (MICMAP_VERSION=${MICMAP_VERSION}; cmake / version.iss in sync)")

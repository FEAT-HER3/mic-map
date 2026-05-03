# cmake/AssertDetectionRunnerNoVrApi.cmake
#
# Phase 7 / D-22 / SVR-05: source-grep lint that fails the build (via CTest)
# if any vr::* symbol use or OpenVR header include appears under
# driver/src/detection_runner.{hpp,cpp} or the SPSC ring header. The
# detection thread MUST NOT call any OpenVR API (Pitfall 3); the SPSC ring
# is a pure data primitive with no driver-context awareness.
#
# Sibling of cmake/AssertAudioWorkerNoVrApi.cmake (per RESEARCH.md
# §"Standard Stack" alternatives — sibling keeps blast radius small; one
# lint failure does not take down all driver-TU lints; scales to P10
# deletions cleanly). Mirrors the regex set and aggregation shape of
# cmake/lint_no_openvr_in_core.cmake (P5 D-02), narrowed to a three-file
# explicit list. Wave 0 RED-tolerant: when the detection_runner source
# files do not yet exist (before Plan 07-03 lands them), the lint exits 0
# cleanly so ctest stays green during Wave 0. Once the files exist, the
# lint scans them and stays green because they contain no `vr::*` and no
# `<openvr*.h>` includes (D-22).
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertDetectionRunnerNoVrApi
#       COMMAND ${CMAKE_COMMAND}
#           -DDETECTION_RUNNER_DIR=${CMAKE_SOURCE_DIR}/driver/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertDetectionRunnerNoVrApi.cmake)

if(NOT DEFINED DETECTION_RUNNER_DIR)
    message(FATAL_ERROR "AssertDetectionRunnerNoVrApi: DETECTION_RUNNER_DIR not provided. "
        "Pass -DDETECTION_RUNNER_DIR=<dir-containing-detection_runner-and-ring-headers>")
endif()

# Three-file explicit list — narrower than lint_no_openvr_in_core's GLOB_RECURSE.
# The lint scope is intentionally limited to the detection translation units
# plus the SPSC ring header; DriverLog lives in driver_log.hpp (not under
# detection_runner.* or the ring header) so a stray <openvr_driver.h>
# include there would fire the regex — exactly what we want.
set(_targets
    "${DETECTION_RUNNER_DIR}/detection_runner.hpp"
    "${DETECTION_RUNNER_DIR}/detection_runner.cpp"
    "${DETECTION_RUNNER_DIR}/sample_ring.hpp")

set(_violations "")
set(_files_scanned 0)

# Mirror lint_no_openvr_in_core.cmake regex set byte-for-byte:
#   - "[<\"]openvr[a-z_]*\\.h[>\"]"   any quoted/angle openvr*.h include
#   - "[^a-zA-Z0-9_]vr::"             vr:: preceded by a non-identifier char
#   - "^vr::"                         vr:: at start of file
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip: file not yet authored (detection_runner.{hpp,cpp}
        # land in Plan 07-03). Skipping keeps ctest green during Wave 0 — the
        # DetectionSettingsPropagation / DeviceProviderLifecycleStress tests are
        # the build-time RED gate, not this lint. Once the file lands, this
        # branch falls through and the regex scan runs.
        continue()
    endif()
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    if(_content MATCHES "[<\"]openvr[a-z_]*\\.h[>\"]"
            OR _content MATCHES "[^a-zA-Z0-9_]vr::"
            OR _content MATCHES "^vr::")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertDetectionRunnerNoVrApi: ${_vcount} file(s) violate the no-vr::-in-detection rule (D-22 / Pitfall 3):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertDetectionRunnerNoVrApi: clean (${_files_scanned} files scanned)")

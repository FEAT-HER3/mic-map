# cmake/AssertAudioWorkerNoVrApi.cmake
#
# Phase 6 / D-07 / SVR-05: source-grep lint that fails the build (via CTest)
# if any vr::* symbol use or OpenVR header include appears under
# driver/src/audio_worker.{hpp,cpp}. The audio worker thread MUST NOT call
# any OpenVR API (Pitfall 3); only DriverLog (which is null-safe and lives
# in driver_log.hpp, not under audio_worker.*) is permitted.
#
# Mirrors the regex set and aggregation shape of cmake/lint_no_openvr_in_core.cmake
# (P5 D-02), narrowed to a two-file explicit list. Wave 0 RED-tolerant: when the
# audio_worker source files do not yet exist (before Plan 06-02 lands them), the
# lint exits 0 cleanly so ctest stays green during Wave 0. Once the files exist,
# the lint scans them and stays green because they contain no `vr::*` and no
# `<openvr*.h>` includes (D-07).
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertAudioWorkerNoVrApi
#       COMMAND ${CMAKE_COMMAND}
#           -DAUDIO_WORKER_DIR=${CMAKE_SOURCE_DIR}/driver/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertAudioWorkerNoVrApi.cmake)

if(NOT DEFINED AUDIO_WORKER_DIR)
    message(FATAL_ERROR "AssertAudioWorkerNoVrApi: AUDIO_WORKER_DIR not provided. "
        "Pass -DAUDIO_WORKER_DIR=<dir-containing-audio_worker.{hpp,cpp}>")
endif()

# Two-file explicit list — narrower than lint_no_openvr_in_core's GLOB_RECURSE.
# The lint scope is intentionally limited to the audio worker translation unit;
# DriverLog lives in driver_log.hpp (not under audio_worker.*) so a stray
# <openvr_driver.h> include there would fire the regex — exactly what we want.
set(_targets
    "${AUDIO_WORKER_DIR}/audio_worker.hpp"
    "${AUDIO_WORKER_DIR}/audio_worker.cpp")

set(_violations "")
set(_files_scanned 0)

# Mirror lint_no_openvr_in_core.cmake regex set byte-for-byte:
#   - "[<\"]openvr[a-z_]*\\.h[>\"]"   any quoted/angle openvr*.h include
#   - "[^a-zA-Z0-9_]vr::"             vr:: preceded by a non-identifier char
#   - "^vr::"                         vr:: at start of file
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip: file not yet authored (audio_worker.{hpp,cpp}
        # land in Plan 06-02). Skipping keeps ctest green during Wave 0 — the
        # AudioWorkerLifecycleHeadless test is the build-time RED gate, not this
        # lint. Once the file lands, this branch falls through and the regex
        # scan runs.
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
    set(_msg "AssertAudioWorkerNoVrApi: ${_vcount} file(s) violate the no-vr::-in-audio-worker rule (D-07 / Pitfall 3):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertAudioWorkerNoVrApi: clean (${_files_scanned} files scanned)")

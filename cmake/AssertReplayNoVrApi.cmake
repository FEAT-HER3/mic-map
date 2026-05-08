# cmake/AssertReplayNoVrApi.cmake
#
# Phase 9 / TEST-01 / D-37: assert no OpenVR symbols in
# apps/mic_test/src/wav_replay.{hpp,cpp}. Replay harness must remain
# headless — TEST-01 invariant. RED-tolerant via skip-on-NOT-EXISTS
# (passes Wave 0 before wav_replay TUs exist).
#
# Sibling of cmake/AssertDetectionRunnerNoVrApi.cmake (P7 D-22 / SVR-05).
# Mirrors the regex set and aggregation shape — narrowed to a two-file
# explicit list. Wave 0 RED-tolerant: when the wav_replay source files do
# not yet exist (before Plan 09-04 lands them), the lint exits 0 cleanly
# so ctest stays green during Wave 0. Once the files exist, the lint scans
# them and stays green so long as the replay harness avoids OpenVR symbols
# (the canonical invariant — replay must run on the same host CI workers
# that have no SteamVR runtime).
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertReplayNoVrApi
#       COMMAND ${CMAKE_COMMAND}
#           -DREPLAY_DIR=${CMAKE_SOURCE_DIR}/apps/mic_test/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertReplayNoVrApi.cmake)

if(NOT DEFINED REPLAY_DIR)
    message(FATAL_ERROR "AssertReplayNoVrApi: REPLAY_DIR not provided. "
        "Pass -DREPLAY_DIR=<dir-containing-wav_replay-sources>")
endif()

# Two-file explicit list — narrower than lint_no_openvr_in_core's
# GLOB_RECURSE. The lint scope is intentionally limited to the WAV replay
# translation units per CONTEXT D-37; broadening to mic_test/main.cpp
# would conflict with TEST-01's existing OpenVR-aware harness paths.
set(_targets
    "${REPLAY_DIR}/wav_replay.hpp"
    "${REPLAY_DIR}/wav_replay.cpp")

set(_violations "")
set(_files_scanned 0)

# Mirror lint_no_openvr_in_core.cmake / AssertDetectionRunnerNoVrApi.cmake
# regex set byte-for-byte:
#   - "[<\"]openvr[a-z_]*\\.h[>\"]"   any quoted/angle openvr*.h include
#   - "[^a-zA-Z0-9_]vr::"             vr:: preceded by a non-identifier char
#   - "^vr::"                         vr:: at start of file
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip: file not yet authored (wav_replay.{hpp,cpp}
        # land in Plan 09-04). Skipping keeps ctest green during Wave 0 — the
        # WavReplayHarness test is the build-time RED gate, not this lint.
        # Once the file lands, this branch falls through and the regex scan
        # runs.
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
    set(_msg "AssertReplayNoVrApi: ${_vcount} file(s) violate the no-vr::-in-replay-harness rule (P9 D-37 / TEST-01):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertReplayNoVrApi: clean (${_files_scanned} files scanned)")

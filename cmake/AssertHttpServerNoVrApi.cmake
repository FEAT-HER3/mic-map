# cmake/AssertHttpServerNoVrApi.cmake
#
# Phase 8 / D-24 / SVR-05: assert no vr::* in driver/src/http_server.{hpp,cpp}.
# HTTP thread must never call OpenVR API (Pitfall 3). All OpenVR mutations
# flow through CommandQueue -> RunFrame; HTTP handlers only push commands or
# read atomic snapshots. Sibling of AssertDetectionRunnerNoVrApi.cmake
# (P7 D-22) — same regex set, different file list.
#
# Wave 0 RED-tolerant via skip-on-NOT-EXISTS. Currently GREEN — existing
# http_server.cpp includes only command_queue.hpp + driver_log.hpp +
# httplib.h + nlohmann/json.hpp; no vr:: surface.
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertHttpServerNoVrApi
#       COMMAND ${CMAKE_COMMAND}
#           -DHTTP_SERVER_DIR=${CMAKE_SOURCE_DIR}/driver/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertHttpServerNoVrApi.cmake)

if(NOT DEFINED HTTP_SERVER_DIR)
    message(FATAL_ERROR "AssertHttpServerNoVrApi: HTTP_SERVER_DIR not provided. "
        "Pass -DHTTP_SERVER_DIR=<dir-containing-http_server-headers-and-source>")
endif()

# Two-file explicit list (no third sibling — http_server is the only HTTP
# TU). driver_log.hpp legitimately includes <openvr_driver.h>, so it is NOT
# in scope; the discipline is "http_server.* never reaches for vr::".
set(_targets
    "${HTTP_SERVER_DIR}/http_server.hpp"
    "${HTTP_SERVER_DIR}/http_server.cpp")

set(_violations "")
set(_files_scanned 0)

# Mirror lint_no_openvr_in_core.cmake regex set byte-for-byte:
#   - "[<\"]openvr[a-z_]*\\.h[>\"]"   any quoted/angle openvr*.h include
#   - "[^a-zA-Z0-9_]vr::"             vr:: preceded by a non-identifier char
#   - "^vr::"                         vr:: at start of file
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip.
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
    set(_msg "AssertHttpServerNoVrApi: ${_vcount} file(s) violate the no-vr::-in-http-server rule (D-24 / SVR-05 / Pitfall 3):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertHttpServerNoVrApi: clean (${_files_scanned} files scanned)")

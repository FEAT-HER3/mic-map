# cmake/AssertHttpServerLocalhostOnly.cmake
#
# Phase 8 / IPC-07 / Pitfall 7: assert HTTP server binds 127.0.0.1 only —
# never 0.0.0.0 or INADDR_ANY. Driver localhost-only invariant; client never
# routes to a remote driver and no firewall rules are needed.
#
# Sibling of cmake/AssertDetectionRunnerNoVrApi.cmake (explicit-target-list,
# Wave 0 RED-tolerant via skip-on-NOT-EXISTS). Narrows scope to the two
# files that actually own the HTTP listen socket.
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME AssertHttpServerLocalhostOnly
#       COMMAND ${CMAKE_COMMAND}
#           -DHTTP_SERVER_DIR=${CMAKE_SOURCE_DIR}/driver/src
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertHttpServerLocalhostOnly.cmake)

if(NOT DEFINED HTTP_SERVER_DIR)
    message(FATAL_ERROR "AssertHttpServerLocalhostOnly: HTTP_SERVER_DIR not provided. "
        "Pass -DHTTP_SERVER_DIR=<dir-containing-http_server-headers-and-source>")
endif()

# Two-file explicit list; no third sibling (cf. AssertDetectionRunnerNoVrApi
# which scans the SPSC ring header alongside the runner). HttpServer is the
# only TU that touches httplib::Server's listen / bind_to_port surface.
set(_targets
    "${HTTP_SERVER_DIR}/http_server.hpp"
    "${HTTP_SERVER_DIR}/http_server.cpp")

set(_violations "")
set(_files_scanned 0)

# Two-pass strategy. The .hpp typically owns the default host literal
# ("127.0.0.1"); the .cpp owns the bind_to_port call. Scanning each file
# in isolation produces a false positive on the .cpp (calls bind_to_port
# but never names 127.0.0.1). Treat the two files as a single unit for
# the bind_to_port-without-127.0.0.1 check, but still flag any file that
# contains a wildcard literal (0.0.0.0 / INADDR_ANY) on its own.
set(_combined "")
foreach(_file ${_targets})
    if(NOT EXISTS "${_file}")
        # Wave 0 RED-tolerant skip: file may not yet exist (or may have
        # been renamed in a later wave). Skipping keeps ctest green during
        # Wave 0.
        continue()
    endif()
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    string(APPEND _combined "${_content}\n")
    # Per-file checks: wildcard literals are always a violation regardless
    # of which file mentions them.
    if(_content MATCHES "0\\.0\\.0\\.0"
            OR _content MATCHES "INADDR_ANY")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

# Combined check: bind_to_port must be paired with the localhost literal
# somewhere in the http_server pair (header default + impl call site).
if(_combined MATCHES "bind_to_port" AND NOT _combined MATCHES "127\\.0\\.0\\.1")
    # Attribute the violation to the .cpp (bind_to_port call site) since
    # that's where the burden of proof lives.
    list(APPEND _violations "${HTTP_SERVER_DIR}/http_server.cpp")
endif()
list(REMOVE_DUPLICATES _violations)

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertHttpServerLocalhostOnly: ${_vcount} file(s) violate the localhost-only-bind rule (IPC-07 / Pitfall 7):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertHttpServerLocalhostOnly: clean (${_files_scanned} files scanned)")

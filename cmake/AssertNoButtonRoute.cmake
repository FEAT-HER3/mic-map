# cmake/AssertNoButtonRoute.cmake
#
# Phase 10 / MIG-05 / D-23: assert POST /button route deleted from
# driver/src/ AND IDriverApi::tap() / .tap() / ->tap() / ::tap call
# surfaces deleted from src/steamvr/. Mirrors v1.5 SVR-04 rip-out: dual
# runtime collapsed; the only producer of the trigger CommandQueue is
# DetectionRunner (driver-resident, P7) plus the debug-build-only
# POST /debug/trigger (10-04). Wave 0 RED-tolerant: lint script ships now;
# ctest registration go-live is 10-05 alongside the route+method+callsite
# deletions (single atomic commit).
#
# Sibling of cmake/AssertNoConfigWriteInClient.cmake (per-file forbidden-
# pattern regex via GLOB_RECURSE) extended with a SECOND scope arg so the
# two distinct concerns (HTTP route registration in driver/src/ vs.
# IDriverApi::tap call surface in src/steamvr/) are scanned with their own
# regex sets. One shared `_violations` list aggregates findings; one
# shared `_files_scanned` math counter for the STATUS clean line.
#
# Qualifier-prefix narrowing for the steamvr scan: the regexes `\.tap\(\)`
# / `->tap\(\)` / `::tap[^a-zA-Z0-9_]` match the v1.5 IDriverApi::tap call
# surface. The `::tap` form has its trailing identifier boundary so future
# legitimately-named `tapSomethingElse` methods would not false-positive.
# A bare-token `tap` regex would over-fire on unrelated identifiers.
#
# Two pinned scope args are enforced by the FATAL_ERROR check below; the
# regex sets are hardcoded. An attacker cannot silently broaden the scope
# without also editing this file (T-10-00-02 mitigation).
#
# Invocation (from tests/CMakeLists.txt — wired in 10-05):
#   add_test(NAME AssertNoButtonRoute
#       COMMAND ${CMAKE_COMMAND}
#           -DBUTTON_ROUTE_ROOT=${CMAKE_SOURCE_DIR}/driver/src
#           -DSTEAMVR_ROOT=${CMAKE_SOURCE_DIR}/src/steamvr
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoButtonRoute.cmake)

if(NOT DEFINED BUTTON_ROUTE_ROOT)
    message(FATAL_ERROR "AssertNoButtonRoute: BUTTON_ROUTE_ROOT not provided. "
        "Pass -DBUTTON_ROUTE_ROOT=<driver/src dir>")
endif()
if(NOT DEFINED STEAMVR_ROOT)
    message(FATAL_ERROR "AssertNoButtonRoute: STEAMVR_ROOT not provided. "
        "Pass -DSTEAMVR_ROOT=<src/steamvr dir>")
endif()
if(NOT IS_DIRECTORY "${BUTTON_ROUTE_ROOT}")
    message(FATAL_ERROR "AssertNoButtonRoute: BUTTON_ROUTE_ROOT is not a directory: ${BUTTON_ROUTE_ROOT}")
endif()
if(NOT IS_DIRECTORY "${STEAMVR_ROOT}")
    message(FATAL_ERROR "AssertNoButtonRoute: STEAMVR_ROOT is not a directory: ${STEAMVR_ROOT}")
endif()

set(_violations "")
set(_files_scanned 0)

# Scan 1 — BUTTON_ROUTE_ROOT (driver/src/): forbid POST /button
# registration. The two regex variants cover the cpp-httplib idioms
# `srv.Post("/button", ...)` and the bare `Post("/button", ...)` call form.
file(GLOB_RECURSE _br_files
    "${BUTTON_ROUTE_ROOT}/*.cpp"
    "${BUTTON_ROUTE_ROOT}/*.hpp")
foreach(_file ${_br_files})
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    if(_content MATCHES "Post\\(\"/button\""
            OR _content MATCHES "srv\\.Post\\(\"/button\"")
        list(APPEND _violations "${_file} (POST /button registration)")
    endif()
endforeach()

# Scan 2 — STEAMVR_ROOT (src/steamvr/): forbid IDriverApi::tap call
# surface. Three regex variants cover the member-call forms `.tap()` and
# `->tap()` (most call sites) and the qualified-name form `::tap` with a
# trailing identifier boundary so `tapSomethingElse` does not false-positive.
file(GLOB_RECURSE _sv_files
    "${STEAMVR_ROOT}/*.cpp"
    "${STEAMVR_ROOT}/*.hpp")
foreach(_file ${_sv_files})
    math(EXPR _files_scanned "${_files_scanned} + 1")
    file(READ "${_file}" _content)
    if(_content MATCHES "\\.tap\\(\\)"
            OR _content MATCHES "->tap\\(\\)"
            OR _content MATCHES "::tap[^a-zA-Z0-9_]")
        list(APPEND _violations "${_file} (IDriverApi::tap call/decl)")
    endif()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoButtonRoute: ${_vcount} file(s) violate the rip-out invariants (P10 D-01 / MIG-05):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

message(STATUS "AssertNoButtonRoute: clean (${_files_scanned} files scanned across BUTTON_ROUTE_ROOT + STEAMVR_ROOT)")

# cmake/AssertNoOpenVRInCore.cmake
#
# Phase 5 / LIB-03: configure-time guard that fails CMake configure if any
# target reachable from micmap_core_runtime links openvr_api (case-insensitive
# name match) or propagates an INTERFACE_INCLUDE_DIRECTORIES entry whose path
# contains 'openvr'. Walks INTERFACE_LINK_LIBRARIES + LINK_LIBRARIES; resolves
# ALIASED_TARGET; idempotent visited-set guard; tolerates missing imported
# targets (Pitfall 5-B) and orderings (Pitfall 5-C — see assertion below).
#
# Contract (D-01 / D-04):
#   - Configure-time fail beats build-time (every developer sees it on first
#     `cmake -B build`, not just CI).
#   - Must be include()d AFTER add_subdirectory(src) so micmap_core_runtime
#     is defined before the walk runs (Pitfall 5-C).

# Sanity guard (Pitfall 5-C): caller forgot to define the runtime target.
if(NOT TARGET micmap_core_runtime)
    message(FATAL_ERROR
        "AssertNoOpenVRInCore: micmap_core_runtime is not defined yet — "
        "include() this module AFTER add_subdirectory(src).")
endif()

function(_assert_no_openvr_recurse target visited_var)
    # (a) Bare library names / missing imports are leaves — nothing to walk.
    if(NOT TARGET ${target})
        return()
    endif()

    # (b/c) Idempotent visited-set; DAG-safe.
    list(FIND ${visited_var} ${target} _seen)
    if(NOT _seen EQUAL -1)
        return()
    endif()
    list(APPEND ${visited_var} ${target})
    set(${visited_var} ${${visited_var}} PARENT_SCOPE)

    # (d) Resolve ALIAS targets — property reads on aliases are restricted
    # in some CMake versions (Pitfall 5-A). CMake 3.18+ permits ALIAS-of-
    # ALIAS, so iterate until ALIASED_TARGET is empty (WR-02).
    set(_resolved ${target})
    while(TRUE)
        get_target_property(_alias ${_resolved} ALIASED_TARGET)
        if(NOT _alias)
            break()
        endif()
        set(_resolved ${_alias})
    endwhile()

    # (e) Forbidden by name (case-insensitive 'openvr' substring).
    string(TOLOWER "${_resolved}" _lc)
    if(_lc MATCHES "openvr")
        message(FATAL_ERROR
            "AssertNoOpenVRInCore: target '${_resolved}' is reachable from "
            "micmap_core_runtime — OpenVR must not leak into the shared layer.")
    endif()

    # (f) Forbidden include-path propagation.
    get_target_property(_incs ${_resolved} INTERFACE_INCLUDE_DIRECTORIES)
    if(_incs)
        foreach(_inc ${_incs})
            string(TOLOWER "${_inc}" _inc_lc)
            if(_inc_lc MATCHES "openvr")
                message(FATAL_ERROR
                    "AssertNoOpenVRInCore: target '${_resolved}' propagates "
                    "include dir '${_inc}' (matches 'openvr').")
            endif()
        endforeach()
    endif()

    # (g) Recurse over both INTERFACE_LINK_LIBRARIES and LINK_LIBRARIES.
    foreach(_prop INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
        get_target_property(_deps ${_resolved} ${_prop})
        if(_deps)
            foreach(_dep ${_deps})
                # Skip $<...> generator expressions and $<LINK_ONLY:...> wrappers.
                if(_dep MATCHES "^\\$<")
                    continue()
                endif()
                _assert_no_openvr_recurse(${_dep} ${visited_var})
                set(${visited_var} ${${visited_var}} PARENT_SCOPE)
            endforeach()
        endif()
    endforeach()
endfunction()

set(_visited "")
_assert_no_openvr_recurse(micmap_core_runtime _visited)
list(LENGTH _visited _visited_count)
message(STATUS "AssertNoOpenVRInCore: clean (visited ${_visited_count} targets)")

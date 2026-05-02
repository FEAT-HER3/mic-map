# cmake/lint_no_driver_macro.cmake
#
# Phase 5 / D-12 / SC4: source-grep lint that fails the build (via CTest)
# if MICMAP_DRIVER_BUILD (or any host-switching macro by that exact name)
# appears under the four shared-lib roots. Locks the zero-hits invariant
# verified at 2026-05-01.
#
# Invocation: identical to lint_no_openvr_in_core.cmake, with -P pointing
# at this file.

if(NOT DEFINED SRC_ROOTS)
    message(FATAL_ERROR "lint_no_driver_macro: SRC_ROOTS not provided. "
        "Pass -DSRC_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_extensions "*.h" "*.hpp" "*.hxx" "*.c" "*.cpp" "*.cc" "*.cxx" "*.inc" "*.ipp")
set(_violations "")
set(_files_scanned 0)

foreach(_root ${SRC_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "lint_no_driver_macro: SRC_ROOTS entry is not a directory: ${_root}")
    endif()
    foreach(_ext ${_extensions})
        file(GLOB_RECURSE _files "${_root}/${_ext}")
        foreach(_file ${_files})
            math(EXPR _files_scanned "${_files_scanned} + 1")
            file(READ "${_file}" _content)
            if(_content MATCHES "MICMAP_DRIVER_BUILD")
                list(APPEND _violations "${_file}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "lint_no_driver_macro: ${_vcount} file(s) reference MICMAP_DRIVER_BUILD inside the shared layer:")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH SRC_ROOTS _rootcount)
message(STATUS "lint_no_driver_macro: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

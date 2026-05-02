# cmake/lint_no_openvr_in_core.cmake
#
# Phase 5 / D-02: source-grep lint that fails the build (via CTest) if any
# OpenVR header or vr:: namespace use appears under the four shared-lib roots.
# Catches violations the linker walk in AssertNoOpenVRInCore.cmake cannot see
# (header-only OpenVR includes, stray vr:: references in source files).
#
# Invocation (from tests/CMakeLists.txt):
#   add_test(NAME lint_no_openvr_in_core
#       COMMAND ${CMAKE_COMMAND}
#           -DSRC_ROOTS=<root1>$<SEMICOLON><root2>$<SEMICOLON>...
#           -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)

if(NOT DEFINED SRC_ROOTS)
    message(FATAL_ERROR "lint_no_openvr_in_core: SRC_ROOTS not provided. "
        "Pass -DSRC_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_extensions "*.h" "*.hpp" "*.hxx" "*.c" "*.cpp" "*.cc" "*.cxx" "*.inc" "*.ipp")
set(_violations "")
set(_files_scanned 0)

foreach(_root ${SRC_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "lint_no_openvr_in_core: SRC_ROOTS entry is not a directory: ${_root}")
    endif()
    foreach(_ext ${_extensions})
        file(GLOB_RECURSE _files "${_root}/${_ext}")
        foreach(_file ${_files})
            math(EXPR _files_scanned "${_files_scanned} + 1")
            file(READ "${_file}" _content)
            if(_content MATCHES "<openvr(_driver)?\\.h>" OR _content MATCHES "vr::")
                list(APPEND _violations "${_file}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "lint_no_openvr_in_core: ${_vcount} file(s) violate the no-OpenVR-in-shared-layer rule:")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH SRC_ROOTS _rootcount)
message(STATUS "lint_no_openvr_in_core: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

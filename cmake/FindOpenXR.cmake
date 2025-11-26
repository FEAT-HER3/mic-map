# FindOpenXR.cmake
# Find the OpenXR SDK
#
# This module defines:
#   OpenXR_FOUND - True if OpenXR was found
#   OpenXR_INCLUDE_DIRS - Include directories for OpenXR
#   OpenXR_LIBRARIES - Libraries to link against
#   OpenXR::openxr_loader - Imported target for OpenXR

include(FindPackageHandleStandardArgs)

# Try to find OpenXR using the OpenXR SDK's CMake config first
find_package(OpenXR CONFIG QUIET)

if(OpenXR_FOUND)
    # OpenXR was found via its own CMake config
    return()
endif()

# Manual search for OpenXR
# Check for OPENXR_SDK_PATH environment variable
if(DEFINED ENV{OPENXR_SDK_PATH})
    set(OPENXR_SDK_PATH "$ENV{OPENXR_SDK_PATH}")
endif()

# Search paths
set(OPENXR_SEARCH_PATHS
    ${OPENXR_SDK_PATH}
    ${CMAKE_SOURCE_DIR}/external/openxr
    "C:/OpenXR-SDK"
    "C:/Program Files/OpenXR"
    "C:/Program Files (x86)/OpenXR"
)

# Find include directory
find_path(OpenXR_INCLUDE_DIR
    NAMES openxr/openxr.h
    PATHS ${OPENXR_SEARCH_PATHS}
    PATH_SUFFIXES include
)

# Find library
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OPENXR_LIB_PATH_SUFFIX lib x64/lib)
else()
    set(OPENXR_LIB_PATH_SUFFIX lib x86/lib)
endif()

find_library(OpenXR_LIBRARY
    NAMES openxr_loader
    PATHS ${OPENXR_SEARCH_PATHS}
    PATH_SUFFIXES ${OPENXR_LIB_PATH_SUFFIX}
)

# Handle standard find_package arguments
find_package_handle_standard_args(OpenXR
    REQUIRED_VARS OpenXR_LIBRARY OpenXR_INCLUDE_DIR
)

if(OpenXR_FOUND)
    set(OpenXR_INCLUDE_DIRS ${OpenXR_INCLUDE_DIR})
    set(OpenXR_LIBRARIES ${OpenXR_LIBRARY})
    
    # Create imported target
    if(NOT TARGET OpenXR::openxr_loader)
        add_library(OpenXR::openxr_loader UNKNOWN IMPORTED)
        set_target_properties(OpenXR::openxr_loader PROPERTIES
            IMPORTED_LOCATION "${OpenXR_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OpenXR_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(OpenXR_INCLUDE_DIR OpenXR_LIBRARY)
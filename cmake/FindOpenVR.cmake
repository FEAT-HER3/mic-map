# FindOpenVR.cmake
# Find the OpenVR SDK
#
# This module defines:
#   OpenVR_FOUND - True if OpenVR was found
#   OpenVR_INCLUDE_DIRS - Include directories for OpenVR
#   OpenVR_LIBRARIES - Libraries to link against
#   OpenVR::openvr_api - Imported target for OpenVR

include(FindPackageHandleStandardArgs)

# Check for OPENVR_SDK_PATH environment variable
if(DEFINED ENV{OPENVR_SDK_PATH})
    set(OPENVR_SDK_PATH "$ENV{OPENVR_SDK_PATH}")
endif()

# Search paths
set(OPENVR_SEARCH_PATHS
    ${OPENVR_SDK_PATH}
    ${CMAKE_SOURCE_DIR}/external/openvr
    "C:/OpenVR"
    "C:/Program Files/OpenVR"
    "C:/Program Files (x86)/OpenVR"
)

# Find include directory
find_path(OpenVR_INCLUDE_DIR
    NAMES openvr.h
    PATHS ${OPENVR_SEARCH_PATHS}
    PATH_SUFFIXES headers include
)

# Determine library path suffix based on architecture and compiler
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(WIN32)
        set(OPENVR_LIB_PATH_SUFFIX lib/win64)
    else()
        set(OPENVR_LIB_PATH_SUFFIX lib/linux64)
    endif()
else()
    if(WIN32)
        set(OPENVR_LIB_PATH_SUFFIX lib/win32)
    else()
        set(OPENVR_LIB_PATH_SUFFIX lib/linux32)
    endif()
endif()

# Find library
find_library(OpenVR_LIBRARY
    NAMES openvr_api
    PATHS ${OPENVR_SEARCH_PATHS}
    PATH_SUFFIXES ${OPENVR_LIB_PATH_SUFFIX}
)

# Handle standard find_package arguments
find_package_handle_standard_args(OpenVR
    REQUIRED_VARS OpenVR_LIBRARY OpenVR_INCLUDE_DIR
)

if(OpenVR_FOUND)
    set(OpenVR_INCLUDE_DIRS ${OpenVR_INCLUDE_DIR})
    set(OpenVR_LIBRARIES ${OpenVR_LIBRARY})
    
    # Create imported target
    if(NOT TARGET OpenVR::openvr_api)
        add_library(OpenVR::openvr_api UNKNOWN IMPORTED)
        set_target_properties(OpenVR::openvr_api PROPERTIES
            IMPORTED_LOCATION "${OpenVR_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OpenVR_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(OpenVR_INCLUDE_DIR OpenVR_LIBRARY)
# src/steamvr/CMakeLists.txt
# SteamVR integration library
#
# This library provides SteamVR/OpenVR integration for MicMap:
# - VR input handling (HMD button events)
# - Dashboard state management
# - SteamVR lifecycle monitoring
#
# The library uses OpenVR SDK for HMD button support. If OpenVR is not
# available, a stub implementation is used for testing.

add_library(micmap_steamvr STATIC
    src/vr_input.cpp
    src/dashboard_manager.cpp
)

target_include_directories(micmap_steamvr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_steamvr
    PUBLIC
        micmap_common
)

target_compile_features(micmap_steamvr PUBLIC cxx_std_17)

# OpenVR SDK - Required for full SteamVR integration
# If not found, the library will use a stub implementation
if(TARGET OpenVR::openvr_api)
    target_link_libraries(micmap_steamvr PRIVATE OpenVR::openvr_api)
    target_compile_definitions(micmap_steamvr PRIVATE MICMAP_HAS_OPENVR)
    message(STATUS "micmap_steamvr: OpenVR support enabled")
else()
    message(STATUS "micmap_steamvr: OpenVR not found - using stub implementation")
    message(STATUS "  To enable OpenVR support, set OPENVR_SDK_PATH environment variable")
    message(STATUS "  or place OpenVR SDK in external/openvr/")
endif()

# OpenXR SDK - Optional, for future OpenXR support
if(TARGET OpenXR::openxr_loader)
    target_link_libraries(micmap_steamvr PRIVATE OpenXR::openxr_loader)
    target_compile_definitions(micmap_steamvr PRIVATE MICMAP_HAS_OPENXR)
    message(STATUS "micmap_steamvr: OpenXR support enabled")
endif()

# Add alias for consistent naming
add_library(micmap::steamvr ALIAS micmap_steamvr)

# Copy OpenVR DLL to output directory on Windows (if using shared library)
if(WIN32 AND TARGET OpenVR::openvr_api)
    get_target_property(OPENVR_LOCATION OpenVR::openvr_api IMPORTED_LOCATION)
    if(OPENVR_LOCATION)
        get_filename_component(OPENVR_DIR "${OPENVR_LOCATION}" DIRECTORY)
        # Look for the DLL in the same directory or bin directory
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OPENVR_DLL_NAME "openvr_api.dll")
        else()
            set(OPENVR_DLL_NAME "openvr_api.dll")
        endif()
        
        find_file(OPENVR_DLL
            NAMES ${OPENVR_DLL_NAME}
            PATHS "${OPENVR_DIR}" "${OPENVR_DIR}/../bin/win64" "${OPENVR_DIR}/../bin/win32"
            NO_DEFAULT_PATH
        )
        
        if(OPENVR_DLL)
            message(STATUS "Found OpenVR DLL: ${OPENVR_DLL}")
            # Copy DLL to output directory after build
            add_custom_command(TARGET micmap_steamvr POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENVR_DLL}"
                    "$<TARGET_FILE_DIR:micmap_steamvr>"
                COMMENT "Copying OpenVR DLL to output directory"
            )
        endif()
    endif()
endif()
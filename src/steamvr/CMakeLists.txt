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
    src/driver_api.cpp        # P8 D-22: renamed from src/vr_input.cpp
    src/vr_input_events.cpp
    src/manifest_registrar.cpp
)

target_include_directories(micmap_steamvr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_steamvr
    PUBLIC
        micmap_common
        # P8 IPC-04 / D-23: driver_api.hpp exposes core::AppConfig in
        # IDriverApi::getSettings() return type. Linkage is PUBLIC so
        # consumers (apps/micmap, apps/hmd_button_test, tests) transitively
        # get the include dir for micmap/core/config_manager.hpp. micmap_core
        # is JSON-free per AssertNoJsonInCore so this remains lint-clean.
        micmap_core
    PRIVATE
        httplib::httplib
        # P7 07-05 Rule-3 fix: vr_input.cpp now parses /health JSON to extract
        # the driver_detection_active boolean (D-10 / Pitfall 10 mitigation).
        # nlohmann/json is INTERFACE-only so this PRIVATE link is sufficient.
        nlohmann_json
)

# Pathcch.lib (Windows SDK): required by manifest_registrar.cpp's
# resolveManifestAbsolutePath() for PathCchRemoveFileSpec. Built into the
# Windows SDK since Vista; naked target name resolves via the MSVC default
# link-library search path.
if(WIN32)
    target_link_libraries(micmap_steamvr PRIVATE Pathcch)
endif()

target_compile_features(micmap_steamvr PUBLIC cxx_std_17)

# Phase 10 / D-11 / TEST-02 / Pitfall 4: MICMAP_DEBUG_BUILD per-build-config
# define so driver_api.{hpp,cpp}'s #if MICMAP_DEBUG_BUILD-guarded
# DebugTriggerResult + virtual debugTrigger() declarations and impl are
# visible inside this TU. PUBLIC propagation matters here because consumers
# of micmap_steamvr (apps/micmap, apps/hmd_button_test, tests) include the
# header — without PUBLIC the #if branch would silently elide the method
# from the consumer's vtable view, ABI-mismatching across this lib boundary.
# The quoting form "MICMAP_DEBUG_BUILD=$<IF:...>" is REQUIRED — see the
# matching driver/CMakeLists.txt + apps/micmap/CMakeLists.txt blocks.
target_compile_definitions(micmap_steamvr PUBLIC
    "MICMAP_DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>"
)

# OpenVR SDK - Required for full SteamVR integration
# If not found, the library will use a stub implementation
#
# Linkage is PUBLIC because manifest_registrar.hpp exposes vr::EVRApplicationError
# in the IVRApplicationsSurface seam (so test doubles can override the methods
# with their native OpenVR return types). Consumers transitively get the OpenVR
# include dirs — and, being a STATIC lib, this is a no-op at link time for
# implementations that don't actually reference OpenVR symbols.
if(TARGET OpenVR::openvr_api)
    target_link_libraries(micmap_steamvr PUBLIC OpenVR::openvr_api)
    target_compile_definitions(micmap_steamvr PUBLIC MICMAP_HAS_OPENVR)
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
            PATHS "${OPENVR_DIR}"
                  "${OPENVR_DIR}/../../bin/win64"
                  "${OPENVR_DIR}/../../bin/win32"
                  "${OPENVR_DIR}/../bin/win64"
                  "${OPENVR_DIR}/../bin/win32"
            NO_DEFAULT_PATH
        )

        if(OPENVR_DLL)
            message(STATUS "Found OpenVR DLL: ${OPENVR_DLL}")
            # Copy DLL to the runtime output directory (shared by all exes).
            # Using CMAKE_RUNTIME_OUTPUT_DIRECTORY is correct because
            # micmap_steamvr is a STATIC library — $<TARGET_FILE_DIR:micmap_steamvr>
            # would resolve to the archive dir (build/lib/<cfg>), not build/bin/<cfg>.
            #
            # Phase 5 Plan 03 Rule-3 fix: CMake 4.3+ rejects `copy_if_different`
            # when the destination is a bare directory path with a trailing slash
            # (returns "Invalid argument" — see file API change c. CMake 3.26).
            # Two-step fix: ensure the destination directory exists with
            # `make_directory`, then copy with the explicit destination filename
            # appended. Behavior is identical to the previous form on CMake 3.x.
            #
            # IN-03 invariant: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>
            # MUST match the per-config directory the executable targets
            # (`micmap`, `mic_test`, `hmd_button_test`) actually land in,
            # otherwise the OpenVR DLL is dropped beside nothing and the
            # exes fail at launch with STATUS_DLL_INIT_FAILED.
            #
            # On multi-config generators (MSBuild / Xcode) `$<CONFIG>`
            # expands to Debug/Release/etc and matches the per-target
            # default. On single-config generators (Ninja / Makefile)
            # `$<CONFIG>` expands to CMAKE_BUILD_TYPE (or empty if unset),
            # which produces an extra `/` in the path — Windows tolerates
            # this but it is semantically odd. Do NOT override
            # RUNTIME_OUTPUT_DIRECTORY on any of the three executable
            # targets to a path that bypasses the per-config directory
            # (e.g. plain `${CMAKE_BINARY_DIR}/bin`) without also updating
            # this destination, or the DLL co-location breaks silently.
            add_custom_command(TARGET micmap_steamvr POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENVR_DLL}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/${OPENVR_DLL_NAME}"
                COMMENT "Copying OpenVR DLL to runtime output directory"
            )
        endif()
    endif()
endif()
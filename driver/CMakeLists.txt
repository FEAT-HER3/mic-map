# driver/CMakeLists.txt
# MicMap OpenVR Virtual Controller Driver
#
# This driver registers a virtual controller with SteamVR that can inject
# button events. The MicMap application communicates with this driver via
# a local HTTP server to trigger dashboard selection.

cmake_minimum_required(VERSION 3.20)

project(micmap_driver
    VERSION 0.1.0
    DESCRIPTION "MicMap OpenVR Virtual Controller Driver"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Driver must be a shared library
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/virtual_controller.cpp
    src/http_server.cpp
    src/process_launcher.cpp
)

# Include directories
target_include_directories(driver_micmap
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# OpenVR SDK is required for the driver
if(TARGET OpenVR::openvr_api)
    target_link_libraries(driver_micmap PRIVATE OpenVR::openvr_api)
    message(STATUS "driver_micmap: OpenVR support enabled")
else()
    message(FATAL_ERROR "driver_micmap: OpenVR SDK is required for the driver. "
                        "Set OPENVR_SDK_PATH environment variable or place OpenVR SDK in external/openvr/")
endif()

# cpp-httplib for HTTP server (header-only)
if(TARGET httplib::httplib)
    target_link_libraries(driver_micmap PRIVATE httplib::httplib)
    # Ensure OpenSSL is disabled for httplib in the driver
    # We only need HTTP for localhost communication
    target_compile_definitions(driver_micmap PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS
    )
else()
    message(FATAL_ERROR "driver_micmap: cpp-httplib is required for the driver.")
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(driver_micmap PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    
    # Windows socket library for HTTP server
    target_link_libraries(driver_micmap PRIVATE ws2_32)
    
    # Set output name without 'lib' prefix
    set_target_properties(driver_micmap PROPERTIES
        PREFIX ""
        OUTPUT_NAME "driver_micmap"
    )
else()
    # Linux/macOS
    target_link_libraries(driver_micmap PRIVATE pthread)
    
    set_target_properties(driver_micmap PROPERTIES
        PREFIX ""
        OUTPUT_NAME "driver_micmap"
    )
endif()

# Set the output directory for the driver
# Note: We set both the general and per-configuration directories to avoid
# Visual Studio adding Release/Debug subdirectories
set_target_properties(driver_micmap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/driver/micmap/bin/win64"
)

# Copy driver resources to output directory
add_custom_command(TARGET driver_micmap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/driver.vrdrivermanifest"
        "${CMAKE_BINARY_DIR}/driver/micmap/driver.vrdrivermanifest"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/settings"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/input"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrresources"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/driver.vrresources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/settings/default.vrsettings"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/settings/default.vrsettings"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/input/micmap_controller_profile.json"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/input/micmap_controller_profile.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/input/vrcompositor_bindings_micmap_controller.json"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/input/vrcompositor_bindings_micmap_controller.json"
    COMMENT "Copying driver resources to output directory"
)

# Installation rules for the driver
install(TARGETS driver_micmap
    RUNTIME DESTINATION driver/micmap/bin/win64
    LIBRARY DESTINATION driver/micmap/bin/win64
)

install(FILES driver.vrdrivermanifest
    DESTINATION driver/micmap
)

install(FILES resources/driver.vrresources
    DESTINATION driver/micmap/resources
)

install(FILES resources/settings/default.vrsettings
    DESTINATION driver/micmap/resources/settings
)

install(FILES resources/input/micmap_controller_profile.json
    DESTINATION driver/micmap/resources/input
)

install(FILES resources/input/vrcompositor_bindings_micmap_controller.json
    DESTINATION driver/micmap/resources/input
)
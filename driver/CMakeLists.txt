# driver/CMakeLists.txt
# MicMap OpenVR HMD Sidecar Driver
#
# This driver creates its own /input/system/click boolean component on the HMD
# property container (no virtual controller, no tracked devices). The MicMap
# application communicates with this driver via a local HTTP server (POST
# /button) to toggle the SteamVR dashboard.

cmake_minimum_required(VERSION 3.20)

project(micmap_driver
    VERSION 0.1.0
    DESCRIPTION "MicMap OpenVR HMD Sidecar Driver"
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
    src/http_server.cpp
    src/audio_worker.cpp     # P6 D-05: driver-side audio capture worker
    src/detection_runner.cpp # P7 D-17: driver-side detection thread
    src/sinks/driver_log_sink.cpp  # P8 LIB-04 / D-19: ILogSink wrapper around SafeDriverLog
    src/config_json.cpp      # P8 D-03: AppConfig to_json/from_json ADL hooks (driver TU)
    src/config_io.cpp        # P8 D-10/D-14: driver-side config.json read (3-attempt retry) + atomic save
    src/settings_validator.cpp  # P8 D-14/D-15: PUT /settings field validation (first-failed-field)
    src/training_session.cpp # P9 D-09/D-10/D-12: driver-side training session state owner (hosts INoiseDetector)
    src/training_io.cpp      # P9 D-23/D-27: atomic ReplaceFileW write of %APPDATA%\\MicMap\\training_data.bin
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

# nlohmann/json for POST /button JSON parse (SVR-05 / SVR-08)
if(TARGET nlohmann_json)
    target_link_libraries(driver_micmap PRIVATE nlohmann_json)
elseif(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(driver_micmap PRIVATE nlohmann_json::nlohmann_json)
else()
    message(FATAL_ERROR "driver_micmap: nlohmann/json is required for POST /button body parse.")
endif()

# Phase 4 D-10: bindings_patcher was lifted to src/bindings/ so both
# driver_micmap.dll and micmap.exe can share one source of truth. The driver
# now links the shared lib + wraps DriverLog in a LogSink adapter (see
# device_provider.cpp).
target_link_libraries(driver_micmap PRIVATE micmap::bindings)

# Phase 5 D-10 / D-11: link the shared runtime layer into the driver.
# - PRIVATE only (D-11): PUBLIC would re-export every shared-lib symbol
#   across the driver DLL boundary, regressing SC3 (only HmdDriverFactory
#   may be exported).
# - LINK-ONLY (D-10): no driver TU (driver/src/*.cpp) may #include any
#   header from src/audio, src/detection, src/core, or src/common in this
#   phase. SC5 demands byte-identical behavior to v1.5; including runtime
#   headers would change template instantiations / debug info even if no
#   symbol is referenced. Phases 6/7 lift the link-only restriction.
target_link_libraries(driver_micmap PRIVATE micmap::core_runtime)

# Phase 10 / D-18: SSoT version string compile define (renamed from
# MICMAP_DRIVER_VERSION to MICMAP_VERSION_STRING — same canonical define
# name across both driver_micmap and micmap binaries; consumed by
# device_provider.cpp's RunFrame init log line at line 803-804).
# MICMAP_VERSION flows from cmake/version.cmake (the SSoT included at root).
#
# Phase 10 / D-11 / TEST-02 / Pitfall 4: MICMAP_DEBUG_BUILD per-build-config
# define for the synthetic-trigger surface. Generator-expression form is
# multi-config-safe (MSBuild / Xcode evaluate $<CONFIG:Debug> per build, while
# the if(CMAKE_BUILD_TYPE STREQUAL "Debug") form is broken at configure time
# under multi-config generators). The quoting "MICMAP_DEBUG_BUILD=$<...>" is
# REQUIRED — without it CMake's list-element semicolons split the genex.
target_compile_definitions(driver_micmap PRIVATE
    MICMAP_VERSION_STRING="${MICMAP_VERSION}"
    "MICMAP_DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>"
)

# Phase 10 / D-18: VS_VERSION_INFO resource for driver_micmap.dll.
# configure_file substitutes @MICMAP_VERSION@ + @MICMAP_VERSION_QUAD@ from
# cmake/version.cmake into driver/driver_micmap.rc.in -> build/.../driver_micmap.rc;
# target_sources adds the generated .rc so the resource compiler embeds
# VS_VERSION_INFO into the DLL.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/driver_micmap.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/driver_micmap.rc"
    @ONLY
)
target_sources(driver_micmap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/driver_micmap.rc")

# Warnings-as-errors -- enforces Pitfall 7 "unused code = dead code" check on the rip-out
if(MSVC)
    target_compile_options(driver_micmap PRIVATE /WX /W4)
else()
    target_compile_options(driver_micmap PRIVATE
        -Werror -Wall -Wextra -Wunused-function -Wunused-variable)
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrresources"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/driver.vrresources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/settings/default.vrsettings"
        "${CMAKE_BINARY_DIR}/driver/micmap/resources/settings/default.vrsettings"
    COMMENT "Copying driver resources to output directory"
)

# Installation rules for the driver.
#
# Phase 4 D-01: installer lands driver files at {SteamVR}\drivers\micmap\ -- so
# the staged tree (cmake --install . --prefix build/stage) must match that
# layout under drivers/ (plural) so installer/MicMap.iss [Files] can source
# {#STAGE_DIR}\drivers\micmap\* verbatim. The dev-workflow POST_BUILD copies
# above still emit to build/driver/micmap/** (singular) for local testing --
# only the install destinations are plural.
install(TARGETS driver_micmap
    RUNTIME DESTINATION drivers/micmap/bin/win64
    LIBRARY DESTINATION drivers/micmap/bin/win64
)

install(FILES driver.vrdrivermanifest
    DESTINATION drivers/micmap
)

install(FILES resources/driver.vrresources
    DESTINATION drivers/micmap/resources
)

install(FILES resources/settings/default.vrsettings
    DESTINATION drivers/micmap/resources/settings
)
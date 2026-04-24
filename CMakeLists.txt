# CMakeLists.txt
# MicMap - SteamVR Add-on for Microphone Pattern Detection
# Root CMake configuration

cmake_minimum_required(VERSION 3.20)

project(MicMap 
    VERSION 0.1.0 
    DESCRIPTION "SteamVR add-on for hands-free interaction via microphone pattern detection"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(MICMAP_BUILD_TESTS "Build unit tests" ON)
option(MICMAP_BUILD_TEST_APPS "Build test applications" ON)
option(MICMAP_BUILD_DRIVER "Build OpenVR driver" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Platform-specific settings
if(WIN32)
    # Windows-specific definitions
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    
    # MSVC-specific settings
    if(MSVC)
        # Warning level
        add_compile_options(/W4)
        
        # Enable multi-processor compilation
        add_compile_options(/MP)
        
        # Use static runtime for release builds (optional)
        # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    # GCC/Clang warnings
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find optional packages
# OpenXR - for VR integration
find_package(OpenXR QUIET)
if(OpenXR_FOUND)
    message(STATUS "Found OpenXR: ${OpenXR_INCLUDE_DIRS}")
else()
    message(STATUS "OpenXR not found - VR features will use stub implementation")
endif()

# OpenVR - for SteamVR-specific features
find_package(OpenVR QUIET)
if(OpenVR_FOUND)
    message(STATUS "Found OpenVR: ${OpenVR_INCLUDE_DIRS}")
else()
    message(STATUS "OpenVR not found - SteamVR features will use stub implementation")
endif()

# Add subdirectories
add_subdirectory(external)
add_subdirectory(src)

if(MICMAP_BUILD_TEST_APPS)
    add_subdirectory(apps)
endif()

# Phase 03 ad-hoc tools (register_manifest). Throwaway — delete after Phase 03 exit.
# Guarded on OpenVR availability (Phase 03 A2 test requires live SteamVR API).
if(WIN32 AND OpenVR_FOUND)
    add_subdirectory(tools)
endif()

if(MICMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build the OpenVR driver (requires OpenVR SDK)
if(MICMAP_BUILD_DRIVER)
    if(OpenVR_FOUND)
        add_subdirectory(driver)
    else()
        message(STATUS "Skipping driver build - OpenVR SDK not found")
    endif()
endif()

# ---------------------------------------------------------------------
# Phase 4 INST-07: CMake `package` target (D-18 / D-19 / D-20 / D-21)
# `cmake --build build --target package --config Release` produces
# ${CMAKE_BINARY_DIR}/installer/MicMap-Setup-v${PROJECT_VERSION}.exe
# via: cmake --install (into build/stage) -> ISCC.exe with /D defines.
# ---------------------------------------------------------------------
find_program(ISCC_EXECUTABLE ISCC
    PATHS
        "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
    DOC "Inno Setup 6 command-line compiler (ISCC.exe)"
)
if(NOT ISCC_EXECUTABLE)
    message(WARNING
        "ISCC.exe not found -- 'package' target will NOT be available. "
        "Install Inno Setup 6.7.1+ from https://jrsoftware.org/isdl.php "
        "and either add it to PATH or configure with "
        "-DISCC_EXECUTABLE=<path-to-ISCC.exe>.")
else()
    set(MICMAP_STAGE_DIR     "${CMAKE_BINARY_DIR}/stage")
    set(MICMAP_INSTALLER_DIR "${CMAKE_BINARY_DIR}/installer")
    set(MICMAP_ISS_FILE      "${CMAKE_SOURCE_DIR}/installer/MicMap.iss")

    add_custom_target(package
        # Step 1: clean stage dir so removed files don't linger
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${MICMAP_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory   "${MICMAP_STAGE_DIR}"

        # Step 2: run all install() rules into the stage
        COMMAND ${CMAKE_COMMAND}
            --install "${CMAKE_BINARY_DIR}"
            --prefix  "${MICMAP_STAGE_DIR}"
            --config  $<CONFIG>

        # Step 3: compile the .iss -> MicMap-Setup-v${VERSION}.exe
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MICMAP_INSTALLER_DIR}"
        COMMAND "${ISCC_EXECUTABLE}"
                "/DMICMAP_VERSION=${PROJECT_VERSION}"
                "/DSTAGE_DIR=${MICMAP_STAGE_DIR}"
                "/DOUTPUT_DIR=${MICMAP_INSTALLER_DIR}"
                "${MICMAP_ISS_FILE}"

        # Step 4: echo the output path
        COMMAND ${CMAKE_COMMAND} -E echo
            "Installer built: ${MICMAP_INSTALLER_DIR}/MicMap-Setup-v${PROJECT_VERSION}.exe"

        # WR-06: target-level dependency on micmap must be expressed via
        # add_dependencies(package micmap) below -- add_custom_target's
        # DEPENDS keyword is for FILE-level dependencies (CMake semantics),
        # not target dependencies, so listing `micmap` there does not force
        # a rebuild when its sources change.
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Building MicMap-Setup-v${PROJECT_VERSION}.exe"
        VERBATIM
    )
    add_dependencies(package micmap)
    if(MICMAP_BUILD_DRIVER AND OpenVR_FOUND)
        add_dependencies(package driver_micmap)
    endif()
endif()

# Installation rules
install(TARGETS micmap
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install configuration files
install(FILES config/default_config.json
    DESTINATION share/micmap
    RENAME config.json
)

# Print configuration summary
message(STATUS "")
message(STATUS "MicMap Configuration Summary")
message(STATUS "============================")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "Build tests:      ${MICMAP_BUILD_TESTS}")
message(STATUS "Build test apps:  ${MICMAP_BUILD_TEST_APPS}")
message(STATUS "Build driver:     ${MICMAP_BUILD_DRIVER}")
message(STATUS "OpenXR found:     ${OpenXR_FOUND}")
message(STATUS "OpenVR found:     ${OpenVR_FOUND}")
message(STATUS "")
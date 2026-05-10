# CMakeLists.txt
# MicMap - SteamVR Add-on for Microphone Pattern Detection
# Root CMake configuration

cmake_minimum_required(VERSION 3.20)

project(MicMap
    VERSION 1.6.0
    DESCRIPTION "SteamVR add-on for hands-free interaction via microphone pattern detection"
    LANGUAGES CXX
)
# Phase 10 / D-18 / Rule 3: PROJECT_VERSION bumped from 0.1.0 -> 1.6.0 to
# co-version with cmake/version.cmake's MICMAP_VERSION="1.6.0" SSoT below.
# The existing ISCC /DMICMAP_VERSION=${PROJECT_VERSION} pass at the bottom
# of this file relies on the value matching the SSoT for Wave 1 (10-06
# removes the /D pass entirely). Without this bump the /D pass would
# stamp installer artifacts with 0.1.0 while VERSIONINFO + version.iss
# read 1.6.0 — the drift the lint is meant to catch.

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(MICMAP_BUILD_TESTS "Build unit tests" ON)
option(MICMAP_BUILD_TEST_APPS "Build test applications" ON)
# Phase 5 SC1: mic_test.exe must build with MICMAP_BUILD_DRIVER=OFF and
# OpenVR absent (headless invariant). CI's headless job sets
# -DMICMAP_BUILD_DRIVER=OFF explicitly. Default-ON is correct for the dev
# workflow (Bigscreen Beyond + SteamVR); do not flip this default to OFF
# without coordinating the CI matrix or SC1 will silently regress.
option(MICMAP_BUILD_DRIVER "Build OpenVR driver" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------
# Phase 10 / INST-09 / D-18: single-version source-of-truth.
# cmake/version.cmake defines MICMAP_VERSION (semver) + MICMAP_VERSION_QUAD
# (RC FILEVERSION/PRODUCTVERSION numeric quad). Driver, client, generated
# installer/version.iss, and VS_VERSION_INFO RC resources all read from here.
# AssertCoVersioning lint (cmake/AssertCoVersioning.cmake) enforces the
# co-versioning invariant on every configure.
# ---------------------------------------------------------------------
include(cmake/version.cmake)
configure_file(
    "${CMAKE_SOURCE_DIR}/installer/version.iss.in"
    "${CMAKE_SOURCE_DIR}/installer/version.iss"
    @ONLY
)
# ---------------------------------------------------------------------
# Phase 10 / D-11 / TEST-02 / Pitfall 4: MICMAP_DEBUG_BUILD compile define.
# Defined per-target via target_compile_definitions in driver/CMakeLists.txt,
# apps/micmap/CMakeLists.txt, AND src/steamvr/CMakeLists.txt using the
# generator expression "MICMAP_DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>"
# (multi-config-safe; the if(CMAKE_BUILD_TYPE STREQUAL "Debug") form is
# broken under MSBuild/Xcode because CMAKE_BUILD_TYPE is unset at
# configure time on multi-config generators). If a 4th target ever needs
# this define, refactor to a shared helper to prevent drift — Pitfall 4
# is "the define lands on one target but not the other and Release
# silently ships the synthetic-trigger surface."
# ---------------------------------------------------------------------

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

# Phase 5 D-04: walk the micmap_core_runtime link/include graph at configure
# time and FATAL_ERROR if any node touches OpenVR. Must run AFTER the src
# tree so all four sub-lib targets are defined, and BEFORE the driver
# tree below so a clean fail message lands before the driver target tries
# to link the shared layer.
include(cmake/AssertNoOpenVRInCore.cmake)

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
        # P10 D-18 / Wave 6: redundant /DMICMAP_VERSION pass REMOVED -- the
        # `#include "version.iss"` at the top of installer/MicMap.iss is now the
        # sole authority for {#MICMAP_VERSION}, populated from cmake/version.cmake
        # via configure_file(installer/version.iss.in -> installer/version.iss).
        # AssertCoVersioning lint enforces the value-match between the cmake-side
        # MICMAP_VERSION and the value baked into the generated version.iss on
        # every configure.
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MICMAP_INSTALLER_DIR}"
        COMMAND "${ISCC_EXECUTABLE}"
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

# Ship OpenVR and MSVC runtime DLLs beside micmap.exe (Plan 4 UAT gap-fix).
# Without these, micmap.exe crashes at launch on any non-dev target with
# 0xC0000142 (STATUS_DLL_INIT_FAILED) — missing openvr_api.dll — or with
# 0xC00004BC (STATUS_SXS_IDENTITIES_DIFFERENT) — missing VC++ runtime.
# Sister project bey-closer-t1 (CMakeLists.txt:148-156, beyond_spike_monitor)
# POST_BUILD-copies openvr_api.dll beside its standalone exe for the same reason.
if(WIN32 AND TARGET OpenVR::openvr_api)
    get_target_property(_OPENVR_LOC OpenVR::openvr_api IMPORTED_LOCATION)
    if(_OPENVR_LOC)
        get_filename_component(_OPENVR_DIR "${_OPENVR_LOC}" DIRECTORY)
        find_file(MICMAP_OPENVR_DLL
            NAMES openvr_api.dll
            PATHS "${_OPENVR_DIR}"
                  "${_OPENVR_DIR}/../../bin/win64"
                  "${_OPENVR_DIR}/../bin/win64"
            NO_DEFAULT_PATH
        )
        if(MICMAP_OPENVR_DLL)
            install(FILES "${MICMAP_OPENVR_DLL}" DESTINATION bin)
        else()
            message(WARNING "openvr_api.dll not found beside ${_OPENVR_LOC}; installer will be broken")
        endif()
    endif()
endif()

# MSVC runtime (msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll).
# CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION must be set BEFORE
# InstallRequiredSystemLibraries is included (module reads it at inclusion time).
if(WIN32 AND MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    include(InstallRequiredSystemLibraries)
endif()

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
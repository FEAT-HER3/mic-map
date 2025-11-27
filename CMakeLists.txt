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

# Copy scripts to build output directory
file(GLOB SCRIPT_FILES "${CMAKE_SOURCE_DIR}/scripts/*.bat")
foreach(SCRIPT_FILE ${SCRIPT_FILES})
    get_filename_component(SCRIPT_NAME ${SCRIPT_FILE} NAME)
    configure_file(${SCRIPT_FILE} ${CMAKE_BINARY_DIR}/bin/${SCRIPT_NAME} COPYONLY)
endforeach()

# Custom target to copy all distributable files to build/bin
# This includes scripts and the driver folder
add_custom_target(copy_distributable_files ALL
    # Create directories
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/driver"
    
    # Copy scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/install_driver.bat"
        "${CMAKE_BINARY_DIR}/bin/install_driver.bat"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/uninstall_driver.bat"
        "${CMAKE_BINARY_DIR}/bin/uninstall_driver.bat"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/test_driver.bat"
        "${CMAKE_BINARY_DIR}/bin/test_driver.bat"
    
    # Copy the entire driver folder to bin/driver/micmap
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/driver/micmap"
        "${CMAKE_BINARY_DIR}/bin/driver/micmap"
    
    COMMENT "Copying distributable files (scripts and driver) to build/bin directory"
)

# Ensure the driver is built before copying
if(MICMAP_BUILD_DRIVER AND OpenVR_FOUND)
    add_dependencies(copy_distributable_files driver_micmap)
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
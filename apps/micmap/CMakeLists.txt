# apps/micmap/CMakeLists.txt
# MicMap Main Application - ImGui GUI with SteamVR Integration

# Source files
set(MICMAP_SOURCES
    main.cpp
)

add_executable(micmap WIN32
    ${MICMAP_SOURCES}
)

target_link_libraries(micmap
    PRIVATE
        micmap_lib
        imgui
)

target_compile_features(micmap PRIVATE cxx_std_17)

# Include directory for resource.h
target_include_directories(micmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows-specific libraries
if(WIN32)
    target_link_libraries(micmap PRIVATE shell32 dwmapi)
endif()

# Set output directory
set_target_properties(micmap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy config files to output directory
add_custom_command(TARGET micmap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:micmap>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/default_config.json"
        "$<TARGET_FILE_DIR:micmap>/config/default_config.json"
    COMMENT "Copying config files..."
)
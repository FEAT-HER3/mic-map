# apps/micmap/CMakeLists.txt
# MicMap Main Application - Win32 GUI with SteamVR Integration

add_executable(micmap WIN32
    main.cpp
)

target_link_libraries(micmap
    PRIVATE
        micmap_lib
)

target_compile_features(micmap PRIVATE cxx_std_17)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(micmap PRIVATE comctl32)
endif()

# Set output directory
set_target_properties(micmap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
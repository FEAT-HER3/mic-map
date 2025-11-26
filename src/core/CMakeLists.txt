# src/core/CMakeLists.txt
# Core application logic library

add_library(micmap_core STATIC
    src/state_machine.cpp
    src/config_manager.cpp
)

target_include_directories(micmap_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_core
    PUBLIC
        micmap_common
)

target_compile_features(micmap_core PUBLIC cxx_std_17)

# Windows-specific libraries for config paths
if(WIN32)
    target_link_libraries(micmap_core PRIVATE shell32)
endif()

# Add alias for consistent naming
add_library(micmap::core ALIAS micmap_core)
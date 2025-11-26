# src/common/CMakeLists.txt
# Common utilities library

add_library(micmap_common STATIC
    src/logger.cpp
)

target_include_directories(micmap_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(micmap_common PUBLIC cxx_std_17)

# Add alias for consistent naming
add_library(micmap::common ALIAS micmap_common)
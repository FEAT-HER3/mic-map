# src/detection/CMakeLists.txt
# White noise detection library

add_library(micmap_detection STATIC
    src/spectral_analyzer.cpp
    src/noise_detector.cpp
    src/pattern_trainer.cpp
)

target_include_directories(micmap_detection
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_detection
    PUBLIC
        micmap_common
)

target_compile_features(micmap_detection PUBLIC cxx_std_17)

# When KissFFT is available, link it
# target_link_libraries(micmap_detection PRIVATE kissfft)

# Add alias for consistent naming
add_library(micmap::detection ALIAS micmap_detection)
# apps/mic_test/CMakeLists.txt
# MicMap Audio Test Application - Win32 GUI

add_executable(mic_test WIN32
    main.cpp
)

# Phase 5 D-08 (SC1 acceptance probe): mic_test.exe links the new shared
# runtime aggregate exclusively. The previous three-lib enum (micmap_audio,
# micmap_detection, micmap_common) is replaced by a single micmap::core_runtime
# reference. mic_test.exe MUST continue to build with -DMICMAP_BUILD_DRIVER=OFF
# and OpenVR SDK absent — this link is the single point of failure for that
# headless invariant.
target_link_libraries(mic_test
    PRIVATE
        micmap::core_runtime
)

target_compile_features(mic_test PRIVATE cxx_std_17)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(mic_test PRIVATE comctl32 comdlg32)
endif()

# Set output directory
set_target_properties(mic_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
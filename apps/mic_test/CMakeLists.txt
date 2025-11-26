# apps/mic_test/CMakeLists.txt
# MicMap Audio Test Application - Win32 GUI

add_executable(mic_test WIN32
    main.cpp
)

target_link_libraries(mic_test
    PRIVATE
        micmap_audio
        micmap_detection
        micmap_common
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
# apps/hmd_button_test/CMakeLists.txt
# MicMap HMD Button Test Application - Win32 GUI

add_executable(hmd_button_test WIN32
    main.cpp
)

target_link_libraries(hmd_button_test
    PRIVATE
        micmap_steamvr
        micmap_common
)

target_compile_features(hmd_button_test PRIVATE cxx_std_17)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(hmd_button_test PRIVATE comctl32)
endif()

# Set output directory
set_target_properties(hmd_button_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
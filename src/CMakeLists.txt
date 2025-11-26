# src/CMakeLists.txt
# MicMap source libraries

add_subdirectory(common)
add_subdirectory(audio)
add_subdirectory(detection)
add_subdirectory(steamvr)
add_subdirectory(core)

# Main library combining all modules
add_library(micmap_lib INTERFACE)
target_link_libraries(micmap_lib INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_steamvr
    micmap_common
)

add_library(micmap::lib ALIAS micmap_lib)
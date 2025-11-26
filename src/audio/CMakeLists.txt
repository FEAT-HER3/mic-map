# src/audio/CMakeLists.txt
# Audio capture library

add_library(micmap_audio STATIC
    src/audio_buffer.cpp
    src/device_enumerator.cpp
    src/audio_capture.cpp
)

target_include_directories(micmap_audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_audio
    PUBLIC
        micmap_common
)

target_compile_features(micmap_audio PUBLIC cxx_std_17)

# Windows-specific libraries for WASAPI audio capture
if(WIN32)
    target_link_libraries(micmap_audio
        PRIVATE
            ole32           # COM support
            uuid            # GUIDs
            ksuser          # Kernel streaming (for KSDATAFORMAT_SUBTYPE_*)
    )
    
    # Add Windows SDK include for audio headers
    target_compile_definitions(micmap_audio
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
endif()

# Add alias for consistent naming
add_library(micmap::audio ALIAS micmap_audio)
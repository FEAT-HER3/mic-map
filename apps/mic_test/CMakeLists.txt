# apps/mic_test/CMakeLists.txt
# MicMap Audio Test Application - Win32 GUI

add_executable(mic_test WIN32
    main.cpp
    # Phase 9 09-04 / TEST-04: WAV replay harness for ctest + agent QA loops.
    # CLI dispatch in main.cpp short-circuits before any GUI init when --replay
    # / --replay-dir is present (see tryRunReplayCli in main.cpp).
    src/wav_replay.cpp
)

# Phase 5 D-08 (SC1 acceptance probe): mic_test.exe links the new shared
# runtime aggregate exclusively. The previous three-lib enum (micmap_audio,
# micmap_detection, micmap_common) is replaced by a single micmap::core_runtime
# reference. mic_test.exe MUST continue to build with -DMICMAP_BUILD_DRIVER=OFF
# and OpenVR SDK absent — this link is the single point of failure for that
# headless invariant.
#
# Phase 9 09-04: nlohmann_json link added for the wav_replay JSON output path
# (CONTEXT D-30 schema). mic_test is a binary, not part of the shared core, so
# AssertNoJsonInCore does not apply.
target_link_libraries(mic_test
    PRIVATE
        micmap::core_runtime
        nlohmann_json
)

target_compile_features(mic_test PRIVATE cxx_std_17)

# Phase 9 09-04: include paths for the WAV replay surface + vendored dr_wav.h
# single-header (dr_wav v0.14.6 @ 243e26ffa, public domain / MIT-0).
target_include_directories(mic_test PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/mic_test/src
    ${CMAKE_SOURCE_DIR}/vendor/dr_wav
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(mic_test PRIVATE comctl32 comdlg32)
endif()

# Set output directory
set_target_properties(mic_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
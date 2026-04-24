# tests/CMakeLists.txt
# MicMap Unit Tests

# For now, create placeholder test structure
# Actual tests will be added when a testing framework is integrated

# Option to use Google Test or Catch2
option(MICMAP_USE_GTEST "Use Google Test for unit tests" OFF)

if(MICMAP_USE_GTEST)
    # Fetch Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
    
    # Test executables would be added here
    # add_executable(test_audio_capture test_audio_capture.cpp)
    # target_link_libraries(test_audio_capture PRIVATE micmap_audio GTest::gtest_main)
    # add_test(NAME test_audio_capture COMMAND test_audio_capture)
endif()

# Placeholder test that always passes
add_executable(test_placeholder test_placeholder.cpp)
target_compile_features(test_placeholder PRIVATE cxx_std_17)
add_test(NAME test_placeholder COMMAND test_placeholder)

# Config manager round-trip, corruption, clamp, missing-file, and retention tests
add_executable(test_config_manager test_config_manager.cpp)
target_compile_features(test_config_manager PRIVATE cxx_std_17)
target_link_libraries(test_config_manager PRIVATE micmap::core)
add_test(NAME test_config_manager COMMAND test_config_manager)

# SVR-05: CommandQueue thread-safety / drop-oldest / depth-8 coverage
add_executable(test_command_queue test_command_queue.cpp)
target_compile_features(test_command_queue PRIVATE cxx_std_17)
target_include_directories(test_command_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
add_test(NAME test_command_queue COMMAND test_command_queue)

# ---- Phase 3 Plan 01 (Wave 0 RED scaffold) ----
# These tests fail to build/link until Plans 04 / 05 / 06 land their impls.
# That is the expected RED state — the Nyquist gate for Phase 3.

# AUTO-06 / D-01: parseCliArgs (impl arrives in Plan 03-06).
add_executable(test_cli_flags_parse test_cli_flags_parse.cpp)
target_compile_features(test_cli_flags_parse PRIVATE cxx_std_17)
target_link_libraries(test_cli_flags_parse PRIVATE micmap::common)
add_test(NAME test_cli_flags_parse COMMAND test_cli_flags_parse)

# AUTO-02 / AUTO-03 / AUTO-04 / D-15 / D-17: manifest registrar
# (impl + manifest_registrar.hpp arrive in Plan 03-04).
add_executable(test_manifest_registrar test_manifest_registrar.cpp)
target_compile_features(test_manifest_registrar PRIVATE cxx_std_17)
target_link_libraries(test_manifest_registrar PRIVATE micmap::steamvr)
add_test(NAME test_manifest_registrar COMMAND test_manifest_registrar)

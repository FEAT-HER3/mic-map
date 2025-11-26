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
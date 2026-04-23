# Testing Patterns

**Analysis Date:** 2026-04-23

## Test Framework

**Runner:**
- CTest (CMake testing framework) - native test runner
- Config: `CMakeLists.txt` root project and `tests/CMakeLists.txt`
- Optional: Google Test support available via `MICMAP_USE_GTEST` CMake option

**Build Configuration:**
```cmake
# From CMakeLists.txt
option(MICMAP_BUILD_TESTS "Build unit tests" ON)
if(MICMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

**Run Commands:**
```bash
cd build
cmake -DMICMAP_BUILD_TESTS=ON ..
cmake --build .
ctest                          # Run all tests
ctest --output-on-failure      # Verbose output on test failure
ctest -V                        # Very verbose mode
```

## Test File Organization

**Location:**
- Tests reside in `/d/Documents/Projects/mic-map/tests/` directory
- Currently contains minimal test infrastructure

**Naming:**
- Pattern: `test_*.cpp` (e.g., `test_placeholder.cpp`)
- Executable targets created via `add_executable(test_<name> test_<name>.cpp)` in CMakeLists.txt

**Structure:**
```
tests/
├── CMakeLists.txt              # Test build configuration
└── test_placeholder.cpp        # Currently a placeholder test
```

## Test Structure

**Current Test Example:**
```cpp
/**
 * @file test_placeholder.cpp
 * @brief Placeholder test file
 * 
 * This is a minimal test that always passes, used to verify
 * the test infrastructure is working correctly.
 */

#include <iostream>

int main() {
    std::cout << "MicMap test placeholder - PASSED\n";
    return 0;
}
```

**Patterns:**
- Simple main() entry point for standalone tests (not using a framework yet)
- Exit code 0 = pass, non-zero = fail
- Output to stdout documenting test results
- No assertions framework integrated currently
- Comments explain purpose and structure

## Mocking

**Framework:** None integrated yet

**Notes:**
- Google Test framework available as option via `MICMAP_USE_GTEST` CMake option
- Commented example in `tests/CMakeLists.txt`:
  ```cmake
  # add_executable(test_audio_capture test_audio_capture.cpp)
  # target_link_libraries(test_audio_capture PRIVATE micmap_audio GTest::gtest_main)
  # add_test(NAME test_audio_capture COMMAND test_audio_capture)
  ```

**Interface-Based Design for Testability:**
- Pure virtual interfaces in headers (e.g., `IAudioCapture`, `IStateMachine`, `INoiseDetector`) enable mock implementations
- Factory functions return interface pointers (e.g., `createStateMachine()` returns `std::unique_ptr<IStateMachine>`)
- Mock implementation classes can be created that inherit from interfaces

**What to Mock:**
- External dependencies: audio devices (`IAudioCapture`), file I/O (`IConfigManager`)
- State machines and callbacks: provide controlled state transitions for testing logic
- Time-dependent operations: use injectable time sources where feasible

**What NOT to Mock:**
- Core algorithms: test actual spectral analysis, pattern detection logic
- Data structures: test real `AudioBuffer` behavior under concurrent access
- Thread synchronization: test actual mutex/atomic behavior

## Fixtures and Factories

**Test Data:**
- Currently no test fixtures or factories established
- Could leverage existing factory functions for dependency creation:
  ```cpp
  // In future tests
  auto config = StateMachineConfig{
      .minDetectionDuration = std::chrono::milliseconds(500),
      .cooldownDuration = std::chrono::milliseconds(300),
      .detectionThreshold = 0.7f
  };
  auto stateMachine = createStateMachine(config);
  ```

**Location:**
- Would typically go in `tests/` directory alongside test files
- Could create `tests/fixtures/` subdirectory for test data files
- Could create `tests/factories/` for test object builders

## Coverage

**Requirements:** None enforced currently

**Coverage Tools:**
- Not yet integrated, but CMake supports:
  - GCC/Clang: `--coverage` compile flag, `gcov`/`lcov` analysis
  - MSVC: OpenCppCoverage or built-in code coverage

**View Coverage (future setup):**
```bash
# After adding coverage flags to CMake
cmake -DCMAKE_CXX_FLAGS="--coverage" ..
cmake --build .
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

## Test Types

**Unit Tests:**
- Not yet implemented; framework prepared
- Scope: Individual classes and functions
- Approach: Would test interfaces like `AudioBuffer`, `StateMachine`, `ConfigManager` in isolation
- Example patterns to follow:
  - `AudioBuffer` tests: write/read/peek operations with various buffer states
  - `StateMachine` tests: state transitions, timer behavior, callback invocation
  - `Logger` tests: message formatting, thread safety of concurrent logs

**Integration Tests:**
- Not yet implemented
- Scope: Component interaction (e.g., audio capture → detection → state machine → trigger callback)
- Approach: Could use test applications in `/d/Documents/Projects/mic-map/apps/`

**E2E Tests:**
- Not implemented; project architecture supports it via apps
- Test applications can be built with `MICMAP_BUILD_TEST_APPS=ON`
- Manual VR headset testing required for true end-to-end validation

## Common Patterns

**Test Setup and Teardown:**
- No framework-level setup yet
- Future pattern (with GTest):
  ```cpp
  class StateMachineTest : public ::testing::Test {
  protected:
      void SetUp() override {
          config_ = StateMachineConfig{...};
          machine_ = createStateMachine(config_);
      }
      
      void TearDown() override {
          // Cleanup if needed
      }
      
      StateMachineConfig config_;
      std::unique_ptr<IStateMachine> machine_;
  };
  ```

**Async Testing:**
- Audio buffer operations are thread-safe via `std::lock_guard<std::mutex>`
- Future tests would verify concurrent read/write:
  ```cpp
  // Example pattern
  AudioBuffer buffer(1024);
  
  std::thread writer([&] { 
      float samples[512] = {...};
      buffer.write(samples, 512); 
  });
  
  std::thread reader([&] { 
      float out[512];
      buffer.read(out, 512); 
  });
  
  writer.join();
  reader.join();
  ```

**Error Testing:**
- Return-code pattern simplifies error testing (no exceptions to catch)
- Pattern would test Result enum values:
  ```cpp
  // Example
  auto capture = createWASAPICapture();
  bool result = capture->selectDevice(L"NonexistentDevice");
  ASSERT_FALSE(result);  // Expected failure
  ```

## Test Build and Compilation

**Compiler Requirements:**
- C++17 standard enforced: `target_compile_features(test_placeholder PRIVATE cxx_std_17)`
- MSVC `/W4` warnings enabled
- GCC/Clang `-Wall -Wextra -Wpedantic` enabled

**Linking:**
```cmake
# Test executables link against component libraries
target_link_libraries(test_<name> PRIVATE micmap_audio micmap_core micmap_detection)
```

**CMake Integration:**
```cmake
# From tests/CMakeLists.txt pattern
add_executable(test_<name> test_<name>.cpp)
target_compile_features(test_<name> PRIVATE cxx_std_17)
add_test(NAME test_<name> COMMAND test_<name>)
```

## Notes on Test Infrastructure

**Current State:**
- Minimal placeholder test infrastructure in place
- Google Test framework available but not yet integrated
- CTest runner ready for use

**Next Steps for Test Coverage:**
1. Enable `MICMAP_USE_GTEST=ON` in CMake configuration
2. Implement unit tests for core modules:
   - `src/audio/` - AudioBuffer, device enumeration, WASAPI capture
   - `src/core/` - StateMachine, ConfigManager
   - `src/detection/` - SpectralAnalyzer, NoiseDetector, PatternTrainer
3. Create test fixtures for audio samples, training data
4. Add integration tests verifying end-to-end detection pipeline
5. Configure code coverage tracking with GCC/Clang coverage tools

---

*Testing analysis: 2026-04-23*

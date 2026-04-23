# Coding Conventions

**Analysis Date:** 2026-04-23

## Naming Patterns

**Files:**
- Header files: `.hpp` extension (e.g., `audio_buffer.hpp`, `state_machine.hpp`)
- Implementation files: `.cpp` extension (e.g., `audio_buffer.cpp`, `state_machine.cpp`)
- Snake_case for filenames matching their primary class (e.g., `audio_buffer.hpp` contains `AudioBuffer` class)

**Functions:**
- camelCase for public function names (e.g., `startCapture()`, `getCurrentState()`, `getAudioBuffer()`)
- camelCase for private helper functions (e.g., `updateIdle()`, `transitionTo()`, `addTrainingSample()`)
- Getter methods: `get` prefix (e.g., `getCurrentState()`, `getConfig()`, `getAudioBuffer()`)
- Setter methods: `set` prefix (e.g., `setLogger()`, `setMinLevel()`, `setAudioCallback()`)
- Boolean query methods: `is` prefix (e.g., `isCapturing()`, `isTraining()`, `empty()`, `full()`)
- Action methods: verb-based (e.g., `startCapture()`, `stopCapture()`, `reset()`, `load()`, `save()`)

**Variables:**
- camelCase for local variables and parameters (e.g., `detectionConfidence`, `deltaTime`, `toWrite`)
- camelCase for member variables with trailing underscore (e.g., `buffer_`, `currentState_`, `capacity_`, `triggerCallback_`)
- UPPER_CASE for constants (e.g., `DEFAULT_MIN_DETECTION_DURATION_MS`, `EPSILON`, `FORMAT_VERSION`)
- `constexpr` for compile-time constants defined in anonymous namespaces or headers

**Classes and Types:**
- PascalCase for class names (e.g., `AudioBuffer`, `StateMachine`, `ConfigManager`)
- PascalCase for interface classes prefixed with `I` (e.g., `IAudioCapture`, `IStateMachine`, `IConfigManager`)
- PascalCase for structs (e.g., `StateMachineConfig`, `SpectralResult`, `TrainingDataHeader`)
- PascalCase for enums (e.g., `State`, `LogLevel`, `Result`)

**Namespaces:**
- Nested snake_case namespaces (e.g., `micmap::audio`, `micmap::core`, `micmap::detection`, `micmap::steamvr`, `micmap::common`)
- Anonymous namespaces `{}` for file-local utilities and constants
- Namespace ending comments (e.g., `} // namespace micmap::audio`)

## Code Style

**Formatting:**
- Indentation: 4 spaces (no tabs)
- Line length: No explicit enforced limit observed, but typical lines under 100 characters
- Blank lines: Single blank line between method definitions
- Class member initialization: Colon-initialized member initializer lists

**Linting:**
- MSVC: `/W4` warning level enabled for Visual Studio builds
- GCC/Clang: `-Wall -Wextra -Wpedantic` flags enabled
- C++ Standard: C++17 (`CMAKE_CXX_STANDARD 17`, `cxx_std_17`)

**Memory Management:**
- Smart pointers: `std::unique_ptr` and `std::shared_ptr` preferred (e.g., in factory functions)
- Factory functions: Return `std::unique_ptr<IInterface>` (e.g., `createStateMachine()`, `createWASAPICapture()`)
- Deleted copy constructors on resource-owning classes (e.g., `AudioBuffer(const AudioBuffer&) = delete`)
- Defaulted move constructors/assignments marked `noexcept` (e.g., `AudioBuffer(AudioBuffer&&) noexcept`)

## Import Organization

**Order:**
1. Module's own header (if in .cpp file) - e.g., `#include "micmap/audio/audio_buffer.hpp"`
2. Related project headers - e.g., `#include "micmap/common/logger.hpp"`
3. Standard library headers - e.g., `#include <vector>`, `#include <mutex>`, `#include <chrono>`
4. Platform-specific headers gated by preprocessor - e.g., `#ifdef _WIN32`

**Include Guards:**
- `#pragma once` used exclusively (no `#ifndef` guards)
- Placed at the very top of header files before any comments

**Path Aliases:**
- Full qualified paths used (e.g., `#include "micmap/audio/audio_capture.hpp"`)
- Relative includes avoided in favor of absolute project paths

## Error Handling

**Patterns:**
- Result enum for operation outcomes: `micmap::common::Result` enum with values like `Success`, `Error`, `NotInitialized`, `InvalidParameter`, `DeviceNotFound`, `Timeout`, `NotSupported`
- Return-code pattern: Functions return `bool` or `Result` instead of throwing exceptions (e.g., `bool startCapture()`, `bool load()`)
- Null checks for input parameters (e.g., `if (!samples || count == 0) { return 0; }`)
- Guard clauses for early returns (e.g., `if (newState == currentState_) { return; }`)
- Callbacks checked before invocation (e.g., `if (triggerCallback_) { triggerCallback_(); }`)

**No Exception Handling:**
- No try-catch blocks observed
- No exception specifications used
- Safe defaults returned on error (e.g., `return 0;`, `return false;`, `return std::vector<>();`)

## Logging

**Framework:** Custom logger in `micmap/common/logger.hpp`

**Patterns:**
- Centralized global logger via static `Logger` class
- Log levels: `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`
- Logging via macros: `MICMAP_LOG_DEBUG()`, `MICMAP_LOG_INFO()`, `MICMAP_LOG_WARNING()`, `MICMAP_LOG_ERROR()`, `MICMAP_LOG_FATAL()`
- Variadic macro support: `MICMAP_LOG_DEBUG("msg", value1, value2)` - concatenates all arguments via fold expression
- Thread-safe logging: Global `logMutex` in logger implementation
- Timestamp format: `[HH:MM:SS.mmm]` printed with each log line
- Output: Logs written to `std::cerr`

**Logging locations:**
- State transitions: `MICMAP_LOG_DEBUG("State transition: ...")` in `state_machine.cpp`
- Operation results: `MICMAP_LOG_INFO("Loaded config from: ...")` in `config_manager.cpp`
- Training events: `MICMAP_LOG_INFO("Started noise detection training")` in detection code
- Debug metrics: `MICMAP_LOG_DEBUG("State machine configured: threshold=..., minDuration=...")` for diagnostics

## Comments

**When to Comment:**
- File-level documentation: `@file`, `@brief` in Doxygen format at top of each file
- Class documentation: `@brief` comment block before class/struct definitions
- Method documentation: `@brief`, `@param`, `@return` for public interface methods
- Complex algorithms: Inline comments for non-obvious logic (e.g., ring buffer wraparound in `audio_buffer.cpp`)
- Implementation notes: TODO comments for incomplete features (e.g., `// TODO: Implement proper JSON parsing`)
- Assumptions: Comments explaining preconditions (e.g., `// FFT size must be power of 2`)

**JSDoc/TSDoc (Doxygen style):**
```cpp
/**
 * @brief Write samples to the buffer
 * @param samples Pointer to sample data
 * @param count Number of samples to write
 * @return Number of samples actually written
 */
size_t write(const float* samples, size_t count);
```

- `@file` + `@brief` for each translation unit
- `@param` for each parameter
- `@return` for return value documentation
- Placed in headers for public interfaces

## Function Design

**Size:** Methods range 2-50 lines; larger functions split into private helpers (e.g., `updateDetecting()`, `updateCooldown()` split from main `update()`)

**Parameters:**
- Input parameters passed by const reference or pointer (e.g., `const float* samples`)
- Size/count always passed separately (e.g., `const float* samples, size_t count`)
- Configuration structs passed by const reference (e.g., `const StateMachineConfig& config`)
- Callback functions passed by value or `std::move` (e.g., `TriggerCallback callback`)
- Return void for setters (`void setLogger()`)
- Return bool for queries and operations that can fail (`bool startCapture()`)

**Return Values:**
- Booleans for success/failure (e.g., `selectDevice()`, `startCapture()`)
- Size values for operations handling multiple items (e.g., `read()`, `write()` return `size_t`)
- Const references for getter accessors (e.g., `const StateMachineConfig& getConfig()`)
- Unique pointers for factory functions (e.g., `std::unique_ptr<IStateMachine> createStateMachine()`)

## Module Design

**Exports:**
- Pure virtual interfaces for public APIs (e.g., `IAudioCapture`, `IStateMachine`, `INoiseDetector`)
- Implementation classes private to .cpp files (e.g., `AudioBufferImpl`, `StateMachineImpl`)
- Factory functions in header files as the public construction method (e.g., `createStateMachine()`)
- Private namespaces used within .cpp files for internal helpers

**Barrel Files:**
- Not used; each component has focused headers (e.g., `audio_buffer.hpp`, `state_machine.hpp`, `noise_detector.hpp`)
- No aggregate include files observed

**Header Structure:**
- One class/interface per header file (or closely related interface + implementation helpers)
- Private implementation classes defined in .cpp files
- Factory functions declared in public headers, implemented in .cpp

---

*Convention analysis: 2026-04-23*

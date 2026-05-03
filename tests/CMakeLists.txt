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

# AUTO-05 / D-11: ack-before-notify ordering inside processVREvent.
# (impl + vr_input_events.hpp arrive in Plan 03-05).
add_executable(test_vr_input_quit_ordering test_vr_input_quit_ordering.cpp)
target_compile_features(test_vr_input_quit_ordering PRIVATE cxx_std_17)
target_link_libraries(test_vr_input_quit_ordering PRIVATE micmap::steamvr)
add_test(NAME test_vr_input_quit_ordering COMMAND test_vr_input_quit_ordering)

# AUTO-06 / D-09 / D-10: first-silent-launch tray balloon one-shot.
# Plan 03-06 Task 2 landed first_launch_balloon.{hpp,cpp}; the test links
# the impl translation unit directly (single-TU link into the test exe) so
# we don't need a dedicated app-level support library. micmap::common is
# linked for the logger that first_launch_balloon.cpp references via
# MICMAP_LOG_* macros.
add_executable(test_tray_balloon_once
    test_tray_balloon_once.cpp
    ${CMAKE_SOURCE_DIR}/apps/micmap/first_launch_balloon.cpp)
target_compile_features(test_tray_balloon_once PRIVATE cxx_std_17)
target_include_directories(test_tray_balloon_once PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/micmap)
target_link_libraries(test_tray_balloon_once PRIVATE micmap::core micmap::common)
if(WIN32)
    target_link_libraries(test_tray_balloon_once PRIVATE shell32)
endif()
add_test(NAME test_tray_balloon_once COMMAND test_tray_balloon_once)

# AUTO-01 + Open-item A2: app.vrmanifest schema gate.
# (manifest emission via configure_file arrives in Plan 03-02.
# add_dependencies forces ctest to build micmap first so the manifest
# is on disk when the schema test runs.)
add_executable(test_vrmanifest_schema test_vrmanifest_schema.cpp)
target_compile_features(test_vrmanifest_schema PRIVATE cxx_std_17)
target_link_libraries(test_vrmanifest_schema PRIVATE nlohmann_json)
target_compile_definitions(test_vrmanifest_schema PRIVATE
    MICMAP_MANIFEST_PATH="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/app.vrmanifest")
add_dependencies(test_vrmanifest_schema micmap)
add_test(NAME test_vrmanifest_schema COMMAND test_vrmanifest_schema)

# Phase 4 INST-08 / D-08 / D-11: bindings patcher unit tests.
# Covers six scenarios (idempotency, write-once backup, unpatch-restore,
# unpatch marker-erase, unpatch no-op, AtomicWriteJson crash safety) against
# a throwaway tmp dir — no SteamVR runtime required. The second add_test
# line with `idempotent-only` argv routes ctest through the same exe but
# filters to scenario 1, satisfying 04-VALIDATION.md 04-01-03.
add_executable(test_bindings_patcher test_bindings_patcher.cpp)
target_compile_features(test_bindings_patcher PRIVATE cxx_std_17)
target_link_libraries(test_bindings_patcher PRIVATE micmap::bindings nlohmann_json)
add_test(NAME test_bindings_patcher COMMAND test_bindings_patcher)
add_test(NAME bindings_patcher_idempotent
         COMMAND test_bindings_patcher idempotent-only)

# ---- Phase 5 D-02 / D-12 source-grep lints ----
# Both lints scan src/{audio,detection,core,common}/ for forbidden tokens.
# They are pure CMake-script-mode runs (no compiled exe), invoked via -P.
# $<SEMICOLON> escapes the list separator so CTest passes a single -D arg.

add_test(NAME lint_no_openvr_in_core
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_openvr_in_core.cmake)

add_test(NAME lint_no_driver_macro
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/lint_no_driver_macro.cmake)

# ---- Phase 6 Wave 0 (RED scaffold) ----
# AudioWorker lifecycle invariants. Compiles audio_worker.cpp directly into
# the test exe — never link driver_micmap.dll into a test binary (Pitfall 6).
# Mirrors tests/CMakeLists.txt:74-84 (test_tray_balloon_once) inline-impl
# pattern: include the impl TU as a source so we don't need a dedicated
# driver-side support library.
#
# RED state until Plan 06-02: when ${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp
# does not yet exist, the conditional below excludes it from the source list
# so configure still succeeds (otherwise the AssertAudioWorkerNoVrApi ctest
# could not run). The test exe is then built from the headless TU alone,
# which fails to BUILD with a missing "audio_worker.hpp" diagnostic — that
# compile failure IS the Nyquist gate. The companion lint
# AssertAudioWorkerNoVrApi (below) stays GREEN at Wave 0 via its
# RED-tolerant skip-on-NOT-EXISTS branch.
#
# P6 Plan 06-02 Rule 3 fix: audio_worker.cpp transitively includes
# driver_log.hpp -> <openvr_driver.h>, so the test target needs OpenVR's
# include path. Gate the target on OpenVR_FOUND so the P5 SC1 headless
# invariant (build with -DMICMAP_BUILD_DRIVER=OFF and OpenVR absent must
# succeed) is preserved. When OpenVR is not found, this test is skipped
# entirely — the AssertAudioWorkerNoVrApi lint still scans the sources
# and the AudioWorkerLifecycleHeadless test runs in the OpenVR-present
# build (where ctest -R AudioWorkerLifecycleHeadless is the GREEN gate).
set(_p6_audio_worker_sources driver/audio_worker_lifecycle_headless.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
    list(APPEND _p6_audio_worker_sources
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp")
endif()
# P7 07-05 follow-on (Rule 3 — blocking): audio_worker.cpp now references
# DetectionRunner::NotifyOne() (D-05 detection wakeup path landed in 07-04),
# so this test exe must compile detection_runner.cpp alongside audio_worker.cpp
# to resolve the symbol. Same EXISTS-gated pattern as the P7 propagation /
# stress tests below. Mirrors the source-list expansion pattern for
# DeviceProviderLifecycleStress at line ~210. Latent in 07-04 because the
# build-driver target uses the in-tree micmap_driver static lib which already
# has detection_runner.obj — only the standalone test exe target lacks it.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p6_audio_worker_sources
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_audio_worker_lifecycle_headless ${_p6_audio_worker_sources})
    target_compile_features(test_audio_worker_lifecycle_headless PRIVATE cxx_std_17)
    target_include_directories(test_audio_worker_lifecycle_headless PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_audio_worker_lifecycle_headless PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api)
    add_test(NAME AudioWorkerLifecycleHeadless
        COMMAND test_audio_worker_lifecycle_headless)
else()
    message(STATUS "AudioWorkerLifecycleHeadless: skipped (OpenVR SDK not found)")
endif()

# P6 D-07 / SVR-05: assert no vr::* in driver/src/audio_worker.{hpp,cpp}.
# Pure CMake script-mode (-P) lint, no compiled exe. Wave 0 RED-tolerant —
# the lint script's skip-on-NOT-EXISTS branch keeps this GREEN before the
# audio_worker source files land in Plan 06-02.
add_test(NAME AssertAudioWorkerNoVrApi
    COMMAND ${CMAKE_COMMAND}
        -DAUDIO_WORKER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertAudioWorkerNoVrApi.cmake)

# ---- Phase 7 Wave 0 (RED scaffold) ----
# DetectionRunner lifecycle + settings propagation. Compiles detection_runner.cpp
# directly into the test exe — never link driver_micmap.dll into a test binary
# (Pitfall 6). Mirrors P6 AudioWorkerLifecycleHeadless registration shape.

# DetectionSettingsPropagation — MIG-06 < 50 ms verifier.
# RED until Plan 07-03 lands detection_runner.cpp.
set(_p7_propagation_sources driver/detection_settings_propagation_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p7_propagation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_detection_settings_propagation ${_p7_propagation_sources})
    target_compile_features(test_detection_settings_propagation PRIVATE cxx_std_17)
    target_include_directories(test_detection_settings_propagation PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_detection_settings_propagation PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api)
    add_test(NAME DetectionSettingsPropagation
        COMMAND test_detection_settings_propagation)
else()
    message(STATUS "DetectionSettingsPropagation: skipped (OpenVR SDK not found)")
endif()

# DeviceProviderLifecycleStress — SC4 / MIG-04 50-cycle harness.
# RED until Plan 07-05 lands DetectionRunner wiring in DeviceProvider.
set(_p7_stress_sources driver/device_provider_lifecycle_stress_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p7_stress_sources
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_device_provider_lifecycle_stress ${_p7_stress_sources})
    target_compile_features(test_device_provider_lifecycle_stress PRIVATE cxx_std_17)
    target_include_directories(test_device_provider_lifecycle_stress PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    # P7 07-04 Rule-3: device_provider.cpp / http_server.cpp need their full
    # transitive deps now that the source list expanded (07-01 RED scaffold
    # left these latent; once detection_runner.cpp landed in 07-03, the test
    # exe compiled all four driver TUs and exposed the missing httplib +
    # bindings_patcher links). Mirrors driver/CMakeLists.txt link list.
    target_link_libraries(test_device_provider_lifecycle_stress PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_device_provider_lifecycle_stress PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME DeviceProviderLifecycleStress
        COMMAND test_device_provider_lifecycle_stress)
else()
    message(STATUS "DeviceProviderLifecycleStress: skipped (OpenVR SDK not found)")
endif()

# P7 D-22 / SVR-05: assert no vr::* in detection_runner.{hpp,cpp} or sample_ring.hpp.
# Pure CMake script-mode (-P) lint, no compiled exe. Wave 0 RED-tolerant.
add_test(NAME AssertDetectionRunnerNoVrApi
    COMMAND ${CMAKE_COMMAND}
        -DDETECTION_RUNNER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertDetectionRunnerNoVrApi.cmake)

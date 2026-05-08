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

# Config manager round-trip, corruption, clamp, missing-file, and retention tests.
# P8 D-02: ConfigManagerImpl + createConfigManager() relocated from src/core
# into apps/micmap/src/config_manager_impl.cpp to keep micmap_core JSON-free.
# Test compiles config_manager_impl.cpp directly (single-TU link, mirrors the
# test_tray_balloon_once pattern that inlines first_launch_balloon.cpp).
add_executable(test_config_manager
    test_config_manager.cpp
    ${CMAKE_SOURCE_DIR}/apps/micmap/src/config_manager_impl.cpp)
target_compile_features(test_config_manager PRIVATE cxx_std_17)
target_link_libraries(test_config_manager PRIVATE micmap::core nlohmann_json)
if(WIN32)
    target_link_libraries(test_config_manager PRIVATE shell32)
endif()
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
# P8 08-05: first_launch_balloon.cpp now references IDriverApi::putSettings
# in the persistence path (called only when caller passes a non-null
# IDriverApi*; the test passes nullptr). The symbol is still referenced at
# link time, so micmap::steamvr is required.
target_link_libraries(test_tray_balloon_once PRIVATE
    micmap::core
    micmap::common
    micmap::steamvr)
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
# Phase 9 Wave 1 (09-01): detection_runner.cpp now references TrainingSession::
# addSample in the Training-mode branch; the linker needs the symbol even
# though this test ctors DetectionRunner with deviceProvider==nullptr.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p6_audio_worker_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
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
# Phase 9 Wave 1 (09-01): detection_runner.cpp now #includes training_session.hpp
# and device_provider.hpp and references TrainingSession::addSample +
# DeviceProvider::trainingSession() in the Training-mode branch. Even though
# this test passes nullptr for deviceProvider (4-arg ctor default), MSVC
# emits a reference to the out-of-line method symbols and the linker needs
# them. Compile-and-link the supporting TUs.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p7_propagation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p7_propagation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
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
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        # P8 08-02: device_provider Init now references config_io +
        # makeDriverLogSink + AppConfig ADL hooks. The test must compile
        # those TUs alongside device_provider.cpp to resolve the symbols.
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): device_provider.cpp now references TrainingSession
# (lazy unique_ptr ctor/dtor + tryStartTrainingSession + resetTrainingSession);
# detection_runner.cpp references TrainingSession::addSample. Compile-and-link
# the new training TUs alongside the existing P7 source list.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p7_stress_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p7_stress_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
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

# ---- Phase 8 Wave 0 (RED scaffold) ----
# Lint scripts: 4 new lint scripts ship at Wave 0 (Plan 08-00). Two of them
# wire into ctest now; the other two (AssertNoJsonInCore + AssertNoConfigWriteInClient)
# wait until 08-02 and 08-04 respectively, because they currently fire on
# existing v1.5 code (config_manager.cpp uses nlohmann/json; main.cpp:498
# calls saveDefault). Registering them now would produce immediate FATAL
# on the existing v1.5 reality, so the ctest registrations are deferred
# per CONTEXT D-29 ordering until the violations are removed.

# P8 D-25 / IPC-07 / Pitfall 7: assert HTTP server binds 127.0.0.1 only.
# Wave 0 RED-tolerant via skip-on-NOT-EXISTS in the lint script. Currently
# stays GREEN — existing http_server.hpp ctor defaults host to "127.0.0.1".
add_test(NAME AssertHttpServerLocalhostOnly
    COMMAND ${CMAKE_COMMAND}
        -DHTTP_SERVER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertHttpServerLocalhostOnly.cmake)

# P8 D-24 / SVR-05 / Pitfall 3: assert no vr::* in http_server.{hpp,cpp}.
# Sibling of AssertDetectionRunnerNoVrApi. Currently GREEN — existing
# http_server.cpp includes only command_queue + driver_log + httplib + nlohmann.
add_test(NAME AssertHttpServerNoVrApi
    COMMAND ${CMAKE_COMMAND}
        -DHTTP_SERVER_DIR=${CMAKE_SOURCE_DIR}/driver/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertHttpServerNoVrApi.cmake)

# P8 D-02 / Pitfall 15: assert nlohmann/json absent under the 4 shared-lib
# roots. Wired in 08-02 (this plan) AFTER ConfigManagerImpl was relocated out
# of src/core/src/config_manager.cpp into apps/micmap/src/config_manager_impl.cpp.
# Full 4-root scope: src/audio + src/detection + src/core + src/common per D-02.
add_test(NAME AssertNoJsonInCore
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOTS=${CMAKE_SOURCE_DIR}/src/audio$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/detection$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/core$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/common
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoJsonInCore.cmake)

# P8 D-07 / IPC-05: assert client TUs do not write config.json. Driver is
# sole writer; client edits flow through PUT /settings. Registered now in
# 08-05 AFTER the saveDefault() callsites in apps/micmap/main.cpp:498 and
# apps/micmap/first_launch_balloon.cpp:42 are deleted (this plan's Task 2).
# CLIENT_ROOTS = apps/micmap + src/steamvr per the lint-script header.
# The relocated ConfigManagerImpl (apps/micmap/src/config_manager_impl.cpp)
# is excluded by basename inside the lint script — the impl itself is fine,
# only client *callers* are forbidden (see lint script comment). P10 deletes
# the impl outright when configManager is removed from the client.
add_test(NAME AssertNoConfigWriteInClient
    COMMAND ${CMAKE_COMMAND}
        -DCLIENT_ROOTS=${CMAKE_SOURCE_DIR}/apps/micmap$<SEMICOLON>${CMAKE_SOURCE_DIR}/src/steamvr
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoConfigWriteInClient.cmake)

# Test executable registrations follow the EXISTS-gated source-list pattern
# from the P7 DetectionSettingsPropagation registration above. Each test
# expects production source files that do not yet exist; the EXISTS gate
# keeps the source list empty (so cmake configure stays clean) while the
# missing #include in the test TU produces the build-time RED diagnostic.

# IPC-01 — GET /state shape. RED until Plan 08-03 lands driver_state.hpp +
# the GET /state route on HttpServer.
# 08-03 expansion: device_provider.cpp now references config_io / config_json /
# sinks/driver_log_sink at Init (composition root + initial AppConfig load).
# Pull those TUs in too so the test exe links cleanly.
set(_p8_get_state_sources driver/get_state_shape_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p8_get_state_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): device_provider.cpp / detection_runner.cpp now
# reference TrainingSession — training TUs join the source list.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_get_state_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_get_state_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_get_state_shape ${_p8_get_state_sources})
    target_compile_features(test_get_state_shape PRIVATE cxx_std_17)
    target_include_directories(test_get_state_shape PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_get_state_shape PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_get_state_shape PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME GetStateShape COMMAND test_get_state_shape)
else()
    message(STATUS "GetStateShape: skipped (OpenVR SDK not found)")
endif()

# IPC-02 — GET /telemetry/level. RED until Plan 08-03 lands the rmsGetter
# ctor parameter on HttpServer + the route handler.
# 08-03 expansion: device_provider.cpp pulls in config_io + config_json + sink TUs.
set(_p8_get_telemetry_level_sources driver/get_telemetry_level_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p8_get_telemetry_level_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_get_telemetry_level_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_get_telemetry_level_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_get_telemetry_level ${_p8_get_telemetry_level_sources})
    target_compile_features(test_get_telemetry_level PRIVATE cxx_std_17)
    target_include_directories(test_get_telemetry_level PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_get_telemetry_level PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_get_telemetry_level PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME GetTelemetryLevel COMMAND test_get_telemetry_level)
else()
    message(STATUS "GetTelemetryLevel: skipped (OpenVR SDK not found)")
endif()

# IPC-03 / D-17 — GET /devices 1 s cache. RED until Plan 08-03.
# 08-03 expansion: device_provider.cpp pulls in config_io + config_json + sink TUs.
set(_p8_get_devices_cache_sources driver/get_devices_cache_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p8_get_devices_cache_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_get_devices_cache_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_get_devices_cache_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_get_devices_cache ${_p8_get_devices_cache_sources})
    target_compile_features(test_get_devices_cache PRIVATE cxx_std_17)
    target_include_directories(test_get_devices_cache PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_get_devices_cache PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_get_devices_cache PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME GetDevicesCache COMMAND test_get_devices_cache)
else()
    message(STATUS "GetDevicesCache: skipped (OpenVR SDK not found)")
endif()

# IPC-04 — GET /settings AppConfig shape. RED until Plan 08-03 lands the
# configGetter ctor parameter + AppConfig to_json/from_json ADL hooks.
set(_p8_get_settings_shape_sources driver/get_settings_shape_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp")
    list(APPEND _p8_get_settings_shape_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")  # P8 D-19 LIB-04 TU
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_get_settings_shape_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_get_settings_shape_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_get_settings_shape ${_p8_get_settings_shape_sources})
    target_compile_features(test_get_settings_shape PRIVATE cxx_std_17)
    target_include_directories(test_get_settings_shape PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_get_settings_shape PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_get_settings_shape PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME GetSettingsShape COMMAND test_get_settings_shape)
else()
    message(STATUS "GetSettingsShape: skipped (OpenVR SDK not found)")
endif()

# IPC-04 / D-09 — PUT /settings round-trip. RED until Plan 08-04 lands PUT
# /settings + saveConfigJson (atomic_store + ReplaceFileW).
set(_p8_put_settings_round_trip_sources driver/put_settings_round_trip_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp" AND EXISTS "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp")
    list(APPEND _p8_put_settings_round_trip_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_put_settings_round_trip_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_put_settings_round_trip_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_put_settings_round_trip ${_p8_put_settings_round_trip_sources})
    target_compile_features(test_put_settings_round_trip PRIVATE cxx_std_17)
    target_include_directories(test_put_settings_round_trip PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_put_settings_round_trip PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_put_settings_round_trip PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME PutSettingsRoundTrip COMMAND test_put_settings_round_trip)
else()
    message(STATUS "PutSettingsRoundTrip: skipped (OpenVR SDK not found)")
endif()

# IPC-04 / D-14 — PUT /settings 400 envelope. RED until Plan 08-04.
set(_p8_put_settings_validation_sources driver/put_settings_validation_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp")
    list(APPEND _p8_put_settings_validation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_put_settings_validation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_put_settings_validation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_put_settings_validation ${_p8_put_settings_validation_sources})
    target_compile_features(test_put_settings_validation PRIVATE cxx_std_17)
    target_include_directories(test_put_settings_validation PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_put_settings_validation PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_put_settings_validation PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME PutSettingsValidation COMMAND test_put_settings_validation)
else()
    message(STATUS "PutSettingsValidation: skipped (OpenVR SDK not found)")
endif()

# D-28 — 100x PUT /settings stress, no leaks. RED until Plan 08-04.
set(_p8_put_settings_stress100_sources driver/put_settings_stress100_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp" AND EXISTS "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp")
    list(APPEND _p8_put_settings_stress100_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): training TUs needed.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_put_settings_stress100_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_put_settings_stress100_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_put_settings_stress100 ${_p8_put_settings_stress100_sources})
    target_compile_features(test_put_settings_stress100 PRIVATE cxx_std_17)
    target_include_directories(test_put_settings_stress100 PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_put_settings_stress100 PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_put_settings_stress100 PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME PutSettingsStress100 COMMAND test_put_settings_stress100)
else()
    message(STATUS "PutSettingsStress100: skipped (OpenVR SDK not found)")
endif()

# D-10 — driver Init 3-attempt SHARING_VIOLATION retry. RED until Plan 08-02
# lands config_io.cpp::loadConfigJson with the retry.
set(_p8_init_config_share_violation_sources driver/init_config_share_violation_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp")
    list(APPEND _p8_init_config_share_violation_sources
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp")  # P8 D-03: ADL hooks needed by loadConfigJson
endif()
if(OpenVR_FOUND)
    add_executable(test_init_config_share_violation ${_p8_init_config_share_violation_sources})
    target_compile_features(test_init_config_share_violation PRIVATE cxx_std_17)
    target_include_directories(test_init_config_share_violation PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_init_config_share_violation PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api
        nlohmann_json)
    add_test(NAME InitConfigShareViolation COMMAND test_init_config_share_violation)
else()
    message(STATUS "InitConfigShareViolation: skipped (OpenVR SDK not found)")
endif()

# HEALTH-05 / D-16 — POST /state/clear-error. RED until Plan 08-04.
# 08-03 expansion: device_provider.cpp pulls in config_io + config_json +
# sinks/driver_log_sink (composition root + initial AppConfig load) -- include
# those TUs in the test exe's source list.
set(_p8_state_clear_error_sources driver/state_clear_error_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")
    list(APPEND _p8_state_clear_error_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp")
endif()
# Phase 9 Wave 1 (09-01): device_provider.cpp now references TrainingSession
# (lazy unique_ptr ctor/dtor + tryStartTrainingSession + resetTrainingSession);
# detection_runner.cpp references TrainingSession::addSample. Add the new
# training TUs alongside the existing P8 source list.
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p8_state_clear_error_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p8_state_clear_error_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_state_clear_error ${_p8_state_clear_error_sources})
    target_compile_features(test_state_clear_error PRIVATE cxx_std_17)
    target_include_directories(test_state_clear_error PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_state_clear_error PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_state_clear_error PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME StateClearError COMMAND test_state_clear_error)
else()
    message(STATUS "StateClearError: skipped (OpenVR SDK not found)")
endif()

# D-14 — saveConfigJson uses atomic ReplaceFileW. RED until Plan 08-02.
# Pure config_io test (no OpenVR transitive). NOT gated on OpenVR_FOUND;
# this test exe builds in headless configurations too.
set(_p8_config_io_atomic_persist_sources driver/config_io_atomic_persist_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp")
    list(APPEND _p8_config_io_atomic_persist_sources
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp")  # P8 D-03: ADL hooks needed by saveConfigJson
endif()
add_executable(test_config_io_atomic_persist ${_p8_config_io_atomic_persist_sources})
target_compile_features(test_config_io_atomic_persist PRIVATE cxx_std_17)
target_include_directories(test_config_io_atomic_persist PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
target_link_libraries(test_config_io_atomic_persist PRIVATE
    micmap::core_runtime
    nlohmann_json)
add_test(NAME ConfigIoAtomicPersist COMMAND test_config_io_atomic_persist)

# LIB-04 / D-19 — MultiSinkLogger fan-out. RED until Plan 08-01 lands the
# log_sink.hpp interface + multi_sink_logger.cpp impl in src/common.
set(_p8_multi_sink_logger_sources test_multi_sink_logger.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/common/src/multi_sink_logger.cpp")
    list(APPEND _p8_multi_sink_logger_sources
        "${CMAKE_SOURCE_DIR}/src/common/src/multi_sink_logger.cpp")
endif()
add_executable(test_multi_sink_logger ${_p8_multi_sink_logger_sources})
target_compile_features(test_multi_sink_logger PRIVATE cxx_std_17)
target_link_libraries(test_multi_sink_logger PRIVATE micmap::common)
add_test(NAME MultiSinkLogger COMMAND test_multi_sink_logger)

# D-14 / D-15 — settings_validator field-level rejects. RED until Plan 08-04.
set(_p8_settings_validator_sources test_settings_validator.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp")
    list(APPEND _p8_settings_validator_sources
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp")
endif()
add_executable(test_settings_validator ${_p8_settings_validator_sources})
target_compile_features(test_settings_validator PRIVATE cxx_std_17)
target_include_directories(test_settings_validator PRIVATE
    ${CMAKE_SOURCE_DIR}/driver/src)
target_link_libraries(test_settings_validator PRIVATE micmap::core)
add_test(NAME SettingsValidator COMMAND test_settings_validator)

# HEALTH-01 / Pitfall 6 — IDriverApi::connect() 3-state result. RED until
# Plan 08-01 lands driver_api.hpp (rename of vr_input.hpp) + ConnectResult enum.
add_executable(test_client_driver_loaded_indicator test_client_driver_loaded_indicator.cpp)
target_compile_features(test_client_driver_loaded_indicator PRIVATE cxx_std_17)
target_link_libraries(test_client_driver_loaded_indicator PRIVATE micmap::steamvr)
add_test(NAME ClientDriverLoadedIndicator COMMAND test_client_driver_loaded_indicator)

# HEALTH-06 — level-meter 5 Hz visible / 0.5 Hz tray cadence. RED until Plan 08-05.
add_executable(test_client_level_meter_cadence test_client_level_meter_cadence.cpp)
target_compile_features(test_client_level_meter_cadence PRIVATE cxx_std_17)
target_link_libraries(test_client_level_meter_cadence PRIVATE micmap::steamvr)
add_test(NAME ClientLevelMeterCadence COMMAND test_client_level_meter_cadence)

# ---- Phase 9 Wave 0 (RED scaffold) ----
# Wave 0 lands 2 new lint scripts under cmake/ (AssertNoClientTraining,
# AssertReplayNoVrApi) plus 3 plain-main test scaffolds. Only ONE of the
# lints is wired to ctest at Wave 0 — AssertNoClientTraining ships in the
# repo but its ctest registration is intentionally deferred to 09-03 because
# it currently fires on the v1.5 client training body still in apps/micmap/main.cpp
# (single-writer cutover protocol per CONTEXT D-23, mirror of P8 D-07 timing
# for AssertNoConfigWriteInClient).

# P9 D-37 / TEST-01: assert no OpenVR symbols in apps/mic_test/src/wav_replay.{hpp,cpp}.
# Wave 0 RED-tolerant via skip-on-NOT-EXISTS in the lint script. Currently
# stays GREEN — wav_replay.{hpp,cpp} do not yet exist (lands in 09-04).
add_test(NAME AssertReplayNoVrApi
    COMMAND ${CMAKE_COMMAND}
        -DREPLAY_DIR=${CMAKE_SOURCE_DIR}/apps/mic_test/src
        -P ${CMAKE_SOURCE_DIR}/cmake/AssertReplayNoVrApi.cmake)

# NOTE: AssertNoClientTraining is intentionally NOT registered at Wave 0.
# The lint script exists at cmake/AssertNoClientTraining.cmake but currently
# FATALs on apps/micmap/main.cpp (existing v1.5 training body at lines
# 962-1027 + saveTrainingData callsites at :618, :974, :991). Single-writer
# cutover protocol per CONTEXT D-23: ctest registration goes live in 09-03
# alongside the client-side training body deletion. Mirror of P8 D-07 timing
# for AssertNoConfigWriteInClient. (See "# ---- Phase 9 Wave 3" block added
# by 09-03 for AssertNoClientTraining go-live.)

# TRAIN-01..04 / TRAIN-06 — TrainingSession lifecycle. RED until Plan 09-01
# lands driver/src/training_session.{hpp,cpp} + driver/src/training_io.{hpp,cpp}.
# Mirrors P7 DetectionSettingsPropagation EXISTS-gated source-list registration.
set(_p9_training_session_sources driver/training_session_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    list(APPEND _p9_training_session_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    list(APPEND _p9_training_session_sources
        "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
endif()
if(OpenVR_FOUND)
    add_executable(test_training_session ${_p9_training_session_sources})
    target_compile_features(test_training_session PRIVATE cxx_std_17)
    target_include_directories(test_training_session PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_training_session PRIVATE
        micmap::core_runtime
        OpenVR::openvr_api
        nlohmann_json)
    add_test(NAME TrainingSessionLifecycle
        COMMAND test_training_session)
else()
    message(STATUS "TrainingSessionLifecycle: skipped (OpenVR SDK not found)")
endif()

# IPC-06 / D-09 / D-14 / D-15 / D-18 / D-40 — Training HTTP endpoint
# validation envelopes. RED until Plan 09-02 lands the training routes on
# http_server + settings_validator extension. Mirrors P8
# PutSettingsValidation EXISTS-gated source-list registration shape.
set(_p9_training_endpoint_sources driver/training_endpoint_validation_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp"
        AND EXISTS "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp")
    list(APPEND _p9_training_endpoint_sources
        "${CMAKE_SOURCE_DIR}/driver/src/http_server.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/device_provider.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/audio_worker.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_io.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/config_json.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/settings_validator.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/sinks/driver_log_sink.cpp"
        "${CMAKE_SOURCE_DIR}/driver/src/training_session.cpp")
    if(EXISTS "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
        list(APPEND _p9_training_endpoint_sources
            "${CMAKE_SOURCE_DIR}/driver/src/training_io.cpp")
    endif()
endif()
if(OpenVR_FOUND)
    add_executable(test_training_endpoint_validation ${_p9_training_endpoint_sources})
    target_compile_features(test_training_endpoint_validation PRIVATE cxx_std_17)
    target_include_directories(test_training_endpoint_validation PRIVATE
        ${CMAKE_SOURCE_DIR}/driver/src)
    target_link_libraries(test_training_endpoint_validation PRIVATE
        micmap::core_runtime
        micmap::bindings
        httplib::httplib
        OpenVR::openvr_api
        nlohmann_json)
    target_compile_definitions(test_training_endpoint_validation PRIVATE
        CPPHTTPLIB_NO_EXCEPTIONS)
    add_test(NAME TrainingEndpointValidation
        COMMAND test_training_endpoint_validation)
else()
    message(STATUS "TrainingEndpointValidation: skipped (OpenVR SDK not found)")
endif()

# TEST-04 / D-30 / D-32 / D-34 — WAV replay decode + downmix + linear-resample
# + JSON output schema + determinism. RED until Plan 09-04 lands
# apps/mic_test/src/wav_replay.{hpp,cpp} + vendor/dr_wav/dr_wav.h.
#
# IMPORTANT: NO if(OpenVR_FOUND) wrapper here. The replay harness is
# headless per TEST-01 invariant — it must build and run on host CI workers
# that have no SteamVR runtime. The EXISTS-gate on wav_replay.cpp keeps the
# executable buildable at Wave 0 (the missing #include on wav_replay.hpp is
# the build-time RED gate per D-37).
set(_p9_wav_replay_sources mic_test/wav_replay_test.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/apps/mic_test/src/wav_replay.cpp")
    list(APPEND _p9_wav_replay_sources
        "${CMAKE_SOURCE_DIR}/apps/mic_test/src/wav_replay.cpp")
endif()
add_executable(test_wav_replay ${_p9_wav_replay_sources})
target_compile_features(test_wav_replay PRIVATE cxx_std_17)
target_include_directories(test_wav_replay PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/mic_test/src
    ${CMAKE_SOURCE_DIR}/vendor/dr_wav)
target_link_libraries(test_wav_replay PRIVATE
    micmap::core_runtime
    nlohmann_json)
add_test(NAME WavReplayHarness
    COMMAND test_wav_replay)

# ---- end Phase 9 Wave 0 ----

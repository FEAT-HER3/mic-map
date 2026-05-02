# src/CMakeLists.txt
# MicMap source libraries

add_subdirectory(common)
add_subdirectory(audio)
add_subdirectory(bindings)
add_subdirectory(detection)
add_subdirectory(steamvr)
add_subdirectory(core)

# Phase 5 shared-runtime aggregation (D-05 / D-06 / D-07).
#
# micmap_core_runtime is the INTERFACE boundary that compiles into the driver
# DLL, the desktop client EXE, and the headless mic_test.exe. It re-exports the
# four OpenVR-free STATIC sub-libs and propagates cxx_std_17 to consumers.
#
# micmap_steamvr is DELIBERATELY EXCLUDED — it links OpenVR::openvr_api PUBLIC
# (src/steamvr/CMakeLists.txt:50), and aggregating it would propagate OpenVR
# into every consumer of the runtime, breaking SC1 (headless build) and SC2
# (no-OpenVR-in-shared-layer guard at cmake/AssertNoOpenVRInCore.cmake).
#
# The four sub-libs already publish their public headers via
# target_include_directories(... PUBLIC $<BUILD_INTERFACE:...>), so consumers
# inherit include paths transitively through this INTERFACE target — no
# umbrella header, no extra target_include_directories call here (D-14).
add_library(micmap_core_runtime INTERFACE)
target_link_libraries(micmap_core_runtime INTERFACE
    micmap_core
    micmap_audio
    micmap_detection
    micmap_common
)
target_compile_features(micmap_core_runtime INTERFACE cxx_std_17)
add_library(micmap::core_runtime ALIAS micmap_core_runtime)
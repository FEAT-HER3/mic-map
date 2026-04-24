# src/bindings/CMakeLists.txt
# MicMap bindings-patcher shared library (Phase 4 D-10 lift)
#
# This library hosts the C++ logic that patches SteamVR's system-level
# generic-HMD vrcompositor bindings so an HMD's /input/system/click routes
# to the SteamVR dashboard + head-locked lasermouse leftclick. Lifted out
# of driver/src/bindings_patcher.{hpp,cpp} so that both driver_micmap.dll
# and micmap.exe can link a single source of truth.
#
# DriverLog dependency is replaced by an injected LogSink
# (std::function<void(const char*)>) so the library carries no driver-only
# symbols and remains testable without a live SteamVR runtime.

add_library(micmap_bindings STATIC
    src/bindings_patcher.cpp
)

target_include_directories(micmap_bindings
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# nlohmann/json is exposed in the public header (AtomicWriteJson signature),
# so linkage must be PUBLIC.
if(TARGET nlohmann_json)
    target_link_libraries(micmap_bindings PUBLIC nlohmann_json)
elseif(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(micmap_bindings PUBLIC nlohmann_json::nlohmann_json)
else()
    message(FATAL_ERROR "micmap_bindings: nlohmann/json is required.")
endif()

target_compile_features(micmap_bindings PUBLIC cxx_std_17)

# Alias target for consistent namespace-style consumption from downstream
# targets: target_link_libraries(... PRIVATE micmap::bindings)
add_library(micmap::bindings ALIAS micmap_bindings)

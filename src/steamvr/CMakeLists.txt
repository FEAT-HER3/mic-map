# src/steamvr/CMakeLists.txt
# SteamVR integration library

add_library(micmap_steamvr STATIC
    src/vr_input.cpp
    src/dashboard_manager.cpp
)

target_include_directories(micmap_steamvr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_steamvr
    PUBLIC
        micmap_common
)

target_compile_features(micmap_steamvr PUBLIC cxx_std_17)

# Optional: Link OpenXR if available
if(TARGET OpenXR::openxr_loader)
    target_link_libraries(micmap_steamvr PRIVATE OpenXR::openxr_loader)
    target_compile_definitions(micmap_steamvr PRIVATE MICMAP_HAS_OPENXR)
endif()

# Optional: Link OpenVR if available
if(TARGET OpenVR::openvr_api)
    target_link_libraries(micmap_steamvr PRIVATE OpenVR::openvr_api)
    target_compile_definitions(micmap_steamvr PRIVATE MICMAP_HAS_OPENVR)
endif()

# Add alias for consistent naming
add_library(micmap::steamvr ALIAS micmap_steamvr)
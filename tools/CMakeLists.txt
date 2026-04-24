# tools/CMakeLists.txt
# Ad-hoc helpers used during Phase 03 empirical bring-up. NOT shipped to end
# users — intentionally lives outside apps/ to make the throwaway intent
# obvious. Delete this subdirectory (and the matching add_subdirectory line in
# the root CMakeLists.txt) after Phase 03 exit.
#
# Targets:
#   register_manifest — registers an arbitrary manifest path with SteamVR via
#                       IVRApplications::AddApplicationManifest. Used by
#                       Plan 03-02 Task 2 to A/B test the `arguments` field
#                       form (string vs array) deterministically without
#                       fighting SteamVR UI state.

if(WIN32 AND TARGET OpenVR::openvr_api)
    add_executable(register_manifest register_manifest.cpp)
    target_compile_features(register_manifest PRIVATE cxx_std_17)
    target_link_libraries(register_manifest
        PRIVATE
            micmap_common
            OpenVR::openvr_api
    )
    target_compile_definitions(register_manifest PRIVATE MICMAP_HAS_OPENVR=1)
    set_target_properties(register_manifest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

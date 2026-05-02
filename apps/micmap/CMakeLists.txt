# apps/micmap/CMakeLists.txt
# MicMap Main Application - ImGui GUI with SteamVR Integration

# Source files
set(MICMAP_SOURCES
    main.cpp
    first_launch_balloon.cpp
)

# ---- app.vrmanifest generation (Plan 03-02 — D-19) -----------------------
# Explicit set() for MICMAP_VERSION per RESEARCH §Pitfall 10 — guards against
# stale substitutions when project() VERSION is bumped upstream. Use @ONLY so
# only @TOKEN@ markers are substituted (prevents accidental ${...} expansion
# inside JSON values).
#
# configure_file() does not honor generator expressions in its OUTPUT path, so
# we emit to ${CMAKE_CURRENT_BINARY_DIR} and copy into the per-config runtime
# dir via a POST_BUILD step. The schema test
# (test_vrmanifest_schema) reads from ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/
# under multi-config generators (MSBuild), so the POST_BUILD copy is the
# load-bearing step.
set(MICMAP_VERSION ${PROJECT_VERSION})
set(MICMAP_APP_KEY "bigscreen.micmap")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app.vrmanifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest"
    @ONLY
)
# A2 RESOLVED 2026-04-23: STRING form wins. SteamVR launched
# `micmap.exe --minimized` from the string-form manifest on Bigscreen Beyond +
# Windows 11. See .planning/phases/03-auto-start/03-02-A2-RESULT.md for the
# empirical evidence; the array-form variant template has been deleted.

add_executable(micmap WIN32
    ${MICMAP_SOURCES}
)

# Phase 5 D-09: micmap.exe links the new shared runtime aggregate AND keeps
# a direct micmap_steamvr link. The steamvr lib carries IDriverClient + the
# HTTP-bridge plumbing that apps/micmap/main.cpp still calls — that lib lives
# OUTSIDE the shared runtime (D-05) because it links OpenVR PUBLIC, so it must
# remain a direct app-level dependency until later v1.6 phases lift the trigger
# path into the driver. The old umbrella aggregator was deleted in Plan 05-02
# (D-07); micmap::core_runtime fully supersedes it.
target_link_libraries(micmap
    PRIVATE
        micmap::core_runtime
        micmap_steamvr
        imgui
        micmap::bindings
)

target_compile_features(micmap PRIVATE cxx_std_17)

# Include directory for resource.h
target_include_directories(micmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows-specific libraries
# Pathcch.lib (D-21) is precondition for Plan 06's runtime path resolution
# (PathCchRemoveFileSpec / PathCchCombineEx). Built into the Windows SDK
# since Vista — naked target name resolves via MSVC default link search.
if(WIN32)
    target_link_libraries(micmap PRIVATE shell32 dwmapi Pathcch)
endif()

# Set output directory
set_target_properties(micmap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy generated app.vrmanifest into the per-config runtime dir so it sits
# beside micmap.exe (Plan 03-02 acceptance criterion + schema test path).
add_custom_command(TARGET micmap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest"
        "$<TARGET_FILE_DIR:micmap>/app.vrmanifest"
    COMMAND ${CMAKE_COMMAND} -E echo "Generated app.vrmanifest at $<TARGET_FILE_DIR:micmap>/app.vrmanifest"
    COMMENT "Staging app.vrmanifest beside micmap.exe"
    VERBATIM
)

# Copy config files to output directory
add_custom_command(TARGET micmap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:micmap>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/default_config.json"
        "$<TARGET_FILE_DIR:micmap>/config/default_config.json"
    COMMENT "Copying config files..."
)

# Phase 4 INST-07 / D-19: install app.vrmanifest alongside micmap.exe so
# the installer stage dir (cmake --install ... --prefix build/stage) has
# both files in {stage}/bin/ for Inno Setup's [Files] to consume.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest"
        DESTINATION bin)

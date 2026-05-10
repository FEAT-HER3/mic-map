# apps/micmap/CMakeLists.txt
# MicMap Main Application - ImGui GUI with SteamVR Integration

# Source files
set(MICMAP_SOURCES
    main.cpp
    first_launch_balloon.cpp
    src/config_json.cpp           # P8 D-03: AppConfig to_json/from_json ADL hooks (client TU)
    src/config_manager_impl.cpp   # P8 D-02: ConfigManagerImpl relocated from src/core to keep micmap_core JSON-free
    src/tray_glyph.cpp            # Phase 10 / HEALTH-08 D-04..D-06: tray-icon state glyphs (deriveTrayGlyph + Shell_NotifyIcon swap)
)

# ---- app.vrmanifest generation (Plan 03-02 — D-19) -----------------------
# Phase 10 / D-18: MICMAP_VERSION now flows from cmake/version.cmake (the SSoT
# included at root level). The prior `set(MICMAP_VERSION ${PROJECT_VERSION})`
# line is gone — MICMAP_VERSION is in cache scope here via the root include().
# Use @ONLY so only @TOKEN@ markers are substituted (prevents accidental
# ${...} expansion inside JSON values).
#
# configure_file() does not honor generator expressions in its OUTPUT path, so
# we emit to ${CMAKE_CURRENT_BINARY_DIR} and copy into the per-config runtime
# dir via a POST_BUILD step. The schema test
# (test_vrmanifest_schema) reads from ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/
# under multi-config generators (MSBuild), so the POST_BUILD copy is the
# load-bearing step.
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

# Phase 10 / D-18: VS_VERSION_INFO from the SSoT.
# configure_file substitutes @MICMAP_VERSION@ + @MICMAP_VERSION_QUAD@ from
# cmake/version.cmake into apps/micmap/micmap.rc.in -> build/.../micmap.rc;
# target_sources adds the generated .rc to the link line so the resource
# compiler embeds VS_VERSION_INFO into micmap.exe. The icon path token uses
# an absolute source-tree path so the generated .rc (in build/) can find
# installer/micmap.ico regardless of where the build dir lives.
set(MICMAP_ICON_PATH "${CMAKE_SOURCE_DIR}/installer/micmap.ico")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/micmap.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/micmap.rc"
    @ONLY
)
target_sources(micmap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/micmap.rc")

# Phase 10 / D-18: SSoT version string compile define (used by main.cpp's
# version-mismatch check landing in 10-06 via apps/micmap/src/version_mismatch.cpp).
target_compile_definitions(micmap PRIVATE
    MICMAP_VERSION_STRING="${MICMAP_VERSION}"
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
        nlohmann_json   # P8 D-02/D-03: client TU now compiles config_json.cpp + config_manager_impl.cpp
)

target_compile_features(micmap PRIVATE cxx_std_17)

# Include directory for resource.h
target_include_directories(micmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows-specific libraries
# Pathcch.lib (D-21) is precondition for Plan 06's runtime path resolution
# (PathCchRemoveFileSpec / PathCchCombineEx). Built into the Windows SDK
# since Vista — naked target name resolves via MSVC default link search.
if(WIN32)
    # Phase 10 / HEALTH-08 / Pitfall 8: Shlwapi for PathCombineW + PathRemoveFileSpecW
    # used by tray_glyph.cpp::initTrayIcons to resolve resources\\tray_*.ico
    # against the EXE directory (absolute-path resolution; SteamVR launches
    # micmap.exe from arbitrary CWDs).
    target_link_libraries(micmap PRIVATE shell32 dwmapi Pathcch Shlwapi)
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

# Phase 10 / HEALTH-08 / Pitfall 8: stage tray glyphs next to the built
# micmap.exe so initTrayIcons()'s
#   GetModuleFileNameW + PathRemoveFileSpecW + PathCombineW("resources\\tray_*.ico")
# resolution finds them during dev builds (the installer-side wiring lands
# in 10-06 / Inno Setup [Files]; this POST_BUILD covers the build/ dev
# loop so applyTrayGlyph swaps the glyph in a developer's hand-launched
# micmap.exe even before the .iss copy is wired up).
add_custom_command(TARGET micmap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:micmap>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_armed.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_triggered.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_error.ico"
        "$<TARGET_FILE_DIR:micmap>/resources/"
    COMMENT "Staging tray_*.ico beside micmap.exe (HEALTH-08 dev-build resolution)"
    VERBATIM
)

# Phase 10 / HEALTH-08 / Pitfall 8: install rule preview. The matching
# Inno Setup [Files] entry lands in 10-06 (Wave 6); this populates the
# cmake --install staging area so SteamVR-launched micmap.exe resolves
# the tray glyphs via the absolute-path GetModuleFileNameW + PathCombineW
# code in tray_glyph.cpp::initTrayIcons.
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_armed.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_triggered.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/tray_error.ico"
    DESTINATION bin/resources)

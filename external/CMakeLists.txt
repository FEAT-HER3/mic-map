# external/CMakeLists.txt
# Third-party dependencies

include(FetchContent)

# nlohmann/json - Header-only JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)

# Only download, don't add as subdirectory (header-only)
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
endif()

# Create interface library for json
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)

# KissFFT - Lightweight FFT library
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG 131.1.0
)

FetchContent_GetProperties(kissfft)
if(NOT kissfft_POPULATED)
    FetchContent_Populate(kissfft)
endif()

# Create a static library for KissFFT (single-precision float version)
# Note: kiss_fftr.c is in the root directory in newer versions of KissFFT
set(KISSFFT_SOURCES
    ${kissfft_SOURCE_DIR}/kiss_fft.c
    ${kissfft_SOURCE_DIR}/kiss_fftr.c
)

# Mark sources as C language explicitly
set_source_files_properties(${KISSFFT_SOURCES} PROPERTIES LANGUAGE C)

add_library(kissfft STATIC ${KISSFFT_SOURCES})

# Set the linker language explicitly
set_target_properties(kissfft PROPERTIES LINKER_LANGUAGE C)

target_include_directories(kissfft PUBLIC
    ${kissfft_SOURCE_DIR}
)

# Use single-precision floats
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float)

# cpp-httplib - Header-only HTTP library
# Used by both the driver (server) and the main application (client)
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.3
)

FetchContent_GetProperties(cpp_httplib)
if(NOT cpp_httplib_POPULATED)
    FetchContent_Populate(cpp_httplib)
endif()

# Create interface library for httplib
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ${cpp_httplib_SOURCE_DIR})
# Note: httplib auto-detects OpenSSL. We don't need HTTPS for localhost communication,
# but if OpenSSL is found on the system, it will be used. This is fine.
add_library(httplib::httplib ALIAS httplib)

# Dear ImGui - Immediate mode GUI library
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# Create ImGui library with Win32 + DirectX11 backend
if(WIN32)
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    
    add_library(imgui STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi)
endif()

# OpenXR SDK - Optional, for VR integration
# The SDK should be installed separately or provided via environment variable
# See cmake/FindOpenXR.cmake

# OpenVR SDK - Optional, for SteamVR-specific features
# The SDK should be installed separately or provided via environment variable
# See cmake/FindOpenVR.cmake
# external/CMakeLists.txt
# Third-party dependencies

# nlohmann/json - Header-only JSON library
include(FetchContent)

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
# For now, we use a stub implementation in spectral_analyzer.cpp
# When ready to use real KissFFT:
#
# FetchContent_Declare(
#     kissfft
#     GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
#     GIT_TAG v131.1.0
# )
# FetchContent_MakeAvailable(kissfft)

# OpenXR SDK - Optional, for VR integration
# The SDK should be installed separately or provided via environment variable
# See cmake/FindOpenXR.cmake

# OpenVR SDK - Optional, for SteamVR-specific features
# The SDK should be installed separately or provided via environment variable
# See cmake/FindOpenVR.cmake
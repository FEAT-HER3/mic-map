# cmake/AssertNoClientDetection.cmake
#
# Phase 10 / MIG-05 / D-01 / D-23: assert client TUs do not host audio
# capture / FFT detection / state-machine runtime entry points. Driver is
# the sole detection runtime post-cutover (10-05); client is settings +
# driver-health UI only. Allowlist: apps/mic_test/ (the headless detection
# harness — TEST-01 invariant; CONTEXT D-23). Wave 0 RED-tolerant: lint
# script ships now; ctest registration go-live is 10-05 alongside the
# client-side audio/FFT/state-machine body deletion.
#
# Sibling of cmake/AssertNoClientTraining.cmake (verbatim shape — file
# scan via GLOB_RECURSE + per-file forbidden-pattern regex with the
# apps/mic_test allowlist guard). The five forbidden tokens are the v1.5
# client detection entry points; once 10-05 deletes the v1.5 detection
# body in apps/micmap/main.cpp, this lint becomes the structural guardrail
# that prevents the single-detection-runtime rule from regressing.
#
# Qualifier-prefix narrowing (mirror of P9 D-03 Rule-3 deviation): bare
# tokens like `IAudioCapture` would match doc-comments or unrelated
# identifiers; the `[^a-zA-Z0-9_]` boundary is the cmake-regex equivalent
# of a word boundary (cmake regex has no \b). The two call-shape regexes
# `detector->loadTrainingData` and `createFFTDetector` are the specific
# v1.5 client patterns at apps/micmap/main.cpp:298, :326, :647, :970 +
# the FFT detector factory call site; they cannot appear in apps/mic_test/
# under the allowlist guard regardless.
#
# Allowlist defense-in-depth (D-23): the canonical invocation passes only
# `apps/micmap`, but if a future caller widens the scope by mistake, the
# `apps/mic_test` directory match below still keeps the headless detection
# harness exempt under TEST-01.
#
# Invocation (from tests/CMakeLists.txt — wired in 10-05):
#   add_test(NAME AssertNoClientDetection
#       COMMAND ${CMAKE_COMMAND}
#           -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>$<SEMICOLON>...
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoClientDetection.cmake)

if(NOT DEFINED CLIENT_ROOTS)
    message(FATAL_ERROR "AssertNoClientDetection: CLIENT_ROOTS not provided. "
        "Pass -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_violations "")
set(_files_scanned 0)

# Narrower extension set than lint_no_openvr_in_core: client production code
# only (.cpp + .hpp). External / vendored headers in third-party trees are
# not a concern because client roots scope down to apps/micmap.
foreach(_root ${CLIENT_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "AssertNoClientDetection: CLIENT_ROOTS entry is not a directory: ${_root}")
    endif()
    file(GLOB_RECURSE _files
        "${_root}/*.cpp" "${_root}/*.hpp")
    foreach(_file ${_files})
        # D-23 allowlist: the headless detection harness under apps/mic_test/
        # retains IAudioCapture / INoiseDetector / IStateMachine consumers
        # under TEST-01 (driver-free harness). Skip any file whose directory
        # path includes "apps/mic_test" — defense-in-depth even if the
        # canonical invocation passed only apps/micmap.
        get_filename_component(_dir "${_file}" DIRECTORY)
        if(_dir MATCHES "apps/mic_test")
            continue()
        endif()

        math(EXPR _files_scanned "${_files_scanned} + 1")
        file(READ "${_file}" _content)
        # Five-condition disjunction. The first three are the v1.5 client
        # interface tokens (IAudioCapture / INoiseDetector / IStateMachine)
        # narrowed by non-identifier boundary so doc-comments and unrelated
        # identifiers don't false-positive. The last two are the specific
        # call shapes the v1.5 client uses (detector->loadTrainingData at
        # main.cpp:326, :647, :970; createFFTDetector at the detection
        # factory call site).
        if(_content MATCHES "[^a-zA-Z0-9_]IAudioCapture[^a-zA-Z0-9_]"
                OR _content MATCHES "[^a-zA-Z0-9_]INoiseDetector[^a-zA-Z0-9_]"
                OR _content MATCHES "[^a-zA-Z0-9_]IStateMachine[^a-zA-Z0-9_]"
                OR _content MATCHES "detector->loadTrainingData"
                OR _content MATCHES "createFFTDetector")
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoClientDetection: ${_vcount} file(s) violate the single-detection-runtime rule (P10 D-01 / D-23):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH CLIENT_ROOTS _rootcount)
message(STATUS "AssertNoClientDetection: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

# cmake/AssertNoClientTraining.cmake
#
# Phase 9 / IPC-06 / D-05 / D-23: assert client TUs do not call
# PatternTrainer / INoiseDetector training entry points. Driver is sole
# trainer (TRAIN-AF-01); client edits flow through POST
# /training/start | finalize | cancel | recompute. Allowlist:
# apps/mic_test/ (the headless training tool — TEST-01 invariant; CONTEXT
# D-06).
#
# Sibling of cmake/AssertNoConfigWriteInClient.cmake (verbatim shape — file
# scan via GLOB_RECURSE + per-file forbidden-pattern regex). The four
# forbidden tokens are the v1.5 client training entry points; once 09-03
# deletes the v1.5 training body in apps/micmap/main.cpp, this lint becomes
# the structural guardrail that prevents the single-trainer rule from
# regressing.
#
# RED-tolerant by GLOB_RECURSE form: lint stays clean if no client TU has
# yet been migrated; fires the moment any client TU calls a training entry
# point. Wave 0 (Plan 09-00) deliberately defers ctest registration — Plan
# 09-03 deletes the last training callsites in apps/micmap/main.cpp:404,
# 618, 962-1027, then registers this lint as a ctest in tests/CMakeLists.txt
# (mirrors the P8 D-07 timing for AssertNoConfigWriteInClient).
#
# 09-03 Rule-3 narrowing (executor): the IDriverApi training methods
# (the new endpoint-driven pane) include a method also named
# IDriverApi::startTraining, which collides with the original token-only
# regex `[^a-zA-Z0-9_]startTraining`. To allow `driverClient->startTraining`
# (the new endpoint-driven pane is the very thing 09-03 ships) while still
# forbidding `detector->startTraining` (the v1.5 PatternTrainer/INoiseDetector
# entry point that single-writer cutover deletes), we tighten the regex to
# match the `detector->` member-call form for ALL four PatternTrainer/
# INoiseDetector entry points. CMake regex has no negative lookbehind, so
# the qualifier-based narrowing is the simplest way to differentiate the two
# call surfaces. The intent of the lint is unchanged: client TUs must not
# call into the v1.5 PatternTrainer/INoiseDetector training surface; they
# must go through IDriverApi instead. The `detector` identifier is the
# canonical handle for the INoiseDetector instance throughout
# apps/micmap/main.cpp (the std::unique_ptr<INoiseDetector> member). Any
# future TU that wants to access INoiseDetector via a different alias would
# additionally have to dance around the v1.5 naming convention; that is an
# acceptable trade-off because the alias-based bypass requires deliberate
# refactor (caught at code review).
#
# Allowlist defense-in-depth (D-06): the canonical invocation passes only
# `apps/micmap`, but if a future caller widens the scope by mistake, the
# `apps/mic_test` directory match below still keeps the headless training
# tool exempt.
#
# Invocation (from tests/CMakeLists.txt — wired in 09-03):
#   add_test(NAME AssertNoClientTraining
#       COMMAND ${CMAKE_COMMAND}
#           -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>$<SEMICOLON>...
#           -P ${CMAKE_SOURCE_DIR}/cmake/AssertNoClientTraining.cmake)

if(NOT DEFINED CLIENT_ROOTS)
    message(FATAL_ERROR "AssertNoClientTraining: CLIENT_ROOTS not provided. "
        "Pass -DCLIENT_ROOTS=<root1>$<SEMICOLON><root2>...")
endif()

set(_violations "")
set(_files_scanned 0)

# Narrower extension set than lint_no_openvr_in_core: client production code
# only (.cpp + .hpp). External / vendored headers in third-party trees are
# not a concern because client roots scope down to apps/micmap.
foreach(_root ${CLIENT_ROOTS})
    if(NOT IS_DIRECTORY "${_root}")
        message(FATAL_ERROR "AssertNoClientTraining: CLIENT_ROOTS entry is not a directory: ${_root}")
    endif()
    file(GLOB_RECURSE _files
        "${_root}/*.cpp" "${_root}/*.hpp")
    foreach(_file ${_files})
        # D-06 allowlist: the headless training tool under apps/mic_test/
        # retains startTraining/addTrainingSample/finishTraining/saveTrainingData
        # under TEST-01 (driver-free harness). Skip any file whose directory
        # path includes "apps/mic_test" — defense-in-depth even if the canonical
        # invocation passed only apps/micmap.
        get_filename_component(_dir "${_file}" DIRECTORY)
        if(_dir MATCHES "apps/mic_test")
            continue()
        endif()

        math(EXPR _files_scanned "${_files_scanned} + 1")
        file(READ "${_file}" _content)
        # Four-condition disjunction: the v1.5 client training entry points
        # on the PatternTrainer / INoiseDetector surface. The qualifier
        # `detector->` is the canonical std::unique_ptr<INoiseDetector>
        # member name across apps/micmap; matching the qualifier-prefixed
        # form distinguishes these v1.5 entry points from the IDriverApi
        # endpoint-driven methods on driverClient (which include a method
        # named startTraining whose call form is `driverClient->startTraining`).
        # See header comment "09-03 Rule-3 narrowing" for the rationale.
        if(_content MATCHES "detector->addTrainingSample"
                OR _content MATCHES "detector->finishTraining"
                OR _content MATCHES "detector->startTraining"
                OR _content MATCHES "detector->saveTrainingData")
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(LENGTH _violations _vcount)
    set(_msg "AssertNoClientTraining: ${_vcount} file(s) violate the single-trainer rule (P9 D-05 / D-23):")
    foreach(_v ${_violations})
        string(APPEND _msg "\n  - ${_v}")
    endforeach()
    message(FATAL_ERROR "${_msg}")
endif()

list(LENGTH CLIENT_ROOTS _rootcount)
message(STATUS "AssertNoClientTraining: clean (${_files_scanned} files scanned across ${_rootcount} roots)")

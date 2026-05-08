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
        # Four-condition disjunction: the v1.5 client training entry points.
        # CMake regex (POSIX-like) does not support \b word-boundary; instead
        # require a non-identifier char OR start-of-file before the token.
        # Mirrors the cmake/lint_no_openvr_in_core.cmake "[^a-zA-Z0-9_]vr::"
        # idiom — prevents matching these tokens as substrings of longer
        # identifiers (e.g. a hypothetical "myAddTrainingSample" stays unflagged).
        # Comments and string literals that name these tokens DO match — that
        # is the intended behaviour: when 09-03 deletes the v1.5 training body
        # the comments go too, and any future client TU that even mentions
        # these entry points trips the lint and forces a re-review.
        if(_content MATCHES "[^a-zA-Z0-9_]addTrainingSample"
                OR _content MATCHES "[^a-zA-Z0-9_]finishTraining"
                OR _content MATCHES "[^a-zA-Z0-9_]startTraining"
                OR _content MATCHES "[^a-zA-Z0-9_]saveTrainingData"
                OR _content MATCHES "^addTrainingSample"
                OR _content MATCHES "^finishTraining"
                OR _content MATCHES "^startTraining"
                OR _content MATCHES "^saveTrainingData")
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

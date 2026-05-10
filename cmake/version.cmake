# cmake/version.cmake
# Phase 10 / INST-09 / D-18: Single source of truth for MicMap semver.
#
# All consumers (driver target, client target, generated installer/version.iss,
# /health driver_version field, VS_VERSION_INFO RC resources) read from here.
# Bump MICMAP_VERSION below for releases:
#   PATCH for bugfix releases; MINOR for milestone releases; MAJOR for breaking
#   protocol changes (HTTP / driver-client mismatch surface).
#
# AssertCoVersioning lint (cmake/AssertCoVersioning.cmake) re-reads this file
# at configure-test time and FATAL_ERRORs if the value here drifts from the
# value baked into the generated installer/version.iss (Pattern 4 + Pitfall 5
# in .planning/phases/10-cutover-cleanup/RESEARCH.md).

set(MICMAP_VERSION "1.6.0")

# Validate semver shape (defensive — future-proofing if someone passes "1.6"
# or "1.6.0-rc1"; v1.6 ships strict MAJOR.MINOR.PATCH per D-18).
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+$" _vmatch "${MICMAP_VERSION}")
if(NOT _vmatch)
    message(FATAL_ERROR
        "cmake/version.cmake: MICMAP_VERSION must be 'MAJOR.MINOR.PATCH' "
        "(no pre-release suffix in v1.6); got: ${MICMAP_VERSION}")
endif()

# Split into VERSIONINFO numeric quad: A.B.C -> A,B,C,0 (RC FILEVERSION /
# PRODUCTVERSION literal form per Microsoft Learn VERSIONINFO reference).
string(REPLACE "." ";" _vparts "${MICMAP_VERSION}")
list(GET _vparts 0 MICMAP_VERSION_MAJOR)
list(GET _vparts 1 MICMAP_VERSION_MINOR)
list(GET _vparts 2 MICMAP_VERSION_PATCH)
set(MICMAP_VERSION_QUAD "${MICMAP_VERSION_MAJOR},${MICMAP_VERSION_MINOR},${MICMAP_VERSION_PATCH},0")

message(STATUS "MicMap version: ${MICMAP_VERSION} (RC quad: ${MICMAP_VERSION_QUAD})")

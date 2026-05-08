/**
 * @file training_io.hpp
 * @brief Phase 9 / IPC-06 / D-23 / D-27: driver-side training_data.bin I/O.
 *
 * Atomic ReplaceFileW + corruption-backup persistence for
 * %APPDATA%\\MicMap\\training_data.bin (driver-as-sole-writer per IPC-06).
 *
 * Mirror of driver/src/config_io.{hpp,cpp} (P8 D-10/D-14) with one deviation:
 * training_data.bin is binary (INoiseDetector internal format), not utf-8
 * JSON. The atomic-write helper accepts an INoiseDetector& and asks it to
 * serialize directly to the .tmp path via the existing ofstream code at
 * src/detection/src/noise_detector.cpp:329 — preserves v1.5 binary format
 * byte-for-byte. ReplaceFileW then swaps tmp over dest atomically (NTFS).
 *
 * Read path is unchanged: callers re-use INoiseDetector::loadTrainingData
 * directly per CONTEXT D-24/D-25; only the *write* path needs atomic
 * wrapping. No JSON dependency here — training_data.bin is binary.
 *
 * Driver-only TU (Pitfall 15 / D-01). The .cpp pulls
 * <micmap/detection/noise_detector.hpp>; AssertNoJsonInCore lint scope is
 * src/audio | src/detection | src/core | src/common — driver/src/ is
 * outside that scope.
 */

#pragma once

#include <filesystem>

namespace micmap::detection { class INoiseDetector; }   // forward decl; full include in .cpp

namespace micmap::driver {

/// @brief Resolve %APPDATA%\\MicMap\\training_data.bin (Windows). Mirrors
///        the v1.5 helper at detection_runner.cpp:127 (D-26). Reuses the
///        same SHGetFolderPathW idiom as getDriverConfigPath() / config_io.
std::filesystem::path getDriverTrainingDataPath();

/// @brief Atomic save via ReplaceFileW (D-27 + v1.5 CFG-04 mirror). Asks
///        the detector to serialize to a sibling `.tmp` path, then renames
///        atomically over the destination. On rename failure, falls back
///        to MoveFileExW(MOVEFILE_REPLACE_EXISTING). The 5-generation
///        corruption-backup ring lives in the .cpp (helper backupAndRotate);
///        09-01 ships the helper for the future load-side corruption handler
///        but does NOT call it from the save success path (would destroy
///        the just-written file — Rule 1 deviation from 09-01-PLAN.md
///        Task 1; mirror of config_io.cpp where backupAndRotate fires from
///        loadConfigJson on parse failure, not from saveConfigJson).
///
///        Returns false on disk-full, ACL block, detector serialize failure,
///        or temp-file failure.
bool saveTrainingFile(const std::filesystem::path& path,
                      micmap::detection::INoiseDetector& detector);

} // namespace micmap::driver

/**
 * @file driver_mode.hpp
 * @brief Phase 9 / D-01: driver mode enum (Detecting | Training).
 *
 * Single-purpose header so detection_runner.hpp and device_provider.hpp
 * can share the enum without creating a circular include between the two
 * larger headers (device_provider.hpp already includes detection_runner.hpp;
 * detection_runner.hpp now needs to know about DriverMode without
 * pulling device_provider.hpp).
 *
 * Atomic discipline: stored as std::atomic<DriverMode> on DeviceProvider;
 * release-store from HTTP thread (after constructing TrainingSession);
 * acquire-load from DetectionRunner per ring-drain iteration. Mirror of
 * the activeConfig_ release/acquire pattern at detection_runner.cpp:101.
 */

#pragma once

#include <cstdint>

namespace micmap::driver {

enum class DriverMode : uint8_t {
    Detecting,   ///< default: detection thread runs detector_->analyze + state machine
    Training     ///< detection thread forwards each block to TrainingSession::addSample
};

} // namespace micmap::driver

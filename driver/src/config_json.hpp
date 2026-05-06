/**
 * @file config_json.hpp (driver TU)
 * @brief Phase 8 D-03: forward declarations of the AppConfig nlohmann/json
 *        ADL hooks defined in config_json.cpp.
 *
 * nlohmann/json's ADL resolution requires the to_json / from_json overloads
 * to be DECLARED in the same TU that triggers serialization (e.g. via
 * `j = cfg;` or `j.get_to(cfg)`). Definitions in config_json.cpp are not
 * sufficient on their own - this header provides the declarations so
 * config_io.cpp (and any future driver TU using the conversion) can find
 * them via ADL.
 */

#pragma once

#include "micmap/core/config_manager.hpp"
#include <nlohmann/json.hpp>

namespace micmap::core {

void to_json(nlohmann::json& j, const AudioConfig& c);
void from_json(const nlohmann::json& j, AudioConfig& c);

void to_json(nlohmann::json& j, const DetectionConfig& c);
void from_json(const nlohmann::json& j, DetectionConfig& c);

void to_json(nlohmann::json& j, const SteamVRConfig& c);
void from_json(const nlohmann::json& j, SteamVRConfig& c);

void to_json(nlohmann::json& j, const TrainingConfig& c);
void from_json(const nlohmann::json& j, TrainingConfig& c);

void to_json(nlohmann::json& j, const AppConfig& c);
void from_json(const nlohmann::json& j, AppConfig& c);

} // namespace micmap::core

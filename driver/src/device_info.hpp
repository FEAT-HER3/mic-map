/**
 * @file device_info.hpp
 * @brief Phase 8 D-17 / IPC-03: DeviceInfo POD struct returned by GET /devices.
 *
 * Lives in its own header so test scaffolds can include it without pulling
 * in the full http_server.hpp surface. The deviceLister callback in HttpServer
 * has signature `std::function<std::vector<DeviceInfo>()>`.
 *
 * UTF-8 strings (driver converts std::wstring endpoint IDs from WASAPI into
 * UTF-8 before returning to HTTP clients).
 */

#pragma once

#include <string>

namespace micmap::driver {

struct DeviceInfo {
    std::string id;          // UTF-8 of std::wstring endpoint id
    std::string name;        // UTF-8 of std::wstring friendly name
    bool isDefault{false};
};

} // namespace micmap::driver

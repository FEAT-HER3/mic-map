/**
 * @file driver_main.cpp
 * @brief OpenVR driver entry point for MicMap virtual controller
 *
 * This file contains the driver factory function that SteamVR calls to
 * initialize the driver. The driver provides a virtual controller that
 * can inject button events for dashboard interaction.
 */

#include <openvr_driver.h>
#include "device_provider.hpp"

// Global driver instance
static micmap::driver::DeviceProvider g_deviceProvider;

/**
 * @brief Driver factory function called by SteamVR
 *
 * SteamVR calls this function to get the driver's interface implementations.
 * We return our device provider for the IServerTrackedDeviceProvider interface.
 *
 * @param pInterfaceName The interface name being requested
 * @param pReturnCode Return code for the operation
 * @return Pointer to the requested interface, or nullptr if not supported
 */
extern "C" __declspec(dllexport) void* HmdDriverFactory(
    const char* pInterfaceName,
    int* pReturnCode)
{
    // Check if SteamVR is requesting the server tracked device provider
    if (std::string(pInterfaceName) == vr::IServerTrackedDeviceProvider_Version) {
        return &g_deviceProvider;
    }

    // Interface not supported
    if (pReturnCode) {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }

    return nullptr;
}
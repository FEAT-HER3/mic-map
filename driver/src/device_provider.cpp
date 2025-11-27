/**
 * @file device_provider.cpp
 * @brief Implementation of the OpenVR device provider
 */

#include "device_provider.hpp"
#include "virtual_controller.hpp"
#include "http_server.hpp"

#include <cstring>

// Use OpenVR driver context macros
using namespace vr;

namespace micmap::driver {

// Interface versions we support
static const char* const k_InterfaceVersions[] = {
    ITrackedDeviceServerDriver_Version,
    IServerTrackedDeviceProvider_Version,
    nullptr
};

DeviceProvider::DeviceProvider() = default;

DeviceProvider::~DeviceProvider() {
    Cleanup();
}

EVRInitError DeviceProvider::Init(IVRDriverContext* pDriverContext) {
    // Initialize the driver context - this sets up VR_INIT_SERVER_DRIVER_CONTEXT
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    // Log initialization
    DriverLog("MicMap driver initializing...\n");

    // Create the virtual controller
    controller_ = std::make_unique<VirtualController>();

    // Add the controller to SteamVR's tracked device list
    // The serial number must be unique
    if (!VRServerDriverHost()->TrackedDeviceAdded(
            controller_->GetSerialNumber(),
            TrackedDeviceClass_Controller,
            controller_.get())) {
        DriverLog("Failed to add virtual controller to SteamVR\n");
        return VRInitError_Driver_Failed;
    }

    DriverLog("Virtual controller added successfully\n");

    // Create and start the HTTP server for receiving commands
    httpServer_ = std::make_unique<HttpServer>(controller_.get());
    if (!httpServer_->Start()) {
        DriverLog("Failed to start HTTP server\n");
        return VRInitError_Driver_Failed;
    }

    DriverLog("HTTP server started on port %d\n", httpServer_->GetPort());

    initialized_ = true;
    DriverLog("MicMap driver initialized successfully\n");

    return VRInitError_None;
}

void DeviceProvider::Cleanup() {
    if (!initialized_) {
        return;
    }

    DriverLog("MicMap driver cleaning up...\n");

    // Stop the HTTP server
    if (httpServer_) {
        httpServer_->Stop();
        httpServer_.reset();
    }

    // Clean up the controller
    controller_.reset();

    initialized_ = false;

    // Clean up driver context
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();

    DriverLog("MicMap driver cleanup complete\n");
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return k_InterfaceVersions;
}

void DeviceProvider::RunFrame() {
    // Called each frame by SteamVR
    // We can use this to process any pending operations
    
    if (controller_) {
        controller_->RunFrame();
    }
}

bool DeviceProvider::ShouldBlockStandbyMode() {
    // We don't need to block standby
    return false;
}

void DeviceProvider::EnterStandby() {
    DriverLog("MicMap driver entering standby\n");
}

void DeviceProvider::LeaveStandby() {
    DriverLog("MicMap driver leaving standby\n");
}

} // namespace micmap::driver
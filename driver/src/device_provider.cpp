/**
 * @file device_provider.cpp
 * @brief Implementation of the OpenVR device provider
 */

#include "device_provider.hpp"
#include "virtual_controller.hpp"
#include "http_server.hpp"
#include "process_launcher.hpp"
#include "driver_log.hpp"

#include <cstring>
#include <filesystem>

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

    // Launch MicMap application if auto-launch is enabled
    if (!launchMicMapApp()) {
        DriverLog("Warning: Failed to auto-launch MicMap application\n");
        // Don't fail initialization - the driver can still work without the app
    }

    initialized_ = true;
    DriverLog("MicMap driver initialized successfully\n");

    return VRInitError_None;
}

void DeviceProvider::Cleanup() {
    if (!initialized_) {
        return;
    }

    DriverLog("MicMap driver cleaning up...\n");

    // Terminate MicMap application if we launched it
    terminateMicMapApp();

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

bool DeviceProvider::launchMicMapApp() {
    // Check if auto-launch is enabled in settings
    bool autoLaunch = VRSettings()->GetBool("driver_micmap", "autoLaunchApp");
    
    // If the setting doesn't exist, default to true
    EVRSettingsError error;
    VRSettings()->GetBool("driver_micmap", "autoLaunchApp", &error);
    if (error == VRSettingsError_UnsetSettingHasNoDefault) {
        autoLaunch = true;
    }

    if (!autoLaunch) {
        DriverLog("Auto-launch is disabled in settings\n");
        return true;  // Not an error, just disabled
    }

    // Get the application path
    std::string appPath = getMicMapAppPath();
    if (appPath.empty()) {
        DriverLog("Could not determine MicMap application path\n");
        return false;
    }

    // Check if the file exists
    if (!std::filesystem::exists(appPath)) {
        DriverLog("MicMap application not found at: %s\n", appPath.c_str());
        return false;
    }

    // Get command line arguments from settings
    char argsBuffer[1024] = "";
    VRSettings()->GetString("driver_micmap", "appArgs", argsBuffer, sizeof(argsBuffer));
    std::string appArgs(argsBuffer);

    DriverLog("Launching MicMap application: %s %s\n", appPath.c_str(), appArgs.c_str());

    // Launch the process
    micmapProcess_ = ProcessLauncher::launchProcess(appPath, appArgs);
    
    if (micmapProcess_.isValid()) {
        micmapLaunchedByUs_ = true;
        DriverLog("MicMap application launched successfully\n");
        return true;
    } else {
        DriverLog("Failed to launch MicMap application\n");
        return false;
    }
}

void DeviceProvider::terminateMicMapApp() {
    if (!micmapLaunchedByUs_ || !micmapProcess_.isValid()) {
        return;
    }

    DriverLog("Terminating MicMap application...\n");

    // Check if the process is still running
    if (!ProcessLauncher::isProcessRunning(micmapProcess_)) {
        DriverLog("MicMap application already terminated\n");
        micmapProcess_.close();
        micmapLaunchedByUs_ = false;
        return;
    }

    // Terminate the process (try graceful first, then force)
    if (ProcessLauncher::terminateProcess(micmapProcess_, 3000)) {
        DriverLog("MicMap application terminated successfully\n");
    } else {
        DriverLog("Warning: Could not terminate MicMap application cleanly\n");
    }

    micmapLaunchedByUs_ = false;
}

std::string DeviceProvider::getMicMapAppPath() {
    // First, check if a custom path is specified in settings
    char pathBuffer[1024] = "";
    VRSettings()->GetString("driver_micmap", "appPath", pathBuffer, sizeof(pathBuffer));
    std::string customPath(pathBuffer);

    if (!customPath.empty()) {
        // If it's a relative path, resolve it relative to the driver directory
        std::filesystem::path path(customPath);
        if (path.is_relative()) {
            return ProcessLauncher::resolveRelativePath(customPath);
        }
        return customPath;
    }

    // Default path: relative to driver installation
    // Driver is at: <steamvr>/drivers/micmap/bin/win64/driver_micmap.dll
    // MicMap app should be at: <steamvr>/drivers/micmap/apps/micmap.exe
    // So relative path from driver DLL is: ../../apps/micmap.exe
    std::string defaultRelativePath = "../../apps/micmap.exe";
    std::string resolvedPath = ProcessLauncher::resolveRelativePath(defaultRelativePath);
    
    DriverLog("Default MicMap app path: %s\n", resolvedPath.c_str());
    
    return resolvedPath;
}

} // namespace micmap::driver
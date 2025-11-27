/**
 * @file virtual_controller.cpp
 * @brief Implementation of the virtual controller device
 */

#include "virtual_controller.hpp"
#include <cstring>
#include <algorithm>

using namespace vr;

namespace micmap::driver {

VirtualController::VirtualController() {
    DriverLog("VirtualController created with serial: %s\n", serialNumber_.c_str());
}

VirtualController::~VirtualController() {
    DriverLog("VirtualController destroyed\n");
}

EVRInitError VirtualController::Activate(uint32_t unObjectId) {
    deviceIndex_ = unObjectId;
    propertyContainer_ = VRProperties()->TrackedDeviceToPropertyContainer(deviceIndex_);

    DriverLog("VirtualController activating with device index %d\n", deviceIndex_);

    // Set device properties
    VRProperties()->SetStringProperty(propertyContainer_, Prop_ModelNumber_String, "MicMap Virtual Controller");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_ManufacturerName_String, "MicMap");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_RenderModelName_String, "generic_controller");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_TrackingSystemName_String, "micmap");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_SerialNumber_String, serialNumber_.c_str());
    
    // Controller-specific properties
    VRProperties()->SetInt32Property(propertyContainer_, Prop_ControllerRoleHint_Int32, TrackedControllerRole_OptOut);
    VRProperties()->SetStringProperty(propertyContainer_, Prop_ControllerType_String, "micmap_controller");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_InputProfilePath_String, "{micmap}/input/micmap_controller_profile.json");
    
    // This controller doesn't provide tracking
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_NeverTracked_Bool, true);
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_WillDriftInYaw_Bool, false);
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_DeviceIsWireless_Bool, false);
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_DeviceIsCharging_Bool, false);
    VRProperties()->SetFloatProperty(propertyContainer_, Prop_DeviceBatteryPercentage_Float, 1.0f);
    
    // Create input components
    // System button - typically used for dashboard interaction
    VRDriverInput()->CreateBooleanComponent(propertyContainer_, "/input/system/click", &systemButtonHandle_);
    
    // A button - alternative select button
    VRDriverInput()->CreateBooleanComponent(propertyContainer_, "/input/a/click", &aButtonHandle_);

    DriverLog("VirtualController activated successfully\n");
    DriverLog("  System button handle: %llu\n", systemButtonHandle_);
    DriverLog("  A button handle: %llu\n", aButtonHandle_);

    return VRInitError_None;
}

void VirtualController::Deactivate() {
    DriverLog("VirtualController deactivating\n");
    deviceIndex_ = k_unTrackedDeviceIndexInvalid;
    propertyContainer_ = k_ulInvalidPropertyContainer;
    systemButtonHandle_ = k_ulInvalidInputComponentHandle;
    aButtonHandle_ = k_ulInvalidInputComponentHandle;
}

void VirtualController::EnterStandby() {
    DriverLog("VirtualController entering standby\n");
}

void* VirtualController::GetComponent(const char* pchComponentNameAndVersion) {
    // We don't expose any additional components
    return nullptr;
}

void VirtualController::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize > 0) {
        pchResponseBuffer[0] = '\0';
    }
}

DriverPose_t VirtualController::GetPose() {
    DriverPose_t pose = {};
    
    // This controller doesn't have a physical position
    // Return a pose that indicates it's not tracked
    pose.poseIsValid = false;
    pose.deviceIsConnected = true;
    pose.result = TrackingResult_Running_OK;
    
    // Identity quaternion
    pose.qWorldFromDriverRotation.w = 1;
    pose.qWorldFromDriverRotation.x = 0;
    pose.qWorldFromDriverRotation.y = 0;
    pose.qWorldFromDriverRotation.z = 0;
    
    pose.qDriverFromHeadRotation.w = 1;
    pose.qDriverFromHeadRotation.x = 0;
    pose.qDriverFromHeadRotation.y = 0;
    pose.qDriverFromHeadRotation.z = 0;
    
    return pose;
}

void VirtualController::UpdateButtonState(VRInputComponentHandle_t button, bool pressed) {
    if (button == k_ulInvalidInputComponentHandle) {
        DriverLog("Cannot update button state: invalid handle\n");
        return;
    }

    VRDriverInput()->UpdateBooleanComponent(button, pressed, 0.0);
    DriverLog("Button %llu state updated to %s\n", button, pressed ? "pressed" : "released");
}

void VirtualController::PressSystemButton() {
    if (!IsActive()) {
        DriverLog("Cannot press system button: controller not active\n");
        return;
    }

    DriverLog("Pressing system button\n");
    systemButtonPressed_ = true;
    UpdateButtonState(systemButtonHandle_, true);
}

void VirtualController::ReleaseSystemButton() {
    if (!IsActive()) {
        DriverLog("Cannot release system button: controller not active\n");
        return;
    }

    DriverLog("Releasing system button\n");
    systemButtonPressed_ = false;
    UpdateButtonState(systemButtonHandle_, false);
}

void VirtualController::ClickSystemButton(int durationMs) {
    if (!IsActive()) {
        DriverLog("Cannot click system button: controller not active\n");
        return;
    }

    DriverLog("Clicking system button (duration: %d ms)\n", durationMs);
    
    // Press the button
    PressSystemButton();
    
    // Schedule release
    std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
    pendingReleases_.push_back({
        systemButtonHandle_,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs)
    });
}

void VirtualController::PressAButton() {
    if (!IsActive()) {
        DriverLog("Cannot press A button: controller not active\n");
        return;
    }

    DriverLog("Pressing A button\n");
    aButtonPressed_ = true;
    UpdateButtonState(aButtonHandle_, true);
}

void VirtualController::ReleaseAButton() {
    if (!IsActive()) {
        DriverLog("Cannot release A button: controller not active\n");
        return;
    }

    DriverLog("Releasing A button\n");
    aButtonPressed_ = false;
    UpdateButtonState(aButtonHandle_, false);
}

void VirtualController::ClickAButton(int durationMs) {
    if (!IsActive()) {
        DriverLog("Cannot click A button: controller not active\n");
        return;
    }

    DriverLog("Clicking A button (duration: %d ms)\n", durationMs);
    
    // Press the button
    PressAButton();
    
    // Schedule release
    std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
    pendingReleases_.push_back({
        aButtonHandle_,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs)
    });
}

void VirtualController::RunFrame() {
    // Process pending button releases
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
    
    // Find and process expired releases
    auto it = pendingReleases_.begin();
    while (it != pendingReleases_.end()) {
        if (now >= it->releaseTime) {
            // Release the button
            if (it->button == systemButtonHandle_) {
                systemButtonPressed_ = false;
            } else if (it->button == aButtonHandle_) {
                aButtonPressed_ = false;
            }
            UpdateButtonState(it->button, false);
            it = pendingReleases_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace micmap::driver
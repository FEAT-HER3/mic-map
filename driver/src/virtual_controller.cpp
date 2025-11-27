/**
 * @file virtual_controller.cpp
 * @brief Implementation of the virtual controller device
 */

#include "virtual_controller.hpp"
#include "driver_log.hpp"
#include <cstring>
#include <cmath>
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
    // Use empty render model so the controller is invisible
    // The laser pointer will use the head pose from our bindings
    VRProperties()->SetStringProperty(propertyContainer_, Prop_RenderModelName_String, "");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_TrackingSystemName_String, "micmap");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_SerialNumber_String, serialNumber_.c_str());
    
    // Controller-specific properties
    // Use RightHand role so the controller is mapped to /user/hand/right
    // This allows the laser mouse bindings to work correctly
    // The controller will use head pose for pointing via the bindings
    VRProperties()->SetInt32Property(propertyContainer_, Prop_ControllerRoleHint_Int32, TrackedControllerRole_RightHand);
    VRProperties()->SetStringProperty(propertyContainer_, Prop_ControllerType_String, "micmap_controller");
    VRProperties()->SetStringProperty(propertyContainer_, Prop_InputProfilePath_String, "{micmap}/input/micmap_controller_profile.json");
    
    // Indicate this device has a controller component
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_HasControllerComponent_Bool, true);
    
    // Device properties - we provide a valid pose now
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_WillDriftInYaw_Bool, false);
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_DeviceIsWireless_Bool, false);
    VRProperties()->SetBoolProperty(propertyContainer_, Prop_DeviceIsCharging_Bool, false);
    VRProperties()->SetFloatProperty(propertyContainer_, Prop_DeviceBatteryPercentage_Float, 1.0f);
    
    // Create input components
    // System button - typically used for dashboard interaction
    VRDriverInput()->CreateBooleanComponent(propertyContainer_, "/input/system/click", &systemButtonHandle_);
    
    // A button - alternative select button
    VRDriverInput()->CreateBooleanComponent(propertyContainer_, "/input/a/click", &aButtonHandle_);
    
    // Trigger - primary selection button for laser mouse / head-locked pointer
    VRDriverInput()->CreateScalarComponent(propertyContainer_, "/input/trigger/value", &triggerValueHandle_,
                                           VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
    VRDriverInput()->CreateBooleanComponent(propertyContainer_, "/input/trigger/click", &triggerClickHandle_);

    DriverLog("VirtualController activated successfully\n");
    DriverLog("  System button handle: %llu\n", systemButtonHandle_);
    DriverLog("  A button handle: %llu\n", aButtonHandle_);
    DriverLog("  Trigger value handle: %llu\n", triggerValueHandle_);
    DriverLog("  Trigger click handle: %llu\n", triggerClickHandle_);

    return VRInitError_None;
}

void VirtualController::Deactivate() {
    DriverLog("VirtualController deactivating\n");
    deviceIndex_ = k_unTrackedDeviceIndexInvalid;
    propertyContainer_ = k_ulInvalidPropertyContainer;
    systemButtonHandle_ = k_ulInvalidInputComponentHandle;
    aButtonHandle_ = k_ulInvalidInputComponentHandle;
    triggerValueHandle_ = k_ulInvalidInputComponentHandle;
    triggerClickHandle_ = k_ulInvalidInputComponentHandle;
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
    
    // Get the HMD pose so we can track it
    TrackedDevicePose_t hmdPose;
    VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, &hmdPose, 1);
    
    pose.deviceIsConnected = true;
    
    if (hmdPose.bPoseIsValid) {
        // Extract position and rotation from HMD pose matrix
        // The matrix is a 3x4 row-major matrix where:
        // - First 3 columns are the rotation matrix
        // - Last column is the position
        const HmdMatrix34_t& m = hmdPose.mDeviceToAbsoluteTracking;
        
        // Position at the head
        pose.vecPosition[0] = m.m[0][3];
        pose.vecPosition[1] = m.m[1][3];
        pose.vecPosition[2] = m.m[2][3];
        
        // Convert rotation matrix to quaternion
        // Using the standard algorithm for 3x3 rotation matrix to quaternion
        float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
        
        if (trace > 0) {
            float s = 0.5f / sqrtf(trace + 1.0f);
            pose.qRotation.w = 0.25f / s;
            pose.qRotation.x = (m.m[2][1] - m.m[1][2]) * s;
            pose.qRotation.y = (m.m[0][2] - m.m[2][0]) * s;
            pose.qRotation.z = (m.m[1][0] - m.m[0][1]) * s;
        } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
            float s = 2.0f * sqrtf(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
            pose.qRotation.w = (m.m[2][1] - m.m[1][2]) / s;
            pose.qRotation.x = 0.25f * s;
            pose.qRotation.y = (m.m[0][1] + m.m[1][0]) / s;
            pose.qRotation.z = (m.m[0][2] + m.m[2][0]) / s;
        } else if (m.m[1][1] > m.m[2][2]) {
            float s = 2.0f * sqrtf(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
            pose.qRotation.w = (m.m[0][2] - m.m[2][0]) / s;
            pose.qRotation.x = (m.m[0][1] + m.m[1][0]) / s;
            pose.qRotation.y = 0.25f * s;
            pose.qRotation.z = (m.m[1][2] + m.m[2][1]) / s;
        } else {
            float s = 2.0f * sqrtf(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
            pose.qRotation.w = (m.m[1][0] - m.m[0][1]) / s;
            pose.qRotation.x = (m.m[0][2] + m.m[2][0]) / s;
            pose.qRotation.y = (m.m[1][2] + m.m[2][1]) / s;
            pose.qRotation.z = 0.25f * s;
        }
        
        // Copy velocity from HMD
        pose.vecVelocity[0] = hmdPose.vVelocity.v[0];
        pose.vecVelocity[1] = hmdPose.vVelocity.v[1];
        pose.vecVelocity[2] = hmdPose.vVelocity.v[2];
        
        pose.vecAngularVelocity[0] = hmdPose.vAngularVelocity.v[0];
        pose.vecAngularVelocity[1] = hmdPose.vAngularVelocity.v[1];
        pose.vecAngularVelocity[2] = hmdPose.vAngularVelocity.v[2];
        
        pose.poseIsValid = true;
        pose.result = TrackingResult_Running_OK;
    } else {
        // HMD pose not available, use identity pose
        pose.vecPosition[0] = 0.0;
        pose.vecPosition[1] = 0.0;
        pose.vecPosition[2] = 0.0;
        
        pose.vecVelocity[0] = 0.0;
        pose.vecVelocity[1] = 0.0;
        pose.vecVelocity[2] = 0.0;
        
        pose.vecAngularVelocity[0] = 0.0;
        pose.vecAngularVelocity[1] = 0.0;
        pose.vecAngularVelocity[2] = 0.0;
        
        pose.qRotation.w = 1;
        pose.qRotation.x = 0;
        pose.qRotation.y = 0;
        pose.qRotation.z = 0;
        
        pose.poseIsValid = true;
        pose.result = TrackingResult_Running_OK;
    }
    
    // Identity quaternion for world from driver rotation
    pose.qWorldFromDriverRotation.w = 1;
    pose.qWorldFromDriverRotation.x = 0;
    pose.qWorldFromDriverRotation.y = 0;
    pose.qWorldFromDriverRotation.z = 0;
    
    // Identity quaternion for driver from head rotation
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

void VirtualController::UpdateScalarState(VRInputComponentHandle_t scalar, float value) {
    if (scalar == k_ulInvalidInputComponentHandle) {
        DriverLog("Cannot update scalar state: invalid handle\n");
        return;
    }

    VRDriverInput()->UpdateScalarComponent(scalar, value, 0.0);
    DriverLog("Scalar %llu state updated to %f\n", scalar, value);
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

void VirtualController::PressTrigger() {
    if (!IsActive()) {
        DriverLog("Cannot press trigger: controller not active\n");
        return;
    }

    DriverLog("Pressing trigger\n");
    triggerPressed_ = true;
    UpdateScalarState(triggerValueHandle_, 1.0f);
    UpdateButtonState(triggerClickHandle_, true);
}

void VirtualController::ReleaseTrigger() {
    if (!IsActive()) {
        DriverLog("Cannot release trigger: controller not active\n");
        return;
    }

    DriverLog("Releasing trigger\n");
    triggerPressed_ = false;
    UpdateScalarState(triggerValueHandle_, 0.0f);
    UpdateButtonState(triggerClickHandle_, false);
}

void VirtualController::ClickTrigger(int durationMs) {
    if (!IsActive()) {
        DriverLog("Cannot click trigger: controller not active\n");
        return;
    }

    DriverLog("Clicking trigger (duration: %d ms)\n", durationMs);
    
    // Press the trigger
    PressTrigger();
    
    // Schedule release - we use the triggerClickHandle_ for the pending release
    // The scalar value will be released along with it in RunFrame
    std::lock_guard<std::mutex> lock(pendingReleasesMutex_);
    pendingReleases_.push_back({
        triggerClickHandle_,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs)
    });
}

void VirtualController::RunFrame() {
    // Update pose every frame to keep the controller "alive"
    if (IsActive()) {
        VRServerDriverHost()->TrackedDevicePoseUpdated(deviceIndex_, GetPose(), sizeof(DriverPose_t));
    }
    
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
            } else if (it->button == triggerClickHandle_) {
                triggerPressed_ = false;
                // Also release the scalar value
                UpdateScalarState(triggerValueHandle_, 0.0f);
            }
            UpdateButtonState(it->button, false);
            it = pendingReleases_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace micmap::driver
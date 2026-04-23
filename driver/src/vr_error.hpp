#pragma once
#include <openvr_driver.h>

namespace micmap::driver {
inline const char* VRInputErrorName(vr::EVRInputError e) {
    switch (e) {
    case vr::VRInputError_None:                  return "None";
    case vr::VRInputError_NameNotFound:          return "NameNotFound";
    case vr::VRInputError_WrongType:             return "WrongType";
    case vr::VRInputError_InvalidHandle:         return "InvalidHandle";
    case vr::VRInputError_InvalidParam:          return "InvalidParam";
    case vr::VRInputError_NoSteam:               return "NoSteam";
    case vr::VRInputError_MaxCapacityReached:    return "MaxCapacityReached";
    case vr::VRInputError_IPCError:              return "IPCError";
    case vr::VRInputError_NoActiveActionSet:     return "NoActiveActionSet";
    case vr::VRInputError_InvalidDevice:         return "InvalidDevice";
    case vr::VRInputError_InvalidSkeleton:       return "InvalidSkeleton";
    case vr::VRInputError_InvalidBoneCount:      return "InvalidBoneCount";
    case vr::VRInputError_InvalidCompressedData: return "InvalidCompressedData";
    case vr::VRInputError_NoData:                return "NoData";
    case vr::VRInputError_BufferTooSmall:        return "BufferTooSmall";
    case vr::VRInputError_MismatchedActionManifest: return "MismatchedActionManifest";
    case vr::VRInputError_MissingSkeletonData:   return "MissingSkeletonData";
    case vr::VRInputError_InvalidBoneIndex:      return "InvalidBoneIndex";
    case vr::VRInputError_InvalidPriority:       return "InvalidPriority";
    case vr::VRInputError_PermissionDenied:      return "PermissionDenied";
    case vr::VRInputError_InvalidRenderModel:    return "InvalidRenderModel";
    }
    return "Unknown";
}
} // namespace micmap::driver

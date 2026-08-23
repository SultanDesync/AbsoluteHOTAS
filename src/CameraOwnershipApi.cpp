#include "PCH.h"

#define ABSOLUTE_HOTAS_EXPORTS
#include "AbsoluteCameraOwnershipAPI.h"
#include "AbsoluteControlSubscriber.h"
#include "NativeShipControl.h"

namespace {

AbsoluteCameraOwnershipApi::Result __cdecl DeclareAbsoluteHeadTrackingOwner() noexcept
{
    NativeShipControl::SetExternalCameraOwner(true);
    AbsoluteControlSubscriber::SetExternalCameraOwner(true);
    return NativeShipControl::CameraHookInstalled()
        ? AbsoluteCameraOwnershipApi::Result::Rejected
        : AbsoluteCameraOwnershipApi::Result::Ok;
}

std::uint8_t __cdecl CameraHookReleased() noexcept
{
    return NativeShipControl::CameraHookInstalled() ? 0U : 1U;
}

std::uint8_t __cdecl FlightObserverInstalled() noexcept
{
    return NativeShipControl::FlightObserverInstalled() ? 1U : 0U;
}

std::int64_t __cdecl CockpitSignalAgeMilliseconds() noexcept
{
    return NativeShipControl::FlightObserverOutputAgeMilliseconds();
}

const AbsoluteCameraOwnershipApi::ApiV1 g_api{
    .providerId = "absolute.hotas",
    .declareAbsoluteHeadTrackingOwner = &DeclareAbsoluteHeadTrackingOwner,
    .cameraHookReleased = &CameraHookReleased,
    .flightObserverInstalled = &FlightObserverInstalled,
    .cockpitSignalAgeMilliseconds = &CockpitSignalAgeMilliseconds,
};

} // namespace

extern "C" ABSOLUTE_CAMERA_OWNERSHIP_API const AbsoluteCameraOwnershipApi::ApiV1*
AbsoluteHOTAS_QueryCameraOwnershipApi(std::uint32_t requestedAbiVersion) noexcept
{
    return requestedAbiVersion == AbsoluteCameraOwnershipApi::kAbiVersion ? &g_api : nullptr;
}

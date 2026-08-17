#pragma once

#include <cstdint>
#include <type_traits>

// Narrow in-process ABI used when Absolute Head Tracking and AbsoluteHOTAS
// share the native camera/flight seams. HOTAS retains its core selected-flight
// observer, while Absolute Head Tracking becomes the only camera-hook owner.
// Only bounded status and signal-age operations cross the DLL boundary.
namespace AbsoluteCameraOwnershipApi {

inline constexpr std::uint32_t kAbiVersion = 1;

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    Rejected,
};

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* providerId{};

    Result(__cdecl* declareAbsoluteHeadTrackingOwner)() noexcept{};
    std::uint8_t(__cdecl* cameraHookReleased)() noexcept{};
    std::uint8_t(__cdecl* flightObserverInstalled)() noexcept{};
    std::int64_t(__cdecl* cockpitSignalAgeMilliseconds)() noexcept{};
};

static_assert(std::is_standard_layout_v<ApiV1>);
static_assert(std::is_trivially_copyable_v<ApiV1>);

} // namespace AbsoluteCameraOwnershipApi

#if defined(ABSOLUTE_HOTAS_EXPORTS)
#define ABSOLUTE_CAMERA_OWNERSHIP_API __declspec(dllexport)
#else
#define ABSOLUTE_CAMERA_OWNERSHIP_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_CAMERA_OWNERSHIP_API const AbsoluteCameraOwnershipApi::ApiV1*
AbsoluteHOTAS_QueryCameraOwnershipApi(std::uint32_t requestedAbiVersion) noexcept;

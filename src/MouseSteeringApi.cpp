#include "PCH.h"

#define ABSOLUTE_HOTAS_EXPORTS
#include "AbsoluteMouseSteeringAPI.h"
#include "AbsoluteControlSubscriber.h"
#include "ThrottleHook.h"

namespace {

AbsoluteMouseSteeringApi::Result __cdecl ReadMouseAccumulator(
    float* yaw, float* pitch) noexcept {
    if (!yaw || !pitch) return AbsoluteMouseSteeringApi::Result::InvalidArgument;
    const std::uintptr_t source = ThrottleHook::GetSourceBasePtr();
    if (!source) return AbsoluteMouseSteeringApi::Result::NotReady;
    __try {
        *yaw = *reinterpret_cast<volatile float*>(source + 0x4C);
        *pitch = *reinterpret_cast<volatile float*>(source + 0x50);
        return AbsoluteMouseSteeringApi::Result::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return AbsoluteMouseSteeringApi::Result::MemoryFault;
    }
}

AbsoluteMouseSteeringApi::Result __cdecl WriteMouseAccumulator(
    float yaw, float pitch) noexcept {
    const std::uintptr_t source = ThrottleHook::GetSourceBasePtr();
    if (!source) return AbsoluteMouseSteeringApi::Result::NotReady;
    __try {
        *reinterpret_cast<volatile float*>(source + 0x4C) = yaw;
        *reinterpret_cast<volatile float*>(source + 0x50) = pitch;
        return AbsoluteMouseSteeringApi::Result::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return AbsoluteMouseSteeringApi::Result::MemoryFault;
    }
}

std::uint8_t __cdecl RotationalWriterHookInstalled() noexcept {
    return ThrottleHook::RotationalWriterHookInstalled() ? 1 : 0;
}

AbsoluteMouseSteeringApi::Result __cdecl DeclareAbsoluteZeroOwner() noexcept {
    ThrottleHook::SetExternalMouseSteeringOwner(true);
    AbsoluteControlSubscriber::SetExternalMouseSteeringOwner(true);
    return AbsoluteMouseSteeringApi::Result::Ok;
}

std::uint8_t __cdecl AbsoluteZeroOwnsPitchYaw() noexcept {
    return ThrottleHook::ExternalMouseSteeringOwnerActive() ? 1 : 0;
}

const AbsoluteMouseSteeringApi::ApiV1 g_api{
    .providerId = "absolute.hotas",
    .readMouseAccumulator = &ReadMouseAccumulator,
    .writeMouseAccumulator = &WriteMouseAccumulator,
    .rotationalWriterHookInstalled = &RotationalWriterHookInstalled,
    .declareAbsoluteZeroOwner = &DeclareAbsoluteZeroOwner,
    .absoluteZeroOwnsPitchYaw = &AbsoluteZeroOwnsPitchYaw,
};

} // namespace

extern "C" ABSOLUTE_MOUSE_STEERING_API const AbsoluteMouseSteeringApi::ApiV1*
AbsoluteHOTAS_QueryMouseSteeringApi(std::uint32_t requestedAbiVersion) noexcept {
    return requestedAbiVersion == AbsoluteMouseSteeringApi::kAbiVersion ? &g_api : nullptr;
}

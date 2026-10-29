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

AbsoluteMouseSteeringApi::Result __cdecl ExtractMouseAccumulatorDelta(
    float* deltaYaw, float* deltaPitch, std::uint8_t zeroAccumulator) noexcept {
    if (!deltaYaw || !deltaPitch) return AbsoluteMouseSteeringApi::Result::InvalidArgument;
    const std::uintptr_t source = ThrottleHook::GetSourceBasePtr();
    if (!source) {
        *deltaYaw = 0.0f;
        *deltaPitch = 0.0f;
        return AbsoluteMouseSteeringApi::Result::NotReady;
    }
    __try {
        *deltaYaw = *reinterpret_cast<volatile float*>(source + 0x4C);
        *deltaPitch = *reinterpret_cast<volatile float*>(source + 0x50);
        if (zeroAccumulator) {
            *reinterpret_cast<volatile float*>(source + 0x4C) = 0.0f;
            *reinterpret_cast<volatile float*>(source + 0x50) = 0.0f;
        }
        return AbsoluteMouseSteeringApi::Result::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *deltaYaw = 0.0f;
        *deltaPitch = 0.0f;
        return AbsoluteMouseSteeringApi::Result::MemoryFault;
    }
}

AbsoluteMouseSteeringApi::Result __cdecl SetVirtualJoySteering(
    std::uint8_t enabled, float yawNorm, float pitchNorm) noexcept {
    ThrottleHook::SetVirtualJoySteering(enabled != 0, yawNorm, pitchNorm);
    return AbsoluteMouseSteeringApi::Result::Ok;
}

AbsoluteMouseSteeringApi::Result __cdecl RegisterWriterSyncCallback(
    AbsoluteMouseSteeringApi::WriterSyncCallback callback) noexcept {
    ThrottleHook::RegisterWriterSyncCallback(callback);
    return AbsoluteMouseSteeringApi::Result::Ok;
}

const AbsoluteMouseSteeringApi::ApiV1 g_apiV1{
    .structSize = sizeof(AbsoluteMouseSteeringApi::ApiV1),
    .abiVersion = AbsoluteMouseSteeringApi::kAbiVersion1,
    .providerId = "absolute.hotas",
    .readMouseAccumulator = &ReadMouseAccumulator,
    .writeMouseAccumulator = &WriteMouseAccumulator,
    .rotationalWriterHookInstalled = &RotationalWriterHookInstalled,
    .declareAbsoluteZeroOwner = &DeclareAbsoluteZeroOwner,
    .absoluteZeroOwnsPitchYaw = &AbsoluteZeroOwnsPitchYaw,
};

const AbsoluteMouseSteeringApi::ApiV2 g_apiV2{
    .structSize = sizeof(AbsoluteMouseSteeringApi::ApiV2),
    .abiVersion = AbsoluteMouseSteeringApi::kAbiVersion,
    .providerId = "absolute.hotas",
    .readMouseAccumulator = &ReadMouseAccumulator,
    .writeMouseAccumulator = &WriteMouseAccumulator,
    .rotationalWriterHookInstalled = &RotationalWriterHookInstalled,
    .declareAbsoluteZeroOwner = &DeclareAbsoluteZeroOwner,
    .absoluteZeroOwnsPitchYaw = &AbsoluteZeroOwnsPitchYaw,
    .extractMouseAccumulatorDelta = &ExtractMouseAccumulatorDelta,
    .setVirtualJoySteering = &SetVirtualJoySteering,
    .registerWriterSyncCallback = &RegisterWriterSyncCallback,
};

} // namespace

extern "C" ABSOLUTE_MOUSE_STEERING_API const AbsoluteMouseSteeringApi::ApiV1*
AbsoluteHOTAS_QueryMouseSteeringApi(std::uint32_t requestedAbiVersion) noexcept {
    if (requestedAbiVersion == AbsoluteMouseSteeringApi::kAbiVersion) {
        return reinterpret_cast<const AbsoluteMouseSteeringApi::ApiV1*>(&g_apiV2);
    }
    if (requestedAbiVersion == AbsoluteMouseSteeringApi::kAbiVersion1) {
        return &g_apiV1;
    }
    return nullptr;
}

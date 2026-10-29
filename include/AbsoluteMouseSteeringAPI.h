#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Narrow in-process ABI used when AbsoluteZero and AbsoluteHOTAS share the
// native rotational writer. HOTAS owns the machine-code patch; Zero owns
// mouse pitch/yaw semantics through bounded accumulator operations and
// synchronized writer-gate VJoy overrides. No raw Starfield address crosses the DLL boundary.
namespace AbsoluteMouseSteeringApi {

inline constexpr std::uint32_t kAbiVersion = 2;
inline constexpr std::uint32_t kAbiVersion1 = 1;

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    MemoryFault,
};

using WriterSyncCallback = void(__cdecl*)() noexcept;

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion1};
    const char* providerId{};

    Result(__cdecl* readMouseAccumulator)(float*, float*) noexcept{};
    Result(__cdecl* writeMouseAccumulator)(float, float) noexcept{};
    std::uint8_t(__cdecl* rotationalWriterHookInstalled)() noexcept{};
    Result(__cdecl* declareAbsoluteZeroOwner)() noexcept{};
    std::uint8_t(__cdecl* absoluteZeroOwnsPitchYaw)() noexcept{};
};

struct ApiV2 {
    std::uint32_t structSize{sizeof(ApiV2)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* providerId{};

    Result(__cdecl* readMouseAccumulator)(float*, float*) noexcept{};
    Result(__cdecl* writeMouseAccumulator)(float, float) noexcept{};
    std::uint8_t(__cdecl* rotationalWriterHookInstalled)() noexcept{};
    Result(__cdecl* declareAbsoluteZeroOwner)() noexcept{};
    std::uint8_t(__cdecl* absoluteZeroOwnsPitchYaw)() noexcept{};

    Result(__cdecl* extractMouseAccumulatorDelta)(float* deltaYaw, float* deltaPitch, std::uint8_t zeroAccumulator) noexcept{};
    Result(__cdecl* setVirtualJoySteering)(std::uint8_t enabled, float yawNorm, float pitchNorm) noexcept{};
    Result(__cdecl* registerWriterSyncCallback)(WriterSyncCallback callback) noexcept{};
};

static_assert(std::is_standard_layout_v<ApiV1>);
static_assert(std::is_trivially_copyable_v<ApiV1>);
static_assert(std::is_standard_layout_v<ApiV2>);
static_assert(std::is_trivially_copyable_v<ApiV2>);
static_assert(offsetof(ApiV2, readMouseAccumulator) == offsetof(ApiV1, readMouseAccumulator));
static_assert(offsetof(ApiV2, absoluteZeroOwnsPitchYaw) == offsetof(ApiV1, absoluteZeroOwnsPitchYaw));

} // namespace AbsoluteMouseSteeringApi

#if defined(ABSOLUTE_HOTAS_EXPORTS)
#define ABSOLUTE_MOUSE_STEERING_API __declspec(dllexport)
#else
#define ABSOLUTE_MOUSE_STEERING_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_MOUSE_STEERING_API const AbsoluteMouseSteeringApi::ApiV1*
AbsoluteHOTAS_QueryMouseSteeringApi(std::uint32_t requestedAbiVersion) noexcept;

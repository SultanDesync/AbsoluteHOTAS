#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Narrow in-process ABI used when AbsoluteZero and AbsoluteHOTAS share the
// native rotational writer. HOTAS owns the one machine-code patch; Zero owns
// mouse pitch/yaw semantics through bounded accumulator operations. No raw
// Starfield address crosses the DLL boundary.
namespace AbsoluteMouseSteeringApi {

inline constexpr std::uint32_t kAbiVersion = 1;

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    MemoryFault,
};

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* providerId{};

    Result(__cdecl* readMouseAccumulator)(float*, float*) noexcept{};
    Result(__cdecl* writeMouseAccumulator)(float, float) noexcept{};
    std::uint8_t(__cdecl* rotationalWriterHookInstalled)() noexcept{};
    Result(__cdecl* declareAbsoluteZeroOwner)() noexcept{};
    std::uint8_t(__cdecl* absoluteZeroOwnsPitchYaw)() noexcept{};
};

static_assert(std::is_standard_layout_v<ApiV1>);
static_assert(std::is_trivially_copyable_v<ApiV1>);

} // namespace AbsoluteMouseSteeringApi

#if defined(ABSOLUTE_HOTAS_EXPORTS)
#define ABSOLUTE_MOUSE_STEERING_API __declspec(dllexport)
#else
#define ABSOLUTE_MOUSE_STEERING_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_MOUSE_STEERING_API const AbsoluteMouseSteeringApi::ApiV1*
AbsoluteHOTAS_QueryMouseSteeringApi(std::uint32_t requestedAbiVersion) noexcept;

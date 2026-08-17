#pragma once

// Stable suite-command binding ABI. Absolute Workbench uses this API to let a
// daughter module bind one of its commands to a DirectInput button owned and
// polled by AbsoluteHOTAS. No C++ object crosses the DLL boundary.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace AbsoluteHOTASApi {

inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr std::size_t kModuleCapacity = 64;
inline constexpr std::size_t kCommandCapacity = 96;
inline constexpr std::size_t kLabelCapacity = 96;
inline constexpr std::size_t kBindingCapacity = 256;
inline constexpr std::size_t kDetailCapacity = 192;

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    NotFound,
    Busy,
    WriteFailure,
};

enum class CaptureState : std::uint32_t {
    Idle,
    Capturing,
    Captured,
    Cancelled,
    TimedOut,
    Error,
};

struct CommandBindingV1 {
    std::uint32_t structSize{sizeof(CommandBindingV1)};
    char moduleId[kModuleCapacity]{};
    char commandId[kCommandCapacity]{};
    char label[kLabelCapacity]{};
    char binding[kBindingCapacity]{};
};

struct CaptureV1 {
    std::uint32_t structSize{sizeof(CaptureV1)};
    CaptureState state{CaptureState::Idle};
    char moduleId[kModuleCapacity]{};
    char commandId[kCommandCapacity]{};
    char binding[kBindingCapacity]{};
    char detail[kDetailCapacity]{};
};

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* moduleId{};
    const char* displayName{};
    const char* version{};

    Result(__cdecl* getCommandBinding)(const char*, const char*, CommandBindingV1*) noexcept{};
    Result(__cdecl* clearCommandBinding)(const char*, const char*) noexcept{};
    Result(__cdecl* beginButtonCapture)(const char*, const char*) noexcept{};
    Result(__cdecl* pollButtonCapture)(CaptureV1*) noexcept{};
    Result(__cdecl* cancelButtonCapture)() noexcept{};
};

static_assert(std::is_standard_layout_v<ApiV1>);
static_assert(std::is_trivially_copyable_v<CommandBindingV1>);

} // namespace AbsoluteHOTASApi

#if defined(ABSOLUTE_HOTAS_EXPORTS)
#define ABSOLUTE_HOTAS_API __declspec(dllexport)
#else
#define ABSOLUTE_HOTAS_API __declspec(dllimport)
#endif
extern "C" ABSOLUTE_HOTAS_API const AbsoluteHOTASApi::ApiV1*
AbsoluteHOTAS_QueryApi(std::uint32_t requestedAbiVersion) noexcept;

#pragma once

// Client-side copy of Absolute Power's stable command ABI. Keep layout-compatible
// with Absolute Power ABI v1; HOTAS only consumes command enumeration/invocation.

#include <cstddef>
#include <cstdint>

namespace AbsolutePowerApi {
inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr std::size_t kSystemCount = 6;
inline constexpr std::size_t kIdCapacity = 64;
inline constexpr std::size_t kLabelCapacity = 96;

enum class Result : std::uint32_t {
    Ok, NotReady, WorkbenchMissing, UnsupportedRuntime, NativeSeamUnavailable,
    PilotNotReady, InvalidArgument, NotFound, Rejected, Conflict, WriteFailure,
};
enum class RuntimeState : std::uint32_t { Uninitialized, WorkbenchMissing, AwaitingNativeSnapshotSeam, Ready };
struct StatusV1 { std::uint32_t structSize{sizeof(StatusV1)}; RuntimeState state{}; std::uint8_t automationEnabled{}; std::uint8_t reserved[3]{}; char activePreset[kIdCapacity]{}; };
struct SystemStateV1 { std::uint8_t present{}; std::uint8_t reserved{}; std::uint16_t current{}; std::uint16_t maximum{}; std::uint16_t reserved2{}; };
struct SnapshotV1 { std::uint32_t structSize{sizeof(SnapshotV1)}; std::uint8_t pilotReady{}; std::uint8_t reserved{}; std::uint16_t available{}; std::uint16_t totalPower{}; std::uint16_t reserved2{}; SystemStateV1 systems[kSystemCount]{}; };
struct TierPlanV1 { std::uint16_t green{}; std::uint16_t yellow{}; std::uint16_t red{}; };
struct PresetV1 { std::uint32_t structSize{sizeof(PresetV1)}; char id[kIdCapacity]{}; char label[kLabelCapacity]{}; TierPlanV1 systems[kSystemCount]{}; std::uint8_t tieBreakOrder[kSystemCount]{}; std::uint8_t reserved[2]{}; };
struct RuleV1 { std::uint32_t structSize{sizeof(RuleV1)}; char id[kIdCapacity]{}; char label[kLabelCapacity]{}; std::uint8_t enabled{}; std::uint8_t trigger{}; std::uint8_t sourceSystem{}; std::uint8_t targetSystem{}; std::uint16_t targetPips{}; std::uint8_t thresholdPercent{}; std::uint8_t hysteresisPercent{}; std::uint32_t holdMilliseconds{}; std::uint16_t priority{}; std::uint16_t reserved{}; };
struct CommandV1 { std::uint32_t structSize{sizeof(CommandV1)}; char id[kIdCapacity]{}; char label[kLabelCapacity]{}; char category[kIdCapacity]{}; };
inline constexpr std::uint8_t kKeyboardModifierControl = 1U << 0U;
inline constexpr std::uint8_t kKeyboardModifierAlt = 1U << 1U;
inline constexpr std::uint8_t kKeyboardModifierShift = 1U << 2U;
inline constexpr std::uint8_t kKeyboardModifierMask = kKeyboardModifierControl | kKeyboardModifierAlt | kKeyboardModifierShift;
struct KeyboardBindingV1 { std::uint32_t structSize{sizeof(KeyboardBindingV1)}; char presetId[kIdCapacity]{}; std::uint8_t virtualKey{}; std::uint8_t modifiers{}; std::uint8_t reserved[2]{}; };
enum class PreviewTier : std::uint8_t { Green, Yellow, Red, Complete = 0xFF };
struct PreviewV1 { std::uint32_t structSize{sizeof(PreviewV1)}; std::uint16_t target[kSystemCount]{}; std::uint16_t clipped[kSystemCount]{}; std::uint16_t unassigned{}; std::uint16_t totalClipped{}; PreviewTier firstIncompleteTier{PreviewTier::Complete}; std::uint8_t reserved[3]{}; };
struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)}; std::uint32_t abiVersion{kAbiVersion};
    const char* moduleId{}; const char* displayName{}; const char* version{};
    Result(__cdecl* getStatus)(StatusV1*) noexcept{}; Result(__cdecl* getSnapshot)(SnapshotV1*) noexcept{};
    std::uint32_t(__cdecl* getPresetCount)() noexcept{}; Result(__cdecl* getPreset)(std::uint32_t, PresetV1*) noexcept{};
    std::uint32_t(__cdecl* getRuleCount)() noexcept{}; Result(__cdecl* getRule)(std::uint32_t, RuleV1*) noexcept{};
    std::uint32_t(__cdecl* getCommandCount)() noexcept{}; Result(__cdecl* getCommand)(std::uint32_t, CommandV1*) noexcept{};
    Result(__cdecl* invokeCommand)(const char*) noexcept{}; Result(__cdecl* setAutomationEnabled)(std::uint8_t) noexcept{};
    Result(__cdecl* reloadConfiguration)() noexcept{}; Result(__cdecl* previewPreset)(const PresetV1*, PreviewV1*) noexcept{};
    Result(__cdecl* processGameThread)() noexcept{};
    Result(__cdecl* getKeyboardBinding)(const char*, KeyboardBindingV1*) noexcept{};
    Result(__cdecl* setKeyboardBinding)(const KeyboardBindingV1*) noexcept{};
    Result(__cdecl* clearKeyboardBinding)(const char*) noexcept{};
    Result(__cdecl* recordWeaponFire)(std::uint32_t) noexcept{};
};
} // namespace AbsolutePowerApi

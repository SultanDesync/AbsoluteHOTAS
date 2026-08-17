#pragma once

// Stable C ABI for Absolute Control Panel. Providers keep ownership of their
// settings and callbacks; the host copies descriptors during registration and
// never passes a C++ object across a DLL boundary.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace AbsoluteControlPanelApi
{
    inline constexpr std::uint32_t kAbiVersion = 1;
    inline constexpr std::string_view kModuleId = "absolute.control_panel";
    inline constexpr std::size_t kIdentifierCapacity = 64;
    inline constexpr std::size_t kLabelCapacity = 96;
    inline constexpr std::size_t kDescriptionCapacity = 192;
    inline constexpr std::size_t kStringValueCapacity = 256;
    inline constexpr std::size_t kMaximumChoiceOptions = 256;

    enum class Result : std::uint32_t
    {
        Ok, NotReady, InvalidArgument, Duplicate, NotFound,
        CapacityExceeded, Rejected, WriteFailure
    };

    enum class ControlKind : std::uint32_t
    {
        Toggle, IntegerSlider, FloatSlider, Choice, Action, InputBinding, TextInput,
        ButtonBinding = InputBinding
    };

    enum class ValueKind : std::uint32_t { Boolean, Integer, Float, String };

    enum ControlFlags : std::uint32_t
    {
        kControlNone = 0,
        kControlReadOnly = 1U << 0,
        kControlRequiresRestart = 1U << 1,
        kControlAdvanced = 1U << 2,
        // A successful action mutates the provider's page draft. The host
        // pins the ordinary page transaction before invoking it and routes
        // the resulting state through the same Apply/Cancel lifecycle used
        // by scalar and compound writes.
        kControlMutatesDraft = 1U << 3,
        // When invoked with a dirty page, the host applies that page's pinned
        // transaction before calling the action. Apply failure suppresses the
        // action and leaves the draft available for correction or rollback.
        kControlAppliesDraftBeforeInvoke = 1U << 4,
        // A Choice used to select provider-owned view state rather than edit
        // configuration. Successful writes refresh the page without pinning a
        // transaction or marking the page dirty.
        kControlTransientChoice = 1U << 5,
        kBindingKeyboard = 1U << 8,
        kBindingMouse = 1U << 9,
        kBindingController = 1U << 10,
        kBindingModifiers = 1U << 11,
        kBindingClearable = 1U << 12
    };

    struct ValueV1
    {
        std::uint32_t structSize{ sizeof(ValueV1) };
        ValueKind kind{ ValueKind::String };
        std::uint32_t booleanValue{};
        std::int64_t integerValue{};
        double floatValue{};
        char stringValue[kStringValueCapacity]{};
    };

    struct ControlDescriptorV1
    {
        std::uint32_t structSize{ sizeof(ControlDescriptorV1) };
        ControlKind kind{ ControlKind::Toggle };
        std::uint32_t flags{ kControlNone };
        char controlId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        char description[kDescriptionCapacity]{};
        double minimumValue{};
        double maximumValue{};
        double stepValue{};
    };

    using ReadValueCallback = Result(__cdecl*)(void*, const char*, ValueV1*) noexcept;
    using WriteDraftCallback = Result(__cdecl*)(void*, const char*, const ValueV1*) noexcept;
    using InvokeActionCallback = Result(__cdecl*)(void*, const char*) noexcept;
    using ApplyCallback = Result(__cdecl*)(void*) noexcept;
    using CancelCallback = void(__cdecl*)(void*) noexcept;

    struct ChoiceOptionV1
    {
        std::uint32_t structSize{ sizeof(ChoiceOptionV1) };
        std::int64_t value{};
        char label[kLabelCapacity]{};
    };

    // The host supplies room for kMaximumChoiceOptions records. The provider
    // writes the populated count and returns CapacityExceeded if its complete
    // list cannot fit. Choice labels may change after requestRefresh.
    using ReadChoiceOptionsCallback = Result(__cdecl*)(void*, const char*,
        ChoiceOptionV1*, std::uint32_t, std::uint32_t*) noexcept;

    struct ModuleDescriptorV1
    {
        std::uint32_t structSize{ sizeof(ModuleDescriptorV1) };
        char moduleId[kIdentifierCapacity]{};
        char displayName[kLabelCapacity]{};
        char description[kDescriptionCapacity]{};
    };

    struct PageDescriptorV1
    {
        std::uint32_t structSize{ sizeof(PageDescriptorV1) };
        char moduleId[kIdentifierCapacity]{};
        char pageId[kIdentifierCapacity]{};
        char displayName[kLabelCapacity]{};
        char description[kDescriptionCapacity]{};
        std::uint32_t controlCount{};
        const ControlDescriptorV1* controls{};
        void* context{};
        ReadValueCallback readValue{};
        WriteDraftCallback writeDraft{};
        InvokeActionCallback invokeAction{};
        ApplyCallback apply{};
        CancelCallback cancel{};
        // Optional appended v1 capability. Older descriptors end immediately
        // before this field and remain valid.
        ReadChoiceOptionsCallback readChoiceOptions{};
    };

    inline constexpr std::uint32_t kPageDescriptorV1BaseSize =
        static_cast<std::uint32_t>(offsetof(PageDescriptorV1, readChoiceOptions));

    enum ApiCapabilities : std::uint64_t
    {
        kCapabilityNone = 0,
        kCapabilityLabeledChoices = 1ULL << 0
    };

    struct ApiV1
    {
        std::uint32_t structSize{ sizeof(ApiV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        const char* moduleId{};
        const char* displayName{};
        const char* version{};
        Result(__cdecl* registerPage)(const PageDescriptorV1*) noexcept{};
        Result(__cdecl* unregisterModule)(const char*) noexcept{};
        Result(__cdecl* requestRefresh)(const char*, const char*) noexcept{};
        Result(__cdecl* registerModule)(const ModuleDescriptorV1*) noexcept{};
        std::uint8_t(__cdecl* isOpen)() noexcept{};
        std::uint8_t(__cdecl* isInputCaptureActive)() noexcept{};
        std::uint64_t capabilities{ kCapabilityNone };
    };

    inline constexpr std::uint32_t kApiV1BaseSize =
        static_cast<std::uint32_t>(offsetof(ApiV1, capabilities));

    static_assert(std::is_standard_layout_v<ValueV1>);
    static_assert(std::is_trivially_copyable_v<ValueV1>);
    static_assert(std::is_standard_layout_v<ControlDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<ControlDescriptorV1>);
    static_assert(std::is_standard_layout_v<ModuleDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<ModuleDescriptorV1>);
    static_assert(std::is_standard_layout_v<ChoiceOptionV1>);
    static_assert(std::is_trivially_copyable_v<ChoiceOptionV1>);
    static_assert(std::is_standard_layout_v<PageDescriptorV1>);
    static_assert(std::is_standard_layout_v<ApiV1>);
    static_assert(sizeof(Result) == sizeof(std::uint32_t));
    static_assert(sizeof(ControlKind) == sizeof(std::uint32_t));
    static_assert(sizeof(ValueKind) == sizeof(std::uint32_t));
    static_assert(static_cast<std::uint32_t>(Result::Ok) == 0);
    static_assert(static_cast<std::uint32_t>(Result::Rejected) == 6);
    static_assert(static_cast<std::uint32_t>(ControlKind::Toggle) == 0);
    static_assert(static_cast<std::uint32_t>(ControlKind::InputBinding) == 5);
    static_assert(static_cast<std::uint32_t>(ControlKind::TextInput) == 6);
    static_assert(static_cast<std::uint32_t>(ValueKind::Boolean) == 0);
    static_assert(static_cast<std::uint32_t>(ValueKind::String) == 3);
    static_assert(offsetof(ValueV1, kind) == sizeof(std::uint32_t));
    static_assert(offsetof(ControlDescriptorV1, kind) == sizeof(std::uint32_t));
    static_assert(std::is_same_v<ReadValueCallback,
        Result(__cdecl*)(void*, const char*, ValueV1*) noexcept>);
    static_assert(std::is_same_v<WriteDraftCallback,
        Result(__cdecl*)(void*, const char*, const ValueV1*) noexcept>);
    static_assert(std::is_same_v<InvokeActionCallback,
        Result(__cdecl*)(void*, const char*) noexcept>);
    static_assert(std::is_same_v<ApplyCallback, Result(__cdecl*)(void*) noexcept>);
    static_assert(std::is_same_v<CancelCallback, void(__cdecl*)(void*) noexcept>);
    static_assert(std::is_same_v<ReadChoiceOptionsCallback,
        Result(__cdecl*)(void*, const char*, ChoiceOptionV1*, std::uint32_t,
            std::uint32_t*) noexcept>);
}

#if defined(ABSOLUTE_CONTROL_PANEL_EXPORTS)
#define ABSOLUTE_CONTROL_PANEL_API __declspec(dllexport)
#else
#define ABSOLUTE_CONTROL_PANEL_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_CONTROL_PANEL_API const AbsoluteControlPanelApi::ApiV1*
AbsoluteControlPanel_QueryApi(std::uint32_t requestedAbiVersion) noexcept;

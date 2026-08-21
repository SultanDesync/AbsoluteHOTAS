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
    inline constexpr std::size_t kMaximumRecordItems = 64;

    enum class Result : std::uint32_t
    {
        Ok, NotReady, InvalidArgument, Duplicate, NotFound,
        CapacityExceeded, Rejected, WriteFailure
    };

    enum class ControlKind : std::uint32_t
    {
        Toggle, IntegerSlider, FloatSlider, Choice, Action, InputBinding, TextInput,
        // Presentation-only control. The host never calls read/write callbacks
        // for it; label and description define a lightweight section divider.
        GroupHeader,
        // Provider-owned bounded list/detail selector. ReadValue returns the
        // selected stable record ID as ValueKind::String; selection is written
        // through the ordinary writeDraft callback without pinning a page
        // transaction when kControlTransientSelection is present.
        RecordCollection,
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
        // A Choice or RecordCollection used to select provider-owned view state
        // rather than edit configuration. Successful writes refresh the page
        // without pinning a transaction or marking the page dirty.
        kControlTransientChoice = 1U << 5,
        // Consecutive Action controls carrying this flag may share one visual
        // row. Providers should flag groups of two or three controls.
        kControlLayoutInline = 1U << 6,
        // The host presents label/description in a confirmation modal before
        // invoking an Action. Cancel never enters provider code.
        kControlRequiresConfirmation = 1U << 7,
        // Clearer spelling for provider-owned RecordCollection selection. This
        // intentionally aliases the established transient Choice behavior.
        kControlTransientSelection = kControlTransientChoice,
        kBindingKeyboard = 1U << 8,
        kBindingMouse = 1U << 9,
        kBindingController = 1U << 10,
        kBindingModifiers = 1U << 11,
        kBindingClearable = 1U << 12,
        // A Choice, RecordCollection, or InputBinding that identifies the
        // editing context shared by every page in a module. Supporting hosts keep a
        // compact group of up to three such controls above the scrolling body.
        kControlPinnedContext = 1U << 13
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

    enum RecordItemFlags : std::uint32_t
    {
        kRecordItemNone = 0,
        kRecordItemDisabled = 1U << 0,
        kRecordItemWarning = 1U << 1
    };

    struct RecordItemV1
    {
        std::uint32_t structSize{ sizeof(RecordItemV1) };
        std::uint32_t flags{ kRecordItemNone };
        char recordId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        char summary[kDescriptionCapacity]{};
        char detail[kDescriptionCapacity]{};
    };

    // The host requests only the active page and supplies exactly
    // kMaximumRecordItems slots. Empty collections are valid. Stable record IDs
    // are selected by writing a string ValueV1 to the owning control.
    using ReadRecordItemsCallback = Result(__cdecl*)(void*, const char*,
        RecordItemV1*, std::uint32_t, std::uint32_t*) noexcept;

    enum class BindingCaptureState : std::uint32_t
    {
        Idle, Capturing, Captured, Cancelled, TimedOut, Error
    };

    struct BindingCaptureV1
    {
        std::uint32_t structSize{ sizeof(BindingCaptureV1) };
        BindingCaptureState state{ BindingCaptureState::Idle };
        char binding[kStringValueCapacity]{};
        char detail[kDescriptionCapacity]{};
    };

    // Optional provider-owned capture tail. The host owns presentation and
    // navigation lock; the provider owns device polling, capture policy, and the
    // returned binding syntax. Callbacks are invoked only on the native UI thread.
    using BeginBindingCaptureCallback = Result(__cdecl*)(
        void*, const char*) noexcept;
    using PollBindingCaptureCallback = Result(__cdecl*)(
        void*, const char*, BindingCaptureV1*) noexcept;
    using CancelBindingCaptureCallback = Result(__cdecl*)(
        void*, const char*) noexcept;
    // Optional conflict-resolution tail. The provider atomically removes the
    // binding from its previous owner and assigns it to controlId in the active
    // page draft. The host pins the normal Apply/Cancel transaction on success.
    using ReassignBindingCallback = Result(__cdecl*)(
        void*, const char*, const char*) noexcept;

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
        BeginBindingCaptureCallback beginBindingCapture{};
        PollBindingCaptureCallback pollBindingCapture{};
        CancelBindingCaptureCallback cancelBindingCapture{};
        ReassignBindingCallback reassignBinding{};
        // Optional appended v1 selected-record/list-detail capability.
        ReadRecordItemsCallback readRecordItems{};
    };

    inline constexpr std::uint32_t kPageDescriptorV1BaseSize =
        static_cast<std::uint32_t>(offsetof(PageDescriptorV1, readChoiceOptions));
    inline constexpr std::uint32_t kPageDescriptorV1RecordItemsSize =
        static_cast<std::uint32_t>(offsetof(PageDescriptorV1, readRecordItems) +
            sizeof(ReadRecordItemsCallback));

    enum ApiCapabilities : std::uint64_t
    {
        kCapabilityNone = 0,
        kCapabilityLabeledChoices = 1ULL << 0,
        kCapabilityProviderBindingCapture = 1ULL << 1,
        kCapabilityBindingConflictResolution = 1ULL << 2,
        // The host accepts GroupHeader controls and kControlLayoutInline.
        kCapabilityStructuredLayout = 1ULL << 3,
        kCapabilityRecordCollections = 1ULL << 4,
        kCapabilityActionConfirmation = 1ULL << 5,
        // The size-gated ApiV1 tail accepts a provider request to show Absolute
        // Control at one of that provider's registered pages.
        kCapabilityPageOpenRequests = 1ULL << 6,
        // The host accepts a bounded kControlPinnedContext group per page and
        // renders it outside the scrolling page body.
        kCapabilityPinnedContextControls = 1ULL << 7
    };

    // Asynchronous host command. Ok means the validated route was queued for the
    // host UI thread; it does not mean a Scaleform movie was opened synchronously.
    using RequestOpenPageCallback = Result(__cdecl*)(
        const char*, const char*) noexcept;

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
        // Optional appended v1 command. Older hosts end before this field.
        RequestOpenPageCallback requestOpenPage{};
    };

    inline constexpr std::uint32_t kApiV1BaseSize =
        static_cast<std::uint32_t>(offsetof(ApiV1, capabilities));
    inline constexpr std::uint32_t kApiV1RequestOpenPageSize =
        static_cast<std::uint32_t>(offsetof(ApiV1, requestOpenPage) +
            sizeof(RequestOpenPageCallback));

    static_assert(std::is_standard_layout_v<ValueV1>);
    static_assert(std::is_trivially_copyable_v<ValueV1>);
    static_assert(std::is_standard_layout_v<ControlDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<ControlDescriptorV1>);
    static_assert(std::is_standard_layout_v<ModuleDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<ModuleDescriptorV1>);
    static_assert(std::is_standard_layout_v<ChoiceOptionV1>);
    static_assert(std::is_trivially_copyable_v<ChoiceOptionV1>);
    static_assert(std::is_standard_layout_v<BindingCaptureV1>);
    static_assert(std::is_trivially_copyable_v<BindingCaptureV1>);
    static_assert(std::is_standard_layout_v<RecordItemV1>);
    static_assert(std::is_trivially_copyable_v<RecordItemV1>);
    static_assert(std::is_standard_layout_v<PageDescriptorV1>);
    static_assert(std::is_standard_layout_v<ApiV1>);
    static_assert(sizeof(Result) == sizeof(std::uint32_t));
    static_assert(sizeof(ControlKind) == sizeof(std::uint32_t));
    static_assert(sizeof(ValueKind) == sizeof(std::uint32_t));
    static_assert(sizeof(BindingCaptureState) == sizeof(std::uint32_t));
    static_assert(static_cast<std::uint32_t>(Result::Ok) == 0);
    static_assert(static_cast<std::uint32_t>(Result::Rejected) == 6);
    static_assert(static_cast<std::uint32_t>(ControlKind::Toggle) == 0);
    static_assert(static_cast<std::uint32_t>(ControlKind::InputBinding) == 5);
    static_assert(static_cast<std::uint32_t>(ControlKind::TextInput) == 6);
    static_assert(static_cast<std::uint32_t>(ControlKind::GroupHeader) == 7);
    static_assert(static_cast<std::uint32_t>(ControlKind::RecordCollection) == 8);
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
    static_assert(std::is_same_v<BeginBindingCaptureCallback,
        Result(__cdecl*)(void*, const char*) noexcept>);
    static_assert(std::is_same_v<PollBindingCaptureCallback,
        Result(__cdecl*)(void*, const char*, BindingCaptureV1*) noexcept>);
    static_assert(std::is_same_v<CancelBindingCaptureCallback,
        Result(__cdecl*)(void*, const char*) noexcept>);
    static_assert(std::is_same_v<ReassignBindingCallback,
        Result(__cdecl*)(void*, const char*, const char*) noexcept>);
    static_assert(std::is_same_v<ReadRecordItemsCallback,
        Result(__cdecl*)(void*, const char*, RecordItemV1*, std::uint32_t,
            std::uint32_t*) noexcept>);
    static_assert(std::is_same_v<RequestOpenPageCallback,
        Result(__cdecl*)(const char*, const char*) noexcept>);
}

#if defined(ABSOLUTE_CONTROL_PANEL_EXPORTS)
#define ABSOLUTE_CONTROL_PANEL_API __declspec(dllexport)
#else
#define ABSOLUTE_CONTROL_PANEL_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_CONTROL_PANEL_API const AbsoluteControlPanelApi::ApiV1*
AbsoluteControlPanel_QueryApi(std::uint32_t requestedAbiVersion) noexcept;

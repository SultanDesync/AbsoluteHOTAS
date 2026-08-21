#pragma once

// Experimental semantic-composition ABI for Absolute Control. This contract
// references controls already registered through AbsoluteControlPanelAPI; it
// never owns configuration values or provider persistence.
//
// The C2 product query advertises semantic cards, status/conditions, anchors,
// and same-page live associations. Later vocabulary remains fail-closed.

#include "AbsoluteControlPanelAPI.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace AbsoluteControlCompositionExperimental
{
    inline constexpr std::uint32_t kAbiVersion = 1;
    inline constexpr std::size_t kIdentifierCapacity =
        AbsoluteControlPanelApi::kIdentifierCapacity;
    inline constexpr std::size_t kLabelCapacity =
        AbsoluteControlPanelApi::kLabelCapacity;
    inline constexpr std::size_t kDescriptionCapacity =
        AbsoluteControlPanelApi::kDescriptionCapacity;

    inline constexpr std::size_t kMaximumPages = 2048;
    inline constexpr std::size_t kMaximumPagesPerModule = 32;
    inline constexpr std::size_t kMaximumNodesPerPage = 128;
    inline constexpr std::size_t kMaximumAssociationsPerPage = 192;
    inline constexpr std::size_t kMaximumAnchorsPerPage = 16;
    inline constexpr std::size_t kMaximumColumns = 8;
    inline constexpr std::size_t kMaximumRecordWindow = 32;
    inline constexpr std::size_t kMaximumWorkflowSteps = 16;
    inline constexpr std::size_t kMaximumProgressRows = 16;
    inline constexpr std::size_t kMaximumPinnedContextControls = 12;

    using Result = AbsoluteControlPanelApi::Result;

    enum class NodeKind : std::uint32_t
    {
        Root,
        Section,
        Card,
        Row,
        Columns,
        AnchorSet,
        Anchor,
        PinnedContext,
        RecordView,
        WorkflowView,
        LiveSlot,
        ControlSlot
    };

    enum class SemanticRole : std::uint32_t
    {
        Default,
        Summary,
        Primary,
        Secondary,
        Tuning,
        Binding,
        Status,
        Danger
    };

    enum NodeFlags : std::uint32_t
    {
        kNodeNone = 0,
        kNodeCompact = 1U << 0,
        kNodeEmphasized = 1U << 1,
        kNodeAdvanced = 1U << 2,
        kNodeCollapsible = 1U << 3,
        kNodeCollapsedByDefault = 1U << 4
    };

    enum class RecordPresentation : std::uint32_t
    {
        Popup,
        MasterDetail,
        Table,
        Repeater,
        OrderedRepeater,
        DirectionPad
    };

    // Array order is semantic reading/focus order. parentNodeId must reference
    // an earlier node, producing a bounded acyclic tree without pointers.
    // referenceId meanings:
    // - ControlSlot: registered control ID
    // - RecordView: registered RecordCollection control ID
    // - Anchor: target composition node ID
    // - LiveSlot: registered live channel ID
    // - WorkflowView: provider workflow ID (reserved until workflow C4)
    // auxiliaryValue is the column count for Columns or RecordPresentation for
    // RecordView; it must be zero for every other kind.
    struct NodeDescriptorV1
    {
        std::uint32_t structSize{ sizeof(NodeDescriptorV1) };
        NodeKind kind{ NodeKind::Root };
        std::uint32_t flags{ kNodeNone };
        SemanticRole role{ SemanticRole::Default };
        char nodeId[kIdentifierCapacity]{};
        char parentNodeId[kIdentifierCapacity]{};
        char referenceId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        char description[kDescriptionCapacity]{};
        std::uint32_t auxiliaryValue{};
    };

    enum class AssociationKind : std::uint32_t
    {
        ControlEditsLiveMarker,
        ActionCapturesLiveMarker,
        StatusExplainsNode,
        ControlSummarizedByNode,
        RecordSelectionDrivesNode,
        TableColumnUsesControl,
        LiveSeriesExplainedByControl
    };

    enum AssociationFlags : std::uint32_t
    {
        kAssociationNone = 0,
        kAssociationOptional = 1U << 0,
        kAssociationDirectManipulation = 1U << 1
    };

    // sourceId is a registered control ID. targetNodeId is a composition node
    // ID. semanticId identifies the target marker, series, or table column for
    // association kinds that require one; it is empty for node-wide relations.
    struct AssociationDescriptorV1
    {
        std::uint32_t structSize{ sizeof(AssociationDescriptorV1) };
        AssociationKind kind{ AssociationKind::ControlSummarizedByNode };
        std::uint32_t flags{ kAssociationNone };
        char associationId[kIdentifierCapacity]{};
        char sourceId[kIdentifierCapacity]{};
        char targetNodeId[kIdentifierCapacity]{};
        char semanticId[kIdentifierCapacity]{};
    };

    enum class StatusSeverity : std::uint32_t
    {
        Normal,
        Information,
        Waiting,
        Warning,
        Error,
        Unavailable
    };

    enum NodeStateFlags : std::uint32_t
    {
        kNodeStateNone = 0,
        kNodeStateVisible = 1U << 0,
        kNodeStateEnabled = 1U << 1,
        kNodeStateRequired = 1U << 2,
        kNodeStateInherited = 1U << 3,
        kNodeStateOverridden = 1U << 4,
        kNodeStateStale = 1U << 5
    };

    // Providers publish only nodes whose state differs from the default
    // Visible|Enabled, Normal severity. The host copies and validates the full
    // bounded response before applying any state to an immutable snapshot.
    struct NodeStateV1
    {
        std::uint32_t structSize{ sizeof(NodeStateV1) };
        std::uint32_t flags{ kNodeStateVisible | kNodeStateEnabled };
        StatusSeverity severity{ StatusSeverity::Normal };
        std::uint32_t reserved{};
        char nodeId[kIdentifierCapacity]{};
        char value[kDescriptionCapacity]{};
        char detail[kDescriptionCapacity]{};
        char sourceLabel[kLabelCapacity]{};
        std::uint64_t sequence{};
    };

    using ReadNodeStatesCallback = Result(__cdecl*)(
        void*, const char*, const char*, NodeStateV1*, std::uint32_t,
        std::uint32_t*) noexcept;

    struct PageCompositionDescriptorV1
    {
        std::uint32_t structSize{ sizeof(PageCompositionDescriptorV1) };
        char moduleId[kIdentifierCapacity]{};
        char pageId[kIdentifierCapacity]{};
        std::uint32_t nodeCount{};
        const NodeDescriptorV1* nodes{};
        std::uint32_t associationCount{};
        const AssociationDescriptorV1* associations{};
        void* context{};
        ReadNodeStatesCallback readNodeStates{};
    };

    enum Capabilities : std::uint64_t
    {
        kCapabilityNone = 0,
        kCapabilitySemanticComposition = 1ULL << 0,
        kCapabilitySemanticStatus = 1ULL << 1,
        kCapabilityConditionalState = 1ULL << 2,
        kCapabilityAnchors = 1ULL << 3,
        kCapabilityRecordPresentations = 1ULL << 4,
        kCapabilityPinnedContext = 1ULL << 5,
        kCapabilityWorkflows = 1ULL << 6,
        kCapabilityProgressRows = 1ULL << 7,
        kCapabilityLiveAssociations = 1ULL << 8,
        kCapabilityDirectLiveManipulation = 1ULL << 9
    };

    inline constexpr std::uint64_t kC1Capabilities =
        kCapabilitySemanticComposition |
        kCapabilitySemanticStatus |
        kCapabilityConditionalState |
        kCapabilityAnchors;
    inline constexpr std::uint64_t kC2Capabilities =
        kC1Capabilities |
        kCapabilityLiveAssociations;
    inline constexpr std::uint64_t kAllCapabilities =
        kC2Capabilities |
        kCapabilityRecordPresentations |
        kCapabilityPinnedContext |
        kCapabilityWorkflows |
        kCapabilityProgressRows |
        kCapabilityDirectLiveManipulation;

    using RegisterPageCompositionCallback = Result(__cdecl*)(
        const PageCompositionDescriptorV1*) noexcept;
    using UnregisterModuleCompositionCallback = Result(__cdecl*)(
        const char*) noexcept;
    using RequestCompositionRefreshCallback = Result(__cdecl*)(
        const char*, const char*) noexcept;

    struct ApiV1
    {
        std::uint32_t structSize{ sizeof(ApiV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        const char* moduleId{};
        const char* version{};
        std::uint64_t capabilities{ kCapabilityNone };
        RegisterPageCompositionCallback registerPageComposition{};
        UnregisterModuleCompositionCallback unregisterModule{};
        RequestCompositionRefreshCallback requestRefresh{};
    };

    static_assert(std::is_standard_layout_v<NodeDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<NodeDescriptorV1>);
    static_assert(std::is_standard_layout_v<NodeStateV1>);
    static_assert(std::is_trivially_copyable_v<NodeStateV1>);
    static_assert(std::is_standard_layout_v<PageCompositionDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<PageCompositionDescriptorV1>);
    static_assert(std::is_standard_layout_v<AssociationDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<AssociationDescriptorV1>);
    static_assert(std::is_standard_layout_v<ApiV1>);
    static_assert(std::is_trivially_copyable_v<ApiV1>);
    static_assert(sizeof(NodeKind) == sizeof(std::uint32_t));
    static_assert(sizeof(SemanticRole) == sizeof(std::uint32_t));
    static_assert(sizeof(StatusSeverity) == sizeof(std::uint32_t));
    static_assert(sizeof(AssociationKind) == sizeof(std::uint32_t));
    static_assert(static_cast<std::uint32_t>(NodeKind::Root) == 0);
    static_assert(static_cast<std::uint32_t>(NodeKind::ControlSlot) == 11);
    static_assert(static_cast<std::uint32_t>(StatusSeverity::Unavailable) == 5);
    static_assert(std::is_same_v<ReadNodeStatesCallback,
        Result(__cdecl*)(void*, const char*, const char*, NodeStateV1*,
            std::uint32_t, std::uint32_t*) noexcept>);
}

// Experimental and independently negotiated from the stable data API. C2 only
// advertises the subset in kC2Capabilities; later vocabulary remains visible so
// descriptors can be source-compatible while unsupported registrations reject.
extern "C" ABSOLUTE_CONTROL_PANEL_API
const AbsoluteControlCompositionExperimental::ApiV1*
AbsoluteControlPanel_QueryCompositionApi(
    std::uint32_t requestedAbiVersion) noexcept;

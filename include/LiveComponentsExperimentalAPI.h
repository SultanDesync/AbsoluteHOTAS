#pragma once

// Experimental, additive live/compound component ABI. This is deliberately
// separate from AbsoluteControlPanelApi::ApiV1: changing this contract cannot
// change the layout or version of the stable configuration API.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace AbsoluteControlPanelExperimental
{
    inline constexpr std::uint32_t kAbiVersion = 1;
    inline constexpr std::size_t kIdentifierCapacity = 64;
    inline constexpr std::size_t kLabelCapacity = 96;
    inline constexpr std::size_t kValueFormatCapacity = 32;
    inline constexpr std::size_t kMaximumChannels = 16;
    inline constexpr std::size_t kMaximumRangeBands = 8;
    inline constexpr std::size_t kMaximumRangeMarkers = 8;
    inline constexpr std::size_t kMaximumPlotSeries = 8;
    inline constexpr std::size_t kMaximumPlotSamples = 120;
    inline constexpr std::size_t kMaximumGridColumns = 8;
    inline constexpr std::size_t kMaximumGridSegments = 32;
    inline constexpr std::size_t kMaximumGridTiers = 4;
    inline constexpr std::size_t kMaximumGridControlAssociations =
        kMaximumGridColumns;

    enum class Result : std::uint32_t
    {
        Ok,
        NotReady,
        InvalidArgument,
        Duplicate,
        NotFound,
        CapacityExceeded,
        Rejected,
        WriteFailure,
        Suspended,
        Stale
    };

    enum class ComponentKind : std::uint32_t
    {
        RangeMeter,
        TelemetryPlot,
        SegmentedAllocationGrid
    };

    enum class RangeBandSemantic : std::uint32_t
    {
        Custom,
        Dead,
        Active,
        Cruise,
        Reverse,
        Boost
    };

    enum class RangeMarkerSemantic : std::uint32_t
    {
        Custom,
        Center,
        Saturation,
        Detent
    };

    enum FrameFlags : std::uint32_t
    {
        kFrameNone = 0,
        kFrameStale = 1U << 0,
        kFrameUnavailable = 1U << 1,
        kFrameSuspended = 1U << 2
    };

    // Theme roles are resolved by the host. Providers describe meaning and
    // never inject raw colors into the shared Control Panel skin.
    enum class VisualRole : std::uint32_t
    {
        Neutral,
        Accent,
        Positive,
        Warning,
        Critical,
        Live,
        Preview,
        Tier1,
        Tier2,
        Tier3
    };

    struct RangeBandV1
    {
        std::uint32_t structSize{ sizeof(RangeBandV1) };
        RangeBandSemantic semantic{ RangeBandSemantic::Custom };
        double minimumValue{};
        double maximumValue{};
        VisualRole visualRole{ VisualRole::Neutral };
        char label[kLabelCapacity]{};
    };

    struct RangeMarkerV1
    {
        std::uint32_t structSize{ sizeof(RangeMarkerV1) };
        RangeMarkerSemantic semantic{ RangeMarkerSemantic::Custom };
        double value{};
        VisualRole visualRole{ VisualRole::Accent };
        char markerId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        // Empty for a fixed marker. A non-empty ID routes direct manipulation
        // through the ordinary typed draft-write lane.
        char controlId[kIdentifierCapacity]{};
    };

    struct RangeMeterDescriptorV1
    {
        std::uint32_t structSize{ sizeof(RangeMeterDescriptorV1) };
        double minimumValue{};
        double maximumValue{};
        std::uint32_t bandCount{};
        std::uint32_t markerCount{};
        char valueFormat[kValueFormatCapacity]{};
        RangeBandV1 bands[kMaximumRangeBands]{};
        RangeMarkerV1 markers[kMaximumRangeMarkers]{};
    };

    struct PlotSeriesDescriptorV1
    {
        std::uint32_t structSize{ sizeof(PlotSeriesDescriptorV1) };
        char seriesId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        VisualRole visualRole{ VisualRole::Accent };
    };

    struct TelemetryPlotDescriptorV1
    {
        std::uint32_t structSize{ sizeof(TelemetryPlotDescriptorV1) };
        std::uint32_t seriesCount{};
        std::uint32_t historyCapacity{};
        std::uint32_t autoRange{};
        double minimumValue{};
        double maximumValue{};
        std::uint32_t bandCount{};
        std::uint32_t markerCount{};
        PlotSeriesDescriptorV1 series[kMaximumPlotSeries]{};
        RangeBandV1 bands[kMaximumRangeBands]{};
        RangeMarkerV1 markers[kMaximumRangeMarkers]{};
    };

    struct GridTierDescriptorV1
    {
        std::uint32_t structSize{ sizeof(GridTierDescriptorV1) };
        char tierId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        VisualRole visualRole{ VisualRole::Neutral };
    };

    struct GridColumnDescriptorV1
    {
        std::uint32_t structSize{ sizeof(GridColumnDescriptorV1) };
        char columnId[kIdentifierCapacity]{};
        char label[kLabelCapacity]{};
        std::uint32_t maximumSegments{};
    };

    struct SegmentedGridDescriptorV1
    {
        std::uint32_t structSize{ sizeof(SegmentedGridDescriptorV1) };
        char controlId[kIdentifierCapacity]{};
        std::uint32_t columnCount{};
        std::uint32_t tierCount{};
        GridColumnDescriptorV1 columns[kMaximumGridColumns]{};
        GridTierDescriptorV1 tiers[kMaximumGridTiers]{};
    };

    enum SegmentedGridFlags : std::uint32_t
    {
        kSegmentedGridNone = 0,
        // Each interactive pip advances directly through the provider's tier
        // order and then back to the hollow tier.
        kSegmentedGridCycleOnClick = 1U << 0
    };

    // Host-owned presentation hints. They are deliberately carried on the
    // channel descriptor rather than provider configuration: a renderer may
    // keep a tuning surface fixed while controls scroll, or disclose an
    // expensive diagnostic only when the user asks for it. These bits are
    // valid for every component kind and compose with the low grid-only bits.
    enum LivePresentationFlags : std::uint32_t
    {
        kLivePresentationNone = 0,
        kLivePresentationPinned = 1U << 8,
        kLivePresentationSecondary = 1U << 9,
        kLivePresentationCollapsedByDefault = 1U << 10
    };

    // Explicitly associates one segmented-grid row with one Choice control on
    // the same page. This record lives in the additive live-channel tail rather
    // than GridColumnDescriptorV1 so the v1 column-array stride never changes.
    struct GridControlAssociationV1
    {
        std::uint32_t structSize{ sizeof(GridControlAssociationV1) };
        char columnId[kIdentifierCapacity]{};
        char controlId[kIdentifierCapacity]{};
    };

    enum LiveApiCapabilities : std::uint64_t
    {
        kLiveCapabilityNone = 0,
        kLiveCapabilityGridControlAssociations = 1ULL << 0,
        kLiveCapabilityPresentationFlags = 1ULL << 1,
        kLiveCapabilityDynamicRangeFrames = 1ULL << 2
    };
    inline constexpr std::uint64_t kLiveCapabilities =
        kLiveCapabilityGridControlAssociations |
        kLiveCapabilityPresentationFlags |
        kLiveCapabilityDynamicRangeFrames;

    struct RangeMeterFrameV1
    {
        std::uint32_t structSize{ sizeof(RangeMeterFrameV1) };
        std::uint32_t available{};
        double liveValue{};
    };

    struct TelemetrySampleV1
    {
        std::uint32_t structSize{ sizeof(TelemetrySampleV1) };
        std::uint32_t seriesCount{};
        std::uint32_t availableMask{};
        double values[kMaximumPlotSeries]{};
    };

    struct GridSegmentStateV1
    {
        std::uint8_t tierIndex{};
        std::uint8_t live{};
        std::uint8_t preview{};
        std::uint8_t interactive{};
    };

    struct GridColumnFrameV1
    {
        std::uint32_t structSize{ sizeof(GridColumnFrameV1) };
        std::uint32_t segmentCount{};
        std::uint32_t currentCount{};
        std::uint32_t maximumCount{};
        std::uint32_t targetCount{};
        GridSegmentStateV1 segments[kMaximumGridSegments]{};
    };

    struct SegmentedGridFrameV1
    {
        std::uint32_t structSize{ sizeof(SegmentedGridFrameV1) };
        std::uint32_t columnCount{};
        GridColumnFrameV1 columns[kMaximumGridColumns]{};
    };

    struct LiveFrameV1
    {
        std::uint32_t structSize{ sizeof(LiveFrameV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        ComponentKind kind{ ComponentKind::RangeMeter };
        std::uint64_t sequence{};
        std::uint64_t monotonicTimestampUs{};
        std::uint32_t flags{ kFrameNone };
        RangeMeterFrameV1 rangeMeter{};
        TelemetrySampleV1 telemetryPlot{};
        SegmentedGridFrameV1 segmentedGrid{};
        // Additive frame tail. A range provider can publish bands and markers
        // from its current draft without unregistering the channel. Older
        // providers stop before this offset; hosts must then use descriptor
        // bands/markers. The tail never shifts any v1 payload above it.
        struct DynamicRangeV1
        {
            std::uint32_t structSize{ sizeof(DynamicRangeV1) };
            std::uint32_t present{};
            std::uint32_t bandCount{};
            std::uint32_t markerCount{};
            RangeBandV1 bands[kMaximumRangeBands]{};
            RangeMarkerV1 markers[kMaximumRangeMarkers]{};
        } dynamicRange{};
    };

    inline constexpr std::uint32_t kLiveFrameV1BaseSize =
        static_cast<std::uint32_t>(offsetof(LiveFrameV1, dynamicRange));

    enum class CompoundOperationKind : std::uint32_t
    {
        SetSegmentCount,
        TrimColumn,
        SetTier,
        // count is a zero-based segment index; tierId is the desired tier.
        SetSegmentTier
    };

    struct CompoundOperationV1
    {
        std::uint32_t structSize{ sizeof(CompoundOperationV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        CompoundOperationKind kind{ CompoundOperationKind::SetSegmentCount };
        char moduleId[kIdentifierCapacity]{};
        char pageId[kIdentifierCapacity]{};
        char channelId[kIdentifierCapacity]{};
        char controlId[kIdentifierCapacity]{};
        char columnId[kIdentifierCapacity]{};
        char tierId[kIdentifierCapacity]{};
        std::uint32_t count{};
    };

    struct CompoundSnapshotV1
    {
        std::uint32_t structSize{ sizeof(CompoundSnapshotV1) };
        std::uint64_t revision{};
        SegmentedGridFrameV1 segmentedGrid{};
    };

    // The provider implementation of this callback must be wait-free from the
    // UI thread's perspective: no locks, allocation, I/O, or gameplay traversal.
    // It copies a previously prepared fixed-capacity frame into `frame`.
    using ReadLiveFrameCallback = Result(__cdecl*)(void* context, LiveFrameV1* frame) noexcept;
    using ApplyCompoundOperationCallback = Result(__cdecl*)(
        void* context, const CompoundOperationV1* operation,
        CompoundSnapshotV1* replacement) noexcept;

    struct LiveChannelDescriptorV1
    {
        std::uint32_t structSize{ sizeof(LiveChannelDescriptorV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        char moduleId[kIdentifierCapacity]{};
        char pageId[kIdentifierCapacity]{};
        char channelId[kIdentifierCapacity]{};
        char title[kLabelCapacity]{};
        ComponentKind kind{ ComponentKind::RangeMeter };
        RangeMeterDescriptorV1 rangeMeter{};
        TelemetryPlotDescriptorV1 telemetryPlot{};
        SegmentedGridDescriptorV1 segmentedGrid{};
        void* context{};
        ReadLiveFrameCallback readLiveFrame{};
        ApplyCompoundOperationCallback applyCompoundOperation{};
        // Additive descriptor tail. Low bits accept SegmentedGridFlags only on
        // segmented grids; LivePresentationFlags are valid for every kind.
        std::uint32_t flags{kSegmentedGridNone};
        // Optional full-size tail. Associations are one-to-one, reference a
        // column in segmentedGrid and a Choice control on the same page, and
        // are ignored unless the queried live API advertises support.
        std::uint32_t associationCount{};
        GridControlAssociationV1
            associations[kMaximumGridControlAssociations]{};
    };

    inline constexpr std::uint32_t kLiveChannelDescriptorV1BaseSize =
        static_cast<std::uint32_t>(offsetof(LiveChannelDescriptorV1, flags));
    inline constexpr std::uint32_t kLiveChannelDescriptorV1FlagsSize =
        static_cast<std::uint32_t>(offsetof(LiveChannelDescriptorV1, flags) +
            sizeof(std::uint32_t));

    // Pointer-free copy intended for renderer/model publication. Provider
    // context and callbacks never enter this type or the ActionScript graph.
    struct LiveChannelModelV1
    {
        std::uint32_t structSize{ sizeof(LiveChannelModelV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        char moduleId[kIdentifierCapacity]{};
        char pageId[kIdentifierCapacity]{};
        char channelId[kIdentifierCapacity]{};
        char title[kLabelCapacity]{};
        ComponentKind kind{ ComponentKind::RangeMeter };
        RangeMeterDescriptorV1 rangeMeter{};
        TelemetryPlotDescriptorV1 telemetryPlot{};
        SegmentedGridDescriptorV1 segmentedGrid{};
        std::uint32_t flags{kSegmentedGridNone};
        std::uint32_t associationCount{};
        GridControlAssociationV1
            associations[kMaximumGridControlAssociations]{};
    };

    struct ExperimentalApiV1
    {
        std::uint32_t structSize{ sizeof(ExperimentalApiV1) };
        std::uint32_t abiVersion{ kAbiVersion };
        Result(__cdecl* registerLiveChannel)(const LiveChannelDescriptorV1*) noexcept{};
        Result(__cdecl* unregisterModule)(const char* moduleId) noexcept{};
        Result(__cdecl* requestImmediateRefresh)(
            const char* moduleId, const char* pageId, const char* channelId) noexcept{};
        // Additive API-table tail. Consumers must size-check before reading.
        std::uint64_t capabilities{kLiveCapabilities};
    };

    inline constexpr std::uint32_t kExperimentalApiV1BaseSize =
        static_cast<std::uint32_t>(offsetof(ExperimentalApiV1, capabilities));

    static_assert(std::is_standard_layout_v<LiveFrameV1> && std::is_trivially_copyable_v<LiveFrameV1>);
    static_assert(std::is_standard_layout_v<GridControlAssociationV1> &&
        std::is_trivially_copyable_v<GridControlAssociationV1>);
    static_assert(std::is_standard_layout_v<LiveChannelDescriptorV1> && std::is_trivially_copyable_v<LiveChannelDescriptorV1>);
    static_assert(std::is_standard_layout_v<LiveChannelModelV1> && std::is_trivially_copyable_v<LiveChannelModelV1>);
    static_assert(std::is_standard_layout_v<CompoundOperationV1> && std::is_trivially_copyable_v<CompoundOperationV1>);
    static_assert(std::is_standard_layout_v<CompoundSnapshotV1> && std::is_trivially_copyable_v<CompoundSnapshotV1>);
}

#if defined(ABSOLUTE_CONTROL_PANEL_EXPORTS)
#define ABSOLUTE_CONTROL_PANEL_EXPERIMENTAL_API __declspec(dllexport)
#else
#define ABSOLUTE_CONTROL_PANEL_EXPERIMENTAL_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_CONTROL_PANEL_EXPERIMENTAL_API
const AbsoluteControlPanelExperimental::ExperimentalApiV1*
AbsoluteControlPanel_QueryLiveComponentsExperimental(std::uint32_t requestedAbiVersion) noexcept;

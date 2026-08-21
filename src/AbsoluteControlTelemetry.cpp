#include "AbsoluteControlTelemetry.h"
#include "AxisShapingPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string_view>

namespace {
namespace Live = AbsoluteControlPanelExperimental;

template <std::size_t N>
struct AtomicMailbox {
    std::atomic<std::uint64_t> guard{};
    std::atomic<std::uint64_t> timestampUs{};
    std::atomic<std::uint32_t> availableMask{};
    std::array<std::atomic<std::uint32_t>, N> values{};
};

AtomicMailbox<AbsoluteControlTelemetry::kFlightValueCount> g_flight;
AtomicMailbox<AbsoluteControlTelemetry::kThrottleValueCount> g_throttle;
std::atomic<std::int64_t> g_throttleRaw{};
std::atomic_bool g_throttleRawAvailable{};
AtomicMailbox<AbsoluteControlTelemetry::kAimValueCount> g_aim;
AtomicMailbox<AbsoluteControlTelemetry::kDeviceValueCount> g_device;
struct AtomicAxisPreviews {
    std::atomic<std::uint64_t> guard{};
    std::atomic<std::uint64_t> timestampUs{};
    std::array<std::atomic<std::uint32_t>,
        AbsoluteControlTelemetry::kAxisPreviewCount> inversions{};
    std::array<std::atomic<std::uint64_t>,
        AbsoluteControlTelemetry::kAxisPreviewCount * 3> reals{};
};
AtomicAxisPreviews g_axisPreviews;
AbsoluteControlTelemetry::ThrottleLayout g_throttleLayout;
struct AtomicThrottleLayout {
    std::atomic<std::uint64_t> guard{};
    std::atomic<std::uint64_t> timestampUs{};
    std::array<std::atomic<std::int64_t>, 11> integers{};
    std::array<std::atomic<std::uint64_t>, 2> reals{};
};
AtomicThrottleLayout g_liveThrottleLayout;
std::atomic<std::uint64_t> g_rangeSequence{};
std::atomic<std::uint64_t> g_rangeTimestampUs{};
std::atomic<AbsoluteControlTelemetry::ThrottleCaptureTarget>
    g_throttleCaptureTarget{
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None};
std::atomic_bool g_registered{};
std::mutex g_descriptorMutex;

enum class Channel : std::uintptr_t {
    FlightThrottleAxis = 1,
    FlightPitchAxis,
    FlightYawAxis,
    FlightRollAxis,
    FlightStrafeLateralAxis,
    FlightStrafeVerticalAxis,
    FlightRotation,
    FlightStrafe,
    ThrottleRange,
    ThrottleResponse,
    AimResponse,
    DeviceAxes,
};

template <std::size_t Size>
void Copy(char (&destination)[Size], std::string_view value) noexcept
{
    const auto count = (std::min)(value.size(), Size - 1);
    std::memcpy(destination, value.data(), count);
    destination[count] = '\0';
}

std::uint64_t TimestampUs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void TouchRange() noexcept
{
    g_rangeTimestampUs.store(TimestampUs(), std::memory_order_relaxed);
    g_rangeSequence.fetch_add(1, std::memory_order_release);
}

void PublishLayout(
    const AbsoluteControlTelemetry::ThrottleLayout& layout) noexcept
{
    auto& mailbox = g_liveThrottleLayout;
    mailbox.guard.fetch_add(1, std::memory_order_acq_rel);
    const std::array<std::int64_t, 11> integers{
        layout.axisMinimum, layout.axisMaximum, layout.inverted ? 1 : 0,
        layout.reverseZoneEnabled ? 1 : 0,
        layout.boostZoneEnabled ? 1 : 0, layout.detentCenter,
        layout.detentDeadzone, layout.reverseZoneCenter,
        layout.reverseZoneDeadzone, layout.boostZoneCenter,
        layout.boostZoneDeadzone};
    for (std::size_t index = 0; index < integers.size(); ++index) {
        mailbox.integers[index].store(integers[index],
            std::memory_order_relaxed);
    }
    mailbox.reals[0].store(std::bit_cast<std::uint64_t>(layout.idlePlateau),
        std::memory_order_relaxed);
    mailbox.reals[1].store(std::bit_cast<std::uint64_t>(layout.saturation),
        std::memory_order_relaxed);
    mailbox.timestampUs.store(TimestampUs(), std::memory_order_relaxed);
    mailbox.guard.fetch_add(1, std::memory_order_release);
    TouchRange();
}

bool CopyLayout(AbsoluteControlTelemetry::ThrottleLayout& layout) noexcept
{
    const auto& mailbox = g_liveThrottleLayout;
    const auto before = mailbox.guard.load(std::memory_order_acquire);
    if ((before & 1U) != 0) return false;
    std::array<std::int64_t, 11> integers{};
    for (std::size_t index = 0; index < integers.size(); ++index) {
        integers[index] = mailbox.integers[index].load(
            std::memory_order_relaxed);
    }
    const auto idle = std::bit_cast<double>(
        mailbox.reals[0].load(std::memory_order_relaxed));
    const auto saturation = std::bit_cast<double>(
        mailbox.reals[1].load(std::memory_order_relaxed));
    const auto after = mailbox.guard.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0) return false;
    layout.axisMinimum = integers[0];
    layout.axisMaximum = integers[1];
    layout.inverted = integers[2] != 0;
    layout.reverseZoneEnabled = integers[3] != 0;
    layout.boostZoneEnabled = integers[4] != 0;
    layout.detentCenter = integers[5];
    layout.detentDeadzone = integers[6];
    layout.reverseZoneCenter = integers[7];
    layout.reverseZoneDeadzone = integers[8];
    layout.boostZoneCenter = integers[9];
    layout.boostZoneDeadzone = integers[10];
    layout.idlePlateau = idle;
    layout.saturation = saturation;
    return true;
}

AbsoluteControlTelemetry::AxisTuningPreviews DefaultAxisPreviews() noexcept
{
    AbsoluteControlTelemetry::AxisTuningPreviews previews;
    for (auto& preview : previews) {
        preview.sensitivity = 1.0;
        preview.saturation = 1.0;
    }
    return previews;
}

void PublishAxisPreviews(
    const AbsoluteControlTelemetry::AxisTuningPreviews& previews) noexcept
{
    auto& mailbox = g_axisPreviews;
    mailbox.guard.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t index = 0; index < previews.size(); ++index) {
        mailbox.inversions[index].store(previews[index].inverted ? 1U : 0U,
            std::memory_order_relaxed);
        mailbox.reals[index * 3].store(
            std::bit_cast<std::uint64_t>(previews[index].sensitivity),
            std::memory_order_relaxed);
        mailbox.reals[index * 3 + 1].store(
            std::bit_cast<std::uint64_t>(previews[index].saturation),
            std::memory_order_relaxed);
        mailbox.reals[index * 3 + 2].store(
            std::bit_cast<std::uint64_t>(previews[index].deadzone),
            std::memory_order_relaxed);
    }
    mailbox.timestampUs.store(TimestampUs(), std::memory_order_relaxed);
    mailbox.guard.fetch_add(1, std::memory_order_release);
}

bool CopyAxisPreviews(
    AbsoluteControlTelemetry::AxisTuningPreviews& previews,
    std::uint64_t& sequence, std::uint64_t& timestamp) noexcept
{
    const auto& mailbox = g_axisPreviews;
    const auto before = mailbox.guard.load(std::memory_order_acquire);
    if ((before & 1U) != 0) return false;
    if (before == 0) {
        previews = DefaultAxisPreviews();
        sequence = 0;
        timestamp = 0;
        return true;
    }
    for (std::size_t index = 0; index < previews.size(); ++index) {
        previews[index].inverted =
            mailbox.inversions[index].load(std::memory_order_relaxed) != 0;
        previews[index].sensitivity = std::bit_cast<double>(
            mailbox.reals[index * 3].load(std::memory_order_relaxed));
        previews[index].saturation = std::bit_cast<double>(
            mailbox.reals[index * 3 + 1].load(std::memory_order_relaxed));
        previews[index].deadzone = std::bit_cast<double>(
            mailbox.reals[index * 3 + 2].load(std::memory_order_relaxed));
    }
    timestamp = mailbox.timestampUs.load(std::memory_order_relaxed);
    const auto after = mailbox.guard.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0) return false;
    sequence = after / 2;
    return true;
}

template <std::size_t N>
void Publish(AtomicMailbox<N>& mailbox, const std::array<float, N>& values,
             std::uint32_t availableMask) noexcept
{
    mailbox.guard.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t index = 0; index < N; ++index) {
        const auto finite = std::isfinite(values[index]) ? values[index] : 0.0F;
        mailbox.values[index].store(
            std::bit_cast<std::uint32_t>(finite), std::memory_order_relaxed);
    }
    mailbox.availableMask.store(availableMask, std::memory_order_relaxed);
    mailbox.timestampUs.store(TimestampUs(), std::memory_order_relaxed);
    mailbox.guard.fetch_add(1, std::memory_order_release);
}

template <std::size_t N>
bool CopyMailbox(const AtomicMailbox<N>& mailbox,
                 double* values,
                 std::uint32_t& availableMask, std::uint64_t& sequence,
                 std::uint64_t& timestamp, std::size_t offset,
                 std::size_t count) noexcept
{
    const auto before = mailbox.guard.load(std::memory_order_acquire);
    if ((before & 1U) != 0) return false;
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = static_cast<double>(std::bit_cast<float>(
            mailbox.values[offset + index].load(std::memory_order_relaxed)));
    }
    availableMask = mailbox.availableMask.load(std::memory_order_relaxed) >> offset;
    timestamp = mailbox.timestampUs.load(std::memory_order_relaxed);
    const auto after = mailbox.guard.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0) return false;
    sequence = after / 2;
    const auto laneMask = count >= 32 ? ~0U : ((1U << count) - 1U);
    availableMask &= laneMask;
    return true;
}

Live::PlotSeriesDescriptorV1 Series(std::string_view id, std::string_view label,
                                    Live::VisualRole role) noexcept
{
    Live::PlotSeriesDescriptorV1 result;
    Copy(result.seriesId, id);
    Copy(result.label, label);
    result.visualRole = role;
    return result;
}

Live::RangeBandV1 Band(Live::RangeBandSemantic semantic, double minimum,
                       double maximum, Live::VisualRole role,
                       std::string_view label) noexcept
{
    Live::RangeBandV1 result;
    result.semantic = semantic;
    result.minimumValue = (std::min)(minimum, maximum);
    result.maximumValue = (std::max)(minimum, maximum);
    result.visualRole = role;
    Copy(result.label, label);
    return result;
}

Live::RangeMarkerV1 Marker(Live::RangeMarkerSemantic semantic, double value,
                           Live::VisualRole role, std::string_view id,
                           std::string_view label,
                           std::string_view controlId = {}) noexcept
{
    Live::RangeMarkerV1 result;
    result.semantic = semantic;
    result.value = value;
    result.visualRole = role;
    Copy(result.markerId, id);
    Copy(result.label, label);
    Copy(result.controlId, controlId);
    return result;
}

constexpr std::array<std::string_view, 6> kAxisPrefixes{
    "throttle", "pitch", "yaw", "roll", "strafe-lateral",
    "strafe-vertical"};
constexpr std::array<std::string_view, 6> kAxisSaturationControls{
    "", "pitch-saturation", "yaw-saturation", "roll-saturation",
    "strafe-lateral-saturation", "strafe-vertical-saturation"};
constexpr std::array<std::string_view, 6> kAxisDeadzoneControls{
    "", "pitch-deadzone", "yaw-deadzone", "roll-deadzone",
    "strafe-lateral-deadzone", "strafe-vertical-deadzone"};

void FillAxisRange(Live::LiveFrameV1::DynamicRangeV1& dynamic,
                   const AbsoluteControlTelemetry::AxisTuningPreview& preview,
                   double shapedOutput, std::size_t axisIndex) noexcept
{
    dynamic = {};
    dynamic.present = 1;
    const auto deadzone = std::clamp(preview.deadzone, 0.0, 0.95) * 100.0;
    const auto saturation = std::clamp(preview.saturation, 0.05, 1.0) * 100.0;
    const auto activeEdge = (std::max)(deadzone, saturation);
    const auto prefix = kAxisPrefixes[axisIndex];
    const auto id = [prefix](std::string_view suffix) {
        return std::string{prefix} + std::string{suffix};
    };
    const auto addBand = [&](Live::RangeBandSemantic semantic,
                             double minimum, double maximum,
                             Live::VisualRole role, std::string_view label) {
        if (dynamic.bandCount >= Live::kMaximumRangeBands) return;
        dynamic.bands[dynamic.bandCount++] =
            Band(semantic, minimum, maximum, role, label);
    };
    const auto addMarker = [&](Live::RangeMarkerSemantic semantic,
                               double value, Live::VisualRole role,
                               std::string_view markerId,
                               std::string_view label,
                               std::string_view controlId = {}) {
        if (dynamic.markerCount >= Live::kMaximumRangeMarkers) return;
        dynamic.markers[dynamic.markerCount++] = Marker(
            semantic, value, role, markerId, label, controlId);
    };

    addBand(Live::RangeBandSemantic::Custom, -100.0, -activeEdge,
        Live::VisualRole::Preview, "Full -");
    addBand(Live::RangeBandSemantic::Active, -activeEdge, -deadzone,
        Live::VisualRole::Positive, {});
    addBand(Live::RangeBandSemantic::Dead, -deadzone, deadzone,
        Live::VisualRole::Warning, "Deadzone");
    addBand(Live::RangeBandSemantic::Active, deadzone, activeEdge,
        Live::VisualRole::Positive, "Active travel");
    addBand(Live::RangeBandSemantic::Custom, activeEdge, 100.0,
        Live::VisualRole::Preview, "Full +");

    addMarker(Live::RangeMarkerSemantic::Center, -deadzone,
        Live::VisualRole::Warning, id("-deadzone-negative"),
        "Deadzone -", kAxisDeadzoneControls[axisIndex]);
    addMarker(Live::RangeMarkerSemantic::Center, deadzone,
        Live::VisualRole::Warning, id("-deadzone-positive"),
        "Deadzone +", kAxisDeadzoneControls[axisIndex]);
    addMarker(Live::RangeMarkerSemantic::Saturation, -saturation,
        Live::VisualRole::Accent, id("-saturation-negative"),
        "Full authority -", kAxisSaturationControls[axisIndex]);
    addMarker(Live::RangeMarkerSemantic::Saturation, saturation,
        Live::VisualRole::Accent, id("-saturation-positive"),
        "Full authority +", kAxisSaturationControls[axisIndex]);
    addMarker(Live::RangeMarkerSemantic::Custom,
        std::clamp(shapedOutput, -1.0, 1.0) * 100.0,
        Live::VisualRole::Positive, id("-output"), "Shaped output");
}

double LogicalPercent(std::int64_t raw,
    const AbsoluteControlTelemetry::ThrottleLayout& layout) noexcept
{
    const auto span = (std::max)(layout.axisMaximum - layout.axisMinimum,
                                 std::int64_t{1});
    return std::clamp(
        static_cast<double>(raw - layout.axisMinimum) /
            static_cast<double>(span), 0.0, 1.0) * 100.0;
}

void FillThrottleRange(
    Live::LiveFrameV1::DynamicRangeV1& dynamic,
    const AbsoluteControlTelemetry::ThrottleLayout& layout) noexcept
{
    dynamic = {};
    dynamic.present = 1;
    const auto addBand = [&](Live::RangeBandSemantic semantic,
                             std::int64_t minimum, std::int64_t maximum,
                             Live::VisualRole role, std::string_view label) {
        if (dynamic.bandCount >= Live::kMaximumRangeBands) return;
        dynamic.bands[dynamic.bandCount++] = Band(semantic,
            LogicalPercent(minimum, layout), LogicalPercent(maximum, layout),
            role, label);
    };
    const auto addMarker = [&](Live::RangeMarkerSemantic semantic,
                               std::int64_t value, Live::VisualRole role,
                               std::string_view id, std::string_view label,
                               std::string_view controlId = {}) {
        if (dynamic.markerCount >= Live::kMaximumRangeMarkers) return;
        dynamic.markers[dynamic.markerCount++] = Marker(semantic,
            LogicalPercent(value, layout), role, id, label, controlId);
    };
    const auto span = (std::max)(layout.axisMaximum - layout.axisMinimum,
        std::int64_t{1});
    const auto cruiseLow = layout.detentCenter - layout.detentDeadzone;
    const auto cruiseHigh = layout.detentCenter + layout.detentDeadzone;
    const auto boostPlateauLow =
        layout.boostZoneCenter - layout.boostZoneDeadzone;
    const auto boostStart =
        layout.boostZoneCenter + layout.boostZoneDeadzone;

    if (layout.reverseZoneEnabled) {
        addBand(Live::RangeBandSemantic::Reverse, layout.axisMinimum,
            layout.reverseZoneCenter - layout.reverseZoneDeadzone,
            Live::VisualRole::Critical, "Reverse");
        addBand(Live::RangeBandSemantic::Dead,
            layout.reverseZoneCenter - layout.reverseZoneDeadzone,
            layout.reverseZoneCenter + layout.reverseZoneDeadzone,
            Live::VisualRole::Warning, "Zero thrust");
        addBand(Live::RangeBandSemantic::Active,
            layout.reverseZoneCenter + layout.reverseZoneDeadzone,
            cruiseLow, Live::VisualRole::Positive, "Forward ramp");
    } else {
        const auto idleEnd = layout.axisMinimum + static_cast<std::int64_t>(
            std::clamp(layout.idlePlateau, 0.0, 1.0) * span);
        addBand(Live::RangeBandSemantic::Dead, layout.axisMinimum, idleEnd,
            Live::VisualRole::Critical, "Idle zone");
        const auto activeEnd = layout.boostZoneEnabled ? boostPlateauLow :
            layout.axisMinimum + static_cast<std::int64_t>(
                std::clamp(layout.saturation, 0.0, 1.0) * span);
        addBand(Live::RangeBandSemantic::Active, idleEnd, activeEnd,
            Live::VisualRole::Positive, "Active travel");
    }

    addBand(Live::RangeBandSemantic::Cruise, cruiseLow, cruiseHigh,
        Live::VisualRole::Warning, "Cruise detent");
    if (layout.reverseZoneEnabled) {
        addBand(Live::RangeBandSemantic::Active, cruiseHigh,
            layout.boostZoneEnabled ? boostPlateauLow : layout.axisMaximum,
            Live::VisualRole::Positive, "Forward ramp");
    }
    if (layout.boostZoneEnabled) {
        addBand(Live::RangeBandSemantic::Cruise, boostPlateauLow,
            boostStart, Live::VisualRole::Preview, "Full thrust plateau");
        addBand(Live::RangeBandSemantic::Boost, boostStart,
            layout.axisMaximum, Live::VisualRole::Critical, "Boost");
    } else if (!layout.reverseZoneEnabled) {
        const auto saturation = layout.axisMinimum + static_cast<std::int64_t>(
            std::clamp(layout.saturation, 0.0, 1.0) * span);
        addBand(Live::RangeBandSemantic::Dead, saturation,
            layout.axisMaximum, Live::VisualRole::Neutral, "Top plateau");
        addMarker(Live::RangeMarkerSemantic::Saturation, saturation,
            Live::VisualRole::Accent, "throttle-saturation", "Full thrust",
            "throttle-saturation");
    }

    addMarker(Live::RangeMarkerSemantic::Detent, layout.detentCenter,
        Live::VisualRole::Accent, "cruise-detent", "Cruise detent",
        "throttle-detent-center");
    if (layout.reverseZoneEnabled) {
        addMarker(Live::RangeMarkerSemantic::Center,
            layout.reverseZoneCenter, Live::VisualRole::Warning,
            "reverse-center", "Zero thrust", "reverse-zone-center");
    }
    if (layout.boostZoneEnabled) {
        addMarker(Live::RangeMarkerSemantic::Saturation,
            layout.boostZoneCenter, Live::VisualRole::Critical,
            "boost-center", "Boost landmark", "boost-zone-center");
    }
}

Live::LiveChannelDescriptorV1 BaseChannel(
    std::string_view page, std::string_view id, std::string_view title,
    Live::ComponentKind kind, Channel channel) noexcept;

Live::Result __cdecl ReadFrame(void* context, Live::LiveFrameV1* output) noexcept
{
    if (!context || !output ||
        output->structSize < Live::kLiveFrameV1BaseSize) {
        return Live::Result::InvalidArgument;
    }
    const auto outputSize = output->structSize;
    Live::LiveFrameV1 frame;
    frame.kind = Live::ComponentKind::TelemetryPlot;
    const auto channel = static_cast<Channel>(reinterpret_cast<std::uintptr_t>(context));
    bool copied{};
    switch (channel) {
    case Channel::FlightThrottleAxis: {
        frame.kind = Live::ComponentKind::RangeMeter;
        AbsoluteControlTelemetry::ThrottleLayout layout;
        double sample[2]{};
        std::uint32_t sampleMask{};
        std::uint64_t sampleSequence{};
        std::uint64_t sampleTimestamp{};
        copied = CopyLayout(layout) && CopyMailbox(g_throttle, sample,
            sampleMask, sampleSequence, sampleTimestamp, 0, 2);
        if (!copied) break;
        frame.sequence = g_rangeSequence.load(std::memory_order_acquire);
        frame.monotonicTimestampUs = (std::max)(sampleTimestamp,
            g_rangeTimestampUs.load(std::memory_order_relaxed));
        frame.rangeMeter.available = (sampleMask & 1U) != 0 ? 1U : 0U;
        if (frame.rangeMeter.available != 0) {
            const auto span = (std::max)(
                layout.axisMaximum - layout.axisMinimum, std::int64_t{1});
            auto raw = layout.axisMinimum + static_cast<std::int64_t>(
                std::clamp((sample[0] + 1.0) * 0.5, 0.0, 1.0) * span);
            if (layout.inverted) {
                raw = layout.axisMinimum + layout.axisMaximum - raw;
            }
            frame.rangeMeter.liveValue = LogicalPercent(raw, layout);
        }
        FillThrottleRange(frame.dynamicRange, layout);
        for (std::uint32_t index = 0;
             index < frame.dynamicRange.markerCount; ++index) {
            frame.dynamicRange.markers[index].controlId[0] = '\0';
        }
        break;
    }
    case Channel::FlightPitchAxis:
    case Channel::FlightYawAxis:
    case Channel::FlightRollAxis:
    case Channel::FlightStrafeLateralAxis:
    case Channel::FlightStrafeVerticalAxis: {
        frame.kind = Live::ComponentKind::RangeMeter;
        const auto axisIndex = static_cast<std::size_t>(channel) -
            static_cast<std::size_t>(Channel::FlightThrottleAxis);
        double sample[2]{};
        std::uint32_t sampleMask{};
        std::uint64_t sampleSequence{};
        std::uint64_t sampleTimestamp{};
        const auto sampleCopied = CopyMailbox(g_flight, sample, sampleMask,
            sampleSequence, sampleTimestamp, (axisIndex - 1) * 2, 2);
        AbsoluteControlTelemetry::AxisTuningPreviews previews;
        std::uint64_t previewSequence{};
        std::uint64_t previewTimestamp{};
        const auto previewCopied = CopyAxisPreviews(
            previews, previewSequence, previewTimestamp);
        copied = sampleCopied && previewCopied;
        if (!copied) break;
        const auto physical = std::clamp(sample[0], -1.0, 1.0);
        const auto logical = previews[axisIndex].inverted ?
            -physical : physical;
        const auto shaped = static_cast<double>(AxisShapingPolicy::Shape(
            static_cast<float>(logical),
            static_cast<float>(previews[axisIndex].sensitivity),
            static_cast<float>(previews[axisIndex].saturation),
            static_cast<float>(previews[axisIndex].deadzone)));
        frame.sequence = sampleSequence + previewSequence;
        frame.monotonicTimestampUs = (std::max)(
            sampleTimestamp, previewTimestamp);
        frame.rangeMeter.available = (sampleMask & 1U) != 0 ? 1U : 0U;
        frame.rangeMeter.liveValue = logical * 100.0;
        FillAxisRange(frame.dynamicRange, previews[axisIndex], shaped,
            axisIndex);
        break;
    }
    case Channel::FlightRotation:
        frame.telemetryPlot.seriesCount = 6;
        copied = CopyMailbox(g_flight, frame.telemetryPlot.values,
            frame.telemetryPlot.availableMask, frame.sequence,
            frame.monotonicTimestampUs, 0, 6);
        break;
    case Channel::FlightStrafe:
        frame.telemetryPlot.seriesCount = 4;
        copied = CopyMailbox(g_flight, frame.telemetryPlot.values,
            frame.telemetryPlot.availableMask, frame.sequence,
            frame.monotonicTimestampUs, 6, 4);
        break;
    case Channel::ThrottleRange: {
        frame.kind = Live::ComponentKind::RangeMeter;
        AbsoluteControlTelemetry::ThrottleLayout layout;
        copied = CopyLayout(layout);
        const auto rangeSequence =
            g_rangeSequence.load(std::memory_order_acquire);
        frame.sequence = rangeSequence;
        frame.monotonicTimestampUs =
            g_rangeTimestampUs.load(std::memory_order_relaxed);
        const bool rawAvailable =
            g_throttleRawAvailable.load(std::memory_order_acquire);
        const auto raw = g_throttleRaw.load(std::memory_order_relaxed);
        frame.rangeMeter.available = rawAvailable ? 1U : 0U;
        frame.rangeMeter.liveValue = rawAvailable ?
            LogicalPercent(raw, layout) : 0.0;
        const auto capture =
            g_throttleCaptureTarget.load(std::memory_order_acquire);
        if (rawAvailable) {
            switch (capture) {
            case AbsoluteControlTelemetry::ThrottleCaptureTarget::Detent:
                layout.detentCenter = raw;
                break;
            case AbsoluteControlTelemetry::ThrottleCaptureTarget::Reverse:
                layout.reverseZoneCenter = raw;
                break;
            case AbsoluteControlTelemetry::ThrottleCaptureTarget::Boost:
                layout.boostZoneCenter = raw;
                break;
            case AbsoluteControlTelemetry::ThrottleCaptureTarget::None:
                break;
            }
        }
        FillThrottleRange(frame.dynamicRange, layout);
        if (capture != AbsoluteControlTelemetry::ThrottleCaptureTarget::None) {
            const std::string_view activeMarker =
                capture == AbsoluteControlTelemetry::ThrottleCaptureTarget::Detent ?
                    "cruise-detent" :
                capture == AbsoluteControlTelemetry::ThrottleCaptureTarget::Reverse ?
                    "reverse-center" : "boost-center";
            for (std::uint32_t index = 0;
                 index < frame.dynamicRange.markerCount; ++index) {
                auto& marker = frame.dynamicRange.markers[index];
                if (std::string_view{marker.markerId} == activeMarker) {
                    marker.visualRole = Live::VisualRole::Live;
                    break;
                }
            }
        }
        break;
    }
    case Channel::ThrottleResponse:
        frame.telemetryPlot.seriesCount = 5;
        copied = CopyMailbox(g_throttle, frame.telemetryPlot.values,
            frame.telemetryPlot.availableMask, frame.sequence,
            frame.monotonicTimestampUs, 0, 5);
        break;
    case Channel::AimResponse:
        frame.telemetryPlot.seriesCount = 4;
        copied = CopyMailbox(g_aim, frame.telemetryPlot.values,
            frame.telemetryPlot.availableMask, frame.sequence,
            frame.monotonicTimestampUs, 0, 4);
        break;
    case Channel::DeviceAxes:
        frame.telemetryPlot.seriesCount = 8;
        copied = CopyMailbox(g_device, frame.telemetryPlot.values,
            frame.telemetryPlot.availableMask, frame.sequence,
            frame.monotonicTimestampUs, 0, 8);
        break;
    }
    if (!copied) return Live::Result::Stale;
    const bool available = frame.kind == Live::ComponentKind::RangeMeter ?
        frame.rangeMeter.available != 0 : frame.telemetryPlot.availableMask != 0;
    if (!available) frame.flags |= Live::kFrameUnavailable;
    frame.structSize = (std::min)(outputSize,
        static_cast<std::uint32_t>(sizeof(frame)));
    std::memcpy(output, &frame, frame.structSize);
    return Live::Result::Ok;
}

Live::LiveChannelDescriptorV1 BaseChannel(
    std::string_view page, std::string_view id, std::string_view title,
    Live::ComponentKind kind, Channel channel) noexcept
{
    Live::LiveChannelDescriptorV1 result;
    Copy(result.moduleId, "absolute.hotas");
    Copy(result.pageId, page);
    Copy(result.channelId, id);
    Copy(result.title, title);
    result.kind = kind;
    result.context = reinterpret_cast<void*>(static_cast<std::uintptr_t>(channel));
    result.readLiveFrame = &ReadFrame;
    return result;
}

std::array<Live::LiveChannelDescriptorV1, 12> BuildChannels() noexcept
{
    std::array<Live::LiveChannelDescriptorV1, 12> channels;
    constexpr std::array<std::string_view, 6> axisIds{
        "axis-throttle", "axis-pitch", "axis-yaw", "axis-roll",
        "axis-strafe-lateral", "axis-strafe-vertical"};
    constexpr std::array<std::string_view, 6> axisTitles{
        "Throttle zones - edit in Throttle Setup", "Pitch axis range", "Yaw axis range",
        "Roll axis range", "Lateral strafe axis range",
        "Vertical strafe axis range"};
    constexpr std::array<Channel, 6> axisChannels{
        Channel::FlightThrottleAxis, Channel::FlightPitchAxis,
        Channel::FlightYawAxis, Channel::FlightRollAxis,
        Channel::FlightStrafeLateralAxis,
        Channel::FlightStrafeVerticalAxis};
    for (std::size_t index = 0; index < axisIds.size(); ++index) {
        auto& axis = channels[index] = BaseChannel(
            "hotas-flight-axes", axisIds[index], axisTitles[index],
            Live::ComponentKind::RangeMeter, axisChannels[index]);
        axis.flags = Live::kLivePresentationPinned;
        axis.rangeMeter.minimumValue = index == 0 ? 0.0 : -100.0;
        axis.rangeMeter.maximumValue = 100.0;
        Copy(axis.rangeMeter.valueFormat, "%.0f%%");
        Live::LiveFrameV1::DynamicRangeV1 initialRange;
        if (index == 0) {
            FillThrottleRange(initialRange, g_throttleLayout);
            for (std::uint32_t marker = 0;
                 marker < initialRange.markerCount; ++marker) {
                initialRange.markers[marker].controlId[0] = '\0';
            }
        } else {
            FillAxisRange(initialRange, DefaultAxisPreviews()[index], 0.0,
                index);
        }
        axis.rangeMeter.bandCount = initialRange.bandCount;
        axis.rangeMeter.markerCount = initialRange.markerCount;
        std::copy_n(initialRange.bands, initialRange.bandCount,
            axis.rangeMeter.bands);
        std::copy_n(initialRange.markers, initialRange.markerCount,
            axis.rangeMeter.markers);
    }

    auto& rotation = channels[6] = BaseChannel(
        "hotas-flight-axes", "flight-rotation", "Rotation input and output",
        Live::ComponentKind::TelemetryPlot, Channel::FlightRotation);
    rotation.telemetryPlot.seriesCount = 6;
    rotation.telemetryPlot.historyCapacity = 120;
    rotation.telemetryPlot.minimumValue = -1.0;
    rotation.telemetryPlot.maximumValue = 1.0;
    rotation.telemetryPlot.series[0] = Series("pitch-input", "Pitch input", Live::VisualRole::Live);
    rotation.telemetryPlot.series[1] = Series("pitch-output", "Pitch output", Live::VisualRole::Preview);
    rotation.telemetryPlot.series[2] = Series("yaw-input", "Yaw input", Live::VisualRole::Live);
    rotation.telemetryPlot.series[3] = Series("yaw-output", "Yaw output", Live::VisualRole::Preview);
    rotation.telemetryPlot.series[4] = Series("roll-input", "Roll input", Live::VisualRole::Live);
    rotation.telemetryPlot.series[5] = Series("roll-output", "Roll output", Live::VisualRole::Preview);

    auto& strafe = channels[7] = BaseChannel(
        "hotas-flight-axes", "flight-strafe", "Strafe input and output",
        Live::ComponentKind::TelemetryPlot, Channel::FlightStrafe);
    strafe.telemetryPlot.seriesCount = 4;
    strafe.telemetryPlot.historyCapacity = 120;
    strafe.telemetryPlot.minimumValue = -1.0;
    strafe.telemetryPlot.maximumValue = 1.0;
    strafe.telemetryPlot.series[0] = Series("lateral-input", "Lateral input", Live::VisualRole::Live);
    strafe.telemetryPlot.series[1] = Series("lateral-output", "Lateral output", Live::VisualRole::Preview);
    strafe.telemetryPlot.series[2] = Series("vertical-input", "Vertical input", Live::VisualRole::Live);
    strafe.telemetryPlot.series[3] = Series("vertical-output", "Vertical output", Live::VisualRole::Preview);

    auto& range = channels[8] = BaseChannel(
        "hotas-throttle", "throttle-range", "Throttle range and landmarks",
        Live::ComponentKind::RangeMeter, Channel::ThrottleRange);
    range.flags = Live::kLivePresentationPinned;
    range.rangeMeter.minimumValue = 0.0;
    range.rangeMeter.maximumValue = 100.0;
    Copy(range.rangeMeter.valueFormat, "%.0f%%");
    const auto& layout = g_throttleLayout;
    Live::LiveFrameV1::DynamicRangeV1 initialRange;
    FillThrottleRange(initialRange, layout);
    range.rangeMeter.bandCount = initialRange.bandCount;
    range.rangeMeter.markerCount = initialRange.markerCount;
    std::copy_n(initialRange.bands, initialRange.bandCount,
        range.rangeMeter.bands);
    std::copy_n(initialRange.markers, initialRange.markerCount,
        range.rangeMeter.markers);

    auto& throttle = channels[9] = BaseChannel(
        "hotas-throttle", "throttle-response", "Throttle interpretation",
        Live::ComponentKind::TelemetryPlot, Channel::ThrottleResponse);
    throttle.flags = Live::kLivePresentationSecondary |
        Live::kLivePresentationCollapsedByDefault;
    throttle.telemetryPlot.seriesCount = 5;
    throttle.telemetryPlot.historyCapacity = 120;
    throttle.telemetryPlot.minimumValue = -1.0;
    throttle.telemetryPlot.maximumValue = 1.0;
    throttle.telemetryPlot.series[0] = Series("hardware-input", "Calibrated input", Live::VisualRole::Live);
    throttle.telemetryPlot.series[1] = Series("logical-request", "Logical request", Live::VisualRole::Preview);
    throttle.telemetryPlot.series[2] = Series("runtime-target", "Runtime target", Live::VisualRole::Positive);
    throttle.telemetryPlot.series[3] = Series("reverse-active", "Reverse active", Live::VisualRole::Warning);
    throttle.telemetryPlot.series[4] = Series("boost-active", "Boost active", Live::VisualRole::Critical);

    auto& aim = channels[10] = BaseChannel(
        "hotas-aiming", "aim-response", "Aim input and output",
        Live::ComponentKind::TelemetryPlot, Channel::AimResponse);
    aim.telemetryPlot.seriesCount = 4;
    aim.telemetryPlot.historyCapacity = 120;
    aim.telemetryPlot.minimumValue = -1.0;
    aim.telemetryPlot.maximumValue = 1.0;
    aim.telemetryPlot.series[0] = Series("yaw-input", "Aim yaw input", Live::VisualRole::Live);
    aim.telemetryPlot.series[1] = Series("pitch-input", "Aim pitch input", Live::VisualRole::Live);
    aim.telemetryPlot.series[2] = Series("yaw-output", "Aim yaw output", Live::VisualRole::Preview);
    aim.telemetryPlot.series[3] = Series("pitch-output", "Aim pitch output", Live::VisualRole::Preview);

    auto& device = channels[11] = BaseChannel(
        "hotas-devices", "device-axes", "Selected device axes",
        Live::ComponentKind::TelemetryPlot, Channel::DeviceAxes);
    device.telemetryPlot.seriesCount = 8;
    device.telemetryPlot.historyCapacity = 120;
    device.telemetryPlot.minimumValue = -1.0;
    device.telemetryPlot.maximumValue = 1.0;
    constexpr std::array<std::string_view, 8> deviceAxisIds{
        "x", "y", "z", "rx", "ry", "rz", "slider-0", "slider-1"};
    constexpr std::array<std::string_view, 8> deviceAxisLabels{
        "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 0", "Slider 1"};
    for (std::size_t index = 0; index < deviceAxisIds.size(); ++index) {
        device.telemetryPlot.series[index] = Series(
            deviceAxisIds[index], deviceAxisLabels[index],
            Live::VisualRole::Live);
    }
    return channels;
}

std::array<Live::LiveChannelDescriptorV1, 12> g_channels = BuildChannels();
} // namespace

namespace AbsoluteControlTelemetry {

void PublishFlight(const FlightSample& sample) noexcept
{
    Publish(g_flight, sample.values, sample.availableMask);
}

void PublishThrottle(const ThrottleSample& sample) noexcept
{
    g_throttleRaw.store(sample.logicalRawPosition, std::memory_order_relaxed);
    g_throttleRawAvailable.store(
        sample.logicalRawAvailable, std::memory_order_release);
    Publish(g_throttle, sample.values, sample.availableMask);
    TouchRange();
}

bool ReadPrimaryThrottleRaw(std::int64_t& logicalRawPosition) noexcept
{
    if (!g_throttleRawAvailable.load(std::memory_order_acquire)) return false;
    logicalRawPosition = g_throttleRaw.load(std::memory_order_relaxed);
    return true;
}

void PublishAim(const AimSample& sample) noexcept
{
    Publish(g_aim, sample.values, sample.availableMask);
}

void PublishDevice(const DeviceSample& sample) noexcept
{
    Publish(g_device, sample.values, sample.availableMask);
}

void SetThrottleLayout(const ThrottleLayout& layout) noexcept
{
    std::scoped_lock lock(g_descriptorMutex);
    g_throttleLayout = layout;
    PublishLayout(layout);
    if (!g_registered.load(std::memory_order_acquire)) {
        g_channels = BuildChannels();
    }
}

void SetThrottlePreview(const ThrottlePreview& preview) noexcept
{
    ThrottleLayout layout;
    for (int attempt = 0; attempt < 4 && !CopyLayout(layout); ++attempt) {}
    layout.inverted = preview.inverted;
    layout.reverseZoneEnabled = preview.reverseZoneEnabled;
    layout.boostZoneEnabled = preview.boostZoneEnabled;
    layout.idlePlateau = preview.idlePlateau;
    layout.saturation = preview.saturation;
    layout.detentCenter = preview.detentCenter;
    layout.detentDeadzone = preview.detentDeadzone;
    layout.reverseZoneCenter = preview.reverseZoneCenter;
    layout.reverseZoneDeadzone = preview.reverseZoneDeadzone;
    layout.boostZoneCenter = preview.boostZoneCenter;
    layout.boostZoneDeadzone = preview.boostZoneDeadzone;
    PublishLayout(layout);
}

void SetAxisTuningPreviews(const AxisTuningPreviews& previews) noexcept
{
    PublishAxisPreviews(previews);
}

void SetThrottleCaptureTarget(ThrottleCaptureTarget target) noexcept
{
    g_throttleCaptureTarget.store(target, std::memory_order_release);
    TouchRange();
}

ThrottleCaptureTarget GetThrottleCaptureTarget() noexcept
{
    return g_throttleCaptureTarget.load(std::memory_order_acquire);
}

void MarkUnavailable() noexcept
{
    g_throttleRawAvailable.store(false, std::memory_order_release);
    Publish(g_flight, {}, 0);
    Publish(g_throttle, {}, 0);
    TouchRange();
    Publish(g_aim, {}, 0);
    Publish(g_device, {}, 0);
}

Live::Result Register(const Live::ExperimentalApiV1* api) noexcept
{
    if (g_registered.load(std::memory_order_acquire)) return Live::Result::Ok;
    if (!api || api->structSize < Live::kExperimentalApiV1BaseSize ||
        api->abiVersion != Live::kAbiVersion || !api->registerLiveChannel ||
        !api->unregisterModule) return Live::Result::InvalidArgument;
    std::scoped_lock lock(g_descriptorMutex);
    if (g_registered.load(std::memory_order_relaxed)) return Live::Result::Ok;
    const auto capabilities = api->structSize >= sizeof(Live::ExperimentalApiV1)
        ? api->capabilities : Live::kLiveCapabilityNone;
    for (const auto& channel : g_channels) {
        auto compatible = channel;
        if ((capabilities & Live::kLiveCapabilityPresentationFlags) == 0) {
            compatible.flags &= Live::kSegmentedGridCycleOnClick;
        }
        const auto result = api->registerLiveChannel(&compatible);
        if (result != Live::Result::Ok && result != Live::Result::Duplicate) {
            (void)api->unregisterModule("absolute.hotas");
            return result;
        }
    }
    g_registered.store(true, std::memory_order_release);
    return Live::Result::Ok;
}

bool IsRegistered() noexcept
{
    return g_registered.load(std::memory_order_acquire);
}

namespace Testing {
const Live::LiveChannelDescriptorV1* Channels(std::size_t& count) noexcept
{
    count = g_channels.size();
    return g_channels.data();
}

void Reset() noexcept
{
    std::scoped_lock lock(g_descriptorMutex);
    g_registered.store(false, std::memory_order_release);
    g_throttleLayout = {};
    PublishLayout(g_throttleLayout);
    PublishAxisPreviews(DefaultAxisPreviews());
    g_throttleCaptureTarget.store(
        ThrottleCaptureTarget::None, std::memory_order_release);
    g_channels = BuildChannels();
    MarkUnavailable();
}
} // namespace Testing
} // namespace AbsoluteControlTelemetry

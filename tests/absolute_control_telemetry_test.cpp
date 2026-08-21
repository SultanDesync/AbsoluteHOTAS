#include "AbsoluteControlTelemetry.h"
#include "AxisShapingPolicy.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {
namespace Live = AbsoluteControlPanelExperimental;

std::array<Live::LiveChannelDescriptorV1, 12> g_channels{};
std::size_t g_registered{};
std::size_t g_failAt{g_channels.size()};
std::size_t g_unregisterCalls{};

Live::Result __cdecl RegisterChannel(
    const Live::LiveChannelDescriptorV1* channel) noexcept
{
    if (!channel || g_registered >= g_channels.size()) {
        return Live::Result::InvalidArgument;
    }
    if (g_registered == g_failAt) return Live::Result::Rejected;
    g_channels[g_registered++] = *channel;
    return Live::Result::Ok;
}

Live::Result __cdecl UnregisterModule(const char* moduleId) noexcept
{
    if (!moduleId || std::strcmp(moduleId, "absolute.hotas") != 0) {
        return Live::Result::InvalidArgument;
    }
    ++g_unregisterCalls;
    g_channels = {};
    g_registered = 0;
    return Live::Result::Ok;
}

Live::Result __cdecl Refresh(const char*, const char*, const char*) noexcept
{
    return Live::Result::Ok;
}

Live::ExperimentalApiV1 g_api{
    .structSize = sizeof(Live::ExperimentalApiV1),
    .abiVersion = Live::kAbiVersion,
    .registerLiveChannel = &RegisterChannel,
    .unregisterModule = &UnregisterModule,
    .requestImmediateRefresh = &Refresh,
};

void Reset()
{
    g_channels = {};
    g_registered = 0;
    g_failAt = g_channels.size();
    g_unregisterCalls = 0;
    AbsoluteControlTelemetry::Testing::Reset();
}

const Live::LiveChannelDescriptorV1& Channel(std::string_view id)
{
    for (std::size_t index = 0; index < g_registered; ++index) {
        if (g_channels[index].channelId == id) return g_channels[index];
    }
    assert(false);
    return g_channels[0];
}

Live::LiveFrameV1 Read(const Live::LiveChannelDescriptorV1& channel)
{
    Live::LiveFrameV1 frame;
    assert(channel.readLiveFrame(channel.context, &frame) == Live::Result::Ok);
    return frame;
}
} // namespace

int main()
{
    static_assert(std::is_standard_layout_v<Live::LiveFrameV1>);
    static_assert(std::is_trivially_copyable_v<Live::LiveFrameV1>);
    static_assert(Live::kMaximumPlotSeries == 8);
    static_assert(Live::kMaximumChannels == 16);

    // The telemetry preview and runtime injection both use this policy. Lock
    // deadzone, saturation, sensitivity, sign, and clamp behavior together.
    assert(AxisShapingPolicy::Shape(0.05F, 1.0F, 0.8F, 0.1F) == 0.0F);
    assert(std::abs(AxisShapingPolicy::Shape(0.45F, 1.0F, 0.8F, 0.1F) -
                    0.5F) < 1e-6F);
    assert(std::abs(AxisShapingPolicy::Shape(-0.45F, 1.0F, 0.8F, 0.1F) +
                    0.5F) < 1e-6F);
    assert(AxisShapingPolicy::Shape(0.8F, 2.0F, 0.8F, 0.1F) == 1.0F);

    Reset();
    Live::ExperimentalApiV1 invalid = g_api;
    invalid.structSize = Live::kExperimentalApiV1BaseSize - 1;
    assert(AbsoluteControlTelemetry::Register(&invalid) ==
           Live::Result::InvalidArgument);

    Reset();
    g_failAt = 3;
    assert(AbsoluteControlTelemetry::Register(&g_api) == Live::Result::Rejected);
    assert(g_unregisterCalls == 1);
    assert(!AbsoluteControlTelemetry::IsRegistered());

    Reset();
    auto legacyApi = g_api;
    legacyApi.capabilities = Live::kLiveCapabilityGridControlAssociations;
    assert(AbsoluteControlTelemetry::Register(&legacyApi) == Live::Result::Ok);
    assert(Channel("throttle-range").flags == Live::kSegmentedGridNone);
    assert(Channel("throttle-response").flags == Live::kSegmentedGridNone);

    Reset();
    AbsoluteControlTelemetry::SetThrottleLayout({
        .reverseZoneEnabled = true,
        .boostZoneEnabled = true,
        .detentCenter = 32768,
        .detentDeadzone = 500,
        .reverseZoneCenter = 3000,
        .reverseZoneDeadzone = 3000,
        .boostZoneCenter = 62000,
        .boostZoneDeadzone = 2000,
    });
    assert(AbsoluteControlTelemetry::Register(&g_api) == Live::Result::Ok);
    assert(AbsoluteControlTelemetry::IsRegistered());
    assert(g_registered == 12);

    constexpr std::array expectedPages{
        "hotas-flight-axes", "hotas-flight-axes", "hotas-flight-axes",
        "hotas-flight-axes", "hotas-flight-axes", "hotas-flight-axes",
        "hotas-flight-axes", "hotas-flight-axes", "hotas-throttle",
        "hotas-throttle", "hotas-aiming", "hotas-devices",
    };
    for (std::size_t index = 0; index < g_registered; ++index) {
        const auto& channel = g_channels[index];
        assert(std::strcmp(channel.moduleId, "absolute.hotas") == 0);
        assert(std::strcmp(channel.pageId, expectedPages[index]) == 0);
        assert(channel.readLiveFrame && channel.context);
        const std::string_view id = channel.channelId;
        assert(id.find("head") == std::string_view::npos);
        assert(id.find("hosam") == std::string_view::npos);
        assert(id.find("mouse") == std::string_view::npos);
        assert(id.find("power") == std::string_view::npos);
    }

    const auto& range = Channel("throttle-range");
    assert(range.kind == Live::ComponentKind::RangeMeter);
    assert((range.flags & Live::kLivePresentationPinned) != 0);
    assert(range.rangeMeter.minimumValue == 0.0);
    assert(range.rangeMeter.maximumValue == 100.0);
    assert(range.rangeMeter.bandCount == 7);
    assert(range.rangeMeter.markerCount == 3);
    assert(std::strcmp(range.rangeMeter.markers[0].controlId,
                       "throttle-detent-center") == 0);
    assert(range.rangeMeter.markers[0].value > 49.9 &&
           range.rangeMeter.markers[0].value < 50.1);
    const auto& response = Channel("throttle-response");
    assert((response.flags & Live::kLivePresentationSecondary) != 0);
    assert((response.flags &
        Live::kLivePresentationCollapsedByDefault) != 0);

    auto unavailable = Read(Channel("flight-rotation"));
    assert((unavailable.flags & Live::kFrameUnavailable) != 0);
    assert(unavailable.telemetryPlot.availableMask == 0);

    AbsoluteControlTelemetry::FlightSample flight;
    flight.values = {-0.5F, -0.25F, 0.4F, 0.3F, 0.2F, 0.1F,
                     -0.8F, -0.7F, 0.6F, 0.5F};
    flight.availableMask = 0x3FFU;
    AbsoluteControlTelemetry::PublishFlight(flight);
    const auto rotation = Read(Channel("flight-rotation"));
    assert(rotation.kind == Live::ComponentKind::TelemetryPlot);
    assert(rotation.telemetryPlot.seriesCount == 6);
    assert(rotation.telemetryPlot.availableMask == 0x3FU);
    assert(std::abs(rotation.telemetryPlot.values[0] + 0.5) < 1e-6);
    assert(std::abs(rotation.telemetryPlot.values[5] - 0.1) < 1e-6);
    const auto strafe = Read(Channel("flight-strafe"));
    assert(strafe.telemetryPlot.seriesCount == 4);
    assert(strafe.telemetryPlot.availableMask == 0xFU);
    assert(std::abs(strafe.telemetryPlot.values[0] + 0.8) < 1e-6);
    assert(std::abs(strafe.telemetryPlot.values[3] - 0.5) < 1e-6);
    const auto pitch = Read(Channel("axis-pitch"));
    assert(pitch.kind == Live::ComponentKind::RangeMeter);
    assert(pitch.rangeMeter.available == 1);
    assert(std::abs(pitch.rangeMeter.liveValue + 50.0) < 1e-6);
    assert(pitch.dynamicRange.bandCount == 5);
    assert(pitch.dynamicRange.markerCount == 5);
    assert(std::strcmp(pitch.dynamicRange.markers[0].controlId,
        "pitch-deadzone") == 0);
    assert(std::strcmp(pitch.dynamicRange.markers[1].controlId,
        "pitch-deadzone") == 0);
    assert(std::strcmp(pitch.dynamicRange.markers[2].controlId,
        "pitch-saturation") == 0);
    assert(std::strcmp(pitch.dynamicRange.markers[3].controlId,
        "pitch-saturation") == 0);

    AbsoluteControlTelemetry::AxisTuningPreviews axisPreviews{};
    for (auto& preview : axisPreviews) {
        preview.sensitivity = 1.0;
        preview.saturation = 1.0;
    }
    axisPreviews[1] = {true, 1.0, 0.8, 0.1};
    AbsoluteControlTelemetry::SetAxisTuningPreviews(axisPreviews);
    const auto pitchPreview = Read(Channel("axis-pitch"));
    assert(pitchPreview.sequence > pitch.sequence);
    assert(std::abs(pitchPreview.rangeMeter.liveValue - 50.0) < 1e-6);
    assert(std::abs(pitchPreview.dynamicRange.markers[1].value - 10.0) < 1e-6);
    assert(std::abs(pitchPreview.dynamicRange.markers[3].value - 80.0) < 1e-6);
    assert(std::abs(pitchPreview.dynamicRange.markers[4].value -
                    (4.0 / 7.0 * 100.0)) < 1e-4);

    AbsoluteControlTelemetry::ThrottleSample throttle;
    throttle.values = {0.75F, 0.5F, 0.45F, 0.0F, 1.0F};
    throttle.availableMask = 0x1FU;
    throttle.logicalRawPosition = 49152;
    throttle.logicalRawAvailable = true;
    AbsoluteControlTelemetry::PublishThrottle(throttle);
    std::int64_t logicalRaw{};
    assert(AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(logicalRaw));
    assert(logicalRaw == 49152);
    const auto rangeFrame = Read(range);
    assert(rangeFrame.rangeMeter.available == 1);
    assert(std::abs(rangeFrame.rangeMeter.liveValue - 75.001) < 0.01);
    assert(rangeFrame.dynamicRange.present == 1);
    assert(rangeFrame.dynamicRange.bandCount == 7);
    assert(rangeFrame.dynamicRange.markerCount == 3);

    // Draft preview changes bands immediately without applying runtime config.
    AbsoluteControlTelemetry::SetThrottlePreview({
        .reverseZoneEnabled = true,
        .boostZoneEnabled = true,
        .idlePlateau = 0.1,
        .saturation = 0.8,
        .detentCenter = 32768,
        .detentDeadzone = 500,
        .reverseZoneCenter = 10000,
        .reverseZoneDeadzone = 4000,
        .boostZoneCenter = 60000,
        .boostZoneDeadzone = 3000,
    });
    const auto previewFrame = Read(range);
    assert(previewFrame.sequence > rangeFrame.sequence);
    assert(previewFrame.dynamicRange.markers[1].value > 15.2 &&
           previewFrame.dynamicRange.markers[1].value < 15.3);

    // During set-by-feel, the selected landmark follows the live logical raw
    // throttle while the draft widths continue to shape the same range frame.
    AbsoluteControlTelemetry::SetThrottleCaptureTarget(
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Reverse);
    throttle.logicalRawPosition = 20000;
    AbsoluteControlTelemetry::PublishThrottle(throttle);
    const auto trackingFrame = Read(range);
    assert(trackingFrame.dynamicRange.markers[1].value > 30.5 &&
           trackingFrame.dynamicRange.markers[1].value < 30.6);
    assert(trackingFrame.dynamicRange.markers[1].visualRole ==
           Live::VisualRole::Live);
    AbsoluteControlTelemetry::SetThrottleCaptureTarget(
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    const auto throttleFrame = Read(response);
    assert(throttleFrame.telemetryPlot.seriesCount == 5);
    assert(throttleFrame.telemetryPlot.availableMask == 0x1FU);
    assert(std::abs(throttleFrame.telemetryPlot.values[4] - 1.0) < 1e-6);
    const auto throttleAxis = Read(Channel("axis-throttle"));
    assert(throttleAxis.kind == Live::ComponentKind::RangeMeter);
    assert(throttleAxis.rangeMeter.available == 1);
    assert(std::abs(throttleAxis.rangeMeter.liveValue - 87.5) < 0.01);
    assert(throttleAxis.dynamicRange.bandCount == 7);
    assert(throttleAxis.dynamicRange.markerCount == 3);
    for (std::uint32_t marker = 0;
         marker < throttleAxis.dynamicRange.markerCount; ++marker) {
        assert(throttleAxis.dynamicRange.markers[marker].controlId[0] == '\0');
    }

    AbsoluteControlTelemetry::AimSample aim;
    aim.values = {0.25F, -0.5F, 0.2F, -0.4F};
    aim.availableMask = 0xFU;
    AbsoluteControlTelemetry::PublishAim(aim);
    const auto aimFrame = Read(Channel("aim-response"));
    assert(aimFrame.telemetryPlot.seriesCount == 4);
    assert(aimFrame.telemetryPlot.availableMask == 0xFU);
    assert(std::abs(aimFrame.telemetryPlot.values[3] + 0.4) < 1e-6);
    assert(aimFrame.sequence > 0);
    assert(aimFrame.monotonicTimestampUs > 0);

    AbsoluteControlTelemetry::DeviceSample device;
    device.values = {-1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 0.25F, -0.25F, 0.75F};
    device.availableMask = 0x3FU;
    AbsoluteControlTelemetry::PublishDevice(device);
    const auto deviceFrame = Read(Channel("device-axes"));
    assert(deviceFrame.telemetryPlot.seriesCount == 8);
    assert(deviceFrame.telemetryPlot.availableMask == 0x3FU);
    assert(std::abs(deviceFrame.telemetryPlot.values[4] - 1.0) < 1e-6);

    AbsoluteControlTelemetry::MarkUnavailable();
    assert(!AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(logicalRaw));
    assert((Read(Channel("aim-response")).flags & Live::kFrameUnavailable) != 0);
    return 0;
}

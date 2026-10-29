#include "AbsoluteControlTelemetry.h"
#include "AxisShapingPolicy.h"

#include <array>
#include "TestCheck.h"
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
    REQUIRE(false);
    return g_channels[0];
}

Live::LiveFrameV1 Read(const Live::LiveChannelDescriptorV1& channel)
{
    Live::LiveFrameV1 frame;
    REQUIRE(channel.readLiveFrame(channel.context, &frame) == Live::Result::Ok);
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
    REQUIRE(AxisShapingPolicy::Shape(0.05F, 1.0F, 0.8F, 0.1F) == 0.0F);
    REQUIRE(std::abs(AxisShapingPolicy::Shape(0.45F, 1.0F, 0.8F, 0.1F) -
                    0.5F) < 1e-6F);
    REQUIRE(std::abs(AxisShapingPolicy::Shape(-0.45F, 1.0F, 0.8F, 0.1F) +
                    0.5F) < 1e-6F);
    REQUIRE(AxisShapingPolicy::Shape(0.8F, 2.0F, 0.8F, 0.1F) == 1.0F);

    Reset();
    Live::ExperimentalApiV1 invalid = g_api;
    invalid.structSize = Live::kExperimentalApiV1BaseSize - 1;
    REQUIRE(AbsoluteControlTelemetry::Register(&invalid) ==
           Live::Result::InvalidArgument);

    Reset();
    g_failAt = 3;
    REQUIRE(AbsoluteControlTelemetry::Register(&g_api) == Live::Result::Rejected);
    REQUIRE(g_unregisterCalls == 1);
    REQUIRE(!AbsoluteControlTelemetry::IsRegistered());

    Reset();
    auto legacyApi = g_api;
    legacyApi.capabilities = Live::kLiveCapabilityGridControlAssociations;
    REQUIRE(AbsoluteControlTelemetry::Register(&legacyApi) == Live::Result::Ok);
    REQUIRE(Channel("throttle-range").flags == Live::kSegmentedGridNone);
    REQUIRE(Channel("throttle-response").flags == Live::kSegmentedGridNone);

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
    REQUIRE(AbsoluteControlTelemetry::Register(&g_api) == Live::Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::IsRegistered());
    REQUIRE(g_registered == 12);

    constexpr std::array expectedPages{
        "hotas-flight-axes", "hotas-flight-axes", "hotas-flight-axes",
        "hotas-flight-axes", "hotas-flight-axes", "hotas-flight-axes",
        "hotas-flight-axes", "hotas-flight-axes", "hotas-throttle",
        "hotas-throttle", "hotas-aiming", "hotas-devices",
    };
    for (std::size_t index = 0; index < g_registered; ++index) {
        const auto& channel = g_channels[index];
        REQUIRE(std::strcmp(channel.moduleId, "absolute.hotas") == 0);
        REQUIRE(std::strcmp(channel.pageId, expectedPages[index]) == 0);
        REQUIRE(channel.readLiveFrame && channel.context);
        const std::string_view id = channel.channelId;
        REQUIRE(id.find("head") == std::string_view::npos);
        REQUIRE(id.find("hosam") == std::string_view::npos);
        REQUIRE(id.find("mouse") == std::string_view::npos);
        REQUIRE(id.find("power") == std::string_view::npos);
    }

    const auto& range = Channel("throttle-range");
    REQUIRE(range.kind == Live::ComponentKind::RangeMeter);
    REQUIRE((range.flags & Live::kLivePresentationPinned) != 0);
    REQUIRE(range.rangeMeter.minimumValue == 0.0);
    REQUIRE(range.rangeMeter.maximumValue == 100.0);
    REQUIRE(range.rangeMeter.bandCount == 7);
    REQUIRE(range.rangeMeter.markerCount == 3);
    REQUIRE(std::strcmp(range.rangeMeter.markers[0].controlId,
                       "throttle-detent-center") == 0);
    REQUIRE(range.rangeMeter.markers[0].value > 49.9 &&
           range.rangeMeter.markers[0].value < 50.1);
    const auto& response = Channel("throttle-response");
    REQUIRE((response.flags & Live::kLivePresentationSecondary) != 0);
    REQUIRE((response.flags &
        Live::kLivePresentationCollapsedByDefault) != 0);

    auto unavailable = Read(Channel("flight-rotation"));
    REQUIRE((unavailable.flags & Live::kFrameUnavailable) != 0);
    REQUIRE(unavailable.telemetryPlot.availableMask == 0);

    AbsoluteControlTelemetry::FlightSample flight;
    flight.values = {-0.5F, -0.25F, 0.4F, 0.3F, 0.2F, 0.1F,
                     -0.8F, -0.7F, 0.6F, 0.5F};
    flight.availableMask = 0x3FFU;
    AbsoluteControlTelemetry::PublishFlight(flight);
    const auto rotation = Read(Channel("flight-rotation"));
    REQUIRE(rotation.kind == Live::ComponentKind::TelemetryPlot);
    REQUIRE(rotation.telemetryPlot.seriesCount == 6);
    REQUIRE(rotation.telemetryPlot.availableMask == 0x3FU);
    REQUIRE(std::abs(rotation.telemetryPlot.values[0] + 0.5) < 1e-6);
    REQUIRE(std::abs(rotation.telemetryPlot.values[5] - 0.1) < 1e-6);
    const auto strafe = Read(Channel("flight-strafe"));
    REQUIRE(strafe.telemetryPlot.seriesCount == 4);
    REQUIRE(strafe.telemetryPlot.availableMask == 0xFU);
    REQUIRE(std::abs(strafe.telemetryPlot.values[0] + 0.8) < 1e-6);
    REQUIRE(std::abs(strafe.telemetryPlot.values[3] - 0.5) < 1e-6);
    const auto pitch = Read(Channel("axis-pitch"));
    REQUIRE(pitch.kind == Live::ComponentKind::RangeMeter);
    REQUIRE(pitch.rangeMeter.available == 1);
    REQUIRE(std::abs(pitch.rangeMeter.liveValue + 50.0) < 1e-6);
    REQUIRE(pitch.dynamicRange.bandCount == 5);
    REQUIRE(pitch.dynamicRange.markerCount == 5);
    REQUIRE(std::strcmp(pitch.dynamicRange.markers[0].controlId,
        "pitch-deadzone") == 0);
    REQUIRE(std::strcmp(pitch.dynamicRange.markers[1].controlId,
        "pitch-deadzone") == 0);
    REQUIRE(std::strcmp(pitch.dynamicRange.markers[2].controlId,
        "pitch-saturation") == 0);
    REQUIRE(std::strcmp(pitch.dynamicRange.markers[3].controlId,
        "pitch-saturation") == 0);

    AbsoluteControlTelemetry::AxisTuningPreviews axisPreviews{};
    for (auto& preview : axisPreviews) {
        preview.sensitivity = 1.0;
        preview.saturation = 1.0;
    }
    axisPreviews[1] = {true, 1.0, 0.8, 0.1};
    AbsoluteControlTelemetry::SetAxisTuningPreviews(axisPreviews);
    const auto pitchPreview = Read(Channel("axis-pitch"));
    REQUIRE(pitchPreview.sequence > pitch.sequence);
    REQUIRE(std::abs(pitchPreview.rangeMeter.liveValue - 50.0) < 1e-6);
    REQUIRE(std::abs(pitchPreview.dynamicRange.markers[1].value - 10.0) < 1e-6);
    REQUIRE(std::abs(pitchPreview.dynamicRange.markers[3].value - 80.0) < 1e-6);
    REQUIRE(std::abs(pitchPreview.dynamicRange.markers[4].value -
                    (4.0 / 7.0 * 100.0)) < 1e-4);

    AbsoluteControlTelemetry::ThrottleSample throttle;
    throttle.values = {0.75F, 0.5F, 0.45F, 0.0F, 1.0F};
    throttle.availableMask = 0x1FU;
    throttle.logicalRawPosition = 49152;
    throttle.logicalRawAvailable = true;
    AbsoluteControlTelemetry::PublishThrottle(throttle);
    std::int64_t logicalRaw{};
    REQUIRE(AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(logicalRaw));
    REQUIRE(logicalRaw == 49152);
    const auto rangeFrame = Read(range);
    REQUIRE(rangeFrame.rangeMeter.available == 1);
    REQUIRE(std::abs(rangeFrame.rangeMeter.liveValue - 75.001) < 0.01);
    REQUIRE(rangeFrame.dynamicRange.present == 1);
    REQUIRE(rangeFrame.dynamicRange.bandCount == 7);
    REQUIRE(rangeFrame.dynamicRange.markerCount == 3);

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
    REQUIRE(previewFrame.sequence > rangeFrame.sequence);
    REQUIRE(previewFrame.dynamicRange.markers[1].value > 15.2 &&
           previewFrame.dynamicRange.markers[1].value < 15.3);

    // During set-by-feel, the selected landmark follows the live logical raw
    // throttle while the draft widths continue to shape the same range frame.
    AbsoluteControlTelemetry::SetThrottleCaptureTarget(
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Reverse);
    throttle.logicalRawPosition = 20000;
    AbsoluteControlTelemetry::PublishThrottle(throttle);
    const auto trackingFrame = Read(range);
    REQUIRE(trackingFrame.dynamicRange.markers[1].value > 30.5 &&
           trackingFrame.dynamicRange.markers[1].value < 30.6);
    REQUIRE(trackingFrame.dynamicRange.markers[1].visualRole ==
           Live::VisualRole::Live);
    AbsoluteControlTelemetry::SetThrottleCaptureTarget(
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    const auto throttleFrame = Read(response);
    REQUIRE(throttleFrame.telemetryPlot.seriesCount == 5);
    REQUIRE(throttleFrame.telemetryPlot.availableMask == 0x1FU);
    REQUIRE(std::abs(throttleFrame.telemetryPlot.values[4] - 1.0) < 1e-6);
    const auto throttleAxis = Read(Channel("axis-throttle"));
    REQUIRE(throttleAxis.kind == Live::ComponentKind::RangeMeter);
    REQUIRE(throttleAxis.rangeMeter.available == 1);
    REQUIRE(std::abs(throttleAxis.rangeMeter.liveValue - 87.5) < 0.01);
    REQUIRE(throttleAxis.dynamicRange.bandCount == 7);
    REQUIRE(throttleAxis.dynamicRange.markerCount == 3);
    for (std::uint32_t marker = 0;
         marker < throttleAxis.dynamicRange.markerCount; ++marker) {
        REQUIRE(throttleAxis.dynamicRange.markers[marker].controlId[0] == '\0');
    }

    AbsoluteControlTelemetry::AimSample aim;
    aim.values = {0.25F, -0.5F, 0.2F, -0.4F};
    aim.availableMask = 0xFU;
    AbsoluteControlTelemetry::PublishAim(aim);
    const auto aimFrame = Read(Channel("aim-response"));
    REQUIRE(aimFrame.telemetryPlot.seriesCount == 4);
    REQUIRE(aimFrame.telemetryPlot.availableMask == 0xFU);
    REQUIRE(std::abs(aimFrame.telemetryPlot.values[3] + 0.4) < 1e-6);
    REQUIRE(aimFrame.sequence > 0);
    REQUIRE(aimFrame.monotonicTimestampUs > 0);

    AbsoluteControlTelemetry::DeviceSample device;
    device.values = {-1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 0.25F, -0.25F, 0.75F};
    device.availableMask = 0x3FU;
    AbsoluteControlTelemetry::PublishDevice(device);
    const auto deviceFrame = Read(Channel("device-axes"));
    REQUIRE(deviceFrame.telemetryPlot.seriesCount == 8);
    REQUIRE(deviceFrame.telemetryPlot.availableMask == 0x3FU);
    REQUIRE(std::abs(deviceFrame.telemetryPlot.values[4] - 1.0) < 1e-6);

    AbsoluteControlTelemetry::MarkUnavailable();
    REQUIRE(!AbsoluteControlTelemetry::ReadPrimaryThrottleRaw(logicalRaw));
    REQUIRE((Read(Channel("aim-response")).flags & Live::kFrameUnavailable) != 0);
    return 0;
}

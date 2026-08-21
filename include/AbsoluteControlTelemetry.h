#pragma once

#include "LiveComponentsExperimentalAPI.h"

#include <array>
#include <cstdint>

namespace AbsoluteControlTelemetry {

inline constexpr std::size_t kFlightValueCount = 10;
inline constexpr std::size_t kThrottleValueCount = 5;
inline constexpr std::size_t kAimValueCount = 4;
inline constexpr std::size_t kDeviceValueCount = 8;
inline constexpr std::size_t kAxisPreviewCount = 6;

struct FlightSample {
    // Physical calibrated inputs (before inversion) followed by the shaped
    // values used by the runtime:
    // pitch, yaw, roll, lateral strafe, vertical strafe.
    std::array<float, kFlightValueCount> values{};
    std::uint32_t availableMask{};
};

struct AxisTuningPreview {
    bool inverted{};
    double sensitivity{1.0};
    double saturation{1.0};
    double deadzone{};
};

using AxisTuningPreviews =
    std::array<AxisTuningPreview, kAxisPreviewCount>;

struct ThrottleSample {
    // Calibrated physical input, logical request, runtime target, reverse active,
    // and boost active. All but the two state lanes use normalized coordinates.
    std::array<float, kThrottleValueCount> values{};
    std::uint32_t availableMask{};
    // Logical raw primary-throttle position after configured inversion. This is
    // the same 0..65535 coordinate used by detent/reverse/boost landmarks.
    std::int64_t logicalRawPosition{};
    bool logicalRawAvailable{};
};

struct AimSample {
    // Prepared input yaw/pitch and final normalized output yaw/pitch.
    std::array<float, kAimValueCount> values{};
    std::uint32_t availableMask{};
};

struct DeviceSample {
    // Calibrated normalized DirectInput axes 0x30..0x37 for the device selected
    // on Devices & Calibration. Unavailable/nonexistent lanes are masked out.
    std::array<float, kDeviceValueCount> values{};
    std::uint32_t availableMask{};
};

struct ThrottleLayout {
    std::int64_t axisMinimum{};
    std::int64_t axisMaximum{65535};
    bool inverted{};
    bool reverseZoneEnabled{};
    bool boostZoneEnabled{};
    double idlePlateau{0.05};
    double saturation{1.0};
    std::int64_t detentCenter{32768};
    std::int64_t detentDeadzone{500};
    std::int64_t reverseZoneCenter{3000};
    std::int64_t reverseZoneDeadzone{3000};
    std::int64_t boostZoneCenter{62000};
    std::int64_t boostZoneDeadzone{2000};
};

struct ThrottlePreview {
    bool inverted{};
    bool reverseZoneEnabled{};
    bool boostZoneEnabled{};
    double idlePlateau{0.05};
    double saturation{1.0};
    std::int64_t detentCenter{32768};
    std::int64_t detentDeadzone{500};
    std::int64_t reverseZoneCenter{3000};
    std::int64_t reverseZoneDeadzone{3000};
    std::int64_t boostZoneCenter{62000};
    std::int64_t boostZoneDeadzone{2000};
};

enum class ThrottleCaptureTarget : std::uint8_t {
    None,
    Detent,
    Reverse,
    Boost,
};

// Controller-thread publication only. These functions perform bounded atomic
// stores and never call the host.
void PublishFlight(const FlightSample& sample) noexcept;
void PublishThrottle(const ThrottleSample& sample) noexcept;
[[nodiscard]] bool ReadPrimaryThrottleRaw(
    std::int64_t& logicalRawPosition) noexcept;
void PublishAim(const AimSample& sample) noexcept;
void PublishDevice(const DeviceSample& sample) noexcept;
void SetThrottleLayout(const ThrottleLayout& layout) noexcept;
// Provider-draft publication. Calibration bounds remain controller-owned while
// every tuning field below them can preview before Apply.
void SetThrottlePreview(const ThrottlePreview& preview) noexcept;
// Draft shaping for Throttle, Pitch, Yaw, Roll, Lateral, and Vertical. Axis
// range meters combine this state with controller-thread physical samples so
// inversion/deadzone/saturation preview before Apply.
void SetAxisTuningPreviews(const AxisTuningPreviews& previews) noexcept;
void SetThrottleCaptureTarget(ThrottleCaptureTarget target) noexcept;
[[nodiscard]] ThrottleCaptureTarget GetThrottleCaptureTarget() noexcept;
void MarkUnavailable() noexcept;

// Registration is fail-optional and independent from stable page registration.
[[nodiscard]] AbsoluteControlPanelExperimental::Result Register(
    const AbsoluteControlPanelExperimental::ExperimentalApiV1* api) noexcept;
[[nodiscard]] bool IsRegistered() noexcept;

namespace Testing {
[[nodiscard]] const AbsoluteControlPanelExperimental::LiveChannelDescriptorV1*
Channels(std::size_t& count) noexcept;
void Reset() noexcept;
} // namespace Testing

} // namespace AbsoluteControlTelemetry

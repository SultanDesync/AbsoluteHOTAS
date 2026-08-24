#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace AbsoluteControlSettings {

enum class ScalarType : std::uint8_t { Boolean, Integer, Float, Choice };

enum class ScalarField : std::uint16_t {
    FlightControlsEnabled,
    ThrottleInvert, ThrottleSensitivity, ThrottleSaturation, ThrottleDeadzone,
    PitchInvert, PitchSensitivity, PitchSaturation, PitchDeadzone,
    YawInvert, YawSensitivity, YawSaturation, YawDeadzone,
    RollInvert, RollSensitivity, RollSaturation, RollDeadzone,
    StrafeLateralInvert, StrafeLateralSensitivity, StrafeLateralSaturation,
    StrafeLateralDeadzone,
    StrafeVerticalInvert, StrafeVerticalSaturation, StrafeVerticalDeadzone,
    ReverseInvert, ReverseSensitivity, ReverseSaturation,
    DigitalRollStrength, DigitalStrafeStrength,
    IdlePlateau, DetentCenter, DetentDeadzone,
    ReverseZoneEnabled, ReverseZoneCenter, ReverseZoneDeadzone,
    BoostZoneEnabled, BoostZoneCenter, BoostZoneDeadzone,
    RateThrottleEnabled, AccumulatorRate, AccumulatorDecay, ReverseGateVelocity,
    TurnAssistEnabled, TurnAssistMode, HoldForBoost,
    AimEnabled, AimSensitivity, AimDeadzone, AimSmoothing,
    AimYawInvert, AimYawSensitivity, AimPitchInvert, AimPitchSensitivity,
    DigitalAimSpeed,
    MenuUsePitch, MenuUseYaw, MenuUsePrimaryWeapon,
    MenuInvertVertical, MenuInvertHorizontal, MenuEngageThreshold, MenuReleaseThreshold,
    PilotContextMode, AutomaticPilotDetection, PilotLatchMilliseconds,
    Count
};

enum class StorageFormat : std::uint8_t {
    Boolean, Integer, Float, PilotContextMode, PilotSignal, HoldForBoostAlias
};

struct ScalarDefinition {
    ScalarField field{};
    std::string_view controlId;
    std::string_view section;
    std::string_view key;
    ScalarType type{ScalarType::Boolean};
    StorageFormat storage{StorageFormat::Boolean};
    double minimum{};
    double maximum{};
    double step{};
    double defaultValue{};
};

struct ScalarValue {
    bool booleanValue{};
    std::int64_t integerValue{};
    double floatValue{};
};

inline constexpr std::size_t kScalarFieldCount =
    static_cast<std::size_t>(ScalarField::Count);

struct ScalarState {
    std::array<ScalarValue, kScalarFieldCount> values{};
};

[[nodiscard]] std::span<const ScalarDefinition> Definitions() noexcept;
[[nodiscard]] const ScalarDefinition* FindDefinition(std::string_view controlId) noexcept;
[[nodiscard]] const ScalarDefinition& Definition(ScalarField field) noexcept;
[[nodiscard]] ScalarState DefaultState() noexcept;

[[nodiscard]] bool GetBoolean(const ScalarState& state, ScalarField field) noexcept;
[[nodiscard]] std::int64_t GetInteger(const ScalarState& state, ScalarField field) noexcept;
[[nodiscard]] double GetFloat(const ScalarState& state, ScalarField field) noexcept;
void SetBoolean(ScalarState& state, ScalarField field, bool value) noexcept;
void SetInteger(ScalarState& state, ScalarField field, std::int64_t value) noexcept;
void SetFloat(ScalarState& state, ScalarField field, double value) noexcept;

[[nodiscard]] bool Validate(const ScalarState& state, std::string& error) noexcept;
[[nodiscard]] bool Equivalent(const ScalarState& left, const ScalarState& right) noexcept;

} // namespace AbsoluteControlSettings

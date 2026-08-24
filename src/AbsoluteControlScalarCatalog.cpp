#include "AbsoluteControlScalarCatalog.h"

#include <algorithm>
#include <cmath>

namespace AbsoluteControlSettings {
namespace {

using Field = ScalarField;
using Type = ScalarType;
using Format = StorageFormat;

constexpr ScalarDefinition Bool(Field field, std::string_view id,
    std::string_view section, std::string_view key, bool defaultValue,
    Format storage = Format::Boolean) noexcept
{
    return {field, id, section, key, Type::Boolean, storage,
        0.0, 1.0, 1.0, defaultValue ? 1.0 : 0.0};
}

constexpr ScalarDefinition Float(Field field, std::string_view id,
    std::string_view section, std::string_view key, double minimum,
    double maximum, double step, double defaultValue) noexcept
{
    return {field, id, section, key, Type::Float, Format::Float,
        minimum, maximum, step, defaultValue};
}

constexpr ScalarDefinition Integer(Field field, std::string_view id,
    std::string_view section, std::string_view key, double minimum,
    double maximum, double step, double defaultValue,
    Type type = Type::Integer, Format storage = Format::Integer) noexcept
{
    return {field, id, section, key, type, storage,
        minimum, maximum, step, defaultValue};
}

constexpr std::array kDefinitions{
    Bool(Field::FlightControlsEnabled, "flight-controls-enabled", "Injection", "bEnableInjection", true),
    Bool(Field::ThrottleInvert, "throttle-inverted", "Hardware", "bInvertThrottle", false),
    Float(Field::ThrottleSensitivity, "throttle-sensitivity", "Hardware", "fThrottleSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::ThrottleSaturation, "throttle-saturation", "Hardware", "fThrottleSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::ThrottleDeadzone, "throttle-deadzone", "Hardware", "fThrottleDeadzone", 0.0, 0.95, 0.01, 0.0),
    Bool(Field::PitchInvert, "pitch-inverted", "Hardware", "bInvertPitch", false),
    Float(Field::PitchSensitivity, "pitch-sensitivity", "Hardware", "fPitchSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::PitchSaturation, "pitch-saturation", "Hardware", "fPitchSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::PitchDeadzone, "pitch-deadzone", "Hardware", "fPitchDeadzone", 0.0, 0.95, 0.01, 0.0),
    Bool(Field::YawInvert, "yaw-inverted", "Hardware", "bInvertYaw", false),
    Float(Field::YawSensitivity, "yaw-sensitivity", "Hardware", "fYawSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::YawSaturation, "yaw-saturation", "Hardware", "fYawSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::YawDeadzone, "yaw-deadzone", "Hardware", "fYawDeadzone", 0.0, 0.95, 0.01, 0.0),
    Bool(Field::RollInvert, "roll-inverted", "Hardware", "bInvertRoll", false),
    Float(Field::RollSensitivity, "roll-sensitivity", "Hardware", "fRollSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::RollSaturation, "roll-saturation", "Hardware", "fRollSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::RollDeadzone, "roll-deadzone", "Hardware", "fRollDeadzone", 0.0, 0.95, 0.01, 0.0),
    Bool(Field::StrafeLateralInvert, "strafe-lateral-inverted", "Hardware", "bInvertStrafeLat", false),
    Float(Field::StrafeLateralSensitivity, "strafe-lateral-sensitivity", "Hardware", "fStrafeSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::StrafeLateralSaturation, "strafe-lateral-saturation", "Hardware", "fStrafeSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::StrafeLateralDeadzone, "strafe-lateral-deadzone", "Hardware", "fStrafeDeadzone", 0.0, 0.95, 0.01, 0.05),
    Bool(Field::StrafeVerticalInvert, "strafe-vertical-inverted", "Hardware", "bInvertStrafeVert", false),
    Float(Field::StrafeVerticalSaturation, "strafe-vertical-saturation", "Hardware", "fStrafeVertSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::StrafeVerticalDeadzone, "strafe-vertical-deadzone", "Hardware", "fStrafeVertDeadzone", 0.0, 0.95, 0.01, 0.05),
    Bool(Field::ReverseInvert, "reverse-axis-inverted", "Hardware", "bInvertReverse", false),
    Float(Field::ReverseSensitivity, "reverse-axis-sensitivity", "Hardware", "fReverseSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::ReverseSaturation, "reverse-axis-saturation", "Hardware", "fReverseSaturation", 0.05, 1.0, 0.05, 1.0),
    Float(Field::DigitalRollStrength, "digital-roll-strength", "DigitalAxes", "fDigitalRollValue", 0.0, 1.0, 0.05, 1.0),
    Float(Field::DigitalStrafeStrength, "digital-strafe-strength", "DigitalAxes", "fDigitalStrafeValue", 0.0, 1.0, 0.05, 1.0),
    Float(Field::IdlePlateau, "throttle-idle-plateau", "Normalization", "fIdlePlateau", 0.0, 0.2, 0.01, 0.05),
    Integer(Field::DetentCenter, "throttle-detent-center", "Normalization", "iDetentCenter", 0, 65535, 100, 32768),
    Integer(Field::DetentDeadzone, "throttle-detent-width", "Normalization", "iDetentDeadzone", 0, 6554, 100, 500),
    Bool(Field::ReverseZoneEnabled, "reverse-zone-enabled", "Normalization", "bUnipolarReverse", false),
    Integer(Field::ReverseZoneCenter, "reverse-zone-center", "Normalization", "iReverseZoneCenter", 0, 65535, 100, 3000),
    Integer(Field::ReverseZoneDeadzone, "reverse-zone-width", "Normalization", "iReverseZoneDeadzone", 0, 9830, 100, 3000),
    Bool(Field::BoostZoneEnabled, "boost-zone-enabled", "Normalization", "bBoostZone", false),
    Integer(Field::BoostZoneCenter, "boost-zone-center", "Normalization", "iBoostZoneCenter", 0, 65535, 100, 62000),
    Integer(Field::BoostZoneDeadzone, "boost-zone-width", "Normalization", "iBoostZoneDeadzone", 0, 9830, 100, 2000),
    Bool(Field::RateThrottleEnabled, "rate-throttle-enabled", "DualStick", "bAccumulatorThrottle", false),
    Float(Field::AccumulatorRate, "rate-throttle-ramp", "DualStick", "fAccumulatorRate", 0.1, 5.0, 0.1, 1.0),
    Float(Field::AccumulatorDecay, "rate-throttle-decay", "DualStick", "fAccumulatorDecay", 0.0, 3.0, 0.1, 0.0),
    Float(Field::ReverseGateVelocity, "reverse-velocity-gate", "DualStick", "fReverseGateVelocity", 0.0, 50.0, 1.0, 5.0),
    Bool(Field::TurnAssistEnabled, "turn-assist-enabled", "DualStick", "bAccumulatorTurnAssist", false),
    Integer(Field::TurnAssistMode, "turn-assist-mode", "DualStick", "iTurnAssistMode", 0, 2, 1, 0, Type::Choice),
    Bool(Field::HoldForBoost, "boost-throttle-authority", "DualStick", "bHoldForBoost", true, Format::HoldForBoostAlias),
    Bool(Field::AimEnabled, "aim-enabled", "Aim", "bSourceObjectAim", true),
    Float(Field::AimSensitivity, "aim-steering-sensitivity", "Aim", "fAimSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::AimDeadzone, "aim-deadzone", "Aim", "fAimDeadzone", 0.0, 0.5, 0.01, 0.04),
    Float(Field::AimSmoothing, "aim-smoothing", "Aim", "fAimSmoothing", 0.0, 0.98, 0.01, 0.0),
    Bool(Field::AimYawInvert, "aim-yaw-inverted", "Aim", "bInvertAimYaw", false),
    Float(Field::AimYawSensitivity, "aim-yaw-sensitivity", "Aim", "fAimYawSensitivity", 0.1, 3.0, 0.05, 1.0),
    Bool(Field::AimPitchInvert, "aim-pitch-inverted", "Aim", "bInvertAimPitch", false),
    Float(Field::AimPitchSensitivity, "aim-pitch-sensitivity", "Aim", "fAimPitchSensitivity", 0.1, 3.0, 0.05, 1.0),
    Float(Field::DigitalAimSpeed, "digital-aim-speed", "Aim", "fDigitalAimValue", 0.1, 3.0, 0.05, 1.0),
    Bool(Field::MenuUsePitch, "menu-use-pitch", "MenuControls", "bUsePitchAxisForNavigation", false),
    Bool(Field::MenuUseYaw, "menu-use-yaw", "MenuControls", "bUseYawAxisForNavigation", false),
    Bool(Field::MenuUsePrimaryWeapon, "menu-use-primary-weapon", "MenuControls", "bUsePrimaryWeaponForSelect", false),
    Bool(Field::MenuInvertVertical, "menu-invert-vertical", "MenuControls", "bInvertVerticalNavigation", false),
    Bool(Field::MenuInvertHorizontal, "menu-invert-horizontal", "MenuControls", "bInvertHorizontalNavigation", false),
    Float(Field::MenuEngageThreshold, "menu-engage-threshold", "MenuControls", "fAxisEngageThreshold", 0.35, 0.95, 0.05, 0.55),
    Float(Field::MenuReleaseThreshold, "menu-release-threshold", "MenuControls", "fAxisReleaseThreshold", 0.05, 0.8, 0.05, 0.35),
    Integer(Field::PilotContextMode, "pilot-context-mode", "Gate", "PilotGateMode", 0, 2, 1, 1, Type::Choice, Format::PilotContextMode),
    Bool(Field::AutomaticPilotDetection, "automatic-pilot-detection", "Gate", "PilotSignal", true, Format::PilotSignal),
    Integer(Field::PilotLatchMilliseconds, "pilot-latch-ms", "Gate", "iPilotLatchMilliseconds", 500, 30000, 500, 5000),
};

static_assert(kDefinitions.size() == kScalarFieldCount);

constexpr std::size_t Index(ScalarField field) noexcept
{
    return static_cast<std::size_t>(field);
}

} // namespace

std::span<const ScalarDefinition> Definitions() noexcept { return kDefinitions; }

const ScalarDefinition* FindDefinition(std::string_view controlId) noexcept
{
    const auto found = std::ranges::find_if(kDefinitions,
        [&](const ScalarDefinition& definition) {
            return definition.controlId == controlId;
        });
    return found == kDefinitions.end() ? nullptr : &*found;
}

const ScalarDefinition& Definition(ScalarField field) noexcept
{
    return kDefinitions[Index(field)];
}

ScalarState DefaultState() noexcept
{
    ScalarState state;
    for (const auto& definition : kDefinitions) {
        auto& value = state.values[Index(definition.field)];
        if (definition.type == ScalarType::Boolean) {
            value.booleanValue = definition.defaultValue != 0.0;
        } else if (definition.type == ScalarType::Integer ||
                   definition.type == ScalarType::Choice) {
            value.integerValue = static_cast<std::int64_t>(definition.defaultValue);
        } else {
            value.floatValue = definition.defaultValue;
        }
    }
    return state;
}

bool GetBoolean(const ScalarState& state, ScalarField field) noexcept
{
    return state.values[Index(field)].booleanValue;
}

std::int64_t GetInteger(const ScalarState& state, ScalarField field) noexcept
{
    return state.values[Index(field)].integerValue;
}

double GetFloat(const ScalarState& state, ScalarField field) noexcept
{
    return state.values[Index(field)].floatValue;
}

void SetBoolean(ScalarState& state, ScalarField field, bool value) noexcept
{
    state.values[Index(field)].booleanValue = value;
}

void SetInteger(ScalarState& state, ScalarField field, std::int64_t value) noexcept
{
    state.values[Index(field)].integerValue = value;
}

void SetFloat(ScalarState& state, ScalarField field, double value) noexcept
{
    state.values[Index(field)].floatValue = value;
}

bool Validate(const ScalarState& state, std::string& error) noexcept
{
    for (const auto& definition : kDefinitions) {
        double value{};
        if (definition.type == ScalarType::Boolean) continue;
        if (definition.type == ScalarType::Integer ||
            definition.type == ScalarType::Choice) {
            value = static_cast<double>(GetInteger(state, definition.field));
        } else {
            value = GetFloat(state, definition.field);
        }
        if (!std::isfinite(value) || value < definition.minimum ||
            value > definition.maximum) {
            error = std::string(definition.controlId) + " is outside its supported range.";
            return false;
        }
    }
    const auto engage = GetFloat(state, Field::MenuEngageThreshold);
    const auto release = GetFloat(state, Field::MenuReleaseThreshold);
    if (release > engage - 0.05 + 1e-9) {
        error = "Menu release threshold must remain at least 0.05 below engage.";
        return false;
    }
    error.clear();
    return true;
}

bool Equivalent(const ScalarState& left, const ScalarState& right) noexcept
{
    for (const auto& definition : kDefinitions) {
        if (definition.type == ScalarType::Boolean) {
            if (GetBoolean(left, definition.field) !=
                GetBoolean(right, definition.field)) return false;
        } else if (definition.type == ScalarType::Integer ||
                   definition.type == ScalarType::Choice) {
            if (GetInteger(left, definition.field) !=
                GetInteger(right, definition.field)) return false;
        } else {
            const auto tolerance = (std::max)(1e-6, definition.step * 0.01);
            if (std::abs(GetFloat(left, definition.field) -
                         GetFloat(right, definition.field)) > tolerance) return false;
        }
    }
    return true;
}

} // namespace AbsoluteControlSettings

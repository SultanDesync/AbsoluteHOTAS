#pragma once

#include "AbsoluteControlSettings.h"

#include <cstdint>
#include <optional>
#include <string>

namespace AbsoluteControlThrottleActions {

enum class Action : std::uint8_t {
    CaptureDetent,
    CaptureReverse,
    CaptureBoost,
    LinkIdleAndSaturation,
};

enum class Result : std::uint8_t { Applied, InputUnavailable, InvalidDraft };

inline Result Apply(AbsoluteControlSettings::ScalarState& state, Action action,
                    std::optional<std::int64_t> logicalRaw,
                    std::string& error) noexcept
{
    using AbsoluteControlSettings::ScalarField;
    auto candidate = state;
    switch (action) {
    case Action::CaptureDetent:
    case Action::CaptureReverse:
    case Action::CaptureBoost: {
        if (!logicalRaw) {
            error = "The primary throttle axis has no current DirectInput sample.";
            return Result::InputUnavailable;
        }
        const auto field = action == Action::CaptureDetent
            ? ScalarField::DetentCenter
            : action == Action::CaptureReverse
                ? ScalarField::ReverseZoneCenter
                : ScalarField::BoostZoneCenter;
        AbsoluteControlSettings::SetInteger(candidate, field, *logicalRaw);
        break;
    }
    case Action::LinkIdleAndSaturation:
        // This is deliberately a one-shot draft mutation, not the legacy
        // presentation checkbox. Later edits remain independent.
        AbsoluteControlSettings::SetFloat(candidate,
            ScalarField::ThrottleSaturation,
            1.0 - AbsoluteControlSettings::GetFloat(
                candidate, ScalarField::IdlePlateau));
        break;
    }
    if (!AbsoluteControlSettings::Validate(candidate, error)) {
        return Result::InvalidDraft;
    }
    state = candidate;
    error.clear();
    return Result::Applied;
}

} // namespace AbsoluteControlThrottleActions

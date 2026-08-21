#pragma once

#include <string_view>

namespace AimInputPolicy {

struct Presence {
    bool analog = false;
    bool digital = false;

    [[nodiscard]] constexpr bool HasSeparateInput() const noexcept
    {
        return analog || digital;
    }
};

[[nodiscard]] constexpr Presence Detect(
    bool aimYawAxisBound, bool aimPitchAxisBound,
    bool digitalAimLeftBound, bool digitalAimRightBound,
    bool digitalAimUpBound, bool digitalAimDownBound) noexcept
{
    return {
        aimYawAxisBound || aimPitchAxisBound,
        digitalAimLeftBound || digitalAimRightBound ||
            digitalAimUpBound || digitalAimDownBound,
    };
}

[[nodiscard]] constexpr bool IsConfiguredBinding(std::string_view binding) noexcept
{
    return !binding.empty() && binding != "(unbound)" && binding != "-1";
}

} // namespace AimInputPolicy

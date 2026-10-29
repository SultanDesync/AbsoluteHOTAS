#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace InputRecoveryPolicy {

struct Axis {
    bool armed{true};

    void Lost() noexcept { armed = false; }
    bool Observe(bool available, bool neutral) noexcept
    {
        if (!available) Lost();
        else if (neutral) armed = true;
        return available && armed;
    }
    std::optional<long> Read(std::optional<long> sample) const noexcept
    {
        return armed ? sample : std::nullopt;
    }
};

inline bool CenteredNeutral(long raw, float low, float high, float deadzone) noexcept
{
    return high > low &&
        std::abs(static_cast<float>(raw) - (low + high) * 0.5F) <=
            (high - low) * 0.5F * (std::max)(deadzone, 0.05F);
}

struct ThrottleNeutralZone {
    long minimum{}, maximum{65535}, center{32768}, deadzone{2000};
    long reverseCenter{}, reverseDeadzone{2000};
    float idlePlateau{};
    bool inverted{}, rate{}, unipolar{true}, reverseZone{};

    bool Contains(long raw) const noexcept
    {
        if (maximum <= minimum) return false;
        if (inverted) raw = minimum + maximum - raw;
        if (rate || !unipolar) return std::abs(raw - center) <= deadzone;
        if (reverseZone) return std::abs(raw - reverseCenter) <= reverseDeadzone;
        return static_cast<float>(raw - minimum) / static_cast<float>(maximum - minimum)
            <= (std::max)(idlePlateau, 0.03F);
    }
};

struct Button {
    bool armed{true};

    void Lost() noexcept { armed = false; }
    bool Observe(bool down) noexcept
    {
        if (!down) armed = true;
        return armed && down;
    }
};

} // namespace InputRecoveryPolicy

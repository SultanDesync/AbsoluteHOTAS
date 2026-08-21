#pragma once

#include <algorithm>
#include <cmath>

namespace AxisShapingPolicy {

inline float Shape(float normalized, float sensitivity,
                   float saturation, float deadzone) noexcept
{
    const auto dead = std::clamp(deadzone, 0.0F, 0.95F);
    const auto saturated = std::clamp(saturation, 0.05F, 1.0F);
    const auto magnitude = std::abs(normalized);
    if (magnitude <= dead) return 0.0F;
    const auto ramp = std::clamp(
        (magnitude - dead) / (std::max)(saturated - dead, 1e-4F),
        0.0F, 1.0F);
    return std::clamp(std::copysign(ramp * sensitivity, normalized),
                      -1.0F, 1.0F);
}

} // namespace AxisShapingPolicy

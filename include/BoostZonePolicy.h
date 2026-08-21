#pragma once

namespace BoostZonePolicy {

// Boost is an independently enabled throttle-zone feature. Reverse-zone
// enablement is intentionally absent from this decision.
[[nodiscard]] constexpr bool ShouldActivate(
    bool flightInjectionAllowed, bool boostZoneEnabled,
    bool unipolarThrottle, bool throttleBound,
    long logicalRawThrottle, long boostZoneCenter,
    long boostZoneDeadzone) noexcept
{
    return flightInjectionAllowed && boostZoneEnabled && unipolarThrottle &&
        throttleBound &&
        logicalRawThrottle > boostZoneCenter + boostZoneDeadzone;
}

} // namespace BoostZonePolicy

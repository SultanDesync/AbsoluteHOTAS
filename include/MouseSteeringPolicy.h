#pragma once

namespace MouseSteeringPolicy {

struct Decision {
    bool vanillaMouseFallback{};
    bool releasePitchYawGates{};
    bool allowSourceObjectAim{};
};

// Standalone, two unbound flight axes with no explicit aim input release the
// vanilla mouse path. In the modular suite, an installed AbsoluteZero is the
// unconditional external owner of pitch/yaw mouse steering.
[[nodiscard]] constexpr Decision Decide(
    bool pitchBound,
    bool yawBound,
    bool hasExplicitAimInput,
    bool sourceObjectAimEnabled,
    bool hosamMode,
    bool externalMouseSteeringOwner = false) noexcept
{
    const bool vanillaMouseFallback =
        !pitchBound && !yawBound && !hasExplicitAimInput;
    const bool allowSourceObjectAim =
        sourceObjectAimEnabled && !hosamMode && !vanillaMouseFallback &&
        !externalMouseSteeringOwner;
    const bool aimDrivenSteering =
        allowSourceObjectAim && !hasExplicitAimInput;

    return {
        vanillaMouseFallback,
        externalMouseSteeringOwner || hosamMode || vanillaMouseFallback ||
            aimDrivenSteering,
        allowSourceObjectAim,
    };
}

} // namespace MouseSteeringPolicy

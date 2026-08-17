#include "MouseSteeringPolicy.h"
#include "AbsoluteMouseSteeringAPI.h"
#include "AbsoluteCameraOwnershipAPI.h"

#include <cassert>
#include <cstddef>

int main()
{
    static_assert(sizeof(AbsoluteMouseSteeringApi::ApiV1) == 56);
    static_assert(offsetof(AbsoluteMouseSteeringApi::ApiV1, readMouseAccumulator) == 16);
    static_assert(offsetof(AbsoluteMouseSteeringApi::ApiV1, declareAbsoluteZeroOwner) == 40);
    static_assert(offsetof(AbsoluteMouseSteeringApi::ApiV1, absoluteZeroOwnsPitchYaw) == 48);
    static_assert(sizeof(AbsoluteCameraOwnershipApi::ApiV1) == 48);
    static_assert(offsetof(AbsoluteCameraOwnershipApi::ApiV1,
                           declareAbsoluteHeadTrackingOwner) == 16);
    static_assert(offsetof(AbsoluteCameraOwnershipApi::ApiV1,
                           cockpitSignalAgeMilliseconds) == 40);

    using MouseSteeringPolicy::Decide;

    // Regression: the shipped/source-aim defaults must not turn two unbound
    // flight axes into authoritative zero writes.
    {
        constexpr auto result = Decide(false, false, false, true, false);
        static_assert(result.vanillaMouseFallback);
        static_assert(result.releasePitchYawGates);
        static_assert(!result.allowSourceObjectAim);
    }

    // The fallback is independent of the legacy source-aim switch.
    {
        constexpr auto result = Decide(false, false, false, false, false);
        static_assert(result.vanillaMouseFallback);
        static_assert(result.releasePitchYawGates);
        static_assert(!result.allowSourceObjectAim);
    }

    // An explicit aim input is outside this narrow fallback policy.
    {
        constexpr auto result = Decide(false, false, true, true, false);
        static_assert(!result.vanillaMouseFallback);
        static_assert(!result.releasePitchYawGates);
        static_assert(result.allowSourceObjectAim);
    }

    // Partially and fully bound steering retain current behavior pending the
    // separate mixed-mode ownership decision.
    {
        constexpr auto partial = Decide(true, false, false, false, false);
        constexpr auto complete = Decide(true, true, false, false, false);
        static_assert(!partial.vanillaMouseFallback);
        static_assert(!partial.releasePitchYawGates);
        static_assert(!complete.vanillaMouseFallback);
        static_assert(!complete.releasePitchYawGates);
    }

    // Existing explicit HOSAM semantics remain unchanged.
    {
        constexpr auto result = Decide(true, true, false, true, true);
        static_assert(!result.vanillaMouseFallback);
        static_assert(result.releasePitchYawGates);
        static_assert(!result.allowSourceObjectAim);
    }

    // Existing aim-driven steering retains source aim and released cluster gates.
    {
        constexpr auto result = Decide(true, true, false, true, false);
        static_assert(!result.vanillaMouseFallback);
        static_assert(result.releasePitchYawGates);
        static_assert(result.allowSourceObjectAim);
    }

    // Suite ownership: AbsoluteZero wins pitch/yaw even when HOTAS steering
    // axes and the legacy source-aim path are configured.
    {
        constexpr auto result = Decide(true, true, true, true, false, true);
        static_assert(!result.vanillaMouseFallback);
        static_assert(result.releasePitchYawGates);
        static_assert(!result.allowSourceObjectAim);
    }

    assert(true);
    return 0;
}

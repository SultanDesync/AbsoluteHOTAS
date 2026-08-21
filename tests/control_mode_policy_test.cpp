#include "AimInputPolicy.h"
#include "BoostZonePolicy.h"

#include <cassert>

int main()
{
    using AimInputPolicy::Detect;

    {
        constexpr auto none = Detect(false, false, false, false, false, false);
        static_assert(!none.analog);
        static_assert(!none.digital);
        static_assert(!none.HasSeparateInput());
    }

    {
        constexpr auto analog = Detect(true, false, false, false, false, false);
        static_assert(analog.analog);
        static_assert(!analog.digital);
        static_assert(analog.HasSeparateInput());
    }

    {
        constexpr auto digital = Detect(false, false, false, true, false, false);
        static_assert(!digital.analog);
        static_assert(digital.digital);
        static_assert(digital.HasSeparateInput());
    }

    static_assert(!AimInputPolicy::IsConfiguredBinding(""));
    static_assert(!AimInputPolicy::IsConfiguredBinding("-1"));
    static_assert(!AimInputPolicy::IsConfiguredBinding("(unbound)"));
    static_assert(AimInputPolicy::IsConfiguredBinding("Throttle@17"));

    // A standalone boost zone is active without any reverse-zone dependency.
    static_assert(BoostZonePolicy::ShouldActivate(
        true, true, true, true, 62001, 60000, 2000));
    static_assert(!BoostZonePolicy::ShouldActivate(
        true, true, true, true, 62000, 60000, 2000));
    static_assert(!BoostZonePolicy::ShouldActivate(
        false, true, true, true, 65535, 60000, 2000));
    static_assert(!BoostZonePolicy::ShouldActivate(
        true, false, true, true, 65535, 60000, 2000));

    assert(true);
    return 0;
}

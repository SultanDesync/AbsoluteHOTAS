#include "AbsoluteControlThrottleActions.h"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
    using namespace AbsoluteControlSettings;
    using namespace AbsoluteControlThrottleActions;

    auto state = DefaultState();
    std::string error;
    assert(Apply(state, Action::CaptureDetent, 31415, error) == Result::Applied);
    assert(GetInteger(state, ScalarField::DetentCenter) == 31415);
    assert(Apply(state, Action::CaptureReverse, 2048, error) == Result::Applied);
    assert(GetInteger(state, ScalarField::ReverseZoneCenter) == 2048);
    assert(Apply(state, Action::CaptureBoost, 63000, error) == Result::Applied);
    assert(GetInteger(state, ScalarField::BoostZoneCenter) == 63000);

    const auto before = state;
    assert(Apply(state, Action::CaptureDetent, std::nullopt, error) ==
           Result::InputUnavailable);
    assert(Equivalent(state, before));
    assert(!error.empty());

    SetFloat(state, ScalarField::IdlePlateau, 0.13);
    SetFloat(state, ScalarField::ThrottleSaturation, 0.5);
    assert(Apply(state, Action::LinkIdleAndSaturation, std::nullopt, error) ==
           Result::Applied);
    assert(std::abs(GetFloat(state, ScalarField::ThrottleSaturation) - 0.87) < 1e-9);

    // It is a one-shot relationship, not hidden persistent state.
    SetFloat(state, ScalarField::IdlePlateau, 0.08);
    assert(std::abs(GetFloat(state, ScalarField::ThrottleSaturation) - 0.87) < 1e-9);

    assert(Apply(state, Action::CaptureBoost, 70000, error) ==
           Result::InvalidDraft);
    assert(GetInteger(state, ScalarField::BoostZoneCenter) == 63000);
    return 0;
}

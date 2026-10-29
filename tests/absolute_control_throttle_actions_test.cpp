#include "AbsoluteControlThrottleActions.h"

#include "TestCheck.h"
#include <cmath>
#include <string>

int main()
{
    using namespace AbsoluteControlSettings;
    using namespace AbsoluteControlThrottleActions;

    auto state = DefaultState();
    std::string error;
    REQUIRE(Apply(state, Action::CaptureDetent, 31415, error) == Result::Applied);
    REQUIRE(GetInteger(state, ScalarField::DetentCenter) == 31415);
    REQUIRE(Apply(state, Action::CaptureReverse, 2048, error) == Result::Applied);
    REQUIRE(GetInteger(state, ScalarField::ReverseZoneCenter) == 2048);
    REQUIRE(Apply(state, Action::CaptureBoost, 63000, error) == Result::Applied);
    REQUIRE(GetInteger(state, ScalarField::BoostZoneCenter) == 63000);

    const auto before = state;
    REQUIRE(Apply(state, Action::CaptureDetent, std::nullopt, error) ==
           Result::InputUnavailable);
    REQUIRE(Equivalent(state, before));
    REQUIRE(!error.empty());

    SetFloat(state, ScalarField::IdlePlateau, 0.13);
    SetFloat(state, ScalarField::ThrottleSaturation, 0.5);
    REQUIRE(Apply(state, Action::LinkIdleAndSaturation, std::nullopt, error) ==
           Result::Applied);
    REQUIRE(std::abs(GetFloat(state, ScalarField::ThrottleSaturation) - 0.87) < 1e-9);

    // It is a one-shot relationship, not hidden persistent state.
    SetFloat(state, ScalarField::IdlePlateau, 0.08);
    REQUIRE(std::abs(GetFloat(state, ScalarField::ThrottleSaturation) - 0.87) < 1e-9);

    REQUIRE(Apply(state, Action::CaptureBoost, 70000, error) ==
           Result::InvalidDraft);
    REQUIRE(GetInteger(state, ScalarField::BoostZoneCenter) == 63000);
    return 0;
}

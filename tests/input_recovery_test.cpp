#include "InputRecoveryPolicy.h"
#include "DeviceIdentity.h"
#include "BindingRef.h"
#include "TestCheck.h"

#include <array>

int main()
{
    InputRecoveryPolicy::Axis axis;
    REQUIRE(axis.Read(0).has_value()); // A real zero sample is valid input.
    REQUIRE(!axis.Read(std::nullopt));
    REQUIRE(axis.Observe(true, false)); // Existing startup behavior.
    REQUIRE(!axis.Observe(false, true)); // Missing is never neutral.
    REQUIRE(!axis.Observe(true, false)); // Deflected reconnect remains parked.
    REQUIRE(axis.Observe(true, true));
    REQUIRE(axis.Observe(true, false));
    axis.Lost();
    REQUIRE(!axis.Read(32768)); // Loss must never masquerade as half throttle.
    REQUIRE(!axis.Read(0));
    REQUIRE(!axis.Observe(true, false)); // A second interruption also rearms.

    InputRecoveryPolicy::Button button;
    REQUIRE(button.Observe(true));

    REQUIRE(InputRecoveryPolicy::CenteredNeutral(32000, 1000, 63000, 0.04F));
    REQUIRE(!InputRecoveryPolicy::CenteredNeutral(40000, 1000, 63000, 0.04F));
    REQUIRE(!InputRecoveryPolicy::CenteredNeutral(0, 1000, 1000, 0.04F));
    InputRecoveryPolicy::ThrottleNeutralZone zone;
    REQUIRE(zone.Contains(0));
    REQUIRE(!zone.Contains(32768));
    zone.inverted = true;
    REQUIRE(zone.Contains(65535));
    REQUIRE(!zone.Contains(0));
    zone.minimum = 1000;
    zone.maximum = 63000;
    REQUIRE(zone.Contains(63000));
    zone.rate = true;
    zone.center = 32000;
    REQUIRE(zone.Contains(32000));
    REQUIRE(!zone.Contains(63000));
    zone.rate = false;
    zone.reverseZone = true;
    zone.reverseCenter = 10000;
    REQUIRE(zone.Contains(54000));
    REQUIRE(!zone.Contains(63000));
    button.Lost();
    REQUIRE(!button.Observe(true));
    REQUIRE(!button.Observe(false));
    REQUIRE(button.Observe(true));

    constexpr std::string_view left = "{12345678-1234-ABCD-1234-123456789ABC}";
    constexpr std::string_view right = "{87654321-1234-ABCD-1234-123456789ABC}";
    std::array ids{left, right};
    const auto selector = DeviceIdentity::Selector(left);
    REQUIRE(DeviceIdentity::Resolve(selector, ids) == 0);
    std::swap(ids[0], ids[1]);
    REQUIRE(DeviceIdentity::Resolve(selector, ids) == 1);
    ids[1] = right;
    REQUIRE(DeviceIdentity::Resolve(selector, ids) == -1);
    REQUIRE(DeviceIdentity::Resolve("guid:malformed", ids) == -1);
    REQUIRE(ParseBindingRef("guid:bad@4", -1).value == -1);
    const auto binding = ParseBindingRef((selector + "@0x32").c_str(), -1);
    REQUIRE(FormatBindingRef(binding, true) == selector + "@0x32");
    REQUIRE(ParseBindingRef("#1@4", -1).deviceIndex == 1);
    REQUIRE(ParseBindingRef("Legacy Stick@4", -1).deviceName == "Legacy Stick");
    REQUIRE(DeviceIdentity::Selector("{12345678-1234-abcd-1234-123456789abc}") == selector);
}

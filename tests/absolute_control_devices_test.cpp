#include "AbsoluteControlDevices.h"
#include "DeviceIdentity.h"

#include <array>
#include "TestCheck.h"
#include <string_view>

namespace {

AbsoluteControlDevices::RuntimeDevice Device(
    std::uint32_t index, std::string_view persistentId,
    std::uint8_t productSeed, std::string_view product,
    bool connected = true)
{
    AbsoluteControlDevices::RuntimeDevice result;
    result.deviceIndex = index;
    result.productGuid[0] = productSeed;
    result.vendorId = 0x231D;
    result.productId = productSeed;
    result.axisCount = 8;
    result.buttonCount = 32;
    result.persistentId = persistentId;
    result.instanceName = std::string(product) + " instance";
    result.productName = product;
    result.connected = connected;
    result.rawAxes.fill(32768);
    result.normalizedAxes.fill(0.0F);
    return result;
}

} // namespace

int main()
{
    using namespace AbsoluteControlDevices;

    std::array devices{
        Device(0, "{00000000-0000-0000-0000-000000000001}", 7, "Twin Stick"),
        Device(1, "{00000000-0000-0000-0000-000000000002}", 9, "Throttle"),
        Device(2, "{00000000-0000-0000-0000-000000000003}", 7, "Twin Stick"),
    };

    Session session;
    session.Publish(devices.data(), devices.size());
    REQUIRE(session.Records().size() == 3);
    REQUIRE(session.Records()[0].recordId ==
           "device-00000000000000000000000000000001");
    REQUIRE(session.Records()[0].detail.find("duplicate product") !=
           std::string::npos);
    REQUIRE(session.Records()[1].detail.find("duplicate product") ==
           std::string::npos);

    // Duplicate reassignment is identity-based and works when duplicates are
    // separated by an unrelated device in enumeration order.
    REQUIRE(session.Select(session.Records()[0].recordId));
    REQUIRE(!session.SelectReassignmentTarget(session.Records()[1].recordId));
    REQUIRE(session.SelectReassignmentTarget(session.Records()[2].recordId));

    HotasBindingCatalog::BindingState bindings{};
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const auto axis = HotasBindingCatalog::kTargets[index].captureKind ==
                          HotasBindingCatalog::CaptureKind::Axis;
        bindings[index] = axis ? "#0@0x30" : "#2@1";
    }
    bindings[3] = "#1@0x33";
    bindings[4] = "Twin Stick@0x34"; // ambiguous name refs remain untouched.

    CalibrationMap calibration{
        {(0 << 8) | 0x30, {100, 65000}},
        {(1 << 8) | 0x31, {200, 64000}},
        {(2 << 8) | 0x32, {300, 63000}},
    };
    const auto reassigned = session.ReassignDuplicate(bindings, calibration);
    REQUIRE(reassigned.accepted);
    REQUIRE(reassigned.bindingChanges == bindings.size() - 2);
    REQUIRE(bindings[0] == "#2@0x30");
    REQUIRE(bindings[3] == "#1@0x33");
    REQUIRE(bindings[4] == "Twin Stick@0x34");
    REQUIRE(calibration.contains((2 << 8) | 0x30));
    REQUIRE(calibration.contains((1 << 8) | 0x31));
    REQUIRE(calibration.contains((0 << 8) | 0x32));

    // Persistent bindings also participate in explicit duplicate reassignment;
    // ordinary enumeration changes require no rewriting of these references.
    const auto left = DeviceIdentity::Selector(devices[0].persistentId);
    const auto right = DeviceIdentity::Selector(devices[2].persistentId);
    bindings[0] = left + "@0x30";
    bindings[1] = right + "@0x31";
    const auto stableReassign = session.ReassignDuplicate(bindings, calibration);
    REQUIRE(stableReassign.accepted);
    REQUIRE(bindings[0] == right + "@0x30");
    REQUIRE(bindings[1] == left + "@0x31");

    // The bounded fixed catalog is exactly the HOTAS-owned reassignment slice.
    // Standalone Head Tracking, HOSAM/mouse steering, and Power never enter it.
    for (const auto& target : HotasBindingCatalog::kTargets) {
        const std::string_view id = target.controlId;
        REQUIRE(id.find("head") == std::string_view::npos);
        REQUIRE(id.find("hosam") == std::string_view::npos);
        REQUIRE(id.find("mouse") == std::string_view::npos);
        REQUIRE(id.find("power") == std::string_view::npos);
    }

    // Eight-axis sweep ignores ghost motion at or below the established 5000
    // raw-unit threshold and saves all axes that exceed it.
    REQUIRE(session.Select(session.Records()[0].recordId));
    REQUIRE(session.BeginCalibration());
    auto moved = devices;
    moved[0].rawAxes[0] = 37768; // exactly threshold: ignored
    moved[0].rawAxes[1] = 37769; // threshold + 1: active
    moved[0].rawAxes[7] = 1000;
    session.Publish(moved.data(), moved.size());
    REQUIRE(session.Calibration().active);
    REQUIRE((session.Calibration().activeAxisMask & (1U << 0)) == 0);
    REQUIRE((session.Calibration().activeAxisMask & (1U << 1)) != 0);
    REQUIRE((session.Calibration().activeAxisMask & (1U << 7)) != 0);
    CalibrationMap swept;
    REQUIRE(session.CommitCalibration(swept) == 2);
    REQUIRE(!session.Calibration().active);
    REQUIRE(!swept.contains((0 << 8) | 0x30));
    REQUIRE((swept.at((0 << 8) | 0x31) ==
            std::pair<long, long>(32768, 37769)));
    REQUIRE((swept.at((0 << 8) | 0x37) ==
            std::pair<long, long>(1000, 32768)));

    REQUIRE(session.ClearCalibration(swept) == 2);
    REQUIRE(swept.empty());

    REQUIRE(session.BeginCalibration());
    session.CancelCalibration();
    REQUIRE(!session.Calibration().active);

    // Records are bounded even if a broken source violates DirectInput's
    // practical device count, and disconnected devices remain truthful records.
    std::array<RuntimeDevice, kMaximumDevices + 4> many{};
    for (std::size_t index = 0; index < many.size(); ++index) {
        many[index] = Device(static_cast<std::uint32_t>(index),
            "fallback-id-" + std::to_string(index),
            static_cast<std::uint8_t>(index + 1), "Device", index != 0);
    }
    session.Publish(many.data(), many.size());
    REQUIRE(session.Records().size() == kMaximumDevices);
    REQUIRE((session.Records()[0].flags & (1U << 1)) != 0);
    return 0;
}

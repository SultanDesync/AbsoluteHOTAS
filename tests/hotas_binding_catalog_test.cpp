#include "HotasBindingCatalog.h"

#include <array>
#include "TestCheck.h"
#include <string_view>

namespace {

constexpr bool ValidId(std::string_view id)
{
    if (id.empty()) return false;
    for (const unsigned char ch : id) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.') {
            continue;
        }
        return false;
    }
    return true;
}

constexpr bool IsExcludedSlot(int slot)
{
    const bool custom = slot >= CaptureSlot::kCustomBase &&
        slot < CaptureSlot::kAimAxisBase;
    const bool headLook = slot >= CaptureSlot::kHeadLookAxisBase &&
        slot <= CaptureSlot::kHeadLookToggle;
    const bool macro = slot >= CaptureSlot::kMacroBase &&
        slot < CaptureSlot::kMacroBase + 100;
    return custom || headLook || macro || slot == CaptureSlot::kProfileTrigger;
}

} // namespace

int main()
{
    using namespace HotasBindingCatalog;

    static_assert(kTargetCount == 59);
    static_assert(kTargets.size() == kTargetCount);

    std::array<bool, kNumAxisSlots> axes{};
    std::array<bool, kNumDigitalAxisSlots> digitalFallbacks{};
    std::array<bool, kNumButtonSlots> pluginButtons{};
    std::array<bool, kNumControlExtensionSlots> flightAssists{};
    std::array<bool, kNumAimAxisSlots> aimAxes{};
    std::array<bool, kNumDigitalAimSlots> digitalAim{};
    std::array<bool, kShipActionCatalog.size()> shipActions{};
    std::array<bool, kMenuNavigationCatalog.size()> menuNavigation{};
    bool aimToggle = false;
    bool turnAssist = false;

    for (std::size_t left = 0; left < kTargets.size(); ++left) {
        const auto& target = kTargets[left];
        REQUIRE(ValidId(target.controlId));
        REQUIRE(ValidId(target.pageId));
        REQUIRE(!target.displayLabel.empty());
        REQUIRE(!target.iniSection.empty());
        REQUIRE(!target.iniKey.empty());
        REQUIRE(target.iniSection != "HeadTracking");
        REQUIRE(target.iniSection != "Power");
        REQUIRE(!IsExcludedSlot(target.captureSlot));
        REQUIRE(target.controlId.find("head") == std::string_view::npos);
        REQUIRE(target.controlId.find("look") == std::string_view::npos);
        REQUIRE(target.controlId.find("hosam") == std::string_view::npos);
        REQUIRE(target.controlId.find("alignment") == std::string_view::npos);
        REQUIRE(target.controlId.find("profile") == std::string_view::npos);
        REQUIRE(target.controlId.find("macro") == std::string_view::npos);
        REQUIRE(target.controlId.find("custom") == std::string_view::npos);
        REQUIRE(target.controlId.find("power-preset") == std::string_view::npos);

        for (std::size_t right = left + 1; right < kTargets.size(); ++right) {
            REQUIRE(target.controlId != kTargets[right].controlId);
            REQUIRE(target.captureSlot != kTargets[right].captureSlot);
        }

        switch (target.family) {
        case TargetFamily::CoreAxis:
        case TargetFamily::ReverseAxis: {
            REQUIRE(target.pageId == kFlightAxesPageId);
            REQUIRE(target.captureKind == CaptureKind::Axis);
            REQUIRE(target.iniSection == "Hardware");
            const int index = target.captureSlot - CaptureSlot::kAxisBase;
            REQUIRE(index >= 0 && index < kNumAxisSlots);
            REQUIRE(target.iniKey == kAxisSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            axes[index] = true;
            break;
        }
        case TargetFamily::DigitalFallback: {
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "DigitalAxes");
            const int index = target.captureSlot - CaptureSlot::kDigitalAxisBase;
            REQUIRE(index >= 0 && index < kNumDigitalAxisSlots);
            REQUIRE(target.iniKey == kDigitalAxisSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            digitalFallbacks[index] = true;
            break;
        }
        case TargetFamily::PluginButton: {
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "Buttons");
            const int index = target.captureSlot - CaptureSlot::kButtonBase;
            REQUIRE(index >= 0 && index < kNumButtonSlots);
            REQUIRE(target.iniKey == kButtonSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            pluginButtons[index] = true;
            break;
        }
        case TargetFamily::FlightAssist: {
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "ControlExtensions");
            const int index = target.captureSlot - CaptureSlot::kControlExtensionBase;
            REQUIRE(index >= 0 && index < kNumControlExtensionSlots);
            REQUIRE(target.iniKey == kControlExtensionSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            flightAssists[index] = true;
            break;
        }
        case TargetFamily::AimAxis: {
            REQUIRE(target.captureKind == CaptureKind::Axis);
            REQUIRE(target.iniSection == "Aim");
            const int index = target.captureSlot - CaptureSlot::kAimAxisBase;
            REQUIRE(index >= 0 && index < kNumAimAxisSlots);
            REQUIRE(target.iniKey == kAimAxisSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            aimAxes[index] = true;
            break;
        }
        case TargetFamily::DigitalAim: {
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "Aim");
            const int index = target.captureSlot - CaptureSlot::kDigitalAimBase;
            REQUIRE(index >= 0 && index < kNumDigitalAimSlots);
            REQUIRE(target.iniKey == kDigitalAimSlots[index].iniKey);
            REQUIRE(target.actionId.empty());
            digitalAim[index] = true;
            break;
        }
        case TargetFamily::AimModeToggle:
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "Aim");
            REQUIRE(target.captureSlot == CaptureSlot::kToggleAimMode);
            REQUIRE(target.iniKey == "iToggleAimModeButton");
            REQUIRE(target.actionId.empty());
            aimToggle = true;
            break;
        case TargetFamily::TurnAssist:
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "DualStick");
            REQUIRE(target.captureSlot == CaptureSlot::kTurnAssistBtn);
            REQUIRE(target.iniKey == "iTurnAssistButton");
            REQUIRE(target.actionId.empty());
            turnAssist = true;
            break;
        case TargetFamily::ShipAction: {
            REQUIRE(target.pageId == kShipButtonsPageId);
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "ShipButtons");
            const int index = target.captureSlot - CaptureSlot::kShipActionBase;
            REQUIRE(index >= 0 && index < static_cast<int>(kShipActionCatalog.size()));
            REQUIRE(target.actionId == kShipActionCatalog[index].actionId);
            REQUIRE(target.iniKey == kShipActionCatalog[index].sourceIniKey);
            shipActions[index] = true;
            break;
        }
        case TargetFamily::MenuNavigation: {
            REQUIRE(target.pageId == kShipButtonsPageId);
            REQUIRE(target.captureKind == CaptureKind::ButtonOrPov);
            REQUIRE(target.iniSection == "MenuControls");
            const int index = target.captureSlot - CaptureSlot::kMenuNavigationBase;
            REQUIRE(index >= 0 &&
                   index < static_cast<int>(kMenuNavigationCatalog.size()));
            REQUIRE(target.actionId == kMenuNavigationCatalog[index].actionId);
            REQUIRE(target.iniKey == kMenuNavigationCatalog[index].iniKey);
            menuNavigation[index] = true;
            break;
        }
        }
    }

    for (const bool covered : axes) REQUIRE(covered);
    for (const bool covered : digitalFallbacks) REQUIRE(covered);
    for (const bool covered : pluginButtons) REQUIRE(covered);
    for (const bool covered : flightAssists) REQUIRE(covered);
    for (const bool covered : aimAxes) REQUIRE(covered);
    for (const bool covered : digitalAim) REQUIRE(covered);
    for (const bool covered : shipActions) REQUIRE(covered);
    for (const bool covered : menuNavigation) REQUIRE(covered);
    REQUIRE(aimToggle);
    REQUIRE(turnAssist);

    REQUIRE(Find("bind-throttle-axis") != nullptr);
    REQUIRE(Find("bind-ship-autopilot-on-off") != nullptr);
    REQUIRE(Find("bind-turn-assist") != nullptr);
    REQUIRE(Find("bind-menu-accept") != nullptr);
    REQUIRE(Find("missing-binding") == nullptr);
    return 0;
}

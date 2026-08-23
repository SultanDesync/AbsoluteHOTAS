#include "HotasBindingCatalog.h"

#include <array>
#include <cassert>
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
        assert(ValidId(target.controlId));
        assert(ValidId(target.pageId));
        assert(!target.displayLabel.empty());
        assert(!target.iniSection.empty());
        assert(!target.iniKey.empty());
        assert(target.iniSection != "HeadTracking");
        assert(target.iniSection != "Power");
        assert(!IsExcludedSlot(target.captureSlot));
        assert(target.controlId.find("head") == std::string_view::npos);
        assert(target.controlId.find("look") == std::string_view::npos);
        assert(target.controlId.find("hosam") == std::string_view::npos);
        assert(target.controlId.find("alignment") == std::string_view::npos);
        assert(target.controlId.find("profile") == std::string_view::npos);
        assert(target.controlId.find("macro") == std::string_view::npos);
        assert(target.controlId.find("custom") == std::string_view::npos);
        assert(target.controlId.find("power-preset") == std::string_view::npos);

        for (std::size_t right = left + 1; right < kTargets.size(); ++right) {
            assert(target.controlId != kTargets[right].controlId);
            assert(target.captureSlot != kTargets[right].captureSlot);
        }

        switch (target.family) {
        case TargetFamily::CoreAxis:
        case TargetFamily::ReverseAxis: {
            assert(target.pageId == kFlightAxesPageId);
            assert(target.captureKind == CaptureKind::Axis);
            assert(target.iniSection == "Hardware");
            const int index = target.captureSlot - CaptureSlot::kAxisBase;
            assert(index >= 0 && index < kNumAxisSlots);
            assert(target.iniKey == kAxisSlots[index].iniKey);
            assert(target.actionId.empty());
            axes[index] = true;
            break;
        }
        case TargetFamily::DigitalFallback: {
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "DigitalAxes");
            const int index = target.captureSlot - CaptureSlot::kDigitalAxisBase;
            assert(index >= 0 && index < kNumDigitalAxisSlots);
            assert(target.iniKey == kDigitalAxisSlots[index].iniKey);
            assert(target.actionId.empty());
            digitalFallbacks[index] = true;
            break;
        }
        case TargetFamily::PluginButton: {
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "Buttons");
            const int index = target.captureSlot - CaptureSlot::kButtonBase;
            assert(index >= 0 && index < kNumButtonSlots);
            assert(target.iniKey == kButtonSlots[index].iniKey);
            assert(target.actionId.empty());
            pluginButtons[index] = true;
            break;
        }
        case TargetFamily::FlightAssist: {
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "ControlExtensions");
            const int index = target.captureSlot - CaptureSlot::kControlExtensionBase;
            assert(index >= 0 && index < kNumControlExtensionSlots);
            assert(target.iniKey == kControlExtensionSlots[index].iniKey);
            assert(target.actionId.empty());
            flightAssists[index] = true;
            break;
        }
        case TargetFamily::AimAxis: {
            assert(target.captureKind == CaptureKind::Axis);
            assert(target.iniSection == "Aim");
            const int index = target.captureSlot - CaptureSlot::kAimAxisBase;
            assert(index >= 0 && index < kNumAimAxisSlots);
            assert(target.iniKey == kAimAxisSlots[index].iniKey);
            assert(target.actionId.empty());
            aimAxes[index] = true;
            break;
        }
        case TargetFamily::DigitalAim: {
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "Aim");
            const int index = target.captureSlot - CaptureSlot::kDigitalAimBase;
            assert(index >= 0 && index < kNumDigitalAimSlots);
            assert(target.iniKey == kDigitalAimSlots[index].iniKey);
            assert(target.actionId.empty());
            digitalAim[index] = true;
            break;
        }
        case TargetFamily::AimModeToggle:
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "Aim");
            assert(target.captureSlot == CaptureSlot::kToggleAimMode);
            assert(target.iniKey == "iToggleAimModeButton");
            assert(target.actionId.empty());
            aimToggle = true;
            break;
        case TargetFamily::TurnAssist:
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "DualStick");
            assert(target.captureSlot == CaptureSlot::kTurnAssistBtn);
            assert(target.iniKey == "iTurnAssistButton");
            assert(target.actionId.empty());
            turnAssist = true;
            break;
        case TargetFamily::ShipAction: {
            assert(target.pageId == kShipButtonsPageId);
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "ShipButtons");
            const int index = target.captureSlot - CaptureSlot::kShipActionBase;
            assert(index >= 0 && index < static_cast<int>(kShipActionCatalog.size()));
            assert(target.actionId == kShipActionCatalog[index].actionId);
            assert(target.iniKey == kShipActionCatalog[index].sourceIniKey);
            shipActions[index] = true;
            break;
        }
        case TargetFamily::MenuNavigation: {
            assert(target.pageId == kShipButtonsPageId);
            assert(target.captureKind == CaptureKind::ButtonOrPov);
            assert(target.iniSection == "MenuControls");
            const int index = target.captureSlot - CaptureSlot::kMenuNavigationBase;
            assert(index >= 0 &&
                   index < static_cast<int>(kMenuNavigationCatalog.size()));
            assert(target.actionId == kMenuNavigationCatalog[index].actionId);
            assert(target.iniKey == kMenuNavigationCatalog[index].iniKey);
            menuNavigation[index] = true;
            break;
        }
        }
    }

    for (const bool covered : axes) assert(covered);
    for (const bool covered : digitalFallbacks) assert(covered);
    for (const bool covered : pluginButtons) assert(covered);
    for (const bool covered : flightAssists) assert(covered);
    for (const bool covered : aimAxes) assert(covered);
    for (const bool covered : digitalAim) assert(covered);
    for (const bool covered : shipActions) assert(covered);
    for (const bool covered : menuNavigation) assert(covered);
    assert(aimToggle);
    assert(turnAssist);

    assert(Find("bind-throttle-axis") != nullptr);
    assert(Find("bind-ship-autopilot-on-off") != nullptr);
    assert(Find("bind-turn-assist") != nullptr);
    assert(Find("bind-menu-accept") != nullptr);
    assert(Find("missing-binding") == nullptr);
    return 0;
}

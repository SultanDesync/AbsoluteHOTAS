#pragma once

#include "ShipActionCatalog.h"
#include "WizardDefs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace HotasBindingCatalog {

enum class CaptureKind : std::uint8_t {
    Axis,
    ButtonOrPov,
};

enum class TargetFamily : std::uint8_t {
    CoreAxis,
    ReverseAxis,
    DigitalFallback,
    PluginButton,
    FlightAssist,
    AimAxis,
    DigitalAim,
    AimModeToggle,
    TurnAssist,
    ShipAction,
    MenuNavigation,
};

struct Target {
    std::string_view controlId;
    std::string_view pageId;
    std::string_view displayLabel;
    TargetFamily family{};
    CaptureKind captureKind{};
    int captureSlot = -1;
    std::string_view iniSection;
    std::string_view iniKey;
    std::string_view actionId; // Populated for named ship/menu actions.
};

inline constexpr std::string_view kFlightAxesPageId = "hotas-flight-axes";
inline constexpr std::string_view kShipButtonsPageId = "hotas-ship-buttons";
inline constexpr std::string_view kAimingPageId = "hotas-aiming";
inline constexpr std::string_view kDiagnosticsPageId = "hotas-diagnostics";

inline constexpr std::array<std::string_view, kNumAxisSlots> kAxisControlIds{
    "bind-throttle-axis",
    "bind-pitch-axis",
    "bind-yaw-axis",
    "bind-roll-axis",
    "bind-strafe-lateral-axis",
    "bind-strafe-vertical-axis",
    "bind-reverse-axis",
};

inline constexpr std::array<std::string_view, kNumDigitalAxisSlots>
    kDigitalFallbackControlIds{
        "bind-digital-reverse",
        "bind-digital-roll-left",
        "bind-digital-roll-right",
        "bind-digital-strafe-left",
        "bind-digital-strafe-right",
        "bind-digital-strafe-up",
        "bind-digital-strafe-down",
    };

inline constexpr std::array<std::string_view, kNumButtonSlots> kPluginButtonControlIds{
    "bind-plugin-activate",
    "bind-plugin-stop",
    // Stable provider ID retained for existing host state; the visible action is
    // now "Open Absolute Control" and has no embedded-workbench fallback.
    "bind-toggle-workbench",
};

inline constexpr std::array<std::string_view, kNumControlExtensionSlots>
    kFlightAssistControlIds{
        "bind-hold-current-throttle",
        "bind-full-stop",
        "bind-cruise-half",
        "bind-cruise-max",
    };

inline constexpr std::array<std::string_view, kNumAimAxisSlots> kAimAxisControlIds{
    "bind-aim-yaw-axis",
    "bind-aim-pitch-axis",
};

inline constexpr std::array<std::string_view, kNumDigitalAimSlots> kDigitalAimControlIds{
    "bind-digital-aim-left",
    "bind-digital-aim-right",
    "bind-digital-aim-up",
    "bind-digital-aim-down",
    "bind-digital-aim-center",
};

inline constexpr std::array<std::string_view, kShipActionCatalog.size()>
    kShipActionControlIds{
        "bind-ship-fire-boosters",
        "bind-ship-switch-flight-modes",
        "bind-ship-toggle-pov",
        "bind-ship-fire-weapon-1",
        "bind-ship-fire-weapon-2",
        "bind-ship-fire-weapon-3",
        "bind-ship-action-1",
        "bind-ship-select-accept",
        "bind-ship-navigation-up",
        "bind-ship-navigation-down",
        "bind-ship-navigation-left",
        "bind-ship-navigation-right",
        "bind-ship-open-scanner",
        "bind-ship-repair",
        "bind-ship-alternate-control",
        "bind-ship-cruise",
        "bind-ship-back-cancel",
        "bind-ship-undock-take-off",
        "bind-ship-get-up",
        "bind-ship-exit",
        "bind-ship-zoom-camera-in",
        "bind-ship-zoom-camera-out",
        "bind-ship-autopilot-on-off",
    };

inline constexpr std::array<std::string_view, kMenuNavigationCatalog.size()>
    kMenuNavigationControlIds{
        "bind-menu-accept",
        "bind-menu-cancel",
        "bind-menu-up",
        "bind-menu-down",
        "bind-menu-left",
        "bind-menu-right",
    };

inline constexpr std::size_t kTargetCount =
    kNumAxisSlots + kNumDigitalAxisSlots + kNumButtonSlots +
    kNumControlExtensionSlots + kNumAimAxisSlots + kNumDigitalAimSlots + 1 +
    1 + kShipActionCatalog.size() + kMenuNavigationCatalog.size();

consteval std::array<Target, kTargetCount> BuildTargets()
{
    std::array<Target, kTargetCount> targets{};
    std::size_t output = 0;

    for (int index = 0; index < kNumAxisSlots; ++index) {
        targets[output++] = {
            kAxisControlIds[index], kFlightAxesPageId, kAxisSlots[index].label,
            index == kNumAxisSlots - 1 ? TargetFamily::ReverseAxis : TargetFamily::CoreAxis,
            CaptureKind::Axis, CaptureSlot::kAxisBase + index,
            "Hardware", kAxisSlots[index].iniKey, {},
        };
    }
    for (int index = 0; index < kNumDigitalAxisSlots; ++index) {
        targets[output++] = {
            kDigitalFallbackControlIds[index], kFlightAxesPageId,
            kDigitalAxisSlots[index].label, TargetFamily::DigitalFallback,
            CaptureKind::ButtonOrPov, CaptureSlot::kDigitalAxisBase + index,
            "DigitalAxes", kDigitalAxisSlots[index].iniKey, {},
        };
    }
    for (int index = 0; index < kNumButtonSlots; ++index) {
        targets[output++] = {
            kPluginButtonControlIds[index], kDiagnosticsPageId,
            kButtonSlots[index].label, TargetFamily::PluginButton,
            CaptureKind::ButtonOrPov, CaptureSlot::kButtonBase + index,
            "Buttons", kButtonSlots[index].iniKey, {},
        };
    }
    for (int index = 0; index < kNumControlExtensionSlots; ++index) {
        targets[output++] = {
            kFlightAssistControlIds[index], kShipButtonsPageId,
            kControlExtensionSlots[index].label, TargetFamily::FlightAssist,
            CaptureKind::ButtonOrPov,
            CaptureSlot::kControlExtensionBase + index,
            "ControlExtensions", kControlExtensionSlots[index].iniKey, {},
        };
    }
    for (int index = 0; index < kNumAimAxisSlots; ++index) {
        targets[output++] = {
            kAimAxisControlIds[index], kAimingPageId, kAimAxisSlots[index].label,
            TargetFamily::AimAxis, CaptureKind::Axis,
            CaptureSlot::kAimAxisBase + index, "Aim", kAimAxisSlots[index].iniKey, {},
        };
    }
    for (int index = 0; index < kNumDigitalAimSlots; ++index) {
        targets[output++] = {
            kDigitalAimControlIds[index], kAimingPageId,
            kDigitalAimSlots[index].label, TargetFamily::DigitalAim,
            CaptureKind::ButtonOrPov, CaptureSlot::kDigitalAimBase + index,
            "Aim", kDigitalAimSlots[index].iniKey, {},
        };
    }

    targets[output++] = {
        "bind-toggle-aim-mode", kAimingPageId, "Toggle Aim Mode",
        TargetFamily::AimModeToggle, CaptureKind::ButtonOrPov,
        CaptureSlot::kToggleAimMode, "Aim", "iToggleAimModeButton", {},
    };

    targets[output++] = {
        "bind-turn-assist", kShipButtonsPageId, "Turn Assist",
        TargetFamily::TurnAssist, CaptureKind::ButtonOrPov,
        CaptureSlot::kTurnAssistBtn, "DualStick", "iTurnAssistButton", {},
    };

    for (std::size_t index = 0; index < kShipActionCatalog.size(); ++index) {
        const auto& action = kShipActionCatalog[index];
        targets[output++] = {
            kShipActionControlIds[index], kShipButtonsPageId, action.displayLabel,
            TargetFamily::ShipAction, CaptureKind::ButtonOrPov,
            CaptureSlot::kShipActionBase + static_cast<int>(index),
            "ShipButtons", action.sourceIniKey, action.actionId,
        };
    }

    for (std::size_t index = 0; index < kMenuNavigationCatalog.size(); ++index) {
        const auto& action = kMenuNavigationCatalog[index];
        targets[output++] = {
            kMenuNavigationControlIds[index], kShipButtonsPageId,
            action.displayLabel, TargetFamily::MenuNavigation,
            CaptureKind::ButtonOrPov,
            CaptureSlot::kMenuNavigationBase + static_cast<int>(index),
            "MenuControls", action.iniKey, action.actionId,
        };
    }

    return targets;
}

inline constexpr auto kTargets = BuildTargets();

using BindingState = std::array<std::string, kTargetCount>;

[[nodiscard]] constexpr const Target* Find(std::string_view controlId) noexcept
{
    for (const auto& target : kTargets) {
        if (target.controlId == controlId) return &target;
    }
    return nullptr;
}

[[nodiscard]] constexpr std::size_t IndexOf(const Target& target) noexcept
{
    return static_cast<std::size_t>(&target - kTargets.data());
}

} // namespace HotasBindingCatalog

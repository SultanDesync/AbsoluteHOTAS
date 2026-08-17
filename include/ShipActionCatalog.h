#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Pure, shared domain model for named ship controls. Runtime dispatch, macros,
// diagnostics, and the workbench all consume this catalog so route defaults and
// ControlMap metadata cannot drift into separate lists.

enum class ShipControlMethod : std::uint8_t {
    Direct,
    Context,
    KeyboardCompatibility,
};

enum class ShipActionGroup : std::uint8_t {
    WeaponsCombat,
    FlightSystems,
    Camera,
    NavigationContext,
    CockpitDocking,
};

enum class ShipActionOutputKind : std::uint8_t {
    None,
    Keyboard,
    Mouse,
};

struct ShipActionOutputSpec {
    ShipActionOutputKind kind{ ShipActionOutputKind::None };
    std::uint16_t code{};
    bool extended{};
};

using ShipControlMethodMask = std::uint8_t;

constexpr ShipControlMethodMask ShipControlMethodBit(ShipControlMethod method) noexcept
{
    return static_cast<ShipControlMethodMask>(1u << static_cast<unsigned>(method));
}

inline constexpr ShipControlMethodMask kDirectOnly =
    ShipControlMethodBit(ShipControlMethod::Direct);
inline constexpr ShipControlMethodMask kContextOnly =
    ShipControlMethodBit(ShipControlMethod::Context);
inline constexpr ShipControlMethodMask kDirectOrKeyboard =
    ShipControlMethodBit(ShipControlMethod::Direct) |
    ShipControlMethodBit(ShipControlMethod::KeyboardCompatibility);

struct ShipActionDefinition {
    std::string_view actionId;
    std::string_view displayLabel;
    ShipActionGroup group;
    ShipControlMethod recommendedMethod;
    ShipControlMethodMask allowedMethods;
    std::string_view sourceIniKey;
    std::string_view legacyOutputIniKey;
    std::string_view controlMapContext;
    std::string_view controlMapAction;
    ShipActionOutputSpec vanillaOutput;
};

inline constexpr std::array<ShipActionDefinition, 23> kShipActionCatalog{
    ShipActionDefinition{ "FireBoosters", "Fire Boosters", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iFireBoostersButton", "sFireBoostersOutput",
        "ShipHUD", "Boosters", { ShipActionOutputKind::Keyboard, 0x2A, false } },
    ShipActionDefinition{ "SwitchFlightModes", "Switch Flight Modes", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iSwitchFlightModesButton", "sSwitchFlightModesOutput",
        "ShipHUD", "SwitchFlightModes", { ShipActionOutputKind::Keyboard, 0x39, false } },
    ShipActionDefinition{ "TogglePov", "Toggle POV", ShipActionGroup::Camera,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iTogglePovButton", "sTogglePovOutput",
        "ShipHUD", "TogglePOV", { ShipActionOutputKind::Keyboard, 0x10, false } },
    ShipActionDefinition{ "FireWeapon0", "Fire Weapon 1", ShipActionGroup::WeaponsCombat,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iFireWeapon0Button", "sFireWeapon0Output",
        "ShipHUD", "WeaponGroup1", { ShipActionOutputKind::Mouse, 1, false } },
    ShipActionDefinition{ "FireWeapon1", "Fire Weapon 2", ShipActionGroup::WeaponsCombat,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iFireWeapon1Button", "sFireWeapon1Output",
        "ShipHUD", "WeaponGroup2", { ShipActionOutputKind::Mouse, 2, false } },
    ShipActionDefinition{ "FireWeapon2", "Fire Weapon 3", ShipActionGroup::WeaponsCombat,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iFireWeapon2Button", "sFireWeapon2Output",
        "ShipHUD", "WeaponGroup3", { ShipActionOutputKind::Keyboard, 0x22, false } },
    ShipActionDefinition{ "ShipAction1", "Ship Action 1", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iShipAction1Button", "sShipAction1Output",
        "ShipHUD", "XButton", { ShipActionOutputKind::Keyboard, 0x13, false } },
    ShipActionDefinition{ "SelectTarget", "Select / Accept", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iSelectTargetButton", "sSelectTargetOutput",
        "ShipHUD", "SelectTarget", { ShipActionOutputKind::Keyboard, 0x12, false } },
    ShipActionDefinition{ "IncreaseSystemPower", "Navigation Up", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iIncreaseSystemPowerButton", "sIncreaseSystemPowerOutput",
        "ShipHUD", "Up", { ShipActionOutputKind::Keyboard, 0x48, true } },
    ShipActionDefinition{ "DecreaseSystemPower", "Navigation Down", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iDecreaseSystemPowerButton", "sDecreaseSystemPowerOutput",
        "ShipHUD", "Down", { ShipActionOutputKind::Keyboard, 0x50, true } },
    ShipActionDefinition{ "PreviousSystem", "Navigation Left", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iPreviousSystemButton", "sPreviousSystemOutput",
        "ShipHUD", "Left", { ShipActionOutputKind::Keyboard, 0x4B, true } },
    ShipActionDefinition{ "NextSystem", "Navigation Right", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iNextSystemButton", "sNextSystemOutput",
        "ShipHUD", "Right", { ShipActionOutputKind::Keyboard, 0x4D, true } },
    ShipActionDefinition{ "OpenScanner", "Open Scanner", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iOpenScannerButton", "sOpenScannerOutput",
        "ShipHUD", "SHMonocle", { ShipActionOutputKind::Keyboard, 0x21, false } },
    ShipActionDefinition{ "Repair", "Repair Ship", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iRepairButton", "sRepairOutput",
        "ShipHUD", "RepairShip", { ShipActionOutputKind::Keyboard, 0x18, false } },
    ShipActionDefinition{ "ShipAlternateControlHold", "Ship Alternate Control", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iShipAlternateControlHoldButton", "sShipAlternateControlHoldOutput",
        "ShipHUD", "AltHold", { ShipActionOutputKind::Keyboard, 0x38, false } },
    ShipActionDefinition{ "Cruise", "Cruise", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iCruiseButton", "sCruiseOutput",
        "ShipHUD", "Cruise", { ShipActionOutputKind::Keyboard, 0x14, false } },
    ShipActionDefinition{ "Cancel", "Back / Cancel", ShipActionGroup::NavigationContext,
        ShipControlMethod::Context, kContextOnly, "iCancelButton", "sCancelOutput",
        "ShipHUD_Cancel", "Cancel", { ShipActionOutputKind::Keyboard, 0x01, false } },
    ShipActionDefinition{ "UndockTakeOff", "Undock / Take-Off", ShipActionGroup::CockpitDocking,
        ShipControlMethod::KeyboardCompatibility, kDirectOrKeyboard, "iUndockTakeOffButton", "sUndockTakeOffOutput",
        "Spaceship_Interaction", "TakeOff", { ShipActionOutputKind::Keyboard, 0x39, false } },
    ShipActionDefinition{ "GetUp", "Get Up", ShipActionGroup::CockpitDocking,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iGetUpButton", "sGetUpOutput",
        "Spaceship_Interaction", "Cancel", { ShipActionOutputKind::Keyboard, 0x12, false } },
    ShipActionDefinition{ "ExitShipFromCockpit", "Exit Ship", ShipActionGroup::CockpitDocking,
        ShipControlMethod::KeyboardCompatibility, kDirectOrKeyboard, "iExitShipFromCockpitButton", "sExitShipFromCockpitOutput",
        "Spaceship_Interaction", "ExitShip", { ShipActionOutputKind::Keyboard, 0x2D, false } },
    ShipActionDefinition{ "ZoomCameraIn", "Zoom Camera In", ShipActionGroup::Camera,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iZoomCameraInButton", "sZoomCameraInOutput",
        "ShipFlightCam_FreeRot", "FOVZoomIn", { ShipActionOutputKind::Mouse, 1, false } },
    ShipActionDefinition{ "ZoomCameraOut", "Zoom Camera Out", ShipActionGroup::Camera,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iZoomCameraOutButton", "sZoomCameraOutOutput",
        "ShipFlightCam_FreeRot", "FOVZoomOut", { ShipActionOutputKind::Mouse, 2, false } },
    ShipActionDefinition{ "AutopilotOnOff", "Autopilot On / Off", ShipActionGroup::FlightSystems,
        ShipControlMethod::Direct, kDirectOrKeyboard, "iAutopilotOnOffButton", "sAutopilotOnOffOutput",
        "ShipHUD_CruiseMode", "LockCourse", { ShipActionOutputKind::Keyboard, 0x39, false } },
};

constexpr const ShipActionDefinition* FindShipAction(std::string_view actionId) noexcept
{
    for (const auto& definition : kShipActionCatalog)
        if (definition.actionId == actionId) return &definition;
    return nullptr;
}

constexpr bool AllowsShipControlMethod(const ShipActionDefinition& definition,
                                       ShipControlMethod method) noexcept
{
    return (definition.allowedMethods & ShipControlMethodBit(method)) != 0;
}

constexpr std::string_view ShipControlMethodToken(ShipControlMethod method) noexcept
{
    switch (method) {
    case ShipControlMethod::Direct: return "direct";
    case ShipControlMethod::Context: return "context";
    case ShipControlMethod::KeyboardCompatibility: return "keyboard";
    }
    return {};
}

constexpr std::string_view ShipControlMethodLabel(ShipControlMethod method) noexcept
{
    switch (method) {
    case ShipControlMethod::Direct: return "Direct";
    case ShipControlMethod::Context: return "Context";
    case ShipControlMethod::KeyboardCompatibility: return "Keyboard compatibility";
    }
    return {};
}

constexpr bool ParseShipControlMethod(std::string_view normalized,
                                      ShipControlMethod& method) noexcept
{
    if (normalized == "direct") {
        method = ShipControlMethod::Direct;
        return true;
    }
    if (normalized == "context") {
        method = ShipControlMethod::Context;
        return true;
    }
    if (normalized == "keyboard" || normalized == "compatibility" ||
        normalized == "keyboardcompatibility") {
        method = ShipControlMethod::KeyboardCompatibility;
        return true;
    }
    return false;
}

struct ShipControlMethodResolution {
    ShipControlMethod method{ ShipControlMethod::Direct };
    bool overridePresent{};
    bool overrideAccepted{};
};

constexpr ShipControlMethodResolution ResolveShipControlMethod(
    const ShipActionDefinition& definition, std::string_view normalizedOverride) noexcept
{
    if (normalizedOverride.empty()) return { definition.recommendedMethod, false, false };
    ShipControlMethod requested{};
    if (!ParseShipControlMethod(normalizedOverride, requested) ||
        !AllowsShipControlMethod(definition, requested)) {
        return { definition.recommendedMethod, true, false };
    }
    return { requested, true, true };
}

enum class KeyboardResolutionSource : std::uint8_t {
    NotApplicable,
    FixedContext,
    VanillaFallback,
    ControlMapCustom,
    LegacyManualOverride,
};

constexpr KeyboardResolutionSource ResolveKeyboardResolutionSource(
    bool controlMapPrimaryFound, bool legacyOverrideFound) noexcept
{
    if (legacyOverrideFound) return KeyboardResolutionSource::LegacyManualOverride;
    if (controlMapPrimaryFound) return KeyboardResolutionSource::ControlMapCustom;
    return KeyboardResolutionSource::VanillaFallback;
}

enum class ShipActionAvailability : std::uint8_t {
    SupportedWaitingForContext,
    AvailableNow,
    UnavailableForBuild,
    UnavailableInContext,
};

constexpr ShipActionAvailability ResolveShipActionAvailability(
    bool buildSupported, bool runtimeEnabled, bool liveShipContext) noexcept
{
    if (!buildSupported) return ShipActionAvailability::UnavailableForBuild;
    if (!runtimeEnabled) return ShipActionAvailability::UnavailableInContext;
    if (!liveShipContext) return ShipActionAvailability::SupportedWaitingForContext;
    return ShipActionAvailability::AvailableNow;
}

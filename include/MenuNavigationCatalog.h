#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Optional controller bindings for Starfield's ordinary menu navigation. These
// are deliberately separate from Select Target, ship-system power, and ship
// Cancel so binding a flight action never implicitly grants it menu authority.
struct MenuNavigationDefinition {
    std::string_view actionId;
    std::string_view displayLabel;
    std::string_view iniKey;
    std::uint16_t scanCode;
    bool extended;
};

inline constexpr std::array<MenuNavigationDefinition, 6>
    kMenuNavigationCatalog{
        MenuNavigationDefinition{ "MenuAccept", "Menu Accept / Select",
            "iMenuAcceptButton", 0x12, false },
        MenuNavigationDefinition{ "MenuCancel", "Menu Cancel / Back",
            "iMenuCancelButton", 0x01, false },
        MenuNavigationDefinition{ "MenuUp", "Menu Up",
            "iMenuUpButton", 0x48, true },
        MenuNavigationDefinition{ "MenuDown", "Menu Down",
            "iMenuDownButton", 0x50, true },
        MenuNavigationDefinition{ "MenuLeft", "Menu Left",
            "iMenuLeftButton", 0x4B, true },
        MenuNavigationDefinition{ "MenuRight", "Menu Right",
            "iMenuRightButton", 0x4D, true },
    };

constexpr const MenuNavigationDefinition* FindMenuNavigationAction(
    std::string_view actionId) noexcept
{
    for (const auto& action : kMenuNavigationCatalog)
        if (action.actionId == actionId) return &action;
    return nullptr;
}

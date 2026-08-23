#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Fixed compatibility routes for native ship bindings. These mappings are
// deliberately not menu bindings: the controller loop gates them to the live
// ship context, while optional menu controls use MenuNavigationCatalog instead.
namespace UniversalContextInput {

struct Mapping {
    std::string_view actionId;
    std::uint16_t scanCode;
    bool extended;
    bool targetingSelector;
};

struct Route {
    bool vanillaKey;
    bool targetingSelector;
};

inline constexpr std::array<Mapping, 6> kMappings{
    Mapping{ "SelectTarget",        0x12, false, false }, // E: ship Select Target compatibility
    Mapping{ "IncreaseSystemPower", 0x48, true,  false }, // Up
    Mapping{ "DecreaseSystemPower", 0x50, true,  false }, // Down
    Mapping{ "PreviousSystem",      0x4B, true,  true  }, // Left + targeting SelectLeft
    Mapping{ "NextSystem",          0x4D, true,  true  }, // Right + targeting SelectRight
    Mapping{ "Cancel",              0x01, false, false }, // Esc: Ship Cancel
};

constexpr const Mapping* Find(std::string_view actionId) noexcept
{
    for (const auto& mapping : kMappings)
        if (mapping.actionId == actionId) return &mapping;
    return nullptr;
}

constexpr Route ResolveRoute(std::string_view actionId,
                             bool targetingModeActive) noexcept
{
    const auto* mapping = Find(actionId);
    if (!mapping) return { false, false };
    const bool selector = mapping->targetingSelector && targetingModeActive;
    return { !selector, selector };
}

} // namespace UniversalContextInput

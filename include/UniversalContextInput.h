#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Compatibility aliases for the six existing ship-button slots that also form
// Starfield's vanilla context-navigation cluster. Keeping the historical action
// IDs and INI keys preserves every profile while fixed keyboard scan codes let
// Starfield's active ControlMap context choose the meaning of each press.
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
    Mapping{ "SelectTarget",        0x12, false, false }, // E: Select Target / Accept
    Mapping{ "IncreaseSystemPower", 0x48, true,  false }, // Up
    Mapping{ "DecreaseSystemPower", 0x50, true,  false }, // Down
    Mapping{ "PreviousSystem",      0x4B, true,  true  }, // Left + targeting SelectLeft
    Mapping{ "NextSystem",          0x4D, true,  true  }, // Right + targeting SelectRight
    Mapping{ "Cancel",              0x01, false, false }, // Esc: Ship Cancel / Menu Cancel
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

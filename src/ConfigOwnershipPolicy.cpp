#include "ConfigOwnershipPolicy.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool Equal(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() &&
        _strnicmp(left.data(), right.data(), left.size()) == 0;
}

bool DynamicSection(std::string_view section) noexcept
{
    return Equal(section, "Calibration") || Equal(section, "ButtonExpansion") ||
           section.size() >= 6 && _strnicmp(section.data(), "Macro:", 6) == 0;
}

void DeleteDynamicSections(CSimpleIniA& ini)
{
    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);
    std::vector<std::string> remove;
    for (const auto& section : sections) {
        if (section.pItem && DynamicSection(section.pItem)) {
            remove.emplace_back(section.pItem);
        }
    }
    for (const auto& section : remove) ini.Delete(section.c_str(), nullptr);
}

void CopySection(const CSimpleIniA& source, CSimpleIniA& destination,
                 const char* section)
{
    CSimpleIniA::TNamesDepend keys;
    source.GetAllKeys(section, keys);
    for (const auto& key : keys) {
        const char* value = source.GetValue(section, key.pItem, nullptr);
        if (value) destination.SetValue(section, key.pItem, value);
    }
}

} // namespace

namespace ConfigOwnershipPolicy {

bool IsStandaloneOwned(std::string_view section,
                       std::string_view key) noexcept
{
    if (Equal(section, "HeadTracking") || Equal(section, "MouseSteering") ||
        Equal(section, "AbsoluteZero") || Equal(section, "HOSAM") ||
        Equal(section, "Alignment") || Equal(section, "Power") ||
        Equal(section, "AbsolutePower")) {
        return true;
    }
    if (!Equal(section, "Aim")) return false;
    constexpr std::array<std::string_view, 5> movedAimKeys{
        "bHOSAMMode", "bAlignmentAssist", "fAlignmentRadius",
        "iAlignmentIdleMs", "fAlignmentDecayRate",
    };
    for (const auto candidate : movedAimKeys) {
        if (Equal(key, candidate)) return true;
    }
    return false;
}

void RemoveStandaloneOwned(CSimpleIniA& ini)
{
    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);
    std::vector<std::string> removeSections;
    for (const auto& section : sections) {
        if (!section.pItem) continue;
        if (IsStandaloneOwned(section.pItem, "") &&
            !Equal(section.pItem, "Aim")) {
            removeSections.emplace_back(section.pItem);
            continue;
        }
        CSimpleIniA::TNamesDepend keys;
        ini.GetAllKeys(section.pItem, keys);
        for (const auto& key : keys) {
            if (key.pItem && IsStandaloneOwned(section.pItem, key.pItem)) {
                ini.Delete(section.pItem, key.pItem, true);
            }
        }
    }
    for (const auto& section : removeSections) {
        ini.Delete(section.c_str(), nullptr);
    }
}

void ReplaceManagedPayload(CSimpleIniA& destination,
                           const CSimpleIniA& incoming,
                           const CSimpleIniA& managedTemplate)
{
    CSimpleIniA::TNamesDepend sections;
    managedTemplate.GetAllSections(sections);
    for (const auto& section : sections) {
        if (!section.pItem) continue;
        CSimpleIniA::TNamesDepend keys;
        managedTemplate.GetAllKeys(section.pItem, keys);
        for (const auto& key : keys) {
            if (!key.pItem) continue;
            destination.Delete(section.pItem, key.pItem, true);
            if (const char* value = incoming.GetValue(
                    section.pItem, key.pItem, nullptr)) {
                destination.SetValue(section.pItem, key.pItem, value);
            }
        }
    }

    DeleteDynamicSections(destination);
    CSimpleIniA::TNamesDepend incomingSections;
    incoming.GetAllSections(incomingSections);
    for (const auto& section : incomingSections) {
        if (section.pItem && DynamicSection(section.pItem)) {
            CopySection(incoming, destination, section.pItem);
        }
    }
}

} // namespace ConfigOwnershipPolicy

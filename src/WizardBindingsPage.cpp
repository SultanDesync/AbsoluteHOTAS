#include "PCH.h"

#include "WizardUI.h"

#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"
#include "ShipActionCatalog.h"
#include "ShipOutput.h"

#include <imgui.h>

namespace WizardUI {


static void DrawCustomKeyBindings(WizardState& s) {
    if (!ImGui::CollapsingHeader("Keyboard & Mouse Shortcuts", ImGuiTreeNodeFlags_None)) return;

    ImGui::Indent(12);
    ImGui::TextWrapped("Send a keyboard key or mouse button from a controller button. Use these for commands that do not have a named control above. For chords or sequences, use Advanced > Macros.");
    if (ImGui::SmallButton("Build a chord or sequence..."))
        WizardSession::Navigate(WizardSession::Route::AdvancedMacros);
    ImGui::Spacing();

    if (ImGui::Button("Add Binding")) s.customBindings.push_back({"(unbound)", "none"});
    ImGui::SameLine();
    if (ImGui::Button("Add menu-navigation preset")) {
        s.customBindings.push_back({"(unbound)", "key:0x11"});
        s.customBindings.push_back({"(unbound)", "key:0x1E"});
        s.customBindings.push_back({"(unbound)", "key:0x1F"});
        s.customBindings.push_back({"(unbound)", "key:0x20"});
        s.customBindings.push_back({"(unbound)", "key:0x0F"});
        s.customBindings.push_back({"(unbound)", "key:0x12"});
        s.customBindings.push_back({"(unbound)", "key:0x01"});
        Log("Added menu-navigation preset (WASD/Tab/E/Esc).");
    }
    ImGui::Spacing();

    int removeIdx = -1;
    for (int i = 0; i < (int)s.customBindings.size(); i++) {
        auto& row = s.customBindings[i];
        ImGui::PushID(5000 + i);
        int currentOutput = FindOutputIndex(row.output);
        const char* previewLabel = (currentOutput >= 0) ? kOutputCatalog[currentOutput].label : row.output.c_str();
        if (ImGui::BeginTable("CustomBinding", 3,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.32f);
            ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 156.0f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string bindingDisplay = WizardConfig::FormatBindingDisplay(row.buttonBinding);
            ImGui::TextColored(row.buttonBinding == "(unbound)"
                    ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f) : ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                "%s", bindingDisplay.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##output", previewLabel)) {
                for (int j = 0; j < kOutputCatalogSize; j++) {
                    bool selected = (j == currentOutput);
                    if (ImGui::Selectable(kOutputCatalog[j].label, selected)) row.output = kOutputCatalog[j].value;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Bind")) {
                char label[64];
                int outputIdx = FindOutputIndex(row.output);
                std::snprintf(label, sizeof(label), "Custom #%d (%s)", i + 1,
                    outputIdx >= 0 ? kOutputCatalog[outputIdx].label : "?");
                WizardSession::BeginButtonCapture(i, CaptureSlot::kCustomBase, label);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) row.buttonBinding = "(unbound)";
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeIdx = i;
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    if (removeIdx >= 0) s.customBindings.erase(s.customBindings.begin() + removeIdx);
    if (s.customBindings.empty())
        ImGui::TextDisabled("No custom bindings. Add one or use the menu-navigation preset to get started.");
    ImGui::Unindent(12);
}

static std::string OutputDisplay(const ShipOutput& output) {
    if (output.kind == ShipOutputKind::Mouse) {
        switch (output.code) {
            case 1: return "Left Mouse";
            case 2: return "Right Mouse";
            case 3: return "Middle Mouse";
            case 4: return "Mouse Button 4";
            default: return "Mouse";
        }
    }
    if (output.kind != ShipOutputKind::Keyboard || output.code == 0) return "Unbound";
    char name[64]{};
    LONG keyData = static_cast<LONG>(output.code) << 16;
    if (output.extended) keyData |= 1 << 24;
    if (GetKeyNameTextA(keyData, name, static_cast<int>(std::size(name))) > 0)
        return name;
    char fallback[16]{};
    std::snprintf(fallback, sizeof(fallback), "Key 0x%02X", output.code);
    return fallback;
}

static const char* ResolutionSourceLabel(KeyboardResolutionSource source) {
    switch (source) {
        case KeyboardResolutionSource::FixedContext: return "fixed context input";
        case KeyboardResolutionSource::ControlMapCustom: return "Starfield Controls binding";
        case KeyboardResolutionSource::LegacyManualOverride: return "manual compatibility override";
        case KeyboardResolutionSource::VanillaFallback: return "vanilla binding";
        case KeyboardResolutionSource::NotApplicable: return "";
    }
    return "";
}

static void DrawRouteSummary(const ShipActionRouteInfo& route) {
    ImGui::Indent(12.0f);
    switch (route.method) {
        case ShipControlMethod::Direct:
            ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "Direct");
            if (route.availability == ShipActionAvailability::UnavailableForBuild) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "Unavailable for this Starfield build");
            } else if (route.availability == ShipActionAvailability::SupportedWaitingForContext) {
                ImGui::SameLine();
                ImGui::TextDisabled("Waiting for a live ship context");
            } else if (route.availability == ShipActionAvailability::UnavailableInContext) {
                ImGui::SameLine();
                ImGui::TextDisabled("Inactive in the current context");
            }
            break;
        case ShipControlMethod::Context:
            ImGui::TextColored(ImVec4(0.7f, 0.75f, 1.0f, 1.0f), "Context");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", OutputDisplay(route.resolvedKeyboardOutput).c_str());
            break;
        case ShipControlMethod::KeyboardCompatibility:
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 1.0f, 1.0f), "Keyboard compatibility");
            ImGui::SameLine();
            ImGui::TextDisabled("%s · %s",
                OutputDisplay(route.resolvedKeyboardOutput).c_str(),
                ResolutionSourceLabel(route.keyboardSource));
            break;
    }
    ImGui::Unindent(12.0f);
}

static void DrawShipActionRow(WizardState& s, int index) {
    if (index < 0 || index >= static_cast<int>(s.shipActionSlots.size()) ||
        index >= static_cast<int>(kShipActionCatalog.size())) return;
    const auto& definition = kShipActionCatalog[index];
    auto& slot = s.shipActionSlots[index];
    ImGui::PushID(3000 + index);
    DrawBindingRow(definition.displayLabel.data(), slot.binding,
                   CaptureSlot::kShipActionBase + index, false);
    DrawRouteSummary(ShipOutputSystem::GetShipActionRouteInfo(definition.actionId));
    if (definition.actionId == "FireBoosters") {
        ImGui::Indent(12.0f);
        ImGui::Checkbox("Let boost temporarily take throttle authority", &s.holdForBoost);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pause throttle injection while boost is held, then resume at maximum throttle.");
        ImGui::Unindent(12.0f);
    }
    ImGui::PopID();
}

static void DrawActionGroup(WizardState& s, ShipActionGroup group,
                            const char* label = nullptr) {
    if (label) ImGui::TextDisabled("%s", label);
    for (int index = 0; index < static_cast<int>(kShipActionCatalog.size()); ++index)
        if (kShipActionCatalog[index].group == group) DrawShipActionRow(s, index);
}

static void DrawMenuControlReuse(WizardState& s) {
    if (!ImGui::TreeNodeEx("Reuse flight controls for menus and targeting",
                          ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("Optionally reuse familiar flight controls for navigation. Inputs must return to neutral after a context opens before they can act.");
    ImGui::Checkbox("Pitch axis navigates Up / Down", &s.usePitchAxisForMenu);
    ImGui::Checkbox("Yaw axis navigates Left / Right", &s.useYawAxisForMenu);
    ImGui::Checkbox("Primary Weapon acts as Select / Accept", &s.usePrimaryWeaponForMenuSelect);

    if (s.usePitchAxisForMenu || s.useYawAxisForMenu) {
        if (s.usePitchAxisForMenu)
            ImGui::Checkbox("Invert vertical menu navigation", &s.invertMenuVertical);
        if (s.useYawAxisForMenu)
            ImGui::Checkbox("Invert horizontal menu navigation", &s.invertMenuHorizontal);
        float engagePercent = s.menuAxisEngageThreshold * 100.0f;
        float releasePercent = s.menuAxisReleaseThreshold * 100.0f;
        if (ImGui::SliderFloat("Axis actuation", &engagePercent, 35.0f, 95.0f, "%.0f%%"))
            s.menuAxisEngageThreshold = engagePercent / 100.0f;
        if (ImGui::SliderFloat("Axis release", &releasePercent, 5.0f, 80.0f, "%.0f%%"))
            s.menuAxisReleaseThreshold = releasePercent / 100.0f;
        s.menuAxisReleaseThreshold = std::clamp(
            s.menuAxisReleaseThreshold, 0.05f, s.menuAxisEngageThreshold - 0.05f);
    }
    ImGui::TreePop();
}

void DrawButtonsTab(WizardState& s) {
    ImGui::SeparatorText("Direct Ship Controls");
    ImGui::TextWrapped("These controls call Starfield's ship functions directly and are active only in the appropriate ship context.");
    DrawActionGroup(s, ShipActionGroup::WeaponsCombat, "Weapons & Combat");
    ImGui::Spacing();
    DrawActionGroup(s, ShipActionGroup::FlightSystems, "Flight Systems");
    ImGui::Spacing();
    DrawActionGroup(s, ShipActionGroup::Camera, "Camera");

    ImGui::Spacing();
    ImGui::SeparatorText("Navigation & Context Controls");
    ImGui::TextWrapped("These controls work across flight, targeting, menus, and dialogue using Starfield's standard Select, Back, and navigation inputs.");
    DrawActionGroup(s, ShipActionGroup::NavigationContext);
    DrawMenuControlReuse(s);

    ImGui::Spacing();
    ImGui::SeparatorText("Cockpit & Docking Shortcuts");
    ImGui::TextWrapped("These shortcuts use the most reliable route for the corresponding cockpit action. Keyboard compatibility follows your current Starfield Controls binding.");
    DrawActionGroup(s, ShipActionGroup::CockpitDocking);

    ImGui::Spacing();
    ImGui::SeparatorText("Flight Assist");
    ImGui::TextWrapped("These commands control AbsoluteHOTAS throttle authority rather than sending a Starfield key.");
    for (int i = 0; i < kNumControlExtensionSlots; ++i) {
        ImGui::PushID(6000 + i);
        DrawBindingRow(kControlExtensionSlots[i].label, s.controlExtensionBindings[i],
                       CaptureSlot::kControlExtensionBase + i, false);
        ImGui::PopID();
    }

    ImGui::Spacing();
    DrawCustomKeyBindings(s);
}

}  // namespace WizardUI

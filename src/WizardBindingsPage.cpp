#include "PCH.h"

#include "WizardUI.h"

#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {


static void DrawCustomKeyBindings(WizardState& s) {
    if (!ImGui::CollapsingHeader("Custom Key Bindings", ImGuiTreeNodeFlags_None)) return;

    ImGui::Indent(12);
    ImGui::TextWrapped("Bind controller buttons to raw keyboard/mouse outputs for menus or actions outside the named Ship Actions list. Raw custom outputs are not reconciled automatically; bind the same key or mouse button to the desired action in Starfield's Controls menu. For chords or sequences, use Advanced > Macros.");
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

void DrawButtonsTab(WizardState& s) {
    if (ImGui::CollapsingHeader("Core Ship Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12);
        ImGui::TextWrapped("Bind physical controller buttons to named Starfield ship actions. Each action follows your current in-game keyboard/mouse binding automatically.");
        ImGui::Spacing();
        for (int i = 0; i < (int)s.shipActionSlots.size(); i++) {
            ImGui::PushID(3000 + i);
            DrawBindingRow(s.shipActionSlots[i].label.c_str(), s.shipActionSlots[i].binding, CaptureSlot::kShipActionBase + i, false);
            if (i == 0) {
                ImGui::SameLine();
                ImGui::Checkbox("Hold for Boost", &s.holdForBoost);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Pause throttle injection while boost is held.\nOn release: set throttle to max and cancel boost.");
                }
            }
            ImGui::PopID();
        }
        ImGui::Unindent(12);
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Flight Assist", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12);
        for (int i = 0; i < kNumControlExtensionSlots; ++i) {
            ImGui::PushID(6000 + i);
            DrawBindingRow(kControlExtensionSlots[i].label, s.controlExtensionBindings[i],
                           CaptureSlot::kControlExtensionBase + i, false);
            ImGui::PopID();
        }
        ImGui::Unindent(12);
    }

    ImGui::Spacing();
    DrawCustomKeyBindings(s);
}

}  // namespace WizardUI

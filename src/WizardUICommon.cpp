#include "PCH.h"

#include "WizardUI.h"

#include "RuntimePaths.h"
#include "WizardConfig.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {

void Log(const std::string& message) {
    RuntimePaths::Log("[BindingWizard]", message);
}

// --- Shared UI helper: draw a binding row with Bind/Clear ---
void DrawBindingRow(const char* label, std::string& binding, int captureSlot,
                           bool isAxis, bool* invert) {
    const auto& pending = WizardSession::Capture();
    std::string displayStr = WizardConfig::FormatBindingDisplay(binding);
    const ImVec4 color = (binding == "(unbound)")
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        : ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
    const bool isCapturing = pending.active && pending.targetConfigSlot == captureSlot;

    ImGui::PushID(captureSlot + 700000);
    const int columns = invert ? 4 : 3;
    if (ImGui::BeginTable("BindingRow", columns,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        if (invert) ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", label);
        ImGui::TableNextColumn();
        ImGui::TextColored(color, "%s", displayStr.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", displayStr.c_str());
        ImGui::TableNextColumn();
        if (isCapturing) {
            if (ImGui::SmallButton("Cancel")) WizardSession::CancelCapture();
        } else {
            if (ImGui::SmallButton("Bind")) {
                if (isAxis) {
                    WizardSession::BeginAxisCapture(captureSlot, label);
                } else {
                    const int category = (captureSlot / 100) * 100;
                    const int index = captureSlot % 100;
                    WizardSession::BeginButtonCapture(index, category, label);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) binding = "(unbound)";
        }
        if (invert) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("Invert", invert);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void DrawBindingSummaryRow(const char* label, const std::string& binding,
                                  bool* invert) {
    const std::string display = WizardConfig::FormatBindingDisplay(binding);
    const ImVec4 color = binding == "(unbound)"
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f) : ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
    ImGui::PushID(label);
    const int columns = invert ? 3 : 2;
    if (ImGui::BeginTable("BindingSummary", columns,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        if (invert) ImGui::TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", label);
        ImGui::TableNextColumn();
        ImGui::TextColored(color, "%s", display.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", display.c_str());
        if (invert) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("Invert", invert);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

}  // namespace WizardUI

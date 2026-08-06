#include "PCH.h"

#include "WizardUI.h"

#include "WizardCapture.h"
#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {
namespace {

std::string s_profileCaptureName;   // "" = base (see s_profileCapturePending)
std::string s_profileCaptureMode = "momentary";
bool        s_profileCapturePending = false;  // a profile-trigger capture is in flight

std::string VisibleProfileName(const std::string& name) {
    return WizardSession::VisibleProfileName(name);
}

void SetStatus(const std::string& message, bool isError = false) {
    WizardSession::SetStatus(message, isError
        ? WizardSession::StatusKind::Error : WizardSession::StatusKind::Success);
}

bool LoadEditorProfile(const std::string& name) {
    return WizardSession::LoadEditorProfile(name);
}

}  // namespace


// --- Capture commit callback ---
void OnCaptureCommit(int slot, const char* binding) {
    auto& s = WizardConfig::GetState();
    if (slot >= CaptureSlot::kAxisBase && slot < CaptureSlot::kButtonBase) {
        s.axisBindings[slot] = binding;
    } else if (slot >= CaptureSlot::kButtonBase && slot < CaptureSlot::kShipActionBase) {
        s.buttonBindings[slot - CaptureSlot::kButtonBase] = binding;
    } else if (slot >= CaptureSlot::kShipActionBase && slot < CaptureSlot::kDigitalAxisBase) {
        int idx = slot - CaptureSlot::kShipActionBase;
        if (idx < (int)s.shipActionSlots.size()) s.shipActionSlots[idx].binding = binding;
    } else if (slot >= CaptureSlot::kDigitalAxisBase && slot < CaptureSlot::kCustomBase) {
        s.digitalAxisBindings[slot - CaptureSlot::kDigitalAxisBase] = binding;
    } else if (slot >= CaptureSlot::kCustomBase && slot < CaptureSlot::kAimAxisBase) {
        int idx = slot - CaptureSlot::kCustomBase;
        if (idx < (int)s.customBindings.size()) s.customBindings[idx].buttonBinding = binding;
    } else if (slot >= CaptureSlot::kAimAxisBase && slot < CaptureSlot::kDigitalAimBase) {
        s.aimAxisBindings[slot - CaptureSlot::kAimAxisBase] = binding;
    } else if (slot >= CaptureSlot::kDigitalAimBase && slot < CaptureSlot::kToggleAimMode) {
        s.digitalAimBindings[slot - CaptureSlot::kDigitalAimBase] = binding;
    } else if (slot == CaptureSlot::kToggleAimMode) {
        s.toggleAimModeBinding = binding;
    } else if (slot == CaptureSlot::kTurnAssistBtn) {
        s.turnAssistBinding = binding;
    } else if (slot >= CaptureSlot::kHeadLookAxisBase
               && slot < CaptureSlot::kHeadLookAxisBase + kNumHeadLookAxisSlots) {
        s.headLookAxisBindings[slot - CaptureSlot::kHeadLookAxisBase] = binding;
    } else if (slot == CaptureSlot::kHeadLookRecenter) {
        s.headLookRecenterBinding = binding;
    } else if (slot == CaptureSlot::kHeadLookToggle) {
        s.headLookToggleBinding = binding;
    } else if (slot >= CaptureSlot::kMacroBase && slot < CaptureSlot::kMacroBase + 100) {
        int idx = slot - CaptureSlot::kMacroBase;
        if (idx < (int)s.macros.size()) s.macros[idx].buttonBinding = binding;
    } else if (slot >= CaptureSlot::kControlExtensionBase
               && slot < CaptureSlot::kControlExtensionBase + kNumControlExtensionSlots) {
        s.controlExtensionBindings[slot - CaptureSlot::kControlExtensionBase] = binding;
    } else if (slot == CaptureSlot::kProfileTrigger && s_profileCapturePending) {
        // s_profileCaptureName may be empty — that is the base config's own trigger.
        if (WizardSession::SetActivationDraft(s_profileCaptureName, binding, s_profileCaptureMode)) {
            WizardSession::SetStatus("Profile activation staged. Save & Apply to commit it.",
                                     WizardSession::StatusKind::Warning);
        } else {
            SetStatus("Could not stage profile activation.", true);
        }
        s_profileCaptureName.clear();
        s_profileCapturePending = false;
    }
}


static void RequestEditorProfile(const std::string& name) {
    WizardSession::RequestEditorProfile(name);
}

static void DrawPendingProfileSwitchModal(const std::string& visibleName) {
    static bool popupWasRequested = false;
    if (WizardSession::HasPendingProfileSwitch() && !popupWasRequested) {
        ImGui::OpenPopup("Unsaved profile changes");
        popupWasRequested = true;
    }
    if (!ImGui::BeginPopupModal("Unsaved profile changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("%s has unsaved changes.", visibleName.c_str());
    ImGui::TextDisabled("Choose what to do before editing %s.",
        VisibleProfileName(WizardSession::PendingProfile()).c_str());
    const auto& status = WizardSession::GetStatus();
    if (status.kind == WizardSession::StatusKind::Error && !status.message.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", status.message.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Save and switch", ImVec2(140, 0))) {
        if (WizardSession::ResolveProfileSwitch(WizardSession::ProfileSwitchChoice::Save)) {
            popupWasRequested = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard and switch", ImVec2(140, 0))) {
        if (WizardSession::ResolveProfileSwitch(WizardSession::ProfileSwitchChoice::Discard)) {
            popupWasRequested = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        WizardSession::ResolveProfileSwitch(WizardSession::ProfileSwitchChoice::Cancel);
        popupWasRequested = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void DrawProfileActivation(const std::string& profile,
                                  const std::string& trigger,
                                  const std::string& mode,
                                  const std::string& keyboardShortcut = "(unbound)",
                                  const std::string& profileFilename = "") {
    const bool base = profile.empty();
    ImGui::PushID(base ? "baseActivation" : profile.c_str());
    if (!base && keyboardShortcut != "(unbound)") {
        if (ImGui::BeginTable("KeyboardShortcut", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Keyboard shortcut");
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s",
                WizardConfig::FormatBindingDisplay(keyboardShortcut).c_str());
            ImGui::EndTable();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("This toggle shortcut is independent of the custom activation below. If it collides with another mod or utility, edit [Profile] sKeyboardShortcut in Profiles/%s and restart the game.",
                           profileFilename.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    if (ImGui::BeginTable("ActivationBinding", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 205.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(base ? "Activation" : "Custom activation");
        ImGui::TableNextColumn();
        ImGui::TextColored(trigger == "(unbound)"
                ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f) : ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
            "%s", WizardConfig::FormatBindingDisplay(trigger).c_str());
        ImGui::TableNextColumn();
        if (ImGui::Button("Bind trigger")) {
            s_profileCaptureName = profile;
            s_profileCaptureMode = mode;
            s_profileCapturePending = true;
            WizardSession::BeginButtonCapture(0, CaptureSlot::kProfileTrigger,
                base ? "Base trigger" : "Profile trigger", mode == "selector"
                    ? WizardCapture::kSelectorCaptureMs : WizardCapture::kButtonCaptureMs);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear trigger")) {
            if (WizardSession::SetActivationDraft(profile, "(unbound)", mode))
                WizardSession::SetStatus(base ? "Main-controls activation clear staged."
                                              : "Profile activation clear staged.",
                                         WizardSession::StatusKind::Warning);
        }
        ImGui::EndTable();
    }

    const char* profileModes[] = {"momentary", "toggle", "selector"};
    const char* baseModes[] = {"momentary", "selector"};
    int modeIndex = mode == "selector" ? (base ? 1 : 2) : mode == "toggle" ? 1 : 0;
    ImGui::SetNextItemWidth(160.0f);
    const bool changed = base
        ? ImGui::Combo("Activation mode", &modeIndex, baseModes, 2)
        : ImGui::Combo("Activation mode", &modeIndex, profileModes, 3);
    if (changed) {
        const char* selectedMode = base ? baseModes[modeIndex] : profileModes[modeIndex];
        if (WizardSession::SetActivationDraft(profile, trigger, selectedMode))
            WizardSession::SetStatus(base ? "Main-controls activation mode staged."
                                          : "Profile activation mode staged.",
                                     WizardSession::StatusKind::Warning);
    }
    ImGui::PopID();
}

void DrawProfileContextBar(bool dirty) {
    const std::string current = WizardConfig::GetEditProfile();
    const std::string visibleName = VisibleProfileName(current);
    const auto& profiles = WizardSession::Profiles();
    DrawPendingProfileSwitchModal(visibleName);

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Editing:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(280.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    if (ImGui::BeginCombo("##editprofile", visibleName.c_str())) {
        if (ImGui::Selectable("Main controls", current.empty())) RequestEditorProfile("");
        for (const auto& profile : profiles) {
            if (ImGui::Selectable(profile.name.c_str(), profile.name == current))
                RequestEditorProfile(profile.name);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Unsaved changes");
    else
        ImGui::TextDisabled("All changes saved");
}

void DrawProfileManagementPanel(bool dirty) {
    const std::string current = WizardConfig::GetEditProfile();
    const auto& profiles = WizardSession::Profiles();
    if (!ImGui::CollapsingHeader("Profile activation and management")) return;

    static char newProfileName[64] = "";
    static char exportName[64] = "";
    static int importIndex = 0;

    if (current.empty()) {
        ImGui::TextDisabled("Base flight config. Other profiles overlay it and return here when their activation ends or base is selected.");
        DrawProfileActivation(current, WizardSession::BaseActivationTrigger(),
                              WizardSession::BaseActivationMode());
    } else {
        const auto it = std::find_if(profiles.begin(), profiles.end(),
            [&](const auto& profile) { return profile.name == current; });
        if (it != profiles.end()) {
            ImGui::TextDisabled("%s  |  %s  |  slot %d", it->kind.c_str(),
                it->filename.c_str(), it->slot);
            DrawProfileActivation(current, it->trigger, it->mode,
                                  it->keyboardShortcut, it->filename);
        }
    }

    // Management actions live in a collapsed subsection so the second (import)
    // profile dropdown doesn't sit next to the "Editing profile" selector above and
    // muddy which one is the profile you're looking at.
    ImGui::Spacing();
    if (ImGui::TreeNode("Manage profiles")) {
        if (dirty) ImGui::TextDisabled("Save or discard the current edits before adding, importing, or resetting profiles.");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::InputTextWithHint("##newprofile", "new overlay name", newProfileName, sizeof(newProfileName));
        ImGui::SameLine();
        ImGui::BeginDisabled(dirty);
        if (ImGui::Button("Add overlay")) {
            std::string err;
            if (WizardConfig::CreateOverlayProfile(newProfileName, err)) {
                WizardSession::RefreshProfiles();
                if (LoadEditorProfile(newProfileName)) {
                    newProfileName[0] = '\0';
                    SetStatus("Overlay created and opened for editing.");
                } else SetStatus(err, true);
            } else SetStatus(err, true);
        }
        ImGui::EndDisabled();

        ImGui::SetNextItemWidth(190.0f);
        ImGui::InputTextWithHint("##exportprofile", "independent profile name", exportName, sizeof(exportName));
        ImGui::SameLine();
        if (ImGui::Button("Export base setup")) {
            std::string err;
            if (WizardConfig::ExportProfile(exportName, err)) {
                exportName[0] = '\0';
                WizardSession::RefreshProfiles();
                SetStatus("Independent profile exported.");
            } else SetStatus(err, true);
        }

        if (importIndex >= (int)profiles.size()) importIndex = 0;
        const char* importPreview = profiles.empty() ? "(no profiles)" : profiles[importIndex].name.c_str();
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::BeginCombo("Import file", importPreview)) {
            for (int i = 0; i < (int)profiles.size(); ++i) {
                if (ImGui::Selectable(profiles[i].name.c_str(), i == importIndex)) importIndex = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool canImport = !dirty && !profiles.empty() && profiles[importIndex].kind == "full";
        ImGui::BeginDisabled(!canImport);
        if (ImGui::Button("Import as base")) {
            std::string err;
            if (WizardConfig::ImportProfile(profiles[importIndex].name, err)) {
                WizardSession::RefreshProfiles();
                SetStatus("Imported; previous base backed up.");
            }
            else SetStatus(err, true);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(dirty);
        if (ImGui::Button("Reset base to defaults")) ImGui::OpenPopup("Reset base configuration?");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Reset base configuration?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("This clears base bindings, tuning, calibration, custom outputs, and macros. Profile files and their activation slots are preserved. A backup is created first.");
            ImGui::Spacing();
            if (ImGui::Button("Reset", ImVec2(120, 0))) {
                std::string err;
                if (WizardConfig::ResetBaseToDefaults(err)) {
                    WizardSession::RefreshProfiles();
                    SetStatus("Base reset to shipped defaults; backup created.");
                }
                else SetStatus(err, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::TreePop();
    }

    ImGui::TextDisabled("Activation changes are committed by Save & Apply.");

}

}  // namespace WizardUI

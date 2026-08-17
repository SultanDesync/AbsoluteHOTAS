#include "PCH.h"

#include "BindingWizard.h"
#include "WizardDefs.h"
#include "WizardConfig.h"
#include "WizardSession.h"
#include "WizardUI.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"
#include "UIHook.h"
#include "DeviceManager.h"
#include "Plugin.h"
#include "PowerModuleUI.h"

#include <imgui.h>

#include <string>
#include <algorithm>

static bool AreGameMenusClosed() {
    const uintptr_t source = ThrottleHook::GetSourceBasePtr();
    if (source < 0x10000) return false;

    // Previously validated while investigating automatic pilot detection:
    // source+0x1B4 is nonzero during gameplay and zero while a game menu is open.
    // Treat an unavailable/stale pointer as unknown so the warning is never shown
    // from a failed read.
    __try {
        return *reinterpret_cast<volatile uint8_t*>(source + 0x1B4) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static std::string VisibleProfileName(const std::string& name) {
    return WizardSession::VisibleProfileName(name);
}

static bool SaveCurrentProfile() {
    return WizardSession::SaveCurrentProfile();
}

static bool DrawSecondaryNavigationButton(const char* label, bool selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.32f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.41f, 0.62f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.11f, 0.36f, 0.56f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.90f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.70f, 0.88f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.15f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.23f, 0.30f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.84f, 0.86f, 0.89f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.33f, 0.37f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, ImVec2(-1.0f, 28.0f));
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    return pressed;
}

static const char* PrimarySectionLabel() {
    switch (WizardSession::GetPage()) {
        case WizardSession::Page::Bind: return "Flight Controls";
        case WizardSession::Page::Tune: return "Flight Modes";
        case WizardSession::Page::Advanced: return "Advanced";
        case WizardSession::Page::Power: return "Absolute Power";
    }
    return "Section";
}

static void DrawPrimaryNavigation() {
    static bool initialized = false;
    static WizardSession::Page presentedPage = WizardSession::Page::Bind;
    const WizardSession::Page requestedPage = WizardSession::GetPage();
    const bool synchronizeSelection = !initialized || presentedPage != requestedPage;

    if (ImGui::BeginTabBar("PrimaryNavigation", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        auto DrawTab = [&](const char* label, WizardSession::Page page) {
            const ImGuiTabItemFlags flags = synchronizeSelection && requestedPage == page
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label, nullptr, flags)) {
                // While synchronizing an externally selected route, ignore the
                // tab bar's formerly active item until the requested tab is drawn.
                if (!synchronizeSelection && WizardSession::GetPage() != page) {
                    if (WizardSession::GetPage() == WizardSession::Page::Power)
                        PowerModuleUI::CancelTransientInteractions();
                    WizardSession::SelectPage(page);
                }
                ImGui::EndTabItem();
            }
        };

        DrawTab("Flight Controls##Primary", WizardSession::Page::Bind);
        DrawTab("Flight Modes##Primary", WizardSession::Page::Tune);
        DrawTab("Advanced##Primary", WizardSession::Page::Advanced);
        if (PowerModuleUI::Available())
            DrawTab("Power##Primary", WizardSession::Page::Power);
        ImGui::EndTabBar();
    }

    presentedPage = WizardSession::GetPage();
    initialized = true;
}

static void DrawSecondaryNavigation() {
    if (WizardSession::GetPage() == WizardSession::Page::Power) return;
    ImGui::Spacing();
    ImGui::SeparatorText(PrimarySectionLabel());

    int columns = WizardSession::GetPage() == WizardSession::Page::Advanced
        || WizardSession::GetPage() == WizardSession::Page::Tune ? 3 : 2;
    if (!ImGui::BeginTable("SecondaryNavigation", columns,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return;

    if (WizardSession::GetPage() == WizardSession::Page::Bind) {
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Flight Axes (Core)##Secondary",
                WizardSession::GetBindPage() == WizardSession::BindPage::FlightAxes))
            WizardSession::SelectBindPage(WizardSession::BindPage::FlightAxes);
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Ship Buttons##Secondary",
                WizardSession::GetBindPage() == WizardSession::BindPage::ShipButtons))
            WizardSession::SelectBindPage(WizardSession::BindPage::ShipButtons);
    } else if (WizardSession::GetPage() == WizardSession::Page::Tune) {
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Aiming & Combat##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::Aiming))
            WizardSession::SelectTunePage(WizardSession::TunePage::Aiming);
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Camera Look##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::CameraLook))
            WizardSession::SelectTunePage(WizardSession::TunePage::CameraLook);
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Rate Throttle##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::GamepadThrottle))
            WizardSession::SelectTunePage(WizardSession::TunePage::GamepadThrottle);
    } else {
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Macros##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::Macros))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::Macros);
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Plugin Controls##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::PluginControls))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::PluginControls);
        ImGui::TableNextColumn();
        if (DrawSecondaryNavigationButton("Devices##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::Devices))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::Devices);
    }
    ImGui::EndTable();
}

static void DrawActivePage(WizardState& s, bool dirty) {
    if (WizardSession::GetPage() == WizardSession::Page::Power) {
        PowerModuleUI::Draw();
        return;
    }
    if (WizardSession::GetPage() == WizardSession::Page::Advanced)
        WizardUI::DrawProfileManagementPanel(dirty);

    switch (WizardSession::GetRoute()) {
        case WizardSession::Route::BindFlightAxes: WizardUI::DrawAxesTab(s); break;
        case WizardSession::Route::BindShipButtons: WizardUI::DrawButtonsTab(s); break;
        case WizardSession::Route::TuneAiming: WizardUI::DrawAimingTab(s); break;
        case WizardSession::Route::TuneCameraLook: WizardUI::DrawCameraLookTab(s); break;
        case WizardSession::Route::TuneGamepadThrottle: WizardUI::DrawGamepadThrottleTab(s); break;
        case WizardSession::Route::AdvancedMacros: WizardUI::DrawMacrosTab(s); break;
        case WizardSession::Route::AdvancedPluginControls: WizardUI::DrawPluginControls(s); break;
        case WizardSession::Route::AdvancedDevices: WizardUI::DrawDevicesTab(s); break;
    }
}

static void DrawCaptureModal() {
    static bool popupRequested = false;
    if (WizardSession::IsCapturing() && !popupRequested) {
        ImGui::OpenPopup("Capture input");
        popupRequested = true;
    }

    if (!ImGui::BeginPopupModal("Capture input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!WizardSession::IsCapturing()) popupRequested = false;
        return;
    }

    if (!WizardSession::IsCapturing()) {
        popupRequested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const auto& capture = WizardSession::Capture();
    ImGui::Text("Binding: %s", capture.targetLabel.c_str());
    ImGui::Spacing();
    const bool axisCapture = CaptureSlot::IsAxis(capture.targetConfigSlot);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.25f, 1.0f),
        axisCapture ? "Move the desired axis through a clear range."
                    : "Press the desired button or hat direction.");
    ImGui::TextDisabled("Capture is locked to this profile and page.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel capture", ImVec2(140.0f, 0.0f))) {
        WizardSession::CancelCapture();
        popupRequested = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static bool CanCloseWorkbench() {
    PowerModuleUI::CancelTransientInteractions();
    if (PowerModuleUI::Dirty()) {
        WizardSession::RequireCloseResolution(
            "Unsaved Power changes must be saved or discarded before closing.");
        return false;
    }
    return WizardSession::RequestClose();
}

static bool SaveAndCloseWorkbench() {
    PowerModuleUI::CancelTransientInteractions();
    if (WizardSession::HasUnsavedChanges() && !WizardSession::SaveCurrentProfile()) return false;
    if (PowerModuleUI::Dirty() && !PowerModuleUI::Save()) return false;
    WizardSession::CancelPendingClose();
    UIHook::ToggleUI();
    return true;
}

static bool CloseWorkbenchWithoutSaving() {
    PowerModuleUI::CancelTransientInteractions();
    if (WizardSession::HasUnsavedChanges() && !WizardSession::DiscardChanges()) return false;
    if (PowerModuleUI::Dirty()) PowerModuleUI::Discard();
    WizardSession::CancelPendingClose();
    UIHook::ToggleUI();
    return true;
}

static void DrawPendingCloseModal() {
    static bool popupRequested = false;
    if (WizardSession::HasPendingClose() && !popupRequested) {
        ImGui::OpenPopup("Unsaved workbench changes");
        popupRequested = true;
    }
    if (!ImGui::BeginPopupModal("Unsaved workbench changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!WizardSession::HasPendingClose()) popupRequested = false;
        return;
    }

    ImGui::Text("The AbsoluteHOTAS workbench has unsaved changes.");
    ImGui::TextDisabled("Save or discard them before closing the workbench.");
    ImGui::Spacing();
    if (ImGui::Button("Save & Close", ImVec2(130.0f, 0.0f))) {
        if (SaveAndCloseWorkbench()) {
            popupRequested = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close Without Saving", ImVec2(160.0f, 0.0f))) {
        if (CloseWorkbenchWithoutSaving()) {
            popupRequested = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
        WizardSession::CancelPendingClose();
        popupRequested = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- Main Draw ---
void BindingWizard::Draw() {
    static bool s_allDevicesOpened = false;
    if (!s_allDevicesOpened) {
        DeviceManager::OpenAllDevices();
        s_allDevicesOpened = true;
    }

    // A live config reload (from a Save or a profile Import) bumps the generation
    // counter once it has been fully applied. Reset the cached wizard state so
    // LoadCurrentBindings repopulates it from the new config — race-free, because a
    // changed generation guarantees GetConfig() already reflects the reload.
    static uint32_t s_lastConfigGen = ThrottleController::ConfigGeneration();
    const uint32_t configGen = ThrottleController::ConfigGeneration();
    std::string profileToReload;
    if (configGen != s_lastConfigGen) {
        s_lastConfigGen = configGen;
        WizardSession::CancelTransientInteractions();
        // Activation routing can reload the runtime while the editor has unrelated
        // unsaved work. Preserve that working copy; a successful Save has already
        // marked it clean and will take the normal refresh path below.
        if (!WizardSession::HasUnsavedChanges()) {
            profileToReload = WizardConfig::GetEditProfile();
            WizardConfig::GetState() = WizardState{};
        }
    }

    WizardConfig::LoadCurrentBindings();
    if (!profileToReload.empty()) {
        std::string err;
        WizardConfig::LoadProfileForEditing(profileToReload, err);
    }
    WizardSession::UpdateCapture(WizardUI::OnCaptureCommit);
    auto& s = WizardConfig::GetState();
    const bool dirty = WizardSession::HasUnsavedChanges();
    bool powerPage = WizardSession::GetPage() == WizardSession::Page::Power;

    ImGui::SetNextWindowSize(ImVec2(800, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(620, 480), ImVec2(1600, 1200));
    const ImGuiWindowFlags shellFlags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool windowOpen = true;
    static const std::string workbenchTitle =
        std::format("AbsoluteHOTAS {} Binding Workbench", Plugin::VersionString);
    const bool windowVisible = ImGui::Begin(workbenchTitle.c_str(), &windowOpen, shellFlags);
    const bool titleBarCloseRequested = !windowOpen;
    if (!windowVisible) {
        ImGui::End();
        if (titleBarCloseRequested) UIHook::ToggleUI();
        return;
    }

    if (AreGameMenusClosed()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.20f, 1.0f));
        ImGui::TextWrapped("Game not paused. Mouse unavailable. Open the wizard from Starfield's pause menu for full interaction, or use keyboard navigation.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    if (!powerPage) WizardUI::DrawProfileContextBar(dirty);
    DrawPrimaryNavigation();
    powerPage = WizardSession::GetPage() == WizardSession::Page::Power;
    DrawSecondaryNavigation();
    ImGui::Separator();

    constexpr float footerButtonHeight = 36.0f;
    const float footerHeight = footerButtonHeight + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    const float pageHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - footerHeight);
    ImGui::BeginChild("WizardPageHost", ImVec2(0, pageHeight), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    static WizardSession::Route lastRenderedRoute = WizardSession::GetRoute();
    static WizardSession::Page lastRenderedPage = WizardSession::GetPage();
    if (lastRenderedRoute != WizardSession::GetRoute() ||
        lastRenderedPage != WizardSession::GetPage()) {
        ImGui::SetScrollY(0.0f);
        lastRenderedRoute = WizardSession::GetRoute();
        lastRenderedPage = WizardSession::GetPage();
    }
    DrawActivePage(s, dirty);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::BeginTable("FooterActions", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
        if (ImGui::Button("Save & Apply", ImVec2(-1.0f, footerButtonHeight))) {
            if (powerPage) (void)PowerModuleUI::Save();
            else SaveCurrentProfile();
        }
        ImGui::PopStyleColor(3);
        ImGui::TableNextColumn();
        if (ImGui::Button("Save & Close", ImVec2(-1.0f, footerButtonHeight)))
            SaveAndCloseWorkbench();
        ImGui::TableNextColumn();
        if (ImGui::Button("Close Without Saving", ImVec2(-1.0f, footerButtonHeight)))
            CloseWorkbenchWithoutSaving();
        ImGui::EndTable();
    }

    const auto& status = WizardSession::GetStatus();
    const bool footerDirty = powerPage ? PowerModuleUI::Dirty()
                                       : WizardSession::HasUnsavedChanges();
    if (powerPage && footerDirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Unsaved: Absolute Power");
    } else if (powerPage) {
        ImGui::TextDisabled("%.*s", static_cast<int>(PowerModuleUI::StatusText().size()),
                            PowerModuleUI::StatusText().data());
    } else if (status.kind == WizardSession::StatusKind::Error && !status.message.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", status.message.c_str());
    } else if (footerDirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Unsaved: %s", VisibleProfileName(WizardConfig::GetEditProfile()).c_str());
    } else if (!status.message.empty()) {
        const ImVec4 color = status.kind == WizardSession::StatusKind::Warning
            ? ImVec4(1.0f, 0.72f, 0.20f, 1.0f)
            : ImVec4(0.45f, 0.9f, 0.55f, 1.0f);
        ImGui::TextColored(color, "%s", status.message.c_str());
    } else {
        ImGui::TextDisabled("Saves the editing profile and reloads live.");
    }

    DrawCaptureModal();
    DrawPendingCloseModal();
    ImGui::End();
    if (titleBarCloseRequested) UIHook::ToggleUI();
}

void BindingWizard::Initialize() {
    WizardSession::Initialize();
    PowerModuleUI::Initialize();
    UIHook::SetDrawCallback(&BindingWizard::Draw);
    UIHook::SetCloseGuardCallback(&CanCloseWorkbench);
    WizardUI::Log("BindingWizard registered with UIHook.");
}

#pragma once

#include <string>

struct WizardState;

// Internal presentation boundary for the binding workbench. WizardSession owns
// workflow state; these functions only render or translate UI actions.
namespace WizardUI {
    void Log(const std::string& message);

    void OnCaptureCommit(int slot, const char* binding);

    void DrawBindingRow(const char* label, std::string& binding, int captureSlot,
                        bool isAxis, bool* invert = nullptr);
    void DrawBindingSummaryRow(const char* label, const std::string& binding,
                               bool* invert = nullptr);

    void DrawAxesTab(WizardState& state);
    void DrawAimingTab(WizardState& state);
    void DrawCameraLookTab(WizardState& state);
    void DrawGamepadThrottleTab(WizardState& state);
    void DrawButtonsTab(WizardState& state);
    void DrawDevicesTab(WizardState& state);
    void DrawPluginControls(WizardState& state);
    void DrawMacrosTab(WizardState& state);

    void DrawProfileContextBar(bool dirty);
    void DrawProfileManagementPanel(bool dirty);
}

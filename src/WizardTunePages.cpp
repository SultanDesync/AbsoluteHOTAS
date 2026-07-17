#include "PCH.h"

#include "WizardUI.h"

#include "WizardConfig.h"
#include "WizardDefs.h"

#include <imgui.h>

namespace WizardUI {


void DrawAimingTab(WizardState& s) {
    ImGui::TextWrapped("Controls how the aiming reticle and ship steering interact. Enable the aim system, then choose a mode below.");

    ImGui::Checkbox("Enable Aim System", &s.sourceObjectAim);
    if (!s.sourceObjectAim) {
        ImGui::TextDisabled("Aim/reticle injection is off. The bound flight axes still steer the ship directly.");
        return;
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    bool hasAimAxes = (s.aimAxisBindings[0] != "(unbound)") || (s.aimAxisBindings[1] != "(unbound)");
    if (hasAimAxes) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Mode: Independent Aim & Steer");
        ImGui::TextWrapped("The flight stick controls ship rotation directly. The bound aim axes below independently drive the weapon reticle. Clear aim axes to switch to Aim-Driven Steering.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Mode: Aim-Driven Steering");
        ImGui::TextWrapped("The flight stick drives both the aiming reticle and ship steering through the mouse accumulator pathway. Bind aim axes below to switch to Independent Aim & Steer.");
        ImGui::Spacing();
        ImGui::PushItemWidth(120);
        ImGui::SliderFloat("Steering Sensitivity", &s.aimSensitivity, 0.1f, 3.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Scales flight stick input to reticle/steering");
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Aim axis bindings
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aim Axis Bindings");
    ImGui::TextWrapped("Bind a second analog input (e.g., throttle thumbstick) to independently drive the aiming reticle.");
    ImGui::Spacing();

    for (int i = 0; i < kNumAimAxisSlots; i++) {
        ImGui::PushID(6000 + i);
        DrawBindingRow(kAimAxisSlots[i].label, s.aimAxisBindings[i],
                       CaptureSlot::kAimAxisBase + i, true, &s.aimAxisInvert[i]);
        ImGui::SetNextItemWidth(std::min(180.0f, ImGui::GetContentRegionAvail().x * 0.4f));
        ImGui::SliderFloat("Sens", &s.aimAxisSensitivity[i], 0.1f, 3.0f, "%.2f");
        if (i < kNumAimAxisSlots - 1) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Indent(180);
    ImGui::PushItemWidth(120);
    ImGui::SliderFloat("Smoothing", &s.aimSmoothing, 0.0f, 0.98f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Low-res sensor filter (0=off)");
    ImGui::Unindent(180);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Digital aim
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Digital Aim Override (5-Way)");
    ImGui::TextWrapped("Bind buttons to move the aiming reticle like a virtual cursor. Hold a direction to accumulate position. Release to hold. Center resets to (0,0).");
    ImGui::Spacing();

    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        ImGui::PushID(7000 + i);
        DrawBindingRow(kDigitalAimSlots[i].label, s.digitalAimBindings[i], CaptureSlot::kDigitalAimBase + i, false);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::PushItemWidth(120);
    ImGui::SliderFloat("Aim Speed", &s.digitalAimValue, 0.1f, 3.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Travel speed per second");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Toggle aim mode
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aim Mode Toggle");
    ImGui::TextWrapped("Bind a button to toggle between Aim-Driven Steering and Independent Aim at runtime. Only useful when aim axes are bound.");
    ImGui::Spacing();
    ImGui::PushID(7005);
    DrawBindingRow("Toggle Mode", s.toggleAimModeBinding, CaptureSlot::kToggleAimMode, false);
    ImGui::PopID();
}

void DrawGamepadThrottleTab(WizardState& s) {
    ImGui::TextWrapped("Works like Starfield's gamepad throttle: push or pull a self-centering stick to change throttle, then return it to center to hold the current setting.");
    ImGui::Checkbox("Use gamepad-style throttle", &s.accumulatorThrottle);
    if (s.accumulatorThrottle) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "(Rate)");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "Stick deflection controls throttle speed, not position.");
            ImGui::Spacing();
            ImGui::PushItemWidth(180);
            ImGui::SliderFloat("Ramp Rate##accRate", &s.accumulatorRate, 0.1f, 5.0f, "%.1f units/s");
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "At full deflection");
            ImGui::SliderFloat("Decay Rate##accDecay", &s.accumulatorDecay, 0.0f, 3.0f, "%.1f units/s");
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "At neutral (0=hold)");
            ImGui::SliderFloat("Rev. Gate##accGate", &s.reverseGateVelocity, 0.0f, 50.0f, "%.0f m/s");
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Reverse below this speed");
            ImGui::PopItemWidth();
            ImGui::Spacing();
            ImGui::TextWrapped("Push forward to accelerate. Pull back to decelerate; at zero throttle, pulling further triggers reverse braking.");

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Checkbox("Pilot Turn Assist", &s.accumulatorTurnAssist);
            if (s.accumulatorTurnAssist) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "Lets the game's native turn-rate assist slow your ship during hard turns. Throttle resumes when you stop turning.");

                ImGui::PushItemWidth(180);
                const char* modeLabels[] = { "Always", "Hold", "Toggle" };
                ImGui::Combo("Activation##turnMode", &s.turnAssistMode, modeLabels, 3);
                ImGui::PopItemWidth();
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f),
                    s.turnAssistMode == 0 ? "Active whenever turning" :
                    s.turnAssistMode == 1 ? "Active while button is held" : "Button toggles on/off");

                if (s.turnAssistMode > 0) {
                    ImGui::PushID(8000);
                    DrawBindingRow("Assist Button", s.turnAssistBinding, CaptureSlot::kTurnAssistBtn, false);
                    ImGui::PopID();
                }
            }
    }
}

}  // namespace WizardUI

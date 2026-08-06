#include "PCH.h"

#include "WizardUI.h"

#include "BindingRef.h"
#include "DeviceManager.h"
#include "HeadTracking.h"
#include "WizardConfig.h"
#include "WizardDefs.h"

#include <imgui.h>

namespace WizardUI {

namespace {

struct HeadLookPreviewValue {
    float degrees = 0.0F;
    bool active = false;
    const char* source = "No live source";
};

HeadLookPreviewValue ReadHeadLookPreview(const WizardState& s, int axisIndex)
{
    const float maximum = std::clamp(s.headLookMaxDegrees[axisIndex], 1.0F, 180.0F);
    const float scale = s.headLookSensitivity[axisIndex];
    const float direction = s.headLookInvert[axisIndex] ? -1.0F : 1.0F;
    const BindingRef binding = ParseBindingRef(
        s.headLookAxisBindings[axisIndex].c_str(), -1);

    if (binding.value >= 0x30 && binding.value <= 0x37) {
        int deviceIndex = binding.deviceIndex;
        if (deviceIndex < 0 && !binding.deviceName.empty())
            deviceIndex = DeviceManager::ResolveByName(binding.deviceName);
        else if (deviceIndex < 0 && DeviceManager::GetDeviceCount() > 0)
            deviceIndex = 0;

        const auto* state = deviceIndex >= 0
            ? DeviceManager::GetCachedState(deviceIndex) : nullptr;
        if (!state) return { 0.0F, false, "Waiting for joystick" };

        long minimum = 0;
        long maximumRaw = 65535;
        const auto calibration = s.calibData.find((deviceIndex << 8) | binding.value);
        if (calibration != s.calibData.end() &&
            calibration->second.second > calibration->second.first) {
            minimum = calibration->second.first;
            maximumRaw = calibration->second.second;
        }
        const float center = (static_cast<float>(minimum) + maximumRaw) * 0.5F;
        const float halfRange = std::max(
            1.0F, (static_cast<float>(maximumRaw) - minimum) * 0.5F);
        const long raw = DeviceManager::GetAxisFromState(state, binding.value);
        float normalized = std::clamp((static_cast<float>(raw) - center) / halfRange,
                                      -1.0F, 1.0F);
        const float deadzone = std::clamp(s.headLookJoystickDeadzone, 0.0F, 0.95F);
        const float magnitude = std::abs(normalized);
        if (magnitude <= deadzone) {
            normalized = 0.0F;
        } else {
            normalized = std::copysign(
                (magnitude - deadzone) / (1.0F - deadzone), normalized);
        }
        return {
            std::clamp(normalized * maximum * scale * direction, -maximum, maximum),
            true,
            "Joystick override"
        };
    }

    if (!s.headLookOpenTrackEnabled)
        return { 0.0F, false, "OpenTrack disabled" };

    const auto live = HeadTracking::GetLiveInput();
    if (!live.trackerActive)
        return { 0.0F, false, "Waiting for OpenTrack" };

    float degrees = live.trackerDegrees[axisIndex] * scale * direction;
    const float deadzone = std::clamp(s.headLookDeadzoneDegrees, 0.0F, 20.0F);
    const float magnitude = std::abs(degrees);
    degrees = magnitude <= deadzone
        ? 0.0F
        : std::copysign(std::min(magnitude - deadzone, maximum), degrees);
    return { degrees, true, "OpenTrack" };
}

void DrawHeadLookAxisGraph(const WizardState& s, int axisIndex)
{
    const float maximum = std::clamp(s.headLookMaxDegrees[axisIndex], 1.0F, 180.0F);
    const HeadLookPreviewValue preview = ReadHeadLookPreview(s, axisIndex);
    const float width = std::min(520.0F, std::max(180.0F, ImGui::GetContentRegionAvail().x));
    constexpr float height = 24.0F;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        IM_COL32(32, 43, 52, 230), 4.0F);
    draw->AddRectFilled(pos, ImVec2(pos.x + width * 0.5F, pos.y + height),
        IM_COL32(45, 75, 92, 120), 4.0F,
        ImDrawFlags_RoundCornersLeft);
    draw->AddLine(ImVec2(pos.x + width * 0.5F, pos.y),
        ImVec2(pos.x + width * 0.5F, pos.y + height),
        IM_COL32(80, 220, 240, 230), 2.0F);

    if (preview.active && s.headLookAxisEnabled[axisIndex]) {
        const float normalized = std::clamp(preview.degrees / maximum, -1.0F, 1.0F);
        const float liveX = (0.5F + normalized * 0.5F) * width;
        draw->AddLine(ImVec2(pos.x + liveX, pos.y - 2.0F),
            ImVec2(pos.x + liveX, pos.y + height + 2.0F),
            IM_COL32(255, 225, 65, 255), 3.0F);
    }
    draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
        IM_COL32(125, 145, 155, 220), 4.0F);
    ImGui::Dummy(ImVec2(width, height));

    if (!s.headLookAxisEnabled[axisIndex]) {
        ImGui::TextDisabled("LIVE OUTPUT  /  Axis disabled");
    } else if (preview.active) {
        ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.28F, 1.0F),
            "LIVE OUTPUT  %+.1f deg", preview.degrees);
        ImGui::SameLine();
        ImGui::TextDisabled("/ +/-%.0f deg / %s", maximum, preview.source);
    } else {
        ImGui::TextDisabled("LIVE OUTPUT  /  %s", preview.source);
    }
}

} // namespace


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

void DrawCameraLookTab(WizardState& s) {
    ImGui::TextWrapped(
        "Tunes cockpit camera rotation. OpenTrack supplies head pose from any supported "
        "tracker; optional joystick axes override yaw, pitch, or roll independently.");

    ImGui::Checkbox("Enable camera look", &s.headLookEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Use OpenTrack input", &s.headLookOpenTrackEnabled);
    if (!s.headLookEnabled) {
        ImGui::TextDisabled(
            "Camera look is parked. Settings and bindings can still be prepared before enabling it.");
    } else if (!s.headLookOpenTrackEnabled) {
        ImGui::TextDisabled(
            "Joystick-only mode: bind at least one look axis below.");
    } else {
        ImGui::TextDisabled(
            "OpenTrack must be started with freetrack 2.0 Enhanced output.");
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Runtime Controls");
    ImGui::TextWrapped(
        "Toggle releases or restores camera authority. Recenter captures the current "
        "OpenTrack pose; in joystick-only mode it clears the filtered offset.");
    ImGui::Spacing();
    DrawBindingRow("Toggle Camera Look", s.headLookToggleBinding,
                   CaptureSlot::kHeadLookToggle, false);
    DrawBindingRow("Recenter Camera Look", s.headLookRecenterBinding,
                   CaptureSlot::kHeadLookRecenter, false);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Axis Tuning");
    ImGui::TextWrapped(
        "Leave a joystick axis unbound to use OpenTrack for that component. A bound "
        "axis becomes an absolute camera-angle override, centered at the stick's neutral position.");

    static const char* descriptions[kNumHeadLookAxisSlots] = {
        "Horizontal look", "Vertical look", "Head tilt"
    };
    for (int i = 0; i < kNumHeadLookAxisSlots; ++i) {
        ImGui::PushID(7100 + i);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s",
                           kHeadLookAxisSlots[i].label);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", descriptions[i]);
        ImGui::SameLine();
        ImGui::Checkbox("Use Axis", &s.headLookAxisEnabled[i]);
        ImGui::BeginDisabled(!s.headLookAxisEnabled[i]);
        DrawBindingRow("Joystick Override", s.headLookAxisBindings[i],
                       CaptureSlot::kHeadLookAxisBase + i, true,
                       &s.headLookInvert[i]);

        if (ImGui::BeginTable("HeadLookAxisTuning", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("Sensitivity", &s.headLookSensitivity[i],
                               0.1f, 5.0f, "%.2f");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("Maximum Angle", &s.headLookMaxDegrees[i],
                               1.0f, 180.0f, "%.0f deg");
            ImGui::EndTable();
        }
        DrawHeadLookAxisGraph(s, i);
        ImGui::EndDisabled();
        if (i + 1 < kNumHeadLookAxisSlots) ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Filtering");
    if (ImGui::BeginTable("HeadLookFiltering", 3,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("Tracker Deadzone", &s.headLookDeadzoneDegrees,
                           0.0f, 10.0f, "%.1f deg");
        ImGui::TableNextColumn();
        float joystickDeadzonePercent = s.headLookJoystickDeadzone * 100.0f;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("Joystick Deadzone", &joystickDeadzonePercent,
                               0.0f, 30.0f, "%.0f%%"))
            s.headLookJoystickDeadzone = joystickDeadzonePercent / 100.0f;
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("Smoothing", &s.headLookSmoothing,
                           0.0f, 0.95f, "%.2f");
        ImGui::EndTable();
    }

    if (ImGui::Button("Reset Camera Tuning")) {
        const float defaults[kNumHeadLookAxisSlots] = { 85.0f, 60.0f, 45.0f };
        for (int i = 0; i < kNumHeadLookAxisSlots; ++i) {
            s.headLookAxisEnabled[i] = true;
            s.headLookInvert[i] = false;
            s.headLookSensitivity[i] = 1.0f;
            s.headLookMaxDegrees[i] = defaults[i];
        }
        s.headLookDeadzoneDegrees = 0.0f;
        s.headLookJoystickDeadzone = 0.08f;
        s.headLookSmoothing = 0.15f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Bindings and enable switches are preserved.");
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

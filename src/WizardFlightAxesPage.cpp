#include "PCH.h"

#include "WizardUI.h"

#include "BindingRef.h"
#include "DeviceManager.h"
#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {


// --- Throttle range visualization ---
static void DrawThrottleRangeGraph(WizardState& s, float barWidth, float barHeight) {
    float sat = s.axisSaturation[0];
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 colDead = IM_COL32(80, 30, 30, 200);
    ImU32 colActive = IM_COL32(50, 200, 80, 220);

    dl->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), colDead, 3.0f);

    // Resolve throttle device
    static std::string s_lastThrottleBinding;
    static int s_cachedThrottleDevIdx = -1;
    static int s_cachedThrottleUsage = -1;
    if (s.axisBindings[0] != s_lastThrottleBinding) {
        s_lastThrottleBinding = s.axisBindings[0];
        BindingRef tRef = ParseBindingRef(s.axisBindings[0].c_str(), -1);
        s_cachedThrottleDevIdx = tRef.deviceIndex;
        s_cachedThrottleUsage = tRef.value;
        if (s_cachedThrottleDevIdx < 0 && tRef.IsValid() && s_cachedThrottleUsage > 0) {
            if (tRef.HasDevice()) {
                s_cachedThrottleDevIdx = DeviceManager::ResolveByName(tRef.deviceName);
            } else if (DeviceManager::GetDeviceCount() > 0) {
                s_cachedThrottleDevIdx = 0;
            }
        }
    }
    int tDevIdx = s_cachedThrottleDevIdx;
    int tUsage = s_cachedThrottleUsage;

    auto NormThrottleRaw = [&](long rawVal) -> float {
        if (tDevIdx >= 0) {
            int calibKey = (tDevIdx << 8) | tUsage;
            auto calibIt = s.calibData.find(calibKey);
            if (calibIt != s.calibData.end()) {
                long cmin = calibIt->second.first;
                long cmax = calibIt->second.second;
                long crange = cmax - cmin;
                if (crange > 0) return std::clamp((float)(rawVal - cmin) / (float)crange, 0.0f, 1.0f);
            }
        }
        return std::clamp(rawVal / 65535.0f, 0.0f, 1.0f);
    };

    // The zone values below are stored in the throttle's logical coordinate
    // system, but the live HID sample is still in hardware coordinates. Mirror
    // only that live sample when inverted, exactly as NormalizeAxis does at
    // runtime. Calibration capture must continue storing the untouched raw value.
    auto NormThrottleLive = [&](long rawVal) -> float {
        if (s.axisInvert[0]) {
            long axisMin = 0;
            long axisMax = 65535;
            if (tDevIdx >= 0) {
                auto calibIt = s.calibData.find((tDevIdx << 8) | tUsage);
                if (calibIt != s.calibData.end() && calibIt->second.second > calibIt->second.first) {
                    axisMin = calibIt->second.first;
                    axisMax = calibIt->second.second;
                }
            }
            rawVal = axisMin + axisMax - std::clamp(rawVal, axisMin, axisMax);
        }
        return NormThrottleRaw(rawVal);
    };

    // Compute center and zone norms
    float centerNorm = NormThrottleRaw(s.detentCenter);
    float dzNorm = (float)s.detentDeadzone / 65535.0f;
    float rzNorm = 0.0f, rzDzNorm = 0.0f, bzNorm = 0.0f, bzDzNorm = 0.0f;

    if (tDevIdx >= 0) {
        int calibKey = (tDevIdx << 8) | tUsage;
        auto calibIt = s.calibData.find(calibKey);
        if (calibIt != s.calibData.end()) {
            long crange = calibIt->second.second - calibIt->second.first;
            if (crange > 0) {
                dzNorm = (float)s.detentDeadzone / (float)crange;
                if (s.unipolarReverse) rzDzNorm = (float)s.reverseZoneDeadzone / (float)crange;
                if (s.boostZone) bzDzNorm = (float)s.boostZoneDeadzone / (float)crange;
            }
        }
    }

    if (s.boostZone) {
        bzNorm = NormThrottleRaw(s.boostZoneCenter);
        if (bzDzNorm == 0.0f) bzDzNorm = (float)s.boostZoneDeadzone / 65535.0f;
    }

    if (s.unipolarReverse) {
        rzNorm = NormThrottleRaw(s.reverseZoneCenter);
        if (rzDzNorm == 0.0f) rzDzNorm = (float)s.reverseZoneDeadzone / 65535.0f;

        // Dead stop zone (amber)
        float dsLeft = std::max(0.0f, rzNorm - rzDzNorm) * barWidth;
        float dsRight = std::min(1.0f, rzNorm + rzDzNorm) * barWidth;
        dl->AddRectFilled(ImVec2(pos.x + dsLeft, pos.y), ImVec2(pos.x + dsRight, pos.y + barHeight),
            IM_COL32(200, 150, 30, 200), 0.0f);

        // 0%→50% ramp (green)
        float ramp1Left = std::min(1.0f, rzNorm + rzDzNorm) * barWidth;
        float ramp1Right = std::max(0.0f, centerNorm - dzNorm) * barWidth;
        if (ramp1Right > ramp1Left)
            dl->AddRectFilled(ImVec2(pos.x + ramp1Left, pos.y), ImVec2(pos.x + ramp1Right, pos.y + barHeight), colActive, 0.0f);

        // 50% cruise plateau (orange)
        if (dzNorm > 0.001f) {
            float cruiseLeft = std::max(0.0f, centerNorm - dzNorm) * barWidth;
            float cruiseRight = std::min(1.0f, centerNorm + dzNorm) * barWidth;
            dl->AddRectFilled(ImVec2(pos.x + cruiseLeft, pos.y), ImVec2(pos.x + cruiseRight, pos.y + barHeight),
                IM_COL32(220, 130, 30, 200), 0.0f);
        }

        // 50%→100% ramp (green)
        float ramp2Left = std::min(1.0f, centerNorm + dzNorm) * barWidth;
        float ramp2Right = s.boostZone ? std::max(0.0f, bzNorm - bzDzNorm) * barWidth : barWidth;
        if (ramp2Right > ramp2Left)
            dl->AddRectFilled(ImVec2(pos.x + ramp2Left, pos.y), ImVec2(pos.x + ramp2Right, pos.y + barHeight), colActive, 0.0f);

        // Reverse zone center marker (red line)
        float rzX = rzNorm * barWidth;
        dl->AddLine(ImVec2(pos.x + rzX, pos.y), ImVec2(pos.x + rzX, pos.y + barHeight), IM_COL32(255, 80, 80, 220), 2.0f);
    } else {
        // Standard unipolar
        float idleEnd = s.idlePlateau * barWidth;
        float satEnd = s.boostZone ? std::max(0.0f, bzNorm - bzDzNorm) * barWidth : sat * barWidth;
        if (satEnd > idleEnd)
            dl->AddRectFilled(ImVec2(pos.x + idleEnd, pos.y), ImVec2(pos.x + satEnd, pos.y + barHeight), colActive, 3.0f);

        // Center deadzone (orange)
        if (dzNorm > 0.001f) {
            float dzLeft = std::max(0.0f, centerNorm - dzNorm) * barWidth;
            float dzRight = std::min(1.0f, centerNorm + dzNorm) * barWidth;
            dl->AddRectFilled(ImVec2(pos.x + dzLeft, pos.y), ImVec2(pos.x + dzRight, pos.y + barHeight),
                IM_COL32(200, 100, 30, 140), 0.0f);
        }
    }

    // Boost zone
    if (s.boostZone) {
        if (bzDzNorm > 0.001f) {
            float platLeft = std::max(0.0f, bzNorm - bzDzNorm) * barWidth;
            float platRight = std::min(1.0f, bzNorm + bzDzNorm) * barWidth;
            dl->AddRectFilled(ImVec2(pos.x + platLeft, pos.y), ImVec2(pos.x + platRight, pos.y + barHeight),
                IM_COL32(210, 220, 235, 200), 0.0f);
        }
        float boostLeft = std::min(1.0f, bzNorm + bzDzNorm) * barWidth;
        dl->AddRectFilled(ImVec2(pos.x + boostLeft, pos.y), ImVec2(pos.x + barWidth, pos.y + barHeight),
            IM_COL32(180, 50, 220, 200), 0.0f);
        float bzX = bzNorm * barWidth;
        dl->AddLine(ImVec2(pos.x + bzX, pos.y), ImVec2(pos.x + bzX, pos.y + barHeight), IM_COL32(255, 100, 255, 230), 2.0f);
    }

    // Center marker (cyan)
    float centerX = centerNorm * barWidth;
    dl->AddLine(ImVec2(pos.x + centerX, pos.y), ImVec2(pos.x + centerX, pos.y + barHeight), IM_COL32(80, 220, 240, 220), 2.0f);

    // Live axis position (yellow marker)
    if (tDevIdx >= 0) {
        const auto* st = DeviceManager::GetCachedState(tDevIdx);
        if (st) {
            long rawVal = DeviceManager::GetAxisFromState(st, tUsage);
            float liveX = NormThrottleLive(rawVal) * barWidth;
            dl->AddLine(ImVec2(pos.x + liveX, pos.y - 1), ImVec2(pos.x + liveX, pos.y + barHeight + 1),
                IM_COL32(255, 220, 50, 255), 2.0f);

            if (s.calibratingCenter) s.detentCenter = rawVal;
            if (s.calibratingReverseZone) s.reverseZoneCenter = rawVal;
            if (s.calibratingBoostZone) s.boostZoneCenter = rawVal;
        }
    }

    // Border + dummy
    dl->AddRect(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), IM_COL32(120, 120, 120, 180), 3.0f);
    ImGui::Dummy(ImVec2(barWidth, barHeight));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%.0f%%", sat * 100.0f);
}

// --- Throttle calibration controls ---
static void DrawThrottleCalibrationPanel(WizardState& s) {
    ImGui::PushItemWidth(120);
    ImGui::SliderFloat("Idle Zone", &s.idlePlateau, 0.0f, 0.20f, "%.2f");
    s.idlePlateau = std::clamp(s.idlePlateau, 0.0f, 0.20f);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Bottom dead zone");

    ImGui::Checkbox("Symmetrical Deadzones", &s.symmetricalThrottleDz);
    if (s.symmetricalThrottleDz) {
        s.axisSaturation[0] = 1.0f - s.idlePlateau;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "(Sat locked to %.0f%%)", s.axisSaturation[0] * 100.0f);
    }

    // Set Center
    if (s.calibratingCenter) {
        if (ImGui::Button("Done##center")) {
            s.calibratingCenter = false;
            Log("Center set to: " + std::to_string(s.detentCenter));
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Move throttle to center... %ld", s.detentCenter);
    } else {
        if (ImGui::Button("Set Center")) s.calibratingCenter = true;
        ImGui::SameLine();
        ImGui::Text("Center: %ld", s.detentCenter);
    }

    // Center deadzone slider
    ImGui::PushItemWidth(120);
    float dzPct = (float)s.detentDeadzone / 65535.0f * 100.0f;
    if (ImGui::SliderFloat("Center Deadzone", &dzPct, 0.0f, 10.0f, "%.1f%%"))
        s.detentDeadzone = (long)(dzPct / 100.0f * 65535.0f);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Around center (%ld raw)", s.detentDeadzone);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Reverse Zone
    ImGui::Checkbox("Reverse Zone", &s.unipolarReverse);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), s.unipolarReverse ? "Bottom of axis = reverse thrust" : "Off");

    if (s.unipolarReverse) {
        if (s.calibratingReverseZone) {
            if (ImGui::Button("Done##revzone")) {
                s.calibratingReverseZone = false;
                Log("Reverse zone set to: " + std::to_string(s.reverseZoneCenter));
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Move throttle to zero-thrust position... %ld", s.reverseZoneCenter);
        } else {
            if (ImGui::Button("Set Zero-Thrust")) s.calibratingReverseZone = true;
            ImGui::SameLine();
            ImGui::Text("Zero-Thrust: %ld", s.reverseZoneCenter);
        }

        ImGui::PushItemWidth(120);
        float rzDzPct = (float)s.reverseZoneDeadzone / 65535.0f * 100.0f;
        if (ImGui::SliderFloat("Dead Stop Range", &rzDzPct, 0.0f, 15.0f, "%.1f%%"))
            s.reverseZoneDeadzone = (long)(rzDzPct / 100.0f * 65535.0f);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Width of dead-stop range (%ld raw)", s.reverseZoneDeadzone);
    }

    ImGui::Spacing();

    // Boost Zone
    ImGui::Checkbox("Boost Zone", &s.boostZone);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), s.boostZone ? "Top of axis = fire boosters" : "Off");

    if (s.boostZone) {
        if (s.calibratingBoostZone) {
            if (ImGui::Button("Done##boostzone")) {
                s.calibratingBoostZone = false;
                Log("Boost zone set to: " + std::to_string(s.boostZoneCenter));
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Move throttle to boost position... %ld", s.boostZoneCenter);
        } else {
            if (ImGui::Button("Set Boost")) s.calibratingBoostZone = true;
            ImGui::SameLine();
            ImGui::Text("Boost: %ld", s.boostZoneCenter);
        }

        ImGui::PushItemWidth(120);
        float bzDzPct = (float)s.boostZoneDeadzone / 65535.0f * 100.0f;
        if (ImGui::SliderFloat("100%% Plateau", &bzDzPct, 0.0f, 15.0f, "%.1f%%"))
            s.boostZoneDeadzone = (long)(bzDzPct / 100.0f * 65535.0f);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Flat 100%% before boost (%ld raw)", s.boostZoneDeadzone);
    }
}

// Reverse is one capability with three hardware strategies. Keep them together in
// the main binding path so users do not have to discover a dedicated axis, a digital
// button, and a throttle zone in three different parts of the wizard.
static void DrawReverseSetup(WizardState& s) {
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Reverse", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Indent(12.0f);
    ImGui::TextWrapped("Choose the reverse control that fits your hardware. Most users should bind a button; a dedicated axis is only useful when you have a spare slider or lever.");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Hold a button (recommended)");
    ImGui::PushID("reverseButton");
    DrawBindingRow("Reverse button", s.digitalAxisBindings[0], CaptureSlot::kDigitalAxisBase, false);
    ImGui::PopID();
    ImGui::TextDisabled("Hold to brake to zero and continue into reverse.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Use the bottom of the throttle axis");
    ImGui::Checkbox("Enable throttle reverse zone##reverseSetup", &s.unipolarReverse);
    ImGui::TextDisabled("Pull below the zero-thrust point to reverse; push above it for forward thrust.");
    if (s.unipolarReverse) {
        ImGui::Spacing();
        DrawThrottleRangeGraph(s, 300.0f, 14.0f);
        if (s.calibratingReverseZone) {
            if (ImGui::Button("Done##reverseSetup")) {
                s.calibratingReverseZone = false;
                Log("Reverse zone set to: " + std::to_string(s.reverseZoneCenter));
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                "Move throttle to zero thrust... %ld", s.reverseZoneCenter);
        } else {
            if (ImGui::Button("Set Zero-Thrust##reverseSetup")) s.calibratingReverseZone = true;
            ImGui::SameLine();
            ImGui::Text("Zero-Thrust: %ld", s.reverseZoneCenter);
        }
        ImGui::TextDisabled("Fine-tune the dead-stop range under Tune > Flight Axes.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Use a dedicated analog control");
    ImGui::PushID("reverseAxis");
    DrawBindingRow("Reverse axis", s.axisBindings[kNumAxisSlots - 1], kNumAxisSlots - 1, true);
    ImGui::PopID();
    ImGui::TextDisabled("A bound reverse button takes precedence over this axis.");

    ImGui::Unindent(12.0f);
}

// Hats and buttons are a normal fallback when a controller does not provide enough
// analog axes. Keep these assignments beside the analog flight axes rather than
// presenting them as an advanced plugin feature.
static void DrawButtonBasedAxes(WizardState& s) {
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Button-based axes (hats and buttons)", ImGuiTreeNodeFlags_None)) return;

    static constexpr const char* kLabels[] = {
        "Roll left", "Roll right",
        "Strafe left", "Strafe right",
        "Strafe up", "Strafe down"
    };

    ImGui::Indent(12.0f);
    ImGui::TextWrapped("Use buttons or a hat switch for roll or strafe when you do not have enough analog axes.");
    ImGui::Spacing();
    for (int i = 1; i < kNumDigitalAxisSlots; ++i) {
        ImGui::PushID(4000 + i);
        DrawBindingRow(kLabels[i - 1], s.digitalAxisBindings[i], CaptureSlot::kDigitalAxisBase + i, false);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushItemWidth(120.0f);
    ImGui::SliderFloat("Roll strength", &s.digitalRollValue, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Strafe strength", &s.digitalStrafeValue, 0.0f, 1.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Sets the axis output while a bound button is held.");
    ImGui::Unindent(12.0f);
}

// --- Tab: Devices ---


static void DrawMouseSteeringOptions(WizardState& s) {
    ImGui::Indent(180.0f);
    ImGui::Checkbox("Alignment assist", &s.alignmentAssist);
    if (s.alignmentAssist) {
        ImGui::TextDisabled("Gently centers steering when the mouse is idle near center.");
        ImGui::PushItemWidth(180.0f);
        ImGui::SliderFloat("Assist radius##mouseSteering", &s.alignmentRadius, 1.0f, 200.0f, "%.0f units");
        int idleMs = s.alignmentIdleMs;
        if (ImGui::SliderInt("Idle time##mouseSteering", &idleMs, 10, 500, "%d ms")) s.alignmentIdleMs = idleMs;
        ImGui::SliderFloat("Centering speed##mouseSteering", &s.alignmentDecayRate, 0.5f, 30.0f, "%.1f");
        ImGui::PopItemWidth();
    }
    ImGui::Unindent(180.0f);
}

static void DrawWizardAccessSetup(WizardState& s) {
    ImGui::SeparatorText("Wizard access");
    ImGui::TextWrapped("Choose a controller button for one-touch access to this workbench. Ctrl+Alt+B always remains available as the keyboard recovery shortcut.");
    if (s.buttonBindings[kToggleWizardButtonSlot] == "(unbound)") {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
            "Recommended: set this now so you do not need to remember the three-key shortcut.");
    }
    ImGui::Spacing();
    DrawBindingRow("Open / close wizard", s.buttonBindings[kToggleWizardButtonSlot],
                   CaptureSlot::kButtonBase + kToggleWizardButtonSlot, false);
    ImGui::Spacing();
    ImGui::SeparatorText("Flight input");
}

void DrawAxesTab(WizardState& s, bool tuningOnly) {
    if (!tuningOnly) {
        DrawWizardAccessSetup(s);
        ImGui::Checkbox("Axis injection enabled", &s.axisInjectionEnabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls flight-axis memory injection; button and macro outputs remain active.");
    } else {
        ImGui::TextWrapped("Adjust response, deadzones, saturation, and throttle behavior for the axes already bound under Bind Controls.");
    }

    for (int i = 0; i < kNumAxisSlots; i++) {
        if (!tuningOnly && i == kNumAxisSlots - 1) continue;  // grouped under Reverse below
        if (!tuningOnly && i == 1) {
            ImGui::Spacing();
            ImGui::SeparatorText("Steering input");
            ImGui::Checkbox("Mouse steering (HOSAM)", &s.hosamMode);
            ImGui::SameLine();
            ImGui::TextDisabled(s.hosamMode
                ? "Mouse controls steering; saved Pitch/Yaw bindings are inactive"
                : "Use the bound Pitch and Yaw axes below");
            if (s.hosamMode) DrawMouseSteeringOptions(s);
            const bool hasAimAxes = s.aimAxisBindings[0] != "(unbound)" || s.aimAxisBindings[1] != "(unbound)";
            const char* aimSummary = !s.sourceObjectAim ? "Aim system disabled"
                : hasAimAxes ? "Independent aim & steer" : "Aim-driven steering";
            ImGui::TextDisabled("Aiming: %s", aimSummary);
            ImGui::SameLine();
            if (ImGui::SmallButton("Configure aiming & steering..."))
                WizardSession::Navigate(WizardSession::Route::TuneAiming);
            ImGui::Spacing();
        }
        ImGui::PushID(i);
        if (i > 0) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }

        const bool steeringAxisDisabled = !tuningOnly && s.hosamMode && (i == 1 || i == 2);
        if (steeringAxisDisabled) ImGui::BeginDisabled();
        if (tuningOnly) {
            DrawBindingSummaryRow(kAxisSlots[i].label, s.axisBindings[i],
                kAxisSlots[i].invertIniKey ? &s.axisInvert[i] : nullptr);
        } else {
            DrawBindingRow(kAxisSlots[i].label, s.axisBindings[i], i, true,
                kAxisSlots[i].invertIniKey ? &s.axisInvert[i] : nullptr);
            if (i == 0) {
                ImGui::Checkbox("Gamepad-style throttle (HOSAS)", &s.accumulatorThrottle);
                ImGui::TextDisabled(s.accumulatorThrottle
                    ? "Like vanilla: deflect to change throttle, center to hold, pull back through zero for reverse"
                    : "Use with a self-centering stick, like the gamepad left stick");
                if (ImGui::SmallButton("Configure gamepad-style throttle..."))
                    WizardSession::Navigate(WizardSession::Route::TuneGamepadThrottle);
            }
        }
        if (steeringAxisDisabled) ImGui::EndDisabled();

        if (tuningOnly && kAxisSlots[i].sensitivityKey) {
            ImGui::Indent(180);
            ImGui::PushItemWidth(120);
            ImGui::SliderFloat("Sens", &s.axisSensitivity[i], 0.1f, 3.0f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::Unindent(180);
        }

        if (tuningOnly && kAxisSlots[i].saturationKey) {
            ImGui::Indent(180);
            ImGui::PushItemWidth(120);
            ImGui::SliderFloat("Sat", &s.axisSaturation[i], 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            s.axisSaturation[i] = std::clamp(s.axisSaturation[i], 0.05f, 1.0f);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            bool isThrottle = (i == 0);
            if (isThrottle) {
                DrawThrottleRangeGraph(s, 200.0f, 14.0f);
            } else {
                float sat = s.axisSaturation[i];
                float dz = s.axisDeadzone[i];
                float barWidth = 200.0f, barHeight = 14.0f;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), IM_COL32(80, 30, 30, 200), 3.0f);
                float cappedEdge = ((1.0f - sat) / 2.0f) * barWidth;
                dl->AddRectFilled(ImVec2(pos.x + cappedEdge, pos.y), ImVec2(pos.x + barWidth - cappedEdge, pos.y + barHeight),
                    IM_COL32(50, 200, 80, 220), 3.0f);

                // Draw Center Deadzone (dz is fraction of half-axis; /2 maps to full bar)
                if (dz > 0.001f) {
                    float dzHalf = dz / 2.0f;
                    float dzLeft = (0.5f - dzHalf) * barWidth;
                    float dzRight = (0.5f + dzHalf) * barWidth;
                    dl->AddRectFilled(ImVec2(pos.x + dzLeft, pos.y), ImVec2(pos.x + dzRight, pos.y + barHeight),
                        IM_COL32(200, 100, 30, 140), 0.0f);
                }

                // Center line
                dl->AddLine(ImVec2(pos.x + barWidth * 0.5f, pos.y), ImVec2(pos.x + barWidth * 0.5f, pos.y + barHeight),
                    IM_COL32(80, 220, 240, 220), 2.0f);

                // Live axis position (yellow marker) — same bipolar normalization
                // the runtime uses (calibration range + invert), so the user can
                // see exactly where the stick sits relative to the deadzone and
                // saturation zones and feel how each maps to output.
                {
                    BindingRef ab = ParseBindingRef(s.axisBindings[i].c_str(), -1);
                    int axDevIdx = -1;
                    if (ab.value > 0) {
                        if (ab.deviceIndex >= 0)            axDevIdx = ab.deviceIndex;
                        else if (!ab.deviceName.empty())    axDevIdx = DeviceManager::ResolveByName(ab.deviceName);
                        else if (DeviceManager::GetDeviceCount() > 0) axDevIdx = 0;
                    }
                    const auto* st = (axDevIdx >= 0) ? DeviceManager::GetCachedState(axDevIdx) : nullptr;
                    if (st) {
                        long rawVal = DeviceManager::GetAxisFromState(st, ab.value);
                        float aMin = 0.0f, aMax = 65535.0f;
                        auto calibIt = s.calibData.find((axDevIdx << 8) | ab.value);
                        if (calibIt != s.calibData.end() && calibIt->second.second > calibIt->second.first) {
                            aMin = (float)calibIt->second.first;
                            aMax = (float)calibIt->second.second;
                        }
                        float center = (aMin + aMax) * 0.5f, half = (aMax - aMin) * 0.5f;
                        float v = (half > 0.0f) ? (rawVal - center) / half : 0.0f;
                        if (s.axisInvert[i]) v = -v;
                        v = std::clamp(v, -1.0f, 1.0f);
                        float liveX = (0.5f + v * 0.5f) * barWidth;
                        dl->AddLine(ImVec2(pos.x + liveX, pos.y - 1), ImVec2(pos.x + liveX, pos.y + barHeight + 1),
                            IM_COL32(255, 220, 50, 255), 2.0f);
                    }
                }

                dl->AddRect(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), IM_COL32(120, 120, 120, 180), 3.0f);
                ImGui::Dummy(ImVec2(barWidth, barHeight));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%.0f%%", sat * 100.0f);
            }
            ImGui::Unindent(180);

            // Throttle-specific panels
            if (isThrottle) {
                ImGui::Indent(180);
                DrawThrottleCalibrationPanel(s);
                ImGui::Unindent(180);
            }
        }

        // Per-axis deadzone
        if (tuningOnly && kAxisSlots[i].deadzoneKey) {
            ImGui::Indent(180);
            ImGui::PushItemWidth(120);
            char dzLabel[32];
            std::snprintf(dzLabel, sizeof(dzLabel), "Deadzone##axdz%d", i);
            float dzPct = s.axisDeadzone[i] * 100.0f;
            if (ImGui::SliderFloat(dzLabel, &dzPct, 0.0f, 95.0f, "%.0f%%"))
                s.axisDeadzone[i] = std::clamp(dzPct / 100.0f, 0.0f, 0.95f);
            ImGui::PopItemWidth();
            if (s.axisDeadzone[i] > 0.001f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Center dead zone");
            }
            ImGui::Unindent(180);
        }

        ImGui::PopID();
    }

    if (!tuningOnly) {
        DrawButtonBasedAxes(s);
        DrawReverseSetup(s);
        if (ImGui::SmallButton("Fine-tune throttle and reverse zones..."))
            WizardSession::Navigate(WizardSession::Route::TuneFlightAxes);
    }
}

}  // namespace WizardUI

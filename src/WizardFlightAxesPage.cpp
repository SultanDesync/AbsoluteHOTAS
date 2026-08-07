#include "PCH.h"

#include "WizardUI.h"

#include "BindingRef.h"
#include "DeviceManager.h"
#include "WizardConfig.h"
#include "WizardDefs.h"
#include "WizardSession.h"

#include <imgui.h>

namespace WizardUI {

static void DrawWrappedColored(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

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

static void DrawBipolarAxisRangeGraph(WizardState& s, int axisIndex,
                                      float barWidth, float barHeight) {
    const float sat = s.axisSaturation[axisIndex];
    const float dz = s.axisDeadzone[axisIndex];
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + barHeight),
        IM_COL32(75, 35, 38, 220), 4.0f);
    const float cappedEdge = ((1.0f - sat) * 0.5f) * barWidth;
    dl->AddRectFilled(ImVec2(pos.x + cappedEdge, pos.y),
        ImVec2(pos.x + barWidth - cappedEdge, pos.y + barHeight),
        IM_COL32(38, 160, 100, 230), 4.0f);

    if (dz > 0.001f) {
        const float dzHalf = dz * 0.5f;
        const float dzLeft = (0.5f - dzHalf) * barWidth;
        const float dzRight = (0.5f + dzHalf) * barWidth;
        dl->AddRectFilled(ImVec2(pos.x + dzLeft, pos.y),
            ImVec2(pos.x + dzRight, pos.y + barHeight),
            IM_COL32(210, 125, 35, 210), 0.0f);
    }

    dl->AddLine(ImVec2(pos.x + barWidth * 0.5f, pos.y),
        ImVec2(pos.x + barWidth * 0.5f, pos.y + barHeight),
        IM_COL32(80, 220, 240, 230), 2.0f);

    const BindingRef binding = ParseBindingRef(s.axisBindings[axisIndex].c_str(), -1);
    int deviceIndex = -1;
    if (binding.value > 0) {
        if (binding.deviceIndex >= 0) deviceIndex = binding.deviceIndex;
        else if (!binding.deviceName.empty()) deviceIndex = DeviceManager::ResolveByName(binding.deviceName);
        else if (DeviceManager::GetDeviceCount() > 0) deviceIndex = 0;
    }

    const auto* state = deviceIndex >= 0 ? DeviceManager::GetCachedState(deviceIndex) : nullptr;
    if (state) {
        const long rawValue = DeviceManager::GetAxisFromState(state, binding.value);
        float axisMin = 0.0f;
        float axisMax = 65535.0f;
        const auto calibration = s.calibData.find((deviceIndex << 8) | binding.value);
        if (calibration != s.calibData.end() &&
            calibration->second.second > calibration->second.first) {
            axisMin = static_cast<float>(calibration->second.first);
            axisMax = static_cast<float>(calibration->second.second);
        }
        const float center = (axisMin + axisMax) * 0.5f;
        const float half = (axisMax - axisMin) * 0.5f;
        float value = half > 0.0f ? (rawValue - center) / half : 0.0f;
        if (s.axisInvert[axisIndex]) value = -value;
        value = std::clamp(value, -1.0f, 1.0f);
        const float liveX = (0.5f + value * 0.5f) * barWidth;
        dl->AddLine(ImVec2(pos.x + liveX, pos.y - 2.0f),
            ImVec2(pos.x + liveX, pos.y + barHeight + 2.0f),
            IM_COL32(255, 225, 65, 255), 3.0f);
    }

    dl->AddRect(pos, ImVec2(pos.x + barWidth, pos.y + barHeight),
        IM_COL32(125, 145, 155, 220), 4.0f);
    ImGui::Dummy(ImVec2(barWidth, barHeight));
}

// Reverse is one capability with three hardware strategies. Keep them together in
// the main binding path so users do not have to discover a dedicated axis, a digital
// button, and a throttle zone in three different parts of the wizard.
static void DrawReverseSetup(WizardState& s) {
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Reverse", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Indent(12.0f);
    ImGui::TextWrapped("Choose the reverse control that fits your hardware. The throttle card above owns its zero-thrust reverse zone; the alternatives below are for a held button or a separate analog lever.");
    ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.96f, 1.0f),
        "CONTROL MODE");
    ImGui::SameLine();
    ImGui::TextWrapped("DIRECT SHIP CONTROL  /  REVERSE THRUST");
    DrawWrappedColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        "The button or lever brakes toward zero and applies reverse thrust when "
        "the configured velocity gate permits it.");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Hold a button (recommended)");
    ImGui::PushID("reverseButton");
    DrawBindingRow("Reverse button", s.digitalAxisBindings[0], CaptureSlot::kDigitalAxisBase, false);
    ImGui::PopID();
    ImGui::TextDisabled("Hold to brake to zero and continue into reverse.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Use a dedicated analog control");
    ImGui::PushID("reverseAxis");
    constexpr int reverseAxis = kNumAxisSlots - 1;
    DrawBindingRow("Reverse axis", s.axisBindings[reverseAxis], reverseAxis, true,
                   &s.axisInvert[reverseAxis]);
    ImGui::PushItemWidth(150.0f);
    ImGui::SliderFloat("Sensitivity##reverse", &s.axisSensitivity[reverseAxis],
                       0.1f, 3.0f, "%.2f");
    ImGui::SameLine();
    float reverseSaturationPct = s.axisSaturation[reverseAxis] * 100.0f;
    if (ImGui::SliderFloat("Saturation##reverse", &reverseSaturationPct,
            5.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        s.axisSaturation[reverseAxis] =
            std::clamp(reverseSaturationPct / 100.0f, 0.05f, 1.0f);
    }
    ImGui::PopItemWidth();
    DrawBipolarAxisRangeGraph(s, reverseAxis,
        std::min(360.0f, ImGui::GetContentRegionAvail().x), 18.0f);
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
    ImGui::TextWrapped("Buttons and POV/hat directions can command roll or strafe directly.");
    DrawWrappedColored(ImVec4(0.38f, 0.82f, 0.96f, 1.0f),
        "Analog roll and strafe remain independent and may be used at the same time.");
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
    ImGui::Indent(12.0f);
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
    ImGui::Unindent(12.0f);
}

static const char* CoreAxisDescription(int axisIndex) {
    static constexpr const char* descriptions[] = {
        "Positional thrust and cruise authority",
        "Nose up / down rotation",
        "Left / right heading rotation",
        "Left / right bank rotation",
        "Left / right translation",
        "Up / down translation",
    };
    return axisIndex >= 0 && axisIndex < 6 ? descriptions[axisIndex] : "Flight axis";
}

static ImVec4 CoreAxisAccent(int axisIndex) {
    if (axisIndex == 0) return ImVec4(1.0f, 0.68f, 0.22f, 1.0f);
    if (axisIndex <= 3) return ImVec4(0.30f, 0.82f, 1.0f, 1.0f);
    return ImVec4(0.66f, 0.50f, 1.0f, 1.0f);
}

static bool HasSeparateAimInput(const WizardState& s) {
    for (int i = 0; i < kNumAimAxisSlots; ++i) {
        if (s.aimAxisBindings[i] != "(unbound)") return true;
    }
    for (int i = 0; i < kNumDigitalAimSlots; ++i) {
        if (s.digitalAimBindings[i] != "(unbound)") return true;
    }
    return false;
}

static const char* CoreAxisInjectionPath(const WizardState& s, int axisIndex) {
    switch (axisIndex) {
        case 0:
            return "DIRECT THROTTLE CONTROL";
        case 1:
            if (s.hosamMode) return "STARFIELD MOUSE STEERING";
            if (s.sourceObjectAim && !HasSeparateAimInput(s))
                return "AIM-DRIVEN SHIP ROTATION";
            return "DIRECT SHIP ROTATION";
        case 2:
            if (s.hosamMode) return "STARFIELD MOUSE STEERING";
            if (s.sourceObjectAim && !HasSeparateAimInput(s))
                return "AIM-DRIVEN SHIP ROTATION";
            return "DIRECT SHIP ROTATION";
        case 3:
            return "DIRECT ROLL CONTROL";
        case 4:
            return "DIRECT LATERAL STRAFE";
        case 5:
            return "DIRECT VERTICAL STRAFE";
        default:
            return "DIRECT SHIP CONTROL";
    }
}

static const char* CoreAxisInjectionBehavior(const WizardState& s, int axisIndex) {
    switch (axisIndex) {
        case 0:
            return s.accumulatorThrottle
                ? "Stick deflection changes the held throttle target; returning to center holds the commanded value."
                : "Hardware position commands ship thrust directly.";
        case 1:
        case 2:
            if (s.hosamMode)
                return "Starfield's mouse steering controls this rotation axis while HOSAM is enabled.";
            if (s.sourceObjectAim && !HasSeparateAimInput(s))
                return "This axis steers through the weapon reticle while Aim-Driven Steering is active.";
            return "The bound axis commands ship rotation directly.";
        case 3:
            return "The bound axis commands roll directly and remains available while strafing.";
        case 4:
            return "The bound axis commands lateral strafe directly and can be combined with roll.";
        case 5:
            return "The bound axis commands vertical strafe directly and can be combined with roll.";
        default:
            return "";
    }
}

static void DrawCoreAxisInjection(const WizardState& s, int axisIndex) {
    ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.96f, 1.0f),
        "CONTROL MODE");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", CoreAxisInjectionPath(s, axisIndex));
    DrawWrappedColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        CoreAxisInjectionBehavior(s, axisIndex));
    if (axisIndex == 0 && s.boostZone) {
        DrawWrappedColored(ImVec4(1.0f, 0.66f, 0.26f, 1.0f),
            "BOOST ZONE  /  Crossing the configured threshold activates ship boost.");
    }
}

static void DrawInjectionSafetyNotice(const WizardState& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 10.0f));
    if (ImGui::BeginTable("InjectionSafetyNotice", 1,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
            ImGui::GetColorU32(ImVec4(0.06f, 0.15f, 0.18f, 0.97f)));

        ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.62f, 1.0f),
            "5.0 DIRECT FLIGHT CONTROL");
        ImGui::TextWrapped(
            "Pitch, yaw, roll, throttle, lateral strafe, and vertical strafe are "
            "independent flight controls. Roll and strafe may be commanded simultaneously.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.96f, 1.0f),
            "NATIVE BOOST + STRAFE");
        ImGui::TextWrapped(
            "Boost-zone and strafe activation use Starfield's internal ship-control "
            "paths. No keyboard bindings are required for these flight functions.");
        DrawWrappedColored(ImVec4(0.55f, 0.75f, 0.82f, 1.0f),
            "Flight controls enabled is the profile-level switch for flight axes, "
            "head pose, boost-zone, and strafe output.");
        if (!s.axisInjectionEnabled) {
            DrawWrappedColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
                "THIS PROFILE'S FLIGHT CONTROLS ARE PARKED");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

static void DrawFlightCoreHero(WizardState& s) {
    int ready = 0;
    int required = 0;
    for (int i = 0; i < 6; ++i) {
        if (s.hosamMode && (i == 1 || i == 2)) continue;
        ++required;
        if (s.axisBindings[i] != "(unbound)") ++ready;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = std::max(300.0f, ImGui::GetContentRegionAvail().x);
    constexpr float height = 116.0f;
    ImGui::Dummy(ImVec2(width, height));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        IM_COL32(20, 34, 46, 245), 7.0f);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
        IM_COL32(55, 125, 165, 230), 7.0f, 0, 1.5f);
    dl->AddRectFilled(pos, ImVec2(pos.x + 7.0f, pos.y + height),
        IM_COL32(45, 180, 235, 255), 7.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20.0f, pos.y + 13.0f));
    ImGui::TextColored(ImVec4(0.38f, 0.87f, 1.0f, 1.0f),
        "DIRECT FLIGHT CONTROL");
    ImGui::TextUnformatted("Bind the ship, then shape how every axis responds.");
    ImGui::TextDisabled("These bindings drive Starfield's ship controls directly.");

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20.0f, pos.y + 75.0f));
    const float progress = required > 0 ? static_cast<float>(ready) / required : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
        ready == required ? ImVec4(0.25f, 0.82f, 0.48f, 1.0f)
                          : ImVec4(0.25f, 0.65f, 0.90f, 1.0f));
    ImGui::ProgressBar(progress, ImVec2(std::max(120.0f, width - 330.0f), 18.0f), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%d / %d active axes bound", ready, required);
    ImGui::SameLine();
    ImGui::Checkbox("Flight controls enabled", &s.axisInjectionEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Profile-level switch for flight axes, aim, head pose, boost zone,\n"
            "and strafe output.");

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + ImGui::GetStyle().ItemSpacing.y));
}

static void DrawCoreAxisBinding(WizardState& s, int axisIndex) {
    const bool bound = s.axisBindings[axisIndex] != "(unbound)";
    const std::string display = WizardConfig::FormatBindingDisplay(s.axisBindings[axisIndex]);
    const ImVec4 bindingColor = bound
        ? ImVec4(0.35f, 1.0f, 0.60f, 1.0f)
        : ImVec4(1.0f, 0.62f, 0.30f, 1.0f);

    if (ImGui::BeginTable("CoreBinding", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.48f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("INPUT SOURCE");
        ImGui::TextColored(bindingColor, "%s", display.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", display.c_str());

        ImGui::TableNextColumn();
        const char* bindLabel = bound ? "REBIND AXIS" : "BIND AXIS";
        if (ImGui::Button(bindLabel, ImVec2(118.0f, 30.0f)))
            WizardSession::BeginAxisCapture(axisIndex, kAxisSlots[axisIndex].label);
        ImGui::SameLine();
        if (ImGui::Button("CLEAR", ImVec2(62.0f, 30.0f)))
            s.axisBindings[axisIndex] = "(unbound)";
        ImGui::SameLine();
        ImGui::Checkbox("Invert", &s.axisInvert[axisIndex]);
        ImGui::EndTable();
    }
}

static void DrawCoreAxisTuning(WizardState& s, int axisIndex) {
    if (ImGui::BeginTable("CoreTuning", 3,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("SENSITIVITY");
        if (kAxisSlots[axisIndex].sensitivityKey) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##Sensitivity", &s.axisSensitivity[axisIndex],
                0.1f, 3.0f, "%.2f");
        } else {
            ImGui::TextDisabled("Uses lateral strafe tuning");
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("SATURATION");
        if (kAxisSlots[axisIndex].saturationKey) {
            float saturationPct = s.axisSaturation[axisIndex] * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##Saturation", &saturationPct,
                    5.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
                s.axisSaturation[axisIndex] =
                    std::clamp(saturationPct / 100.0f, 0.05f, 1.0f);
            }
        } else {
            ImGui::TextDisabled("Not applicable");
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("CENTER DEADZONE");
        if (kAxisSlots[axisIndex].deadzoneKey) {
            float deadzonePct = s.axisDeadzone[axisIndex] * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##Deadzone", &deadzonePct,
                    0.0f, 95.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
                s.axisDeadzone[axisIndex] =
                    std::clamp(deadzonePct / 100.0f, 0.0f, 0.95f);
            }
        } else {
            ImGui::TextDisabled("Not applicable");
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled("LIVE INPUT");
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (axisIndex == 0) {
        const float meterWidth =
            std::min(520.0f, std::max(180.0f, availableWidth - 60.0f));
        DrawThrottleRangeGraph(s, meterWidth, 20.0f);
    } else {
        DrawBipolarAxisRangeGraph(s, axisIndex,
            std::min(520.0f, availableWidth), 20.0f);
    }
}

static void DrawCoreAxisCard(WizardState& s, int axisIndex, bool inactive) {
    ImGui::PushID(10000 + axisIndex);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 10.0f));
    if (ImGui::BeginTable("CoreAxisCard", 1,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
            ImGui::GetColorU32(ImVec4(0.075f, 0.095f, 0.115f, 0.96f)));

        const ImVec4 accent = CoreAxisAccent(axisIndex);
        if (ImGui::BeginTable("CoreAxisHeader", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Identity", ImGuiTableColumnFlags_WidthStretch, 0.75f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableNextColumn();
            ImGui::TextColored(accent, "%s", kAxisSlots[axisIndex].label);
            ImGui::TextDisabled("%s", CoreAxisDescription(axisIndex));
            ImGui::TableNextColumn();
            if (inactive) {
                ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.30f, 1.0f),
                    "MOUSE STEERING ACTIVE");
            } else if (s.axisBindings[axisIndex] == "(unbound)") {
                ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.30f, 1.0f), "NEEDS BINDING");
            } else {
                ImGui::TextColored(ImVec4(0.32f, 0.92f, 0.54f, 1.0f), "BOUND");
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        DrawCoreAxisInjection(s, axisIndex);
        ImGui::Separator();
        if (inactive) ImGui::BeginDisabled();
        DrawCoreAxisBinding(s, axisIndex);
        ImGui::Separator();
        DrawCoreAxisTuning(s, axisIndex);

        if (axisIndex == 0) {
            ImGui::Spacing();
            ImGui::Checkbox("Self-centering / rate throttle (HOSAS)",
                &s.accumulatorThrottle);
            ImGui::SameLine();
            if (ImGui::SmallButton("Configure rate throttle..."))
                WizardSession::Navigate(WizardSession::Route::TuneGamepadThrottle);
            ImGui::TextDisabled(s.accumulatorThrottle
                ? "Stick deflection changes throttle; centering holds the commanded value."
                : "Positional throttle: hardware position directly commands thrust.");

            ImGui::Spacing();
            if (ImGui::CollapsingHeader("THROTTLE RANGE, DETENTS, REVERSE & BOOST",
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(10.0f);
                DrawThrottleCalibrationPanel(s);
                ImGui::Unindent(10.0f);
            }
        }
        if (inactive) ImGui::EndDisabled();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::Spacing();
}

static void DrawMouseSteeringCard(WizardState& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 9.0f));
    if (ImGui::BeginTable("MouseSteeringCard", 1,
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
            ImGui::GetColorU32(ImVec4(0.075f, 0.095f, 0.115f, 0.96f)));
        ImGui::Checkbox("Mouse steering (HOSAM)", &s.hosamMode);
        ImGui::SameLine();
        ImGui::TextDisabled(s.hosamMode
            ? "Mouse owns pitch and yaw; saved stick bindings remain available."
            : "Pitch and yaw use the flight-axis cards below.");
        if (s.hosamMode) DrawMouseSteeringOptions(s);

        const bool hasAimAxes =
            s.aimAxisBindings[0] != "(unbound)" || s.aimAxisBindings[1] != "(unbound)";
        const char* aimSummary = !s.sourceObjectAim ? "Aim system disabled"
            : hasAimAxes ? "Independent aim & steer" : "Aim-driven steering";
        ImGui::TextDisabled("Aiming: %s", aimSummary);
        ImGui::SameLine();
        if (ImGui::SmallButton("Configure aiming & steering..."))
            WizardSession::Navigate(WizardSession::Route::TuneAiming);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

static void DrawFlightControlsLanding(WizardState& s) {
    DrawFlightCoreHero(s);
    DrawInjectionSafetyNotice(s);

    ImGui::SeparatorText("THRUST");
    DrawCoreAxisCard(s, 0, false);

    ImGui::SeparatorText("ROTATION");
    DrawMouseSteeringCard(s);
    DrawCoreAxisCard(s, 1, s.hosamMode);
    DrawCoreAxisCard(s, 2, s.hosamMode);
    DrawCoreAxisCard(s, 3, false);

    ImGui::SeparatorText("6-DOF TRANSLATION");
    DrawCoreAxisCard(s, 4, false);
    DrawCoreAxisCard(s, 5, false);

    ImGui::SeparatorText("REVERSE & DIGITAL FALLBACKS");
    DrawReverseSetup(s);
    DrawButtonBasedAxes(s);
}

void DrawAxesTab(WizardState& s) {
    DrawFlightControlsLanding(s);
}

}  // namespace WizardUI

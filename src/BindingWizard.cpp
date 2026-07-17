#include "PCH.h"

#include "BindingWizard.h"
#include "WizardDefs.h"
#include "WizardCapture.h"
#include "WizardConfig.h"
#include "WizardSession.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"
#include "UIHook.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"

#include <imgui.h>

#include <string>
#include <cmath>
#include <algorithm>

static void WizLog(const std::string& msg) {
    RuntimePaths::Log("[BindingWizard]", msg);
}

static std::string s_profileCaptureName;   // "" = base (see s_profileCapturePending)
static std::string s_profileCaptureMode = "momentary";
static bool        s_profileCapturePending = false;  // a profile-trigger capture is in flight

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

static void SetStatus(const std::string& message, bool isError = false) {
    WizardSession::SetStatus(message, isError
        ? WizardSession::StatusKind::Error : WizardSession::StatusKind::Success);
}

static bool SaveCurrentProfile() {
    return WizardSession::SaveCurrentProfile();
}

static bool LoadEditorProfile(const std::string& name) {
    return WizardSession::LoadEditorProfile(name);
}

// --- Capture commit callback ---
static void OnCaptureCommit(int slot, const char* binding) {
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

// --- Shared UI helper: draw a binding row with Bind/Clear ---
static void DrawBindingRow(const char* label, std::string& binding, int captureSlot,
                           bool isAxis, bool* invert = nullptr) {
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

static void DrawBindingSummaryRow(const char* label, const std::string& binding,
                                  bool* invert = nullptr) {
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
            WizLog("Center set to: " + std::to_string(s.detentCenter));
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
                WizLog("Reverse zone set to: " + std::to_string(s.reverseZoneCenter));
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
                WizLog("Boost zone set to: " + std::to_string(s.boostZoneCenter));
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
                WizLog("Reverse zone set to: " + std::to_string(s.reverseZoneCenter));
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
static void DrawDevicesTab(WizardState& s) {
    int devCount = DeviceManager::GetDeviceCount();
    if (devCount == 0) ImGui::TextWrapped("No DirectInput devices detected.");
    ImGui::TextDisabled("Device indices (#N) follow DirectInput enumeration order; name-based bindings survive index changes.");
    ImGui::Spacing();

    auto& devCalib = WizardCapture::GetCalibState();
    constexpr long kGhostThreshold = 5000;

    for (int d = 0; d < devCount; d++) {
        const auto& info = DeviceManager::GetDevice(d);
        std::string header = "#" + std::to_string(d) + ": " + info.productName;
        if (!info.vidpidString.empty()) header += " [" + info.vidpidString + "]";

        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(12.0f);
            ImGui::Text("Instance: %s", info.instanceName.c_str());
            ImGui::Text("Axes: %d  Buttons: %d", info.axisCount, info.buttonCount);

            // Swap button for duplicate device names
            if (d + 1 < devCount && info.productName == DeviceManager::GetDevice(d + 1).productName) {
                char swapLabel[64];
                std::snprintf(swapLabel, sizeof(swapLabel), "Swap #%d <-> #%d", d, d + 1);
                if (ImGui::SmallButton(swapLabel)) {
                    char prefA[16], prefB[16], tempPref[16];
                    std::snprintf(prefA, sizeof(prefA), "#%d@", d);
                    std::snprintf(prefB, sizeof(prefB), "#%d@", d + 1);
                    std::snprintf(tempPref, sizeof(tempPref), "#__SWAP__@");
                    size_t lenA = strlen(prefA), lenB = strlen(prefB), tempLen = strlen(tempPref);

                    auto doSwap = [&](std::string& binding) {
                        if (binding.substr(0, lenA) == prefA) binding = std::string(tempPref) + binding.substr(lenA);
                        else if (binding.substr(0, lenB) == prefB) binding = std::string(prefA) + binding.substr(lenB);
                    };
                    auto finalize = [&](std::string& binding) {
                        if (binding.substr(0, tempLen) == tempPref) binding = std::string(prefB) + binding.substr(tempLen);
                    };

                    std::vector<std::string*> allBindings;
                    for (auto& b : s.axisBindings) allBindings.push_back(&b);
                    for (auto& b : s.buttonBindings) allBindings.push_back(&b);
                    for (auto& b : s.controlExtensionBindings) allBindings.push_back(&b);
                    for (auto& b : s.digitalAxisBindings) allBindings.push_back(&b);
                    for (auto& sa : s.shipActionSlots) allBindings.push_back(&sa.binding);
                    for (auto& cb : s.customBindings) allBindings.push_back(&cb.buttonBinding);
                    for (auto& b : s.aimAxisBindings) allBindings.push_back(&b);
                    for (auto& b : s.digitalAimBindings) allBindings.push_back(&b);
                    allBindings.push_back(&s.toggleAimModeBinding);
                    allBindings.push_back(&s.turnAssistBinding);
                    for (auto& macro : s.macros) allBindings.push_back(&macro.buttonBinding);

                    for (auto* bp : allBindings) doSwap(*bp);
                    for (auto* bp : allBindings) finalize(*bp);

                    std::unordered_map<int, std::pair<long, long>> swappedCalibration;
                    for (const auto& [key, range] : s.calibData) {
                        int deviceIndex = key >> 8;
                        if (deviceIndex == d) deviceIndex = d + 1;
                        else if (deviceIndex == d + 1) deviceIndex = d;
                        swappedCalibration[(deviceIndex << 8) | (key & 0xFF)] = range;
                    }
                    s.calibData.swap(swappedCalibration);
                    WizardSession::SwapActivationDeviceIndices(d, d + 1);
                    WizardSession::SetStatus("Device reassignment staged. Save & Apply to commit it.",
                                             WizardSession::StatusKind::Warning);
                    WizLog("Staged device index swap #" + std::to_string(d) + " <-> #" + std::to_string(d + 1));
                }
            }

            // Device calibration
            bool isCalibThisDevice = devCalib.active && devCalib.deviceIndex == d;
            if (isCalibThisDevice) {
                const auto* st = DeviceManager::GetCachedState(d);
                if (st) {
                    for (int a = 0; a < 8; a++) {
                        long val = DeviceManager::GetAxisFromState(st, 0x30 + a);
                        if (val < devCalib.observedMin[a]) devCalib.observedMin[a] = val;
                        if (val > devCalib.observedMax[a]) devCalib.observedMax[a] = val;
                    }
                }
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Move ALL axes to their full extremes, then click Done.");
                int activeCount = 0;
                for (int a = 0; a < 8; a++) {
                    if (devCalib.observedMax[a] - devCalib.observedMin[a] > kGhostThreshold) activeCount++;
                }
                ImGui::Text("Detected %d active axes", activeCount);

                if (ImGui::Button("Done##devCalib")) {
                    int saved = 0;
                    for (int a = 0; a < 8; a++) {
                        if (devCalib.observedMax[a] - devCalib.observedMin[a] > kGhostThreshold) {
                            s.calibData[(d << 8) | (0x30 + a)] = { devCalib.observedMin[a], devCalib.observedMax[a] };
                            saved++;
                        }
                    }
                    WizLog("Calibrated device #" + std::to_string(d) + ": " + std::to_string(saved) + " axes saved");
                    devCalib.Reset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##devCalib")) devCalib.Reset();
            } else {
                bool hasCalib = false;
                for (int a = 0; a < 8; a++) {
                    if (s.calibData.count((d << 8) | (0x30 + a))) { hasCalib = true; break; }
                }
                if (hasCalib) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Calibration:");
                    for (int a = 0; a < 8; a++) {
                        int calibKey = (d << 8) | (0x30 + a);
                        auto calibIt = s.calibData.find(calibKey);
                        if (calibIt != s.calibData.end()) {
                            ImGui::Text("  0x%02X (%s): [%ld - %ld]", 0x30 + a, WizardCapture::AxisName(0x30 + a),
                                calibIt->second.first, calibIt->second.second);
                        }
                    }
                    ImGui::PushID(d * 1000 + 998);
                    if (ImGui::SmallButton("Clear All##calib")) {
                        for (int a = 0; a < 8; a++) s.calibData.erase((d << 8) | (0x30 + a));
                        WizLog("Cleared all calibration for device #" + std::to_string(d));
                    }
                    ImGui::PopID();
                }
                if (!devCalib.active) {
                    ImGui::Spacing();
                    ImGui::PushID(d * 1000 + 999);
                    if (ImGui::SmallButton("Calibrate Device")) {
                        devCalib.Reset();
                        devCalib.active = true;
                        devCalib.deviceIndex = d;
                        const auto* st = DeviceManager::GetCachedState(d);
                        if (st) {
                            for (int a = 0; a < 8; a++) {
                                long val = DeviceManager::GetAxisFromState(st, 0x30 + a);
                                devCalib.observedMin[a] = val;
                                devCalib.observedMax[a] = val;
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::Unindent(12.0f);
        }
    }
}

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

static void DrawAxesTab(WizardState& s, bool tuningOnly = false) {
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

static void DrawAimingTab(WizardState& s) {
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

static void DrawGamepadThrottleTab(WizardState& s) {
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
        WizLog("Added menu-navigation preset (WASD/Tab/E/Esc).");
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

static void DrawButtonsTab(WizardState& s) {
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

static void DrawPluginControls(WizardState& s) {
    ImGui::TextWrapped("Master runtime controls for enabling or parking AbsoluteHOTAS output.");
    for (int i = 0; i < kToggleWizardButtonSlot; ++i) {
        ImGui::PushID(2000 + i);
        DrawBindingRow(kButtonSlots[i].label, s.buttonBindings[i], CaptureSlot::kButtonBase + i, false);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Wizard access is promoted to the start of Bind Controls so it can be configured first.");
    if (ImGui::SmallButton("Go to wizard access"))
        WizardSession::Navigate(WizardSession::Route::BindFlightAxes);
}

// --- Tab: Macros ---

// Target picker: ship actions first (they follow the user's in-game rebinds), raw
// keys/mouse second. An unrecognized token (hand-edited INI) previews as itself
// rather than vanishing.
static void DrawTargetCombo(std::string& token) {
    const char* label   = FindMacroTargetLabel(token);
    const char* preview = label ? label : token.c_str();

    ImGui::PushItemWidth(170);
    if (ImGui::BeginCombo("##target", preview)) {
        ImGui::SeparatorText("Ship Actions (follow in-game binds)");
        for (int i = 0; i < kNumShipActionTargets; i++) {
            const bool sel = (token == kShipActionTargets[i].value);
            ImGui::PushID(i);
            if (ImGui::Selectable(kShipActionTargets[i].label, sel)) token = kShipActionTargets[i].value;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::SeparatorText("Keys & Mouse");
        for (int i = 0; i < kOutputCatalogSize; i++) {
            const bool sel = (token == kOutputCatalog[i].value);
            ImGui::PushID(1000 + i);
            if (ImGui::Selectable(kOutputCatalog[i].label, sel)) token = kOutputCatalog[i].value;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
}

static std::string UniqueMacroName(const WizardState& s) {
    for (int n = 1;; ++n) {
        std::string candidate = "Macro" + std::to_string(n);
        bool taken = false;
        for (const auto& m : s.macros) {
            if (m.name == candidate) { taken = true; break; }
        }
        if (!taken) return candidate;
    }
}

static void DrawMacroSteps(MacroRow& m) {
    int removeStep = -1, moveUp = -1, moveDown = -1;

    for (int si = 0; si < (int)m.steps.size(); si++) {
        auto& st = m.steps[si];
        ImGui::PushID(si);

        ImGui::Text("%2d", si);
        ImGui::SameLine();

        // Targets. More than one = a chord: pressed together in the same step.
        int removeTarget = -1;
        for (int ti = 0; ti < (int)st.targets.size(); ti++) {
            if (ti) { ImGui::SameLine(0, 4); ImGui::Text("+"); ImGui::SameLine(0, 4); }
            ImGui::PushID(ti);
            DrawTargetCombo(st.targets[ti]);
            if (st.targets.size() > 1) {
                ImGui::SameLine(0, 2);
                if (ImGui::SmallButton("x")) removeTarget = ti;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this target from the chord");
            }
            ImGui::PopID();
        }
        if (removeTarget >= 0) st.targets.erase(st.targets.begin() + removeTarget);

        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("+")) st.targets.push_back("key:0x1E");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a target to press together with this one (chord)");

        // Tap vs hold, and the amount that means different things for each.
        ImGui::SameLine();
        int actionIdx = st.hold ? 1 : 0;
        const char* kActions[] = { "Tap", "Hold" };
        ImGui::PushItemWidth(70);
        if (ImGui::Combo("##action", &actionIdx, kActions, 2)) st.hold = (actionIdx == 1);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled(st.hold ? "for" : "x");
        ImGui::SameLine();
        ImGui::PushItemWidth(64);
        ImGui::InputInt("##amount", &st.amount, 0);
        ImGui::PopItemWidth();
        if (st.amount < 0) st.amount = 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(st.hold ? "How long to hold, in milliseconds" : "How many times to press");

        ImGui::SameLine();
        ImGui::TextDisabled(st.hold ? "ms, gap" : "times, gap");
        ImGui::SameLine();
        ImGui::PushItemWidth(64);
        ImGui::InputInt("##gap", &st.gapMs, 0);
        ImGui::PopItemWidth();
        if (st.gapMs < 0) st.gapMs = 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Milliseconds to wait before the next step");
        ImGui::SameLine();
        ImGui::TextDisabled("ms");

        ImGui::SameLine();
        if (ImGui::SmallButton("^")) moveUp = si;
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) moveDown = si;
        ImGui::SameLine();
        if (ImGui::SmallButton("Del")) removeStep = si;

        ImGui::PopID();
    }

    if (moveUp > 0)                                     std::swap(m.steps[moveUp], m.steps[moveUp - 1]);
    if (moveDown >= 0 && moveDown + 1 < (int)m.steps.size()) std::swap(m.steps[moveDown], m.steps[moveDown + 1]);
    if (removeStep >= 0)                                m.steps.erase(m.steps.begin() + removeStep);
}

static void DrawMacrosTab(WizardState& s) {
    ImGui::TextWrapped(
        "A macro plays an ordered sequence of key actions from one button press. Steps can target "
        "ship actions (which follow your in-game Starfield keybinds automatically) or raw keys. "
        "Press '+' on a step to add targets pressed together as a chord.");
    ImGui::TextWrapped(
        "One press runs the whole sequence to the end - you do not need to hold the button. "
        "Turbo instead repeats the sequence for as long as the button IS held.");
    if (ImGui::Button("Add Macro")) {
        MacroRow m;
        m.name = UniqueMacroName(s);
        m.steps.push_back({ {"NextSystem"}, false, 1, 50 });
        s.macros.push_back(std::move(m));
    }
    ImGui::SameLine();
    if (ImGui::Button("Add \"Grav -> Shields\" Preset")) {
        MacroRow m;
        m.name = "Power Grav to Shields";
        m.steps.push_back({ {"NextSystem"},          false, 6,    5 });  // right edge = GRV (clamps)
        m.steps.push_back({ {"DecreaseSystemPower"}, true,  1200, 0 });  // drain grav -> surplus
        m.steps.push_back({ {"PreviousSystem"},      false, 1,    5 });  // GRV -> SHD (adjacent)
        m.steps.push_back({ {"IncreaseSystemPower"}, true,  1200, 0 });  // pour surplus into shields
        s.macros.push_back(std::move(m));
        WizLog("Added Grav -> Shields preset macro.");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dumps grav-drive power into shields in one press.\n"
                          "Anchors to the right edge of the power bar, so it works\n"
                          "regardless of your weapon loadout. Bind a button and Save.");

    ImGui::Spacing();

    if (s.macros.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "No macros. Click 'Add Macro', or try the Grav -> Shields preset.");
        return;
    }

    int removeMacro = -1;
    for (int mi = 0; mi < (int)s.macros.size(); mi++) {
        auto& m = s.macros[mi];
        ImGui::PushID(6000 + mi);

        // Duplicate friendly names are allowed now (they get distinct section keys on
        // save). Disambiguate them for display only, with an integer suffix in paste
        // order: "Power Grav to Shield (0)", "(1)", ...
        int ordinal = 0, sameName = 0;
        for (int j = 0; j < (int)s.macros.size(); j++) {
            if (s.macros[j].name == m.name) { sameName++; if (j < mi) ordinal++; }
        }
        char disp[96];
        if (sameName > 1) std::snprintf(disp, sizeof(disp), "%s (%d)", m.name.c_str(), ordinal);
        else              std::snprintf(disp, sizeof(disp), "%s", m.name.c_str());

        char header[160];
        std::snprintf(header, sizeof(header), "%s%s###macro%d",
                      m.name.empty() ? "(unnamed)" : disp,
                      m.buttonBinding == "(unbound)" ? "  [no button]" : "", mi);

        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(12);

            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
            ImGui::PushItemWidth(200);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) m.name = nameBuf;
            ImGui::PopItemWidth();
            if (m.name.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "name required - will not save");
            }

            if (mi < 100) {
                DrawBindingRow("Trigger Button", m.buttonBinding, CaptureSlot::kMacroBase + mi, false);
            }

            if (ImGui::BeginTable("MacroOptions", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox("Turbo (repeat while held)", &m.turbo);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Off: one press plays the sequence once, to the end.\n"
                                      "On:  the sequence repeats while the button is held,\n"
                                      "     and stops as soon as you release it.");
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Delete Macro")) removeMacro = mi;
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Steps");
            DrawMacroSteps(m);

            if (ImGui::Button("Add Step")) m.steps.push_back({ {"NextSystem"}, false, 1, 50 });
            if (m.steps.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Add a step - this macro will not run yet.");
            else if (m.buttonBinding == "(unbound)")
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Bind a trigger button - this macro will not run yet.");

            ImGui::Unindent(12);
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (removeMacro >= 0) s.macros.erase(s.macros.begin() + removeMacro);
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

static void DrawProfileContextBar(bool dirty) {
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

static void DrawProfileManagementPanel(bool dirty) {
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

static bool DrawNavigationButton(const char* label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.35f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.44f, 0.70f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, ImVec2(-1.0f, 0.0f));
    if (selected) ImGui::PopStyleColor(2);
    return pressed;
}

static void DrawPrimaryNavigation() {
    if (!ImGui::BeginTable("PrimaryNavigation", 3, ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableNextColumn();
    if (DrawNavigationButton("Bind Controls##Primary", WizardSession::GetPage() == WizardSession::Page::Bind))
        WizardSession::SelectPage(WizardSession::Page::Bind);
    ImGui::TableNextColumn();
    if (DrawNavigationButton("Tune##Primary", WizardSession::GetPage() == WizardSession::Page::Tune))
        WizardSession::SelectPage(WizardSession::Page::Tune);
    ImGui::TableNextColumn();
    if (DrawNavigationButton("Advanced##Primary", WizardSession::GetPage() == WizardSession::Page::Advanced))
        WizardSession::SelectPage(WizardSession::Page::Advanced);
    ImGui::EndTable();
}

static void DrawSecondaryNavigation() {
    int columns = 2;
    if (WizardSession::GetPage() != WizardSession::Page::Bind) columns = 3;
    if (!ImGui::BeginTable("SecondaryNavigation", columns, ImGuiTableFlags_SizingStretchSame)) return;

    if (WizardSession::GetPage() == WizardSession::Page::Bind) {
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Flight Axes##Secondary",
                WizardSession::GetBindPage() == WizardSession::BindPage::FlightAxes))
            WizardSession::SelectBindPage(WizardSession::BindPage::FlightAxes);
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Ship Buttons##Secondary",
                WizardSession::GetBindPage() == WizardSession::BindPage::ShipButtons))
            WizardSession::SelectBindPage(WizardSession::BindPage::ShipButtons);
    } else if (WizardSession::GetPage() == WizardSession::Page::Tune) {
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Flight Axes##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::FlightAxes))
            WizardSession::SelectTunePage(WizardSession::TunePage::FlightAxes);
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Aiming & Combat##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::Aiming))
            WizardSession::SelectTunePage(WizardSession::TunePage::Aiming);
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Gamepad Throttle##Secondary",
                WizardSession::GetTunePage() == WizardSession::TunePage::GamepadThrottle))
            WizardSession::SelectTunePage(WizardSession::TunePage::GamepadThrottle);
    } else {
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Macros##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::Macros))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::Macros);
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Plugin Controls##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::PluginControls))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::PluginControls);
        ImGui::TableNextColumn();
        if (DrawNavigationButton("Devices##Secondary",
                WizardSession::GetAdvancedPage() == WizardSession::AdvancedPage::Devices))
            WizardSession::SelectAdvancedPage(WizardSession::AdvancedPage::Devices);
    }
    ImGui::EndTable();
}

static void DrawActivePage(WizardState& s, bool dirty) {
    if (WizardSession::GetPage() == WizardSession::Page::Advanced)
        DrawProfileManagementPanel(dirty);

    switch (WizardSession::GetRoute()) {
        case WizardSession::Route::BindFlightAxes: DrawAxesTab(s, false); break;
        case WizardSession::Route::BindShipButtons: DrawButtonsTab(s); break;
        case WizardSession::Route::TuneFlightAxes: DrawAxesTab(s, true); break;
        case WizardSession::Route::TuneAiming: DrawAimingTab(s); break;
        case WizardSession::Route::TuneGamepadThrottle: DrawGamepadThrottleTab(s); break;
        case WizardSession::Route::AdvancedMacros: DrawMacrosTab(s); break;
        case WizardSession::Route::AdvancedPluginControls: DrawPluginControls(s); break;
        case WizardSession::Route::AdvancedDevices: DrawDevicesTab(s); break;
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
    return WizardSession::RequestClose();
}

static bool SaveAndCloseWorkbench() {
    if (!WizardSession::SaveCurrentProfile()) return false;
    WizardSession::CancelPendingClose();
    UIHook::ToggleUI();
    return true;
}

static bool CloseWorkbenchWithoutSaving() {
    if (WizardSession::HasUnsavedChanges() && !WizardSession::DiscardChanges()) return false;
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

    ImGui::Text("%s has unsaved changes.",
        VisibleProfileName(WizardConfig::GetEditProfile()).c_str());
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
    WizardSession::UpdateCapture(OnCaptureCommit);
    auto& s = WizardConfig::GetState();
    const bool dirty = WizardSession::HasUnsavedChanges();

    ImGui::SetNextWindowSize(ImVec2(800, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(620, 480), ImVec2(1600, 1200));
    const ImGuiWindowFlags shellFlags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool windowOpen = true;
    const bool windowVisible = ImGui::Begin("AbsoluteHOTAS Binding Wizard", &windowOpen, shellFlags);
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

    DrawProfileContextBar(dirty);
    DrawPrimaryNavigation();
    DrawSecondaryNavigation();
    ImGui::Separator();

    constexpr float footerButtonHeight = 36.0f;
    const float footerHeight = footerButtonHeight + ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    const float pageHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - footerHeight);
    ImGui::BeginChild("WizardPageHost", ImVec2(0, pageHeight), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    static WizardSession::Route lastRenderedRoute = WizardSession::GetRoute();
    if (lastRenderedRoute != WizardSession::GetRoute()) {
        ImGui::SetScrollY(0.0f);
        lastRenderedRoute = WizardSession::GetRoute();
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
        if (ImGui::Button("Save & Apply", ImVec2(-1.0f, footerButtonHeight))) SaveCurrentProfile();
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
    const bool footerDirty = WizardSession::HasUnsavedChanges();
    if (status.kind == WizardSession::StatusKind::Error && !status.message.empty()) {
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
    UIHook::SetDrawCallback(&BindingWizard::Draw);
    UIHook::SetCloseGuardCallback(&CanCloseWorkbench);
    WizLog("BindingWizard registered with UIHook.");
}

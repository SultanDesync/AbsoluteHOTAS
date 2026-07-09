#include "PCH.h"

#include "BindingWizard.h"
#include "WizardDefs.h"
#include "WizardCapture.h"
#include "WizardConfig.h"
#include "ThrottleController.h"
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
    }
}

// --- Shared UI helper: draw a binding row with Bind/Clear ---
static void DrawBindingRow(const char* label, std::string& binding, int captureSlot, bool isAxis) {
    auto& pending = WizardCapture::GetPendingBind();

    if (label[0] != '\0') {
        ImGui::Text("%-22s", label);
        ImGui::SameLine(180);
    }

    std::string displayStr = WizardConfig::FormatBindingDisplay(binding);
    ImVec4 color = (binding == "(unbound)")
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        : ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
    ImGui::TextColored(color, "%s", displayStr.c_str());
    ImGui::SameLine(500);

    bool isCapturing = pending.active && pending.targetConfigSlot == captureSlot;
    if (isCapturing) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), isAxis ? ">> Move axis..." : ">> Press button...");
        ImGui::SameLine();
        ImGui::PushID(captureSlot + 90000);
        if (ImGui::SmallButton("Cancel")) {
            pending.active = false;
        }
        ImGui::PopID();
    } else {
        ImGui::PushID(captureSlot);
        if (ImGui::SmallButton("Bind")) {
            if (isAxis) {
                WizardCapture::StartAxisCapture(captureSlot, label);
            } else {
                int category = (captureSlot / 100) * 100;
                int index = captureSlot % 100;
                WizardCapture::StartButtonCapture(index, category, label);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            binding = "(unbound)";
        }
        ImGui::PopID();
    }
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
            float liveX = NormThrottleRaw(rawVal) * barWidth;
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

// --- Tab: Devices ---
static void DrawDevicesTab(WizardState& s) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Connected HID Devices");
    ImGui::Separator();

    int devCount = DeviceManager::GetDeviceCount();
    if (devCount == 0) ImGui::TextWrapped("No DirectInput devices detected.");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Device indices (#N) follow USB enumeration order.");
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
                ImGui::SameLine(400);
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
                    for (auto& b : s.digitalAxisBindings) allBindings.push_back(&b);
                    for (auto& sa : s.shipActionSlots) allBindings.push_back(&sa.binding);
                    for (auto& cb : s.customBindings) allBindings.push_back(&cb.buttonBinding);
                    for (auto& b : s.aimAxisBindings) allBindings.push_back(&b);
                    for (auto& b : s.digitalAimBindings) allBindings.push_back(&b);

                    for (auto* bp : allBindings) doSwap(*bp);
                    for (auto* bp : allBindings) finalize(*bp);

                    WizardConfig::SaveBindingsToINI();
                    WizLog("Swapped device indices #" + std::to_string(d) + " <-> #" + std::to_string(d + 1));
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

// --- Tab: Axes & Settings ---
static void DrawAxesTab(WizardState& s) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Flight Axis Assignments");
    ImGui::TextWrapped("Click 'Bind' then move the physical axis you want to assign.");
    ImGui::SameLine(500);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), ">> Save & Apply to commit changes");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < kNumAxisSlots; i++) {
        ImGui::PushID(i);
        if (i > 0) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }

        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", kAxisSlots[i].label);
        ImGui::SameLine(180);
        DrawBindingRow("", s.axisBindings[i], i, true);

        if (kAxisSlots[i].invertIniKey) {
            ImGui::SameLine(640);
            ImGui::Checkbox("Inv", &s.axisInvert[i]);
        }

        if (kAxisSlots[i].sensitivityKey) {
            ImGui::Indent(180);
            ImGui::PushItemWidth(120);
            ImGui::SliderFloat("Sens", &s.axisSensitivity[i], 0.1f, 3.0f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::Unindent(180);
        }

        if (kAxisSlots[i].saturationKey) {
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
        if (kAxisSlots[i].deadzoneKey) {
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
}

// --- Tab: Aiming ---
static void DrawAimingTab(WizardState& s) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aiming System");
    ImGui::TextWrapped("Controls how the aiming reticle and ship steering interact. Enable the aim system, then choose a mode below.");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Enable Aim System", &s.sourceObjectAim);
    if (!s.sourceObjectAim) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Aim system disabled. Ship steering uses cluster gates only.");
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
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", kAimAxisSlots[i].label);
        ImGui::SameLine(180);
        DrawBindingRow("", s.aimAxisBindings[i], CaptureSlot::kAimAxisBase + i, true);
        ImGui::SameLine(640);
        ImGui::Checkbox("Inv", &s.aimAxisInvert[i]);
        ImGui::Indent(180);
        ImGui::PushItemWidth(120);
        ImGui::SliderFloat("Sens", &s.aimAxisSensitivity[i], 0.1f, 3.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::Unindent(180);
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

static void DrawAdvancedModesTab(WizardState& s) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "HOSAM / HOSAS");
    ImGui::TextWrapped("Configure paradigm-shifting playstyles that fundamentally change how the ship is controlled.");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Dual-Stick / Accumulator Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12);
        ImGui::Checkbox("Enable Accumulator Throttle", &s.accumulatorThrottle);
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
        ImGui::Unindent(12);
    }
    
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("HOSAM Mode (Stick + Mouse)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12);
        ImGui::Checkbox("Enable HOSAM Mode", &s.hosamMode);
        if (s.hosamMode) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "(Active)");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "Mouse drives steering. Pitch/Yaw axes released to native mouse.");
            ImGui::Spacing();
            ImGui::Checkbox("Alignment Assist", &s.alignmentAssist);
            if (s.alignmentAssist) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "Gently centers steering when mouse is idle near center.");
                ImGui::PushItemWidth(180);
                ImGui::SliderFloat("Radius##alignRad", &s.alignmentRadius, 1.0f, 200.0f, "%.0f units");
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "of 200 max");
                int idleMs = s.alignmentIdleMs;
                if (ImGui::SliderInt("Idle Time##alignIdle", &idleMs, 10, 500, "%d ms")) s.alignmentIdleMs = idleMs;
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Before decay starts");
                ImGui::SliderFloat("Decay Speed##alignDecay", &s.alignmentDecayRate, 0.5f, 30.0f, "%.1f");
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Higher = faster snap");
                ImGui::PopItemWidth();
            }
        } else {
            ImGui::TextWrapped("Use a joystick for throttle/strafe and your mouse for steering. Pitch and Yaw are released to the game's native mouse pipeline.");
        }
        ImGui::Unindent(12);
    }
}

static void DrawButtonsTab(WizardState& s) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Buttons");
    ImGui::TextWrapped("Configure button bindings for vanilla ship actions and plugin controls.");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Core Ship Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12);
        ImGui::TextWrapped("These are vanilla ship bindings. Bind your controller buttons to emit the default keyboard/mouse outputs set in Starfield's 'Spaceflight Bindings' menu.");
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

    if (ImGui::CollapsingHeader("Custom Key Bindings", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent(12);
        ImGui::TextWrapped("Bind controller buttons to emit a single custom keyboard/mouse output. Use Starfield's vanilla binding menu to assign matching secondary bindings. For multi-key sequences or chords, use the Macros tab.");
        ImGui::Spacing();

        if (ImGui::Button("Add Binding")) s.customBindings.push_back({"(unbound)", "none"});
        ImGui::SameLine();
        if (ImGui::Button("Add Menu Cluster")) {
            s.customBindings.push_back({"(unbound)", "key:0x11"});
            s.customBindings.push_back({"(unbound)", "key:0x1E"});
            s.customBindings.push_back({"(unbound)", "key:0x1F"});
            s.customBindings.push_back({"(unbound)", "key:0x20"});
            s.customBindings.push_back({"(unbound)", "key:0x0F"});
            s.customBindings.push_back({"(unbound)", "key:0x12"});
            s.customBindings.push_back({"(unbound)", "key:0x01"});
            WizLog("Added menu cluster preset (WASD/Tab/E/Esc).");
        }
        ImGui::Spacing();

        int removeIdx = -1;
        for (int i = 0; i < (int)s.customBindings.size(); i++) {
            auto& row = s.customBindings[i];
            ImGui::PushID(5000 + i);
            ImGui::Text("%-22s", row.buttonBinding.c_str());
            ImGui::SameLine(180);

            int currentOutput = FindOutputIndex(row.output);
            const char* previewLabel = (currentOutput >= 0) ? kOutputCatalog[currentOutput].label : row.output.c_str();
            ImGui::PushItemWidth(120);
            if (ImGui::BeginCombo("##output", previewLabel)) {
                for (int j = 0; j < kOutputCatalogSize; j++) {
                    bool selected = (j == currentOutput);
                    if (ImGui::Selectable(kOutputCatalog[j].label, selected)) row.output = kOutputCatalog[j].value;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::SmallButton("Bind")) {
                char label[64];
                int outputIdx = FindOutputIndex(row.output);
                std::snprintf(label, sizeof(label), "Custom #%d (%s)", i + 1, outputIdx >= 0 ? kOutputCatalog[outputIdx].label : "?");
                WizardCapture::StartButtonCapture(i, CaptureSlot::kCustomBase, label);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) row.buttonBinding = "(unbound)";
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0) s.customBindings.erase(s.customBindings.begin() + removeIdx);
        if (s.customBindings.empty())
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No custom bindings. Click 'Add Binding' or 'Add Menu Cluster' to get started.");
        ImGui::Unindent(12);
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Digital Axis Emulation", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent(12);
        ImGui::TextWrapped("Bind buttons to emulate axis input digitally (on/off). Useful for hat switches.");
        ImGui::Spacing();
        for (int i = 0; i < kNumDigitalAxisSlots; i++) {
            ImGui::PushID(4000 + i);
            DrawBindingRow(kDigitalAxisSlots[i].label, s.digitalAxisBindings[i], CaptureSlot::kDigitalAxisBase + i, false);
            ImGui::PopID();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::PushItemWidth(120);
        ImGui::SliderFloat("Roll Value", &s.digitalRollValue, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Strafe Value", &s.digitalStrafeValue, 0.0f, 1.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::Unindent(12);
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Plugin Controls", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent(12);
        ImGui::TextWrapped("Bind physical buttons to control the AbsoluteHOTAS plugin itself.");
        ImGui::Spacing();
        for (int i = 0; i < kNumButtonSlots; i++) {
            ImGui::PushID(2000 + i);
            DrawBindingRow(kButtonSlots[i].label, s.buttonBindings[i], CaptureSlot::kButtonBase + i, false);
            ImGui::PopID();
        }
        ImGui::Unindent(12);
    }
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
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Macros");
    ImGui::TextWrapped(
        "A macro plays an ordered sequence of key actions from one button press. Steps can target "
        "ship actions (which follow your in-game Starfield keybinds automatically) or raw keys. "
        "Press '+' on a step to add targets pressed together as a chord.");
    ImGui::TextWrapped(
        "One press runs the whole sequence to the end - you do not need to hold the button. "
        "Turbo instead repeats the sequence for as long as the button IS held.");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Add Macro")) {
        MacroRow m;
        m.name = UniqueMacroName(s);
        m.steps.push_back({ {"NextSystem"}, false, 1, 50 });
        s.macros.push_back(std::move(m));
    }
    ImGui::SameLine();
    if (ImGui::Button("Add \"Grav -> Shields\" Preset")) {
        MacroRow m;
        m.name = "GravToShields";
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

        // Duplicate names collide into one INI section, so the save skips the later
        // one. Say so here rather than letting a macro silently not persist.
        bool duplicate = false;
        for (int j = 0; j < mi; j++) {
            if (s.macros[j].name == m.name) { duplicate = true; break; }
        }

        char header[160];
        std::snprintf(header, sizeof(header), "%s%s###macro%d",
                      m.name.empty() ? "(unnamed)" : m.name.c_str(),
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
            } else if (duplicate) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "duplicate name - will not save");
            }

            if (mi < 100) {
                DrawBindingRow("Trigger Button", m.buttonBinding, CaptureSlot::kMacroBase + mi, false);
            }

            ImGui::Checkbox("Turbo (repeat while held)", &m.turbo);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off: one press plays the sequence once, to the end.\n"
                                  "On:  the sequence repeats while the button is held,\n"
                                  "     and stops as soon as you release it.");
            ImGui::SameLine(500);
            if (ImGui::SmallButton("Delete Macro")) removeMacro = mi;

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

// --- Profiles footer row (Export / Import) ---
static void DrawProfilesRow() {
    static char profileName[64] = "";
    static int  selectedProfile = 0;
    static std::string profileStatus;

    ImGui::Spacing();

    // Export: name field + button.
    ImGui::PushItemWidth(160);
    ImGui::InputTextWithHint("##profilename", "profile name", profileName, sizeof(profileName));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        std::string err;
        if (WizardConfig::ExportProfile(profileName, err))
            profileStatus = std::string("Exported '") + profileName + "'.";
        else
            profileStatus = "Export failed: " + err;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Import: profile dropdown + button. The list is the user's own exports;
    // auto-backups are hidden but remain on disk under Profiles/ as a safety net.
    auto profiles = WizardConfig::ListProfiles();
    if (selectedProfile >= (int)profiles.size()) selectedProfile = 0;
    const char* preview = profiles.empty() ? "(no profiles)" : profiles[selectedProfile].c_str();
    ImGui::PushItemWidth(160);
    if (ImGui::BeginCombo("##profilelist", preview)) {
        for (int i = 0; i < (int)profiles.size(); i++) {
            bool sel = (i == selectedProfile);
            if (ImGui::Selectable(profiles[i].c_str(), sel)) selectedProfile = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::BeginDisabled(profiles.empty());
    if (ImGui::Button("Import")) {
        std::string err;
        if (WizardConfig::ImportProfile(profiles[selectedProfile], err))
            profileStatus = std::string("Imported '") + profiles[selectedProfile] +
                            "'. Previous setup auto-backed up.";
        else
            profileStatus = "Import failed: " + err;
    }
    ImGui::EndDisabled();

    if (!profileStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.9f, 1.0f), "%s", profileStatus.c_str());
    }
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
    if (configGen != s_lastConfigGen) {
        s_lastConfigGen = configGen;
        WizardConfig::GetState() = WizardState{};
    }

    WizardConfig::LoadCurrentBindings();
    WizardCapture::UpdateCapture(OnCaptureCommit);
    auto& s = WizardConfig::GetState();

    ImGui::SetNextWindowSize(ImVec2(760, 680), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AbsoluteHOTAS Binding Wizard", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + 20.0f;
    ImGui::BeginChild("WizardTabsChild", ImVec2(0, -footerHeightToReserve), false);

    if (ImGui::BeginTabBar("WizardTabs")) {

        if (ImGui::BeginTabItem("Hardware & Devices")) {
            DrawDevicesTab(s);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Flight Axes")) {
            DrawAxesTab(s);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Aiming & Combat")) {
            DrawAimingTab(s);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Buttons")) {
            DrawButtonsTab(s);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Macros")) {
            DrawMacrosTab(s);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HOSAM/HOSAS")) {
            DrawAdvancedModesTab(s);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // Fixed Footer for Save Button
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
    if (ImGui::Button("Save & Apply", ImVec2(160, 36))) {
        WizardConfig::SaveBindingsToINI();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Writes to AbsoluteHOTAS_User.ini and reloads live.");

    DrawProfilesRow();

    ImGui::End();
}

void BindingWizard::Initialize() {
    UIHook::SetDrawCallback(&BindingWizard::Draw);
    WizLog("BindingWizard registered with UIHook.");
}

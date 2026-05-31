#include "BindingWizard.h"
#include "UIHook.h"
#include "DeviceManager.h"
#include "ThrottleController.h"
#include "RuntimePaths.h"

#include <imgui.h>

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

static void WizLog(const std::string& msg) {
    RuntimePaths::AppendLog("[BindingWizard]", msg);
}

// ============================================================================
// Axis name helper
// ============================================================================
static const char* AxisName(int usageId) {
    switch (usageId) {
        case 0x30: return "X";
        case 0x31: return "Y";
        case 0x32: return "Z";
        case 0x33: return "Rx";
        case 0x34: return "Ry";
        case 0x35: return "Rz";
        case 0x36: return "Slider0";
        case 0x37: return "Slider1";
        default: return "?";
    }
}

// Extract an axis value from DIJOYSTATE2 by usage ID
static long GetAxisFromState(const DIJOYSTATE2* st, int usageId) {
    if (!st) return 0;
    switch (usageId) {
        case 0x30: return st->lX;
        case 0x31: return st->lY;
        case 0x32: return st->lZ;
        case 0x33: return st->lRx;
        case 0x34: return st->lRy;
        case 0x35: return st->lRz;
        case 0x36: return st->rglSlider[0];
        case 0x37: return st->rglSlider[1];
        default: return 0;
    }
}

// ============================================================================
// Binding Capture State
// ============================================================================
struct PendingBind {
    bool active = false;
    std::string targetLabel;
    int targetConfigSlot = -1;
    // Snapshot of all axes AND buttons at the moment capture started
    struct DeviceSnapshot {
        int deviceIndex;
        long axes[8];
        BYTE buttons[128];
    };
    std::vector<DeviceSnapshot> snapshots;
};

static PendingBind s_pendingBind;

// ============================================================================
// Slot definitions — axes with invert/sensitivity
// ============================================================================
struct AxisSlot {
    const char* label;
    const char* iniKey;
    const char* invertIniKey;     // May be nullptr
    const char* sensitivityKey;   // May be nullptr
};

static const AxisSlot kAxisSlots[] = {
    {"Throttle",         "iThrottleAxis",    "bInvertThrottle",    nullptr},
    {"Pitch",            "iPitchAxis",       "bInvertPitch",       "fPitchSensitivity"},
    {"Yaw",              "iYawAxis",         "bInvertYaw",         "fYawSensitivity"},
    {"Roll",             "iRollAxis",        "bInvertRoll",        "fRollSensitivity"},
    {"Strafe Lateral",   "iStrafeLatAxis",   "bInvertStrafeLat",   "fStrafeSensitivity"},
    {"Strafe Vertical",  "iStrafeVertAxis",  "bInvertStrafeVert",  nullptr},
    {"Reverse",          "iReverseAxis",     "bInvertReverse",     "fReverseSensitivity"},
};
static constexpr int kNumAxisSlots = sizeof(kAxisSlots) / sizeof(kAxisSlots[0]);

// Control button slots (activate/stop only — boost deprecated)
struct ButtonSlot {
    const char* label;
    const char* iniKey;
};

static const ButtonSlot kButtonSlots[] = {
    {"Activate",  "iActivateButtonId"},
    {"Stop",      "iStopButtonId"},
};
static constexpr int kNumButtonSlots = sizeof(kButtonSlots) / sizeof(kButtonSlots[0]);

// Ship action slots (populated from ThrottleController at load time)
struct ShipActionSlot {
    std::string label;
    std::string iniKey;
    std::string binding;
};
static std::vector<ShipActionSlot> s_shipActionSlots;

// Digital axis button slots
struct DigitalAxisSlot {
    const char* label;
    const char* iniKey;
};
static const DigitalAxisSlot kDigitalAxisSlots[] = {
    {"Digital Reverse",       "iDigitalReverseButton"},
    {"Digital Roll Left",     "iDigitalRollLeftButton"},
    {"Digital Roll Right",    "iDigitalRollRightButton"},
    {"Digital Strafe Left",   "iDigitalStrafeLeftButton"},
    {"Digital Strafe Right",  "iDigitalStrafeRightButton"},
    {"Digital Strafe Up",     "iDigitalStrafeUpButton"},
    {"Digital Strafe Down",   "iDigitalStrafeDownButton"},
};
static constexpr int kNumDigitalAxisSlots = sizeof(kDigitalAxisSlots) / sizeof(kDigitalAxisSlots[0]);

// Resolved binding strings for each category
static std::string s_axisBindings[kNumAxisSlots];
static bool        s_axisInvert[kNumAxisSlots];
static float       s_axisSensitivity[kNumAxisSlots];  // 0 = n/a
static std::string s_buttonBindings[kNumButtonSlots];
static std::string s_digitalAxisBindings[kNumDigitalAxisSlots];
static float       s_digitalRollValue = 1.0f;
static float       s_digitalStrafeValue = 1.0f;
static bool        s_bindingsLoaded = false;

// ============================================================================
// Format a BindingRef as a display string
// ============================================================================
static std::string FormatRef(const BindingRef& ref) {
    if (!ref.IsValid() || ref.value <= 0) return "(unbound)";
    char buf[256];
    if (ref.deviceName.empty()) {
        sprintf_s(buf, "0x%02X", ref.value);
    } else {
        sprintf_s(buf, "%s@0x%02X", ref.deviceName.c_str(), ref.value);
    }
    return buf;
}

static std::string FormatButtonRef(const BindingRef& ref) {
    if (!ref.IsValid() || ref.value <= 0) return "(unbound)";
    char buf[256];
    if (ref.deviceName.empty()) {
        sprintf_s(buf, "%d", ref.value);
    } else {
        sprintf_s(buf, "%s@%d", ref.deviceName.c_str(), ref.value);
    }
    return buf;
}

// ============================================================================
// Load current bindings from running config
// ============================================================================
static void LoadCurrentBindings() {
    if (s_bindingsLoaded) return;

    auto& cfg = ThrottleController::GetConfig();

    s_axisBindings[0] = FormatRef(cfg.throttleAxis);
    s_axisBindings[1] = FormatRef(cfg.pitchAxis);
    s_axisBindings[2] = FormatRef(cfg.yawAxis);
    s_axisBindings[3] = FormatRef(cfg.rollAxis);
    s_axisBindings[4] = FormatRef(cfg.strafeLatAxis);
    s_axisBindings[5] = FormatRef(cfg.strafeVertAxis);
    s_axisBindings[6] = FormatRef(cfg.reverseAxis);

    s_axisInvert[0] = cfg.bInvertThrottle;
    s_axisInvert[1] = cfg.bInvertPitch;
    s_axisInvert[2] = cfg.bInvertYaw;
    s_axisInvert[3] = cfg.bInvertRoll;
    s_axisInvert[4] = cfg.bInvertStrafeLat;
    s_axisInvert[5] = cfg.bInvertStrafeVert;
    s_axisInvert[6] = cfg.bInvertReverse;

    s_axisSensitivity[0] = 0.0f; // Throttle has no sensitivity
    s_axisSensitivity[1] = cfg.fPitchSensitivity;
    s_axisSensitivity[2] = cfg.fYawSensitivity;
    s_axisSensitivity[3] = cfg.fRollSensitivity;
    s_axisSensitivity[4] = cfg.fStrafeSensitivity;
    s_axisSensitivity[5] = 0.0f; // Strafe vert has no sensitivity
    s_axisSensitivity[6] = cfg.fReverseSensitivity;

    s_buttonBindings[0] = FormatButtonRef(cfg.activateButton);
    s_buttonBindings[1] = FormatButtonRef(cfg.stopButton);

    // Ship actions
    auto shipActions = ThrottleController::GetShipActionBindings();
    s_shipActionSlots.clear();
    for (auto& sa : shipActions) {
        s_shipActionSlots.push_back({
            sa.label,
            sa.iniKey,
            FormatButtonRef(sa.binding)
        });
    }

    // Digital axes
    s_digitalAxisBindings[0] = FormatButtonRef(cfg.digitalReverseButton);
    s_digitalAxisBindings[1] = FormatButtonRef(cfg.digitalRollLeftButton);
    s_digitalAxisBindings[2] = FormatButtonRef(cfg.digitalRollRightButton);
    s_digitalAxisBindings[3] = FormatButtonRef(cfg.digitalStrafeLeftButton);
    s_digitalAxisBindings[4] = FormatButtonRef(cfg.digitalStrafeRightButton);
    s_digitalAxisBindings[5] = FormatButtonRef(cfg.digitalStrafeUpButton);
    s_digitalAxisBindings[6] = FormatButtonRef(cfg.digitalStrafeDownButton);
    s_digitalRollValue = cfg.digitalRollValue;
    s_digitalStrafeValue = cfg.digitalStrafeValue;

    s_bindingsLoaded = true;
}

// ============================================================================
// Capture helpers
// ============================================================================
static void TakeSnapshots() {
    s_pendingBind.snapshots.clear();
    int count = DeviceManager::GetDeviceCount();
    for (int d = 0; d < count; d++) {
        const auto* st = DeviceManager::GetCachedState(d);
        if (!st) continue;

        PendingBind::DeviceSnapshot snap;
        snap.deviceIndex = d;
        for (int a = 0; a < 8; a++) {
            snap.axes[a] = GetAxisFromState(st, 0x30 + a);
        }
        memcpy(snap.buttons, st->rgbButtons, 128);
        s_pendingBind.snapshots.push_back(snap);
    }
}

// Capture target encoding:
//   0..99       = axis slots
//   100..199    = control button slots
//   200..299    = ship action slots
//   300..399    = digital axis slots

static void StartAxisCapture(int slotIndex, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = slotIndex;
    TakeSnapshots();
    WizLog("Axis capture started for: " + std::string(label));
}

static void StartButtonCapture(int slotIndex, int categoryOffset, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = categoryOffset + slotIndex;
    TakeSnapshots();
    WizLog("Button capture started for: " + std::string(label));
}

static void UpdateCapture() {
    if (!s_pendingBind.active) return;

    int slot = s_pendingBind.targetConfigSlot;

    if (slot >= 0 && slot < 100) {
        // Axis capture: compare current state to snapshot
        constexpr long kAxisThreshold = 8000;

        for (auto& snap : s_pendingBind.snapshots) {
            if (snap.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
            if (!st) continue;

            for (int a = 0; a < 8; a++) {
                long current = GetAxisFromState(st, 0x30 + a);
                long delta = std::abs(current - snap.axes[a]);
                if (delta > kAxisThreshold) {
                    const auto& info = DeviceManager::GetDevice(snap.deviceIndex);
                    int usageId = 0x30 + a;
                    char buf[256];
                    sprintf_s(buf, "%s@0x%02X", info.productName.c_str(), usageId);

                    s_axisBindings[slot] = buf;
                    WizLog("Axis captured: " + std::string(buf) + " for " + s_pendingBind.targetLabel);
                    s_pendingBind.active = false;
                    return;
                }
            }
        }
    } else {
        // Button capture (all categories): detect delta
        for (auto& snap : s_pendingBind.snapshots) {
            if (snap.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
            if (!st) continue;

            for (int b = 0; b < 128; b++) {
                bool nowPressed = (st->rgbButtons[b] & 0x80) != 0;
                bool wasPressed = (snap.buttons[b] & 0x80) != 0;
                if (nowPressed && !wasPressed) {
                    const auto& info = DeviceManager::GetDevice(snap.deviceIndex);
                    char buf[256];
                    sprintf_s(buf, "%s@%d", info.productName.c_str(), b + 1);

                    // Route to the correct binding array
                    if (slot >= 100 && slot < 200) {
                        s_buttonBindings[slot - 100] = buf;
                    } else if (slot >= 200 && slot < 300) {
                        int idx = slot - 200;
                        if (idx < (int)s_shipActionSlots.size()) {
                            s_shipActionSlots[idx].binding = buf;
                        }
                    } else if (slot >= 300 && slot < 400) {
                        s_digitalAxisBindings[slot - 300] = buf;
                    }

                    WizLog("Button captured: " + std::string(buf) + " for " + s_pendingBind.targetLabel);
                    s_pendingBind.active = false;
                    return;
                }
            }
        }
    }
}

// ============================================================================
// Save all bindings to INI
// ============================================================================
static void SaveBindingsToINI() {
    auto iniPath = RuntimePaths::IniPath();
    WizLog("Saving bindings to: " + iniPath.string());

    CSimpleIniA ini;
    ini.SetUnicode(false);
    ini.LoadFile(iniPath.string().c_str());

    // Axes
    for (int i = 0; i < kNumAxisSlots; i++) {
        if (s_axisBindings[i] != "(unbound)") {
            ini.SetValue("Hardware", kAxisSlots[i].iniKey, s_axisBindings[i].c_str());
        }
        if (kAxisSlots[i].invertIniKey) {
            ini.SetBoolValue("Hardware", kAxisSlots[i].invertIniKey, s_axisInvert[i]);
        }
        if (kAxisSlots[i].sensitivityKey && s_axisSensitivity[i] > 0.0f) {
            char sensStr[32];
            sprintf_s(sensStr, "%.2f", s_axisSensitivity[i]);
            ini.SetValue("Hardware", kAxisSlots[i].sensitivityKey, sensStr);
        }
    }

    // Control buttons
    for (int i = 0; i < kNumButtonSlots; i++) {
        if (s_buttonBindings[i] != "(unbound)") {
            ini.SetValue("Buttons", kButtonSlots[i].iniKey, s_buttonBindings[i].c_str());
        } else {
            ini.SetValue("Buttons", kButtonSlots[i].iniKey, "-1");
        }
    }

    // Ship actions
    for (auto& sa : s_shipActionSlots) {
        if (sa.binding != "(unbound)") {
            ini.SetValue("ShipButtons", sa.iniKey.c_str(), sa.binding.c_str());
        } else {
            ini.SetValue("ShipButtons", sa.iniKey.c_str(), "-1");
        }
    }

    // Digital axes
    for (int i = 0; i < kNumDigitalAxisSlots; i++) {
        if (s_digitalAxisBindings[i] != "(unbound)") {
            ini.SetValue("DigitalAxes", kDigitalAxisSlots[i].iniKey, s_digitalAxisBindings[i].c_str());
        } else {
            ini.SetValue("DigitalAxes", kDigitalAxisSlots[i].iniKey, "-1");
        }
    }
    char rollStr[32], strafeStr[32];
    sprintf_s(rollStr, "%.2f", s_digitalRollValue);
    sprintf_s(strafeStr, "%.2f", s_digitalStrafeValue);
    ini.SetValue("DigitalAxes", "fDigitalRollValue", rollStr);
    ini.SetValue("DigitalAxes", "fDigitalStrafeValue", strafeStr);

    ini.SaveFile(iniPath.string().c_str());
    WizLog("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    s_bindingsLoaded = false;
    WizLog("Config reloaded.");
}

// ============================================================================
// Helper: draw a binding row with Bind/Clear buttons
// ============================================================================
static void DrawBindingRow(const char* label, std::string& binding, int captureSlot, bool isAxis) {
    ImGui::Text("%-22s", label);
    ImGui::SameLine(180);

    ImVec4 color = (binding == "(unbound)")
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        : ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
    ImGui::TextColored(color, "%s", binding.c_str());
    ImGui::SameLine(500);

    bool isCapturing = s_pendingBind.active && s_pendingBind.targetConfigSlot == captureSlot;
    if (isCapturing) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), isAxis ? ">> Move axis... <<" : ">> Press button... <<");
    } else {
        ImGui::PushID(captureSlot);
        if (ImGui::SmallButton("Bind")) {
            if (isAxis) {
                StartAxisCapture(captureSlot, label);
            } else {
                int category = (captureSlot / 100) * 100;
                int index = captureSlot % 100;
                StartButtonCapture(index, category, label);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            binding = "(unbound)";
        }
        ImGui::PopID();
    }
}

// ============================================================================
// Draw: Main ImGui UI
// ============================================================================
void BindingWizard::Draw() {
    // Ensure all enumerated devices are open for polling on first draw.
    static bool s_allDevicesOpened = false;
    if (!s_allDevicesOpened) {
        DeviceManager::OpenAllDevices();
        s_allDevicesOpened = true;
    }

    LoadCurrentBindings();
    UpdateCapture();

    ImGui::SetNextWindowSize(ImVec2(760, 680), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AbsoluteHOTAS Binding Wizard", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // ---- Tab bar ----
    if (ImGui::BeginTabBar("WizardTabs")) {

        // ==== TAB 1: Live Device Monitor ====
        if (ImGui::BeginTabItem("Devices")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Connected HID Devices");
            ImGui::Separator();

            int devCount = DeviceManager::GetDeviceCount();
            if (devCount == 0) {
                ImGui::TextWrapped("No DirectInput devices detected.");
            }

            for (int d = 0; d < devCount; d++) {
                const auto& info = DeviceManager::GetDevice(d);
                const auto* st = DeviceManager::GetCachedState(d);

                std::string header = std::to_string(d) + ": " + info.productName;
                if (!info.vidpidString.empty()) header += " [" + info.vidpidString + "]";

                if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent(12.0f);
                    ImGui::Text("Instance: %s", info.instanceName.c_str());
                    ImGui::Text("Axes: %d  Buttons: %d", info.axisCount, info.buttonCount);

                    if (st) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Live Axis Values:");

                        for (int a = 0; a < 8; a++) {
                            int usageId = 0x30 + a;
                            long val = GetAxisFromState(st, usageId);
                            float normalized = static_cast<float>(val) / 65535.0f;

                            char label[64];
                            sprintf_s(label, "0x%02X (%s)", usageId, AxisName(usageId));

                            ImGui::PushID(d * 100 + a);
                            ImGui::ProgressBar(normalized, ImVec2(200, 0), "");
                            ImGui::SameLine();
                            ImGui::Text("%-14s %6ld", label, val);
                            ImGui::PopID();
                        }

                        // Active buttons
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Active Buttons:");
                        std::string pressed;
                        for (int b = 0; b < 128; b++) {
                            if (st->rgbButtons[b] & 0x80) {
                                if (!pressed.empty()) pressed += ", ";
                                pressed += std::to_string(b + 1);
                            }
                        }
                        ImGui::TextWrapped("%s", pressed.empty() ? "(none)" : pressed.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Device not open/polling.");
                    }

                    ImGui::Unindent(12.0f);
                }
            }
            ImGui::EndTabItem();
        }

        // ==== TAB 2: Axes & Settings ====
        if (ImGui::BeginTabItem("Axes & Settings")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Flight Axis Assignments");
            ImGui::TextWrapped("Click 'Bind' then move the physical axis you want to assign.");
            ImGui::Separator();
            ImGui::Spacing();

            // Header row
            ImGui::Text("%-22s %-32s %-8s %-12s", "Axis", "Binding", "Invert", "Sensitivity");
            ImGui::Separator();

            for (int i = 0; i < kNumAxisSlots; i++) {
                ImGui::PushID(i);

                // Binding + Bind/Clear
                DrawBindingRow(kAxisSlots[i].label, s_axisBindings[i], i, true);

                // Inversion checkbox on same line
                if (kAxisSlots[i].invertIniKey) {
                    ImGui::SameLine(640);
                    ImGui::Checkbox("Inv", &s_axisInvert[i]);
                }

                // Sensitivity slider (next line, indented)
                if (kAxisSlots[i].sensitivityKey) {
                    ImGui::Indent(180);
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("Sens", &s_axisSensitivity[i], 0.1f, 3.0f, "%.2f");
                    ImGui::PopItemWidth();
                    ImGui::Unindent(180);
                }

                ImGui::PopID();
            }

            ImGui::EndTabItem();
        }

        // ==== TAB 3: Control Buttons ====
        if (ImGui::BeginTabItem("Control Buttons")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Plugin Control Buttons");
            ImGui::TextWrapped("Click 'Bind' then press the physical button to assign.");
            ImGui::Separator();
            ImGui::Spacing();

            for (int i = 0; i < kNumButtonSlots; i++) {
                ImGui::PushID(2000 + i);
                DrawBindingRow(kButtonSlots[i].label, s_buttonBindings[i], 100 + i, false);
                ImGui::PopID();
            }

            ImGui::EndTabItem();
        }

        // ==== TAB 4: Ship Actions ====
        if (ImGui::BeginTabItem("Ship Actions")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Ship Action Button Bindings");
            ImGui::TextWrapped("Bind physical buttons to ship actions. Each action emits a keyboard/mouse output to Starfield.");
            ImGui::Separator();
            ImGui::Spacing();

            for (int i = 0; i < (int)s_shipActionSlots.size(); i++) {
                ImGui::PushID(3000 + i);
                DrawBindingRow(s_shipActionSlots[i].label.c_str(), s_shipActionSlots[i].binding, 200 + i, false);
                ImGui::PopID();
            }

            ImGui::EndTabItem();
        }

        // ==== TAB 5: Digital Axes ====
        if (ImGui::BeginTabItem("Digital Axes")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Digital Axis Button Bindings");
            ImGui::TextWrapped("Bind buttons to emulate axis input digitally (on/off). Useful for hat switches.");
            ImGui::Separator();
            ImGui::Spacing();

            for (int i = 0; i < kNumDigitalAxisSlots; i++) {
                ImGui::PushID(4000 + i);
                DrawBindingRow(kDigitalAxisSlots[i].label, s_digitalAxisBindings[i], 300 + i, false);
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushItemWidth(120);
            ImGui::SliderFloat("Roll Value", &s_digitalRollValue, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Strafe Value", &s_digitalStrafeValue, 0.0f, 1.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cancel capture button
    if (s_pendingBind.active) {
        if (ImGui::Button("Cancel Capture")) {
            s_pendingBind.active = false;
            WizLog("Capture cancelled by user.");
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "Waiting for input: %s", s_pendingBind.targetLabel.c_str());
    }

    // Save button
    ImGui::Spacing();
    if (ImGui::Button("Save & Apply", ImVec2(160, 36))) {
        SaveBindingsToINI();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Writes to AbsoluteHOTAS.ini and reloads live.");

    ImGui::End();
}

// ============================================================================
// Initialize
// ============================================================================
void BindingWizard::Initialize() {
    UIHook::SetDrawCallback(&BindingWizard::Draw);
    WizLog("BindingWizard registered with UIHook.");
}

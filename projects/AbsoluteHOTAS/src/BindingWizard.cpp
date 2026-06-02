#include "BindingWizard.h"
#include "UIHook.h"
#include "DeviceManager.h"
#include "ThrottleController.h"
#include "RuntimePaths.h"

#include <imgui.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>

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

    // Debounce state for sustained-signal capture
    int debounceDeviceIndex = -1;
    int debounceButtonIndex = -1;
    int debounceButtonFrames = 0;
    int debounceAxisDeviceIndex = -1;
    int debounceAxisIndex = -1;
    int debounceAxisFrames = 0;
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
    {"Activate",       "iActivateButtonId"},
    {"Stop",           "iStopButtonId"},
    {"Toggle Wizard",  "iToggleWizardButton"},
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
// Per-axis calibration state
// ============================================================================
struct CalibrationState {
    bool  active = false;
    int   deviceIndex = -1;
    int   usageId = -1;
    long  observedMin = LONG_MAX;
    long  observedMax = LONG_MIN;
};
static CalibrationState s_calib;

// Stored calibration results: key = (deviceIndex << 8) | usageId, value = {min, max}
static std::unordered_map<int, std::pair<long, long>> s_calibData;
static bool s_calibLoaded = false;

// ============================================================================
// Custom button expansion bindings (Button → Keyboard/Mouse output)
// ============================================================================
struct CustomBindingRow {
    std::string buttonBinding;  // "DeviceName@42" or "(unbound)"
    std::string output;         // "key:0x11" or "mouse:1" or "none"
};
static std::vector<CustomBindingRow> s_customBindings;
static bool s_customBindingsLoaded = false;

struct OutputOption {
    const char* label;
    const char* value;
};
static const OutputOption kOutputCatalog[] = {
    {"W", "key:0x11"}, {"A", "key:0x1E"}, {"S", "key:0x1F"}, {"D", "key:0x20"},
    {"E", "key:0x12"}, {"R", "key:0x13"}, {"F", "key:0x21"}, {"G", "key:0x22"},
    {"Q", "key:0x10"}, {"X", "key:0x2D"}, {"T", "key:0x14"}, {"O", "key:0x18"},
    {"Tab", "key:0x0F"}, {"Space", "key:0x39"}, {"Esc", "key:0x01"},
    {"L Shift", "key:0x2A"}, {"L Ctrl", "key:0x1D"}, {"L Alt", "key:0x38"},
    {"Enter", "key:0x1C"},
    {"Up", "key:0x48"}, {"Down", "key:0x50"}, {"Left", "key:0x4B"}, {"Right", "key:0x4D"},
    {"[", "key:0x1A"}, {"]", "key:0x1B"}, {";", "key:0x27"}, {"'", "key:0x28"},
    {",", "key:0x33"}, {".", "key:0x34"}, {"/", "key:0x35"},
    {"Mouse 1", "mouse:1"}, {"Mouse 2", "mouse:2"}, {"Mouse 3", "mouse:3"}, {"Mouse 4", "mouse:4"},
    {"Numpad 0", "key:0x52"}, {"Numpad 1", "key:0x4F"}, {"Numpad 3", "key:0x51"},
    {"Numpad 5", "key:0x4C"}, {"Numpad 7", "key:0x47"}, {"Numpad 9", "key:0x49"},
    {"F1", "key:0x3B"}, {"F2", "key:0x3C"}, {"F3", "key:0x3D"}, {"F4", "key:0x3E"},
    {"F5", "key:0x3F"}, {"F6", "key:0x40"}, {"F7", "key:0x41"}, {"F8", "key:0x42"},
};
static constexpr int kOutputCatalogSize = sizeof(kOutputCatalog) / sizeof(kOutputCatalog[0]);

// Find the catalog index for a given output value string, or -1
static int FindOutputIndex(const std::string& val) {
    for (int i = 0; i < kOutputCatalogSize; i++) {
        if (val == kOutputCatalog[i].value) return i;
    }
    return -1;
}

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

    // Load calibration data from config
    if (!s_calibLoaded) {
        s_calibData = ThrottleController::GetCalibrationData();
        s_calibLoaded = true;
    }

    // Load custom bindings from [ButtonExpansion]
    if (!s_customBindingsLoaded) {
        s_customBindings.clear();
        auto iniPath = RuntimePaths::IniPath();
        CSimpleIniA ini;
        ini.SetUnicode(false);
        if (ini.LoadFile(iniPath.string().c_str()) == SI_OK) {
            CSimpleIniA::TNamesDepend keys;
            if (ini.GetAllKeys("ButtonExpansion", keys)) {
                for (const auto& entry : keys) {
                    const char* iniKey = entry.pItem;
                    const char* outputVal = ini.GetValue("ButtonExpansion", iniKey, "none");

                    // Parse key: could be "iButton12" or "DeviceName@iButton12"
                    std::string keyStr(iniKey);
                    std::string devicePrefix;
                    std::string btnPart = keyStr;
                    auto atPos = keyStr.rfind('@');
                    if (atPos != std::string::npos) {
                        devicePrefix = keyStr.substr(0, atPos);
                        btnPart = keyStr.substr(atPos + 1);
                    }

                    // Extract button number
                    int btnId = -1;
                    if (btnPart.size() > 7 && (btnPart.substr(0, 7) == "iButton" || btnPart.substr(0, 7) == "IButton")) {
                        btnId = std::atoi(btnPart.c_str() + 7);
                    }
                    if (btnId < 1 || btnId > 128) continue;

                    std::string binding;
                    if (!devicePrefix.empty()) {
                        binding = devicePrefix + "@" + std::to_string(btnId);
                    } else {
                        binding = std::to_string(btnId);
                    }

                    s_customBindings.push_back({ binding, outputVal });
                }
            }
        }
        s_customBindingsLoaded = true;
    }

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

static void ResetDebounce() {
    s_pendingBind.debounceDeviceIndex = -1;
    s_pendingBind.debounceButtonIndex = -1;
    s_pendingBind.debounceButtonFrames = 0;
    s_pendingBind.debounceAxisDeviceIndex = -1;
    s_pendingBind.debounceAxisIndex = -1;
    s_pendingBind.debounceAxisFrames = 0;
}

static void StartAxisCapture(int slotIndex, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = slotIndex;
    ResetDebounce();
    TakeSnapshots();
    WizLog("Axis capture started for: " + std::string(label));
}

static void StartButtonCapture(int slotIndex, int categoryOffset, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = categoryOffset + slotIndex;
    ResetDebounce();
    TakeSnapshots();
    WizLog("Button capture started for: " + std::string(label));
}

static void UpdateCapture() {
    if (!s_pendingBind.active) return;

    int slot = s_pendingBind.targetConfigSlot;

    // Debounce thresholds: require sustained signal over multiple frames
    // to filter out noise, ghost presses, and Proton/Wine artifacts.
    constexpr int kButtonDebounceFrames = 8;  // ~130ms at 60fps
    constexpr int kAxisDebounceFrames = 5;    // ~80ms at 60fps

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
                    // Sustained displacement check
                    if (s_pendingBind.debounceAxisDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceAxisIndex == a) {
                        s_pendingBind.debounceAxisFrames++;
                    } else {
                        s_pendingBind.debounceAxisDeviceIndex = snap.deviceIndex;
                        s_pendingBind.debounceAxisIndex = a;
                        s_pendingBind.debounceAxisFrames = 1;
                    }

                    if (s_pendingBind.debounceAxisFrames >= kAxisDebounceFrames) {
                        const auto& info = DeviceManager::GetDevice(snap.deviceIndex);
                        int usageId = 0x30 + a;
                        char buf[256];
                        sprintf_s(buf, "%s@0x%02X", info.productName.c_str(), usageId);

                        s_axisBindings[slot] = buf;
                        WizLog("Axis captured: " + std::string(buf) + " for " + s_pendingBind.targetLabel);
                        s_pendingBind.active = false;
                        return;
                    }
                } else if (s_pendingBind.debounceAxisDeviceIndex == snap.deviceIndex &&
                           s_pendingBind.debounceAxisIndex == a) {
                    // Axis fell back below threshold — reset debounce for this axis
                    s_pendingBind.debounceAxisFrames = 0;
                }
            }
        }
    } else {
        // Button capture (all categories): detect delta with debounce
        for (auto& snap : s_pendingBind.snapshots) {
            if (snap.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
            if (!st) continue;

            for (int b = 0; b < 128; b++) {
                bool nowPressed = (st->rgbButtons[b] & 0x80) != 0;
                bool wasPressed = (snap.buttons[b] & 0x80) != 0;

                // Only consider buttons that are newly pressed (delta from snapshot)
                if (!nowPressed || wasPressed) {
                    // If this was our debounce candidate but it released, reset
                    if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceButtonIndex == b && !nowPressed) {
                        s_pendingBind.debounceButtonFrames = 0;
                    }
                    continue;
                }

                // Button is pressed and wasn't at snapshot — count debounce frames
                if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                    s_pendingBind.debounceButtonIndex == b) {
                    s_pendingBind.debounceButtonFrames++;
                } else {
                    s_pendingBind.debounceDeviceIndex = snap.deviceIndex;
                    s_pendingBind.debounceButtonIndex = b;
                    s_pendingBind.debounceButtonFrames = 1;
                }

                if (s_pendingBind.debounceButtonFrames >= kButtonDebounceFrames) {
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
                    } else if (slot >= 400 && slot < 600) {
                        int idx = slot - 400;
                        if (idx < (int)s_customBindings.size()) {
                            s_customBindings[idx].buttonBinding = buf;
                        }
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
        } else {
            ini.SetValue("Hardware", kAxisSlots[i].iniKey, "");
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

    // Write calibration data
    ini.Delete("Calibration", nullptr); // Clear entire section
    for (const auto& [key, range] : s_calibData) {
        int devIdx = (key >> 8) & 0xFF;
        int usage = key & 0xFF;
        char keyBuf[64], valBuf[64];
        sprintf_s(keyBuf, "iCalib_%d_0x%02X", devIdx, usage);
        sprintf_s(valBuf, "%ld,%ld", range.first, range.second);
        ini.SetValue("Calibration", keyBuf, valBuf);
    }

    // Write custom button expansion bindings
    ini.Delete("ButtonExpansion", nullptr); // Clear entire section
    for (const auto& row : s_customBindings) {
        if (row.buttonBinding == "(unbound)" || row.output == "none" || row.output.empty()) continue;

        // Convert binding "DeviceName@42" to INI key "DeviceName@iButton42"
        std::string iniKey;
        auto atPos = row.buttonBinding.rfind('@');
        if (atPos != std::string::npos) {
            std::string dev = row.buttonBinding.substr(0, atPos);
            std::string btnNum = row.buttonBinding.substr(atPos + 1);
            iniKey = dev + "@iButton" + btnNum;
        } else {
            iniKey = "iButton" + row.buttonBinding;
        }
        ini.SetValue("ButtonExpansion", iniKey.c_str(), row.output.c_str());
    }

    ini.SaveFile(iniPath.string().c_str());
    WizLog("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    // NOTE: Do NOT reset s_bindingsLoaded here. The UI locals already hold the
    // correct values. ReloadConfig() is async (control-loop thread), so reading
    // back from GetConfig() on the next frame would revert to stale data.
    WizLog("Config reload requested. UI retains current values.");
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
                            int calibKey = (d << 8) | usageId;

                            // Normalize using calibrated range if available
                            float normalized;
                            auto calibIt = s_calibData.find(calibKey);
                            if (calibIt != s_calibData.end()) {
                                long cmin = calibIt->second.first;
                                long cmax = calibIt->second.second;
                                long crange = cmax - cmin;
                                normalized = (crange > 0) ? std::clamp((float)(val - cmin) / (float)crange, 0.0f, 1.0f) : 0.0f;
                            } else {
                                normalized = static_cast<float>(val) / 65535.0f;
                            }

                            char label[64];
                            sprintf_s(label, "0x%02X (%s)", usageId, AxisName(usageId));

                            ImGui::PushID(d * 100 + a);
                            ImGui::ProgressBar(normalized, ImVec2(200, 0), "");
                            ImGui::SameLine();
                            ImGui::Text("%-14s %6ld", label, val);

                            // Calibration controls
                            bool isCalibrating = s_calib.active && s_calib.deviceIndex == d && s_calib.usageId == usageId;
                            if (isCalibrating) {
                                // Update observed range
                                if (val < s_calib.observedMin) s_calib.observedMin = val;
                                if (val > s_calib.observedMax) s_calib.observedMax = val;

                                ImGui::SameLine();
                                if (ImGui::SmallButton("Done")) {
                                    if (s_calib.observedMax > s_calib.observedMin) {
                                        s_calibData[calibKey] = { s_calib.observedMin, s_calib.observedMax };
                                        char logBuf[128];
                                        sprintf_s(logBuf, "Calibrated dev=%d axis=0x%02X: [%ld, %ld]", d, usageId, s_calib.observedMin, s_calib.observedMax);
                                        WizLog(logBuf);
                                    }
                                    s_calib = {};
                                }
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                                    "Sweep axis... Min:%ld Max:%ld", s_calib.observedMin, s_calib.observedMax);
                            } else {
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Calibrate")) {
                                    s_calib = {};
                                    s_calib.active = true;
                                    s_calib.deviceIndex = d;
                                    s_calib.usageId = usageId;
                                    s_calib.observedMin = val;
                                    s_calib.observedMax = val;
                                }
                                if (calibIt != s_calibData.end()) {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Clear")) {
                                        s_calibData.erase(calibIt);
                                        WizLog("Cleared calibration for dev=" + std::to_string(d) + " axis=0x" + std::to_string(usageId));
                                    }
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[%ld-%ld]",
                                        calibIt->second.first, calibIt->second.second);
                                }
                            }

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

        // ==== TAB 6: Custom Bindings ====
        if (ImGui::BeginTabItem("Custom Bindings")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Custom Button Bindings");
            ImGui::TextWrapped("Bind controller buttons to emit keyboard/mouse outputs. Use Starfield's vanilla binding menu to assign matching secondary bindings.");
            ImGui::Separator();
            ImGui::Spacing();

            // Add / Menu Cluster buttons
            if (ImGui::Button("Add Binding")) {
                s_customBindings.push_back({"(unbound)", "none"});
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Menu Cluster")) {
                s_customBindings.push_back({"(unbound)", "key:0x11"}); // W
                s_customBindings.push_back({"(unbound)", "key:0x1E"}); // A
                s_customBindings.push_back({"(unbound)", "key:0x1F"}); // S
                s_customBindings.push_back({"(unbound)", "key:0x20"}); // D
                s_customBindings.push_back({"(unbound)", "key:0x0F"}); // Tab
                s_customBindings.push_back({"(unbound)", "key:0x12"}); // E
                s_customBindings.push_back({"(unbound)", "key:0x01"}); // Esc
                WizLog("Added menu cluster preset (WASD/Tab/E/Esc).");
            }

            ImGui::Spacing();

            int removeIdx = -1;
            for (int i = 0; i < (int)s_customBindings.size(); i++) {
                auto& row = s_customBindings[i];
                ImGui::PushID(5000 + i);

                // Button binding display
                ImGui::Text("%-22s", row.buttonBinding.c_str());
                ImGui::SameLine(180);

                // Output dropdown
                int currentOutput = FindOutputIndex(row.output);
                const char* previewLabel = (currentOutput >= 0) ? kOutputCatalog[currentOutput].label : row.output.c_str();
                ImGui::PushItemWidth(120);
                if (ImGui::BeginCombo("##output", previewLabel)) {
                    for (int j = 0; j < kOutputCatalogSize; j++) {
                        bool selected = (j == currentOutput);
                        if (ImGui::Selectable(kOutputCatalog[j].label, selected)) {
                            row.output = kOutputCatalog[j].value;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                // Bind button
                ImGui::SameLine();
                if (ImGui::SmallButton("Bind")) {
                    char label[64];
                    int outputIdx = FindOutputIndex(row.output);
                    sprintf_s(label, "Custom #%d (%s)", i + 1,
                        outputIdx >= 0 ? kOutputCatalog[outputIdx].label : "?");
                    StartButtonCapture(i, 400, label);
                }

                // Clear button
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    row.buttonBinding = "(unbound)";
                }

                // Remove button
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    removeIdx = i;
                }

                ImGui::PopID();
            }

            if (removeIdx >= 0) {
                s_customBindings.erase(s_customBindings.begin() + removeIdx);
            }

            if (s_customBindings.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No custom bindings. Click 'Add Binding' or 'Add Menu Cluster' to get started.");
            }

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
            ResetDebounce();
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

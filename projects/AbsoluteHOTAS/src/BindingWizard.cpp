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
// POV direction helper (shared with capture logic)
// ============================================================================
static bool IsPovDirectionActive(DWORD pov, int direction) {
    if (LOWORD(pov) == 0xFFFF) return false; // centered
    static constexpr DWORD kDirAngles[4] = { 0, 9000, 18000, 27000 };
    DWORD target = kDirAngles[direction];
    DWORD diff = (pov > target) ? (pov - target) : (target - pov);
    if (diff > 18000) diff = 36000 - diff;
    return diff <= 4500;
}

static const char* PovDirectionName(int direction) {
    static const char* names[4] = { "Up", "Right", "Down", "Left" };
    return (direction >= 0 && direction < 4) ? names[direction] : "?";
}

// ============================================================================
// Binding Capture State
// ============================================================================
struct PendingBind {
    bool active = false;
    std::string targetLabel;
    int targetConfigSlot = -1;
    // Snapshot of all axes, buttons, and POV switches at the moment capture started
    struct DeviceSnapshot {
        int deviceIndex;
        long axes[8];
        BYTE buttons[128];
        DWORD povs[4];
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
    const char* saturationKey;    // May be nullptr
    const char* deadzoneKey;      // May be nullptr
};

static const AxisSlot kAxisSlots[] = {
    {"Throttle",         "iThrottleAxis",    "bInvertThrottle",    "fThrottleSensitivity","fThrottleSaturation",  "fThrottleDeadzone"},
    {"Pitch",            "iPitchAxis",       "bInvertPitch",       "fPitchSensitivity",   "fPitchSaturation",    "fPitchDeadzone"},
    {"Yaw",              "iYawAxis",         "bInvertYaw",         "fYawSensitivity",     "fYawSaturation",      "fYawDeadzone"},
    {"Roll",             "iRollAxis",        "bInvertRoll",        "fRollSensitivity",    "fRollSaturation",     "fRollDeadzone"},
    {"Strafe Lateral",   "iStrafeLatAxis",   "bInvertStrafeLat",   "fStrafeSensitivity",  "fStrafeSaturation",   "fStrafeDeadzone"},
    {"Strafe Vertical",  "iStrafeVertAxis",  "bInvertStrafeVert",  nullptr,               "fStrafeVertSaturation","fStrafeVertDeadzone"},
    {"Reverse",          "iReverseAxis",     "bInvertReverse",     "fReverseSensitivity", "fReverseSaturation",  nullptr},
};
static constexpr int kNumAxisSlots = sizeof(kAxisSlots) / sizeof(kAxisSlots[0]);

// Control button slots
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

static std::string s_axisBindings[kNumAxisSlots];
static bool        s_axisInvert[kNumAxisSlots];
static float       s_axisSensitivity[kNumAxisSlots];  // 0 = n/a
static float       s_axisSaturation[kNumAxisSlots];   // 1.0 = full range
static float       s_axisDeadzone[kNumAxisSlots];     // per-axis deadzone (0.0 = none)
static std::string s_buttonBindings[kNumButtonSlots];
static std::string s_digitalAxisBindings[kNumDigitalAxisSlots];
static float       s_digitalRollValue = 1.0f;
static float       s_digitalStrafeValue = 1.0f;
static bool        s_bindingsLoaded = false;

// DualStick accumulator mode state (maps to [DualStick] INI section)
static bool        s_accumulatorThrottle = false;
static float       s_accumulatorRate = 1.0f;
static float       s_accumulatorDecay = 0.0f;
static float       s_reverseGateVelocity = 5.0f;
static bool        s_symmetricalThrottleDz = true;
static bool        s_holdForBoost = true;

// Aim axis slots (saved to [Aim] section, capture range 600-699)
struct AimAxisSlot {
    const char* label;
    const char* iniKey;
    const char* invertIniKey;
    const char* sensitivityKey;
};
static const AimAxisSlot kAimAxisSlots[] = {
    {"Aim Yaw",   "iAimYawAxis",   "bInvertAimYaw",   "fAimYawSensitivity"},
    {"Aim Pitch", "iAimPitchAxis", "bInvertAimPitch",  "fAimPitchSensitivity"},
};
static constexpr int kNumAimAxisSlots = 2;
static std::string s_aimAxisBindings[2];
static bool        s_aimAxisInvert[2];
static float       s_aimAxisSensitivity[2];
static float       s_aimSensitivity = 1.0f;
static float       s_aimSmoothing = 0.0f;
static bool        s_mirrorFlightToAim = true;
static bool        s_sourceObjectAim = true;

// Digital aim button slots (capture range 700-704, toggle = 705)
struct DigitalAimSlot {
    const char* label;
    const char* iniKey;
};
static const DigitalAimSlot kDigitalAimSlots[] = {
    {"Aim Left",   "iDigitalAimLeftButton"},
    {"Aim Right",  "iDigitalAimRightButton"},
    {"Aim Up",     "iDigitalAimUpButton"},
    {"Aim Down",   "iDigitalAimDownButton"},
    {"Aim Center", "iDigitalAimCenterButton"},
};
static constexpr int kNumDigitalAimSlots = 5;
static std::string s_digitalAimBindings[5];
static float       s_digitalAimValue = 1.0f;
static std::string s_toggleAimModeBinding;

// HOSAM (Hands On Stick And Mouse) mode state (maps to [Aim] INI section)
static bool        s_hosamMode = false;
static bool        s_alignmentAssist = false;
static float       s_alignmentRadius = 15.0f;
static int         s_alignmentIdleMs = 80;
static float       s_alignmentDecayRate = 4.0f;

// Throttle calibration state (maps to [Normalization] INI section)
static float       s_idlePlateau = 0.05f;
static long        s_detentCenter = 32768;
static long        s_detentDeadzone = 500;
static bool        s_calibratingCenter = false;

// ============================================================================
// Full-device calibration state — tracks all 8 axes simultaneously
// ============================================================================
struct DeviceCalibState {
    bool  active = false;
    int   deviceIndex = -1;
    long  observedMin[8];
    long  observedMax[8];
    void Reset() {
        active = false;
        deviceIndex = -1;
        for (int i = 0; i < 8; i++) {
            observedMin[i] = LONG_MAX;
            observedMax[i] = LONG_MIN;
        }
    }
};
static DeviceCalibState s_devCalib;

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
    if (ref.HasIndex() && ref.deviceName.empty()) {
        sprintf_s(buf, "#%d@0x%02X", ref.deviceIndex, ref.value);
    } else if (ref.deviceName.empty()) {
        sprintf_s(buf, "0x%02X", ref.value);
    } else {
        sprintf_s(buf, "%s@0x%02X", ref.deviceName.c_str(), ref.value);
    }
    return buf;
}

static std::string FormatButtonRef(const BindingRef& ref) {
    if (!ref.IsValid() || ref.value <= 0) return "(unbound)";
    char buf[256];
    if (ref.HasIndex() && ref.deviceName.empty()) {
        sprintf_s(buf, "#%d@%d", ref.deviceIndex, ref.value);
    } else if (ref.deviceName.empty()) {
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

    s_axisSensitivity[0] = cfg.fThrottleSensitivity;
    s_axisSensitivity[1] = cfg.fPitchSensitivity;
    s_axisSensitivity[2] = cfg.fYawSensitivity;
    s_axisSensitivity[3] = cfg.fRollSensitivity;
    s_axisSensitivity[4] = cfg.fStrafeSensitivity;
    s_axisSensitivity[5] = 0.0f; // Strafe vert has no sensitivity
    s_axisSensitivity[6] = cfg.fReverseSensitivity;

    s_axisSaturation[0] = cfg.fThrottleSaturation;
    s_axisSaturation[1] = cfg.fPitchSaturation;
    s_axisSaturation[2] = cfg.fYawSaturation;
    s_axisSaturation[3] = cfg.fRollSaturation;
    s_axisSaturation[4] = cfg.fStrafeSaturation;
    s_axisSaturation[5] = cfg.fStrafeVertSaturation;
    s_axisSaturation[6] = cfg.fReverseSaturation;

    s_axisDeadzone[0] = cfg.fThrottleDeadzone;
    s_axisDeadzone[1] = cfg.fPitchDeadzone;
    s_axisDeadzone[2] = cfg.fYawDeadzone;
    s_axisDeadzone[3] = cfg.fRollDeadzone;
    s_axisDeadzone[4] = cfg.fStrafeDeadzone;
    s_axisDeadzone[5] = cfg.fStrafeVertDeadzone;
    s_axisDeadzone[6] = 0.0f; // Reverse has no deadzone

    // DualStick accumulator mode
    s_accumulatorThrottle = cfg.bAccumulatorThrottle;
    s_accumulatorRate = cfg.fAccumulatorRate;
    s_accumulatorDecay = cfg.fAccumulatorDecay;
    s_reverseGateVelocity = cfg.fReverseGateVelocity;
    // Symmetry detection: if idle plateau ≈ (1 - saturation), start in symmetrical mode
    s_symmetricalThrottleDz = (std::abs(cfg.idlePlateau - (1.0f - cfg.fThrottleSaturation)) < 0.01f);
    s_holdForBoost = cfg.bHoldForBoost;

    // HOSAM mode
    s_hosamMode = cfg.bHOSAMMode;
    s_alignmentAssist = cfg.bAlignmentAssist;
    s_alignmentRadius = cfg.fAlignmentRadius;
    s_alignmentIdleMs = cfg.iAlignmentIdleMs;
    s_alignmentDecayRate = cfg.fAlignmentDecayRate;

    // Throttle calibration
    s_idlePlateau = cfg.idlePlateau;
    s_detentCenter = cfg.detentCenter;
    s_detentDeadzone = cfg.detentDeadzone;

    s_buttonBindings[0] = FormatButtonRef(cfg.activateButton);
    s_buttonBindings[1] = FormatButtonRef(cfg.stopButton);
    s_buttonBindings[2] = FormatButtonRef(cfg.toggleWizardButton);

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

    // Aim settings
    s_aimAxisBindings[0]    = FormatRef(cfg.aimYawAxis);
    s_aimAxisBindings[1]    = FormatRef(cfg.aimPitchAxis);
    s_aimAxisInvert[0]      = cfg.bInvertAimYaw;
    s_aimAxisInvert[1]      = cfg.bInvertAimPitch;
    s_aimAxisSensitivity[0] = cfg.fAimYawSensitivity;
    s_aimAxisSensitivity[1] = cfg.fAimPitchSensitivity;
    s_aimSensitivity        = cfg.fAimSensitivity;
    s_aimSmoothing          = cfg.fAimSmoothing;
    s_mirrorFlightToAim     = cfg.bMirrorFlightToAim;
    s_sourceObjectAim       = cfg.bSourceObjectAim;

    // Digital aim buttons
    s_digitalAimBindings[0] = FormatButtonRef(cfg.digitalAimLeftButton);
    s_digitalAimBindings[1] = FormatButtonRef(cfg.digitalAimRightButton);
    s_digitalAimBindings[2] = FormatButtonRef(cfg.digitalAimUpButton);
    s_digitalAimBindings[3] = FormatButtonRef(cfg.digitalAimDownButton);
    s_digitalAimBindings[4] = FormatButtonRef(cfg.digitalAimCenterButton);
    s_digitalAimValue       = cfg.fDigitalAimValue;
    s_toggleAimModeBinding  = FormatButtonRef(cfg.toggleAimModeButton);
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
        memcpy(snap.povs, st->rgdwPOV, sizeof(snap.povs));
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

    // Helper: check if a device has a duplicate product name
    auto hasDupe = [](int devIdx) -> bool {
        const auto& name = DeviceManager::GetDevice(devIdx).productName;
        for (int d = 0; d < DeviceManager::GetDeviceCount(); d++) {
            if (d != devIdx && DeviceManager::GetDevice(d).productName == name)
                return true;
        }
        return false;
    };

    if ((slot >= 0 && slot < 100) || (slot >= 600 && slot < 700)) {
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
                        if (hasDupe(snap.deviceIndex)) {
                            sprintf_s(buf, "#%d@0x%02X", snap.deviceIndex, usageId);
                        } else {
                            sprintf_s(buf, "%s@0x%02X", info.productName.c_str(), usageId);
                        }

                        if (slot >= 600)
                            s_aimAxisBindings[slot - 600] = buf;
                        else
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

            // Helper lambda to commit a captured button binding string
            auto CommitButton = [&](const char* buf) {
                if (slot >= 100 && slot < 200) {
                    s_buttonBindings[slot - 100] = buf;
                } else if (slot >= 200 && slot < 300) {
                    int idx = slot - 200;
                    if (idx < (int)s_shipActionSlots.size()) {
                        s_shipActionSlots[idx].binding = buf;
                    }
                } else if (slot >= 300 && slot < 400) {
                    s_digitalAxisBindings[slot - 300] = buf;
                } else if (slot >= 700 && slot < 705) {
                    s_digitalAimBindings[slot - 700] = buf;
                } else if (slot == 705) {
                    s_toggleAimModeBinding = buf;
                } else if (slot >= 400 && slot < 600) {
                    int idx = slot - 400;
                    if (idx < (int)s_customBindings.size()) {
                        s_customBindings[idx].buttonBinding = buf;
                    }
                }
                WizLog("Button captured: " + std::string(buf) + " for " + s_pendingBind.targetLabel);
                s_pendingBind.active = false;
            };

            // --- Physical buttons (1-128) ---
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
                    if (hasDupe(snap.deviceIndex)) {
                        sprintf_s(buf, "#%d@%d", snap.deviceIndex, b + 1);
                    } else {
                        sprintf_s(buf, "%s@%d", info.productName.c_str(), b + 1);
                    }
                    CommitButton(buf);
                    return;
                }
            }

            // --- POV / HAT switches (virtual buttons 129-144) ---
            for (int p = 0; p < 4; p++) {
                for (int dir = 0; dir < 4; dir++) {
                    int virtualBtn = 129 + p * 4 + dir; // 129-144
                    bool nowActive = IsPovDirectionActive(st->rgdwPOV[p], dir);
                    bool wasActive = IsPovDirectionActive(snap.povs[p], dir);

                    if (!nowActive || wasActive) {
                        if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                            s_pendingBind.debounceButtonIndex == virtualBtn && !nowActive) {
                            s_pendingBind.debounceButtonFrames = 0;
                        }
                        continue;
                    }

                    if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceButtonIndex == virtualBtn) {
                        s_pendingBind.debounceButtonFrames++;
                    } else {
                        s_pendingBind.debounceDeviceIndex = snap.deviceIndex;
                        s_pendingBind.debounceButtonIndex = virtualBtn;
                        s_pendingBind.debounceButtonFrames = 1;
                    }

                    if (s_pendingBind.debounceButtonFrames >= kButtonDebounceFrames) {
                        const auto& info = DeviceManager::GetDevice(snap.deviceIndex);
                        char buf[256];
                        if (hasDupe(snap.deviceIndex)) {
                            sprintf_s(buf, "#%d@%d", snap.deviceIndex, virtualBtn);
                        } else {
                            sprintf_s(buf, "%s@%d", info.productName.c_str(), virtualBtn);
                        }
                        CommitButton(buf);
                        return;
                    }
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
        if (kAxisSlots[i].saturationKey) {
            char satStr[32];
            sprintf_s(satStr, "%.2f", s_axisSaturation[i]);
            ini.SetValue("Hardware", kAxisSlots[i].saturationKey, satStr);
        }
        if (kAxisSlots[i].deadzoneKey) {
            char dzStr[32];
            sprintf_s(dzStr, "%.2f", s_axisDeadzone[i]);
            ini.SetValue("Hardware", kAxisSlots[i].deadzoneKey, dzStr);
        }
    }

    // Throttle calibration ([Normalization] section)
    {
        char plateauStr[32];
        sprintf_s(plateauStr, "%.2f", s_idlePlateau);
        ini.SetValue("Normalization", "fIdlePlateau", plateauStr);

        char centerStr[32];
        sprintf_s(centerStr, "%ld", s_detentCenter);
        ini.SetValue("Normalization", "iDetentCenter", centerStr);

        char dzStr[32];
        sprintf_s(dzStr, "%ld", s_detentDeadzone);
        ini.SetValue("Normalization", "iDetentDeadzone", dzStr);
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

    // Aim settings ([Aim] section)
    ini.SetBoolValue("Aim", "bSourceObjectAim", s_sourceObjectAim);
    {
        char aimSensStr[32];
        sprintf_s(aimSensStr, "%.2f", s_aimSensitivity);
        ini.SetValue("Aim", "fAimSensitivity", aimSensStr);
    }
    ini.SetBoolValue("Aim", "bMirrorFlightToAim", s_mirrorFlightToAim);
    for (int i = 0; i < kNumAimAxisSlots; i++) {
        if (s_aimAxisBindings[i] != "(unbound)") {
            ini.SetValue("Aim", kAimAxisSlots[i].iniKey, s_aimAxisBindings[i].c_str());
        } else {
            ini.SetValue("Aim", kAimAxisSlots[i].iniKey, "");
        }
        ini.SetBoolValue("Aim", kAimAxisSlots[i].invertIniKey, s_aimAxisInvert[i]);
        char sensStr[32];
        sprintf_s(sensStr, "%.2f", s_aimAxisSensitivity[i]);
        ini.SetValue("Aim", kAimAxisSlots[i].sensitivityKey, sensStr);
    }
    {
        char smoothStr[32];
        sprintf_s(smoothStr, "%.2f", s_aimSmoothing);
        ini.SetValue("Aim", "fAimSmoothing", smoothStr);
    }


    // Digital aim buttons ([Aim] section)
    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        if (s_digitalAimBindings[i] != "(unbound)") {
            ini.SetValue("Aim", kDigitalAimSlots[i].iniKey, s_digitalAimBindings[i].c_str());
        } else {
            ini.SetValue("Aim", kDigitalAimSlots[i].iniKey, "-1");
        }
    }
    {
        char dAimStr[32];
        sprintf_s(dAimStr, "%.2f", s_digitalAimValue);
        ini.SetValue("Aim", "fDigitalAimValue", dAimStr);
    }
    if (s_toggleAimModeBinding != "(unbound)") {
        ini.SetValue("Aim", "iToggleAimModeButton", s_toggleAimModeBinding.c_str());
    } else {
        ini.SetValue("Aim", "iToggleAimModeButton", "-1");
    }

    // DualStick accumulator mode ([DualStick] section)
    ini.SetBoolValue("DualStick", "bAccumulatorThrottle", s_accumulatorThrottle);
    {
        char rateStr[32], decayStr[32], gateStr[32];
        sprintf_s(rateStr, "%.1f", s_accumulatorRate);
        sprintf_s(decayStr, "%.1f", s_accumulatorDecay);
        sprintf_s(gateStr, "%.1f", s_reverseGateVelocity);
        ini.SetValue("DualStick", "fAccumulatorRate", rateStr);
        ini.SetValue("DualStick", "fAccumulatorDecay", decayStr);
        ini.SetValue("DualStick", "fReverseGateVelocity", gateStr);
    }
    ini.SetBoolValue("Injection", "bHoldForBoost", s_holdForBoost);

    // HOSAM mode ([Aim] section — appended to existing aim writes)
    ini.SetBoolValue("Aim", "bHOSAMMode", s_hosamMode);
    ini.SetBoolValue("Aim", "bAlignmentAssist", s_alignmentAssist);
    {
        char radStr[32], decayStr2[32];
        sprintf_s(radStr, "%.1f", s_alignmentRadius);
        sprintf_s(decayStr2, "%.1f", s_alignmentDecayRate);
        ini.SetValue("Aim", "fAlignmentRadius", radStr);
        char idleStr[32];
        sprintf_s(idleStr, "%d", s_alignmentIdleMs);
        ini.SetValue("Aim", "iAlignmentIdleMs", idleStr);
        ini.SetValue("Aim", "fAlignmentDecayRate", decayStr2);
    }

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
// Helper: annotate POV virtual button IDs (129-144) with human-readable labels
// ============================================================================
static std::string FormatBindingDisplay(const std::string& binding) {
    if (binding == "(unbound)") return binding;
    // Find the button number after the last '@', or the whole string if no '@'
    auto atPos = binding.rfind('@');
    std::string numPart = (atPos != std::string::npos) ? binding.substr(atPos + 1) : binding;
    char* endPtr = nullptr;
    long btnId = std::strtol(numPart.c_str(), &endPtr, 10);
    if (endPtr != numPart.c_str() && *endPtr == '\0' && btnId >= 129 && btnId <= 144) {
        int povIndex = (int)(btnId - 129) / 4;
        int direction = (int)(btnId - 129) % 4;
        char label[256];
        sprintf_s(label, "%s (POV%d-%s)", binding.c_str(), povIndex + 1, PovDirectionName(direction));
        return label;
    }
    return binding;
}

// ============================================================================
// Helper: draw a binding row with Bind/Clear buttons
// ============================================================================
static void DrawBindingRow(const char* label, std::string& binding, int captureSlot, bool isAxis) {
    if (label[0] != '\0') {
        ImGui::Text("%-22s", label);
        ImGui::SameLine(180);
    }

    std::string displayStr = FormatBindingDisplay(binding);
    ImVec4 color = (binding == "(unbound)")
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        : ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
    ImGui::TextColored(color, "%s", displayStr.c_str());
    ImGui::SameLine(500);

    bool isCapturing = s_pendingBind.active && s_pendingBind.targetConfigSlot == captureSlot;
    if (isCapturing) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), isAxis ? ">> Move axis..." : ">> Press button...");
        ImGui::SameLine();
        ImGui::PushID(captureSlot + 90000);
        if (ImGui::SmallButton("Cancel")) {
            s_pendingBind.active = false;
        }
        ImGui::PopID();
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

        // ==== TAB 1: Device Summary ====
        if (ImGui::BeginTabItem("Devices")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Connected HID Devices");
            ImGui::Separator();

            int devCount = DeviceManager::GetDeviceCount();
            if (devCount == 0) {
                ImGui::TextWrapped("No DirectInput devices detected.");
            }
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Device indices (#N) follow USB enumeration order.");
            ImGui::Spacing();

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
                        sprintf_s(swapLabel, "Swap #%d <-> #%d", d, d + 1);
                        if (ImGui::SmallButton(swapLabel)) {
                            char prefA[16], prefB[16], tempPref[16];
                            sprintf_s(prefA, "#%d@", d);
                            sprintf_s(prefB, "#%d@", d + 1);
                            sprintf_s(tempPref, "#__SWAP__@");
                            size_t lenA = strlen(prefA);
                            size_t lenB = strlen(prefB);
                            size_t tempLen = strlen(tempPref);

                            auto doSwap = [&](std::string& binding) {
                                if (binding.substr(0, lenA) == prefA) {
                                    binding = std::string(tempPref) + binding.substr(lenA);
                                } else if (binding.substr(0, lenB) == prefB) {
                                    binding = std::string(prefA) + binding.substr(lenB);
                                }
                            };
                            auto finalize = [&](std::string& binding) {
                                if (binding.substr(0, tempLen) == tempPref) {
                                    binding = std::string(prefB) + binding.substr(tempLen);
                                }
                            };

                            std::vector<std::string*> allBindings;
                            for (auto& b : s_axisBindings) allBindings.push_back(&b);
                            for (auto& b : s_buttonBindings) allBindings.push_back(&b);
                            for (auto& b : s_digitalAxisBindings) allBindings.push_back(&b);
                            for (auto& sa : s_shipActionSlots) allBindings.push_back(&sa.binding);
                            for (auto& cb : s_customBindings) allBindings.push_back(&cb.buttonBinding);
                            for (auto& b : s_aimAxisBindings) allBindings.push_back(&b);
                            for (auto& b : s_digitalAimBindings) allBindings.push_back(&b);

                            for (auto* bp : allBindings) doSwap(*bp);
                            for (auto* bp : allBindings) finalize(*bp);

                            SaveBindingsToINI();
                            WizLog("Swapped device indices #" + std::to_string(d) + " <-> #" + std::to_string(d + 1));
                        }
                    }

                    // Full-device calibration
                    bool isCalibThisDevice = s_devCalib.active && s_devCalib.deviceIndex == d;

                    if (isCalibThisDevice) {
                        // Poll this device and update all 8 axes min/max
                        const auto* st = DeviceManager::GetCachedState(d);
                        if (st) {
                            for (int a = 0; a < 8; a++) {
                                long val = GetAxisFromState(st, 0x30 + a);
                                if (val < s_devCalib.observedMin[a]) s_devCalib.observedMin[a] = val;
                                if (val > s_devCalib.observedMax[a]) s_devCalib.observedMax[a] = val;
                            }
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                            "Move ALL axes to their full extremes, then click Done.");

                        // Show which axes have moved (live feedback)
                        constexpr long kGhostThreshold = 5000;
                        int activeCount = 0;
                        for (int a = 0; a < 8; a++) {
                            long range = s_devCalib.observedMax[a] - s_devCalib.observedMin[a];
                            if (range > kGhostThreshold) activeCount++;
                        }
                        ImGui::Text("Detected %d active axes", activeCount);

                        if (ImGui::Button("Done##devCalib")) {
                            int saved = 0;
                            for (int a = 0; a < 8; a++) {
                                long range = s_devCalib.observedMax[a] - s_devCalib.observedMin[a];
                                if (range > kGhostThreshold) {
                                    int calibKey = (d << 8) | (0x30 + a);
                                    s_calibData[calibKey] = { s_devCalib.observedMin[a], s_devCalib.observedMax[a] };
                                    saved++;
                                }
                            }
                            char logBuf[128];
                            sprintf_s(logBuf, "Calibrated device #%d: %d axes saved", d, saved);
                            WizLog(logBuf);
                            s_devCalib.Reset();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel##devCalib")) {
                            s_devCalib.Reset();
                        }
                    } else {
                        // Show existing calibration data (text only, no polling)
                        bool hasCalib = false;
                        for (int a = 0; a < 8; a++) {
                            int calibKey = (d << 8) | (0x30 + a);
                            if (s_calibData.count(calibKey)) { hasCalib = true; break; }
                        }

                        if (hasCalib) {
                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Calibration:");
                            for (int a = 0; a < 8; a++) {
                                int calibKey = (d << 8) | (0x30 + a);
                                auto calibIt = s_calibData.find(calibKey);
                                if (calibIt != s_calibData.end()) {
                                    ImGui::Text("  0x%02X (%s): [%ld - %ld]", 0x30 + a, AxisName(0x30 + a),
                                        calibIt->second.first, calibIt->second.second);
                                }
                            }
                            ImGui::PushID(d * 1000 + 998);
                            if (ImGui::SmallButton("Clear All##calib")) {
                                for (int a = 0; a < 8; a++) {
                                    s_calibData.erase((d << 8) | (0x30 + a));
                                }
                                WizLog("Cleared all calibration for device #" + std::to_string(d));
                            }
                            ImGui::PopID();
                        }

                        // Start calibration button
                        if (!s_devCalib.active) {
                            ImGui::Spacing();
                            ImGui::PushID(d * 1000 + 999);
                            if (ImGui::SmallButton("Calibrate Device")) {
                                s_devCalib.Reset();
                                s_devCalib.active = true;
                                s_devCalib.deviceIndex = d;
                                // Seed with current values so ghost axes stay near zero range
                                const auto* st = DeviceManager::GetCachedState(d);
                                if (st) {
                                    for (int a = 0; a < 8; a++) {
                                        long val = GetAxisFromState(st, 0x30 + a);
                                        s_devCalib.observedMin[a] = val;
                                        s_devCalib.observedMax[a] = val;
                                    }
                                }
                            }
                            ImGui::PopID();
                        }
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
            ImGui::SameLine(500);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), ">> Save & Apply to commit changes");
            ImGui::Separator();
            ImGui::Spacing();

            for (int i = 0; i < kNumAxisSlots; i++) {
                ImGui::PushID(i);

                // Axis group header: colored label + thin separator
                if (i > 0) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", kAxisSlots[i].label);
                ImGui::SameLine(180);

                // Binding + Bind/Clear (inline with axis label)
                DrawBindingRow("", s_axisBindings[i], i, true);

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

                // Saturation slider + visual actuation range graph
                if (kAxisSlots[i].saturationKey) {
                    ImGui::Indent(180);
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("Sat", &s_axisSaturation[i], 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    s_axisSaturation[i] = std::clamp(s_axisSaturation[i], 0.05f, 1.0f);
                    ImGui::PopItemWidth();

                    // Visual actuation range graph
                    ImGui::SameLine();
                    {
                        float barWidth = 200.0f;
                        float barHeight = 14.0f;
                        float sat = s_axisSaturation[i];
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        ImDrawList* dl = ImGui::GetWindowDrawList();

                        ImU32 colDead = IM_COL32(80, 30, 30, 200);
                        ImU32 colActive = IM_COL32(50, 200, 80, 220);

                        // Background: dead zone color (entire bar)
                        dl->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), colDead, 3.0f);

                        bool isThrottle = (i == 0);
                        if (isThrottle) {
                            // Throttle (unipolar): red on both ends
                            // Left dead = idle plateau, right dead = saturation cap
                            float idleEnd = s_idlePlateau * barWidth;
                            float satEnd = sat * barWidth;
                            if (satEnd > idleEnd) {
                                dl->AddRectFilled(
                                    ImVec2(pos.x + idleEnd, pos.y),
                                    ImVec2(pos.x + satEnd, pos.y + barHeight),
                                    colActive, 3.0f);
                            }

                            // Resolve throttle device (cached — only re-resolve when binding changes)
                            static std::string s_lastThrottleBinding;
                            static int s_cachedThrottleDevIdx = -1;
                            static int s_cachedThrottleUsage = -1;
                            if (s_axisBindings[0] != s_lastThrottleBinding) {
                                s_lastThrottleBinding = s_axisBindings[0];
                                BindingRef tRef = ParseBindingRef(s_axisBindings[0].c_str(), -1);
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

                            // Normalization helper: raw value → [0,1] using calibration or 0-65535
                            auto NormThrottleRaw = [&](long rawVal) -> float {
                                if (tDevIdx >= 0) {
                                    int calibKey = (tDevIdx << 8) | tUsage;
                                    auto calibIt = s_calibData.find(calibKey);
                                    if (calibIt != s_calibData.end()) {
                                        long cmin = calibIt->second.first;
                                        long cmax = calibIt->second.second;
                                        long crange = cmax - cmin;
                                        if (crange > 0) return std::clamp((float)(rawVal - cmin) / (float)crange, 0.0f, 1.0f);
                                    }
                                }
                                return std::clamp(rawVal / 65535.0f, 0.0f, 1.0f);
                            };

                            // Center marker (cyan line)
                            float centerNorm = NormThrottleRaw(s_detentCenter);
                            float centerX = centerNorm * barWidth;
                            dl->AddLine(
                                ImVec2(pos.x + centerX, pos.y),
                                ImVec2(pos.x + centerX, pos.y + barHeight),
                                IM_COL32(80, 220, 240, 220), 2.0f);

                            // Deadzone around center (orange shading)
                            float dzNorm = (float)s_detentDeadzone / 65535.0f;
                            if (tDevIdx >= 0) {
                                int calibKey = (tDevIdx << 8) | tUsage;
                                auto calibIt = s_calibData.find(calibKey);
                                if (calibIt != s_calibData.end()) {
                                    long crange = calibIt->second.second - calibIt->second.first;
                                    if (crange > 0) dzNorm = (float)s_detentDeadzone / (float)crange;
                                }
                            }
                            if (dzNorm > 0.001f) {
                                float dzLeft = std::max(0.0f, centerNorm - dzNorm) * barWidth;
                                float dzRight = std::min(1.0f, centerNorm + dzNorm) * barWidth;
                                dl->AddRectFilled(
                                    ImVec2(pos.x + dzLeft, pos.y),
                                    ImVec2(pos.x + dzRight, pos.y + barHeight),
                                    IM_COL32(200, 100, 30, 140), 0.0f);
                            }

                            // Live axis position (yellow marker)
                            if (tDevIdx >= 0) {
                                const auto* st = DeviceManager::GetCachedState(tDevIdx);
                                if (st) {
                                    long rawVal = GetAxisFromState(st, tUsage);
                                    float liveNorm = NormThrottleRaw(rawVal);
                                    float liveX = liveNorm * barWidth;
                                    dl->AddLine(
                                        ImVec2(pos.x + liveX, pos.y - 1),
                                        ImVec2(pos.x + liveX, pos.y + barHeight + 1),
                                        IM_COL32(255, 220, 50, 255), 2.0f);

                                    // "Set Center" capture: update detent to current position
                                    if (s_calibratingCenter) {
                                        s_detentCenter = rawVal;
                                    }
                                }
                            }
                        } else {
                            // Bipolar axes: red on both ends (saturation dead zones)
                            float cappedEdge = ((1.0f - sat) / 2.0f) * barWidth;
                            dl->AddRectFilled(
                                ImVec2(pos.x + cappedEdge, pos.y),
                                ImVec2(pos.x + barWidth - cappedEdge, pos.y + barHeight),
                                colActive, 3.0f);
                        }

                        // Border
                        dl->AddRect(pos, ImVec2(pos.x + barWidth, pos.y + barHeight), IM_COL32(120, 120, 120, 180), 3.0f);
                        ImGui::Dummy(ImVec2(barWidth, barHeight));

                        // Percentage label
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%.0f%%", sat * 100.0f);
                    }

                    ImGui::Unindent(180);

                    // Throttle-specific calibration controls
                    if (i == 0) {
                        ImGui::Indent(180);

                        // Idle plateau slider
                        ImGui::PushItemWidth(120);
                        ImGui::SliderFloat("Idle Zone", &s_idlePlateau, 0.0f, 0.20f, "%.2f");
                        s_idlePlateau = std::clamp(s_idlePlateau, 0.0f, 0.20f);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Bottom dead zone");

                        // Symmetrical throttle deadzone checkbox
                        ImGui::Checkbox("Symmetrical Deadzones", &s_symmetricalThrottleDz);
                        if (s_symmetricalThrottleDz) {
                            s_axisSaturation[0] = 1.0f - s_idlePlateau;
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "(Sat locked to %.0f%%)", s_axisSaturation[0] * 100.0f);
                        }

                        // Set Center button + live readout
                        if (s_calibratingCenter) {
                            if (ImGui::Button("Done##center")) {
                                s_calibratingCenter = false;
                                WizLog("Center set to: " + std::to_string(s_detentCenter));
                            }
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Move throttle to center... %ld", s_detentCenter);
                        } else {
                            if (ImGui::Button("Set Center")) {
                                s_calibratingCenter = true;
                            }
                            ImGui::SameLine();
                            ImGui::Text("Center: %ld", s_detentCenter);
                        }

                        // Deadzone slider (raw units, but show as percentage)
                        ImGui::PushItemWidth(120);
                        float dzPct = (float)s_detentDeadzone / 65535.0f * 100.0f;
                        if (ImGui::SliderFloat("Deadzone", &dzPct, 0.0f, 10.0f, "%.1f%%")) {
                            s_detentDeadzone = (long)(dzPct / 100.0f * 65535.0f);
                        }
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Around center (%ld raw)", s_detentDeadzone);

                        ImGui::Unindent(180);

                        // --- DualStick / Accumulator Mode Panel ---
                        ImGui::Spacing();
                        ImGui::Indent(20);
                        bool accOpen = ImGui::CollapsingHeader("Dual-Stick / Accumulator Mode", ImGuiTreeNodeFlags_None);
                        if (accOpen) {
                            ImGui::Indent(12);

                            ImGui::Checkbox("Enable Accumulator Throttle", &s_accumulatorThrottle);
                            if (s_accumulatorThrottle) {
                                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "(Rate)");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f),
                                    "Stick deflection controls throttle speed, not position.");

                                ImGui::Spacing();
                                ImGui::PushItemWidth(180);
                                ImGui::SliderFloat("Ramp Rate##accRate", &s_accumulatorRate, 0.1f, 5.0f, "%.1f units/s");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "At full deflection");

                                ImGui::SliderFloat("Decay Rate##accDecay", &s_accumulatorDecay, 0.0f, 3.0f, "%.1f units/s");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "At neutral (0=hold)");

                                ImGui::SliderFloat("Rev. Gate##accGate", &s_reverseGateVelocity, 0.0f, 50.0f, "%.0f m/s");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Reverse below this speed");
                                ImGui::PopItemWidth();

                                ImGui::Spacing();
                                ImGui::TextWrapped(
                                    "Push forward to accelerate. Pull back to decelerate; "
                                    "at zero throttle, pulling further triggers reverse braking.");
                            }

                            ImGui::Unindent(12);
                        }
                        ImGui::Unindent(20);

                        // --- HOSAM (Hands On Stick And Mouse) Mode Panel ---
                        ImGui::Spacing();
                        ImGui::Indent(20);
                        bool hosamOpen = ImGui::CollapsingHeader("HOSAM Mode (Stick + Mouse)", ImGuiTreeNodeFlags_None);
                        if (hosamOpen) {
                            ImGui::Indent(12);

                            ImGui::Checkbox("Enable HOSAM Mode", &s_hosamMode);
                            if (s_hosamMode) {
                                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "(Active)");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f),
                                    "Mouse drives steering. Pitch/Yaw axes released to native mouse.");

                                ImGui::Spacing();
                                ImGui::Checkbox("Alignment Assist", &s_alignmentAssist);
                                if (s_alignmentAssist) {
                                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f),
                                        "Gently centers steering when mouse is idle near center.");

                                    ImGui::PushItemWidth(180);
                                    ImGui::SliderFloat("Radius##alignRad", &s_alignmentRadius, 1.0f, 100.0f, "%.0f units");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "of 200 max");

                                    int idleMs = s_alignmentIdleMs;
                                    if (ImGui::SliderInt("Idle Time##alignIdle", &idleMs, 10, 500, "%d ms")) {
                                        s_alignmentIdleMs = idleMs;
                                    }
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Before decay starts");

                                    ImGui::SliderFloat("Decay Speed##alignDecay", &s_alignmentDecayRate, 0.5f, 20.0f, "%.1f");
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Higher = faster snap");
                                    ImGui::PopItemWidth();
                                }
                            } else {
                                ImGui::TextWrapped(
                                    "Use a joystick for throttle/strafe and your mouse for steering. "
                                    "Pitch and Yaw are released to the game's native mouse pipeline.");
                            }

                            ImGui::Unindent(12);
                        }
                        ImGui::Unindent(20);
                    }
                }

                // Per-axis deadzone slider (below saturation)
                if (kAxisSlots[i].deadzoneKey) {
                    ImGui::Indent(180);
                    ImGui::PushItemWidth(120);
                    char dzLabel[32];
                    sprintf_s(dzLabel, "Deadzone##axdz%d", i);
                    float dzPct = s_axisDeadzone[i] * 100.0f;
                    if (ImGui::SliderFloat(dzLabel, &dzPct, 0.0f, 50.0f, "%.0f%%")) {
                        s_axisDeadzone[i] = std::clamp(dzPct / 100.0f, 0.0f, 0.50f);
                    }
                    ImGui::PopItemWidth();
                    if (s_axisDeadzone[i] > 0.001f) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Center dead zone");
                    }
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
                // Show "Hold for Boost" checkbox next to Fire Boosters (index 0)
                if (i == 0) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Hold for Boost", &s_holdForBoost);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Pause throttle injection while boost is held.\n"
                                          "On release: set throttle to max and cancel boost.");
                    }
                }
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

        // ==== TAB 7: Aiming ====
        if (ImGui::BeginTabItem("Aiming")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aiming System");
            ImGui::TextWrapped(
                "Controls how the aiming reticle and ship steering interact. "
                "Enable the aim system, then choose a mode below.");
            ImGui::Separator();
            ImGui::Spacing();

            // Master enable
            ImGui::Checkbox("Enable Aim System", &s_sourceObjectAim);
            if (!s_sourceObjectAim) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "Aim system disabled. Ship steering uses cluster gates only (legacy mode).");
            }

            if (s_sourceObjectAim) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Mode determination: if aim axes are bound → independent, else → aim-driven steering
                bool hasAimAxes = (s_aimAxisBindings[0] != "(unbound)") || (s_aimAxisBindings[1] != "(unbound)");

                // Mode header
                if (hasAimAxes) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Mode: Independent Aim & Steer");
                    ImGui::TextWrapped(
                        "The flight stick controls ship rotation directly. "
                        "The bound aim axes below independently drive the weapon reticle. "
                        "Clear aim axes to switch to Aim-Driven Steering.");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Mode: Aim-Driven Steering");
                    ImGui::TextWrapped(
                        "The flight stick drives both the aiming reticle and ship steering "
                        "through the mouse accumulator pathway. Bind aim axes below to "
                        "switch to Independent Aim & Steer.");

                    // Mirror sensitivity (only relevant in aim-driven steering mode)
                    ImGui::Spacing();
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("Steering Sensitivity", &s_aimSensitivity, 0.1f, 3.0f, "%.2f");
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Scales flight stick input to reticle/steering");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Aim axis bindings
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aim Axis Bindings");
                ImGui::TextWrapped(
                    "Bind a second analog input (e.g., throttle thumbstick) to independently "
                    "drive the aiming reticle.");
                ImGui::Spacing();

                for (int i = 0; i < kNumAimAxisSlots; i++) {
                    ImGui::PushID(6000 + i);

                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", kAimAxisSlots[i].label);
                    ImGui::SameLine(180);

                    // Binding row (uses slot 600+i)
                    DrawBindingRow("", s_aimAxisBindings[i], 600 + i, true);

                    // Inversion checkbox
                    ImGui::SameLine(640);
                    ImGui::Checkbox("Inv", &s_aimAxisInvert[i]);

                    // Sensitivity slider
                    ImGui::Indent(180);
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("Sens", &s_aimAxisSensitivity[i], 0.1f, 3.0f, "%.2f");
                    ImGui::PopItemWidth();
                    ImGui::Unindent(180);

                    if (i < kNumAimAxisSlots - 1) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    ImGui::PopID();
                }

                // Aim smoothing slider (applies to all analog aim axes)
                ImGui::Spacing();
                ImGui::Indent(180);
                ImGui::PushItemWidth(120);
                ImGui::SliderFloat("Smoothing", &s_aimSmoothing, 0.0f, 0.98f, "%.2f");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Low-res sensor filter (0=off)");

                ImGui::Unindent(180);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Digital aim override (5-way)
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Digital Aim Override (5-Way)");
                ImGui::TextWrapped(
                    "Bind buttons to move the aiming reticle like a virtual cursor. "
                    "Hold a direction to accumulate position. Release to hold. Center resets to (0,0).");
                ImGui::Spacing();

                for (int i = 0; i < kNumDigitalAimSlots; i++) {
                    ImGui::PushID(7000 + i);
                    DrawBindingRow(kDigitalAimSlots[i].label, s_digitalAimBindings[i], 700 + i, false);
                    ImGui::PopID();
                }

                ImGui::Spacing();
                ImGui::PushItemWidth(120);
                ImGui::SliderFloat("Aim Speed", &s_digitalAimValue, 0.1f, 3.0f, "%.2f");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Travel speed per second");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Toggle aim mode button
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Aim Mode Toggle");
                ImGui::TextWrapped(
                    "Bind a button to toggle between Aim-Driven Steering and "
                    "Independent Aim at runtime. Only useful when aim axes are bound.");
                ImGui::Spacing();
                ImGui::PushID(7005);
                DrawBindingRow("Toggle Mode", s_toggleAimModeBinding, 705, false);
                ImGui::PopID();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // (Inline cancel buttons are now on each binding row)

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

#pragma once

// Slot definition structs and capture category encoding for the BindingWizard.
// Extracted from BindingWizard.cpp to share across WizardCapture and WizardConfig.

#include <string>
#include <vector>

// --- Capture slot encoding ---
// Each binding category gets a range of integer slot IDs so the capture system
// can route a completed capture back to the correct binding array.
namespace CaptureSlot {
    constexpr int kAxisBase        = 0;     // 0..99
    constexpr int kButtonBase      = 100;   // 100..199
    constexpr int kShipActionBase  = 200;   // 200..299
    constexpr int kDigitalAxisBase = 300;   // 300..399
    constexpr int kCustomBase      = 400;   // 400..599
    constexpr int kAimAxisBase     = 600;   // 600..699
    constexpr int kDigitalAimBase  = 700;   // 700..704
    constexpr int kToggleAimMode   = 705;

    inline bool IsAxis(int slot) {
        return (slot >= kAxisBase && slot < kButtonBase)
            || (slot >= kAimAxisBase && slot < kDigitalAimBase);
    }
    inline bool IsButton(int slot) { return !IsAxis(slot); }
}

// --- Axis slot descriptor ---
struct AxisSlot {
    const char* label;
    const char* iniKey;
    const char* invertIniKey;     // May be nullptr
    const char* sensitivityKey;   // May be nullptr
    const char* saturationKey;    // May be nullptr
    const char* deadzoneKey;      // May be nullptr
};

inline const AxisSlot kAxisSlots[] = {
    {"Throttle",         "iThrottleAxis",    "bInvertThrottle",    "fThrottleSensitivity","fThrottleSaturation",  "fThrottleDeadzone"},
    {"Pitch",            "iPitchAxis",       "bInvertPitch",       "fPitchSensitivity",   "fPitchSaturation",    "fPitchDeadzone"},
    {"Yaw",              "iYawAxis",         "bInvertYaw",         "fYawSensitivity",     "fYawSaturation",      "fYawDeadzone"},
    {"Roll",             "iRollAxis",        "bInvertRoll",        "fRollSensitivity",    "fRollSaturation",     "fRollDeadzone"},
    {"Strafe Lateral",   "iStrafeLatAxis",   "bInvertStrafeLat",   "fStrafeSensitivity",  "fStrafeSaturation",   "fStrafeDeadzone"},
    {"Strafe Vertical",  "iStrafeVertAxis",  "bInvertStrafeVert",  nullptr,               "fStrafeVertSaturation","fStrafeVertDeadzone"},
    {"Reverse",          "iReverseAxis",     "bInvertReverse",     "fReverseSensitivity", "fReverseSaturation",  nullptr},
};
inline constexpr int kNumAxisSlots = sizeof(kAxisSlots) / sizeof(kAxisSlots[0]);

// --- Button slot descriptor ---
struct ButtonSlot {
    const char* label;
    const char* iniKey;
};

inline const ButtonSlot kButtonSlots[] = {
    {"Activate",       "iActivateButtonId"},
    {"Stop",           "iStopButtonId"},
    {"Toggle Wizard",  "iToggleWizardButton"},
};
inline constexpr int kNumButtonSlots = sizeof(kButtonSlots) / sizeof(kButtonSlots[0]);

// --- Digital axis button slot ---
struct DigitalAxisSlot {
    const char* label;
    const char* iniKey;
};

inline const DigitalAxisSlot kDigitalAxisSlots[] = {
    {"Digital Reverse",       "iDigitalReverseButton"},
    {"Digital Roll Left",     "iDigitalRollLeftButton"},
    {"Digital Roll Right",    "iDigitalRollRightButton"},
    {"Digital Strafe Left",   "iDigitalStrafeLeftButton"},
    {"Digital Strafe Right",  "iDigitalStrafeRightButton"},
    {"Digital Strafe Up",     "iDigitalStrafeUpButton"},
    {"Digital Strafe Down",   "iDigitalStrafeDownButton"},
};
inline constexpr int kNumDigitalAxisSlots = sizeof(kDigitalAxisSlots) / sizeof(kDigitalAxisSlots[0]);

// --- Aim axis slot ---
struct AimAxisSlot {
    const char* label;
    const char* iniKey;
    const char* invertIniKey;
    const char* sensitivityKey;
};

inline const AimAxisSlot kAimAxisSlots[] = {
    {"Aim Yaw",   "iAimYawAxis",   "bInvertAimYaw",   "fAimYawSensitivity"},
    {"Aim Pitch", "iAimPitchAxis", "bInvertAimPitch",  "fAimPitchSensitivity"},
};
inline constexpr int kNumAimAxisSlots = 2;

// --- Digital aim slot ---
struct DigitalAimSlot {
    const char* label;
    const char* iniKey;
};

inline const DigitalAimSlot kDigitalAimSlots[] = {
    {"Aim Left",   "iDigitalAimLeftButton"},
    {"Aim Right",  "iDigitalAimRightButton"},
    {"Aim Up",     "iDigitalAimUpButton"},
    {"Aim Down",   "iDigitalAimDownButton"},
    {"Aim Center", "iDigitalAimCenterButton"},
};
inline constexpr int kNumDigitalAimSlots = 5;

// --- Ship action slot (runtime-populated) ---
struct ShipActionSlot {
    std::string label;
    std::string iniKey;
    std::string binding;
};

// --- Custom button expansion ---
struct CustomBindingRow {
    std::string buttonBinding;  // "DeviceName@42" or "(unbound)"
    std::string output;         // "key:0x11" or "mouse:1" or "none"
};

// --- Output catalog for custom bindings ---
struct OutputOption {
    const char* label;
    const char* value;
};

inline const OutputOption kOutputCatalog[] = {
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
inline constexpr int kOutputCatalogSize = sizeof(kOutputCatalog) / sizeof(kOutputCatalog[0]);

inline int FindOutputIndex(const std::string& val) {
    for (int i = 0; i < kOutputCatalogSize; i++) {
        if (val == kOutputCatalog[i].value) return i;
    }
    return -1;
}

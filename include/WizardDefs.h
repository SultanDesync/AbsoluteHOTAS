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
    constexpr int kTurnAssistBtn   = 706;
    constexpr int kMacroBase       = 800;   // 800..899, one per macro trigger button
    constexpr int kProfileTrigger  = 900;
    constexpr int kControlExtensionBase = 920; // 920..935

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

inline const ButtonSlot kControlExtensionSlots[] = {
    {"Hold Current Throttle", "iCruiseHoldButton"},
    {"Full Stop",             "iFullStopButton"},
    {"Cruise 50%",            "iCruiseHalfButton"},
    {"Cruise Max",            "iCruiseMaxButton"},
};
inline constexpr int kNumControlExtensionSlots =
    sizeof(kControlExtensionSlots) / sizeof(kControlExtensionSlots[0]);

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

// --- Macro editor rows (wizard-owned mirror of the [Macro:*] sections) ---
//
// The wizard reads and writes [Macro:*] sections in AbsoluteHOTAS_Custom.ini rather than going
// through MacroEngine. Two reasons, both about not losing the user's work:
//   1. MacroStep stores *resolved* ShipOutputs, so "NextSystem" would round-trip
//      out as "key:0x4D" — silently breaking the control-map-follows-your-rebinds
//      property that makes action targets worth having.
//   2. MacroEngine drops macros it can't run (unbound button, no valid steps). A
//      save built from its view would delete a half-configured macro from the file.
// Editing the file's own tokens keeps the round-trip lossless.

struct MacroStepRow {
    std::vector<std::string> targets;  // tokens: "NextSystem", "key:0x1E"; >1 = chord
    bool hold   = false;               // false = tap, true = hold
    int  amount = 1;                   // tap: repeat count; hold: duration in ms
    int  gapMs  = 50;                  // wait before the next step
};

struct MacroRow {
    std::string name;
    std::string buttonBinding = "(unbound)";
    bool        turbo = false;
    std::vector<MacroStepRow> steps;
};

// Ship-action targets for the macro step picker. Values are the actionIds that
// ShipOutputSystem::ResolveOutputToken understands; labels mirror the wizard's
// ship action rows. Order matches the binding table in ShipOutput.cpp.
struct MacroTargetOption {
    const char* label;
    const char* value;
};

inline const MacroTargetOption kShipActionTargets[] = {
    {"Fire Boosters",          "FireBoosters"},
    {"Switch Flight Modes",    "SwitchFlightModes"},
    {"Toggle POV",             "TogglePov"},
    {"Fire Weapon 0",          "FireWeapon0"},
    {"Fire Weapon 1",          "FireWeapon1"},
    {"Fire Weapon 2",          "FireWeapon2"},
    {"Ship Action 1",          "ShipAction1"},
    {"Select Target",          "SelectTarget"},
    {"Increase System Power",  "IncreaseSystemPower"},
    {"Decrease System Power",  "DecreaseSystemPower"},
    {"Previous System",        "PreviousSystem"},
    {"Next System",            "NextSystem"},
    {"Open Scanner",           "OpenScanner"},
    {"Repair",                 "Repair"},
    {"Ship Alternate Control", "ShipAlternateControlHold"},
    {"Cruise",                 "Cruise"},
    {"Cancel",                 "Cancel"},
    {"Undock / Take-Off",      "UndockTakeOff"},
    {"Get Up",                 "GetUp"},
    {"Exit Ship",              "ExitShipFromCockpit"},
    {"Zoom Camera In",         "ZoomCameraIn"},
    {"Zoom Camera Out",        "ZoomCameraOut"},
    {"Autopilot On/Off",       "AutopilotOnOff"},
};
inline constexpr int kNumShipActionTargets = sizeof(kShipActionTargets) / sizeof(kShipActionTargets[0]);

// --- Output catalog for custom bindings ---
struct OutputOption {
    const char* label;
    const char* value;
};

inline const OutputOption kOutputCatalog[] = {
    // Mouse
    {"Mouse 1", "mouse:1"}, {"Mouse 2", "mouse:2"}, {"Mouse 3", "mouse:3"}, {"Mouse 4", "mouse:4"},

    // Modifiers & Control
    {"Space", "key:0x39"}, {"Tab", "key:0x0F"}, {"Esc", "key:0x01"}, {"Enter", "key:0x1C"}, {"Backspace", "key:0x0E"},
    {"L Shift", "key:0x2A"}, {"L Ctrl", "key:0x1D"}, {"L Alt", "key:0x38"},
    {"R Shift", "key:0x36"}, {"R Ctrl", "key:0x9D"}, {"R Alt", "key:0xB8"},

    // Alphabet
    {"A", "key:0x1E"}, {"B", "key:0x30"}, {"C", "key:0x2E"}, {"D", "key:0x20"},
    {"E", "key:0x12"}, {"F", "key:0x21"}, {"G", "key:0x22"}, {"H", "key:0x23"},
    {"I", "key:0x17"}, {"J", "key:0x24"}, {"K", "key:0x25"}, {"L", "key:0x26"},
    {"M", "key:0x32"}, {"N", "key:0x31"}, {"O", "key:0x18"}, {"P", "key:0x19"},
    {"Q", "key:0x10"}, {"R", "key:0x13"}, {"S", "key:0x1F"}, {"T", "key:0x14"},
    {"U", "key:0x16"}, {"V", "key:0x2F"}, {"W", "key:0x11"}, {"X", "key:0x2D"},
    {"Y", "key:0x15"}, {"Z", "key:0x2C"},

    // Numbers
    {"1", "key:0x02"}, {"2", "key:0x03"}, {"3", "key:0x04"}, {"4", "key:0x05"},
    {"5", "key:0x06"}, {"6", "key:0x07"}, {"7", "key:0x08"}, {"8", "key:0x09"},
    {"9", "key:0x0A"}, {"0", "key:0x0B"},

    // Symbols
    {"Minus (-)", "key:0x0C"}, {"Equals (=)", "key:0x0D"},
    {"L Bracket ([)", "key:0x1A"}, {"R Bracket (])", "key:0x1B"},
    {"Semicolon (;)", "key:0x27"}, {"Apostrophe (')", "key:0x28"},
    {"Grave (`)", "key:0x29"}, {"Backslash (\\)", "key:0x2B"},
    {"Comma (,)", "key:0x33"}, {"Period (.)", "key:0x34"}, {"Slash (/)", "key:0x35"},

    // Arrows
    {"Up", "key:0x48"}, {"Down", "key:0x50"}, {"Left", "key:0x4B"}, {"Right", "key:0x4D"},

    // Numpad
    {"Numpad 0", "key:0x52"}, {"Numpad 1", "key:0x4F"}, {"Numpad 2", "key:0x50"}, {"Numpad 3", "key:0x51"},
    {"Numpad 4", "key:0x4B"}, {"Numpad 5", "key:0x4C"}, {"Numpad 6", "key:0x4D"},
    {"Numpad 7", "key:0x47"}, {"Numpad 8", "key:0x48"}, {"Numpad 9", "key:0x49"},
    {"Numpad Add", "key:0x4E"}, {"Numpad Sub", "key:0x4A"}, {"Numpad Mul", "key:0x37"}, {"Numpad Div", "key:0xB5"},
    {"Numpad Enter", "key:0x9C"}, {"Numpad Dec", "key:0x53"},

    // F-Keys
    {"F1", "key:0x3B"}, {"F2", "key:0x3C"}, {"F3", "key:0x3D"}, {"F4", "key:0x3E"},
    {"F5", "key:0x3F"}, {"F6", "key:0x40"}, {"F7", "key:0x41"}, {"F8", "key:0x42"},
    {"F9", "key:0x43"}, {"F10", "key:0x44"}, {"F11", "key:0x57"}, {"F12", "key:0x58"},
    
    // Extended Control
    {"Insert", "key:0xD2"}, {"Delete", "key:0xD3"}, {"Home", "key:0xC7"}, {"End", "key:0xCF"},
    {"Page Up", "key:0xC9"}, {"Page Down", "key:0xD1"},
};
inline constexpr int kOutputCatalogSize = sizeof(kOutputCatalog) / sizeof(kOutputCatalog[0]);

inline int FindOutputIndex(const std::string& val) {
    for (int i = 0; i < kOutputCatalogSize; i++) {
        if (val == kOutputCatalog[i].value) return i;
    }
    return -1;
}

// Friendly label for a macro target token, or nullptr if it's neither a known ship
// action nor a catalog key — callers then show the raw token, so a hand-written
// "key:0xFF" stays visible and editable instead of rendering as blank.
inline const char* FindMacroTargetLabel(const std::string& token) {
    for (int i = 0; i < kNumShipActionTargets; i++) {
        if (token == kShipActionTargets[i].value) return kShipActionTargets[i].label;
    }
    const int k = FindOutputIndex(token);
    return (k >= 0) ? kOutputCatalog[k].label : nullptr;
}

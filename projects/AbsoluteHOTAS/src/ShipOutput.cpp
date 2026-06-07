#include "PCH.h"
#include "ShipOutput.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Internal helpers
// ============================================================================

static std::string TrimLower(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) return {};
    std::string lowered(begin, end);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

static void ShipLog(const char* msg) {
    RuntimePaths::AppendLogAlways("[ShipOutput]", msg);
}

// ============================================================================
// POV (HAT) switch support
// Virtual buttons 129-144 → 4 POV switches × 4 directions
// POV 0: Up=129, Right=130, Down=131, Left=132
// POV 1: Up=133, Right=134, Down=135, Left=136
// ============================================================================
static bool IsPovButtonPressed(const DIJOYSTATE2* state, int virtualButton) {
    if (!state || virtualButton < 129 || virtualButton > 144) return false;
    int povIndex  = (virtualButton - 129) / 4;
    int direction = (virtualButton - 129) % 4;
    DWORD pov = state->rgdwPOV[povIndex];
    if (LOWORD(pov) == 0xFFFF) return false;
    static constexpr DWORD kDirAngles[4] = { 0, 9000, 18000, 27000 };
    DWORD target = kDirAngles[direction];
    DWORD diff = (pov > target) ? (pov - target) : (target - pov);
    if (diff > 18000) diff = 36000 - diff;
    return diff <= 4500;
}

static bool IsButtonPressedImpl(const BindingRef& ref) {
    if (ref.value < 1) return false;
    const DIJOYSTATE2* st = DeviceManager::GetCachedState(ref.deviceIndex);
    if (!st) return false;
    if (ref.value <= 128) return (st->rgbButtons[ref.value - 1] & 0x80) != 0;
    if (ref.value <= 144) return IsPovButtonPressed(st, ref.value);
    return false;
}

// ============================================================================
// Output parsing
// ============================================================================

static bool IsExtendedKeyboardScanCode(uint16_t sc) {
    return sc == 0x48 || sc == 0x50 || sc == 0x4B || sc == 0x4D;
}

static ShipOutput ParseShipOutput(std::string_view value, ShipOutput fallback) {
    const std::string normalized = TrimLower(value);
    if (normalized.empty()) return fallback;
    if (normalized == "none") return NoOutput;

    auto parseNumber = [](std::string_view text, uint16_t& result) {
        const std::string number(text);
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(number.c_str(), &end, 0);
        if (end == number.c_str() || *end != '\0' || parsed > 0xFFFFul) return false;
        result = static_cast<uint16_t>(parsed);
        return true;
    };

    uint16_t code = 0;
    if (normalized.rfind("key:", 0) == 0 && parseNumber(std::string_view(normalized).substr(4), code) && code > 0)
        return { ShipOutputKind::Keyboard, code, IsExtendedKeyboardScanCode(code) };

    if (normalized.rfind("mouse:", 0) == 0 && parseNumber(std::string_view(normalized).substr(6), code) && code >= 1 && code <= 4)
        return { ShipOutputKind::Mouse, code, false };

    ShipLog(("Warning: invalid [ShipButtonOutputs] value '" + std::string(value) + "'; using vanilla default.").c_str());
    return fallback;
}

static const ShipOutput& DefaultShipOutputForAction(std::string_view actionId) {
    struct DefaultOutput { const char* actionId; ShipOutput output; };
    static constexpr std::array<DefaultOutput, 23> defaults{ {
        { "FireBoosters",             { ShipOutputKind::Keyboard, 0x2A, false } },
        { "SwitchFlightModes",        SpaceOutput },
        { "TogglePov",                { ShipOutputKind::Keyboard, 0x10, false } },
        { "FireWeapon0",              { ShipOutputKind::Mouse,    1,    false } },
        { "FireWeapon1",              { ShipOutputKind::Mouse,    2,    false } },
        { "FireWeapon2",              { ShipOutputKind::Keyboard, 0x22, false } },
        { "ShipAction1",              { ShipOutputKind::Keyboard, 0x13, false } },
        { "SelectTarget",             { ShipOutputKind::Keyboard, 0x12, false } },
        { "IncreaseSystemPower",      { ShipOutputKind::Keyboard, 0x48, true  } },
        { "DecreaseSystemPower",      { ShipOutputKind::Keyboard, 0x50, true  } },
        { "PreviousSystem",           { ShipOutputKind::Keyboard, 0x4B, true  } },
        { "NextSystem",               { ShipOutputKind::Keyboard, 0x4D, true  } },
        { "OpenScanner",              { ShipOutputKind::Keyboard, 0x21, false } },
        { "Repair",                   { ShipOutputKind::Keyboard, 0x18, false } },
        { "ShipAlternateControlHold", { ShipOutputKind::Keyboard, 0x38, false } },
        { "Cruise",                   { ShipOutputKind::Keyboard, 0x14, false } },
        { "Cancel",                   NoOutput },
        { "UndockTakeOff",            SpaceOutput },
        { "GetUp",                    { ShipOutputKind::Keyboard, 0x12, false } },
        { "ExitShipFromCockpit",      { ShipOutputKind::Keyboard, 0x2D, false } },
        { "ZoomCameraIn",             { ShipOutputKind::Mouse,    1,    false } },
        { "ZoomCameraOut",            { ShipOutputKind::Mouse,    2,    false } },
        { "AutopilotOnOff",           SpaceOutput },
    } };
    const auto found = std::find_if(defaults.begin(), defaults.end(),
        [&](const DefaultOutput& d) { return actionId == d.actionId; });
    return (found != defaults.end()) ? found->output : NoOutput;
}

static int ParseExpansionButtonKey(std::string_view key) {
    const std::string normalized = TrimLower(key);
    std::string_view numberText;
    if (normalized.rfind("ibutton", 0) == 0)       numberText = std::string_view(normalized).substr(7);
    else if (normalized.rfind("button", 0) == 0)   numberText = std::string_view(normalized).substr(6);
    else return -1;
    if (numberText.empty()) return -1;
    const std::string number(numberText);
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (end == number.c_str() || *end != '\0' || parsed < 1 || parsed > 144) return -1;
    return static_cast<int>(parsed);
}

// ============================================================================
// Internal state
// ============================================================================

struct HeldShipOutput {
    ShipOutput output;
    std::vector<uint32_t> owners;
};

static std::vector<ShipButtonBinding> s_shipButtonBindings;
static std::vector<HeldShipOutput>    s_heldShipOutputs;

static bool SameOutput(const ShipOutput& lhs, const ShipOutput& rhs) {
    return lhs.kind == rhs.kind && lhs.code == rhs.code && lhs.extended == rhs.extended;
}

static uint32_t ShipOwnerIdForIndex(size_t index) {
    return OwnerShipButtonBase + static_cast<uint32_t>(index);
}

// ============================================================================
// SendInput helpers
// ============================================================================

static void SendKeyboardScanCode(uint16_t scanCode, bool extended, bool keyUp) {
    if (scanCode == 0) return;
    INPUT input = {};
    input.type      = INPUT_KEYBOARD;
    input.ki.wScan  = scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (keyUp ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &input, sizeof(INPUT));
}

static void SendMouseButton(uint16_t mouseButton, bool keyUp) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (mouseButton) {
        case 1: input.mi.dwFlags = keyUp ? MOUSEEVENTF_LEFTUP   : MOUSEEVENTF_LEFTDOWN;   break;
        case 2: input.mi.dwFlags = keyUp ? MOUSEEVENTF_RIGHTUP  : MOUSEEVENTF_RIGHTDOWN;  break;
        case 3: input.mi.dwFlags = keyUp ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN; break;
        case 4:
            input.mi.dwFlags   = keyUp ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
            input.mi.mouseData = XBUTTON1;
            break;
        default: return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

static void EmitShipOutput(const ShipOutput& output, bool keyUp) {
    switch (output.kind) {
        case ShipOutputKind::Keyboard: SendKeyboardScanCode(output.code, output.extended, keyUp); break;
        case ShipOutputKind::Mouse:    SendMouseButton(output.code, keyUp);                       break;
        case ShipOutputKind::None:     break;
    }
}

static void PulseOutput(const ShipOutput& output) {
    if (output.kind == ShipOutputKind::None) return;
    EmitShipOutput(output, false);
    EmitShipOutput(output, true);
}

// ============================================================================
// Public API implementation
// ============================================================================

namespace ShipOutputSystem {

void SetOutputHeld(const ShipOutput& output, uint32_t ownerId, bool held) {
    if (output.kind == ShipOutputKind::None) return;

    auto it = std::find_if(s_heldShipOutputs.begin(), s_heldShipOutputs.end(),
        [&](const HeldShipOutput& h) { return SameOutput(h.output, output); });

    if (held) {
        if (it == s_heldShipOutputs.end()) {
            s_heldShipOutputs.push_back({ output, { ownerId } });
            EmitShipOutput(output, false);
            return;
        }
        if (std::find(it->owners.begin(), it->owners.end(), ownerId) == it->owners.end())
            it->owners.push_back(ownerId);
        return;
    }

    if (it == s_heldShipOutputs.end()) return;
    std::erase(it->owners, ownerId);
    if (it->owners.empty()) {
        EmitShipOutput(it->output, true);
        s_heldShipOutputs.erase(it);
    }
}

void ReleaseOwnerOutputs(uint32_t ownerId) {
    for (auto it = s_heldShipOutputs.begin(); it != s_heldShipOutputs.end();) {
        std::erase(it->owners, ownerId);
        if (it->owners.empty()) {
            EmitShipOutput(it->output, true);
            it = s_heldShipOutputs.erase(it);
        } else {
            ++it;
        }
    }
}

void ReleaseAllShipButtonOutputs() {
    for (const auto& h : s_heldShipOutputs) EmitShipOutput(h.output, true);
    s_heldShipOutputs.clear();
    for (auto& b : s_shipButtonBindings) b.previousPressed = false;
}

bool IsBoostOutputHeld() {
    if (s_shipButtonBindings.empty()) return false;
    const auto& boostBinding = s_shipButtonBindings[0];
    for (const auto& held : s_heldShipOutputs)
        if (SameOutput(held.output, boostBinding.output)) return true;
    return false;
}

ShipButtonBinding* GetShipButtonBindings() {
    return s_shipButtonBindings.data();
}

int GetShipButtonCount() {
    return static_cast<int>(s_shipButtonBindings.size());
}

void LoadShipButtonBindings(CSimpleIniA& ini) {
    struct BindingDef {
        const char* actionId;
        const char* sourceIniKey;
        const char* outputIniKey;
    };

    static constexpr std::array<BindingDef, 23> defs{ {
        { "FireBoosters",             "iFireBoostersButton",             "sFireBoostersOutput"             },
        { "SwitchFlightModes",        "iSwitchFlightModesButton",        "sSwitchFlightModesOutput"        },
        { "TogglePov",                "iTogglePovButton",                "sTogglePovOutput"                },
        { "FireWeapon0",              "iFireWeapon0Button",              "sFireWeapon0Output"              },
        { "FireWeapon1",              "iFireWeapon1Button",              "sFireWeapon1Output"              },
        { "FireWeapon2",              "iFireWeapon2Button",              "sFireWeapon2Output"              },
        { "ShipAction1",              "iShipAction1Button",              "sShipAction1Output"              },
        { "SelectTarget",             "iSelectTargetButton",             "sSelectTargetOutput"             },
        { "IncreaseSystemPower",      "iIncreaseSystemPowerButton",      "sIncreaseSystemPowerOutput"      },
        { "DecreaseSystemPower",      "iDecreaseSystemPowerButton",      "sDecreaseSystemPowerOutput"      },
        { "PreviousSystem",           "iPreviousSystemButton",           "sPreviousSystemOutput"           },
        { "NextSystem",               "iNextSystemButton",               "sNextSystemOutput"               },
        { "OpenScanner",              "iOpenScannerButton",              "sOpenScannerOutput"              },
        { "Repair",                   "iRepairButton",                   "sRepairOutput"                   },
        { "ShipAlternateControlHold", "iShipAlternateControlHoldButton", "sShipAlternateControlHoldOutput" },
        { "Cruise",                   "iCruiseButton",                   "sCruiseOutput"                   },
        { "Cancel",                   "iCancelButton",                   "sCancelOutput"                   },
        { "UndockTakeOff",            "iUndockTakeOffButton",            "sUndockTakeOffOutput"            },
        { "GetUp",                    "iGetUpButton",                    "sGetUpOutput"                    },
        { "ExitShipFromCockpit",      "iExitShipFromCockpitButton",      "sExitShipFromCockpitOutput"      },
        { "ZoomCameraIn",             "iZoomCameraInButton",             "sZoomCameraInOutput"             },
        { "ZoomCameraOut",            "iZoomCameraOutButton",            "sZoomCameraOutOutput"            },
        { "AutopilotOnOff",           "iAutopilotOnOffButton",           "sAutopilotOnOffOutput"           },
    } };

    s_shipButtonBindings.clear();
    s_shipButtonBindings.reserve(defs.size());

    for (const auto& def : defs) {
        const ShipOutput fallback = DefaultShipOutputForAction(def.actionId);
        const char* outputValue  = ini.GetValue("ShipButtonOutputs", def.outputIniKey, nullptr);
        BindingRef bRef = ParseBindingRef(ini.GetValue("ShipButtons", def.sourceIniKey, ""), -1);

        ShipButtonBinding binding{
            def.actionId,
            def.sourceIniKey,
            def.outputIniKey,
            bRef,
            outputValue ? ParseShipOutput(outputValue, fallback) : fallback,
            ShipBindingMode::Hold,  // All actions default to Hold; future: per-action overrides
            false
        };

        if (binding.buttonRef.value > 144) {
            ShipLog(("Warning: [ShipButtons] " + std::string(def.sourceIniKey) +
                     " is outside 1-144 range; disabling.").c_str());
            binding.buttonRef.value = -1;
        }

        s_shipButtonBindings.push_back(binding);
    }

    // [ButtonExpansion] — extra button → output mappings
    CSimpleIniA::TNamesDepend keys;
    if (!ini.GetAllKeys("ButtonExpansion", keys)) return;

    int loaded = 0;
    for (const auto& key : keys) {
        if (!key.pItem) continue;
        const char* outputValue = ini.GetValue("ButtonExpansion", key.pItem, nullptr);
        if (!outputValue || TrimLower(outputValue).empty() || TrimLower(outputValue) == "none") continue;

        const ShipOutput output = ParseShipOutput(outputValue, NoOutput);
        if (output.kind == ShipOutputKind::None) continue;

        std::string deviceName;
        std::string_view svKey(key.pItem);
        auto atPos = svKey.rfind('@');
        if (atPos != std::string_view::npos) {
            deviceName = std::string(svKey.substr(0, atPos));
            svKey = svKey.substr(atPos + 1);
        }

        int buttonId = ParseExpansionButtonKey(svKey);
        if (buttonId < 1 || buttonId > 144) {
            ShipLog(("Warning: [ButtonExpansion] key '" + std::string(key.pItem) +
                     "' is not iButton1..iButton144; ignoring.").c_str());
            continue;
        }

        BindingRef finalRef;
        finalRef.deviceName  = deviceName;
        finalRef.value       = buttonId;
        finalRef.deviceIndex = -1;

        // #N@ syntax written by BindingWizard for duplicate devices
        if (!deviceName.empty() && deviceName.front() == '#') {
            char* endPtr = nullptr;
            long idx = std::strtol(deviceName.c_str() + 1, &endPtr, 10);
            if (endPtr != deviceName.c_str() + 1 && idx >= 0) {
                finalRef.deviceIndex = static_cast<int>(idx);
                finalRef.deviceName.clear();
            }
        }

        s_shipButtonBindings.push_back({
            "ButtonExpansion",
            key.pItem,
            key.pItem,
            finalRef,
            output,
            ShipBindingMode::Hold,
            false
        });
        loaded++;
    }

    if (loaded > 0)
        ShipLog(("Loaded " + std::to_string(loaded) + " [ButtonExpansion] binding(s).").c_str());
}

void UpdateShipButtonBindings() {
    for (int i = 0; i < static_cast<int>(s_shipButtonBindings.size()); ++i) {
        auto& binding = s_shipButtonBindings[i];
        bool pressed  = IsButtonPressedImpl(binding.buttonRef);
        const uint32_t ownerId = ShipOwnerIdForIndex(i);

        if (binding.mode == ShipBindingMode::Hold) {
            if (pressed != binding.previousPressed)
                SetOutputHeld(binding.output, ownerId, pressed);
        } else if (pressed && !binding.previousPressed) {
            PulseOutput(binding.output);
        }

        if (!pressed && binding.previousPressed)
            ReleaseOwnerOutputs(ownerId);

        binding.previousPressed = pressed;
    }
}

} // namespace ShipOutputSystem

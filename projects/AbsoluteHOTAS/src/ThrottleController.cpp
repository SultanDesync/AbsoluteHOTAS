#include "PCH.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"
#include "AbsoluteGlobals.h"
#include "RuntimePaths.h"
#include "DeviceManager.h"
#include "UIHook.h"
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <cmath>
#include <SimpleIni.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

// ---- Static member definitions ----
ThrottleController::Config ThrottleController::s_config;
std::atomic<bool>  ThrottleController::s_running{ false };
std::atomic<bool>  ThrottleController::s_isStandingDown{ false };
std::atomic<bool>  ThrottleController::s_configReloadRequested{ false };
std::atomic<float> ThrottleController::s_currentThrottle{ 0.0f };
std::thread        ThrottleController::s_thread;

static bool g_verboseLog = false;

// ---- Logging ----
static void RotateLogIfNeeded() {
    static bool checked = false;
    if (checked) return;
    checked = true;

    const auto logPath = RuntimePaths::LogPath();
    const auto oldLogPath = RuntimePaths::PluginDirectory() / L"AbsoluteHOTAS.log.old";
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        if (size.QuadPart > 1024ull * 1024ull) {
            DeleteFileW(oldLogPath.c_str());
            MoveFileExW(logPath.c_str(), oldLogPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

static void RawCtrlLog(const char* msg) {
    if (!g_verboseLog) return;
    RotateLogIfNeeded();
    RuntimePaths::AppendLogAlways("[Controller]", msg);
}

static void CtrlLog(const std::string& msg) {
    RawCtrlLog(msg.c_str());
}

static std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

// ---- POV (HAT) Switch Support ----
// Virtual buttons 129-144 map to the 4 POV switches x 4 directions.
// POV 0: Up=129, Right=130, Down=131, Left=132
// POV 1: Up=133, Right=134, Down=135, Left=136
// POV 2: Up=137, Right=138, Down=139, Left=140
// POV 3: Up=141, Right=142, Down=143, Left=144
static bool IsPovButtonPressed(const DIJOYSTATE2* state, int virtualButton) {
    if (!state || virtualButton < 129 || virtualButton > 144) return false;
    int povIndex = (virtualButton - 129) / 4;   // 0-3
    int direction = (virtualButton - 129) % 4;  // 0=Up, 1=Right, 2=Down, 3=Left
    DWORD pov = state->rgdwPOV[povIndex];
    if (LOWORD(pov) == 0xFFFF) return false; // POV centered (not pressed)
    // POV values are in hundredths of degrees: 0=Up, 9000=Right, 18000=Down, 27000=Left
    // Allow ±4500 tolerance for diagonal detection
    static constexpr DWORD kDirAngles[4] = { 0, 9000, 18000, 27000 };
    DWORD target = kDirAngles[direction];
    DWORD diff = (pov > target) ? (pov - target) : (target - pov);
    if (diff > 18000) diff = 36000 - diff; // wrap-around
    return diff <= 4500;
}

static bool IsButtonPressedStatic(const BindingRef& ref) {
    if (ref.value < 1) return false;
    const DIJOYSTATE2* st = DeviceManager::GetCachedState(ref.deviceIndex);
    if (!st) return false;
    if (ref.value <= 128) {
        return (st->rgbButtons[ref.value - 1] & 0x80) != 0;
    }
    if (ref.value <= 144) {
        return IsPovButtonPressed(st, ref.value);
    }
    return false;
}

enum class ShipOutputKind {
    Keyboard,
    Mouse,
    None
};

enum class ShipBindingMode {
    Hold,
    Pulse
};

struct ShipOutput {
    ShipOutputKind kind;
    uint16_t code;
    bool extended;
};

struct ShipButtonBinding {
    const char* actionId;
    const char* sourceIniKey;
    const char* outputIniKey;
    BindingRef buttonRef;
    ShipOutput output;
    ShipBindingMode mode;
    bool previousPressed;
};

static constexpr ShipOutput NoOutput{ ShipOutputKind::None, 0, false };
static constexpr ShipOutput SpaceOutput{ ShipOutputKind::Keyboard, 0x39, false };
static constexpr uint32_t OwnerStrafeModifier = 0x00000001u;
static constexpr ShipOutput ReverseOutput{ ShipOutputKind::Keyboard, 0x1F, false };
static constexpr uint32_t OwnerDigitalReverse = 0x00000002u;
static constexpr uint32_t OwnerBoostCancel    = 0x00000003u;  // Brief reverse hold to cancel boost on release
static constexpr uint32_t OwnerBoostZone      = 0x00000004u;  // Throttle axis boost zone trigger
static constexpr uint32_t OwnerShipButtonBase = 0x00001000u;

static std::vector<ShipButtonBinding> s_shipButtonBindings;

struct HeldShipOutput {
    ShipOutput output;
    std::vector<uint32_t> owners;
};

static std::vector<HeldShipOutput> s_heldShipOutputs;

static bool SameOutput(const ShipOutput& lhs, const ShipOutput& rhs) {
    return lhs.kind == rhs.kind && lhs.code == rhs.code && lhs.extended == rhs.extended;
}

static std::string TrimLower(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) return {};

    return LowerAscii(std::string_view(begin, end));
}

static bool IsExtendedKeyboardScanCode(uint16_t scanCode) {
    switch (scanCode) {
        case 0x48: // Up
        case 0x50: // Down
        case 0x4B: // Left
        case 0x4D: // Right
            return true;
        default:
            return false;
    }
}

static uint32_t ShipOwnerIdForIndex(size_t index) {
    return OwnerShipButtonBase + static_cast<uint32_t>(index);
}

static void SendKeyboardScanCode(uint16_t scanCode, bool extended, bool keyUp) {
    if (scanCode == 0) return;

    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (keyUp ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &input, sizeof(INPUT));
}

static void SendMouseButton(uint16_t mouseButton, bool keyUp) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;

    switch (mouseButton) {
        case 1:
            input.mi.dwFlags = keyUp ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
            break;
        case 2:
            input.mi.dwFlags = keyUp ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
            break;
        case 3:
            input.mi.dwFlags = keyUp ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
            break;
        case 4:
            input.mi.dwFlags = keyUp ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
            input.mi.mouseData = XBUTTON1;
            break;
        default:
            return;
    }

    SendInput(1, &input, sizeof(INPUT));
}

static void EmitShipOutput(const ShipOutput& output, bool keyUp) {
    switch (output.kind) {
        case ShipOutputKind::Keyboard:
            SendKeyboardScanCode(output.code, output.extended, keyUp);
            break;
        case ShipOutputKind::Mouse:
            SendMouseButton(output.code, keyUp);
            break;
        case ShipOutputKind::None:
            break;
    }
}

static void PulseOutput(const ShipOutput& output) {
    if (output.kind == ShipOutputKind::None) return;
    EmitShipOutput(output, false);
    EmitShipOutput(output, true);
}

static void SetOutputHeld(const ShipOutput& output, uint32_t ownerId, bool held) {
    if (output.kind == ShipOutputKind::None) return;

    auto it = std::find_if(s_heldShipOutputs.begin(), s_heldShipOutputs.end(), [&](const HeldShipOutput& heldOutput) {
        return SameOutput(heldOutput.output, output);
    });

    if (held) {
        if (it == s_heldShipOutputs.end()) {
            s_heldShipOutputs.push_back({ output, { ownerId } });
            EmitShipOutput(output, false);
            return;
        }

        if (std::find(it->owners.begin(), it->owners.end(), ownerId) == it->owners.end()) {
            it->owners.push_back(ownerId);
        }
        return;
    }

    if (it == s_heldShipOutputs.end()) return;

    std::erase(it->owners, ownerId);
    if (it->owners.empty()) {
        EmitShipOutput(it->output, true);
        s_heldShipOutputs.erase(it);
    }
}

static void ReleaseOwnerOutputs(uint32_t ownerId) {
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

static void ReleaseAllShipButtonOutputs() {
    for (const auto& heldOutput : s_heldShipOutputs) {
        EmitShipOutput(heldOutput.output, true);
    }
    s_heldShipOutputs.clear();

    for (auto& binding : s_shipButtonBindings) {
        binding.previousPressed = false;
    }
}

static const ShipOutput& DefaultShipOutputForAction(std::string_view actionId) {
    struct DefaultOutput {
        const char* actionId;
        ShipOutput output;
    };

    static constexpr std::array<DefaultOutput, 23> defaults{ {
        { "FireBoosters", { ShipOutputKind::Keyboard, 0x2A, false } },
        { "SwitchFlightModes", SpaceOutput },
        { "TogglePov", { ShipOutputKind::Keyboard, 0x10, false } },
        { "FireWeapon0", { ShipOutputKind::Mouse, 1, false } },
        { "FireWeapon1", { ShipOutputKind::Mouse, 2, false } },
        { "FireWeapon2", { ShipOutputKind::Keyboard, 0x22, false } },
        { "ShipAction1", { ShipOutputKind::Keyboard, 0x13, false } },
        { "SelectTarget", { ShipOutputKind::Keyboard, 0x12, false } },
        { "IncreaseSystemPower", { ShipOutputKind::Keyboard, 0x48, true } },
        { "DecreaseSystemPower", { ShipOutputKind::Keyboard, 0x50, true } },
        { "PreviousSystem", { ShipOutputKind::Keyboard, 0x4B, true } },
        { "NextSystem", { ShipOutputKind::Keyboard, 0x4D, true } },
        { "OpenScanner", { ShipOutputKind::Keyboard, 0x21, false } },
        { "Repair", { ShipOutputKind::Keyboard, 0x18, false } },
        { "ShipAlternateControlHold", { ShipOutputKind::Keyboard, 0x38, false } },
        { "Cruise", { ShipOutputKind::Keyboard, 0x14, false } },
        { "Cancel", NoOutput },
        { "UndockTakeOff", SpaceOutput },
        { "GetUp", { ShipOutputKind::Keyboard, 0x12, false } },
        { "ExitShipFromCockpit", { ShipOutputKind::Keyboard, 0x2D, false } },
        { "ZoomCameraIn", { ShipOutputKind::Mouse, 1, false } },
        { "ZoomCameraOut", { ShipOutputKind::Mouse, 2, false } },
        { "AutopilotOnOff", SpaceOutput },
    } };

    const auto found = std::find_if(defaults.begin(), defaults.end(), [&](const DefaultOutput& candidate) {
        return actionId == candidate.actionId;
    });

    return (found != defaults.end()) ? found->output : NoOutput;
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
    if (normalized.rfind("key:", 0) == 0 && parseNumber(std::string_view(normalized).substr(4), code) && code > 0) {
        return { ShipOutputKind::Keyboard, code, IsExtendedKeyboardScanCode(code) };
    }

    if (normalized.rfind("mouse:", 0) == 0 && parseNumber(std::string_view(normalized).substr(6), code) && code >= 1 && code <= 4) {
        return { ShipOutputKind::Mouse, code, false };
    }

    CtrlLog("Warning: invalid [ShipButtonOutputs] value '" + std::string(value) + "'; using vanilla default.");
    return fallback;
}

static ShipBindingMode DefaultShipBindingModeForAction(std::string_view actionId) {
    (void)actionId;
    return ShipBindingMode::Hold;
}

static int ParseExpansionButtonKey(std::string_view key) {
    const std::string normalized = TrimLower(key);
    std::string_view numberText;

    if (normalized.rfind("ibutton", 0) == 0) {
        numberText = std::string_view(normalized).substr(7);
    } else if (normalized.rfind("button", 0) == 0) {
        numberText = std::string_view(normalized).substr(6);
    } else {
        return -1;
    }

    if (numberText.empty()) return -1;

    const std::string number(numberText);
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (end == number.c_str() || *end != '\0' || parsed < 1 || parsed > 144) {
        return -1;
    }

    return static_cast<int>(parsed);
}

static void LoadButtonExpansionBindings(CSimpleIniA& ini) {
    CSimpleIniA::TNamesDepend keys;
    if (!ini.GetAllKeys("ButtonExpansion", keys)) {
        return;
    }

    int loaded = 0;
    for (const auto& key : keys) {
        if (!key.pItem) continue;

        const char* outputValue = ini.GetValue("ButtonExpansion", key.pItem, nullptr);
        if (!outputValue || TrimLower(outputValue).empty() || TrimLower(outputValue) == "none") {
            continue;
        }

        const ShipOutput output = ParseShipOutput(outputValue, NoOutput);
        if (output.kind == ShipOutputKind::None) {
            continue;
        }

        // Parse key: "iButton12" or "DeviceName@iButton12"
        std::string deviceName;
        std::string_view svKey(key.pItem);
        auto atPos = svKey.rfind('@');
        if (atPos != std::string_view::npos) {
            deviceName = std::string(svKey.substr(0, atPos));
            svKey = svKey.substr(atPos + 1);
        }

        int buttonId = ParseExpansionButtonKey(std::string(svKey).c_str());
        if (buttonId < 1 || buttonId > 144) {
            CtrlLog("Warning: [ButtonExpansion] key '" + std::string(key.pItem) + "' is not iButton1..iButton144; ignoring.");
            continue;
        }

        BindingRef finalRef;
        finalRef.deviceName = deviceName;
        finalRef.value = buttonId;
        finalRef.deviceIndex = -1;

        // Handle #N device index prefix written by BindingWizard for duplicate devices
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

    if (loaded > 0) {
        CtrlLog("Loaded " + std::to_string(loaded) + " [ButtonExpansion] extra button binding(s).");
    }
}

static void LoadShipButtonBindings(CSimpleIniA& ini) {
    struct BindingDef {
        const char* actionId;
        const char* sourceIniKey;
        const char* outputIniKey;
    };

    static constexpr std::array<BindingDef, 23> defs{ {
        { "FireBoosters", "iFireBoostersButton", "sFireBoostersOutput" },
        { "SwitchFlightModes", "iSwitchFlightModesButton", "sSwitchFlightModesOutput" },
        { "TogglePov", "iTogglePovButton", "sTogglePovOutput" },
        { "FireWeapon0", "iFireWeapon0Button", "sFireWeapon0Output" },
        { "FireWeapon1", "iFireWeapon1Button", "sFireWeapon1Output" },
        { "FireWeapon2", "iFireWeapon2Button", "sFireWeapon2Output" },
        { "ShipAction1", "iShipAction1Button", "sShipAction1Output" },
        { "SelectTarget", "iSelectTargetButton", "sSelectTargetOutput" },
        { "IncreaseSystemPower", "iIncreaseSystemPowerButton", "sIncreaseSystemPowerOutput" },
        { "DecreaseSystemPower", "iDecreaseSystemPowerButton", "sDecreaseSystemPowerOutput" },
        { "PreviousSystem", "iPreviousSystemButton", "sPreviousSystemOutput" },
        { "NextSystem", "iNextSystemButton", "sNextSystemOutput" },
        { "OpenScanner", "iOpenScannerButton", "sOpenScannerOutput" },
        { "Repair", "iRepairButton", "sRepairOutput" },
        { "ShipAlternateControlHold", "iShipAlternateControlHoldButton", "sShipAlternateControlHoldOutput" },
        { "Cruise", "iCruiseButton", "sCruiseOutput" },
        { "Cancel", "iCancelButton", "sCancelOutput" },
        { "UndockTakeOff", "iUndockTakeOffButton", "sUndockTakeOffOutput" },
        { "GetUp", "iGetUpButton", "sGetUpOutput" },
        { "ExitShipFromCockpit", "iExitShipFromCockpitButton", "sExitShipFromCockpitOutput" },
        { "ZoomCameraIn", "iZoomCameraInButton", "sZoomCameraInOutput" },
        { "ZoomCameraOut", "iZoomCameraOutButton", "sZoomCameraOutOutput" },
        { "AutopilotOnOff", "iAutopilotOnOffButton", "sAutopilotOnOffOutput" },
    } };

    s_shipButtonBindings.clear();
    s_shipButtonBindings.reserve(defs.size());

    for (const auto& def : defs) {
        const ShipOutput fallback = DefaultShipOutputForAction(def.actionId);
        const char* outputValue = ini.GetValue("ShipButtonOutputs", def.outputIniKey, nullptr);
        BindingRef bRef = ParseBindingRef(ini.GetValue("ShipButtons", def.sourceIniKey, ""), -1);
        
        ShipButtonBinding binding{
            def.actionId,
            def.sourceIniKey,
            def.outputIniKey,
            bRef,
            outputValue ? ParseShipOutput(outputValue, fallback) : fallback,
            DefaultShipBindingModeForAction(def.actionId),
            false
        };

        if (binding.buttonRef.value > 144) {
            CtrlLog("Warning: [ShipButtons] " + std::string(def.sourceIniKey) + " is outside 1-144 range (128 buttons + 16 POV); disabling.");
            binding.buttonRef.value = -1;
        }

        s_shipButtonBindings.push_back(binding);
    }

    LoadButtonExpansionBindings(ini);
}

static void UpdateShipButtonBindings() {
    if (!ThrottleController::GetConfig().shipButtonsEnabled) {
        ReleaseAllShipButtonOutputs();
        return;
    }

    for (size_t i = 0; i < s_shipButtonBindings.size(); ++i) {
        auto& binding = s_shipButtonBindings[i];
        bool pressed = IsButtonPressedStatic(binding.buttonRef);
        
        const uint32_t ownerId = ShipOwnerIdForIndex(i);

        if (binding.mode == ShipBindingMode::Hold) {
            if (pressed != binding.previousPressed) {
                SetOutputHeld(binding.output, ownerId, pressed);
            }
        } else if (pressed && !binding.previousPressed) {
            PulseOutput(binding.output);
        }

        if (!pressed && binding.previousPressed) {
            ReleaseOwnerOutputs(ownerId);
        }

        binding.previousPressed = pressed;
    }
}

// ---- INI Config Loading ----
void ThrottleController::LoadConfig() {
    CSimpleIniA ini;
    ini.SetUnicode();

    const auto path = RuntimePaths::IniPath().string();

    if (ini.LoadFile(path.c_str()) == SI_OK) {
        CtrlLog("Loaded config from: " + path);
    } else {
        CtrlLog("No AbsoluteHOTAS.ini found, using defaults.");
    }

    s_config.enabled = ini.GetBoolValue("General", "bEnabled", true);
    
    s_config.throttleAxis = ParseBindingRef(ini.GetValue("Hardware", "iThrottleAxis", ""), 0x32);
    s_config.pitchAxis = ParseBindingRef(ini.GetValue("Hardware", "iPitchAxis", ""), 0x31);
    s_config.yawAxis = ParseBindingRef(ini.GetValue("Hardware", "iYawAxis", ""), 0x30);
    s_config.rollAxis = ParseBindingRef(ini.GetValue("Hardware", "iRollAxis", ""), 0x33);
    s_config.strafeLatAxis = ParseBindingRef(ini.GetValue("Hardware", "iStrafeLatAxis", ""), 0x33);
    s_config.strafeVertAxis = ParseBindingRef(ini.GetValue("Hardware", "iStrafeVertAxis", ""), 0x34);
    s_config.reverseAxis = ParseBindingRef(ini.GetValue("Hardware", "iReverseAxis", ""), 0x36);

    s_config.fPitchSensitivity = (float)ini.GetDoubleValue("Hardware", "fPitchSensitivity", 1.0);
    s_config.fYawSensitivity = (float)ini.GetDoubleValue("Hardware", "fYawSensitivity", 1.0);
    s_config.fRollSensitivity = (float)ini.GetDoubleValue("Hardware", "fRollSensitivity", 1.0);
    s_config.fStrafeSensitivity = (float)ini.GetDoubleValue("Hardware", "fStrafeSensitivity", 1.0);
    s_config.fReverseSensitivity = (float)ini.GetDoubleValue("Hardware", "fReverseSensitivity", 1.0);
    s_config.fThrottleSensitivity = (float)ini.GetDoubleValue("Hardware", "fThrottleSensitivity", 1.0);

    s_config.fThrottleSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fThrottleSaturation", 1.0), 0.05f, 1.0f);
    s_config.fPitchSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fPitchSaturation", 1.0), 0.05f, 1.0f);
    s_config.fYawSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fYawSaturation", 1.0), 0.05f, 1.0f);
    s_config.fRollSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fRollSaturation", 1.0), 0.05f, 1.0f);
    s_config.fStrafeSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeSaturation", 1.0), 0.05f, 1.0f);
    s_config.fStrafeVertSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeVertSaturation", 1.0), 0.05f, 1.0f);
    s_config.fReverseSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fReverseSaturation", 1.0), 0.05f, 1.0f);

    s_config.bInvertPitch = ini.GetBoolValue("Hardware", "bInvertPitch", true);
    s_config.bInvertThrottle = ini.GetBoolValue("Hardware", "bInvertThrottle", false);
    s_config.bInvertYaw = ini.GetBoolValue("Hardware", "bInvertYaw", false);
    s_config.bInvertRoll = ini.GetBoolValue("Hardware", "bInvertRoll", false);
    s_config.bInvertStrafeLat = ini.GetBoolValue("Hardware", "bInvertStrafeLat", false);
    s_config.bInvertStrafeVert = ini.GetBoolValue("Hardware", "bInvertStrafeVert", false);
    s_config.bInvertReverse = ini.GetBoolValue("Hardware", "bInvertReverse", false);

    // Per-axis deadzones
    s_config.fThrottleDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fThrottleDeadzone", 0.0), 0.0f, 0.5f);
    s_config.fPitchDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fPitchDeadzone", 0.0), 0.0f, 0.5f);
    s_config.fYawDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fYawDeadzone", 0.0), 0.0f, 0.5f);
    s_config.fRollDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fRollDeadzone", 0.0), 0.0f, 0.5f);
    s_config.fStrafeDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeDeadzone", 0.05), 0.0f, 0.5f);
    s_config.fStrafeVertDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeVertDeadzone", 0.05), 0.0f, 0.5f);

    s_config.activateButton = ParseBindingRef(ini.GetValue("Buttons", "iActivateButtonId", ""), -1);
    s_config.stopButton = ParseBindingRef(ini.GetValue("Buttons", "iStopButtonId", ""), -1);
    s_config.toggleWizardButton = ParseBindingRef(ini.GetValue("Buttons", "iToggleWizardButton", ""), -1);
    s_config.alwaysOn = ini.GetBoolValue("Buttons", "bAlwaysOn", true);
    
    s_config.detentCenter = ini.GetLongValue("Normalization", "iDetentCenter", 32768);
    s_config.detentDeadzone = ini.GetLongValue("Normalization", "iDetentDeadzone", 500);
    s_config.reverseEnabled = ini.GetBoolValue("Normalization", "bReverseEnabled", false);
    s_config.unipolarMode = ini.GetBoolValue("Normalization", "bUnipolarMode", true);
    s_config.bUnipolarReverse = ini.GetBoolValue("Normalization", "bUnipolarReverse", false);
    s_config.reverseZoneCenter = ini.GetLongValue("Normalization", "iReverseZoneCenter", 3000);
    s_config.reverseZoneDeadzone = ini.GetLongValue("Normalization", "iReverseZoneDeadzone", 3000);
    s_config.bBoostZone = ini.GetBoolValue("Normalization", "bBoostZone", false);
    s_config.boostZoneCenter = ini.GetLongValue("Normalization", "iBoostZoneCenter", 62000);
    s_config.boostZoneDeadzone = ini.GetLongValue("Normalization", "iBoostZoneDeadzone", 2000);
    s_config.idlePlateau = (float)ini.GetDoubleValue("Normalization", "fIdlePlateau", 0.05);
    s_config.reverseDeadzone = (float)ini.GetDoubleValue("Normalization", "fReverseDeadzone", 0.05);
    s_config.reverseActivationThreshold = (float)ini.GetDoubleValue("Normalization", "fReverseActivationThreshold", 0.05);
    s_config.reverseAxisEnabled = ini.GetBoolValue("Normalization", "bReverseAxisEnabled", true);
    
    s_config.pollRateHz = (int)ini.GetLongValue("Injection", "iPollRateHz", 120);
    s_config.throttleBurstMs = (int)ini.GetLongValue("Injection", "iThrottleBurstMs", 250);
    s_config.rollEnabled = ini.GetBoolValue("Injection", "bRollEnabled", true);
    s_config.logThrottle = ini.GetBoolValue("Injection", "bLogThrottle", false);
    g_verboseLog = s_config.logThrottle;
    s_config.bHoldForBoost = ini.GetBoolValue("Injection", "bHoldForBoost", true);

    s_config.digitalReverseButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalReverseButton", ""), -1);
    s_config.digitalRollLeftButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalRollLeftButton", ""), -1);
    s_config.digitalRollRightButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalRollRightButton", ""), -1);
    s_config.digitalStrafeLeftButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeLeftButton", ""), -1);
    s_config.digitalStrafeRightButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeRightButton", ""), -1);
    s_config.digitalStrafeUpButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeUpButton", ""), -1);
    s_config.digitalStrafeDownButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeDownButton", ""), -1);
    s_config.digitalRollValue = (float)ini.GetDoubleValue("DigitalAxes", "fDigitalRollValue", 1.0);
    s_config.digitalStrafeValue = (float)ini.GetDoubleValue("DigitalAxes", "fDigitalStrafeValue", 1.0);

    s_config.shipButtonsEnabled = ini.GetBoolValue("ShipButtons", "bShipButtonsEnabled", true);
    LoadShipButtonBindings(ini);

    // [Aim] - Experimental source-object reticle injection
    s_config.bSourceObjectAim  = ini.GetBoolValue("Aim", "bSourceObjectAim", false);
    s_config.fAimSensitivity   = (float)ini.GetDoubleValue("Aim", "fAimSensitivity", 1.0);
    s_config.aimYawAxis        = ParseBindingRef(ini.GetValue("Aim", "iAimYawAxis", nullptr), -1);
    s_config.aimPitchAxis      = ParseBindingRef(ini.GetValue("Aim", "iAimPitchAxis", nullptr), -1);
    s_config.fAimYawSensitivity   = (float)ini.GetDoubleValue("Aim", "fAimYawSensitivity", 1.0);
    s_config.fAimPitchSensitivity = (float)ini.GetDoubleValue("Aim", "fAimPitchSensitivity", 1.0);
    s_config.bInvertAimYaw     = ini.GetBoolValue("Aim", "bInvertAimYaw", false);
    s_config.bInvertAimPitch   = ini.GetBoolValue("Aim", "bInvertAimPitch", false);
    s_config.fAimSmoothing     = std::clamp((float)ini.GetDoubleValue("Aim", "fAimSmoothing", 0.0), 0.0f, 1.0f);
    s_config.bMirrorFlightToAim = ini.GetBoolValue("Aim", "bMirrorFlightToAim", true);
    s_config.digitalAimLeftButton  = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimLeftButton", nullptr), -1);
    s_config.digitalAimRightButton = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimRightButton", nullptr), -1);
    s_config.digitalAimUpButton    = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimUpButton", nullptr), -1);
    s_config.digitalAimDownButton  = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimDownButton", nullptr), -1);
    s_config.digitalAimCenterButton = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimCenterButton", nullptr), -1);
    s_config.fDigitalAimValue = (float)ini.GetDoubleValue("Aim", "fDigitalAimValue", 1.0);
    s_config.toggleAimModeButton = ParseBindingRef(ini.GetValue("Aim", "iToggleAimModeButton", nullptr), -1);

    // HOSAM (Hands On Stick And Mouse)
    s_config.bHOSAMMode = ini.GetBoolValue("Aim", "bHOSAMMode", false);
    s_config.bAlignmentAssist = ini.GetBoolValue("Aim", "bAlignmentAssist", false);
    s_config.fAlignmentRadius = std::clamp((float)ini.GetDoubleValue("Aim", "fAlignmentRadius", 15.0), 0.0f, 200.0f);
    s_config.iAlignmentIdleMs = std::clamp((int)ini.GetLongValue("Aim", "iAlignmentIdleMs", 80), 0, 2000);
    s_config.fAlignmentDecayRate = std::clamp((float)ini.GetDoubleValue("Aim", "fAlignmentDecayRate", 4.0), 0.1f, 50.0f);

    {
        char aimMsg[512];
        snprintf(aimMsg, sizeof(aimMsg),
            "[Aim] bSourceObjectAim=%s fAimSensitivity=%.2f aimYaw=%d aimPitch=%d mirror=%s HOSAM=%s align=%s",
            s_config.bSourceObjectAim ? "true" : "false",
            s_config.fAimSensitivity,
            s_config.aimYawAxis.value,
            s_config.aimPitchAxis.value,
            s_config.bMirrorFlightToAim ? "true" : "false",
            s_config.bHOSAMMode ? "true" : "false",
            s_config.bAlignmentAssist ? "true" : "false");
        RuntimePaths::AppendLogAlways("[Controller]", aimMsg);
    }


    // Load per-axis calibration overrides from [Calibration]
    s_config.axisCalibration.clear();
    CSimpleIniA::TNamesDepend calibKeys;
    ini.GetAllKeys("Calibration", calibKeys);
    for (const auto& entry : calibKeys) {
        const char* key = entry.pItem;
        // Expected format: iCalib_<devIdx>_0x<usage>
        if (strncmp(key, "iCalib_", 7) != 0) continue;
        int devIdx = -1, usage = -1;
        if (sscanf_s(key, "iCalib_%d_0x%x", &devIdx, &usage) == 2 && devIdx >= 0 && usage >= 0) {
            const char* val = ini.GetValue("Calibration", key, "");
            long cmin = 0, cmax = 65535;
            if (sscanf_s(val, "%ld,%ld", &cmin, &cmax) == 2 && cmin < cmax) {
                int calibKey = (devIdx << 8) | usage;
                s_config.axisCalibration[calibKey] = { cmin, cmax };
                char logBuf[128];
                sprintf_s(logBuf, "Calibration loaded: dev=%d axis=0x%02X range=[%ld, %ld]", devIdx, usage, cmin, cmax);
                CtrlLog(logBuf);
            }
        }
    }

    // [DualStick] — Self-centering throttle accumulator mode
    s_config.bAccumulatorThrottle = ini.GetBoolValue("DualStick", "bAccumulatorThrottle", false);
    s_config.fAccumulatorRate = std::clamp((float)ini.GetDoubleValue("DualStick", "fAccumulatorRate", 1.0), 0.1f, 10.0f);
    s_config.fAccumulatorDecay = std::clamp((float)ini.GetDoubleValue("DualStick", "fAccumulatorDecay", 0.0), 0.0f, 20.0f);
    s_config.fReverseGateVelocity = std::clamp((float)ini.GetDoubleValue("DualStick", "fReverseGateVelocity", 5.0), 0.0f, 100.0f);
    if (s_config.bAccumulatorThrottle) {
        char dsBuf[256];
        snprintf(dsBuf, sizeof(dsBuf),
            "[DualStick] Accumulator mode ON: rate=%.2f decay=%.2f reverseGate=%.1f m/s",
            s_config.fAccumulatorRate, s_config.fAccumulatorDecay, s_config.fReverseGateVelocity);
        RuntimePaths::AppendLogAlways("[Controller]", dsBuf);
    }

    CtrlLog("Config Loaded - AbsoluteHOTAS 6DOF Dashboard Initialized.");
}

// ---- Axis Normalization ----
float ThrottleController::NormalizeAxis(long rawValue, long axisMin, long axisMax) {
    long center = s_config.detentCenter;
    long deadzone = s_config.detentDeadzone;

    if (rawValue < axisMin) rawValue = axisMin;
    if (rawValue > axisMax) rawValue = axisMax;
    if (s_config.bInvertThrottle) {
        rawValue = axisMin + axisMax - rawValue;
    }

    if (s_config.unipolarMode) {
        // Multi-zone normalization: reverseZone + center(50%) + boostZone
        if (s_config.bUnipolarReverse) {
            long rzCenter = s_config.reverseZoneCenter;
            long rzDz = s_config.reverseZoneDeadzone;

            // Zone 1: Reverse — below reverse zone (throttle = 0, S-key handled in control loop)
            if (rawValue < rzCenter - rzDz) return 0.0f;

            // Zone 2: Dead stop — within reverse zone deadzone (zero velocity)
            if (rawValue <= rzCenter + rzDz) return 0.0f;

            // Above reverse zone: forward territory using center as 50% cruise point
            long fwdStart = rzCenter + rzDz;

            // Zone 3: 0% → 50% ramp — from top of dead stop to bottom of center deadzone
            if (rawValue < center - deadzone) {
                float range = (float)((center - deadzone) - fwdStart);
                if (range <= 0.0f) return 0.0f;
                return 0.5f * (float)(rawValue - fwdStart) / range;
            }

            // Zone 4: 50% cruise plateau — within center deadzone
            if (rawValue <= center + deadzone) return 0.5f;

            // Above center: 50% → 100% territory
            long rampStart = center + deadzone;

            if (s_config.bBoostZone) {
                long bzCenter = s_config.boostZoneCenter;
                long bzDz = s_config.boostZoneDeadzone;

                // Zone 5: 50% → 100% ramp — from top of center dz to bottom of boost zone
                if (rawValue < bzCenter - bzDz) {
                    float range = (float)((bzCenter - bzDz) - rampStart);
                    if (range <= 0.0f) return 1.0f;
                    return 0.5f + 0.5f * (float)(rawValue - rampStart) / range;
                }

                // Zone 6 & 7: 100% plateau + boost zone (boost trigger handled in control loop)
                return 1.0f;
            }

            // No boost zone: 50% → 100% ramp to axisMax with saturation
            float range = (float)(axisMax - rampStart);
            if (range <= 0.0f) return 1.0f;
            float norm = 0.5f + 0.5f * (float)(rawValue - rampStart) / range;
            return std::clamp(norm / s_config.fThrottleSaturation, 0.0f, 1.0f);
        }

        float range = (float)(axisMax - axisMin);
        float norm = (range <= 0.0f) ? 0.0f : (float)(rawValue - axisMin) / range;
        
        // PLATEAU LOGIC: Flatten the bottom 5% (tunable)
        if (norm < s_config.idlePlateau) return 0.0f;
        
        // Scale the remaining range [idlePlateau, 1.0] -> [0.0, 1.0]
        float result = (norm - s_config.idlePlateau) / (1.0f - s_config.idlePlateau);

        // Apply throttle saturation: rescale so full output is reached at saturation % of deflection
        return std::clamp(result / s_config.fThrottleSaturation, 0.0f, 1.0f);
    }

    if (rawValue >= center - deadzone && rawValue <= center + deadzone) return 0.0f;

    if (rawValue > center + deadzone) {
        float range = (float)(axisMax - (center + deadzone));
        return (range <= 0.0f) ? 0.0f : (float)(rawValue - (center + deadzone)) / range;
    } else {
        if (!s_config.reverseEnabled) return 0.0f;
        float range = (float)((center - deadzone) - axisMin);
        return (range <= 0.0f) ? 0.0f : -1.0f + (float)(rawValue - axisMin) / range;
    }
}

// Check if the FireBoosters ship action output is currently being held.
// Starfield cancels boost on throttle reduction — the accumulator must
// suppress writes during boost to prevent jitter-induced cancellation.
static bool IsBoostOutputHeld() {
    if (s_shipButtonBindings.empty()) return false;
    // FireBoosters is index 0 in the ship button bindings array
    const auto& boostBinding = s_shipButtonBindings[0];
    for (const auto& held : s_heldShipOutputs) {
        if (SameOutput(held.output, boostBinding.output)) return true;
    }
    return false;
}

// ---- Safe Memory Access ----
#pragma warning(push)
#pragma warning(disable: 4733)
static void SafeInjectThrottle(uintptr_t baseAddr, float throttle) {
    if (!baseAddr) return;
    __try {
        *(float*)(baseAddr + 0x68) = throttle;
        // +0x6C is the game's effective throttle (after turn-rate penalties).
        // Only write it at or below the penalty floor (0.50) so feathering
        // works during rotation. Above 0.50 the game manages +0x6C.
        if (throttle <= 0.50f)
            *(float*)(baseAddr + 0x6C) = throttle;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool IsThrottlePlausible(uintptr_t basePtr) {
    __try {
        float val = *(volatile float*)(basePtr + 0x68);
        return std::isfinite(val);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float SafeReadThrottle(uintptr_t basePtr) {
    __try { return *(volatile float*)(basePtr + 0x68); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -999.0f; }
}

static float SafeReadFloat(uintptr_t addr) {
    __try { return *(volatile float*)(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -999.0f; }
}

// Read ship velocity from the validated flight control cluster (+0x70).
// Lives inside the same struct discovered by SignalHunter — no separate
// pointer resolution needed. Returns -1.0f if cluster not yet locked.
static float ReadClusterVelocity(uintptr_t clusterBase) {
    if (!clusterBase) return -1.0f;
    __try {
        return *(volatile float*)(clusterBase + 0x70);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1.0f;
    }
}


#pragma warning(pop)

// ---- Global Discovery State ----
static int g_candidateScores[2048] = { 0 };
static int g_candidateAges[2048] = { 0 };
static uintptr_t g_lastFrameCandidates[2048] = { 0 };
static float g_lastFrameValues[2048] = { 0.0f };

static const int SCORE_THRESHOLD = 250; 
static bool g_discoveryLocked = false;
static bool g_discoveryArmed = false;
static int  s_activeCandidateIndex = -1;
static uintptr_t s_activeThrottlePtr = 0;
static int s_plausibilityFailCount = 0;
static bool s_lastLogLockedState = false;
static bool s_reacquireWatchdogEnabled = false;
static float s_lastInjectedThrottle = -999.0f;
static int s_handoverGraceFrames = 0;
static int s_throttleBurstFrames = 0;
static float s_throttleBurstValue = 0.0f;
static float s_accumulatorThrottle = 0.0f;  // Accumulated throttle target for dual-stick mode
static int   s_accumBurstFrames = 0;        // Delta burst frames remaining for accumulator writes
static float s_lastAccumBurstValue = -999.0f; // Last accumulator value that triggered a burst
static bool  s_prevBoostHeld = false;         // Edge detection for boost release transition
static int   s_boostCancelFrames = 0;         // Frames remaining for reverse-hold boost cancel

static void DisarmFlightControlState() {
    g_discoveryArmed = false;
    g_discoveryLocked = false;
    s_activeCandidateIndex = -1;
    s_activeThrottlePtr = 0;
    s_plausibilityFailCount = 0;
    s_reacquireWatchdogEnabled = false;
    s_lastInjectedThrottle = -999.0f;
    s_handoverGraceFrames = 0;
    s_throttleBurstFrames = 0;
    s_throttleBurstValue = 0.0f;
    s_accumulatorThrottle = 0.0f;
    s_accumBurstFrames = 0;
    s_lastAccumBurstValue = -999.0f;
    s_prevBoostHeld = false;
    s_boostCancelFrames = 0;
    ReleaseAllShipButtonOutputs();
    ThrottleHook::SetReverseOverride(false);
    ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
    ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, false);
    ThrottleHook::SetSilenceEnabled(false);
    ThrottleHook::SetCaptureEnabled(false);
    ThrottleHook::ClearCandidates();

}

static void ArmDiscoveryForReacquire(const char* reason) {
    if (reason) {
        CtrlLog(std::string("[SignalHunter] Re-arming discovery: ") + reason);
    }
    g_discoveryArmed = true;
    g_discoveryLocked = false;
    s_activeCandidateIndex = -1;
    s_activeThrottlePtr = 0;
    s_plausibilityFailCount = 0;
    s_throttleBurstFrames = 0;
    ThrottleHook::SetReverseOverride(false);
    ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
    ThrottleHook::SetSilenceEnabled(false);
    ThrottleHook::ClearCandidates();
    ThrottleHook::SetCaptureEnabled(true);
}


// SEH-guarded float write — needed because ControlLoop() uses C++ objects
// that prevent inline __try/__except.
static void SafeWriteFloat2(uintptr_t addr, float value) {
    __try {
        *(volatile float*)addr = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void ThrottleController::ControlLoop() {
    CtrlLog("=== DirectInput Polling Loop Starting ===");

    if (!DeviceManager::Initialize()) {
        CtrlLog("Failed to initialize DeviceManager!");
        return;
    }

    DeviceManager::LogDeviceManifest();

    // 1. Resolve and open all referenced devices
    auto ResolveAndOpen = [](BindingRef& ref) {
        if (!ref.IsValid()) return;
        
        int resolvedIndex = -1;
        if (ref.HasIndex()) {
            // Pre-resolved by #N@ syntax — validate index exists
            if (ref.deviceIndex < DeviceManager::GetDeviceCount()) {
                resolvedIndex = ref.deviceIndex;
            } else {
                char buf[256];
                sprintf_s(buf, "Warning: Device index #%d out of range (only %d devices)", ref.deviceIndex, DeviceManager::GetDeviceCount());
                CtrlLog(buf);
            }
        } else if (ref.HasDevice()) {
            resolvedIndex = DeviceManager::ResolveByName(ref.deviceName);
        } else {
            // If unbound device name, we can try to fall back to device 0 if it exists
            resolvedIndex = DeviceManager::GetDeviceCount() > 0 ? 0 : -1;
        }
        
        if (resolvedIndex >= 0) {
            ref.deviceIndex = resolvedIndex;
            DeviceManager::OpenDevice(resolvedIndex);
        } else {
            char buf[256];
            sprintf_s(buf, "Warning: Could not resolve device for binding: %s", ref.HasDevice() ? ref.deviceName.c_str() : "default fallback");
            CtrlLog(buf);
        }
    };

    ResolveAndOpen(s_config.throttleAxis);
    ResolveAndOpen(s_config.pitchAxis);
    ResolveAndOpen(s_config.yawAxis);
    ResolveAndOpen(s_config.rollAxis);
    ResolveAndOpen(s_config.strafeLatAxis);
    ResolveAndOpen(s_config.strafeVertAxis);
    ResolveAndOpen(s_config.reverseAxis);
    ResolveAndOpen(s_config.aimYawAxis);
    ResolveAndOpen(s_config.aimPitchAxis);

    ResolveAndOpen(s_config.activateButton);
    ResolveAndOpen(s_config.stopButton);
    ResolveAndOpen(s_config.toggleWizardButton);

    ResolveAndOpen(s_config.digitalReverseButton);
    ResolveAndOpen(s_config.digitalRollLeftButton);
    ResolveAndOpen(s_config.digitalRollRightButton);
    ResolveAndOpen(s_config.digitalStrafeLeftButton);
    ResolveAndOpen(s_config.digitalStrafeRightButton);
    ResolveAndOpen(s_config.digitalStrafeUpButton);
    ResolveAndOpen(s_config.digitalStrafeDownButton);

    ResolveAndOpen(s_config.digitalAimLeftButton);
    ResolveAndOpen(s_config.digitalAimRightButton);
    ResolveAndOpen(s_config.digitalAimUpButton);
    ResolveAndOpen(s_config.digitalAimDownButton);
    ResolveAndOpen(s_config.digitalAimCenterButton);
    ResolveAndOpen(s_config.toggleAimModeButton);

    for (auto& sb : s_shipButtonBindings) {
        ResolveAndOpen(sb.buttonRef);
    }

    // Detect Range for the primary Throttle axis
    long axisMin = 0, axisMax = 65535;
    if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.deviceIndex >= 0) {
        LPDIRECTINPUTDEVICE8 tDev = DeviceManager::OpenDevice(s_config.throttleAxis.deviceIndex);
        if (tDev) {
            DIPROPRANGE dipr;
            dipr.diph.dwSize = sizeof(DIPROPRANGE);
            dipr.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            dipr.diph.dwHow = DIPH_BYUSAGE;
            dipr.diph.dwObj = s_config.throttleAxis.value;

            HRESULT hr = tDev->GetProperty(DIPROP_RANGE, &dipr.diph);
            if (FAILED(hr)) {
                // Fallback: Try Axis ID directly (might be DI index instead of usage)
                dipr.diph.dwHow = DIPH_BYID;
                dipr.diph.dwObj = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0); // Try first axis
                hr = tDev->GetProperty(DIPROP_RANGE, &dipr.diph);
            }

            if (SUCCEEDED(hr)) {
                axisMin = dipr.lMin;
                axisMax = dipr.lMax;
                char rangeBuf[128];
                sprintf_s(rangeBuf, "Hardware Range Detected: [%ld, %ld]", axisMin, axisMax);
                CtrlLog(rangeBuf);
            } else {
                CtrlLog("Warning: Could not detect hardware range. Using default 0-65535.");
            }
        }
    }

    // Override with calibration data if present
    if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.deviceIndex >= 0) {
        int calibKey = (s_config.throttleAxis.deviceIndex << 8) | s_config.throttleAxis.value;
        auto it = s_config.axisCalibration.find(calibKey);
        if (it != s_config.axisCalibration.end()) {
            axisMin = it->second.first;
            axisMax = it->second.second;
            char calibBuf[128];
            sprintf_s(calibBuf, "Throttle using CALIBRATED range: [%ld, %ld]", axisMin, axisMax);
            CtrlLog(calibBuf);
        }
    }

    auto sleepDuration = std::chrono::milliseconds(1000 / s_config.pollRateHz);
    uint64_t iter = 0;
    float lastInjectedHardwareValue = -999.0f;
    int candCount = 0;
    bool wasPiloting = false;
    auto lastLoopTime = std::chrono::steady_clock::now();

    while (s_running) {
        iter++;

        // Compute actual delta time for frame-rate-independent logic
        auto nowTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(nowTime - lastLoopTime).count();
        dt = std::clamp(dt, 0.0001f, 0.1f); // Clamp to [0.1ms, 100ms] to survive hitches
        lastLoopTime = nowTime;

        // Hot-reload: if BindingWizard (or anything) requested a config reload,
        // re-parse the INI and re-resolve/re-open all devices on the loop thread.
        if (s_configReloadRequested.exchange(false)) {
            CtrlLog("=== CONFIG HOT-RELOAD REQUESTED ===");
            LoadConfig();

            // Re-resolve and re-open all devices for the new bindings
            ResolveAndOpen(s_config.throttleAxis);
            ResolveAndOpen(s_config.pitchAxis);
            ResolveAndOpen(s_config.yawAxis);
            ResolveAndOpen(s_config.rollAxis);
            ResolveAndOpen(s_config.strafeLatAxis);
            ResolveAndOpen(s_config.strafeVertAxis);
            ResolveAndOpen(s_config.reverseAxis);
            ResolveAndOpen(s_config.aimYawAxis);
            ResolveAndOpen(s_config.aimPitchAxis);

            ResolveAndOpen(s_config.activateButton);
            ResolveAndOpen(s_config.stopButton);
            ResolveAndOpen(s_config.toggleWizardButton);

            ResolveAndOpen(s_config.digitalReverseButton);
            ResolveAndOpen(s_config.digitalRollLeftButton);
            ResolveAndOpen(s_config.digitalRollRightButton);
            ResolveAndOpen(s_config.digitalStrafeLeftButton);
            ResolveAndOpen(s_config.digitalStrafeRightButton);
            ResolveAndOpen(s_config.digitalStrafeUpButton);
            ResolveAndOpen(s_config.digitalStrafeDownButton);

            ResolveAndOpen(s_config.digitalAimLeftButton);
            ResolveAndOpen(s_config.digitalAimRightButton);
            ResolveAndOpen(s_config.digitalAimUpButton);
            ResolveAndOpen(s_config.digitalAimDownButton);
            ResolveAndOpen(s_config.digitalAimCenterButton);
            ResolveAndOpen(s_config.toggleAimModeButton);

            for (auto& sb : s_shipButtonBindings) {
                ResolveAndOpen(sb.buttonRef);
            }

            sleepDuration = std::chrono::milliseconds(1000 / s_config.pollRateHz);
            CtrlLog("=== CONFIG HOT-RELOAD COMPLETE ===");
        }

        // Batch poll all tracked devices
        DeviceManager::PollAll();

        // Suppress ship button outputs while wizard overlay is open to prevent
        // noisy buttons from feeding SendInput events back into ImGui keyboard nav.
        if (!UIHook::IsUIOpen()) {
            UpdateShipButtonBindings();
        } else {
            ReleaseAllShipButtonOutputs();
        }

        const bool isPiloting = AbsoluteGlobals::g_isPilotState.load(std::memory_order_acquire);
        if (!isPiloting) {
            if (wasPiloting) {
                CtrlLog("[PilotState] Pilot seat exited; injection disarmed.");
            }
            DisarmFlightControlState();
            lastInjectedHardwareValue = -999.0f;
            wasPiloting = false;
            std::this_thread::sleep_for(sleepDuration);
            continue;
        }

        if (!wasPiloting) {
            DisarmFlightControlState();
            lastInjectedHardwareValue = -999.0f;
            if (s_config.alwaysOn) {
                CtrlLog("[PilotState] Standalone mode active; discovery armed automatically.");
                s_reacquireWatchdogEnabled = true;
                g_discoveryArmed = true;
                ThrottleHook::SetCaptureEnabled(true);
            } else {
                CtrlLog("[PilotState] Standalone mode active; waiting for activate button.");
            }
            wasPiloting = true;
        }

        auto IsButtonPressed = [](const BindingRef& ref) -> bool {
            return IsButtonPressedStatic(ref);
        };

        bool curActivate = IsButtonPressed(s_config.activateButton);
        bool curStop = IsButtonPressed(s_config.stopButton);
        bool curToggleWizard = IsButtonPressed(s_config.toggleWizardButton);
        static bool prevActivate = false;
        static bool prevStop = false;
        static bool prevToggleWizard = false;

        bool curFailsafeReset = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) &&
            ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) &&
            ((GetAsyncKeyState(VK_F8) & 0x8000) != 0);
        static bool prevFailsafeReset = false;

        // HOTAS Activate / keyboard failsafe reset.
        if ((curActivate && !prevActivate) || (curFailsafeReset && !prevFailsafeReset)) {
             CtrlLog("==================================================");
             CtrlLog("[SignalHunter] MANUAL TRIGGER: activate/failsafe reset pressed. Resetting hooks.");
             CtrlLog("==================================================");
             s_reacquireWatchdogEnabled = true;
             g_discoveryArmed = true; 
             g_discoveryLocked = false;
             s_activeThrottlePtr = 0;
             s_lastInjectedThrottle = -999.0f; // Reset delta guard
             lastInjectedHardwareValue = -999.0f;
             s_throttleBurstFrames = 0;
             ReleaseAllShipButtonOutputs();
             ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
             ThrottleHook::ClearCandidates();
             ThrottleHook::SetCaptureEnabled(true);
             ThrottleHook::SetSilenceEnabled(false); 
        }
        
        // HOTAS Stop (Force Disarm)
        if (curStop && !prevStop) {
             CtrlLog("==================================================");
             CtrlLog("[SignalHunter] MANUAL STOP: HOTAS Stop pressed. Disarming hooks.");
             CtrlLog("==================================================");
             s_reacquireWatchdogEnabled = false;
             g_discoveryArmed = false; 
             g_discoveryLocked = false;
             s_activeThrottlePtr = 0;
             s_lastInjectedThrottle = -999.0f; // Reset delta guard
             lastInjectedHardwareValue = -999.0f;
             s_throttleBurstFrames = 0;
             ReleaseAllShipButtonOutputs();
             ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
             ThrottleHook::ClearCandidates();
             ThrottleHook::SetSilenceEnabled(false);
             ThrottleHook::SetCaptureEnabled(true); // Back to passive mode waiting for 0.0314
        }
        
        // Toggle Wizard overlay from HOTAS button
        if (curToggleWizard && !prevToggleWizard) {
            UIHook::ToggleUI();
        }
        
        prevFailsafeReset = curFailsafeReset;
        prevActivate = curActivate;
        prevStop = curStop;
        prevToggleWizard = curToggleWizard;

        auto GetRawAxis = [&](const BindingRef& ref) -> long {
            if (ref.value > 0) {
                const DIJOYSTATE2* st = DeviceManager::GetCachedState(ref.deviceIndex);
                if (st) {
                    switch (ref.value) {
                        case 0x30: return st->lX;
                        case 0x31: return st->lY;
                        case 0x32: return st->lZ;
                        case 0x33: return st->lRx;
                        case 0x34: return st->lRy;
                        case 0x35: return st->lRz;
                        case 0x36: return st->rglSlider[0];
                        case 0x37: return st->rglSlider[1];
                    }
                }
            }
            return 32768; // Neutral fallback
        };

        const bool digitalReverseBound = s_config.digitalReverseButton.IsValid() && s_config.digitalReverseButton.value > 0 && s_config.digitalReverseButton.value <= 128;
        const bool digitalReverseHeld = digitalReverseBound && IsButtonPressed(s_config.digitalReverseButton);

        auto ApplyUnipolarDeadzone = [](float value, float deadzone) {
            const float dz = std::clamp(deadzone, 0.0f, 0.95f);
            if (value <= dz) return 0.0f;
            return (value - dz) / (1.0f - dz);
        };

        auto NormalizeReverseInput = [&]() {
            float value = 0.0f;
            if (s_config.reverseAxisEnabled && !digitalReverseBound
                && s_config.reverseAxis.IsValid() && s_config.reverseAxis.value > 0) {
                value = ((float)GetRawAxis(s_config.reverseAxis)) / 65535.0f;
                value = s_config.bInvertReverse ? (1.0f - value) : value;
                value = std::clamp(value * s_config.fReverseSensitivity, 0.0f, 1.0f);
                value = ApplyUnipolarDeadzone(value, s_config.reverseDeadzone);
                value = std::clamp(value / s_config.fReverseSaturation, 0.0f, 1.0f);
            }

            return std::clamp(value, 0.0f, 1.0f);
        };

        float reverseAxis = NormalizeReverseInput();
        const bool reverseAxisHeld = reverseAxis > s_config.reverseActivationThreshold;
        bool reverseHeld = digitalReverseHeld || reverseAxisHeld;

        // Unipolar reverse zone: derive reverse from main throttle axis when below reverseZone-dz
        if (s_config.bUnipolarReverse && s_config.unipolarMode
            && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            long rawThrottle = GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            long rzCenter = s_config.reverseZoneCenter;
            long rzDeadzone = s_config.reverseZoneDeadzone;
            if (rawThrottle < rzCenter - rzDeadzone) {
                reverseHeld = true;
            }
        }

        // Reverse is now handled via direct-memory override in the injection branch below.
        // No S-key emulation needed — SetReverseOverride() drives +0x5C, +0x3C, and +0x68 directly.

        // Boost zone: trigger FireBoosters output when throttle axis is above boostZone+dz
        // Uses the same SetOutputHeld system so IsBoostOutputHeld() detects it and
        // the existing boost guard (throttle write suppression + release cancellation) works.
        if (s_config.bBoostZone && s_config.bUnipolarReverse && s_config.unipolarMode
            && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0
            && !s_shipButtonBindings.empty()) {
            long rawThrottle = GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            long bzCenter = s_config.boostZoneCenter;
            long bzDz = s_config.boostZoneDeadzone;
            bool inBoostZone = rawThrottle > bzCenter + bzDz;
            // FireBoosters is index 0 in ship button bindings
            SetOutputHeld(s_shipButtonBindings[0].output, OwnerBoostZone, inBoostZone);
        }

        // 1. POLL ALL HARDWARE AXES
        auto NormalizeBipolarRate = [&](long rawValue) -> float {
            long center = s_config.detentCenter;
            long deadzone = s_config.detentDeadzone;

            if (rawValue < axisMin) rawValue = axisMin;
            if (rawValue > axisMax) rawValue = axisMax;
            if (s_config.bInvertThrottle) {
                rawValue = axisMin + axisMax - rawValue;
            }

            if (rawValue >= center - deadzone && rawValue <= center + deadzone) return 0.0f;

            if (rawValue > center + deadzone) {
                float range = (float)(axisMax - (center + deadzone));
                return (range <= 0.0f) ? 0.0f : (float)(rawValue - (center + deadzone)) / range;
            } else {
                float range = (float)((center - deadzone) - axisMin);
                return (range <= 0.0f) ? 0.0f : -1.0f + (float)(rawValue - axisMin) / range;
            }
        };

        float throttle = 0.0f;
        if (s_config.bAccumulatorThrottle) {
            // Accumulator mode: use bipolar normalization with detent center/deadzone
            if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
                throttle = NormalizeBipolarRate(GetRawAxis(s_config.throttleAxis));
                throttle = std::clamp(throttle * s_config.fThrottleSensitivity, -1.0f, 1.0f);
            }
        } else if (reverseHeld) {
            if (s_activeThrottlePtr && IsThrottlePlausible(s_activeThrottlePtr)) {
                throttle = SafeReadThrottle(s_activeThrottlePtr);
            } else {
                throttle = 0.0f;
            }

        } else if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            throttle = NormalizeAxis(GetRawAxis(s_config.throttleAxis), axisMin, axisMax);
            throttle = std::clamp(throttle * s_config.fThrottleSensitivity, 0.0f, 1.0f);
        }
        // else: throttle axis unbound — leave throttle at 0 (no injection)
        
        auto NormBipolar = [&](const BindingRef& ref, float sens, bool invert, float saturation = 1.0f) {
            if (!ref.IsValid() || ref.value <= 0) return 0.0f; // Unbound = neutral
            float raw = (float)GetRawAxis(ref);
            float aMin = 0.0f, aMax = 65535.0f;

            // Use calibrated range if available, otherwise defaults
            if (ref.deviceIndex >= 0) {
                int calibKey = (ref.deviceIndex << 8) | ref.value;
                auto it = s_config.axisCalibration.find(calibKey);
                if (it != s_config.axisCalibration.end()) {
                    aMin = (float)it->second.first;
                    aMax = (float)it->second.second;
                }
            }

            float center = (aMin + aMax) / 2.0f;
            float halfRange = (aMax - aMin) / 2.0f;
            if (halfRange <= 0.0f) return 0.0f;
            float val = (raw - center) / halfRange;
            val *= sens;
            val = invert ? -val : val;
            float sat = std::clamp(saturation, 0.05f, 1.0f);
            return std::clamp(val / sat, -1.0f, 1.0f);
        };

        auto ApplyDeadzone = [](float value, float deadzone) {
            const float dz = std::clamp(deadzone, 0.0f, 0.95f);
            if (std::abs(value) <= dz) return 0.0f;
            const float sign = value < 0.0f ? -1.0f : 1.0f;
            return sign * ((std::abs(value) - dz) / (1.0f - dz));
        };

        float pitch = ApplyDeadzone(NormBipolar(s_config.pitchAxis, s_config.fPitchSensitivity, s_config.bInvertPitch, s_config.fPitchSaturation), s_config.fPitchDeadzone);
        float yaw   = ApplyDeadzone(NormBipolar(s_config.yawAxis,   s_config.fYawSensitivity,   s_config.bInvertYaw,   s_config.fYawSaturation), s_config.fYawDeadzone);
        float roll  = ApplyDeadzone(NormBipolar(s_config.rollAxis,  s_config.fRollSensitivity,  s_config.bInvertRoll,  s_config.fRollSaturation), s_config.fRollDeadzone);
        if (IsButtonPressed(s_config.digitalRollLeftButton)) roll -= s_config.digitalRollValue;
        if (IsButtonPressed(s_config.digitalRollRightButton)) roll += s_config.digitalRollValue;
        roll = std::clamp(roll, -1.0f, 1.0f);

        float strafeX = ApplyDeadzone(NormBipolar(s_config.strafeLatAxis, s_config.fStrafeSensitivity, s_config.bInvertStrafeLat, s_config.fStrafeSaturation), s_config.fStrafeDeadzone);
        float strafeY = ApplyDeadzone(NormBipolar(s_config.strafeVertAxis, s_config.fStrafeSensitivity, s_config.bInvertStrafeVert, s_config.fStrafeVertSaturation), s_config.fStrafeVertDeadzone);
        if (IsButtonPressed(s_config.digitalStrafeLeftButton)) strafeX -= s_config.digitalStrafeValue;
        if (IsButtonPressed(s_config.digitalStrafeRightButton)) strafeX += s_config.digitalStrafeValue;
        if (IsButtonPressed(s_config.digitalStrafeUpButton)) strafeY += s_config.digitalStrafeValue;
        if (IsButtonPressed(s_config.digitalStrafeDownButton)) strafeY -= s_config.digitalStrafeValue;
        strafeX = std::clamp(strafeX, -1.0f, 1.0f);
        strafeY = std::clamp(strafeY, -1.0f, 1.0f);

        const bool strafeModifierHeld = std::abs(strafeX) > 0.05f || std::abs(strafeY) > 0.05f;
        SetOutputHeld(SpaceOutput, OwnerStrafeModifier, strafeModifierHeld);

        s_currentThrottle.store(throttle);

        candCount = ThrottleHook::GetCandidateCount();

        // SIGNAL HUNTER: Magic Number Pulse detection
        if (g_discoveryArmed && !g_discoveryLocked && candCount > 0) {
            // Periodic clear to flush noise if we haven't locked in 5 seconds
            if (iter % (s_config.pollRateHz * 5) == 0) {
                ThrottleHook::ClearCandidates();
                CtrlLog("[SignalHunter] Periodic buffer flush (Noise reduction).");
            }

            for (int i = 0; i < candCount && i < 2048; i++) {
                uintptr_t cand = ThrottleHook::GetCandidate(i);
                if (!cand) continue;

                float memVal = SafeReadThrottle(cand);

                // AGE FILTER: Track frames to evaluate stability
                if (cand == g_lastFrameCandidates[i]) {
                    g_candidateAges[i]++;
                } else {
                    g_candidateAges[i] = 0;
                    g_lastFrameCandidates[i] = cand;
                    g_lastFrameValues[i] = memVal;
                }

                // MAGIC NUMBER LOCK: Auto-Activate if 0.0314f is broadcast.
                if (std::abs(memVal - 0.0314f) < 0.0001f) {
                    CtrlLog("**************************************************");
                    CtrlLog("[SignalHunter] MAGIC NUMBER DETECTED! (0.0314f)");
                    char winBuf[128]; sprintf_s(winBuf, "[SignalHunter] Winner: Candidate #%d at 0x%llX", i, (unsigned long long)cand);
                    CtrlLog(winBuf);
                    CtrlLog("**************************************************");
                    s_activeCandidateIndex = i;
                    s_activeThrottlePtr = cand;
                    g_discoveryLocked = true;
                    s_reacquireWatchdogEnabled = true;
                    s_plausibilityFailCount = 0;
                    ThrottleHook::SetCaptureEnabled(false);
                    ThrottleHook::SetSilenceEnabled(false);
                    break;
                }

                // MANUAL CORRELATION LOCK: if the signpost fails, activation arms pure stability search.
                if (g_candidateAges[i] >= 15 && memVal >= -2.1f && memVal <= 2.1f) {
                    CtrlLog("**************************************************");
                    CtrlLog("[SignalHunter] STABLE SIGNAL DETECTED! Locking (Manual Fallback).");
                    char winBuf[128]; sprintf_s(winBuf, "[SignalHunter] Winner: Candidate #%d at 0x%llX", i, (unsigned long long)cand);
                    CtrlLog(winBuf);
                    CtrlLog("**************************************************");
                    s_activeCandidateIndex = i;
                    s_activeThrottlePtr = cand;
                    g_discoveryLocked = true;
                    s_reacquireWatchdogEnabled = true;
                    g_discoveryArmed = false; // Disarm since it was manual
                    s_plausibilityFailCount = 0;
                    ThrottleHook::SetCaptureEnabled(false);
                    ThrottleHook::SetSilenceEnabled(false);
                    break;
                }
            }
        }

        if (s_reacquireWatchdogEnabled) {
            if (g_discoveryLocked && !s_activeThrottlePtr) {
                ArmDiscoveryForReacquire("locked state had no active pointer");
            } else if (!g_discoveryLocked && !g_discoveryArmed) {
                ArmDiscoveryForReacquire("inactive state after prior lock");
            }
        }

        // Injection & Hysteresis
        if (s_activeThrottlePtr) {
            if (IsThrottlePlausible(s_activeThrottlePtr)) {
                s_plausibilityFailCount = 0;

                // HEURISTICS: Stand down if game logic should take priority
                float gameTarget = SafeReadThrottle(s_activeThrottlePtr);
                constexpr float kThrottleDeltaAuthority = 0.015f;
                int throttleBurstFrameCount = (s_config.throttleBurstMs <= 0)
                    ? 1
                    : std::max(1, (s_config.pollRateHz * s_config.throttleBurstMs) / 1000);

                // Roll shares the +0x58 writer with lateral strafe. Own that slot only
                // while roll or plugin-owned lateral strafe is displaced.
                bool rollOverrideActive = s_config.rollEnabled && (std::abs(roll) > 0.05f);
                bool strafeLatOverrideActive = std::abs(strafeX) > 0.05f;
                bool strafeVertOverrideActive = std::abs(strafeY) > 0.05f;
                float lateral = strafeLatOverrideActive ? strafeX : roll;
                bool sourceAimActive = s_config.bSourceObjectAim && ThrottleHook::IsSourcePtrValid();
                // "Separate aim input" = any aim input method that independently drives
                // the reticle.  This includes bound analog aim axes AND digital aim buttons.
                // When true, the flight stick keeps direct cluster authority for ship steering.
                bool hasDigitalAimButtons = s_config.digitalAimLeftButton.IsValid()
                                         || s_config.digitalAimRightButton.IsValid()
                                         || s_config.digitalAimUpButton.IsValid()
                                         || s_config.digitalAimDownButton.IsValid();
                bool hasSeparateAimAxes = s_config.aimYawAxis.IsValid() || s_config.aimPitchAxis.IsValid();
                bool hasSeparateAimInput = hasSeparateAimAxes || hasDigitalAimButtons;

                // Toggle aim mode button: edge-detected toggle between independent and aim-driven
                {
                    static bool s_aimModeOverride = false; // true = force aim-driven even with axes bound
                    static bool s_toggleAimModePrev = false;
                    bool curToggleAimMode = IsButtonPressed(s_config.toggleAimModeButton);
                    if (curToggleAimMode && !s_toggleAimModePrev) {
                        s_aimModeOverride = !s_aimModeOverride;
                        RuntimePaths::AppendLogAlways("[Controller]",
                            s_aimModeOverride ? "[Aim] Toggled to: Aim-Driven Steering"
                                             : "[Aim] Toggled to: Independent Aim & Steer");
                    }
                    s_toggleAimModePrev = curToggleAimMode;
                    // Override: when toggled, treat as if no separate aim input
                    if (s_aimModeOverride) hasSeparateAimInput = false;
                }

                // Only suppress cluster gates when source aim is active WITHOUT separate input
                // (i.e., aim-driven steering where engine derives steering from mouse accumulators).
                // With separate aim input (analog axes OR digital buttons), the flight stick keeps
                // direct cluster authority.
                // HOSAM mode: plugin releases pitch/yaw gates entirely so native mouse drives
                // steering through the game's normal pipeline.
                bool suppressForHOSAM = s_config.bHOSAMMode;
                bool suppressClusterForAim = suppressForHOSAM || (sourceAimActive && !hasSeparateAimInput);
                ThrottleHook::SetRotationalOverride(
                    lateral,
                    yaw,
                    pitch,
                    true,
                    strafeLatOverrideActive || rollOverrideActive,
                    strafeY,
                    strafeVertOverrideActive,
                    !suppressClusterForAim,   // yawEnabled: re-enabled with separate aim axes
                    !suppressClusterForAim);  // pitchEnabled: re-enabled with separate aim axes

                // Source-object reticle injection: drive the aiming reticle by writing
                // aim axes scaled to mouse accumulator range into source+0x4C (yaw) /
                // source+0x50 (pitch), range [-200.0, +200.0]. The mouse accumulator
                // pathway works regardless of controller mode state, unlike the gamepad
                // input lanes (+0x44/+0x48). If the source pointer is not yet valid
                // or the feature is disabled, this is a no-op.
                if (s_config.bSourceObjectAim && !s_config.bHOSAMMode) {
                    static bool s_aimDiagLogged = false;
                    if (!s_aimDiagLogged) {
                        s_aimDiagLogged = true;
                        char diagMsg[256];
                        snprintf(diagMsg, sizeof(diagMsg),
                            "[Aim] first fire: srcPtrValid=%s srcPtr=0x%llX writeAddr4C=0x%llX writeAddr50=0x%llX separateAxes=%s",
                            ThrottleHook::IsSourcePtrValid() ? "YES" : "NO",
                            (unsigned long long)ThrottleHook::GetSourceBasePtr(),
                            (unsigned long long)(ThrottleHook::GetSourceBasePtr() + 0x4C),
                            (unsigned long long)(ThrottleHook::GetSourceBasePtr() + 0x50),
                            hasSeparateAimAxes ? "YES" : "NO");
                        RuntimePaths::AppendLogAlways("[Controller]", diagMsg);
                    }
                    if (ThrottleHook::IsSourcePtrValid()) {
                        float aimYaw, aimPitch;

                        if (hasSeparateAimInput && hasSeparateAimAxes) {
                            // Poll the aim axes — this is the TARGET position
                            float targetYaw   = NormBipolar(s_config.aimYawAxis,
                                                   s_config.fAimYawSensitivity,
                                                   s_config.bInvertAimYaw);
                            float targetPitch = NormBipolar(s_config.aimPitchAxis,
                                                   s_config.fAimPitchSensitivity,
                                                   s_config.bInvertAimPitch);
                            aimYaw   = targetYaw;
                            aimPitch = targetPitch;

                            // EMA smoothing for low-resolution analog aim sensors
                            if (s_config.fAimSmoothing > 0.001f) {
                                static float s_smoothYaw   = 0.0f;
                                static float s_smoothPitch = 0.0f;
                                float smoothPow = std::pow(s_config.fAimSmoothing, dt * 60.0f);
                                s_smoothYaw   = s_smoothYaw   * smoothPow + aimYaw   * (1.0f - smoothPow);
                                s_smoothPitch = s_smoothPitch * smoothPow + aimPitch * (1.0f - smoothPow);
                                aimYaw   = s_smoothYaw;
                                aimPitch = s_smoothPitch;
                            }
                        } else if (!hasSeparateAimInput && s_config.bMirrorFlightToAim) {
                            // Aim-driven steering: flight stick mirrors to reticle
                            aimYaw   = yaw   * s_config.fAimSensitivity;
                            aimPitch = pitch * s_config.fAimSensitivity;
                        } else {
                            // Digital-only aim or no aim input: start at center.
                            // Digital accumulators (below) will override with actual position.
                            aimYaw   = 0.0f;
                            aimPitch = 0.0f;
                        }

                        // Digital aim: accumulator-style virtual cursor.
                        // Only active in independent aim mode (hasSeparateAimInput).
                        // When digital buttons are the aim method, the accumulators
                        // ALWAYS drive the reticle — even at (0,0) which means "center",
                        // not "defer to flight stick".
                        bool digitalAimActive = false;
                        {
                            static float s_digitalAimYaw   = 0.0f;
                            static float s_digitalAimPitch = 0.0f;

                            if (!hasSeparateAimInput) {
                                // Aim-driven steering mode: reset accumulators so there's
                                // no stale position when toggling back to independent.
                                s_digitalAimYaw   = 0.0f;
                                s_digitalAimPitch = 0.0f;
                            } else if (IsButtonPressed(s_config.digitalAimCenterButton)) {
                                // Center resets the accumulated position
                                s_digitalAimYaw   = 0.0f;
                                s_digitalAimPitch = 0.0f;
                            } else {
                                // Accumulate while direction buttons are held
                                float dY = 0.0f, dP = 0.0f;
                                if (IsButtonPressed(s_config.digitalAimLeftButton))  dY -= 1.0f;
                                if (IsButtonPressed(s_config.digitalAimRightButton)) dY += 1.0f;
                                if (IsButtonPressed(s_config.digitalAimUpButton))    dP -= 1.0f;
                                if (IsButtonPressed(s_config.digitalAimDownButton))  dP += 1.0f;

                                // Normalize diagonal so it doesn't travel faster
                                float dMag = std::sqrt(dY * dY + dP * dP);
                                if (dMag > 1.0f) {
                                    dY /= dMag;
                                    dP /= dMag;
                                }

                                // Integrate: position += direction * speed * dt
                                s_digitalAimYaw   += dY * s_config.fDigitalAimValue * dt;
                                s_digitalAimPitch += dP * s_config.fDigitalAimValue * dt;

                                // Clamp accumulated position to [-1, +1]
                                s_digitalAimYaw   = std::clamp(s_digitalAimYaw,   -1.0f, 1.0f);
                                s_digitalAimPitch = std::clamp(s_digitalAimPitch, -1.0f, 1.0f);
                            }

                            // When digital buttons are the active aim method, always apply
                            // (even at 0,0 — that means "reticle at center", not "no input").
                            // When analog aim axes are the primary, digital overrides only
                            // when the accumulator has been displaced from center.
                            if (hasSeparateAimInput && hasDigitalAimButtons) {
                                aimYaw   = s_digitalAimYaw;
                                aimPitch = s_digitalAimPitch;
                                digitalAimActive = true;
                            }
                        }

                        // Circular normalization: clamp vector magnitude to 1.0 before
                        // scaling to the mouse accumulator range. Without this, diagonal
                        // deflection produces magnitude √2 ≈ 1.414 which exceeds the
                        // game's circular limit and causes snap-back at the corners.
                        {
                            float mag = std::sqrt(aimYaw * aimYaw + aimPitch * aimPitch);
                            if (mag > 1.0f) {
                                aimYaw   /= mag;
                                aimPitch /= mag;
                            }
                        }

                        // Scale to mouse accumulator range [-200.0, +200.0]
                        aimYaw   *= 200.0f;
                        aimPitch *= 200.0f;

                        ThrottleHook::SetSourceObjectAim(aimYaw, aimPitch, true);

                        // Periodic diagnostics (only when bLogThrottle is enabled)
                        if (s_config.logThrottle) {
                            static uint64_t s_aimLogCounter = 0;
                            if (++s_aimLogCounter % (s_config.pollRateHz * 2) == 1) {
                                uintptr_t src = ThrottleHook::GetSourceBasePtr();
                                float rb4C = SafeReadFloat(src + 0x4C);
                                float rb50 = SafeReadFloat(src + 0x50);
                                float rb54 = SafeReadFloat(src + 0x54);
                                float rb58 = SafeReadFloat(src + 0x58);
                                char diagBuf[256];
                                snprintf(diagBuf, sizeof(diagBuf),
                                    "[Aim] mode=%s wrote Y=%.3f P=%.3f | readback +4C=%.3f +50=%.3f +54=%.3f +58=%.3f",
                                    hasSeparateAimAxes ? "separate" : (s_config.bMirrorFlightToAim ? "mirror" : "center"),
                                    aimYaw, aimPitch, rb4C, rb50, rb54, rb58);
                                RuntimePaths::AppendLogAlways("[Controller]", diagBuf);
                            }
                        }
                    } else {
                        ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, true);
                    }
                }

                // --- HOSAM Alignment Assist ---
                // Observes the game's native mouse accumulator. When the mouse has been
                // idle (no change) for the configured duration and the accumulator is
                // within the alignment radius, apply exponential decay toward (0,0).
                if (s_config.bHOSAMMode && s_config.bAlignmentAssist && ThrottleHook::IsSourcePtrValid()) {
                    static float s_prevAccumYaw = 0.0f;
                    static float s_prevAccumPitch = 0.0f;
                    static int   s_alignIdleFrames = 0;

                    uintptr_t src = ThrottleHook::GetSourceBasePtr();
                    float curYaw   = SafeReadFloat(src + 0x4C);
                    float curPitch = SafeReadFloat(src + 0x50);

                    // Detect mouse movement: has the accumulator changed since last frame?
                    constexpr float kMoveEpsilon = 0.5f; // Ignore sub-pixel jitter
                    bool mouseMoved = (std::abs(curYaw - s_prevAccumYaw) > kMoveEpsilon)
                                   || (std::abs(curPitch - s_prevAccumPitch) > kMoveEpsilon);

                    s_prevAccumYaw = curYaw;
                    s_prevAccumPitch = curPitch;

                    if (mouseMoved) {
                        s_alignIdleFrames = 0;
                    } else {
                        s_alignIdleFrames++;
                    }

                    int idleFrameThreshold = std::max(1, (s_config.pollRateHz * s_config.iAlignmentIdleMs) / 1000);

                    if (s_alignIdleFrames >= idleFrameThreshold) {
                        float dist = std::sqrt(curYaw * curYaw + curPitch * curPitch);

                        if (dist <= s_config.fAlignmentRadius && dist > 0.1f) {
                            // Exponential decay: each frame, reduce toward zero
                            float decayFactor = std::exp(-s_config.fAlignmentDecayRate * dt);
                            float newYaw   = curYaw   * decayFactor;
                            float newPitch = curPitch * decayFactor;

                            // Snap to exactly zero when close enough to prevent asymptotic creep
                            if (std::abs(newYaw) < 0.1f) newYaw = 0.0f;
                            if (std::abs(newPitch) < 0.1f) newPitch = 0.0f;

                            SafeWriteFloat2(src + 0x4C, newYaw);
                            SafeWriteFloat2(src + 0x50, newPitch);

                            // Update prev so our own write doesn't register as mouse movement
                            s_prevAccumYaw = newYaw;
                            s_prevAccumPitch = newPitch;
                        }
                    }
                }

                if (s_config.bAccumulatorThrottle) {
                    // --- BOOST GUARD ---
                    // Hold-for-boost: when the boost output is held, completely pause
                    // accumulator logic and let the game engine own the throttle.
                    // Combined with delta burst below, this ensures the game retains
                    // ownership of +0x68/+0x6C whenever we're not actively changing speed.
                    const bool boostHeld = s_config.bHoldForBoost && IsBoostOutputHeld();
                    float stickDeflection = throttle; // [-1.0, +1.0] bipolar

                    if (!boostHeld) {
                     // --- BOOST RELEASE TRANSITION ---
                     // On the falling edge of boost, cancel the boost state and
                     // set accumulator to max throttle (post-boost = full speed).
                     if (s_prevBoostHeld) {
                         s_accumulatorThrottle = 1.0f;
                         s_lastAccumBurstValue = -999.0f; // Force burst to write new value
                         // Brief reverse key hold to trip the engine's boost cancellation.
                         // A pulse is too short for the engine to recognize; a sustained
                         // hold of ~150ms reliably triggers the deceleration check.
                         s_boostCancelFrames = std::max(1, (s_config.pollRateHz * 150) / 1000);
                     }

                     // --- BOOST CANCEL: sustained reverse hold ---
                     if (s_boostCancelFrames > 0) {
                         SetOutputHeld(ReverseOutput, OwnerBoostCancel, true);
                         s_boostCancelFrames--;
                         if (s_boostCancelFrames == 0) {
                             SetOutputHeld(ReverseOutput, OwnerBoostCancel, false);
                         }
                     }

                     // --- ACCUMULATOR THROTTLE MODE (Dual-Stick / Self-Centering) ---
                     // Stick deflection is a RATE input:
                     //   Forward  → accumulate throttle upward, proportional to deflection
                     //   Backward → reduce throttle toward 0, then direct-memory reverse
                     //   Neutral  → HOLD current throttle value (no decay)
                     const float kDeadzone = std::max(s_config.fThrottleDeadzone, 0.05f); // Min 5% for accumulator stability

                     if (stickDeflection > kDeadzone) {
                         // Forward: accumulate upward
                         ThrottleHook::SetReverseOverride(false);
                         s_accumulatorThrottle += stickDeflection * s_config.fAccumulatorRate * dt;
                         s_accumulatorThrottle = std::clamp(s_accumulatorThrottle, 0.0f, 1.0f);
                     } else if (stickDeflection < -kDeadzone) {
                         // Backward: reduce throttle toward 0
                         if (s_accumulatorThrottle > 0.0f) {
                             s_accumulatorThrottle += stickDeflection * s_config.fAccumulatorRate * dt;
                             if (s_accumulatorThrottle < 0.0f) s_accumulatorThrottle = 0.0f;
                         } else {
                              // Throttle at 0 and stick still back → direct-memory reverse
                              s_accumulatorThrottle = 0.0f;
                              float vel = ReadClusterVelocity(s_activeThrottlePtr);
                              bool gateOpen = (vel < 0.0f) || (vel >= 0.0f && vel <= s_config.fReverseGateVelocity);

                              if (gateOpen) {
                                  ThrottleHook::SetReverseOverride(true);
                                  SafeInjectThrottle(s_activeThrottlePtr, -1.0f);
                                  s_lastInjectedThrottle = -1.0f;
                              } else {
                                  ThrottleHook::SetReverseOverride(false);
                                  SafeInjectThrottle(s_activeThrottlePtr, 0.0f);
                              }

                              // Reverse diagnostics
                              if (s_config.logThrottle) {
                                  static uint64_t s_revLogIter = 0;
                                  if (iter > s_revLogIter + 15) {
                                      s_revLogIter = iter;
                                      char revBuf[256];
                                      snprintf(revBuf, sizeof(revBuf),
                                          "[Reverse] accum vel=%.1f gate=%s src=%s +5C=%+.3f +68=%+.3f +6C=%+.3f +70=%.1f",
                                          vel, gateOpen ? "OPEN" : "CLOSED",
                                          ThrottleHook::IsSourcePtrValid() ? "OK" : "STALE",
                                          SafeReadFloat(s_activeThrottlePtr + 0x5C),
                                          SafeReadFloat(s_activeThrottlePtr + 0x68),
                                          SafeReadFloat(s_activeThrottlePtr + 0x6C),
                                          SafeReadFloat(s_activeThrottlePtr + 0x70));
                                      RuntimePaths::AppendLogAlways("[Controller]", revBuf);
                                  }
                              }
                          }
                     } else {
                         // Neutral: HOLD current throttle value (optional decay if configured)
                         ThrottleHook::SetReverseOverride(false);
                         if (s_config.fAccumulatorDecay > 0.0f) {
                             if (s_accumulatorThrottle > 0.0f) {
                                 s_accumulatorThrottle -= s_config.fAccumulatorDecay * dt;
                                 if (s_accumulatorThrottle < 0.0f) s_accumulatorThrottle = 0.0f;
                             }
                         }
                         // else: decay=0 → hold at current value
                     }

                     // --- DELTA BURST INJECTION ---
                     // Only write to +0x68/+0x6C when the accumulator value actually
                     // changes, then stop after a burst window. This lets the game
                     // engine retain ownership between changes — required for boost
                     // and other game systems that monitor throttle address writes.
                     // At max throttle (1.0) with stick forward, the clamped value
                     // doesn't change → no burst → game keeps ownership.
                     float accTarget = std::max(s_accumulatorThrottle, 0.0f);
                     bool accMoved = (std::abs(accTarget - s_lastAccumBurstValue) > kThrottleDeltaAuthority);
                     if (accMoved) {
                         s_accumBurstFrames = throttleBurstFrameCount;
                         s_lastAccumBurstValue = accTarget;
                     }

                     if (s_accumBurstFrames > 0) {
                         s_lastInjectedThrottle = accTarget;
                         SafeInjectThrottle(s_activeThrottlePtr, accTarget);
                         s_accumBurstFrames--;
                     }
                    } else {
                     // Boost held: accumulator frozen, no writes. Game owns throttle.
                     // Reset burst state so we don't resume with stale burst frames.
                     s_accumBurstFrames = 0;
                    }

                    s_prevBoostHeld = boostHeld;

                     float targetThrottle = std::max(s_accumulatorThrottle, 0.0f);

                     // --- VELOCITY DIAGNOSTICS (only when logging is enabled) ---
                     if (s_config.logThrottle) {
                         static bool s_velStartupLogged = false;
                         static uint64_t s_velLogIter = 0;
                         float vel = ReadClusterVelocity(s_activeThrottlePtr);

                         if (!s_velStartupLogged) {
                             s_velStartupLogged = true;
                             char velBuf[256];
                             snprintf(velBuf, sizeof(velBuf),
                                 "[DualStick] Velocity monitor online: cluster+0x70 @ 0x%llX rawVal=%.4f",
                                 (unsigned long long)(s_activeThrottlePtr + 0x70),
                                 vel);
                             RuntimePaths::AppendLogAlways("[Controller]", velBuf);
                         }

                         // Periodic velocity log (~every 2 seconds)
                         if (iter > s_velLogIter + (uint64_t)(s_config.pollRateHz * 2)) {
                             s_velLogIter = iter;
                             char velBuf[256];
                             snprintf(velBuf, sizeof(velBuf),
                                 "[DualStick] vel=%.2f accum=%+.3f stick=%+.3f gate=%.1f %s",
                                 vel, s_accumulatorThrottle, stickDeflection,
                                 s_config.fReverseGateVelocity,
                                 (vel >= 0.0f && vel <= s_config.fReverseGateVelocity) ? "GATE_OPEN" : "GATE_CLOSED");
                             RuntimePaths::AppendLogAlways("[Controller]", velBuf);
                         }
                     }

                     // --- 6DOF TELEMETRY ---
                     static float lP=0, lY=0, lR=0;
                     static uint64_t lastLogIter = 0;
                     if (s_config.logThrottle && iter > lastLogIter + 30) {
                         if (std::abs(pitch - lP) > 0.05f || std::abs(yaw - lY) > 0.05f || std::abs(roll - lR) > 0.05f || std::abs(stickDeflection) > 0.05f) {
                             float vel = ReadClusterVelocity(s_activeThrottlePtr);
                             char tel[256];
                             sprintf_s(tel, "[Telemetry] Vector: P%+.3f Y%+.3f R%+.3f | Accum%+.3f Stick%+.3f Vel%.0f", pitch, yaw, roll, targetThrottle, stickDeflection, vel);
                             CtrlLog(tel);
                             lP=pitch; lY=yaw; lR=roll; lastLogIter = iter;
                         }
                     }
                } else if (reverseHeld) {
                     // Direct-memory reverse: binary -1.0 across all three lanes.
                     float vel = ReadClusterVelocity(s_activeThrottlePtr);
                     // Gate logic: allow reverse when velocity is at or below the threshold,
                     // OR already negative (already reversing — don't cut out mid-reverse).
                     // Only block when moving forward faster than the gate threshold.
                     bool gateOpen = (vel < 0.0f) || (vel >= 0.0f && vel <= s_config.fReverseGateVelocity);

                     if (gateOpen) {
                         ThrottleHook::SetReverseOverride(true);
                         SafeInjectThrottle(s_activeThrottlePtr, -1.0f);
                         s_lastInjectedThrottle = -1.0f;
                     } else {
                         // Above velocity gate: brake to zero, don't engage reverse yet
                         ThrottleHook::SetReverseOverride(false);
                         SafeInjectThrottle(s_activeThrottlePtr, 0.0f);
                         s_lastInjectedThrottle = 0.0f;
                     }

                     // Reverse diagnostics — log every gate transition + periodic
                     if (s_config.logThrottle) {
                         static uint64_t s_revLogIter2 = 0;
                         static bool s_prevGateOpen = false;
                         bool gateTransition = (gateOpen != s_prevGateOpen);
                         s_prevGateOpen = gateOpen;

                         if (gateTransition || iter > s_revLogIter2 + 15) {
                             s_revLogIter2 = iter;
                             uintptr_t src = ThrottleHook::GetSourceBasePtr();
                             float src3C = src ? SafeReadFloat(src + 0x3C) : -999.0f;
                             char revBuf[384];
                             snprintf(revBuf, sizeof(revBuf),
                                 "[Reverse] hw %svel=%.1f gate=%s src=%s src3C=%+.3f +5C=%+.3f +68=%+.3f +6C=%+.3f +70=%.1f",
                                 gateTransition ? "TRANSITION " : "",
                                 vel, gateOpen ? "OPEN" : "CLOSED",
                                 ThrottleHook::IsSourcePtrValid() ? "OK" : "STALE",
                                 src3C,
                                 SafeReadFloat(s_activeThrottlePtr + 0x5C),
                                 SafeReadFloat(s_activeThrottlePtr + 0x68),
                                 SafeReadFloat(s_activeThrottlePtr + 0x6C),
                                 SafeReadFloat(s_activeThrottlePtr + 0x70));
                             RuntimePaths::AppendLogAlways("[Controller]", revBuf);
                         }
                     }

                     s_throttleBurstFrames = 0;
                     lastInjectedHardwareValue = -999.0f; // Reset delta guard for forward re-engagement
                } else if (s_config.bHoldForBoost && IsBoostOutputHeld()) {
                     // Hold-for-boost: suppress throttle writes, let the game own +0x68/+0x6C
                     ThrottleHook::SetReverseOverride(false);
                     s_throttleBurstFrames = 0;
                } else {
                     ThrottleHook::SetReverseOverride(false);
                     float commandedThrottle = throttle;
                     bool firstThrottleCommand = (lastInjectedHardwareValue == -999.0f);
                     bool throttleMoved = firstThrottleCommand ||
                         (std::abs(commandedThrottle - lastInjectedHardwareValue) > kThrottleDeltaAuthority);

                     if (throttleMoved) {
                         s_throttleBurstFrames = throttleBurstFrameCount;
                         s_throttleBurstValue = commandedThrottle;
                         lastInjectedHardwareValue = commandedThrottle;
                     }

                     // Delta burst authority: write the paired throttle fields for a short
                     // window after movement, then hand the speed envelope back to physics.
                     bool throttleBurstActive = (s_throttleBurstFrames > 0);
                     float targetThrottle = throttleBurstActive ? s_throttleBurstValue : gameTarget;

                     s_lastInjectedThrottle = targetThrottle;
                     if (throttleBurstActive) {
                         SafeInjectThrottle(s_activeThrottlePtr, targetThrottle);
                         s_throttleBurstFrames--;
                     }

                     // --- 6DOF TELEMETRY ---
                     static float lP=0, lY=0, lR=0;
                     static uint64_t lastLogIter = 0;
                    if (s_config.logThrottle && iter > lastLogIter + 30) { // Log at most ~4Hz
                         if (std::abs(pitch - lP) > 0.05f || std::abs(yaw - lY) > 0.05f || std::abs(roll - lR) > 0.05f) {
                             char tel[256];
                             sprintf_s(tel, "[Telemetry] Vector: P%+.3f Y%+.3f R%+.3f | T%+.3f", pitch, yaw, roll, targetThrottle);
                             CtrlLog(tel);
                             lP=pitch; lY=yaw; lR=roll; lastLogIter = iter;
                         }
                     }
                }
            } else {
                s_plausibilityFailCount++;
                if (s_plausibilityFailCount > 60) { // 0.5s of failure required to drop
                    CtrlLog("[SignalHunter] Persistent signal loss. Pointer invalidated.");
                    ArmDiscoveryForReacquire("persistent signal loss");
                    lastInjectedHardwareValue = -999.0f;
                    ReleaseAllShipButtonOutputs();
                }
            }
        }

        // Dampened state log
        if (g_discoveryLocked != s_lastLogLockedState) {
            s_lastLogLockedState = g_discoveryLocked;
            if (g_discoveryLocked) CtrlLog("[SignalHunter] State: PASSIVE (Locked)");
            else CtrlLog("[SignalHunter] State: DISCOVERY (Active)");
        }

        // Logging (1Hz when armed, 10s when locked, silent when passive/unarmed)
        if (s_config.logThrottle && g_discoveryArmed && (iter % s_config.pollRateHz == 0)) {
            char buf[256];
            sprintf_s(buf, "[iter=%llu] FINDING SIGNAL: norm=%.3f candidates=%d",
                iter, throttle, candCount);
            CtrlLog(buf);
        }
        // Dampened state log (every 1.2s at 100Hz)
        if (s_config.logThrottle && g_discoveryLocked && s_activeThrottlePtr && (iter % (s_config.pollRateHz * 12)) == 0) {
            char buf[256];
            sprintf_s(buf, "[AbsoluteHOTAS] [iter=%llu] ACTIVE INJECTION: throttle=%.3f ptr=0x%llX", 
                iter, throttle, (unsigned long long)s_activeThrottlePtr);
            CtrlLog(buf);
        }
        std::this_thread::sleep_for(sleepDuration);
    }

    ReleaseAllShipButtonOutputs();
    DeviceManager::Shutdown();
}

bool ThrottleController::Initialize() {
    LoadConfig();
    return s_config.enabled;
}

void ThrottleController::ReloadConfig() {
    s_configReloadRequested.store(true, std::memory_order_release);
    CtrlLog("Config reload requested (will apply on next loop iteration).");
}

void ThrottleController::Start() {
    if (s_running) return;
    s_running = true;
    
    // In standalone mode discovery is armed by bAlwaysOn or the activate button.
    ThrottleHook::SetCaptureEnabled(false);
    s_thread = std::thread(ControlLoop);
    s_thread.detach();
    CtrlLog("Signal Hunter thread launched.");
}

void ThrottleController::Stop() {
    s_running = false;
    ReleaseAllShipButtonOutputs();
}
ThrottleController::Config& ThrottleController::GetConfig() { return s_config; }
float ThrottleController::GetCurrentThrottle() { return s_currentThrottle.load(std::memory_order_relaxed); }

std::vector<ThrottleController::ShipActionInfo> ThrottleController::GetShipActionBindings() {
    static const char* labels[] = {
        "Fire Boosters", "Switch Flight Modes", "Toggle POV",
        "Fire Weapon 0", "Fire Weapon 1", "Fire Weapon 2",
        "Ship Action 1", "Select Target",
        "Increase System Power", "Decrease System Power",
        "Previous System", "Next System",
        "Open Scanner", "Repair",
        "Ship Alternate Control", "Cruise", "Cancel",
        "Undock / Take-Off", "Get Up", "Exit Ship",
        "Zoom Camera In", "Zoom Camera Out", "Autopilot On/Off"
    };
    std::vector<ShipActionInfo> result;
    for (size_t i = 0; i < s_shipButtonBindings.size() && i < 23; i++) {
        result.push_back({
            labels[i],
            s_shipButtonBindings[i].sourceIniKey,
            s_shipButtonBindings[i].buttonRef
        });
    }
    return result;
}

const std::unordered_map<int, std::pair<long, long>>& ThrottleController::GetCalibrationData() {
    return s_config.axisCalibration;
}

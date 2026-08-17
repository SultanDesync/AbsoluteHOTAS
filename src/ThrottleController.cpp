#include "PCH.h"

#include "SuiteCommandBindings.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"
#include "ShipOutput.h"
#include "MacroEngine.h"
#include "SignalHunter.h"
#include "AimController.h"
#include "PilotState.h"
#include "RuntimePaths.h"
#include "DeviceManager.h"
#include "UIHook.h"
#include "NativeShipControl.h"
#include "HeadTracking.h"
#include "MenuControlReuse.h"
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <chrono>
#include <SimpleIni.h>



// ---- Static member definitions ----
ThrottleController::Config    ThrottleController::s_config;
std::atomic<bool>  ThrottleController::s_running{ false };
std::atomic<bool>  ThrottleController::s_configReloadRequested{ false };
std::atomic<uint32_t> ThrottleController::s_configGeneration{ 0 };
std::thread        ThrottleController::s_thread;

static bool s_turnAssistRuntimeActive = false;
static MenuControlReuse::AxisState s_menuVerticalState;
static MenuControlReuse::AxisState s_menuHorizontalState;
static MenuControlReuse::ButtonState s_menuSelectState;

// ---- Profile switching (see docs/reference/profile-switching.md) ----
// Each slot holds a fully-resolved snapshot of the three things a swap replaces:
// the Config, the ship-button table, and the macro set. Slot 0
// is the base profile; slots 1+ are switch profiles with a trigger button. All of
// this is control-thread-only state — no locking.
// How a slot's trigger button selects it.
//   Momentary — active while held (spring-return shift button); returns on release.
//   Toggle    — press flips in/out of the profile.
//   Selector  — active while held, evaluated by LEVEL not edge: for a rotary/detent
//               switch where each position keeps its button held. Self-syncs to the
//               physical position at startup and after the overlay closes.
enum class SwapMode { Momentary, Toggle, Selector };

struct ProfileSlot {
    ThrottleController::Config      config;
    std::vector<ShipButtonBinding>  shipBindings;
    std::vector<Macro>              macros;

    // Activation (unused for slot 0).
    BindingRef  trigger;
    int         triggerKey = 0;
    int         triggerMods = 0;     // keyboard modifier chord (bit0 Ctrl, bit1 Shift, bit2 Alt)
    int         shortcutKey = 0;     // independent profile-file keyboard shortcut
    int         shortcutMods = 0;
    SwapMode    mode       = SwapMode::Momentary;
    bool        prevDown   = false;  // momentary/toggle edge-detect state
    bool        prevShortcutDown = false;
    int         restoreSlot = 0;     // momentary: slot that was active when this was pressed
};
static std::vector<ProfileSlot> s_profiles;   // [0] = base
static int s_activeSlot = 0;
static int s_lastSelectorPos = -2;            // last selector position acted on (-2 = re-eval)

static void ApplyProfile(const ProfileSlot& slot, ThrottleController::Config& config) {
    config = slot.config;
    ShipOutputSystem::RestoreBindings(slot.shipBindings);
    MacroEngine::RestoreMacros(slot.macros);
}

enum class CruiseAssistMode { Off, HoldCurrent, Stop, Half, Max };
static CruiseAssistMode s_cruiseAssistMode = CruiseAssistMode::Off;
static float s_cruiseAssistTarget = 0.0f;
static bool s_resetCruiseEdges = true;

// ---- Logging ----
// All controller lines are gated by bEnableLog inside RuntimePaths::Log.
static void CtrlLog(const std::string& msg) {
    RuntimePaths::Log("[Controller]", msg);
}

static void ResetMenuControlReuse() {
    ShipOutputSystem::ReleaseOwnerOutputs(OwnerMenuUp);
    ShipOutputSystem::ReleaseOwnerOutputs(OwnerMenuDown);
    ShipOutputSystem::ReleaseOwnerOutputs(OwnerMenuLeft);
    ShipOutputSystem::ReleaseOwnerOutputs(OwnerMenuRight);
    ShipOutputSystem::ReleaseOwnerOutputs(OwnerMenuSelect);
    s_menuVerticalState = {};
    s_menuHorizontalState = {};
    s_menuSelectState = {};
}

static float NormalizeMenuAxis(const BindingRef& ref, bool invert) {
    if (!ref.IsValid() || ref.value <= 0) return 0.0f;

    float minimum = 0.0f;
    float maximum = 65535.0f;
    if (ref.deviceIndex >= 0) {
        const int calibrationKey = (ref.deviceIndex << 8) | ref.value;
        const auto& calibration = ThrottleController::GetConfig().axisCalibration;
        if (const auto it = calibration.find(calibrationKey); it != calibration.end()) {
            minimum = static_cast<float>(it->second.first);
            maximum = static_cast<float>(it->second.second);
        }
    }

    const float center = (minimum + maximum) * 0.5f;
    const float raw = std::clamp(
        static_cast<float>(DeviceManager::GetRawAxis(ref)), minimum, maximum);
    const float span = raw < center ? center - minimum : maximum - center;
    float normalized = span > 0.0f ? (raw - center) / span : 0.0f;
    if (invert) normalized = -normalized;
    return std::clamp(normalized, -1.0f, 1.0f);
}

static void UpdateMenuControlReuse(bool menuContextAllowed,
                                   bool targetingContextAllowed) {
    const auto& cfg = ThrottleController::GetConfig();
    const bool pitchValid = cfg.pitchAxis.IsValid() && cfg.pitchAxis.value > 0;
    const bool yawValid = cfg.yawAxis.IsValid() && cfg.yawAxis.value > 0;

    const int verticalDirection = MenuControlReuse::UpdateAxis(
        s_menuVerticalState, menuContextAllowed, cfg.bUsePitchAxisForMenu, pitchValid,
        NormalizeMenuAxis(cfg.pitchAxis, cfg.bInvertMenuVertical),
        cfg.fMenuAxisEngageThreshold, cfg.fMenuAxisReleaseThreshold);
    const int horizontalDirection = MenuControlReuse::UpdateAxis(
        s_menuHorizontalState, menuContextAllowed || targetingContextAllowed,
        cfg.bUseYawAxisForMenu, yawValid,
        NormalizeMenuAxis(cfg.yawAxis, cfg.bInvertMenuHorizontal),
        cfg.fMenuAxisEngageThreshold, cfg.fMenuAxisReleaseThreshold);

    ShipOutputSystem::SetUniversalContextHeld(
        "IncreaseSystemPower", OwnerMenuUp, verticalDirection < 0);
    ShipOutputSystem::SetUniversalContextHeld(
        "DecreaseSystemPower", OwnerMenuDown, verticalDirection > 0);
    ShipOutputSystem::SetUniversalContextHeld(
        "PreviousSystem", OwnerMenuLeft, horizontalDirection < 0);
    ShipOutputSystem::SetUniversalContextHeld(
        "NextSystem", OwnerMenuRight, horizontalDirection > 0);

    const ShipButtonBinding* primary =
        ShipOutputSystem::FindShipButtonBinding("FireWeapon0");
    const bool primaryValid = primary && primary->buttonRef.IsValid();
    const bool primaryPressed = primaryValid &&
        DeviceManager::IsButtonPressed(primary->buttonRef);
    const bool selectHeld = MenuControlReuse::UpdateButton(
        s_menuSelectState, menuContextAllowed, cfg.bUsePrimaryWeaponForMenuSelect,
        primaryValid, primaryPressed);
    ShipOutputSystem::SetUniversalContextHeld(
        "SelectTarget", OwnerMenuSelect, selectHeld);
}

// ---- Axis Normalization ----
float ThrottleController::NormalizeAxis(long rawValue, long axisMin, long axisMax) {
    long center   = s_config.detentCenter;
    long deadzone = s_config.detentDeadzone;

    if (rawValue < axisMin) rawValue = axisMin;
    if (rawValue > axisMax) rawValue = axisMax;
    if (s_config.bInvertThrottle) rawValue = axisMin + axisMax - rawValue;

    if (s_config.unipolarMode) {
        if (s_config.bUnipolarReverse) {
            long rzCenter = s_config.reverseZoneCenter;
            long rzDz     = s_config.reverseZoneDeadzone;
            if (rawValue < rzCenter - rzDz)     return 0.0f;
            if (rawValue <= rzCenter + rzDz)    return 0.0f;
            long fwdStart = rzCenter + rzDz;
            if (rawValue < center - deadzone) {
                float range = static_cast<float>((center - deadzone) - fwdStart);
                if (range <= 0.0f) return 0.0f;
                return 0.5f * static_cast<float>(rawValue - fwdStart) / range;
            }
            if (rawValue <= center + deadzone) return 0.5f;
            long rampStart = center + deadzone;
            if (s_config.bBoostZone) {
                long bzCenter = s_config.boostZoneCenter;
                long bzDz     = s_config.boostZoneDeadzone;
                if (rawValue < bzCenter - bzDz) {
                    float range = static_cast<float>((bzCenter - bzDz) - rampStart);
                    if (range <= 0.0f) return 1.0f;
                    return 0.5f + 0.5f * static_cast<float>(rawValue - rampStart) / range;
                }
                return 1.0f;
            }
            float range = static_cast<float>(axisMax - rampStart);
            if (range <= 0.0f) return 1.0f;
            float norm = 0.5f + 0.5f * static_cast<float>(rawValue - rampStart) / range;
            return std::clamp(norm / s_config.fThrottleSaturation, 0.0f, 1.0f);
        }

        float range = static_cast<float>(axisMax - axisMin);
        float norm  = (range <= 0.0f) ? 0.0f : static_cast<float>(rawValue - axisMin) / range;
        if (norm < s_config.idlePlateau) return 0.0f;
        float result = (norm - s_config.idlePlateau) / (1.0f - s_config.idlePlateau);
        return std::clamp(result / s_config.fThrottleSaturation, 0.0f, 1.0f);
    }

    if (rawValue >= center - deadzone && rawValue <= center + deadzone) return 0.0f;
    if (rawValue > center + deadzone) {
        float range = static_cast<float>(axisMax - (center + deadzone));
        return (range <= 0.0f) ? 0.0f : static_cast<float>(rawValue - (center + deadzone)) / range;
    } else {
        if (!s_config.reverseEnabled) return 0.0f;
        float range = static_cast<float>((center - deadzone) - axisMin);
        return (range <= 0.0f) ? 0.0f : -1.0f + static_cast<float>(rawValue - axisMin) / range;
    }
}

// ---- INI Config Loading ----
void ThrottleController::LoadConfig(const std::string* slotFile) {
    // Layered overlay: mod defaults, then custom overrides win, then (for a switch
    // profile) the slot file wins over both. Sequential
    // LoadFile into one object overwrites single-value keys (multikey is off), so a
    // sparse slot inherits every key it does not mention, and [Macro:*] lands in the
    // same object for MacroEngine::LoadMacros below.
    CSimpleIniA ini;
    ini.SetUnicode();
    const auto path      = RuntimePaths::IniPath().string();
    const auto customPath = RuntimePaths::CustomIniPath().string();

    const bool haveMain = ini.LoadFile(path.c_str()) == SI_OK;
    bool haveCustom = false;
    CSimpleIniA customMeta;
    customMeta.SetUnicode(false);
    if (customMeta.LoadFile(customPath.c_str()) == SI_OK) {
        const long version = customMeta.GetLongValue("Meta", "iConfigVersion", -1);
        if (version >= 1 && version <= 1)
            haveCustom = ini.LoadFile(customPath.c_str()) == SI_OK;
        else
            CtrlLog("Ignored invalid or unsupported custom config: " + customPath);
    }
    if (slotFile)
        ini.LoadFile(slotFile->c_str());  // switch-profile overlay, wins per-key

    if (haveMain || haveCustom)
        CtrlLog("Loaded config (main: " + std::string(haveMain ? "yes" : "no") +
                ", custom: " + (haveCustom ? "yes" : "no") +
                (slotFile ? ", slot: " + *slotFile : "") + ").");
    else
        CtrlLog("No config files found, using defaults.");

    s_config.enabled = ini.GetBoolValue("General", "bEnabled", true);

    s_config.throttleAxis    = ParseBindingRef(ini.GetValue("Hardware", "iThrottleAxis",    ""), 0x32);
    s_config.pitchAxis       = ParseBindingRef(ini.GetValue("Hardware", "iPitchAxis",       ""), 0x31);
    s_config.yawAxis         = ParseBindingRef(ini.GetValue("Hardware", "iYawAxis",         ""), 0x30);
    s_config.rollAxis        = ParseBindingRef(ini.GetValue("Hardware", "iRollAxis",        ""), 0x33);
    s_config.strafeLatAxis   = ParseBindingRef(ini.GetValue("Hardware", "iStrafeLatAxis",   ""), 0x33);
    s_config.strafeVertAxis  = ParseBindingRef(ini.GetValue("Hardware", "iStrafeVertAxis",  ""), 0x34);
    s_config.reverseAxis     = ParseBindingRef(ini.GetValue("Hardware", "iReverseAxis",     ""), 0x36);

    s_config.fPitchSensitivity   = (float)ini.GetDoubleValue("Hardware", "fPitchSensitivity",   1.0);
    s_config.fYawSensitivity     = (float)ini.GetDoubleValue("Hardware", "fYawSensitivity",     1.0);
    s_config.fRollSensitivity    = (float)ini.GetDoubleValue("Hardware", "fRollSensitivity",    1.0);
    s_config.fStrafeSensitivity  = (float)ini.GetDoubleValue("Hardware", "fStrafeSensitivity",  1.0);
    s_config.fReverseSensitivity = (float)ini.GetDoubleValue("Hardware", "fReverseSensitivity", 1.0);
    s_config.fThrottleSensitivity = (float)ini.GetDoubleValue("Hardware", "fThrottleSensitivity", 1.0);

    s_config.fThrottleSaturation  = std::clamp((float)ini.GetDoubleValue("Hardware", "fThrottleSaturation",  1.0), 0.05f, 1.0f);
    s_config.fPitchSaturation     = std::clamp((float)ini.GetDoubleValue("Hardware", "fPitchSaturation",     1.0), 0.05f, 1.0f);
    s_config.fYawSaturation       = std::clamp((float)ini.GetDoubleValue("Hardware", "fYawSaturation",       1.0), 0.05f, 1.0f);
    s_config.fRollSaturation      = std::clamp((float)ini.GetDoubleValue("Hardware", "fRollSaturation",      1.0), 0.05f, 1.0f);
    s_config.fStrafeSaturation    = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeSaturation",    1.0), 0.05f, 1.0f);
    s_config.fStrafeVertSaturation = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeVertSaturation", 1.0), 0.05f, 1.0f);
    s_config.fReverseSaturation   = std::clamp((float)ini.GetDoubleValue("Hardware", "fReverseSaturation",   1.0), 0.05f, 1.0f);

    s_config.bInvertPitch      = ini.GetBoolValue("Hardware", "bInvertPitch",      true);
    s_config.bInvertThrottle   = ini.GetBoolValue("Hardware", "bInvertThrottle",   false);
    s_config.bInvertYaw        = ini.GetBoolValue("Hardware", "bInvertYaw",        false);
    s_config.bInvertRoll       = ini.GetBoolValue("Hardware", "bInvertRoll",       false);
    s_config.bInvertStrafeLat  = ini.GetBoolValue("Hardware", "bInvertStrafeLat",  false);
    s_config.bInvertStrafeVert = ini.GetBoolValue("Hardware", "bInvertStrafeVert", false);
    s_config.bInvertReverse    = ini.GetBoolValue("Hardware", "bInvertReverse",    false);

    s_config.fThrottleDeadzone  = std::clamp((float)ini.GetDoubleValue("Hardware", "fThrottleDeadzone",  0.0),  0.0f, 0.95f);
    s_config.fPitchDeadzone     = std::clamp((float)ini.GetDoubleValue("Hardware", "fPitchDeadzone",     0.0),  0.0f, 0.95f);
    s_config.fYawDeadzone       = std::clamp((float)ini.GetDoubleValue("Hardware", "fYawDeadzone",       0.0),  0.0f, 0.95f);
    s_config.fRollDeadzone      = std::clamp((float)ini.GetDoubleValue("Hardware", "fRollDeadzone",      0.0),  0.0f, 0.95f);
    s_config.fStrafeDeadzone    = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeDeadzone",    0.05), 0.0f, 0.95f);
    s_config.fStrafeVertDeadzone = std::clamp((float)ini.GetDoubleValue("Hardware", "fStrafeVertDeadzone", 0.05), 0.0f, 0.95f);

    s_config.activateButton     = ParseBindingRef(ini.GetValue("Buttons", "iActivateButtonId",   ""), -1);
    s_config.stopButton         = ParseBindingRef(ini.GetValue("Buttons", "iStopButtonId",       ""), -1);
    s_config.toggleWizardButton = ParseBindingRef(ini.GetValue("Buttons", "iToggleWizardButton", ""), -1);
    s_config.cruiseHoldButton = ParseBindingRef(ini.GetValue("ControlExtensions", "iCruiseHoldButton", ""), -1);
    s_config.fullStopButton   = ParseBindingRef(ini.GetValue("ControlExtensions", "iFullStopButton", ""), -1);
    s_config.cruiseHalfButton = ParseBindingRef(ini.GetValue("ControlExtensions", "iCruiseHalfButton", ""), -1);
    s_config.cruiseMaxButton  = ParseBindingRef(ini.GetValue("ControlExtensions", "iCruiseMaxButton", ""), -1);
    s_config.alwaysOn           = ini.GetBoolValue("Buttons", "bAlwaysOn", true);
    s_config.toggleActiveKey    = (int)ini.GetLongValue("Buttons", "iToggleActiveKey", 0x91);

    s_config.detentCenter     = ini.GetLongValue("Normalization", "iDetentCenter",     32768);
    s_config.detentDeadzone   = ini.GetLongValue("Normalization", "iDetentDeadzone",   500);
    s_config.reverseEnabled   = ini.GetBoolValue("Normalization", "bReverseEnabled",   false);
    s_config.unipolarMode     = ini.GetBoolValue("Normalization", "bUnipolarMode",     true);
    s_config.bUnipolarReverse = ini.GetBoolValue("Normalization", "bUnipolarReverse",  false);
    s_config.reverseZoneCenter   = ini.GetLongValue("Normalization", "iReverseZoneCenter",   3000);
    s_config.reverseZoneDeadzone = ini.GetLongValue("Normalization", "iReverseZoneDeadzone", 3000);
    s_config.bBoostZone       = ini.GetBoolValue("Normalization", "bBoostZone",        false);
    s_config.boostZoneCenter  = ini.GetLongValue("Normalization", "iBoostZoneCenter",  62000);
    s_config.boostZoneDeadzone = ini.GetLongValue("Normalization", "iBoostZoneDeadzone", 2000);
    s_config.idlePlateau      = (float)ini.GetDoubleValue("Normalization", "fIdlePlateau",      0.05);
    s_config.reverseDeadzone  = (float)ini.GetDoubleValue("Normalization", "fReverseDeadzone",  0.05);
    s_config.reverseActivationThreshold = (float)ini.GetDoubleValue("Normalization", "fReverseActivationThreshold", 0.05);
    s_config.reverseAxisEnabled = ini.GetBoolValue("Normalization", "bReverseAxisEnabled", true);

    s_config.pollRateHz        = (int)ini.GetLongValue("Injection", "iPollRateHz",     120);
    s_config.throttleBurstMs   = (int)ini.GetLongValue("Injection", "iThrottleBurstMs", 250);
    s_config.rollEnabled       = ini.GetBoolValue("Injection", "bRollEnabled",     true);
    s_config.bEnableInjection  = ini.GetBoolValue("Injection", "bEnableInjection", true);
    // bHoldForBoost relocated [Injection] -> [DualStick] in 4.0. Read the new home,
    // falling back to the old location as a migration alias for un-migrated files.
    s_config.bHoldForBoost     = ini.GetBoolValue("DualStick", "bHoldForBoost",
                                     ini.GetBoolValue("Injection", "bHoldForBoost", true));

    s_config.bNativeShipControls = ini.GetBoolValue("NativeControls", "bEnabled", true);
    s_config.headTracking.enabled = ini.GetBoolValue("HeadTracking", "bEnabled", false);
    s_config.headTracking.openTrackEnabled = ini.GetBoolValue(
        "HeadTracking", "bOpenTrackEnabled", true);
    {
        const char* source = ini.GetValue("HeadTracking", "sSource", "OpenTrack");
        s_config.headTracking.source = _stricmp(source, "TobiiViaOpenTrack") == 0
            ? HeadTracking::Source::TobiiViaOpenTrack
            : HeadTracking::Source::OpenTrack;
    }
    s_config.headTracking.recenterButton = ParseBindingRef(
        ini.GetValue("HeadTracking", "iRecenterButton", ""), -1);
    s_config.headTracking.toggleButton = ParseBindingRef(
        ini.GetValue("HeadTracking", "iToggleButton", ""), -1);
    s_config.headTracking.yawAxis = ParseBindingRef(
        ini.GetValue("HeadTracking", "iLookYawAxis", ""), -1);
    s_config.headTracking.pitchAxis = ParseBindingRef(
        ini.GetValue("HeadTracking", "iLookPitchAxis", ""), -1);
    s_config.headTracking.rollAxis = ParseBindingRef(
        ini.GetValue("HeadTracking", "iLookRollAxis", ""), -1);
    s_config.headTracking.yawScale = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fYawScale", 1.0)), 0.05F, 10.0F);
    s_config.headTracking.pitchScale = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fPitchScale", 1.0)), 0.05F, 10.0F);
    s_config.headTracking.rollScale = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fRollScale", 1.0)), 0.05F, 10.0F);
    s_config.headTracking.maxYawDegrees = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fMaxYawDegrees", 85.0)), 1.0F, 180.0F);
    s_config.headTracking.maxPitchDegrees = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fMaxPitchDegrees", 60.0)), 1.0F, 180.0F);
    s_config.headTracking.maxRollDegrees = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fMaxRollDegrees", 45.0)), 1.0F, 180.0F);
    s_config.headTracking.deadzoneDegrees = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fDeadzoneDegrees", 0.0)), 0.0F, 20.0F);
    s_config.headTracking.joystickDeadzone = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fJoystickDeadzone", 0.08)), 0.0F, 0.95F);
    s_config.headTracking.smoothing = std::clamp(
        static_cast<float>(ini.GetDoubleValue("HeadTracking", "fSmoothing", 0.15)), 0.0F, 0.99F);
    s_config.headTracking.staleMilliseconds = std::clamp(
        static_cast<int>(ini.GetLongValue("HeadTracking", "iStaleMilliseconds", 500)), 50, 5000);
    s_config.headTracking.yawEnabled = ini.GetBoolValue("HeadTracking", "bYawEnabled", true);
    s_config.headTracking.pitchEnabled = ini.GetBoolValue("HeadTracking", "bPitchEnabled", true);
    s_config.headTracking.rollEnabled = ini.GetBoolValue("HeadTracking", "bRollEnabled", true);
    s_config.headTracking.invertYaw = ini.GetBoolValue("HeadTracking", "bInvertYaw", false);
    s_config.headTracking.invertPitch = ini.GetBoolValue("HeadTracking", "bInvertPitch", false);
    s_config.headTracking.invertRoll = ini.GetBoolValue("HeadTracking", "bInvertRoll", false);

    // [Gate] live pilot-state gate. InjectionOnly + Auto is the safe 5.0 default:
    // flight writes park after leaving the seat while buttons/macros remain usable.
    {
        const char* gm = ini.GetValue("Gate", "PilotGateMode", "InjectionOnly");
        if (_stricmp(gm, "InjectionOnly") == 0)  s_config.pilotGateMode = ThrottleController::GateMode::InjectionOnly;
        else if (_stricmp(gm, "Full") == 0)      s_config.pilotGateMode = ThrottleController::GateMode::Full;
        else                                     s_config.pilotGateMode = ThrottleController::GateMode::Off;
    }
    s_config.pilotGateManualToggleKey = (int)ini.GetLongValue("Gate", "iManualToggleKey", 0);
    {
        const char* ps = ini.GetValue("Gate", "PilotSignal", "Auto");
        s_config.pilotSignal = (_stricmp(ps, "Auto") == 0)
            ? ThrottleController::PilotSignal::Auto
            : ThrottleController::PilotSignal::Manual;
    }
    s_config.pilotLatchMilliseconds = std::clamp(
        static_cast<int>(ini.GetLongValue("Gate", "iPilotLatchMilliseconds", 5000)),
        500, 30000);

    s_config.digitalReverseButton     = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalReverseButton",     ""), -1);
    s_config.digitalRollLeftButton    = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalRollLeftButton",    ""), -1);
    s_config.digitalRollRightButton   = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalRollRightButton",   ""), -1);
    s_config.digitalStrafeLeftButton  = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeLeftButton",  ""), -1);
    s_config.digitalStrafeRightButton = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeRightButton", ""), -1);
    s_config.digitalStrafeUpButton    = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeUpButton",    ""), -1);
    s_config.digitalStrafeDownButton  = ParseBindingRef(ini.GetValue("DigitalAxes", "iDigitalStrafeDownButton",  ""), -1);
    s_config.digitalRollValue   = (float)ini.GetDoubleValue("DigitalAxes", "fDigitalRollValue",   1.0);
    s_config.digitalStrafeValue = (float)ini.GetDoubleValue("DigitalAxes", "fDigitalStrafeValue", 1.0);

    s_config.shipButtonsEnabled = ini.GetBoolValue("ShipButtons", "bShipButtonsEnabled", true);
    s_config.bUsePitchAxisForMenu = ini.GetBoolValue(
        "MenuControls", "bUsePitchAxisForNavigation", false);
    s_config.bUseYawAxisForMenu = ini.GetBoolValue(
        "MenuControls", "bUseYawAxisForNavigation", false);
    s_config.bUsePrimaryWeaponForMenuSelect = ini.GetBoolValue(
        "MenuControls", "bUsePrimaryWeaponForSelect", false);
    s_config.bInvertMenuVertical = ini.GetBoolValue(
        "MenuControls", "bInvertVerticalNavigation", false);
    s_config.bInvertMenuHorizontal = ini.GetBoolValue(
        "MenuControls", "bInvertHorizontalNavigation", false);
    s_config.fMenuAxisEngageThreshold = std::clamp(static_cast<float>(ini.GetDoubleValue(
        "MenuControls", "fAxisEngageThreshold", 0.55)), 0.35f, 0.95f);
    s_config.fMenuAxisReleaseThreshold = std::clamp(static_cast<float>(ini.GetDoubleValue(
        "MenuControls", "fAxisReleaseThreshold", 0.35)), 0.05f,
        s_config.fMenuAxisEngageThreshold - 0.05f);
    ShipOutputSystem::LoadShipButtonBindings(ini);
    MacroEngine::LoadMacros(ini);  // after ship bindings: action-id targets resolve here

    s_config.bSourceObjectAim  = ini.GetBoolValue("Aim", "bSourceObjectAim",  false);
    s_config.fAimSensitivity   = (float)ini.GetDoubleValue("Aim", "fAimSensitivity",   1.0);
    s_config.aimYawAxis        = ParseBindingRef(ini.GetValue("Aim", "iAimYawAxis",   nullptr), -1);
    s_config.aimPitchAxis      = ParseBindingRef(ini.GetValue("Aim", "iAimPitchAxis", nullptr), -1);
    s_config.fAimYawSensitivity   = (float)ini.GetDoubleValue("Aim", "fAimYawSensitivity",   1.0);
    s_config.fAimPitchSensitivity = (float)ini.GetDoubleValue("Aim", "fAimPitchSensitivity", 1.0);
    s_config.bInvertAimYaw     = ini.GetBoolValue("Aim", "bInvertAimYaw",   false);
    s_config.bInvertAimPitch   = ini.GetBoolValue("Aim", "bInvertAimPitch", false);
    s_config.fAimSmoothing     = std::clamp((float)ini.GetDoubleValue("Aim", "fAimSmoothing", 0.0), 0.0f, 1.0f);
    s_config.bMirrorFlightToAim = ini.GetBoolValue("Aim", "bMirrorFlightToAim", true);
    s_config.digitalAimLeftButton   = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimLeftButton",   nullptr), -1);
    s_config.digitalAimRightButton  = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimRightButton",  nullptr), -1);
    s_config.digitalAimUpButton     = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimUpButton",     nullptr), -1);
    s_config.digitalAimDownButton   = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimDownButton",   nullptr), -1);
    s_config.digitalAimCenterButton = ParseBindingRef(ini.GetValue("Aim", "iDigitalAimCenterButton", nullptr), -1);
    s_config.fDigitalAimValue   = (float)ini.GetDoubleValue("Aim", "fDigitalAimValue", 1.0);
    s_config.toggleAimModeButton = ParseBindingRef(ini.GetValue("Aim", "iToggleAimModeButton", nullptr), -1);
    s_config.bHOSAMMode        = ini.GetBoolValue("Aim", "bHOSAMMode",        false);
    s_config.bAlignmentAssist  = ini.GetBoolValue("Aim", "bAlignmentAssist",  false);
    s_config.fAlignmentRadius  = std::clamp((float)ini.GetDoubleValue("Aim", "fAlignmentRadius",  130.0), 0.0f, 200.0f);
    s_config.iAlignmentIdleMs  = std::clamp((int)ini.GetLongValue("Aim", "iAlignmentIdleMs",      50),  0, 2000);
    s_config.fAlignmentDecayRate = std::clamp((float)ini.GetDoubleValue("Aim", "fAlignmentDecayRate", 8.0), 0.1f, 50.0f);

    // Per-axis calibration overrides from [Calibration]
    s_config.axisCalibration.clear();
    CSimpleIniA::TNamesDepend calibKeys;
    ini.GetAllKeys("Calibration", calibKeys);
    for (const auto& entry : calibKeys) {
        const char* key = entry.pItem;
        if (strncmp(key, "iCalib_", 7) != 0) continue;
        int devIdx = -1, usage = -1;
        if (sscanf_s(key, "iCalib_%d_0x%x", &devIdx, &usage) == 2 && devIdx >= 0 && usage >= 0) {
            const char* val = ini.GetValue("Calibration", key, "");
            long cmin = 0, cmax = 65535;
            if (sscanf_s(val, "%ld,%ld", &cmin, &cmax) == 2 && cmin < cmax) {
                int calibKey = (devIdx << 8) | usage;
                s_config.axisCalibration[calibKey] = { cmin, cmax };
            }
        }
    }

    s_config.bAccumulatorThrottle = ini.GetBoolValue("DualStick", "bAccumulatorThrottle", false);
    s_config.fAccumulatorRate     = std::clamp((float)ini.GetDoubleValue("DualStick", "fAccumulatorRate",     1.0), 0.1f, 10.0f);
    s_config.fAccumulatorDecay    = std::clamp((float)ini.GetDoubleValue("DualStick", "fAccumulatorDecay",    0.0), 0.0f, 20.0f);
    s_config.fReverseGateVelocity = std::clamp((float)ini.GetDoubleValue("DualStick", "fReverseGateVelocity", 5.0), 0.0f, 100.0f);
    s_config.bAccumulatorTurnAssist = ini.GetBoolValue("DualStick", "bAccumulatorTurnAssist", false);
    s_config.iTurnAssistMode      = std::clamp((int)ini.GetLongValue("DualStick", "iTurnAssistMode", 0), 0, 2);
    s_config.turnAssistButton     = ParseBindingRef(ini.GetValue("DualStick", "iTurnAssistButton", ""), -1);
}

// ---- Profile switching ----

// Resolve every BindingRef in the active config, ship-button table, and macro set
// to a device index and open that device. Formerly the ResolveAll lambda inside
// ControlLoop; hoisted so PreloadProfiles can resolve each slot.
// Resolve one BindingRef to a device index and open that device. Shared by the
// active-config resolve below and per-slot trigger resolution in PreloadProfiles —
// profile triggers need this too, or a name-based trigger keeps deviceIndex = -1 and
// IsButtonPressed never sees it (the profile would never swap).
static void ResolveAndOpenRef(BindingRef& ref) {
    if (!ref.IsValid()) return;
    int resolvedIndex = -1;
    if (ref.HasIndex()) {
        if (ref.deviceIndex < DeviceManager::GetDeviceCount())
            resolvedIndex = ref.deviceIndex;
        else {
            char buf[256];
            sprintf_s(buf, "Warning: Device index #%d out of range (%d devices)",
                ref.deviceIndex, DeviceManager::GetDeviceCount());
            RuntimePaths::Log("[Controller]", buf);
        }
    } else if (ref.HasDevice()) {
        resolvedIndex = DeviceManager::ResolveByName(ref.deviceName);
    } else {
        resolvedIndex = DeviceManager::GetDeviceCount() > 0 ? 0 : -1;
    }
    if (resolvedIndex >= 0) {
        ref.deviceIndex = resolvedIndex;
        DeviceManager::OpenDevice(resolvedIndex);
    } else {
        char buf[256];
        sprintf_s(buf, "Warning: Could not resolve device: %s",
            ref.HasDevice() ? ref.deviceName.c_str() : "default fallback");
        RuntimePaths::Log("[Controller]", buf);
    }
}

void ThrottleController::ResolveActiveDevices() {
    auto ResolveAndOpen = ResolveAndOpenRef;  // shared resolver; call sites unchanged

    Config& cfg = s_config;
    ResolveAndOpen(cfg.throttleAxis);
    ResolveAndOpen(cfg.pitchAxis);
    ResolveAndOpen(cfg.yawAxis);
    ResolveAndOpen(cfg.rollAxis);
    ResolveAndOpen(cfg.strafeLatAxis);
    ResolveAndOpen(cfg.strafeVertAxis);
    ResolveAndOpen(cfg.reverseAxis);
    ResolveAndOpen(cfg.aimYawAxis);
    ResolveAndOpen(cfg.aimPitchAxis);
    ResolveAndOpen(cfg.activateButton);
    ResolveAndOpen(cfg.stopButton);
    ResolveAndOpen(cfg.toggleWizardButton);
    ResolveAndOpen(cfg.cruiseHoldButton);
    ResolveAndOpen(cfg.fullStopButton);
    ResolveAndOpen(cfg.cruiseHalfButton);
    ResolveAndOpen(cfg.cruiseMaxButton);
    ResolveAndOpen(cfg.digitalReverseButton);
    ResolveAndOpen(cfg.digitalRollLeftButton);
    ResolveAndOpen(cfg.digitalRollRightButton);
    ResolveAndOpen(cfg.digitalStrafeLeftButton);
    ResolveAndOpen(cfg.digitalStrafeRightButton);
    ResolveAndOpen(cfg.digitalStrafeUpButton);
    ResolveAndOpen(cfg.digitalStrafeDownButton);
    ResolveAndOpen(cfg.digitalAimLeftButton);
    ResolveAndOpen(cfg.digitalAimRightButton);
    ResolveAndOpen(cfg.digitalAimUpButton);
    ResolveAndOpen(cfg.digitalAimDownButton);
    ResolveAndOpen(cfg.digitalAimCenterButton);
    ResolveAndOpen(cfg.toggleAimModeButton);
    ResolveAndOpen(cfg.turnAssistButton);
    ResolveAndOpen(cfg.headTracking.recenterButton);
    ResolveAndOpen(cfg.headTracking.toggleButton);
    ResolveAndOpen(cfg.headTracking.yawAxis);
    ResolveAndOpen(cfg.headTracking.pitchAxis);
    ResolveAndOpen(cfg.headTracking.rollAxis);

    int count = ShipOutputSystem::GetShipButtonCount();
    for (int i = 0; i < count; i++)
        ResolveAndOpen(ShipOutputSystem::GetShipButtonBindings()[i].buttonRef);

    // Macro trigger buttons: resolved here (not in LoadMacros, which runs before
    // DeviceManager init) so name-based macro buttons get a device index and fire.
    for (Macro& m : MacroEngine::GetMacrosMutable())
        ResolveAndOpen(m.button);
}

// One [Profiles] slot definition. Discrete keys per slot (Slot<N>File /
// Slot<N>Button / Slot<N>Mode) rather than one packed value, because a device-name
// button ref contains spaces and could not be split from the value cleanly.
struct ProfileSlotDef {
    std::string file;              // resolved absolute path under Profiles/ (empty if base)
    BindingRef  trigger;
    int         triggerKey  = 0;   // keyboard VK, 0 = none
    int         triggerMods = 0;   // keyboard modifier chord: bit0 Ctrl, bit1 Shift, bit2 Alt
    int         shortcutKey = 0;   // independent [Profile] sKeyboardShortcut
    int         shortcutMods = 0;
    SwapMode    mode = SwapMode::Momentary;
};

// Parse a "key:" trigger value into a VK plus a modifier mask. Accepts a '+'-joined
// list of VK codes; Ctrl/Shift/Alt (0x11/0x10/0x12) fold into mods, the remaining VK
// is the key. So "key:0x11+0x31" = Ctrl+1. Modifiers avoid colliding with the game's
// own single-key bindings (F5/F9 quicksave/load, etc.).
static void ParseKeyTrigger(const char* value, int& outKey, int& outMods) {
    outKey = 0; outMods = 0;
    const char* p = value + 4;  // skip "key:"
    while (*p) {
        char* end = nullptr;
        const long vk = std::strtol(p, &end, 0);
        if (end == p) break;
        if      (vk == 0x11) outMods |= 1;  // Ctrl
        else if (vk == 0x10) outMods |= 2;  // Shift
        else if (vk == 0x12) outMods |= 4;  // Alt
        else                 outKey = (int)vk;
        p = end;
        while (*p == '+' || *p == ' ') ++p;
    }
}

static std::vector<ProfileSlotDef> ParseProfileSlots() {
    std::vector<ProfileSlotDef> out;
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(RuntimePaths::IniPath().string().c_str());
    CSimpleIniA custom;
    custom.SetUnicode(false);
    const auto customPath = RuntimePaths::CustomIniPath().string();
    if (custom.LoadFile(customPath.c_str()) == SI_OK
        && custom.GetLongValue("Meta", "iConfigVersion", -1) == 1)
        ini.LoadFile(customPath.c_str());

    for (int n = 1; n <= 16; ++n) {  // sparse slot numbering tolerated
        const std::string base = "Slot" + std::to_string(n);
        const char* file = ini.GetValue("Profiles", (base + "File").c_str(), nullptr);
        if (!file || !*file) continue;

        ProfileSlotDef def;
        // "(base)" selects the base config itself — a first-class swap position (e.g.
        // a rotary detent for base flight), not a profile file.
        if (_stricmp(file, "(base)") == 0 || _stricmp(file, "base") == 0) {
        } else {
            def.file = (RuntimePaths::ProfilesDir() / file).string();
            CSimpleIniA profileMeta;
            profileMeta.SetUnicode(false);
            if (profileMeta.LoadFile(def.file.c_str()) != SI_OK) continue;
            const char* kind = profileMeta.GetValue("Profile", "sKind", nullptr);
            const long version = profileMeta.GetLongValue("Profile", "iConfigVersion", -1);
            if (!kind || (_stricmp(kind, "full") != 0 && _stricmp(kind, "overlay") != 0)
                || version < 1 || version > 1) {
                CtrlLog("Ignored invalid or unsupported profile: " + def.file);
                continue;
            }
            const char* shortcut = profileMeta.GetValue("Profile", "sKeyboardShortcut", "");
            if (_strnicmp(shortcut, "key:", 4) == 0)
                ParseKeyTrigger(shortcut, def.shortcutKey, def.shortcutMods);
        }
        const char* trigger = ini.GetValue("Profiles", (base + "Button").c_str(), "");
        if (_strnicmp(trigger, "key:", 4) == 0)
            ParseKeyTrigger(trigger, def.triggerKey, def.triggerMods);
        else
            def.trigger = ParseBindingRef(trigger, -1);
        const char* mode = ini.GetValue("Profiles", (base + "Mode").c_str(), "momentary");
        if (_stricmp(mode, "toggle") == 0)        def.mode = SwapMode::Toggle;
        else if (_stricmp(mode, "selector") == 0) def.mode = SwapMode::Selector;
        else                                      def.mode = SwapMode::Momentary;
        out.push_back(std::move(def));
    }
    return out;
}

// Build, resolve, and snapshot every slot; leave the base profile active.
void ThrottleController::PreloadProfiles() {
    // Installation-wide route methods and ControlMap bindings are resolved once
    // before building snapshots. Individual profile files cannot alter them.
    ShipOutputSystem::RefreshRoutingInputs();
    const std::vector<ProfileSlotDef> defs = ParseProfileSlots();

    s_profiles.clear();
    s_profiles.reserve(defs.size() + 1);

    // Slot 0 = base: no slot file.
    {
        LoadConfig(nullptr);
        ResolveActiveDevices();
        ProfileSlot base;
        base.config       = s_config;
        base.shipBindings = ShipOutputSystem::SnapshotBindings();
        base.macros       = MacroEngine::SnapshotMacros();
        s_profiles.push_back(std::move(base));
    }

    // Slots 1..N = switch profiles, each a fourth overlay on the base stack.
    for (const ProfileSlotDef& def : defs) {
        ProfileSlot slot;
        if (def.file.empty()) {
            // A base-target slot IS base: copy slot 0's snapshot. Activating it restores
            // the base config, so the swap state machine treats it like any other slot —
            // no special-casing needed there. This is the rotary "base flight" detent.
            slot.config       = s_profiles[0].config;
            slot.shipBindings = s_profiles[0].shipBindings;
            slot.macros       = s_profiles[0].macros;
        } else {
            LoadConfig(&def.file);
            ResolveActiveDevices();
            slot.config       = s_config;
            slot.shipBindings = ShipOutputSystem::SnapshotBindings();
            slot.macros       = MacroEngine::SnapshotMacros();
        }
        slot.trigger      = def.trigger;
        slot.triggerKey   = def.triggerKey;
        slot.triggerMods  = def.triggerMods;
        slot.shortcutKey  = def.shortcutKey;
        slot.shortcutMods = def.shortcutMods;
        slot.mode         = def.mode;
        // Resolve the trigger's device too — it is not part of the active config, so
        // ResolveActiveDevices above never touched it. Without this a name-based
        // trigger stays deviceIndex = -1 and the profile can never swap.
        ResolveAndOpenRef(slot.trigger);
        s_profiles.push_back(std::move(slot));
    }

    CtrlLog("Preloaded " + std::to_string(s_profiles.size()) + " profile(s) (incl. base).");

    // Make base active without a release storm (nothing is held yet at preload).
    s_activeSlot = 0;
    s_lastSelectorPos = -2;
    s_cruiseAssistMode = CruiseAssistMode::Off;
    s_resetCruiseEdges = true;
    ApplyProfile(s_profiles[0], s_config);
}

// Swap the live configuration to a preloaded slot.
void ThrottleController::ActivateProfile(int slot) {
    if (slot < 0 || slot >= (int)s_profiles.size() || slot == s_activeSlot) return;

    // Release everything the outgoing profile was holding so no key sticks across
    // the swap, then copy the target snapshot into the live globals.
    ResetMenuControlReuse();
    ShipOutputSystem::ReleaseAllShipButtonOutputs();
    MacroEngine::ReleaseAll();
    s_cruiseAssistMode = CruiseAssistMode::Off;
    s_resetCruiseEdges = true;

    ApplyProfile(s_profiles[slot], s_config);

    // A button still physically down must not re-fire as its new-profile meaning.
    ShipOutputSystem::SeedDownButtonsConsumed();

    s_activeSlot = slot;
    s_configGeneration.fetch_add(1, std::memory_order_release);  // refresh a clean wizard snapshot
    CtrlLog("Profile swap -> slot " + std::to_string(slot));
}

// ---- Control Loop ----
void ThrottleController::ControlLoop() {
    CtrlLog("=== DirectInput Polling Loop Starting ===");

    if (!DeviceManager::Initialize()) {
        CtrlLog("Failed to initialize DeviceManager!");
        return;
    }
    DeviceManager::LogDeviceManifest();

    // Base + every switch profile: build, resolve, snapshot; base becomes active.
    PreloadProfiles();
    // AbsoluteHOTAS owns joystick capture and button edges for suite commands.
    // This remains optional: if no compatible daughter API is loaded, its saved
    // bindings simply stay dormant until that module becomes available.
    SuiteCommandBindings::Initialize();

    // Detect hardware range for the primary throttle axis
    long axisMin = 0, axisMax = 65535;
    if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.deviceIndex >= 0) {
        LPDIRECTINPUTDEVICE8 tDev = DeviceManager::OpenDevice(s_config.throttleAxis.deviceIndex);
        if (tDev) {
            DIPROPRANGE dipr;
            dipr.diph.dwSize       = sizeof(DIPROPRANGE);
            dipr.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            dipr.diph.dwHow        = DIPH_BYUSAGE;
            dipr.diph.dwObj        = s_config.throttleAxis.value;
            HRESULT hr = tDev->GetProperty(DIPROP_RANGE, &dipr.diph);
            if (FAILED(hr)) {
                dipr.diph.dwHow = DIPH_BYID;
                dipr.diph.dwObj = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0);
                hr = tDev->GetProperty(DIPROP_RANGE, &dipr.diph);
            }
            if (SUCCEEDED(hr)) {
                axisMin = dipr.lMin; axisMax = dipr.lMax;
                char buf[128]; sprintf_s(buf, "Hardware Range: [%ld, %ld]", axisMin, axisMax);
                CtrlLog(buf);
            } else {
                CtrlLog("Warning: Could not detect hardware range. Using 0-65535.");
            }
        }
    }
    // Override with calibration data if present
    if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.deviceIndex >= 0) {
        int calibKey = (s_config.throttleAxis.deviceIndex << 8) | s_config.throttleAxis.value;
        auto it = s_config.axisCalibration.find(calibKey);
        if (it != s_config.axisCalibration.end()) {
            axisMin = it->second.first; axisMax = it->second.second;
            char buf[128]; sprintf_s(buf, "Throttle using CALIBRATED range: [%ld, %ld]", axisMin, axisMax);
            CtrlLog(buf);
        }
    }

    auto sleepDuration = std::chrono::milliseconds(1000 / s_config.pollRateHz);
    uint64_t iter = 0;
    float lastInjectedHardwareValue = -999.0f;
    // Master runtime gate. When false, all plugin-owned outputs and axis injection
    // are suppressed; driven by the activate/stop bindings (and Ctrl+Alt+F8).
    // Initialized from bAlwaysOn so default users come up active, while users who
    // want manual control (bAlwaysOn=false) come up deactivated until they press
    // the activate binding.
    bool active = s_config.alwaysOn;
    bool wasActive = false;
    bool injectionWasAllowed = true;  // pilot gate (InjectionOnly): tracks memory-injection arm state
    bool overlayWasOpen = false;
    bool fullGateWasClosed = false;
    bool resetOverlayEdges = false;
    auto lastLoopTime = std::chrono::steady_clock::now();

    while (s_running) {
        iter++;

        auto nowTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(nowTime - lastLoopTime).count();
        dt = std::clamp(dt, 0.0001f, 0.1f);
        lastLoopTime = nowTime;

        // Hot-reload
        if (s_configReloadRequested.exchange(false)) {
            CtrlLog("=== CONFIG HOT-RELOAD ===");
            // Release anything the outgoing config was holding, then rebuild every
            // slot and return to base. (PreloadProfiles releases macro keys itself.)
            ResetMenuControlReuse();
            ShipOutputSystem::ReleaseAllShipButtonOutputs();
            PreloadProfiles();
            SuiteCommandBindings::Reload();
            sleepDuration = std::chrono::milliseconds(1000 / s_config.pollRateHz);
            // Publish AFTER the new config is fully applied so a wizard reading a
            // bumped generation is guaranteed to see the reloaded values, not a
            // half-applied state.
            s_configGeneration.fetch_add(1, std::memory_order_release);
            CtrlLog("=== HOT-RELOAD COMPLETE ===");
        }

        DeviceManager::PollAll();
        SuiteCommandBindings::Poll();

        // ---- Control buttons (always processed, even while deactivated, so the
        //      activate binding and wizard overlay can always be reached) ----
        bool curActivate    = DeviceManager::IsButtonPressed(s_config.activateButton);
        bool curStop        = DeviceManager::IsButtonPressed(s_config.stopButton);
        bool curToggleWizard = DeviceManager::IsButtonPressed(s_config.toggleWizardButton);
        static bool prevActivate = false, prevStop = false, prevToggleWizard = false;

        // Own the recovery chord on the controller thread rather than inside a
        // render callback. If another overlay temporarily bypasses our Present
        // hook, the open request is still recorded and can initialize as soon as
        // presentation returns. HookedWndProc receives only our posted message,
        // preventing the keyboard event from toggling twice after initialization.
        const bool curWizardChord = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
            && (GetAsyncKeyState(VK_MENU) & 0x8000) != 0
            && (GetAsyncKeyState('B') & 0x8000) != 0;
        static bool prevWizardChord = false;

        // Keyboard master on/off toggle (single configurable key; default ScrollLock).
        bool curToggleKey = s_config.toggleActiveKey != 0 &&
                            (GetAsyncKeyState(s_config.toggleActiveKey) & 0x8000) != 0;
        static bool prevToggleKey = false;
        const bool toggleEdge = curToggleKey && !prevToggleKey;
        prevToggleKey = curToggleKey;

        // Activate: enable injection + outputs and (re)arm discovery. Triggered by
        // the activate binding or the keyboard toggle when currently deactivated.
        if ((curActivate && !prevActivate) || (toggleEdge && !active)) {
            CtrlLog("[Master] ACTIVATE: enabling injection and outputs.");
            active = true;
            SignalHunter::ArmForReacquire();
            lastInjectedHardwareValue = -999.0f;
        }
        // Deactivate: full shutdown — release every held output and stop
        // axis injection. Triggered by the stop binding or the keyboard toggle when
        // currently active. Lets the user kill all ghost inputs on foot.
        else if ((curStop && !prevStop) || (toggleEdge && active)) {
            CtrlLog("[Master] DEACTIVATE: releasing all outputs and axis injection.");
            active = false;
            ShipOutputSystem::ReleaseAllShipButtonOutputs();
            MacroEngine::ReleaseAll();
            SignalHunter::Disarm();
            lastInjectedHardwareValue = -999.0f;
            s_cruiseAssistMode = CruiseAssistMode::Off;
            s_resetCruiseEdges = true;
            ActivateProfile(0);  // always return home; no-op if already on base
        }
        if ((curToggleWizard && !prevToggleWizard) ||
            (curWizardChord && !prevWizardChord)) {
            UIHook::ToggleUI();
        }

        prevActivate     = curActivate;
        prevStop         = curStop;
        prevToggleWizard = curToggleWizard;
        prevWizardChord  = curWizardChord;
        const bool overlayOpen = UIHook::IsUIOpen();
        if (overlayOpen) HeadTracking::PollPreview(s_config.headTracking);

        // ---- Profile switch triggers ----
        // Three activation modes coexist (a rig may use any mix): momentary and
        // toggle are edge-driven; selector is level-driven for rotary/detent switches.
        // Invariant: a switch profile is only active while armed and the overlay is
        // closed. While suppressed we snap to base and keep trigger state current
        // without acting, so a release under suppression can't strand the user and
        // resuming can't fire a spurious swap; the selector re-syncs on resume.
        {
            const bool swapEnabled = active && !overlayOpen;
            auto IsKeyTriggerDown = [](int key, int mods) {
                if (key <= 0 || !(GetAsyncKeyState(key) & 0x8000)) return false;
                if ((mods & 1) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return false;
                if ((mods & 2) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) return false;
                if ((mods & 4) && !(GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
                return true;
            };
            auto IsProfileTriggerDown = [&](const ProfileSlot& slot) {
                if (slot.triggerKey > 0) return IsKeyTriggerDown(slot.triggerKey, slot.triggerMods);
                return slot.trigger.IsValid() && DeviceManager::IsButtonPressed(slot.trigger);
            };
            if (!swapEnabled) {
                if (s_activeSlot != 0) ActivateProfile(0);
                for (size_t i = 1; i < s_profiles.size(); ++i) {
                    s_profiles[i].prevDown = IsProfileTriggerDown(s_profiles[i]);
                    s_profiles[i].prevShortcutDown = IsKeyTriggerDown(
                        s_profiles[i].shortcutKey, s_profiles[i].shortcutMods);
                }
                s_lastSelectorPos = -2;  // force selector re-sync to the physical position on resume
            } else {
                // The live "base selection": the selector's current position, else
                // slot 0. Momentary/toggle overrides return here, so a shift layered
                // over a rotary returns to the dial's position rather than slot 0.
                auto CurrentBaseSelection = [&]() -> int {
                    int b = 0;
                    for (size_t i = 1; i < s_profiles.size(); ++i)
                        if (s_profiles[i].mode == SwapMode::Selector && IsProfileTriggerDown(s_profiles[i]))
                            b = (int)i;
                    return b;
                };

                // Selector: read by level, but act only when the physical position
                // CHANGES, so an override on top is not stomped each tick. A gap with
                // no position held (break-before-make rotary) holds the last position.
                {
                    int pos = -1;
                    bool haveSelector = false;
                    for (size_t i = 1; i < s_profiles.size(); ++i) {
                        if (s_profiles[i].mode != SwapMode::Selector) continue;
                        haveSelector = true;
                        if (IsProfileTriggerDown(s_profiles[i]))
                            pos = (int)i;
                    }
                    if (haveSelector && pos != s_lastSelectorPos) {
                        s_lastSelectorPos = pos;
                        if (pos >= 0) ActivateProfile(pos);  // pos == -1: hold last, no swap
                    }
                }

                // Momentary / toggle: edge-driven.
                for (size_t i = 1; i < s_profiles.size(); ++i) {
                    ProfileSlot& sl = s_profiles[i];
                    if (sl.mode == SwapMode::Selector || (!sl.trigger.IsValid() && sl.triggerKey <= 0)) continue;

                    const bool down        = IsProfileTriggerDown(sl);
                    const bool pressEdge   = down && !sl.prevDown;
                    const bool releaseEdge = !down && sl.prevDown;
                    sl.prevDown = down;

                    if (sl.mode == SwapMode::Toggle) {
                        if (pressEdge)
                            ActivateProfile(s_activeSlot == (int)i ? CurrentBaseSelection() : (int)i);
                    } else {  // Momentary
                        if (pressEdge) {
                            sl.restoreSlot = s_activeSlot;  // per-slot so nesting unwinds correctly
                            ActivateProfile((int)i);
                        } else if (releaseEdge && s_activeSlot == (int)i) {
                            ActivateProfile(sl.restoreSlot);
                        }
                    }
                }

                // Profile-file keyboard shortcuts are toggle fallbacks and remain
                // active alongside the optional controller/custom trigger above.
                for (size_t i = 1; i < s_profiles.size(); ++i) {
                    ProfileSlot& sl = s_profiles[i];
                    const bool down = IsKeyTriggerDown(sl.shortcutKey, sl.shortcutMods);
                    const bool pressEdge = down && !sl.prevShortcutDown;
                    sl.prevShortcutDown = down;
                    if (pressEdge)
                        ActivateProfile(s_activeSlot == (int)i ? CurrentBaseSelection() : (int)i);
                }
            }
        }

        // Arm and tick cluster discovery before evaluating automatic pilot state.
        // This prevents a parked startup (for example, loading on foot) from
        // deadlocking discovery: the selected handler can still be acquired, its
        // first live output hit opens the gate, and only Inject remains parked.
        if (active && !wasActive) {
            if (s_config.alwaysOn) {
                CtrlLog("[Master] Active; discovery armed automatically.");
                SignalHunter::ArmForReacquire();
            }
            lastInjectedHardwareValue = -999.0f;
            wasActive = true;
        }
        if (active && !overlayOpen) {
            const int candidateCount = ThrottleHook::GetCandidateCount();
            SignalHunter::Tick(candidateCount, iter);
        }

        // ---- Pilot-state gate ----
        // Auto is driven by the selected flight handler's live output cadence.
        // Menus/loading report Suspended; they never trigger an OnFoot transition.
        if (s_config.pilotGateManualToggleKey != 0) {
            bool gateKeyDown = (GetAsyncKeyState(s_config.pilotGateManualToggleKey) & 0x8000) != 0;
            static bool prevGateKey = false;
            if (gateKeyDown && !prevGateKey) PilotState::Toggle();
            prevGateKey = gateKeyDown;
        }
        const bool automaticPilotSignal =
            s_config.pilotSignal == ThrottleController::PilotSignal::Auto;
        const auto pilotSnapshot = PilotState::Update(
            automaticPilotSignal, s_config.pilotLatchMilliseconds);
        const bool contextPiloting = pilotSnapshot.state == PilotState::State::Piloting;
        const bool gateOn = s_config.pilotGateMode != ThrottleController::GateMode::Off;
        const bool piloting = !gateOn || contextPiloting;
        const bool fullClosed =
            s_config.pilotGateMode == ThrottleController::GateMode::Full && !piloting;
        const bool menuContext = pilotSnapshot.gameplayContextKnown &&
            !pilotSnapshot.gameplayContextActive;
        const bool contextReuseAllowed = active && !fullClosed && !overlayOpen;
        UpdateMenuControlReuse(
            contextReuseAllowed && menuContext,
            contextReuseAllowed && pilotSnapshot.targetingModeActive);

        // ---- Master active gate ----
        // While deactivated, suppress all plugin-owned outputs and axis injection.
        // The deactivate edge above already released held keys; this transition
        // guard is a safety net for any other path that clears `active`.
        if (!active) {
            if (wasActive) {
                NativeShipControl::SetEnabled(false);
                HeadTracking::Suspend();
                ShipOutputSystem::ReleaseAllShipButtonOutputs();
                MacroEngine::ReleaseAll();
                SignalHunter::Disarm();
                lastInjectedHardwareValue = -999.0f;
                wasActive = false;
                injectionWasAllowed = false;
                s_cruiseAssistMode = CruiseAssistMode::Off;
                s_resetCruiseEdges = true;
            }
            fullGateWasClosed = false;
            std::this_thread::sleep_for(sleepDuration);
            continue;
        }

        // Full closes plugin-owned output without tearing down detection. Keeping
        // discovery alive is what lets a fresh selected-handler hit reopen the gate.
        if (fullClosed) {
            if (!fullGateWasClosed) {
                CtrlLog("[PilotState] Full gate suspended plugin output outside the pilot seat.");
                NativeShipControl::SetEnabled(false);
                HeadTracking::Suspend();
                ShipOutputSystem::ReleaseAllShipButtonOutputs();
                MacroEngine::ReleaseAll();
                SignalHunter::SuspendInjection();
                lastInjectedHardwareValue = -999.0f;
                injectionWasAllowed = false;
                s_cruiseAssistMode = CruiseAssistMode::Off;
                s_resetCruiseEdges = true;
            }
            fullGateWasClosed = true;
            std::this_thread::sleep_for(sleepDuration);
            continue;
        }
        if (fullGateWasClosed) {
            fullGateWasClosed = false;
            resetOverlayEdges = true;
            ShipOutputSystem::SeedDownButtonsConsumed();
            MacroEngine::SeedDownButtonsConsumed();
            lastInjectedHardwareValue = -999.0f;
            CtrlLog("[PilotState] Piloting resumed; full gate reopened.");
        }

        // The workbench is a capture/editing context, not a second flight-input
        // surface. Park every plugin-owned output before rendering it and keep
        // polling DirectInput only so capture and the close binding remain live.
        if (overlayOpen) {
            if (!overlayWasOpen) {
                CtrlLog("[Wizard] Parking gameplay injection and plugin-owned outputs.");
                ShipOutputSystem::ReleaseAllShipButtonOutputs();
                MacroEngine::ReleaseAll();
                NativeShipControl::SetEnabled(false);
                HeadTracking::Suspend();
                SignalHunter::SuspendInjection();
                injectionWasAllowed = false;
                s_resetCruiseEdges = true;
            }
            overlayWasOpen = true;
            std::this_thread::sleep_for(sleepDuration);
            continue;
        }
        if (overlayWasOpen) {
            overlayWasOpen = false;
            resetOverlayEdges = true;
            s_resetCruiseEdges = true;
            ShipOutputSystem::SeedDownButtonsConsumed();
            MacroEngine::SeedDownButtonsConsumed();
            lastInjectedHardwareValue = -999.0f;
            CtrlLog("[Wizard] Gameplay input resumed; held edge controls reseeded.");
        }

        // Native ship actions use the longer automatic latch even when the flight
        // injection gate is Off. Manual signal mode preserves the legacy override.
        const bool nativeShipContextAllowed = !automaticPilotSignal || contextPiloting;
        NativeShipControl::SetEnabled(
            s_config.bNativeShipControls && nativeShipContextAllowed);
        const bool flightInjectionAllowed = s_config.bEnableInjection &&
            ((s_config.pilotGateMode == ThrottleController::GateMode::Off) || piloting);

        // Ship action buttons are live only outside the wizard workbench.
        if (s_config.shipButtonsEnabled)
            ShipOutputSystem::UpdateShipButtonBindings();
        else
            ShipOutputSystem::ReleaseShipButtonBindingOutputs();

        MacroEngine::Update();
        NativeShipControl::PumpControllerThread();

        // ---- Reverse input ----
        const bool digitalReverseBound = s_config.digitalReverseButton.IsValid()
            && s_config.digitalReverseButton.value > 0
            && s_config.digitalReverseButton.value <= 128;
        const bool digitalReverseHeld  = digitalReverseBound && DeviceManager::IsButtonPressed(s_config.digitalReverseButton);

        auto ApplyUnipolarDeadzone = [](float value, float deadzone) {
            const float dz = std::clamp(deadzone, 0.0f, 0.95f);
            if (value <= dz) return 0.0f;
            return (value - dz) / (1.0f - dz);
        };

        float reverseAxis = 0.0f;
        if (s_config.reverseAxisEnabled && !digitalReverseBound
            && s_config.reverseAxis.IsValid() && s_config.reverseAxis.value > 0) {
            reverseAxis = (float)DeviceManager::GetRawAxis(s_config.reverseAxis) / 65535.0f;
            reverseAxis = s_config.bInvertReverse ? (1.0f - reverseAxis) : reverseAxis;
            reverseAxis = std::clamp(reverseAxis * s_config.fReverseSensitivity, 0.0f, 1.0f);
            reverseAxis = ApplyUnipolarDeadzone(reverseAxis, s_config.reverseDeadzone);
            reverseAxis = std::clamp(reverseAxis / s_config.fReverseSaturation, 0.0f, 1.0f);
        }

        bool reverseAxisHeld = reverseAxis > s_config.reverseActivationThreshold;
        bool reverseHeld     = digitalReverseHeld || reverseAxisHeld;

        // Unipolar reverse zone from main throttle axis
        if (s_config.bUnipolarReverse && s_config.unipolarMode
            && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            long rawThrottle = DeviceManager::GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            if (rawThrottle < s_config.reverseZoneCenter - s_config.reverseZoneDeadzone)
                reverseHeld = true;
        }

        // Boost zone: request the native booster action above boostZone+dz. Compute
        // inBoostZone unconditionally and always publish its ownership state so a
        // gate flip (for example, unbinding the throttle) cannot orphan the request.
        bool inBoostZone = false;
        if (flightInjectionAllowed && s_config.bBoostZone && s_config.bUnipolarReverse
            && s_config.unipolarMode
            && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            long rawThrottle = DeviceManager::GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            inBoostZone = rawThrottle > s_config.boostZoneCenter + s_config.boostZoneDeadzone;
        }
        NativeShipControl::SetActionHeld(
            NativeShipControl::Action::FireBoosters, OwnerBoostZone, inBoostZone);

        // ---- Throttle ----
        float throttle = 0.0f;
        auto NormBipolarRate = [&](long rawValue) -> float {
            long center   = s_config.detentCenter;
            long deadzone = s_config.detentDeadzone;
            if (rawValue < axisMin) rawValue = axisMin;
            if (rawValue > axisMax) rawValue = axisMax;
            if (s_config.bInvertThrottle) rawValue = axisMin + axisMax - rawValue;
            if (rawValue >= center - deadzone && rawValue <= center + deadzone) return 0.0f;
            if (rawValue > center + deadzone) {
                float range = (float)(axisMax - (center + deadzone));
                return (range <= 0.0f) ? 0.0f : (float)(rawValue - (center + deadzone)) / range;
            } else {
                float range = (float)((center - deadzone) - axisMin);
                return (range <= 0.0f) ? 0.0f : -1.0f + (float)(rawValue - axisMin) / range;
            }
        };

        if (s_config.bAccumulatorThrottle) {
            if (s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
                throttle = NormBipolarRate(DeviceManager::GetRawAxis(s_config.throttleAxis));
                throttle = std::clamp(throttle * s_config.fThrottleSensitivity, -1.0f, 1.0f);
            }
        } else if (!reverseHeld && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            throttle = NormalizeAxis(DeviceManager::GetRawAxis(s_config.throttleAxis), axisMin, axisMax);
            throttle = std::clamp(throttle * s_config.fThrottleSensitivity, 0.0f, 1.0f);
        }

        // ---- Native flight-assist controls ----
        // One mutually-exclusive latched target. Pressing the active command again
        // returns throttle authority to the hardware axis.
        static bool prevCruise[4]{};
        const BindingRef* cruiseButtons[] = {
            &s_config.cruiseHoldButton, &s_config.fullStopButton,
            &s_config.cruiseHalfButton, &s_config.cruiseMaxButton
        };
        bool cruiseDown[4]{};
        for (int i = 0; i < 4; ++i) cruiseDown[i] = DeviceManager::IsButtonPressed(*cruiseButtons[i]);
        if (s_resetCruiseEdges) {
            for (int i = 0; i < 4; ++i) prevCruise[i] = cruiseDown[i];
            s_resetCruiseEdges = false;
        } else if (!overlayOpen) {
            const CruiseAssistMode modes[] = {
                CruiseAssistMode::HoldCurrent, CruiseAssistMode::Stop,
                CruiseAssistMode::Half, CruiseAssistMode::Max
            };
            const float targets[] = {0.0f, 0.0f, 0.5f, 1.0f};
            for (int i = 0; i < 4; ++i) {
                if (!cruiseDown[i] || prevCruise[i]) continue;
                if (s_cruiseAssistMode == modes[i]) {
                    s_cruiseAssistMode = CruiseAssistMode::Off;
                } else {
                    s_cruiseAssistMode = modes[i];
                    s_cruiseAssistTarget = modes[i] == CruiseAssistMode::HoldCurrent
                        ? (s_config.bAccumulatorThrottle ? SignalHunter::GetCurrentThrottleTarget() : throttle)
                        : targets[i];
                }
                break;
            }
        }
        for (int i = 0; i < 4; ++i) prevCruise[i] = cruiseDown[i];

        const bool cruiseOverride = s_cruiseAssistMode != CruiseAssistMode::Off;
        if (cruiseOverride) {
            throttle = s_cruiseAssistTarget;
            reverseHeld = false;
            NativeShipControl::SetActionHeld(
                NativeShipControl::Action::FireBoosters, OwnerBoostZone, false);
        }

        // ---- Rotation axes ----
        // Raw bipolar normalization: maps the calibrated axis range to [-1,+1]
        // with center at 0. NO sensitivity/saturation/deadzone — that shaping is
        // done by ShapeAxis so the deadzone and saturation behave as ABSOLUTE
        // positions on the physical axis, matching exactly what the wizard draws.
        auto NormRaw = [&](const BindingRef& ref, bool invert) -> float {
            if (!ref.IsValid() || ref.value <= 0) return 0.0f;
            float raw  = static_cast<float>(DeviceManager::GetRawAxis(ref));
            float aMin = 0.0f, aMax = 65535.0f;
            if (ref.deviceIndex >= 0) {
                int calibKey = (ref.deviceIndex << 8) | ref.value;
                auto it = s_config.axisCalibration.find(calibKey);
                if (it != s_config.axisCalibration.end()) {
                    aMin = static_cast<float>(it->second.first);
                    aMax = static_cast<float>(it->second.second);
                }
            }
            float center = (aMin + aMax) / 2.0f, halfRange = (aMax - aMin) / 2.0f;
            if (halfRange <= 0.0f) return 0.0f;
            float n = (raw - center) / halfRange;
            return invert ? -n : n;
        };

        // Shape a raw normalized value [-1,1] into output [-1,1] using absolute
        // axis zones:
        //   |n| <= dz        -> 0          (deadzone edge sits at dz of the axis)
        //   dz < |n| < sat   -> linear ramp 0..1
        //   |n| >= sat       -> +/-1       (saturation edge sits at sat of the axis)
        // Because dz and sat are absolute axis fractions (no cross-multiplication),
        // the wizard's drawn zones are literally where these activate in flight.
        // Sensitivity is a final output gain.
        auto ShapeAxis = [](float n, float sens, float sat, float dz) -> float {
            const float d = std::clamp(dz, 0.0f, 0.95f);
            const float s = std::clamp(sat, 0.05f, 1.0f);
            const float mag = std::abs(n);
            if (mag <= d) return 0.0f;
            const float span = std::max(s - d, 1e-4f);   // guard against sat <= dz
            const float ramp = std::clamp((mag - d) / span, 0.0f, 1.0f);
            const float sign = n < 0.0f ? -1.0f : 1.0f;
            return std::clamp(sign * ramp * sens, -1.0f, 1.0f);
        };

        float pitch  = ShapeAxis(NormRaw(s_config.pitchAxis, s_config.bInvertPitch), s_config.fPitchSensitivity, s_config.fPitchSaturation, s_config.fPitchDeadzone);
        float yaw    = ShapeAxis(NormRaw(s_config.yawAxis,   s_config.bInvertYaw),   s_config.fYawSensitivity,   s_config.fYawSaturation,   s_config.fYawDeadzone);
        float roll   = ShapeAxis(NormRaw(s_config.rollAxis,  s_config.bInvertRoll),  s_config.fRollSensitivity,  s_config.fRollSaturation,  s_config.fRollDeadzone);
        if (DeviceManager::IsButtonPressed(s_config.digitalRollLeftButton))  roll -= s_config.digitalRollValue;
        if (DeviceManager::IsButtonPressed(s_config.digitalRollRightButton)) roll += s_config.digitalRollValue;
        roll = std::clamp(roll, -1.0f, 1.0f);

        // Raw normalized magnitudes drive the strafe ACTIVATION gate; ShapeAxis
        // turns the same raw value into the strafe amount. Both reference the
        // deadzone as an absolute axis position, so they engage together.
        const float strafeLatNorm  = NormRaw(s_config.strafeLatAxis,  s_config.bInvertStrafeLat);
        const float strafeVertNorm = NormRaw(s_config.strafeVertAxis, s_config.bInvertStrafeVert);
        float strafeX = ShapeAxis(strafeLatNorm,  s_config.fStrafeSensitivity, s_config.fStrafeSaturation,     s_config.fStrafeDeadzone);
        float strafeY = ShapeAxis(strafeVertNorm, s_config.fStrafeSensitivity, s_config.fStrafeVertSaturation, s_config.fStrafeVertDeadzone);

        const bool digStrafeLeft  = DeviceManager::IsButtonPressed(s_config.digitalStrafeLeftButton);
        const bool digStrafeRight = DeviceManager::IsButtonPressed(s_config.digitalStrafeRightButton);
        const bool digStrafeUp    = DeviceManager::IsButtonPressed(s_config.digitalStrafeUpButton);
        const bool digStrafeDown  = DeviceManager::IsButtonPressed(s_config.digitalStrafeDownButton);
        if (digStrafeLeft)  strafeX -= s_config.digitalStrafeValue;
        if (digStrafeRight) strafeX += s_config.digitalStrafeValue;
        if (digStrafeUp)    strafeY += s_config.digitalStrafeValue;
        if (digStrafeDown)  strafeY -= s_config.digitalStrafeValue;
        strafeX = std::clamp(strafeX, -1.0f, 1.0f);
        strafeY = std::clamp(strafeY, -1.0f, 1.0f);

        // Strafe activation gate. Tested against the PRE-deadzone magnitude (or a
        // digital button) so the deadzone isn't double-counted; the 0.05 floor is a
        // fixed noise gate so jitter never fires the native flight-mode modifier,
        // even when the configured deadzone is 0.
        const float strafeActThreshX = std::max(0.05f, s_config.fStrafeDeadzone);
        const float strafeActThreshY = std::max(0.05f, s_config.fStrafeVertDeadzone);
        const bool strafeLatActive  = std::abs(strafeLatNorm)  > strafeActThreshX || digStrafeLeft || digStrafeRight;
        const bool strafeVertActive = std::abs(strafeVertNorm) > strafeActThreshY || digStrafeUp   || digStrafeDown;
        NativeShipControl::SetActionHeld(
            NativeShipControl::Action::SwitchFlightModes, OwnerStrafeModifier,
            flightInjectionAllowed && (strafeLatActive || strafeVertActive));

        // ---- Aim mode toggle ----
        {
            static bool s_aimModeOverride = false;
            static bool s_toggleAimModePrev = false;
            bool curToggleAimMode = DeviceManager::IsButtonPressed(s_config.toggleAimModeButton);
            if (resetOverlayEdges) s_toggleAimModePrev = curToggleAimMode;
            if (curToggleAimMode && !s_toggleAimModePrev) {
                s_aimModeOverride = !s_aimModeOverride;
                RuntimePaths::Log("[Controller]",
                    s_aimModeOverride ? "[Aim] Toggled to: Aim-Driven Steering"
                                      : "[Aim] Toggled to: Independent Aim & Steer");
            }
            s_toggleAimModePrev = curToggleAimMode;

            bool hasDigitalAimButtons = s_config.digitalAimLeftButton.IsValid()
                                     || s_config.digitalAimRightButton.IsValid()
                                     || s_config.digitalAimUpButton.IsValid()
                                     || s_config.digitalAimDownButton.IsValid();
            bool hasSeparateAimAxes  = s_config.aimYawAxis.IsValid() || s_config.aimPitchAxis.IsValid();
            bool hasSeparateAimInput = hasSeparateAimAxes || hasDigitalAimButtons;
            if (s_aimModeOverride) hasSeparateAimInput = false;

            bool suppressForHOSAM      = s_config.bHOSAMMode;
            bool suppressClusterForAim = suppressForHOSAM ||
                (s_config.bSourceObjectAim && !hasSeparateAimInput);

            // ---- Turn Assist Button State ----
            // Compute whether turn assist is active this frame.
            // bAccumulatorTurnAssist (INI) is the master switch.
            // Mode 0=Always, 1=Hold (need button held), 2=Toggle (button toggles on/off).
            static bool s_turnAssistToggled = false;
            static bool s_prevTurnAssistBtn = false;
            bool assistMasterEnabled = s_config.bAccumulatorTurnAssist;
            if (assistMasterEnabled && s_config.iTurnAssistMode > 0) {
                bool btnBound = s_config.turnAssistButton.IsValid()
                    && s_config.turnAssistButton.value > 0
                    && s_config.turnAssistButton.value <= 128;
                bool btnHeld = btnBound && DeviceManager::IsButtonPressed(s_config.turnAssistButton);
                if (resetOverlayEdges) s_prevTurnAssistBtn = btnHeld;
                if (s_config.iTurnAssistMode == 1) {
                    s_turnAssistRuntimeActive = btnHeld;
                } else {
                    if (btnHeld && !s_prevTurnAssistBtn) s_turnAssistToggled = !s_turnAssistToggled;
                    s_turnAssistRuntimeActive = s_turnAssistToggled;
                }
                s_prevTurnAssistBtn = btnHeld;
            } else {
                // Mode 0 (Always): active whenever master switch is on
                s_turnAssistRuntimeActive = assistMasterEnabled;
            }
            resetOverlayEdges = false;

            // ---- SignalHunter / AimController (pilot-gated injection) ----
            // Discovery already ticked above regardless of gate state. InjectionOnly
            // parks axis/head injection while leaving raw buttons and macros live.
            //
            // bEnableInjection is the per-profile switch for the same thing: a "parked"
            // profile sets it false to disable flight-axis, aim, and head-pose injection
            // while its buttons/macros keep working. It rides the config swap, so
            // landing on such a profile parks injection via the same transition path as
            // the pilot gate — no separate release logic. See profile-switching.md.
            if (flightInjectionAllowed) {
                injectionWasAllowed = true;
                SignalHunter::Inject(throttle, pitch, yaw, roll, strafeX, strafeY, dt, reverseHeld, suppressClusterForAim,
                                     strafeLatActive, strafeVertActive, cruiseOverride, s_cruiseAssistTarget);
                AimController::Update(s_config, yaw, pitch,
                    hasSeparateAimInput, hasSeparateAimAxes, hasDigitalAimButtons, dt);
            } else if (injectionWasAllowed) {
                // Transition to parked: stop axis/head injection, keep discrete bindings live.
                SignalHunter::SuspendInjection();
                injectionWasAllowed = false;
            }
            HeadTracking::Update(s_config.headTracking, s_config.axisCalibration,
                                 dt, flightInjectionAllowed &&
                                     pilotSnapshot.headTrackingAllowed);
        }

        std::this_thread::sleep_for(sleepDuration);
    }

    ShipOutputSystem::ReleaseAllShipButtonOutputs();
    MacroEngine::ReleaseAll();
    NativeShipControl::SetEnabled(false);
    HeadTracking::Shutdown();
    SuiteCommandBindings::Shutdown();
    DeviceManager::Shutdown();
}

// ---- Public API ----
bool ThrottleController::Initialize() {
    LoadConfig();
    return s_config.enabled;
}

void ThrottleController::ReloadConfig() {
    s_configReloadRequested.store(true, std::memory_order_release);
    CtrlLog("Config reload requested (will apply on next loop iteration).");
}

uint32_t ThrottleController::ConfigGeneration() {
    return s_configGeneration.load(std::memory_order_acquire);
}

void ThrottleController::Start() {
    if (s_running) return;
    s_running = true;
    ThrottleHook::SetCaptureEnabled(false);
    s_thread = std::thread(ControlLoop);
    s_thread.detach();
    CtrlLog("Signal Hunter thread launched.");
}

void ThrottleController::Stop() {
    s_running = false;
    ShipOutputSystem::ReleaseAllShipButtonOutputs();
    MacroEngine::ReleaseAll();
    NativeShipControl::SetEnabled(false);
    HeadTracking::Shutdown();
}

ThrottleController::Config& ThrottleController::GetConfig() { return s_config; }
bool ThrottleController::IsTurnAssistActive() { return s_turnAssistRuntimeActive; }

std::vector<ThrottleController::ShipActionInfo> ThrottleController::GetShipActionBindings() {
    std::vector<ShipActionInfo> result;
    int count = ShipOutputSystem::GetShipButtonCount();
    for (int i = 0; i < count && i < static_cast<int>(kShipActionCatalog.size()); i++) {
        const auto& b = ShipOutputSystem::GetShipButtonBindings()[i];
        result.push_back({ kShipActionCatalog[i].displayLabel.data(),
            b.sourceIniKey, b.buttonRef });
    }
    return result;
}

const std::unordered_map<int, std::pair<long, long>>& ThrottleController::GetCalibrationData() {
    return s_config.axisCalibration;
}

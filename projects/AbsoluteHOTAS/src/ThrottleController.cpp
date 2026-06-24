#include "PCH.h"
#include "ThrottleController.h"
#include "ThrottleHook.h"
#include "ShipOutput.h"
#include "SignalHunter.h"
#include "AimController.h"
#include "RuntimePaths.h"
#include "DeviceManager.h"
#include "UIHook.h"
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <chrono>
#include <SimpleIni.h>



// ---- Static member definitions ----
ThrottleController::Config    ThrottleController::s_config;
std::atomic<bool>  ThrottleController::s_running{ false };
std::atomic<bool>  ThrottleController::s_configReloadRequested{ false };
std::atomic<float> ThrottleController::s_currentThrottle{ 0.0f };
std::thread        ThrottleController::s_thread;

static bool g_verboseLog = false;
static bool s_turnAssistRuntimeActive = false;

// ---- Logging ----
static void CtrlLog(const char* msg) {
    if (!g_verboseLog) return;
    RuntimePaths::AppendLogAlways("[Controller]", msg);
}
static void CtrlLog(const std::string& msg) { CtrlLog(msg.c_str()); }

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
void ThrottleController::LoadConfig() {
    CSimpleIniA ini;
    ini.SetUnicode();
    const auto path = RuntimePaths::IniPath().string();
    if (ini.LoadFile(path.c_str()) == SI_OK)
        CtrlLog("Loaded config from: " + path);
    else
        CtrlLog("No AbsoluteHOTAS.ini found, using defaults.");

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
    s_config.logThrottle       = ini.GetBoolValue("Injection", "bLogThrottle",     false);
    g_verboseLog               = s_config.logThrottle;
    s_config.bHoldForBoost     = ini.GetBoolValue("Injection", "bHoldForBoost",    true);

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
    ShipOutputSystem::LoadShipButtonBindings(ini);

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

    {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "[Aim] bSourceObjectAim=%s fAimSensitivity=%.2f aimYaw=%d aimPitch=%d mirror=%s HOSAM=%s align=%s",
            s_config.bSourceObjectAim ? "true" : "false",
            s_config.fAimSensitivity,
            s_config.aimYawAxis.value,
            s_config.aimPitchAxis.value,
            s_config.bMirrorFlightToAim ? "true" : "false",
            s_config.bHOSAMMode ? "true" : "false",
            s_config.bAlignmentAssist ? "true" : "false");
        RuntimePaths::AppendLogAlways("[Controller]", msg);
    }

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
                char logBuf[128];
                sprintf_s(logBuf, "Calibration: dev=%d axis=0x%02X range=[%ld, %ld]", devIdx, usage, cmin, cmax);
                CtrlLog(logBuf);
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
    if (s_config.bAccumulatorThrottle) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[DualStick] Accumulator ON: rate=%.2f decay=%.2f reverseGate=%.1f m/s turnAssist=%s mode=%d",
            s_config.fAccumulatorRate, s_config.fAccumulatorDecay, s_config.fReverseGateVelocity,
            s_config.bAccumulatorTurnAssist ? "true" : "false", s_config.iTurnAssistMode);
        RuntimePaths::AppendLogAlways("[Controller]", buf);
    }

    CtrlLog("Config Loaded - AbsoluteHOTAS 6DOF Dashboard Initialized.");
}

// ---- Control Loop ----
void ThrottleController::ControlLoop() {
    CtrlLog("=== DirectInput Polling Loop Starting ===");

    if (!DeviceManager::Initialize()) {
        CtrlLog("Failed to initialize DeviceManager!");
        return;
    }
    DeviceManager::LogDeviceManifest();

    // Resolve a BindingRef to a device index and open that device.
    auto ResolveAll = [](Config& cfg) {
        auto ResolveAndOpen = [](BindingRef& ref) {
            if (!ref.IsValid()) return;
            int resolvedIndex = -1;
            if (ref.HasIndex()) {
                if (ref.deviceIndex < DeviceManager::GetDeviceCount())
                    resolvedIndex = ref.deviceIndex;
                else {
                    char buf[256];
                    sprintf_s(buf, "Warning: Device index #%d out of range (%d devices)",
                        ref.deviceIndex, DeviceManager::GetDeviceCount());
                    RuntimePaths::AppendLogAlways("[Controller]", buf);
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
                RuntimePaths::AppendLogAlways("[Controller]", buf);
            }
        };

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

        int count = ShipOutputSystem::GetShipButtonCount();
        for (int i = 0; i < count; i++)
            ResolveAndOpen(ShipOutputSystem::GetShipButtonBindings()[i].buttonRef);
    };

    ResolveAll(s_config);

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
    // Master runtime gate. When false, ALL SendInput outputs and axis injection
    // are suppressed; driven by the activate/stop bindings (and Ctrl+Alt+F8).
    // Initialized from bAlwaysOn so default users come up active, while users who
    // want manual control (bAlwaysOn=false) come up deactivated until they press
    // the activate binding.
    bool active = s_config.alwaysOn;
    bool wasActive = false;
    auto lastLoopTime = std::chrono::steady_clock::now();

    // Inline axis reading helper — delegates to DeviceManager
    auto GetRawAxis = [](const BindingRef& ref) -> long {
        return DeviceManager::GetRawAxis(ref);
    };

    auto IsButtonPressed = [](const BindingRef& ref) -> bool {
        return DeviceManager::IsButtonPressed(ref);
    };

    while (s_running) {
        iter++;

        auto nowTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(nowTime - lastLoopTime).count();
        dt = std::clamp(dt, 0.0001f, 0.1f);
        lastLoopTime = nowTime;

        // Hot-reload
        if (s_configReloadRequested.exchange(false)) {
            CtrlLog("=== CONFIG HOT-RELOAD ===");
            LoadConfig();
            ResolveAll(s_config);
            sleepDuration = std::chrono::milliseconds(1000 / s_config.pollRateHz);
            CtrlLog("=== HOT-RELOAD COMPLETE ===");
        }

        DeviceManager::PollAll();

        // ---- Control buttons (always processed, even while deactivated, so the
        //      activate binding and wizard overlay can always be reached) ----
        bool curActivate    = IsButtonPressed(s_config.activateButton);
        bool curStop        = IsButtonPressed(s_config.stopButton);
        bool curToggleWizard = IsButtonPressed(s_config.toggleWizardButton);
        static bool prevActivate = false, prevStop = false, prevToggleWizard = false;

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
            SignalHunter::ArmForReacquire("manual activate");
            lastInjectedHardwareValue = -999.0f;
        }
        // Deactivate: full shutdown — release every held SendInput output AND stop
        // axis injection. Triggered by the stop binding or the keyboard toggle when
        // currently active. Lets the user kill all ghost inputs on foot.
        else if ((curStop && !prevStop) || (toggleEdge && active)) {
            CtrlLog("[Master] DEACTIVATE: releasing all outputs and axis injection.");
            active = false;
            ShipOutputSystem::ReleaseAllShipButtonOutputs();
            SignalHunter::Disarm();
            lastInjectedHardwareValue = -999.0f;
        }
        if (curToggleWizard && !prevToggleWizard) UIHook::ToggleUI();

        prevActivate     = curActivate;
        prevStop         = curStop;
        prevToggleWizard = curToggleWizard;

        // ---- Master active gate ----
        // While deactivated, suppress ALL SendInput outputs and axis injection.
        // The deactivate edge above already released held keys; this transition
        // guard is a safety net for any other path that clears `active`.
        if (!active) {
            if (wasActive) {
                ShipOutputSystem::ReleaseAllShipButtonOutputs();
                SignalHunter::Disarm();
                lastInjectedHardwareValue = -999.0f;
                wasActive = false;
            }
            std::this_thread::sleep_for(sleepDuration);
            continue;
        }

        // ---- Entering active state (covers bAlwaysOn startup auto-arm) ----
        if (!wasActive) {
            if (s_config.alwaysOn) {
                CtrlLog("[Master] Active; discovery armed automatically.");
                SignalHunter::ArmForReacquire(nullptr);
            }
            lastInjectedHardwareValue = -999.0f;
            wasActive = true;
        }

        // Ship action buttons: gated by bShipButtonsEnabled and suppressed while
        // the wizard overlay is open. Releases only ship-button-owned outputs so
        // the axis-driven strafe/boost modifiers below are left untouched.
        if (s_config.shipButtonsEnabled && !UIHook::IsUIOpen())
            ShipOutputSystem::UpdateShipButtonBindings();
        else
            ShipOutputSystem::ReleaseShipButtonBindingOutputs();

        // ---- Reverse input ----
        const bool digitalReverseBound = s_config.digitalReverseButton.IsValid()
            && s_config.digitalReverseButton.value > 0
            && s_config.digitalReverseButton.value <= 128;
        const bool digitalReverseHeld  = digitalReverseBound && IsButtonPressed(s_config.digitalReverseButton);

        auto ApplyUnipolarDeadzone = [](float value, float deadzone) {
            const float dz = std::clamp(deadzone, 0.0f, 0.95f);
            if (value <= dz) return 0.0f;
            return (value - dz) / (1.0f - dz);
        };

        float reverseAxis = 0.0f;
        if (s_config.reverseAxisEnabled && !digitalReverseBound
            && s_config.reverseAxis.IsValid() && s_config.reverseAxis.value > 0) {
            reverseAxis = (float)GetRawAxis(s_config.reverseAxis) / 65535.0f;
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
            long rawThrottle = GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            if (rawThrottle < s_config.reverseZoneCenter - s_config.reverseZoneDeadzone)
                reverseHeld = true;
        }

        // Boost zone: fire boosters when throttle axis is above boostZone+dz
        if (s_config.bBoostZone && s_config.bUnipolarReverse && s_config.unipolarMode
            && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0
            && ShipOutputSystem::GetShipButtonCount() > 0) {
            long rawThrottle = GetRawAxis(s_config.throttleAxis);
            if (s_config.bInvertThrottle) rawThrottle = axisMin + axisMax - rawThrottle;
            bool inBoostZone = rawThrottle > s_config.boostZoneCenter + s_config.boostZoneDeadzone;
            ShipOutputSystem::SetOutputHeld(
                ShipOutputSystem::GetShipButtonOutput("FireBoosters"), OwnerBoostZone, inBoostZone);
        }

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
                throttle = NormBipolarRate(GetRawAxis(s_config.throttleAxis));
                throttle = std::clamp(throttle * s_config.fThrottleSensitivity, -1.0f, 1.0f);
            }
        } else if (!reverseHeld && s_config.throttleAxis.IsValid() && s_config.throttleAxis.value > 0) {
            throttle = NormalizeAxis(GetRawAxis(s_config.throttleAxis), axisMin, axisMax);
            throttle = std::clamp(throttle * s_config.fThrottleSensitivity, 0.0f, 1.0f);
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
        if (IsButtonPressed(s_config.digitalRollLeftButton))  roll -= s_config.digitalRollValue;
        if (IsButtonPressed(s_config.digitalRollRightButton)) roll += s_config.digitalRollValue;
        roll = std::clamp(roll, -1.0f, 1.0f);

        // Raw normalized magnitudes drive the strafe ACTIVATION gate; ShapeAxis
        // turns the same raw value into the strafe amount. Both reference the
        // deadzone as an absolute axis position, so they engage together.
        const float strafeLatNorm  = NormRaw(s_config.strafeLatAxis,  s_config.bInvertStrafeLat);
        const float strafeVertNorm = NormRaw(s_config.strafeVertAxis, s_config.bInvertStrafeVert);
        float strafeX = ShapeAxis(strafeLatNorm,  s_config.fStrafeSensitivity, s_config.fStrafeSaturation,     s_config.fStrafeDeadzone);
        float strafeY = ShapeAxis(strafeVertNorm, s_config.fStrafeSensitivity, s_config.fStrafeVertSaturation, s_config.fStrafeVertDeadzone);

        const bool digStrafeLeft  = IsButtonPressed(s_config.digitalStrafeLeftButton);
        const bool digStrafeRight = IsButtonPressed(s_config.digitalStrafeRightButton);
        const bool digStrafeUp    = IsButtonPressed(s_config.digitalStrafeUpButton);
        const bool digStrafeDown  = IsButtonPressed(s_config.digitalStrafeDownButton);
        if (digStrafeLeft)  strafeX -= s_config.digitalStrafeValue;
        if (digStrafeRight) strafeX += s_config.digitalStrafeValue;
        if (digStrafeUp)    strafeY += s_config.digitalStrafeValue;
        if (digStrafeDown)  strafeY -= s_config.digitalStrafeValue;
        strafeX = std::clamp(strafeX, -1.0f, 1.0f);
        strafeY = std::clamp(strafeY, -1.0f, 1.0f);

        // Strafe activation gate. Tested against the PRE-deadzone magnitude (or a
        // digital button) so the deadzone isn't double-counted; the 0.05 floor is a
        // fixed noise gate so jitter never fires the Space modifier (which locks
        // roll) even when the configured deadzone is 0.
        const float strafeActThreshX = std::max(0.05f, s_config.fStrafeDeadzone);
        const float strafeActThreshY = std::max(0.05f, s_config.fStrafeVertDeadzone);
        const bool strafeLatActive  = std::abs(strafeLatNorm)  > strafeActThreshX || digStrafeLeft || digStrafeRight;
        const bool strafeVertActive = std::abs(strafeVertNorm) > strafeActThreshY || digStrafeUp   || digStrafeDown;
        ShipOutputSystem::SetOutputHeld(ShipOutputSystem::GetShipButtonOutput("SwitchFlightModes"), OwnerStrafeModifier, strafeLatActive || strafeVertActive);

        s_currentThrottle.store(throttle);

        // ---- Aim mode toggle ----
        {
            static bool s_aimModeOverride = false;
            static bool s_toggleAimModePrev = false;
            bool curToggleAimMode = IsButtonPressed(s_config.toggleAimModeButton);
            if (curToggleAimMode && !s_toggleAimModePrev) {
                s_aimModeOverride = !s_aimModeOverride;
                RuntimePaths::AppendLogAlways("[Controller]",
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
                bool btnHeld = btnBound && IsButtonPressed(s_config.turnAssistButton);
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

            // ---- SignalHunter ----
            int candCount = ThrottleHook::GetCandidateCount();
            SignalHunter::Tick(candCount, throttle, dt, iter);
            SignalHunter::Inject(throttle, pitch, yaw, roll, strafeX, strafeY, dt, iter, reverseHeld, suppressClusterForAim,
                                 strafeLatActive, strafeVertActive);

            // ---- AimController ----
            AimController::Update(s_config, yaw, pitch,
                hasSeparateAimInput, hasSeparateAimAxes,
                suppressClusterForAim, dt, iter);
        }

        std::this_thread::sleep_for(sleepDuration);
    }

    ShipOutputSystem::ReleaseAllShipButtonOutputs();
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
}

ThrottleController::Config& ThrottleController::GetConfig() { return s_config; }
float ThrottleController::GetCurrentThrottle() { return s_currentThrottle.load(std::memory_order_relaxed); }
bool ThrottleController::IsTurnAssistActive() { return s_turnAssistRuntimeActive; }

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
    int count = ShipOutputSystem::GetShipButtonCount();
    for (int i = 0; i < count && i < 23; i++) {
        const auto& b = ShipOutputSystem::GetShipButtonBindings()[i];
        result.push_back({ labels[i], b.sourceIniKey, b.buttonRef });
    }
    return result;
}

const std::unordered_map<int, std::pair<long, long>>& ThrottleController::GetCalibrationData() {
    return s_config.axisCalibration;
}

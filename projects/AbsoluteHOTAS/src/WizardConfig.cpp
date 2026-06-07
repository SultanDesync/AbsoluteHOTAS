#include "WizardConfig.h"
#include "WizardCapture.h"
#include "ThrottleController.h"
#include "ShipOutput.h"
#include "RuntimePaths.h"

#include <SimpleIni.h>
#include <cstdio>
#include <cmath>
#include <cstring>

static void WizLog(const std::string& msg) {
    RuntimePaths::AppendLog("[BindingWizard]", msg);
}

// --- INI write helpers ---
static void SetIniFloat(CSimpleIniA& ini, const char* section, const char* key, float val, const char* fmt = "%.2f") {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, val);
    ini.SetValue(section, key, buf);
}

static void SetIniLong(CSimpleIniA& ini, const char* section, const char* key, long val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ld", val);
    ini.SetValue(section, key, buf);
}

static void SetIniInt(CSimpleIniA& ini, const char* section, const char* key, int val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", val);
    ini.SetValue(section, key, buf);
}

namespace WizardConfig {

static WizardState s_state;

WizardState& GetState() { return s_state; }

void LoadCurrentBindings() {
    if (s_state.loaded) return;

    auto& cfg = ThrottleController::GetConfig();
    auto& s = s_state;

    // Axis bindings
    const BindingRef* axisRefs[] = {
        &cfg.throttleAxis, &cfg.pitchAxis, &cfg.yawAxis, &cfg.rollAxis,
        &cfg.strafeLatAxis, &cfg.strafeVertAxis, &cfg.reverseAxis
    };
    const bool invertVals[] = {
        cfg.bInvertThrottle, cfg.bInvertPitch, cfg.bInvertYaw, cfg.bInvertRoll,
        cfg.bInvertStrafeLat, cfg.bInvertStrafeVert, cfg.bInvertReverse
    };
    const float sensVals[] = {
        cfg.fThrottleSensitivity, cfg.fPitchSensitivity, cfg.fYawSensitivity,
        cfg.fRollSensitivity, cfg.fStrafeSensitivity, 0.0f, cfg.fReverseSensitivity
    };
    const float satVals[] = {
        cfg.fThrottleSaturation, cfg.fPitchSaturation, cfg.fYawSaturation,
        cfg.fRollSaturation, cfg.fStrafeSaturation, cfg.fStrafeVertSaturation, cfg.fReverseSaturation
    };
    const float dzVals[] = {
        cfg.fThrottleDeadzone, cfg.fPitchDeadzone, cfg.fYawDeadzone,
        cfg.fRollDeadzone, cfg.fStrafeDeadzone, cfg.fStrafeVertDeadzone, 0.0f
    };

    for (int i = 0; i < kNumAxisSlots; i++) {
        s.axisBindings[i]    = FormatBindingRef(*axisRefs[i], true);
        s.axisInvert[i]      = invertVals[i];
        s.axisSensitivity[i] = sensVals[i];
        s.axisSaturation[i]  = satVals[i];
        s.axisDeadzone[i]    = dzVals[i];
    }

    // DualStick accumulator
    s.accumulatorThrottle = cfg.bAccumulatorThrottle;
    s.accumulatorRate     = cfg.fAccumulatorRate;
    s.accumulatorDecay    = cfg.fAccumulatorDecay;
    s.reverseGateVelocity = cfg.fReverseGateVelocity;
    s.symmetricalThrottleDz = (std::abs(cfg.idlePlateau - (1.0f - cfg.fThrottleSaturation)) < 0.01f);
    s.holdForBoost = cfg.bHoldForBoost;

    // HOSAM
    s.hosamMode         = cfg.bHOSAMMode;
    s.alignmentAssist   = cfg.bAlignmentAssist;
    s.alignmentRadius   = cfg.fAlignmentRadius;
    s.alignmentIdleMs   = cfg.iAlignmentIdleMs;
    s.alignmentDecayRate = cfg.fAlignmentDecayRate;

    // Throttle calibration
    s.idlePlateau       = cfg.idlePlateau;
    s.detentCenter      = cfg.detentCenter;
    s.detentDeadzone    = cfg.detentDeadzone;
    s.unipolarReverse   = cfg.bUnipolarReverse;
    s.reverseZoneCenter = cfg.reverseZoneCenter;
    s.reverseZoneDeadzone = cfg.reverseZoneDeadzone;
    s.boostZone         = cfg.bBoostZone;
    s.boostZoneCenter   = cfg.boostZoneCenter;
    s.boostZoneDeadzone = cfg.boostZoneDeadzone;

    // Control buttons
    const BindingRef* btnRefs[] = { &cfg.activateButton, &cfg.stopButton, &cfg.toggleWizardButton };
    for (int i = 0; i < kNumButtonSlots; i++) {
        s.buttonBindings[i] = FormatBindingRef(*btnRefs[i], false);
    }

    // Ship actions
    auto shipActions = ThrottleController::GetShipActionBindings();
    s.shipActionSlots.clear();
    for (auto& sa : shipActions) {
        s.shipActionSlots.push_back({ sa.label, sa.iniKey, FormatBindingRef(sa.binding, false) });
    }

    // Digital axes
    const BindingRef* digRefs[] = {
        &cfg.digitalReverseButton, &cfg.digitalRollLeftButton, &cfg.digitalRollRightButton,
        &cfg.digitalStrafeLeftButton, &cfg.digitalStrafeRightButton,
        &cfg.digitalStrafeUpButton, &cfg.digitalStrafeDownButton
    };
    for (int i = 0; i < kNumDigitalAxisSlots; i++) {
        s.digitalAxisBindings[i] = FormatBindingRef(*digRefs[i], false);
    }
    s.digitalRollValue   = cfg.digitalRollValue;
    s.digitalStrafeValue = cfg.digitalStrafeValue;

    // Aim
    s.aimAxisBindings[0]    = FormatBindingRef(cfg.aimYawAxis, true);
    s.aimAxisBindings[1]    = FormatBindingRef(cfg.aimPitchAxis, true);
    s.aimAxisInvert[0]      = cfg.bInvertAimYaw;
    s.aimAxisInvert[1]      = cfg.bInvertAimPitch;
    s.aimAxisSensitivity[0] = cfg.fAimYawSensitivity;
    s.aimAxisSensitivity[1] = cfg.fAimPitchSensitivity;
    s.aimSensitivity        = cfg.fAimSensitivity;
    s.aimSmoothing          = cfg.fAimSmoothing;
    s.sourceObjectAim       = cfg.bSourceObjectAim;

    // Digital aim
    const BindingRef* dAimRefs[] = {
        &cfg.digitalAimLeftButton, &cfg.digitalAimRightButton,
        &cfg.digitalAimUpButton, &cfg.digitalAimDownButton, &cfg.digitalAimCenterButton
    };
    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        s.digitalAimBindings[i] = FormatBindingRef(*dAimRefs[i], false);
    }
    s.digitalAimValue      = cfg.fDigitalAimValue;
    s.toggleAimModeBinding = FormatBindingRef(cfg.toggleAimModeButton, false);

    // Calibration data
    if (s.calibData.empty()) {
        s.calibData = ThrottleController::GetCalibrationData();
    }

    // Custom bindings from [ButtonExpansion]
    if (s.customBindings.empty()) {
        int count = ShipOutputSystem::GetShipButtonCount();
        for (int i = 0; i < count; i++) {
            const auto& b = ShipOutputSystem::GetShipButtonBindings()[i];
            if (strcmp(b.actionId, "ButtonExpansion") != 0) continue;
            if (b.buttonRef.value < 1) continue;

            std::string binding;
            if (!b.buttonRef.deviceName.empty()) {
                binding = b.buttonRef.deviceName + "@" + std::to_string(b.buttonRef.value);
            } else if (b.buttonRef.deviceIndex >= 0) {
                binding = "#" + std::to_string(b.buttonRef.deviceIndex) + "@" + std::to_string(b.buttonRef.value);
            } else {
                binding = std::to_string(b.buttonRef.value);
            }

            std::string output;
            switch (b.output.kind) {
                case ShipOutputKind::Keyboard:
                    { char buf[32]; std::snprintf(buf, sizeof(buf), "key:0x%02X", b.output.code); output = buf; }
                    break;
                case ShipOutputKind::Mouse:
                    { char buf[32]; std::snprintf(buf, sizeof(buf), "mouse:%d", b.output.code); output = buf; }
                    break;
                default: output = "none"; break;
            }
            s.customBindings.push_back({ binding, output });
        }
    }

    s.loaded = true;
}

void SaveBindingsToINI() {
    auto iniPath = RuntimePaths::IniPath();
    WizLog("Saving bindings to: " + iniPath.string());

    CSimpleIniA ini;
    ini.SetUnicode(false);
    ini.LoadFile(iniPath.string().c_str());

    auto& s = s_state;

    // Axes
    for (int i = 0; i < kNumAxisSlots; i++) {
        const char* val = (s.axisBindings[i] != "(unbound)") ? s.axisBindings[i].c_str() : "";
        ini.SetValue("Hardware", kAxisSlots[i].iniKey, val);
        if (kAxisSlots[i].invertIniKey)
            ini.SetBoolValue("Hardware", kAxisSlots[i].invertIniKey, s.axisInvert[i]);
        if (kAxisSlots[i].sensitivityKey && s.axisSensitivity[i] > 0.0f)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].sensitivityKey, s.axisSensitivity[i]);
        if (kAxisSlots[i].saturationKey)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].saturationKey, s.axisSaturation[i]);
        if (kAxisSlots[i].deadzoneKey)
            SetIniFloat(ini, "Hardware", kAxisSlots[i].deadzoneKey, s.axisDeadzone[i]);
    }

    // Throttle calibration
    SetIniFloat(ini, "Normalization", "fIdlePlateau", s.idlePlateau);
    SetIniLong(ini, "Normalization", "iDetentCenter", s.detentCenter);
    SetIniLong(ini, "Normalization", "iDetentDeadzone", s.detentDeadzone);
    ini.SetBoolValue("Normalization", "bUnipolarReverse", s.unipolarReverse);
    SetIniLong(ini, "Normalization", "iReverseZoneCenter", s.reverseZoneCenter);
    SetIniLong(ini, "Normalization", "iReverseZoneDeadzone", s.reverseZoneDeadzone);
    ini.SetBoolValue("Normalization", "bBoostZone", s.boostZone);
    SetIniLong(ini, "Normalization", "iBoostZoneCenter", s.boostZoneCenter);
    SetIniLong(ini, "Normalization", "iBoostZoneDeadzone", s.boostZoneDeadzone);

    // Control buttons
    for (int i = 0; i < kNumButtonSlots; i++) {
        const char* val = (s.buttonBindings[i] != "(unbound)") ? s.buttonBindings[i].c_str() : "-1";
        ini.SetValue("Buttons", kButtonSlots[i].iniKey, val);
    }

    // Ship actions
    for (auto& sa : s.shipActionSlots) {
        const char* val = (sa.binding != "(unbound)") ? sa.binding.c_str() : "-1";
        ini.SetValue("ShipButtons", sa.iniKey.c_str(), val);
    }

    // Digital axes
    for (int i = 0; i < kNumDigitalAxisSlots; i++) {
        const char* val = (s.digitalAxisBindings[i] != "(unbound)") ? s.digitalAxisBindings[i].c_str() : "-1";
        ini.SetValue("DigitalAxes", kDigitalAxisSlots[i].iniKey, val);
    }
    SetIniFloat(ini, "DigitalAxes", "fDigitalRollValue", s.digitalRollValue);
    SetIniFloat(ini, "DigitalAxes", "fDigitalStrafeValue", s.digitalStrafeValue);

    // Aim
    ini.SetBoolValue("Aim", "bSourceObjectAim", s.sourceObjectAim);
    SetIniFloat(ini, "Aim", "fAimSensitivity", s.aimSensitivity);
    ini.SetBoolValue("Aim", "bMirrorFlightToAim", true);
    for (int i = 0; i < kNumAimAxisSlots; i++) {
        const char* val = (s.aimAxisBindings[i] != "(unbound)") ? s.aimAxisBindings[i].c_str() : "";
        ini.SetValue("Aim", kAimAxisSlots[i].iniKey, val);
        ini.SetBoolValue("Aim", kAimAxisSlots[i].invertIniKey, s.aimAxisInvert[i]);
        SetIniFloat(ini, "Aim", kAimAxisSlots[i].sensitivityKey, s.aimAxisSensitivity[i]);
    }
    SetIniFloat(ini, "Aim", "fAimSmoothing", s.aimSmoothing);

    // Digital aim
    for (int i = 0; i < kNumDigitalAimSlots; i++) {
        const char* val = (s.digitalAimBindings[i] != "(unbound)") ? s.digitalAimBindings[i].c_str() : "-1";
        ini.SetValue("Aim", kDigitalAimSlots[i].iniKey, val);
    }
    SetIniFloat(ini, "Aim", "fDigitalAimValue", s.digitalAimValue);
    {
        const char* val = (s.toggleAimModeBinding != "(unbound)") ? s.toggleAimModeBinding.c_str() : "-1";
        ini.SetValue("Aim", "iToggleAimModeButton", val);
    }

    // DualStick accumulator
    ini.SetBoolValue("DualStick", "bAccumulatorThrottle", s.accumulatorThrottle);
    SetIniFloat(ini, "DualStick", "fAccumulatorRate", s.accumulatorRate, "%.1f");
    SetIniFloat(ini, "DualStick", "fAccumulatorDecay", s.accumulatorDecay, "%.1f");
    SetIniFloat(ini, "DualStick", "fReverseGateVelocity", s.reverseGateVelocity, "%.1f");
    ini.SetBoolValue("Injection", "bHoldForBoost", s.holdForBoost);

    // HOSAM
    ini.SetBoolValue("Aim", "bHOSAMMode", s.hosamMode);
    ini.SetBoolValue("Aim", "bAlignmentAssist", s.alignmentAssist);
    SetIniFloat(ini, "Aim", "fAlignmentRadius", s.alignmentRadius, "%.1f");
    SetIniInt(ini, "Aim", "iAlignmentIdleMs", s.alignmentIdleMs);
    SetIniFloat(ini, "Aim", "fAlignmentDecayRate", s.alignmentDecayRate, "%.1f");

    // Calibration
    ini.Delete("Calibration", nullptr);
    for (const auto& [key, range] : s.calibData) {
        int devIdx = (key >> 8) & 0xFF;
        int usage = key & 0xFF;
        char keyBuf[64], valBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "iCalib_%d_0x%02X", devIdx, usage);
        std::snprintf(valBuf, sizeof(valBuf), "%ld,%ld", range.first, range.second);
        ini.SetValue("Calibration", keyBuf, valBuf);
    }

    // Custom button expansion
    ini.Delete("ButtonExpansion", nullptr);
    for (const auto& row : s.customBindings) {
        if (row.buttonBinding == "(unbound)" || row.output == "none" || row.output.empty()) continue;
        std::string iniKey;
        auto atPos = row.buttonBinding.rfind('@');
        if (atPos != std::string::npos) {
            iniKey = row.buttonBinding.substr(0, atPos) + "@iButton" + row.buttonBinding.substr(atPos + 1);
        } else {
            iniKey = "iButton" + row.buttonBinding;
        }
        ini.SetValue("ButtonExpansion", iniKey.c_str(), row.output.c_str());
    }

    ini.SaveFile(iniPath.string().c_str());
    WizLog("INI saved. Reloading config...");

    ThrottleController::ReloadConfig();
    WizLog("Config reload requested. UI retains current values.");
}

std::string FormatBindingDisplay(const std::string& binding) {
    if (binding == "(unbound)") return binding;
    auto atPos = binding.rfind('@');
    std::string numPart = (atPos != std::string::npos) ? binding.substr(atPos + 1) : binding;
    char* endPtr = nullptr;
    long btnId = std::strtol(numPart.c_str(), &endPtr, 10);
    if (endPtr != numPart.c_str() && *endPtr == '\0' && btnId >= 129 && btnId <= 144) {
        int povIndex = (int)(btnId - 129) / 4;
        int direction = (int)(btnId - 129) % 4;
        char label[256];
        std::snprintf(label, sizeof(label), "%s (POV%d-%s)", binding.c_str(), povIndex + 1,
            WizardCapture::PovDirectionName(direction));
        return label;
    }
    return binding;
}

} // namespace WizardConfig

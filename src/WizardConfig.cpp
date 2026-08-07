#include "PCH.h"

#include "WizardConfig.h"
#include "WizardConfigInternal.h"

#include "RuntimePaths.h"
#include "ShipOutput.h"
#include "ThrottleController.h"

#include <SimpleIni.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace WizardConfig {
namespace {

WizardState s_state;
WizardState s_baseState;
std::string s_editProfile;
std::string s_savedStateSignature;

}  // namespace

namespace Detail {

void Log(const std::string& message) {
    RuntimePaths::Log("[BindingWizard]", message);
}

WizardState& State() { return s_state; }
WizardState& BaseState() { return s_baseState; }
std::string& EditProfile() { return s_editProfile; }
std::string& SavedStateSignature() { return s_savedStateSignature; }

void MarkStateSaved() {
    SavedStateSignature() = StateSignature(State());
}

}  // namespace Detail

WizardState& GetState() { return Detail::State(); }

const std::string& GetEditProfile() { return Detail::EditProfile(); }


void LoadCurrentBindings() {
    if (Detail::State().loaded) return;

    auto& cfg = ThrottleController::GetConfig();
    auto& s = Detail::State();
    s.axisInjectionEnabled = cfg.bEnableInjection;

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
    s.accumulatorTurnAssist = cfg.bAccumulatorTurnAssist;
    s.turnAssistMode      = cfg.iTurnAssistMode;
    s.turnAssistBinding   = FormatBindingRef(cfg.turnAssistButton, false);
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
    s.pilotGateMode = cfg.pilotGateMode == ThrottleController::GateMode::Full ? 2
        : (cfg.pilotGateMode == ThrottleController::GateMode::InjectionOnly ? 1 : 0);
    s.automaticPilotSignal = cfg.pilotSignal == ThrottleController::PilotSignal::Auto;
    s.pilotLatchMilliseconds = cfg.pilotLatchMilliseconds;
    const BindingRef* extensionRefs[] = {
        &cfg.cruiseHoldButton, &cfg.fullStopButton, &cfg.cruiseHalfButton, &cfg.cruiseMaxButton
    };
    for (int i = 0; i < kNumControlExtensionSlots; ++i)
        s.controlExtensionBindings[i] = FormatBindingRef(*extensionRefs[i], false);

    // Ship actions
    auto shipActions = ThrottleController::GetShipActionBindings();
    s.shipActionSlots.clear();
    for (auto& sa : shipActions) {
        s.shipActionSlots.push_back({ sa.label, sa.iniKey, FormatBindingRef(sa.binding, false) });
    }
    s.usePitchAxisForMenu = cfg.bUsePitchAxisForMenu;
    s.useYawAxisForMenu = cfg.bUseYawAxisForMenu;
    s.usePrimaryWeaponForMenuSelect = cfg.bUsePrimaryWeaponForMenuSelect;
    s.invertMenuVertical = cfg.bInvertMenuVertical;
    s.invertMenuHorizontal = cfg.bInvertMenuHorizontal;
    s.menuAxisEngageThreshold = cfg.fMenuAxisEngageThreshold;
    s.menuAxisReleaseThreshold = cfg.fMenuAxisReleaseThreshold;

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

    // Camera look
    s.headLookEnabled = cfg.headTracking.enabled;
    s.headLookOpenTrackEnabled = cfg.headTracking.openTrackEnabled;
    const BindingRef* headAxes[] = {
        &cfg.headTracking.yawAxis, &cfg.headTracking.pitchAxis, &cfg.headTracking.rollAxis
    };
    const bool headInvert[] = {
        cfg.headTracking.invertYaw, cfg.headTracking.invertPitch, cfg.headTracking.invertRoll
    };
    const bool headEnabled[] = {
        cfg.headTracking.yawEnabled, cfg.headTracking.pitchEnabled,
        cfg.headTracking.rollEnabled
    };
    const float headSensitivity[] = {
        cfg.headTracking.yawScale, cfg.headTracking.pitchScale, cfg.headTracking.rollScale
    };
    const float headMaximums[] = {
        cfg.headTracking.maxYawDegrees, cfg.headTracking.maxPitchDegrees,
        cfg.headTracking.maxRollDegrees
    };
    for (int i = 0; i < kNumHeadLookAxisSlots; ++i) {
        s.headLookAxisBindings[i] = FormatBindingRef(*headAxes[i], true);
        s.headLookAxisEnabled[i] = headEnabled[i];
        s.headLookInvert[i] = headInvert[i];
        s.headLookSensitivity[i] = headSensitivity[i];
        s.headLookMaxDegrees[i] = headMaximums[i];
    }
    s.headLookDeadzoneDegrees = cfg.headTracking.deadzoneDegrees;
    s.headLookJoystickDeadzone = cfg.headTracking.joystickDeadzone;
    s.headLookSmoothing = cfg.headTracking.smoothing;
    s.headLookRecenterBinding = FormatBindingRef(cfg.headTracking.recenterButton, false);
    s.headLookToggleBinding = FormatBindingRef(cfg.headTracking.toggleButton, false);

    // Calibration data
    s.calibData = ThrottleController::GetCalibrationData();

    // Custom bindings from [ButtonExpansion]
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

    // Macros come straight from their INI, not from MacroEngine — see WizardDefs.h.
    Detail::LoadMacroRows(s);

    // Snapshot base for sparse-overlay diffs. The wizard forces the
    // engine to base while open, so what we just loaded IS base.
    Detail::BaseState() = s;

    s.loaded = true;
    Detail::MarkStateSaved();
}


bool LoadProfileForEditing(const std::string& name, std::string& err) {
    LoadCurrentBindings();
    if (name.empty()) {
        Detail::State() = Detail::BaseState();
        Detail::EditProfile().clear();
        Detail::MarkStateSaved();
        return true;
    }

    const auto path = Detail::FindProfilePath(name);
    if (path.empty()) { err = "Profile not found."; return false; }
    CSimpleIniA profile;
    profile.SetUnicode(false);
    if (profile.LoadFile(path.string().c_str()) != SI_OK) {
        err = "Could not read profile file.";
        return false;
    }
    const char* kind = profile.GetValue("Profile", "sKind", nullptr);
    const long version = profile.GetLongValue("Profile", "iConfigVersion", -1);
    if (!kind || (_stricmp(kind, "full") != 0 && _stricmp(kind, "overlay") != 0)
        || version < 1 || version > kConfigVersion) {
        err = "Profile format is invalid or newer than this version of AbsoluteHOTAS.";
        return false;
    }

    WizardState effective = Detail::BaseState();
    Detail::ApplyProfileScalars(profile, effective);
    Detail::LoadEffectiveCollections(path, effective);
    Detail::LoadMacroRows(effective, &path);
    effective.loaded = true;
    Detail::State() = std::move(effective);
    Detail::EditProfile() = name;
    Detail::MarkStateSaved();
    return true;
}


bool HasUnsavedChanges() {
    return Detail::State().loaded && Detail::StateSignature(Detail::State()) != Detail::SavedStateSignature();
}


// Route Save to whichever profile is the current edit target.
bool SaveActiveProfile(std::string& err) {
    err.clear();
    for (const auto& row : Detail::State().customBindings) {
        if (row.buttonBinding == "(unbound)" || row.output == "none" || row.output.empty()) {
            err = "Complete or remove every custom key-binding row before saving.";
            return false;
        }
    }
    for (const auto& macro : Detail::State().macros) {
        if (Detail::SanitizeMacroName(macro.name).empty()) {
            err = "Name or remove every macro before saving.";
            return false;
        }
    }
    if (Detail::EditProfile().empty()) return Detail::SaveBindingsToINI(err);     // base -> _Custom.ini
    return Detail::SaveProfileOverlay(Detail::EditProfile(), err);
}

}  // namespace WizardConfig

#pragma once

// Config load/save and state management for the BindingWizard.
// Consolidates all s_* globals into a single WizardState struct.

#include "WizardDefs.h"
#include "BindingRef.h"

#include <string>
#include <vector>
#include <unordered_map>

struct WizardState {
    bool        axisInjectionEnabled = true;

    // Axis bindings
    std::string axisBindings[kNumAxisSlots];
    bool        axisInvert[kNumAxisSlots];
    float       axisSensitivity[kNumAxisSlots];
    float       axisSaturation[kNumAxisSlots];
    float       axisDeadzone[kNumAxisSlots];

    // Control buttons
    std::string buttonBindings[kNumButtonSlots];
    std::string controlExtensionBindings[kNumControlExtensionSlots];

    // Ship actions (runtime-populated)
    std::vector<ShipActionSlot> shipActionSlots;

    // Digital axes
    std::string digitalAxisBindings[kNumDigitalAxisSlots];
    float       digitalRollValue = 1.0f;
    float       digitalStrafeValue = 1.0f;

    // Aim axes
    std::string aimAxisBindings[kNumAimAxisSlots];
    bool        aimAxisInvert[kNumAimAxisSlots];
    float       aimAxisSensitivity[kNumAimAxisSlots];
    float       aimSensitivity = 1.0f;
    float       aimSmoothing = 0.0f;
    bool        sourceObjectAim = true;

    // Digital aim
    std::string digitalAimBindings[kNumDigitalAimSlots];
    float       digitalAimValue = 1.0f;
    std::string toggleAimModeBinding;

    // DualStick accumulator mode
    bool        accumulatorThrottle = false;
    float       accumulatorRate = 1.0f;
    float       accumulatorDecay = 0.0f;
    float       reverseGateVelocity = 5.0f;
    bool        accumulatorTurnAssist = false;
    int         turnAssistMode = 0;        // 0=Always, 1=Hold, 2=Toggle
    std::string turnAssistBinding;         // Button binding for Hold/Toggle activation
    bool        symmetricalThrottleDz = true;
    bool        holdForBoost = true;

    // HOSAM mode
    bool        hosamMode = false;
    bool        alignmentAssist = false;
    float       alignmentRadius = 130.0f;
    int         alignmentIdleMs = 50;
    float       alignmentDecayRate = 8.0f;

    // Throttle calibration
    float       idlePlateau = 0.05f;
    long        detentCenter = 32768;
    long        detentDeadzone = 500;
    bool        calibratingCenter = false;
    bool        unipolarReverse = false;
    long        reverseZoneCenter = 3000;
    long        reverseZoneDeadzone = 3000;
    bool        calibratingReverseZone = false;
    bool        boostZone = false;
    long        boostZoneCenter = 62000;
    long        boostZoneDeadzone = 2000;
    bool        calibratingBoostZone = false;

    // Calibration data: key = (deviceIndex << 8) | usageId, value = {min, max}
    std::unordered_map<int, std::pair<long, long>> calibData;

    // Custom button expansion bindings
    std::vector<CustomBindingRow> customBindings;

    // Macro editor rows, read from / written to AbsoluteHOTAS_Custom.ini
    std::vector<MacroRow> macros;

    bool loaded = false;
};

namespace WizardConfig {

struct ProfileSummary {
    std::string name;
    std::string kind;
    std::string filename;
    std::string trigger = "(unbound)";
    std::string keyboardShortcut = "(unbound)";
    std::string mode = "momentary";
    int sequence = 0;
    int slot = 0;
};

WizardState& GetState();

// Load bindings from ThrottleController::GetConfig() into WizardState.
void LoadCurrentBindings();
bool LoadProfileForEditing(const std::string& name, std::string& err);

// --- Profile edit target ---
// Which config the wizard's Save writes to: "" = base (_Custom.ini, full save);
// otherwise a Profiles/<name> sparse overlay. The dropdown in the profiles header
// sets this; the Save button routes through SaveActiveProfile.
const std::string& GetEditProfile();

// Save to the current edit target: base -> full _Custom.ini; a profile -> a sparse
// overlay of only the keys that differ from base. See docs/reference/profile-switching.md.
bool SaveActiveProfile(std::string& err);

// Compare the editable working copy with the last successfully loaded or saved
// snapshot. Runtime-only calibration gestures are excluded by serialization.
bool HasUnsavedChanges();

// Format a binding string for display, annotating POV virtual buttons.
std::string FormatBindingDisplay(const std::string& binding);

// --- Profiles (Export/Import) ---
// A profile is one INI payload plus a [Profile] header.

// Managed profiles in display order, including activation metadata.
std::vector<ProfileSummary> ListProfileSummaries();

// Create the two optional starter overlays. Flight is the base and has no file;
// FPS parks axis injection, while Flight Aux initially inherits base unchanged.
bool EnsureStarterProfiles(std::string& err);
bool CreateOverlayProfile(const std::string& name, std::string& err);
bool SetProfileActivation(const std::string& name, const std::string& trigger,
                          const std::string& mode, std::string& err);
// Read the base config's own activation (the "(base)" swap slot), so the wizard can
// bind base as a first-class swap position (e.g. a rotary detent for base flight).
void GetBaseActivation(std::string& trigger, std::string& mode);
bool ResetBaseToDefaults(std::string& err);

// Materialize the effective base config into a managed profile file. Returns
// false with a reason in `err` (empty name, nothing saved yet, write failure).
bool ExportProfile(const std::string& name, std::string& err);

// Replace the current custom control payload with the named full profile, after
// auto-backing up the current pair. Triggers a live reload. Returns false with a
// reason in `err`.
bool ImportProfile(const std::string& name, std::string& err);

} // namespace WizardConfig

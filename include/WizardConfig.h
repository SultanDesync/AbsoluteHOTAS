#pragma once

// Config load/save and state management for the BindingWizard.
// Consolidates all s_* globals into a single WizardState struct.

#include "WizardDefs.h"
#include "BindingRef.h"

#include <string>
#include <vector>
#include <unordered_map>

struct WizardState {
    // Axis bindings
    std::string axisBindings[kNumAxisSlots];
    bool        axisInvert[kNumAxisSlots];
    float       axisSensitivity[kNumAxisSlots];
    float       axisSaturation[kNumAxisSlots];
    float       axisDeadzone[kNumAxisSlots];

    // Control buttons
    std::string buttonBindings[kNumButtonSlots];

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

    bool loaded = false;
};

namespace WizardConfig {

WizardState& GetState();

// Load bindings from ThrottleController::GetConfig() into WizardState.
void LoadCurrentBindings();

// Save all wizard state back to AbsoluteHOTAS_User.ini and request a config reload.
void SaveBindingsToINI();

// Format a binding string for display, annotating POV virtual buttons.
std::string FormatBindingDisplay(const std::string& binding);

// --- Profiles (Export/Import) ---
// A profile is one INI = the user file + macros file merged (disjoint by
// ownership) plus a [Profile] header. See docs/reference/config-layout.md.

// Names of exportable profiles in ProfilesDir() (stems, no extension). Hides the
// underscore-prefixed auto-backups so the import list stays the user's own saves.
std::vector<std::string> ListProfiles();

// Merge the current user + macros files into ProfilesDir()/<name>.ini. Returns
// false with a reason in `err` (empty name, nothing saved yet, write failure).
bool ExportProfile(const std::string& name, std::string& err);

// Replace the current user + macros files with the named profile, after
// auto-backing up the current pair. Triggers a live reload. Returns false with a
// reason in `err`.
bool ImportProfile(const std::string& name, std::string& err);

} // namespace WizardConfig

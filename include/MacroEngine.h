#pragma once
#include "ShipOutput.h"
#include "BindingRef.h"
#include <SimpleIni.h>
#include <string>
#include <vector>

// ============================================================================
// MacroEngine — button-triggered key sequences (chord / tap / hold / turbo).
//
// A macro plays an ordered list of steps when its physical button is pressed.
// See docs/reference/macros.md for the model and the "Grav -> Shields" example.
//
// ============================================================================

enum class MacroAction { Tap, Hold };

struct MacroStep {
    std::vector<ShipControlTarget> targets;       // chord = multiple native/raw targets
    MacroAction             action = MacroAction::Tap;
    int                     amount = 1;           // Tap: repeat count; Hold: duration ms
    int                     gapMs  = 50;          // wait before the next step
};

struct Macro {
    std::string            name;
    BindingRef             button;                // physical trigger
    // Turbo repeats the whole sequence while the button is held, and stops on
    // release. Without it a macro is fire-and-forget: one press plays the sequence
    // to completion, so a 3-second routine does not demand a 3-second hold.
    bool                   turbo = false;
    std::vector<MacroStep> steps;
};

namespace MacroEngine {

// Parse every [Macro:<name>] section. Named action ids resolve to native ship
// controls or the six universal context inputs; explicit key:/mouse: targets
// resolve to raw outputs.
void LoadMacros(CSimpleIniA& ini);

// Mutable access to the loaded macros so the control loop can resolve each macro's
// trigger button to a device index. LoadMacros runs before DeviceManager is
// initialized, so button resolution must happen later, in ThrottleController's
// ResolveAll — without it, name-based macro buttons keep deviceIndex = -1 and the
// macro never fires.
std::vector<Macro>& GetMacrosMutable();

// Tick the macro state machines once per control-loop iteration: detect button
// presses, advance active sequences, emit through logical ownership. Call only when
// macros should be live (armed, overlay closed).
void Update();

// Release every macro-held output and reset all runtime state. Call on disarm,
// overlay open, stop, or config reload so no macro key is left stuck down.
void ReleaseAll();

// Mark physically held triggers as already consumed after a suppression window,
// so closing the wizard cannot start a macro from a stale held button.
void SeedDownButtonsConsumed();

// ---- Profile snapshot / restore (see docs/reference/profile-switching.md) ----
// A profile swap preloads each slot's resolved macro set and restores one on swap.
// RestoreMacros resets the parallel runtime array, so call ReleaseAll first to
// free any keys the outgoing set was holding.
std::vector<Macro> SnapshotMacros();
void RestoreMacros(const std::vector<Macro>& macros);

} // namespace MacroEngine

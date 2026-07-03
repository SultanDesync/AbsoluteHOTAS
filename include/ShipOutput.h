#pragma once
#include "BindingRef.h"
#include <SimpleIni.h>
#include <cstdint>
#include <string_view>

// ============================================================================
// ShipOutput — SendInput emission and held-key ownership management
//
// Manages keyboard/mouse output for ship action buttons and the ButtonExpansion
// feature. Uses a reference-counted "held output" model so multiple logical
// owners can share a single physical key press without early-releasing it.
// ============================================================================

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
    uint16_t       code;
    bool           extended;
};

struct ShipButtonBinding {
    const char*  actionId;
    const char*  sourceIniKey;
    const char*  outputIniKey;
    BindingRef   buttonRef;
    ShipOutput   output;
    ShipBindingMode mode;
    bool         previousPressed;
};

// Well-known output constants
inline constexpr ShipOutput NoOutput    { ShipOutputKind::None,     0,    false };
inline constexpr ShipOutput SpaceOutput { ShipOutputKind::Keyboard, 0x39, false };

// Owner-ID constants for SetOutputHeld
inline constexpr uint32_t OwnerStrafeModifier = 0x00000001u;
inline constexpr uint32_t OwnerBoostZone      = 0x00000004u;
inline constexpr uint32_t OwnerShipButtonBase = 0x00001000u;
inline constexpr uint32_t OwnerMacroBase      = 0x00002000u;  // + macro index

// ---- Public API ----

namespace ShipOutputSystem {

// Load ship action bindings from INI (called by ThrottleController::LoadConfig).
void LoadShipButtonBindings(CSimpleIniA& ini);

// Process button state each control-loop tick.
// Call only when the UI overlay is NOT open.
void UpdateShipButtonBindings();

// Release all held outputs (called on disarm or stop).
void ReleaseAllShipButtonOutputs();

// Release only outputs held by ship-button bindings (leaves axis-driven owners
// such as the strafe modifier and boost zone intact). Used to suppress ship
// buttons per-tick (wizard open, or bShipButtonsEnabled = false) without
// disturbing flight modifiers.
void ReleaseShipButtonBindingOutputs();

// Emit/release a held output with reference-counted ownership.
void SetOutputHeld(const ShipOutput& output, uint32_t ownerId, bool held);
void ReleaseOwnerOutputs(uint32_t ownerId);

// Returns true if the FireBoosters (index 0) output is currently held.
bool IsBoostOutputHeld();

// Direct access to ship button bindings (for BindingWizard and ControlLoop).
ShipButtonBinding* GetShipButtonBindings();
int                GetShipButtonCount();

// Resolved (control-map-aware) output for a named ship action. Lets the axis-
// driven paths — throttle boost zone, strafe/flight-mode modifier — follow
// in-game rebinds the same way the button path does. NoOutput if not found.
ShipOutput GetShipButtonOutput(std::string_view actionId);

// Resolve a macro/binding target token to an output: either a ship action id
// (e.g. "NextSystem", resolved control-map-aware) or a raw "key:0xNN" / "mouse:N"
// / "none". NoOutput if it resolves to nothing.
ShipOutput ResolveOutputToken(std::string_view token);

} // namespace ShipOutputSystem

#pragma once
#include "BindingRef.h"
#include "NativeShipControl.h"
#include <SimpleIni.h>
#include <cstdint>
#include <string_view>

// ============================================================================
// ShipOutput — native action ownership plus explicit raw-output management
//
// Most named ship actions resolve to NativeShipControl. The six profile-stable
// context inputs (Select, Back, Up, Down, Left, Right) emit fixed vanilla keys;
// explicit ButtonExpansion and key:/mouse: macro targets remain raw as well.
// Every path uses ownership so one owner cannot release another's hold.
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

struct ShipControlTarget {
    NativeShipControl::Action nativeAction = NativeShipControl::Action::Invalid;
    ShipOutput output = NoOutput;

    bool IsNative() const { return nativeAction != NativeShipControl::Action::Invalid; }
};

// Owner-ID constants for SetOutputHeld
inline constexpr uint32_t OwnerStrafeModifier = 0x00000001u;
inline constexpr uint32_t OwnerBoostZone      = 0x00000004u;
inline constexpr uint32_t OwnerMenuUp         = 0x00000010u;
inline constexpr uint32_t OwnerMenuDown       = 0x00000011u;
inline constexpr uint32_t OwnerMenuLeft       = 0x00000012u;
inline constexpr uint32_t OwnerMenuRight      = 0x00000013u;
inline constexpr uint32_t OwnerMenuSelect     = 0x00000014u;
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

// Request one of the six profile-stable context inputs. The vanilla key is
// always retained; targeting-selector directions add their exact native lane
// only while Targeting Mode is active.
void SetUniversalContextHeld(std::string_view actionId, uint32_t ownerId, bool held);

// ---- Profile snapshot / restore (see docs/reference/profile-switching.md) ----
// A profile swap preloads the ship-button table per slot and
// restores one into place on swap. RestoreBindings does NOT touch held outputs —
// the caller releases those first — and SeedDownButtonsConsumed then marks any
// physically-held button as already-seen so it will not re-fire under its new
// meaning until genuinely re-pressed.
std::vector<ShipButtonBinding> SnapshotBindings();
void RestoreBindings(const std::vector<ShipButtonBinding>& bindings);
void SeedDownButtonsConsumed();

// Returns true if any logical owner currently requests native Boost.
bool IsBoostRequested();

// Direct access to ship button bindings (for BindingWizard and ControlLoop).
ShipButtonBinding* GetShipButtonBindings();
int                GetShipButtonCount();
const ShipButtonBinding* FindShipButtonBinding(std::string_view actionId);

// Named ship actions resolve to native operations except the six compatibility
// aliases, which resolve to fixed vanilla context inputs. Explicit key:/mouse:
// tokens remain raw SendInput targets for general-purpose macros.
ShipControlTarget ResolveControlTarget(std::string_view token);
void SetControlTargetHeld(const ShipControlTarget& target, uint32_t ownerId, bool held);

} // namespace ShipOutputSystem

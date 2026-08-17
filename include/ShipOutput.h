#pragma once
#include "BindingRef.h"
#include "NativeShipControl.h"
#include "ShipActionCatalog.h"
#include <SimpleIni.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// ShipOutput — native action ownership plus explicit raw-output management
//
// Named ship actions resolve through the shared Direct/Context/Keyboard
// compatibility catalog. The six profile-stable context inputs (Select, Back,
// Up, Down, Left, Right) retain fixed vanilla navigation behavior; explicit
// ButtonExpansion and key:/mouse: macro targets remain raw as well.
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
    ShipControlMethod method;
    KeyboardResolutionSource resolutionSource;
    bool         methodOverridden;
    ShipBindingMode mode;
    bool         previousPressed;
};

// Well-known output constants
inline constexpr ShipOutput NoOutput    { ShipOutputKind::None,     0,    false };
inline constexpr ShipOutput SpaceOutput { ShipOutputKind::Keyboard, 0x39, false };

enum class ShipControlTargetKind : std::uint8_t {
    None,
    Native,
    Context,
    RawOutput,
};

struct ShipControlTarget {
    ShipControlTargetKind kind = ShipControlTargetKind::None;
    NativeShipControl::Action nativeAction = NativeShipControl::Action::Invalid;
    ShipOutput output = NoOutput;
    std::string_view actionId;

    bool IsNative() const { return kind == ShipControlTargetKind::Native; }
    bool IsContext() const { return kind == ShipControlTargetKind::Context; }
    bool IsValid() const { return kind != ShipControlTargetKind::None; }
};

struct ShipActionRouteInfo {
    std::string_view actionId;
    std::string_view displayLabel;
    ShipActionGroup group{};
    ShipControlMethod method{};
    bool methodOverridden{};
    ShipOutput resolvedKeyboardOutput{ NoOutput };
    KeyboardResolutionSource keyboardSource{ KeyboardResolutionSource::NotApplicable };
    ShipActionAvailability availability{ ShipActionAvailability::UnavailableForBuild };
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

// Refresh installation-wide method preferences and the cached Starfield
// ControlMap resolution. Called at startup/config reload, never from rendering
// or a profile swap.
void RefreshRoutingInputs();

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
ShipActionRouteInfo GetShipActionRouteInfo(std::string_view actionId);
std::vector<ShipActionRouteInfo> GetShipActionRouteInfos();

// Named ship actions resolve through their currently selected catalog method.
// Explicit key:/mouse: tokens remain raw targets for general-purpose macros.
ShipControlTarget ResolveControlTarget(std::string_view token);
void SetControlTargetHeld(const ShipControlTarget& target, uint32_t ownerId, bool held);

} // namespace ShipOutputSystem

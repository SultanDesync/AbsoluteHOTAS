#pragma once

#include "ShipActionCatalog.h"

#include <cstdint>
#include <string_view>

// Production-native Starfield control seams recovered and causally validated by
// the 5.0 research pass. The implementation is exact-build/object gated and
// fails closed: this layer never falls back to Windows input synthesis.
namespace NativeShipControl {

enum class Action : std::uint8_t {
    FireBoosters,
    SwitchFlightModes,
    TogglePov,
    FireWeapon0,
    FireWeapon1,
    FireWeapon2,
    ShipAction1,
    SelectTarget,
    IncreaseSystemPower,
    DecreaseSystemPower,
    PreviousSystem,
    NextSystem,
    OpenScanner,
    Repair,
    ShipAlternateControlHold,
    Cruise,
    Cancel,
    UndockTakeOff,
    GetUp,
    ExitShipFromCockpit,
    ZoomCameraIn,
    ZoomCameraOut,
    AutopilotOnOff,
    Count,
    Invalid = 0xFF,
};

// Installs the exact-gated camera and selected-flight-handler hooks. It is safe
// to call once during plugin load; individual capabilities remain unavailable
// until their live object gates pass.
bool Initialize();
void Shutdown();

// Master authority for native ship actions, continuous split output, and head
// pose. Disabling requests releases every logical owner and clears all poses.
void SetEnabled(bool enabled);
bool Enabled();

// Refresh the selected handler from SignalHunter's acquired flight cluster.
// Passing zero invalidates ship-thread operations immediately.
void UpdateCluster(std::uintptr_t cluster);
bool ShipHandlerReady();

// Age of the most recent selected-handler flight-output execution. A negative
// value means the current handler has never executed (or changed since the last
// observation). Unlike ShipHandlerReady(), this is a live pilot-seat signal: the
// handler pointer can remain cached after the player gets up, but this timestamp
// stops advancing immediately.
std::int64_t SelectedHandlerOutputAgeMilliseconds();
bool SelectedHandlerOutputFresh(std::int64_t maximumAgeMilliseconds);
// Age of any execution through the exact-gated flight-output vtable seam. This
// mirrors Absolute Head Tracking's standalone cockpit observer and remains
// independent of HOTAS controller acquisition/enabled state.
std::int64_t FlightObserverOutputAgeMilliseconds();

// Absolute Head Tracking owns the FirstPersonState camera seam when present.
// HOTAS keeps the selected-flight observer and exposes only its bounded signal
// age through the suite ABI.
void SetExternalCameraOwner(bool active);
bool ExternalCameraOwnerActive();
bool CameraHookInstalled();
bool FlightObserverInstalled();

// Targeting Mode replaces the normal cockpit flight update with a dedicated
// camera state. This exact-vtable check remains live while the selected-handler
// output hook is intentionally suspended.
bool TargetingModeActive();

Action ActionFromId(std::string_view actionId);
std::string_view ActionId(Action action);

// Structured route status for the workbench and diagnostics. A failed Direct
// route remains failed closed; this status never causes an automatic keyboard
// fallback.
ShipActionAvailability GetActionAvailability(Action action);

// Reference-counted logical ownership. Multiple bindings/macros can own the
// same native operation without generating duplicate press/release edges.
void SetActionHeld(Action action, std::uint32_t ownerId, bool held);
void ReleaseOwner(std::uint32_t ownerId);
void ReleaseAll();
bool IsActionHeld(Action action);

// Dispatches content-keyed semantic action edges from the controller thread.
// Ship-thread-only work is consumed by the selected-handler hook instead.
void PumpControllerThread();

// The selected-handler transform runs first. These values replace only its
// separated lateral (output[0]) and roll (output[5]) fields.
void SetSplitFlightAxes(float roll, float lateral, bool active);

// Head pose is a normalized NiQuaternion in (w,x,y,z) order. The camera hook
// post-composes it after Starfield's native FirstPersonState rotation.
void SetHeadQuaternion(float w, float x, float y, float z, bool active);
void ClearHeadPose();

} // namespace NativeShipControl

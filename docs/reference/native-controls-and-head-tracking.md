# Native Controls and Head Tracking

Status: **5.0.0-beta implementation baseline**
Validated Starfield runtimes: **1.16.242 and 1.16.244**

## Control boundary

5.0 separates two output classes:

- Named ship actions use Starfield's internal ship objects, action handlers, or
  content-keyed semantic event broadcaster.
- Explicit `key:` and `mouse:` targets remain raw Windows input for custom menu
  helpers and general-purpose macros.

The native service owns logical press/release state for physical bindings,
macros, the boost zone, and the analog-strafe modifier. It consumes ship-thread
operations from the validated selected-handler callback and publishes semantic
edges from the controller thread.

## Movement modifiers

Boost writes the selected flight handler's one-frame native request latch. The
game consumes that request through its normal boost lifecycle.

Analog strafe repeats the native `SwitchFlightModes` held action while strafe is
active and emits one release when it ends. Starfield's selected-handler transform
runs first; AbsoluteHOTAS then restores the separated lateral and roll fields so
both axes can remain active simultaneously.

## Ship functions

Operations are routed according to the narrowest validated seam:

- weapon groups call exact-vtable per-weapon start/stop leaves on the ship thread;
- target selection and repair call their no-argument engine entry points;
- POV and zoom call the current camera state's lifecycle/handler;
- HUD, power, scanner, seat, cruise, cancel, and autopilot commands publish fresh
  native `ButtonEvent` objects through the input manager's semantic broadcaster.

No native operation resolves or emits a keyboard scancode. Function-specific
validation failures leave the action unapplied so a later valid frame can retry.

## Camera-look transport

AbsoluteHOTAS reads OpenTrack's FreeTrack 2.0 shared-memory stream and applies a
normalized quaternion after Starfield's native first-person camera rotation. It
does not replace Starfield's base camera pose.

Only rotational yaw, pitch, and roll are implemented. Translation values in the
FreeTrack packet are ignored. The integration is OpenTrack-compatible: it has
been validated with OpenTrack's Tobii Eye Tracker and NeuralNet webcam inputs,
both publishing the same FreeTrack output.

Tobii support is dependency-free: OpenTrack supplies the Tobii tracker input and
publishes the same FreeTrack output consumed by AbsoluteHOTAS. The plugin does not
bundle or load Tobii's proprietary Game Integration SDK.

## OpenTrack setup

1. Start OpenTrack before or after Starfield.
2. Choose your tracker input. For Tobii hardware, select OpenTrack's Tobii input.
3. Set the output protocol to **freetrack 2.0 Enhanced**.
4. Start tracking in OpenTrack.
5. Add this to `AbsoluteHOTAS_Custom.ini`:

```ini
[HeadTracking]
bEnabled = true
bOpenTrackEnabled = true
sSource = OpenTrack
iRecenterButton = -1
iToggleButton = -1
```

For Tobii-specific log wording, set `sSource = TobiiViaOpenTrack`; the transport
and pose data remain identical.

The first valid frame becomes the center pose. Assign `iRecenterButton` to capture
a new center while seated. `iToggleButton` releases or restores camera authority;
restoring it captures a fresh OpenTrack center. Stale or invalid data clears the
plugin pose and leaves the native camera untouched unless a joystick override is
still providing an axis.

## Joystick camera look

The Camera Look workbench page can bind absolute DirectInput axes for yaw, pitch,
and roll. Each binding overrides only its matching OpenTrack component, so a
mini-stick can provide vertical look while OpenTrack continues to supply yaw and
roll. Disable `bOpenTrackEnabled` for joystick-only camera look.

Joystick axes use their saved device calibration, a separate normalized center
deadzone, the same per-axis sensitivity/inversion controls as OpenTrack, and the
configured maximum angle. Recenter clears their filtered offset; returning the
physical control to neutral is always the absolute center.

Yaw, pitch, and roll also have independent enable switches in the Camera Look
workbench. Disabling a component forces only that component to neutral; disabling
roll therefore leaves normal two-axis horizontal/vertical head look.

Each component card includes a live bipolar output graph and degree readout. While
the workbench parks gameplay injection, a monitor-only OpenTrack poll keeps these
graphs moving without publishing a camera quaternion; joystick overrides read the
same cached DirectInput state used by the flight-axis graphs.

## Fail-closed behavior

The camera pose is composed only when all of these are true:

- native controls and camera look are enabled;
- the selected flight handler is live and exact-gated;
- the hooked state is still the player camera's current first-person state;
- each applied component has a finite, non-stale FreeTrack frame or a bound
  joystick override.

The SFSE plugin metadata is also pinned to the two validated Starfield runtimes.
A game update therefore requires explicit revalidation before 5.0 loads or uses
these seams.

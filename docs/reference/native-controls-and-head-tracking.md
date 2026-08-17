# Native Controls and Head Tracking

Status: **5.0.1 experimental implementation baseline**
Validated Starfield runtimes: **1.16.242 and 1.16.244**

## Control boundary

5.0 separates three output classes:

- Seventeen ship-specific named actions use internal ship objects, action handlers, or
  content-keyed semantic event broadcaster.
- Six profile-compatible context inputs emit fixed vanilla E, Esc, and arrow keys.
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
- repair calls its no-argument engine entry point;
- POV and zoom call the current camera state's lifecycle/handler;
- HUD, scanner, seat, cruise, and autopilot commands publish fresh
  native `ButtonEvent` objects through the input manager's semantic broadcaster.

Because direct weapon leaves bypass the higher native WeaponGroup listener observed by
standalone Absolute Power, a successful start also reports its zero-based group through
Power API v1's optional size-gated `recordWeaponFire` tail when present. HOTAS checks the
published struct size and callback pointer, never requires Power to load, and never reports
a failed start or stop edge. Power remains the sole owner of automation policy, allocation,
settlement, and base-preset restoration.

No native operation resolves or emits a keyboard scancode. Function-specific
validation failures leave the action unapplied so a later valid frame can retry.

## Pilot/FPS context

`ShipHandlerReady()` is only an object-validity check. Starfield keeps the same
selected handler and flight cluster cached after the player gets up, so their
continued validity does not prove current piloting.

The selected flight handler's output callback is the live positive seam. It runs
continuously at roughly the ship update rate while piloting and stops as the get-up
transition begins. AbsoluteHOTAS records each selected-handler hit and derives a
three-state context:

- **Piloting** — the output timestamp is fresh or still within the configured
  general-control latch;
- **OnFoot** — active gameplay has resumed and the timestamp has exceeded that
  latch;
- **Suspended** — a menu/loading context is active or the auxiliary gameplay flag
  is unavailable.

The shipped default uses `InjectionOnly` with a 5000 ms latch. This parks flight
axes, aim, boost/strafe output, and head pose after leaving the seat while keeping
raw custom buttons and macros available. `Full` also parks those discrete outputs;
`Off` disables automatic flight-axis parking. Discovery continues behind every
gate so starting on foot cannot prevent a later cockpit from being acquired.

Native ship actions use the longer pilot-context latch even when automatic
flight-axis parking is Off. A held action cannot queue while the native context is
closed, so returning to the cockpit requires a genuine new button edge.

The six universal context inputs are not native ship actions and remain available
through menus, dialogue, and on-foot UI under `InjectionOnly`. `Full` mode still
parks them with every plugin-owned output.

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
- its output callback has executed within the last 400 ms;
- the hooked state is still the player camera's current first-person state;
- each applied component has a finite, non-stale FreeTrack frame or a bound
  joystick override.

The SFSE plugin metadata is also pinned to the two validated Starfield runtimes.
A game update therefore requires explicit revalidation before 5.0 loads or uses
these seams.

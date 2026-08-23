# AbsoluteHOTAS 5.1 Ship Button Bindings

AbsoluteHOTAS maps DirectInput buttons to all 23 profile-stable native ship
controls. They are active only in ship or targeting context. Six separate,
optional menu-navigation bindings use vanilla E, Esc, and arrow inputs and do
not inherit assignments from the ship controls.

The physical button side is configured in `[ShipButtons]`. IDs `1..128` are
physical DirectInput buttons and `129..144` are virtual POV/hat directions. Add
a `DeviceName@` prefix for multi-device setups and use `-1` to leave an action
unbound.

## Action table

| Action | Physical button key | Runtime path |
| --- | --- | --- |
| Fire Boosters | `iFireBoostersButton` | Selected flight-handler request |
| Switch Flight Modes | `iSwitchFlightModesButton` | Selected flight-handler digital action |
| Toggle POV | `iTogglePovButton` | Current camera-state lifecycle |
| Fire Weapon 0 | `iFireWeapon0Button` | Weapon group 0 start/stop |
| Fire Weapon 1 | `iFireWeapon1Button` | Weapon group 1 start/stop |
| Fire Weapon 2 | `iFireWeapon2Button` | Weapon group 2 start/stop |
| Ship Action 1 | `iShipAction1Button` | Native `XButton` semantic action |
| Select Target | `iSelectTargetButton` | Validated native Select Target function by default; optional ship-context `E` compatibility route |
| Increase System Power | `iIncreaseSystemPowerButton` | Vanilla Up arrow while in ship context |
| Decrease System Power | `iDecreaseSystemPowerButton` | Vanilla Down arrow while in ship context |
| Previous System | `iPreviousSystemButton` | Vanilla Left arrow; exact-gated `SelectLeft` in Targeting Mode |
| Next System | `iNextSystemButton` | Vanilla Right arrow; exact-gated `SelectRight` in Targeting Mode |
| Open Scanner | `iOpenScannerButton` | Native `SHMonocle` semantic action |
| Repair | `iRepairButton` | Direct repair backend |
| Ship Alternate Control Hold | `iShipAlternateControlHoldButton` | Native `AltHold` semantic action |
| Cruise | `iCruiseButton` | Native `Cruise` semantic action |
| Ship Cancel | `iCancelButton` | Vanilla `Esc` while in ship context |
| Undock / Take-Off | `iUndockTakeOffButton` | Native `TakeOff` semantic action |
| Get Up | `iGetUpButton` | Native `SelectTarget` seat-exit lifecycle |
| Exit Ship From Cockpit | `iExitShipFromCockpitButton` | Native `ExitShip` semantic action |
| Zoom Camera In | `iZoomCameraInButton` | Active camera-state zoom handler |
| Zoom Camera Out | `iZoomCameraOutButton` | Active camera-state zoom handler |
| Autopilot On / Off | `iAutopilotOnOffButton` | Native `LockCourse` semantic action |

Press and release ownership is reference-counted. A physical binding, macro,
boost zone, and analog-strafe modifier can request the same action without one
owner releasing another owner's hold.

## Runtime safety

Native operations are available only while the selected ship handler validates.
Function bytes, object vtables, active camera state, and the current player-camera
state are checked at the relevant boundary. A mismatch fails closed and never
falls back to `SendInput`.

Every row above is suppressed outside the ship/targeting gate. A compatibility
`SendInput` route is still a ship route; it does not gain authority over menus.
Buttons held while the ship gate is closed are release-armed before they can act
again.

Targeting Mode is the one context-routed exception. Its component interface does
not consume the ShipHUD Left/Right arrow actions. While the exact targeting-camera
gate is active, Navigation Left and Right suppress their arrow output and publish
only native `SelectLeft` / `SelectRight` semantic events. This prevents component
selection from changing ship power simultaneously. The selector lane is released
immediately when Targeting Mode closes and is never active in menus or ordinary
flight; a physical direction held across that transition must be released before
it can resume power control.

## Optional dedicated menu navigation

`[MenuControls]` exposes six independent controller bindings, all unbound by
default: Menu Accept (`E`), Menu Cancel (`Esc`), and Menu Up/Down/Left/Right
(arrow keys). They are profile-aware, operate only in a known suspended menu
context, and release-arm when that context opens.

These rows are the normal way to give a HOTAS menu controls. They intentionally
do not reuse Select Target, Ship Cancel, or the four ship-system power bindings.

## Optional flight-control reuse

The **Menu Control Reuse** panel under **Flight Controls > Ship Buttons** can also
reuse the currently bound Pitch axis for Up/Down, Yaw for Left/Right, and Primary
Weapon (`iFireWeapon0Button`) for Select/Accept. Each function has its own opt-in;
vertical and horizontal direction can be inverted independently, and axis engage
and release thresholds are adjustable. The settings are serialized into profiles,
so a menu-oriented layer can differ from a flight layer.

Pitch, Yaw, and Primary Weapon reuse are menu-only. They never drive ship power
or Targeting Mode component selection.

This layer only acts when the runtime has a known suspended menu context. Each
axis must return to neutral after entry, and Primary Weapon must be released after
entry, before it can emit anything. This avoids accepting a prompt because the
trigger was already held or navigating because the stick was already deflected.
The reused keys still use the ref-counted raw `SendInput` path; custom bindings and
explicit raw macro targets remain available alongside it.

`UndockTakeOff` and `ExitShipFromCockpit` use the mapped native semantic routes,
but their final contextual fixtures still need beta coverage. Treat them as
test-track actions until both seated scenarios have been exercised in game.

## Raw custom bindings

`[ShipButtonOutputs]` is retained for 4.x parsing but does not override native
named actions. Explicit raw passthroughs remain available for other
menu helpers and non-ship commands:

```ini
[ButtonExpansion]
iButton99 = key:0x14
iButton100 = key:0x01
iButton101 = mouse:3
```

Raw `key:` and `mouse:` macro targets remain synthetic by design. Named macro
targets follow the native ship routes above.

For raw output values, see [key-output-reference.md](key-output-reference.md).

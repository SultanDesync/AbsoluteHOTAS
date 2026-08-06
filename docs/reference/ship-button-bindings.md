# AbsoluteHOTAS 5.0 Ship Button Bindings

AbsoluteHOTAS maps DirectInput buttons to 23 named Starfield ship actions. In
5.0, named actions enter validated internal ship-control paths; they do not emit
keyboard or mouse input and do not depend on `ControlMap_Custom.txt`.

The physical button side is configured in `[ShipButtons]`. IDs `1..128` are
physical DirectInput buttons and `129..144` are virtual POV/hat directions. Add
a `DeviceName@` prefix for multi-device setups and use `-1` to leave an action
unbound.

## Native action table

| Action | Physical button key | Native path |
| --- | --- | --- |
| Fire Boosters | `iFireBoostersButton` | Selected flight-handler request |
| Switch Flight Modes | `iSwitchFlightModesButton` | Selected flight-handler digital action |
| Toggle POV | `iTogglePovButton` | Current camera-state lifecycle |
| Fire Weapon 0 | `iFireWeapon0Button` | Weapon group 0 start/stop |
| Fire Weapon 1 | `iFireWeapon1Button` | Weapon group 1 start/stop |
| Fire Weapon 2 | `iFireWeapon2Button` | Weapon group 2 start/stop |
| Ship Action 1 | `iShipAction1Button` | Native `XButton` semantic action |
| Select Target | `iSelectTargetButton` | Direct target-selection wrapper |
| Increase System Power | `iIncreaseSystemPowerButton` | Native `Up` semantic action |
| Decrease System Power | `iDecreaseSystemPowerButton` | Native `Down` semantic action |
| Previous System | `iPreviousSystemButton` | Native `Left` semantic action |
| Next System | `iNextSystemButton` | Native `Right` semantic action |
| Open Scanner | `iOpenScannerButton` | Native `SHMonocle` semantic action |
| Repair | `iRepairButton` | Direct repair backend |
| Ship Alternate Control Hold | `iShipAlternateControlHoldButton` | Native `AltHold` semantic action |
| Cruise | `iCruiseButton` | Native `Cruise` semantic action |
| Cancel | `iCancelButton` | Native `Cancel` semantic action |
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

`UndockTakeOff` and `ExitShipFromCockpit` use the mapped native semantic routes,
but their final contextual fixtures still need beta coverage. Treat them as
test-track actions until both seated scenarios have been exercised in game.

## Raw custom bindings

`[ShipButtonOutputs]` is a retained 4.x compatibility section and no longer
controls named ship actions. Explicit raw passthroughs remain available for menu
helpers and non-ship commands:

```ini
[ButtonExpansion]
iButton99 = key:0x14
iButton100 = key:0x01
iButton101 = mouse:3
```

Raw `key:` and `mouse:` macro targets also remain synthetic by design. Named
macro targets such as `FireWeapon0` or `OpenScanner` use the native paths above.

For raw output values, see [key-output-reference.md](key-output-reference.md).

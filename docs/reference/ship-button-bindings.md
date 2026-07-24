# AbsoluteHOTAS Ship Button Bindings

AbsoluteHOTAS 4.0 can map DirectInput buttons to named Starfield spaceship actions and emit keyboard or mouse outputs through `SendInput`.

The physical button side is configured in `[ShipButtons]`. IDs `1..128` are physical DirectInput buttons and `129..144` are virtual POV/hat directions; use `-1` to leave an action unbound.

By default, the emitted keyboard/mouse side is resolved automatically from the player's current in-game bindings in `ControlMap_Custom.txt`. If no override exists or the file cannot be read, the plugin falls back to the vanilla Starfield output. An explicit `[ShipButtonOutputs]` value takes final precedence and disables reconciliation for that action.

## Why Use Plugin Button Bindings

The plugin reads physical HOTAS/HOSAS buttons through DirectInput, then emits the configured Starfield action output itself. This is often more predictable than asking an external mapper to simulate keyboard/mouse input directly into Starfield.

Plugin-polled DirectInput keeps the hardware path separate from Steam Input and helps avoid input-mode flicker. The emitted `SendInput` output is still a synthetic keyboard/mouse event, so Starfield must be foreground. Named ship actions stay aligned automatically; raw custom bindings and explicit output overrides must match an in-game binding chosen by the user.

Ship buttons should be treated as holds, not instant pulses. Starfield can miss very short synthetic key or mouse taps, especially across frame timing, focus, or Proton routing boundaries. AbsoluteHOTAS mirrors the physical DirectInput button duration: press sends down, release sends up. If an action is not registering reliably, hold the physical button slightly longer rather than trying to create a shorter pulse.

| Action | Physical button key | Optional override key | Vanilla fallback |
| --- | --- | --- | --- |
| Fire Boosters | `iFireBoostersButton` | `sFireBoostersOutput` | `key:0x2A` |
| Switch Flight Modes | `iSwitchFlightModesButton` | `sSwitchFlightModesOutput` | `key:0x39` |
| Toggle POV | `iTogglePovButton` | `sTogglePovOutput` | `key:0x10` |
| Fire Weapon 0 | `iFireWeapon0Button` | `sFireWeapon0Output` | `mouse:1` |
| Fire Weapon 1 | `iFireWeapon1Button` | `sFireWeapon1Output` | `mouse:2` |
| Fire Weapon 2 | `iFireWeapon2Button` | `sFireWeapon2Output` | `key:0x22` |
| Ship Action 1 | `iShipAction1Button` | `sShipAction1Output` | `key:0x13` |
| Select Target | `iSelectTargetButton` | `sSelectTargetOutput` | `key:0x12` |
| Increase System Power | `iIncreaseSystemPowerButton` | `sIncreaseSystemPowerOutput` | `key:0x48` |
| Decrease System Power | `iDecreaseSystemPowerButton` | `sDecreaseSystemPowerOutput` | `key:0x50` |
| Previous System | `iPreviousSystemButton` | `sPreviousSystemOutput` | `key:0x4B` |
| Next System | `iNextSystemButton` | `sNextSystemOutput` | `key:0x4D` |
| Open Scanner | `iOpenScannerButton` | `sOpenScannerOutput` | `key:0x21` |
| Repair | `iRepairButton` | `sRepairOutput` | `key:0x18` |
| Ship Alternate Control Hold | `iShipAlternateControlHoldButton` | `sShipAlternateControlHoldOutput` | `key:0x38` |
| Cruise | `iCruiseButton` | `sCruiseOutput` | `key:0x14` |
| Cancel | `iCancelButton` | `sCancelOutput` | `none` |
| Undock / Take-Off | `iUndockTakeOffButton` | `sUndockTakeOffOutput` | `key:0x39` |
| Get Up | `iGetUpButton` | `sGetUpOutput` | `key:0x12` |
| Exit Ship From Cockpit | `iExitShipFromCockpitButton` | `sExitShipFromCockpitOutput` | `key:0x2D` |
| Zoom Camera In | `iZoomCameraInButton` | `sZoomCameraInOutput` | `mouse:1` |
| Zoom Camera Out | `iZoomCameraOutButton` | `sZoomCameraOutOutput` | `mouse:2` |
| Autopilot On / Off | `iAutopilotOnOffButton` | `sAutopilotOnOffOutput` | `key:0x39` |

Example forcing outputs instead of using automatic reconciliation:

```ini
[ShipButtons]
iOpenScannerButton = 14
iCancelButton = 15

[ShipButtonOutputs]
sOpenScannerOutput = key:0x21
sCancelOutput = key:0x01
```

For the full keyboard/mouse output table, see [key-output-reference.md](key-output-reference.md).

## Extra Buttons

`[ButtonExpansion]` adds optional raw DirectInput-to-`SendInput` passthrough bindings without adding new named ship actions.

Use this for menu helpers, dialog helpers, or extra cockpit controls that should mirror a physical HOTAS button:

```ini
[ButtonExpansion]
iButton99 = key:0x14
iButton100 = key:0x01
iButton101 = mouse:3
```

`iButton99` means physical DirectInput button 99. Values use the same output formats as `[ShipButtonOutputs]`.

Extra button bindings mirror physical button duration. Pressing button 99 holds `key:0x14`; releasing button 99 releases it. Use this hold behavior even for actions that feel like single-shot commands; Starfield is more reliable when it sees the key held for at least a normal input frame.

Invalid keys, buttons outside `1..144`, `none`, and empty outputs are ignored. IDs `129..144` represent POV/hat directions captured by the wizard.

# AbsoluteHOTAS Ship Button Bindings

AbsoluteHOTAS 1.6 can map DirectInput buttons to Starfield spaceship actions and emit keyboard or mouse outputs through `SendInput`.

The physical button side is configured in `[ShipButtons]`. Each value is a 1-indexed DirectInput button ID from `1..128`; use `-1` to leave an action unbound.

The emitted keyboard/mouse side is configured in `[ShipButtonOutputs]`. If an output is omitted, the plugin uses the vanilla Starfield spaceship default for that action.

## Why Use Plugin Button Bindings

The plugin reads physical HOTAS/HOSAS buttons through DirectInput, then emits the configured Starfield action output itself. This is often more predictable than asking an external mapper to simulate keyboard/mouse input directly into Starfield.

Joystick Gremlin is still useful for shaping hardware into vJoy, but Starfield can receive simulated keyboard/mouse input inconsistently depending on focus, Steam Input, Proton routing, and mixed gamepad/KBM UI state. Plugin-polled DirectInput keeps the hardware path separate from Steam Input and helps avoid input-mode flicker. The emitted `SendInput` output is still a synthetic keyboard/mouse event, so Starfield must be foreground and the selected output must match an in-game binding.

Ship buttons should be treated as holds, not instant pulses. Starfield can miss very short synthetic key or mouse taps, especially across frame timing, focus, or Proton routing boundaries. AbsoluteHOTAS mirrors the physical DirectInput button duration: press sends down, release sends up. If an action is not registering reliably, hold the physical button slightly longer rather than trying to create a shorter pulse.

| Action | Physical button key | Output key | Vanilla output |
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

Example:

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

Invalid keys, buttons outside `1..128`, `none`, and empty outputs are ignored.

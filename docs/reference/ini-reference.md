# AbsoluteHOTAS INI Reference

Complete reference for `AbsoluteHOTAS.ini` key=value pairs. Most users should configure the plugin via the in-game wizard (`Ctrl+Alt+B`).

Managed profile files may also contain `[Profile] sKeyboardShortcut`. It accepts a
Windows virtual-key chord such as `key:0x11+0x31` (Ctrl+1), operates independently
of the profile's controller/custom activation, and can be set to `-1` to disable the
keyboard shortcut.

> **Tip**: The wizard saves to this INI on each **Save & Apply**, so manual edits and wizard changes coexist.

---

## [General]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bEnabled` | bool | `true` | Master enable for the plugin. |

---

## [UI]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bEnableWorkbench` | bool | `true` | Install the optional D3D12 in-game workbench. Set to `false` to bypass all renderer hooks while retaining flight controls and manual INI configuration. Requires a game restart. |

If a graphics injector conflicts with the workbench, temporarily disable that
injector, configure and save AbsoluteHOTAS, then set `bEnableWorkbench=false` in
`AbsoluteHOTAS_Custom.ini` before restoring the graphics stack. A renderer failure
also disables the workbench for the remainder of that game session without
disabling the controller.

---

## [Hardware]

Axis bindings use **HID Usage ID** syntax with optional device name prefix.

### Axis Binding Syntax

```
DeviceName@0xNN    — Bind to axis 0xNN on the named device
#2@0xNN            — Bind to axis 0xNN on device index 2 (disambiguation)
0xNN               — Bind to axis 0xNN on the first available device
(empty)            — Unbound (game retains native control)
```

### Usage ID Reference

| ID | Axis |
|----|------|
| `0x30` | X |
| `0x31` | Y |
| `0x32` | Z |
| `0x33` | Rx |
| `0x34` | Ry |
| `0x35` | Rz |
| `0x36` | Slider 0 |
| `0x37` | Slider 1 |

### Axis Keys

| Key | Description |
|-----|-------------|
| `iThrottleAxis` | Primary throttle axis |
| `iPitchAxis` | Pitch (nose up/down) |
| `iYawAxis` | Yaw (nose left/right) |
| `iRollAxis` | Roll |
| `iStrafeLatAxis` | Lateral strafe (left/right) |
| `iStrafeVertAxis` | Vertical strafe (up/down) |

### Per-Axis Modifiers

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bInvertThrottle` | bool | `false` | Flip throttle direction |
| `bInvertPitch` | bool | `false` | Flip pitch direction |
| `bInvertYaw` | bool | `false` | Flip yaw direction |
| `bInvertRoll` | bool | `false` | Flip roll direction |
| `bInvertStrafeLat` | bool | `false` | Flip lateral strafe |
| `bInvertStrafeVert` | bool | `false` | Flip vertical strafe |
| `fPitchSensitivity` | float | `1.0` | Pitch gain multiplier |
| `fYawSensitivity` | float | `1.0` | Yaw gain multiplier |
| `fRollSensitivity` | float | `1.0` | Roll gain multiplier |
| `fStrafeSensitivity` | float | `1.0` | Strafe gain multiplier |
| `fThrottleSensitivity` | float | `1.0` | Throttle gain multiplier (scales both unipolar and accumulator paths) |

### Saturation (Output Capping)

Caps the maximum output of each axis. At `0.80`, output is limited to 80% of full deflection. Range: `0.05` to `1.0`.

| Key | Default |
|-----|---------|
| `fThrottleSaturation` | `1.0` |
| `fPitchSaturation` | `1.0` |
| `fYawSaturation` | `1.0` |
| `fRollSaturation` | `1.0` |
| `fStrafeSaturation` | `1.0` |
| `fStrafeVertSaturation` | `1.0` |
| `fReverseSaturation` | `1.0` |

### Per-Axis Deadzones

Center deadzone for bipolar axes. After normalizing to [-1.0, +1.0], deflections within `±deadzone` read as zero. The remaining range is remapped to full scale. Range: `0.0` to `0.5`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `fThrottleDeadzone` | float | `0.0` | Throttle axis center deadzone (also used as accumulator minimum). Accumulator mode enforces a 5% floor. |
| `fPitchDeadzone` | float | `0.0` | Pitch axis center deadzone |
| `fYawDeadzone` | float | `0.0` | Yaw axis center deadzone |
| `fRollDeadzone` | float | `0.0` | Roll axis center deadzone |
| `fStrafeDeadzone` | float | `0.05` | Lateral strafe deadzone. Default 5% prevents accidental actuation on HOSAS left-stick X-axis. |
| `fStrafeVertDeadzone` | float | `0.05` | Vertical strafe deadzone |

---

## [Buttons]

Plugin control buttons. IDs 1–128 are physical DirectInput buttons and 129–144 are virtual POV/hat directions. Supports `DeviceName@ID` for multi-device. Set to `-1` to disable.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bAlwaysOn` | bool | `true` | Auto-arm discovery on DLL load. If `false`, requires manual Activate button press. |
| `iActivateButtonId` | int | `-1` | Arms signal discovery / resets flight hooks. Also available via `Ctrl+Alt+F8`. |
| `iStopButtonId` | int | `-1` | Force-disarms flight hooks. |
| `iToggleWizardButton` | int | `-1` | Opens/closes the binding wizard overlay (alternative to `Ctrl+Alt+B`). |

---

## [ControlExtensions]

Native AbsoluteHOTAS flight-assist commands. They are mutually exclusive toggles:
pressing a command latches its throttle target, pressing it again returns control to
the hardware axis, and pressing another command changes the target.

| Key | Default | Description |
|-----|---------|-------------|
| `iCruiseHoldButton` | `-1` | Capture and hold the current throttle target |
| `iFullStopButton` | `-1` | Hold throttle at 0% |
| `iCruiseHalfButton` | `-1` | Hold throttle at 50% |
| `iCruiseMaxButton` | `-1` | Hold throttle at 100% without firing boost |

---

## [ShipButtons]

Map physical buttons to ship actions. All use 1-indexed DirectInput button IDs with optional `DeviceName@` prefix. Set to `-1` to leave unbound.

| Key | Default | Description |
|-----|---------|-------------|
| `bShipButtonsEnabled` | `true` | Master enable for all ship button output |
| `iFireBoostersButton` | `-1` | Fire boosters |
| `iSwitchFlightModesButton` | `-1` | Toggle hover/cruise |
| `iTogglePovButton` | `-1` | Toggle POV camera |
| `iFireWeapon0Button` | `-1` | Fire primary weapon |
| `iFireWeapon1Button` | `-1` | Fire secondary weapon |
| `iFireWeapon2Button` | `-1` | Fire tertiary weapon |
| `iShipAction1Button` | `-1` | Ship action 1 |
| `iSelectTargetButton` | `-1` | Select target |
| `iIncreaseSystemPowerButton` | `-1` | Increase system power |
| `iDecreaseSystemPowerButton` | `-1` | Decrease system power |
| `iPreviousSystemButton` | `-1` | Previous system |
| `iNextSystemButton` | `-1` | Next system |
| `iOpenScannerButton` | `-1` | Open scanner |
| `iRepairButton` | `-1` | Repair |
| `iShipAlternateControlHoldButton` | `-1` | Ship alternate control (hold) |
| `iCruiseButton` | `-1` | Cruise |
| `iCancelButton` | `-1` | Cancel |
| `iUndockTakeOffButton` | `-1` | Undock / take off |
| `iGetUpButton` | `-1` | Get up from seat |
| `iExitShipFromCockpitButton` | `-1` | Exit ship from cockpit |
| `iZoomCameraInButton` | `-1` | Zoom camera in |
| `iZoomCameraOutButton` | `-1` | Zoom camera out |
| `iAutopilotOnOffButton` | `-1` | Autopilot toggle |

---

## [ShipButtonOutputs]

Optional keyboard/mouse overrides emitted when ship buttons are pressed. When a key is omitted, AbsoluteHOTAS reconciles the named action against the player's `ControlMap_Custom.txt` binding and falls back to the listed vanilla output only when necessary. An explicit value always wins.

**Format**: `key:0xNN` (DirectInput scancode), `mouse:1..4` (mouse button), or `none` (disabled).

See [key-output-reference.md](key-output-reference.md) for the full scancode table.

| Key | Default | Game Binding |
|-----|---------|-------------|
| `sFireBoostersOutput` | `key:0x2A` | Left Shift |
| `sSwitchFlightModesOutput` | `key:0x39` | Space |
| `sTogglePovOutput` | `key:0x10` | Q |
| `sFireWeapon0Output` | `mouse:1` | Left Click |
| `sFireWeapon1Output` | `mouse:2` | Right Click |
| `sFireWeapon2Output` | `key:0x22` | G |
| `sShipAction1Output` | `key:0x13` | R |
| `sSelectTargetOutput` | `key:0x12` | E |
| `sIncreaseSystemPowerOutput` | `key:0x48` | Up Arrow |
| `sDecreaseSystemPowerOutput` | `key:0x50` | Down Arrow |
| `sPreviousSystemOutput` | `key:0x4B` | Left Arrow |
| `sNextSystemOutput` | `key:0x4D` | Right Arrow |
| `sOpenScannerOutput` | `key:0x21` | F |
| `sRepairOutput` | `key:0x18` | O |
| `sShipAlternateControlHoldOutput` | `key:0x38` | Left Alt |
| `sCruiseOutput` | `key:0x14` | T |
| `sCancelOutput` | `none` | — |
| `sUndockTakeOffOutput` | `key:0x39` | Space |
| `sGetUpOutput` | `key:0x12` | E |
| `sExitShipFromCockpitOutput` | `key:0x2D` | X |
| `sZoomCameraInOutput` | `mouse:1` | Left Click |
| `sZoomCameraOutOutput` | `mouse:2` | Right Click |
| `sAutopilotOnOffOutput` | `key:0x39` | Space |

---

## [ButtonExpansion]

Free-form button → keyboard/mouse output passthroughs. Configure via the wizard Custom Bindings tab.

**Syntax**: `iButton<1..144> = key:0xNN` or `mouse:1..4` or `none` (129–144 are POV/hat directions)

Multi-device: `DeviceName@iButton42 = key:0x14`

---

## [Normalization]

Throttle calibration and reverse axis configuration.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bUnipolarMode` | bool | `true` | `true`: full travel = 0–100% thrust. `false`: center-detent bipolar mode. |
| `fIdlePlateau` | float | `0.05` | Bottom percentage of travel treated as idle. Increase for noisy pots (0.08–0.10). |
| `bReverseAxisEnabled` | bool | `true` | Enable dedicated reverse axis (requires `iReverseAxis` in `[Hardware]`). |
| `iDetentCenter` | int | `32768` | Center position for bipolar mode (only when `bUnipolarMode=false`). |
| `iDetentDeadzone` | int | `500` | Deadzone width around center for bipolar mode. |
| `bReverseEnabled` | bool | `false` | Legacy center-detent reverse (throttle axis below center = reverse). Only for bipolar mode. |
| `fReverseDeadzone` | float | `0.05` | Deadzone for the reverse axis. |
| `fReverseActivationThreshold` | float | `0.05` | Minimum reverse axis deflection to trigger reverse. |

### Reverse Axis Keys (in [Hardware])

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iReverseAxis` | binding | (empty) | Dedicated reverse axis binding |
| `bInvertReverse` | bool | `false` | Flip reverse axis direction |
| `fReverseSensitivity` | float | `1.0` | Reverse axis gain multiplier |

> **Warning**: Enabling `bReverseAxisEnabled` without binding `iReverseAxis` may cause phantom reverse input.

---

## [Injection]

Internal engine control. Most users should not modify these.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iPollRateHz` | int | `120` | DirectInput polling frequency in Hz |
| `iThrottleBurstMs` | int | `250` | Duration of throttle authority burst after movement (ms) |
| `bEnableLog` | bool | `false` | Write `AbsoluteHOTAS.log` (device enumeration, hook installation with addresses, errors, crashes). When off, nothing is written at all — not even crashes. Logs rotate at 1 MB. |
| `bHoldForBoost` | bool | `true` | Pause throttle injection while boost is held; cancel on release |
| `bRollEnabled` | bool | `true` | Enable roll axis injection |

---

## [DigitalAxes]

Bind buttons to emulate axis input digitally (full on/off). Useful for hat switches.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iDigitalReverseButton` | int | `-1` | Digital reverse toggle |
| `iDigitalRollLeftButton` | int | `-1` | Digital roll left |
| `iDigitalRollRightButton` | int | `-1` | Digital roll right |
| `iDigitalStrafeLeftButton` | int | `-1` | Digital strafe left |
| `iDigitalStrafeRightButton` | int | `-1` | Digital strafe right |
| `iDigitalStrafeUpButton` | int | `-1` | Digital strafe up |
| `iDigitalStrafeDownButton` | int | `-1` | Digital strafe down |
| `fDigitalRollValue` | float | `1.0` | Roll deflection when digital button held (0.0–1.0) |
| `fDigitalStrafeValue` | float | `1.0` | Strafe deflection when digital button held (0.0–1.0) |

---

## [Calibration]

Auto-generated by the wizard's per-axis calibration feature. Do not edit manually unless you know the exact min/max values from your hardware.

**Format**: `iCalib_<deviceIndex>_0x<usageHex> = min,max`

Example: `iCalib_0_0x32 = 120,65400`

---

## [Aim]

Drives the ship's aiming reticle independently from steering via the game's mouse accumulator pathway (`source+0x4C` = yaw, `source+0x50` = pitch). This works regardless of controller mode state.

### Core Settings

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bSourceObjectAim` | bool | `true` | Master enable for aim injection |
| `fAimSensitivity` | float | `1.0` | Global aim sensitivity multiplier |
| `bMirrorFlightToAim` | bool | `true` | Mirror flight stick to reticle when no aim axes are bound |

### Separated Analog Aim Axes

Bind a secondary analog input (thumbstick, analog hat) to independently drive the reticle. Uses the same binding syntax as `[Hardware]`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iAimYawAxis` | binding | (empty) | Aim yaw axis |
| `iAimPitchAxis` | binding | (empty) | Aim pitch axis |
| `fAimYawSensitivity` | float | `1.0` | Per-axis yaw sensitivity |
| `fAimPitchSensitivity` | float | `1.0` | Per-axis pitch sensitivity |
| `bInvertAimYaw` | bool | `false` | Flip aim yaw direction |
| `bInvertAimPitch` | bool | `false` | Flip aim pitch direction |
| `fAimSmoothing` | float | `0.0` | EMA smoothing filter for low-resolution analog sensors. `0.0` = off (raw input), up to `0.98` = maximum smoothing. |

### Digital Aim Override (5-Way)

Bind buttons to move the reticle like a virtual cursor. Hold a direction to accumulate position. Release to hold. Center resets to `(0,0)`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iDigitalAimLeftButton` | int | `-1` | Aim left |
| `iDigitalAimRightButton` | int | `-1` | Aim right |
| `iDigitalAimUpButton` | int | `-1` | Aim up |
| `iDigitalAimDownButton` | int | `-1` | Aim down |
| `iDigitalAimCenterButton` | int | `-1` | Reset reticle to center |
| `fDigitalAimValue` | float | `1.0` | Travel speed (full deflection per second) |

### Aim Mode Toggle

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iToggleAimModeButton` | int | `-1` | Toggle between Independent Aim and Aim-Driven Steering at runtime. Only useful when aim axes are bound. |

### HOSAM Mode (Stick + Mouse)

HOSAM (Hands On Stick And Mouse) mode releases the pitch and yaw cluster gates so that the game's native mouse steering pipeline drives ship rotation. Throttle, strafe, and roll remain under plugin control. This enables left-hand joystick + right-hand mouse setups.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bHOSAMMode` | bool | `false` | Enable HOSAM mode. When `true`, the plugin releases pitch/yaw gates to the game's native mouse pipeline. |
| `bAlignmentAssist` | bool | `false` | Enable alignment assist. When the mouse is idle near center, gently decay steering toward `(0,0)`. Requires `bHOSAMMode = true`. |
| `fAlignmentRadius` | float | `15.0` | Mouse accumulator radius (0–200) within which alignment assist triggers. At `15.0`, the assist activates when the reticle is within ~7.5% of center. |
| `iAlignmentIdleMs` | int | `80` | Milliseconds the mouse must be idle before the decay begins. At 120Hz polling, this is ~10 frames. Range: `0`–`2000`. |
| `fAlignmentDecayRate` | float | `4.0` | Exponential decay speed. At `4.0`, 95% of remaining offset decays in ~0.75 seconds. Higher values = faster centering. Range: `0.1`–`50.0`. |

---

## [DualStick]

Self-centering throttle accumulator mode for dual-stick and self-centering throttle setups. When enabled, the throttle axis is treated as a **rate input** (deflection controls how fast throttle ramps) rather than an absolute position.

Reverse throttle requires velocity awareness: the ship must reach near-zero speed before negative throttle values are applied. This prevents fighting the engine's forward momentum with direct negative writes.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bAccumulatorThrottle` | bool | `false` | Enable rate-based throttle accumulation. When `true`, the throttle axis drives accumulation rate rather than absolute position. |
| `fAccumulatorRate` | float | `1.0` | Throttle units per second at full stick deflection. At `1.0`, full forward stick reaches max throttle in 1 second. Range: `0.1`–`10.0`. |
| `fAccumulatorDecay` | float | `2.0` | Throttle units per second of decay when the stick is centered. At `2.0`, throttle drops from `1.0` to `0.0` in 0.5 seconds. Range: `0.0`–`20.0`. Set to `0.0` to disable decay (throttle holds its position). |
| `fReverseGateVelocity` | float | `5.0` | Ship velocity (m/s, HUD-displayed) below which reverse throttle is allowed. While velocity exceeds this, backward stick writes `0.0` to decelerate instead of writing negative throttle. Range: `0.0`–`100.0`. |

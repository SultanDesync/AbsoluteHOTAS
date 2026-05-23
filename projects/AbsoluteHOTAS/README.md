# AbsoluteHOTAS 1.6.2

Experimental SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield.

This build gives direct HOTAS authority for ship pitch, yaw, roll, and throttle. It also lets DirectInput buttons emit configurable keyboard/mouse outputs for Starfield spaceship actions.

## Requirements

- Starfield 1.16.242
- SFSE 0.2.20
- Latest Starfield Address Library
- vJoy
- Joystick Gremlin

Install `AbsoluteHOTAS.dll` and `AbsoluteHOTAS.ini` to:

```text
Data\SFSE\Plugins\
```

## Recommended Setup

You can configure `AbsoluteHOTAS.ini` manually, or use the optional
[AbsoluteHOTAS Configurator](https://github.com/SultanDesync/AbsoluteHOTAS-Configurator)
to select devices, record button IDs, and write the INI.

Use vJoy for flight axes only:

- X: yaw
- Y: pitch
- Z: throttle
- Rx/Ry/Rz/Slider: roll, strafe, and reverse axes as configured

Use Joystick Gremlin for shaping physical hardware into vJoy axes. For HOTAS buttons, hats, toggles, and mode switches, prefer the plugin's `[ShipButtons]` / `[ButtonExpansion]` DirectInput path when you want to avoid Steam Input or mixed-input flicker. Joystick Gremlin keyboard/mouse simulation can work, but Starfield may receive those simulated inputs inconsistently depending on focus, Steam Input, Proton routing, and UI input mode.

Examples:

- hat up/down/left/right -> power management keys
- trigger/buttons -> weapon keys
- throttle button -> boost
- reverse/brake slider -> vJoy reverse axis

Do not use Steam Input or Steam controller bindings for this setup. Steam controller translation can add another input layer between the HOTAS and Starfield, which makes behavior harder to diagnose and can fight the plugin.

`Ctrl+Alt+F8` is reserved as a hard-coded keyboard fail-safe reset. It re-arms the plugin hooks even when no HOTAS activation button is available.

## Ship Buttons

AbsoluteHOTAS can also consume HOTAS/vJoy DirectInput buttons from `[ShipButtons]` in `AbsoluteHOTAS.ini`. The optional [AbsoluteHOTAS Configurator](https://github.com/SultanDesync/AbsoluteHOTAS-Configurator) records the source button ID, and the plugin emits the vanilla Starfield keyboard or mouse input for that ship action. Reverse/brake is handled separately by the reverse slider memory-injection path.

Input devices are selected per input family:

```ini
[InputDevices]
sAxisDeviceName = vJoy
iAxisDeviceIndex = 0
sShipButtonDeviceName = VKB
iShipButtonDeviceIndex = 0
```

Device names match DirectInput instance or product names case-insensitively. If a name is empty, the corresponding 0-based index is used as a DirectInput enumeration fallback.

Button IDs are 1-indexed DirectInput buttons from `1..128`. Set an action to `-1` to disable it, or set `bShipButtonsEnabled = false` to leave all ship button output to Joystick Gremlin or another tool. Ship outputs mirror physical DirectInput button duration: press sends key/mouse down, release sends key/mouse up. Treat them as holds, not instant pulses; Starfield can miss very short synthetic taps.

`[ShipButtonOutputs]` is optional. When it is omitted, the plugin uses the vanilla Starfield defaults. Supported override formats are:

```ini
sOpenScannerOutput = key:0x21
sFireWeapon0Output = mouse:1
sCancelOutput = none
```

Keyboard values are scan codes. Mouse values support `mouse:1` left, `mouse:2` right, `mouse:3` middle, and `mouse:4` XBUTTON1. Synthetic input is delivered through Windows `SendInput`, so Starfield must be the foreground window for reliable in-game behavior.

For manual setup, see:

- [Ship button binding table](../../docs/reference/ship-button-bindings.md)
- [Keyboard/mouse output reference](../../docs/reference/key-output-reference.md)

Optional `[ButtonExpansion]` entries can also map extra physical DirectInput buttons directly to keyboard or mouse outputs, for example `iButton99 = key:0x14`. These are useful for menu or dialog helpers and are documented in the ship button binding table.

## Throttle Calibration

Edit `AbsoluteHOTAS.ini`:

```ini
iThrottleAxis = 0x32
bInvertThrottle = false
iReverseAxis = 0x36
bReverseEnabled = false
bReverseAxisEnabled = true
bUnipolarMode = true
```

The default beta setup is unipolar throttle: physical minimum is 0% thrust and physical maximum is 100% thrust. Reverse/brake should use the dedicated reverse slider fields:

Set `bInvertThrottle = true` if your throttle axis reports physical minimum as maximum thrust and physical maximum as idle.

- `iReverseAxis`
- `fReverseSensitivity`
- `bInvertReverse`
- `fReverseDeadzone`
- `fReverseActivationThreshold`
- `bReverseAxisEnabled`

Leave `bReverseEnabled = false` unless using legacy center-detent throttle reverse. It is separate from `bReverseAxisEnabled`.

Only change `iDetentCenter` if you set `bUnipolarMode=false` for a centered throttle.

Common values:

- `0..65535` axis range: midpoint `32768`
- `0..32768` axis range: midpoint `16384`

If your hardware detent is offset, use the raw value shown in vJoy Monitor at the detent.

## Tuning

Throttle authority is applied as a short burst when the physical throttle moves, then released back to Starfield physics.

```ini
iPollRateHz = 120
iThrottleBurstMs = 250
```

- Lower `iThrottleBurstMs` for a softer, more vanilla feel.
- Set `iThrottleBurstMs = 0` for an instant one-frame throttle command.
- Raise it if throttle changes feel too weak.

Holding `S` releases throttle authority so vanilla reverse/brake behavior can take over.

## HOSAS Incremental Throttle & Physics Adherence (Alpha)

This release includes experimental flight modes for dual-joystick HOSAS configurations using a centering, spring-loaded axis for speed control.

### Incremental Rate Throttle

Rather than a direct 1:1 mapping, **Incremental Mode** treats physical displacement as a rate accelerator/decelerator. When the stick returns to center, the target speed cruise-locks where it is so Starfield's autopilot, boost decay, and landing/takeoff sequences can continue without fighting a spring-centered throttle.

### Keyboard Emulation Pulse (Simple HOSAS Mode)

For pilots who want a robust alternate bindings pipeline with no downstream throttle memory override, **Keyboard Emulation Pulse Mode** dynamically modulates standard `W` and `S` keyboard strokes. Pushing the stick further scales the key-press pulse frequency from a soft **230ms** down to a rapid-fire **20ms**. Because it sends standard keyboard keys, the game stays in keyboard/mouse mode while pitch, yaw, and roll can still use analog memory overrides.

### Maneuver Physics Adherence

Continuous throttle injection can override the game's natural speed envelope during sharp turns, making maneuvers feel wider. **Physics Adherence** automatically suspends memory injection when pitch/yaw deflection exceeds a threshold while throttle is high. This lets Starfield's native flight physics limit ship speed during turns while still allowing manual throttle feathering below the threshold.

Enable these features in your `AbsoluteHOTAS.ini` under the `[Normalization]` block:

```ini
[Normalization]
; Enable relative rate accumulation for spring-to-center sticks
bIncrementalThrottleMode = false
fThrottleRampRate = 0.67

; Enable turn physics speed-limiting adherence
bPhysicsAdherenceMode = false
fPhysicsAdherenceDeflection = 0.15
fPhysicsAdherenceThrottleThreshold = 0.50

; Enable keyboard pulse emulation mode
bIncrementalKeyboardMode = false
```

## Logging

Logging is off by default:

```ini
bLogThrottle = false
```

Set it to `true` only when diagnosing input behavior.

## Notes

This is a beta/experimental build. Expect tuning differences between HOTAS hardware, vJoy resolution, and Joystick Gremlin profiles.

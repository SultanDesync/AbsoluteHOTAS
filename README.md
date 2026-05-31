# AbsoluteHOTAS 2.0 Beta

Direct HID SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield — no vJoy or Joystick Gremlin required.

This build reads DirectInput devices natively and provides direct authority for ship pitch, yaw, roll, strafe, and throttle via memory injection. It includes an in-game binding wizard and configurable button-to-keyboard/mouse mapping for all ship actions.

## ⚠️ Required: StarfieldCustom.ini Setup

**You must add the following to your `StarfieldCustom.ini` or the plugin will not work.** This sets a known signal value that the plugin uses to locate the flight control memory structure at runtime.

Add to `Documents\My Games\Starfield\StarfieldCustom.ini`:

```ini
[Spaceship]
fThrottleAtEngineStart = 0.0314
```

> **Without this line, the plugin cannot discover the flight control cluster and will not inject any input.** If using MO2, add it to the profile's `StarfieldCustom.ini` instead.

## What's New in 2.0

- **Direct HID input** — Reads your HOTAS hardware directly via DirectInput. No vJoy or Joystick Gremlin dependency.
- **In-game binding wizard** — Press `Ctrl+Alt+B` to open the ImGui overlay. Bind axes and buttons by moving/pressing them on your hardware.
- **Multi-device support** — Bind axes and buttons across multiple devices using `DeviceName@UsageID` syntax (e.g., `My Throttle@0x32`).
- **Per-axis settings** — Inversion toggles and sensitivity sliders per axis, configurable in-game.
- **22 ship action bindings** — Fire weapons, boost, power management, scanner, target select, and more — all bindable to physical HOTAS buttons.
- **Digital axis buttons** — Bind hat switches or buttons to emulate axis input for roll, strafe, and reverse.
- **Live config reload** — Bindings saved from the wizard take effect immediately without restarting the game.
- **Mouse cursor support** — The overlay captures input and renders a cursor; the game pauses input processing while the wizard is open.
- **Silent logging** — No log file writes unless `bLogThrottle = true` in the INI (startup banner always writes).

## Requirements

- Starfield 1.16.242
- SFSE 0.2.20
- Latest Starfield Address Library

Install `AbsoluteHOTAS.dll` and `AbsoluteHOTAS.ini` to:

```text
Data\SFSE\Plugins\
```

Or install via MO2 using the provided archive.

## Quick Start

1. Install the plugin files.
2. Launch the game via SFSE.
3. Press `Ctrl+Alt+B` to open the binding wizard.
4. Go to the **Axes & Settings** tab, click **Bind** next to each axis, and move the physical control.
5. Go to the **Ship Actions** tab and bind buttons to ship functions (boost, weapons, power, etc.).
6. Click **Save & Apply**. Bindings take effect immediately.

`Ctrl+Alt+F8` is reserved as a keyboard fail-safe reset for the flight control hooks.

## In-Game Binding Wizard

Press `Ctrl+Alt+B` to toggle the overlay. The wizard has five tabs:

| Tab | Purpose |
|-----|---------|
| **Devices** | Live readout of all connected DirectInput devices, axes, and buttons. |
| **Axes & Settings** | Bind flight axes (throttle, pitch, yaw, roll, strafe, reverse) with inversion and sensitivity controls. |
| **Control Buttons** | Bind activate/stop buttons for the plugin's signal hunter. |
| **Ship Actions** | Bind physical buttons to 22 ship functions (boost, weapons, power management, scanner, etc.). |
| **Digital Axes** | Bind buttons to emulate axis input for roll, strafe, and reverse. |

## Manual Configuration

You can also edit `AbsoluteHOTAS.ini` directly. Axes use HID Usage ID syntax with optional device name prefix:

```ini
[Hardware]
; Usage IDs: 0x30=X, 0x31=Y, 0x32=Z, 0x33=Rx, 0x34=Ry, 0x35=Rz, 0x36=Slider0, 0x37=Slider1
iThrottleAxis = My Throttle@0x32
iPitchAxis = My Stick@0x31
iYawAxis = My Stick@0x30
iRollAxis = My Pedals@0x33

; Per-axis inversion
bInvertPitch = true
bInvertThrottle = false

; Per-axis sensitivity (multiplier)
fPitchSensitivity = 1.0
fYawSensitivity = 1.0
```

Device names are matched case-insensitively against DirectInput instance or product names. If no device name is prefixed, the plugin uses the first device with that usage.

## Ship Buttons

Button IDs are 1-indexed DirectInput buttons (1–128). Prefix with device name to target a specific device:

```ini
[ShipButtons]
bShipButtonsEnabled = true
iFireBoostersButton = VKB Gunfighter@44
iFireWeapon0Button = VKB Gunfighter@1
iSelectTargetButton = VKB Gunfighter@2
iIncreaseSystemPowerButton = -1
```

Set an action to `-1` to disable it. Each ship button emits a configurable keyboard or mouse output to Starfield. Default outputs match Starfield's vanilla bindings.

Override outputs in `[ShipButtonOutputs]`:

```ini
[ShipButtonOutputs]
sOpenScannerOutput = key:0x21
sFireWeapon0Output = mouse:1
sCancelOutput = none
```

## Throttle Calibration

```ini
[Normalization]
bUnipolarMode = true          ; 0% to 100% throttle range
fIdlePlateau = 0.05           ; Bottom 5% treated as idle
bReverseEnabled = false       ; Legacy center-detent reverse
bReverseAxisEnabled = true    ; Dedicated reverse slider
iDetentCenter = 16384         ; Only for bUnipolarMode=false
```

Set `bInvertThrottle = true` if your throttle reports minimum as maximum thrust.

## Tuning

```ini
[Injection]
iPollRateHz = 120             ; DirectInput polling rate
iThrottleBurstMs = 250        ; Throttle authority burst duration
bLogThrottle = false          ; Enable verbose logging
```

## HOSAS Modes (Experimental)

For dual-joystick setups with spring-to-center throttle axes:

```ini
[Normalization]
bIncrementalThrottleMode = false    ; Rate-based throttle accumulation
fThrottleRampRate = 0.67

bPhysicsAdherenceMode = false       ; Suspend injection during hard turns
fPhysicsAdherenceDeflection = 0.15
fPhysicsAdherenceThrottleThreshold = 0.50

bIncrementalKeyboardMode = false    ; Pure keyboard pulse emulation (zero UI flicker)
```

## Logging

Logging is **off by default**. Only the startup banner (4 lines) is written to confirm the plugin loaded.

Set `bLogThrottle = true` to enable full diagnostic logging. Logs rotate at 1 MB.

## Migration from 1.x

- **vJoy and Joystick Gremlin are no longer required.** Remove them from your setup unless you use them for other games.
- **`iBoostButtonId` is deprecated.** Use `iFireBoostersButton` in `[ShipButtons]` instead.
- **`bAlwaysOn` defaults to `true`.** The plugin arms automatically on load. Activate/stop buttons are optional.
- All axis bindings default to empty. Use the in-game wizard (`Ctrl+Alt+B`) or edit the INI manually.

## Notes

This is a beta build. Axis enumeration varies across hardware — if an axis doesn't map correctly via the wizard, you may need to manually adjust the usage ID in the INI. Report hardware-specific issues with your device names and the plugin log (`bLogThrottle = true`).

# AbsoluteHOTAS v2.2.3

Direct HID SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield — no vJoy or Joystick Gremlin required.

This plugin reads DirectInput devices natively and provides direct authority for ship pitch, yaw, roll, strafe, and throttle via memory injection. It includes an in-game binding wizard and configurable button-to-keyboard/mouse mapping for all ship actions.

## ⚠️ Required: StarfieldCustom.ini Setup

**You must add the following to your `StarfieldCustom.ini` or the plugin will not work.** This sets a known signal value that the plugin uses to locate the flight control memory structure at runtime.

Add to `Documents\My Games\Starfield\StarfieldCustom.ini`:

```ini
[Spaceship]
fThrottleAtEngineStart = 0.0314
```

> **Without this line, the plugin cannot discover the flight control cluster and will not inject any input.** If using MO2, add it to the profile's `StarfieldCustom.ini` instead.

## ⚠️ Antivirus / Threat Protection Notice

AbsoluteHOTAS uses **memory injection** (`SendInput`, DirectInput polling, and in-process memory writes) to inject HOTAS input into Starfield's flight control system. These are standard game modding techniques, but they look identical to the methods used by keyloggers and game cheats — which is exactly what antivirus heuristics are designed to flag.

**You may see a Windows Defender / antivirus warning when installing or running `AbsoluteHOTAS.dll`.** This is a false positive. The plugin:

- Calls `SendInput()` to emit keyboard/mouse events mapped to your HOTAS buttons — the same Win32 API used by AutoHotKey, vJoy, and accessibility tools.
- Hooks Direct3D12 `Present` to render the ImGui overlay — standard technique used by RivaTuner, MSI Afterburner, and SFSE itself.
- Writes to Starfield's in-process memory to inject throttle/axis values — same approach as every SFSE plugin.

**To resolve:** Add an exception for `AbsoluteHOTAS.dll` in your antivirus, or review the source code directly here on GitHub.

## Key Features

* **Direct HID Input** — Reads your HOTAS/HOSAS hardware directly via DirectInput. No vJoy or Joystick Gremlin dependency.
* **In-Game Binding Wizard** — Press `Ctrl+Alt+B` (or bind a controller button) to open the ImGui overlay. Bind axes and buttons by moving/pressing them on your hardware in real-time.
* **Multi-Device Support** — Bind axes and buttons across multiple devices using `DeviceName@UsageID` syntax (e.g., `My Throttle@0x32`).
* **Per-Axis Calibration & Tuning** — Calibrate physical axis limits in-game (compensating for low-resolution ADCs or worn pots) and tune inversion/sensitivity sliders on the fly.
* **22 Ship Action Bindings** — Map physical buttons to flight functions (boost, weapons, power management, scanner, target selection, etc.).
* **Custom Bindings** — Map any controller button to emit any keyboard/mouse output. Includes a one-click "Add Menu Cluster" preset for quick menu navigation (WASD/Tab/E/Esc).
* **Digital Axis Buttons** — Bind hat switches or buttons to emulate axis input for roll, strafe, and reverse.
* **Live Config Reload** — Bindings saved from the wizard take effect immediately without restarting the game.
* **Mouse Cursor Support** — The overlay captures input and renders a cursor; the game pauses input processing while the wizard is open.
* **Silent logging** — Diagnostic logging is disabled by default to minimize performance impact, and can be toggled on via `bLogThrottle = true` in the INI.

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

Press `Ctrl+Alt+B` to toggle the overlay (or bind a HOTAS button via the **Toggle Wizard** slot in Control Buttons). The wizard has six tabs:

| Tab | Purpose |
|-----|---------|
| **Devices** | Live readout of all connected DirectInput devices, axes, and buttons. Per-axis calibration. |
| **Axes & Settings** | Bind flight axes (throttle, pitch, yaw, roll, strafe, reverse) with inversion and sensitivity controls. |
| **Control Buttons** | Bind activate/stop/toggle-wizard buttons for the plugin's signal hunter. |
| **Ship Actions** | Bind physical buttons to 22 ship functions (boost, weapons, power management, scanner, etc.). |
| **Digital Axes** | Bind buttons to emulate axis input for roll, strafe, and reverse. |
| **Custom Bindings** | Free-form button → keyboard/mouse output mapping. Menu Cluster preset for WASD/Tab/E/Esc. |

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

## Notes

Axis enumeration varies across hardware — if an axis doesn't map correctly via the wizard, you may need to manually adjust the usage ID in the INI. Report hardware-specific issues with your device names and the plugin log (`bLogThrottle = true`).

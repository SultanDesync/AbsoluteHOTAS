# AbsoluteHOTAS v2.5.2

Direct HID SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield — no vJoy or Joystick Gremlin required.

This plugin reads DirectInput devices natively and provides direct authority for ship pitch, yaw, roll, strafe, and throttle via memory injection. It includes an in-game binding wizard and configurable button-to-keyboard/mouse mapping for all ship actions.

## ⚠️ Required: StarfieldCustom.ini Setup

**You must add the following to your `StarfieldCustom.ini` or the plugin will not work.** This sets a known signal value that the plugin uses to locate the flight control memory structure at runtime.

Add to `Documents\My Games\Starfield\StarfieldCustom.ini`:

```ini
[Spaceship]
fThrottleAtEngineStart = 0.0314
```

> **Without this line, the plugin cannot discover the flight control cluster and will not inject any input.** If using a mod manager, add it to the profile's `StarfieldCustom.ini` instead.

> **Plugin Fails to Load / DLL Blocked?** If the plugin fails to load or shows error `000011CC`, right-click `AbsoluteHOTAS.dll`, select **Properties**, check the **Unblock** box at the bottom of the General tab, and click **Apply**. For virtualizing mod managers like MO2, see the [Mod Manager Compatibility](#mod-manager-compatibility) section at the bottom.

## Key Features

* **Direct HID Input** — Reads your HOTAS/HOSAS hardware directly via DirectInput. No vJoy or Joystick Gremlin dependency.
* **In-Game Binding Wizard** — Press `Ctrl+Alt+B` (or bind a controller button) to open the ImGui overlay. Bind axes and buttons by moving/pressing them on your hardware in real-time.
* **Frame Generation Support** — Fully compatible with Starfield's built-in Frame Generation (FSR3 Frame Gen). The overlay automatically handles swap chain resizing and reinitializes seamlessly when toggled in-game.
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

Or install via your mod manager using the provided archive.

## Quick Start

1. Install the plugin files.
2. Launch the game via SFSE.
3. Press `Ctrl+Alt+B` to open the binding wizard (it is recommended to do this from the main menu or pause menu rather than while active in the pilot seat).
4. Go to the **Axes & Settings** tab, click **Bind** next to each axis, and move the physical control.
5. Go to the **Ship Actions** tab and bind buttons to ship functions (boost, weapons, power, etc.).
6. Click **Save & Apply**. Bindings take effect immediately.

`Ctrl+Alt+F8` is reserved as a keyboard fail-safe reset for the flight control hooks.

## In-Game Binding Wizard

Press `Ctrl+Alt+B` to toggle the overlay (it is recommended to access the wizard from the main menu or pause menu to avoid active control conflicts, though it can also be toggled from a HOTAS button bound via the **Toggle Wizard** slot in Control Buttons). The wizard has seven tabs:

| Tab | Purpose |
|-----|---------|
| **Devices** | Live readout of all connected DirectInput devices, axes, and buttons. Per-axis calibration. |
| **Axes & Settings** | Bind flight axes (throttle, pitch, yaw, roll, strafe, reverse) with inversion and sensitivity controls. |
| **Control Buttons** | Bind activate/stop/toggle-wizard buttons for the plugin's signal hunter. |
| **Ship Actions** | Bind physical buttons to 22 ship functions (boost, weapons, power management, scanner, etc.). |
| **Digital Axes** | Bind buttons to emulate axis input for roll, strafe, and reverse. |
| **Aiming** | Bind separate aim axes or digital buttons to drive the ship reticle independently from steering. |
| **Custom Bindings** | Free-form button → keyboard/mouse output mapping. Menu Cluster preset for WASD/Tab/E/Esc. |

## Frame Generation Support

Frame Generation (FSR3 Frame Gen) is fully supported. Enabling or disabling Frame Generation in-game causes the overlay to reinitialize automatically. The overlay will be available again within one or two frames of the next `Ctrl+Alt+B` press. FSR3 upscaling (without frame gen) is unaffected and has always worked normally.

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

## Dual-Stick / HOSAS Accumulator Mode

For HOSAS (Hands On Throttle And Stick) or dual-joystick setups where the left stick's Y-axis (or another axis) self-centers, a traditional absolute throttle mapping is difficult to use. To address this, AbsoluteHOTAS provides a **Velocity-Aware Throttle Accumulator** mode.

In this mode, the throttle stick behaves as a **rate-of-change controller** rather than mapping directly to absolute throttle percentage:
* **Push Forward**: Accumulates/ramps up throttle.
* **Release to Center (Neutral)**: Throttle decays back to zero (the decay rate is customizable, or can be set to zero to "hold" the current throttle setting).
* **Pull Backward**: Decelerates the ship toward 0% throttle, then triggers reverse thrusters once the ship slows down.

### Basic Logic & Reverse Gate

To handle reverse thrust smoothly in Starfield's flight model, the accumulator uses a **Velocity Gate**:
1. **At Speed**: Pulling back on the stick acts as a brake, reducing absolute throttle to zero to let the ship decelerate.
2. **Below the Gate**: Once the ship's current velocity drops below the configurable **Reverse Gate Velocity** (default `5.0` m/s on the HUD), pulling back on the stick activates the game's backward/reverse thrusters.
3. This prevents fighting the engine's forward momentum with direct negative values, resulting in smooth transitions.

### How to Bind and Configure

#### Option A: In-Game Binding Wizard (Recommended)
1. Press `Ctrl+Alt+B` to open the wizard.
2. Go to the **Axes & Settings** tab.
3. Under **Flight Axes**, find **Throttle** and click **Bind**. Move your self-centering joystick axis (e.g., Left Stick Y-axis).
4. Scroll down to the **Dual-Stick / Accumulator Mode** collapsible panel under throttle calibration and click to expand it.
5. Check **Enable Accumulator Mode**.
6. Set the parameters to your preference:
   * **Ramp Rate**: How fast the throttle ramps up when the stick is fully deflected forward (in throttle units per second).
   * **Decay Rate**: How fast the throttle decays back to zero when you release the stick. Set to `0.0` to disable decay and lock your current throttle setting when the stick centers.
   * **Reverse Gate Velocity**: The HUD speed threshold below which backward stick deflection engages reverse thrusters.
7. Click **Save & Apply**.

#### Option B: Manual INI Configuration
Edit `AbsoluteHOTAS.ini` and configure the following sections:

```ini
[Hardware]
iThrottleAxis = Left Stick@0x31  ; Map throttle to self-centering axis
fThrottleSensitivity = 1.0       ; Scales rate input sensitivity
fThrottleDeadzone = 0.05         ; Center deadzone (minimum floor is 5%)

[DualStick]
bAccumulatorThrottle = true      ; Enable the accumulator
fAccumulatorRate = 1.0           ; Seconds to reach 100% (1.0 = 1 sec)
fAccumulatorDecay = 2.0          ; Decay speed on center (2.0 = 0.5 sec to 0%)
fReverseGateVelocity = 5.0       ; Engage reverse under this HUD speed (m/s)
```

## Aiming

The plugin can drive the ship's aiming reticle independently from steering via the source-object mouse accumulator pathway. This works regardless of controller mode and provides full analog precision.

```ini
[Aim]
bSourceObjectAim = true       ; Master enable for aim injection
bMirrorFlightToAim = true     ; Mirror flight stick to reticle when no aim axes bound
iAimYawAxis =                 ; Separate aim yaw axis (DeviceName@0xNN syntax)
iAimPitchAxis =               ; Separate aim pitch axis
fAimSensitivity = 1.0         ; Global aim sensitivity multiplier
fAimSmoothing = 0.0           ; EMA smoothing for low-res sensors (0=off, 0.98=max)
```

Digital aim buttons (5-way directional) can also be bound via the wizard Aiming tab to move the reticle like a virtual cursor. A toggle button can switch between independent aim and aim-driven steering at runtime.

## HOSAM Mode (Stick + Mouse)

For players using a left-hand joystick for throttle/strafe and a mouse for steering, **HOSAM (Hands On Stick And Mouse)** mode releases the pitch and yaw cluster gates so the game's native mouse steering pipeline drives ship rotation. Throttle, strafe, and roll remain under plugin control.

Mice don't self-center like joysticks, so an optional **Alignment Assist** feature observes the mouse accumulator and gently decays the ship's steering back to neutral when the mouse is idle near center. This prevents the "drift problem" where the ship keeps turning after you stop moving the mouse.

### How to Bind and Configure

#### Option A: In-Game Binding Wizard (Recommended)
1. Press `Ctrl+Alt+B` to open the wizard.
2. Go to the **Axes & Settings** tab.
3. Bind your **Throttle** (and optionally **Strafe** / **Roll**) axes to your joystick.
4. Leave **Pitch** and **Yaw** unbound (or they will be ignored in HOSAM mode).
5. Scroll down to the **HOSAM Mode (Stick + Mouse)** collapsible panel and expand it.
6. Check **Enable HOSAM Mode**.
7. Optionally check **Alignment Assist** and tune:
   * **Radius**: How close to center the mouse must be before the assist activates (default: 15 of 200 units).
   * **Idle Time**: How long the mouse must be idle before decay starts (default: 80ms).
   * **Decay Speed**: How fast steering decays to center (default: 4.0; higher = faster).
8. Click **Save & Apply**.

#### Option B: Manual INI Configuration
```ini
[Aim]
bHOSAMMode = true              ; Enable stick+mouse hybrid mode
bAlignmentAssist = true        ; Enable mouse centering assist
fAlignmentRadius = 15.0        ; Accumulator radius for assist trigger (0-200)
iAlignmentIdleMs = 80          ; Idle time before decay starts (ms)
fAlignmentDecayRate = 4.0      ; Decay speed (higher = faster centering)
```

## Tuning

```ini
[Injection]
iPollRateHz = 120             ; DirectInput polling rate
iThrottleBurstMs = 250        ; Throttle authority burst duration
bLogThrottle = false          ; Enable verbose logging
```


## Logging

Logging is **off by default**. Only the startup banner (4 lines) is written to confirm the plugin loaded.

Set `bLogThrottle = true` to enable full diagnostic logging. Logs rotate at 1 MB.

## Mod Manager Compatibility

If you use a virtualizing mod manager (such as Mod Organizer 2), its Virtual File System (VFS) hook mechanism (`usvfs_x64.dll`) intercepts file paths dynamically. Because AbsoluteHOTAS hooks DirectX and uses input injection (standard techniques for Direct HID input), the combination of virtualization and hooking can occasionally trigger false positives in antivirus heuristics or Windows Smart App Control, resulting in error `000011CC` or a silent load failure.

**To resolve:**
1. Right-click `AbsoluteHOTAS.dll` inside your mod manager's physical mods directory, select **Properties**, check **Unblock** at the bottom of the General tab, and click **Apply** / **OK**.
2. If the issue persists, add exceptions/exclusions in your security software for both your **Starfield game folder** and your **Mod manager's installation/mods folder**.

## Troubleshooting

### Wizard Crash / Overlay Doesn't Open
If the overlay crashes or refuses to open on `Ctrl+Alt+B`, this is most likely caused by a leftover/orphaned SFSE plugin DLL from a previously uninstalled mod.
1. Check `Data\SFSE\Plugins\` for any `.dll` files that do not belong to your currently enabled/active mods and remove them.
2. If the issue persists: temporarily disable all mods except **AbsoluteHOTAS**, **SFSE**, and **Starfield Address Library**. Launch, open the wizard, save your bindings (which saves to the INI), and then re-enable your mods and relaunch. The plugin loads bindings automatically from the INI, so you do not need to access the wizard again in order to fly.

### Logging & Diagnostic Data
If you encounter hardware axis mapping or detection issues:
1. Set `bLogThrottle = true` in `AbsoluteHOTAS.ini` under `[Injection]`.
2. Re-test the issue in-game.
3. Check `Data\SFSE\Plugins\AbsoluteHOTAS.log` for logs and report issues along with your hardware device names.

## Notes

Axis enumeration varies across hardware — if an axis doesn't map correctly via the wizard, you may need to manually adjust the usage ID in the INI. Report hardware-specific issues with your device names and the plugin log (`bLogThrottle = true`).

# AbsoluteHOTAS v3.0.2-beta

Direct HID SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield — no vJoy or Joystick Gremlin required.

## Changelog
### v3.0.2-beta
- **On-Foot Sprint Fix:** Sprinting no longer cancels after you've flown a ship. This is fixed at the source (the plugin was continuously writing to a shared input lane while on foot), so the ON/OFF toggle is no longer needed as a sprint workaround.
- **In-Game Rebinds Respected:** Ship action buttons now follow your in-game Starfield keybinds automatically (read from your ControlMap) instead of assuming the vanilla defaults — so if you've rebound ship controls, your HOTAS button outputs match.
- **Throttle Hands Back to Keyboard When Unbound:** With no throttle axis assigned, the plugin no longer silences the game's throttle channel — keyboard accelerate/decelerate work normally again.
- **Boost Output Release Fix:** The boost (FireBoosters) output could stay held down when the throttle input changed; it is now released cleanly.
- **Overlay Conflict Diagnostic:** If another mod's render hook (frame generation, capture, or overlay) prevents the in-game overlay from appearing, the plugin now logs it so a silent no-overlay is self-explaining.

### v3.0.1-beta
- **On-Foot Input Control:** Fixed phantom movement/sprint interference after flying. Added a master ON/OFF gate (default ScrollLock) to instantly kill all injected inputs while on foot.
- **Axis Scaling / Deadzone Fix:** Deadzones and saturation are now absolute positions on the axis, exactly as drawn in the binding wizard.
- **Overlay Crash Prevention:** Hardened the ImGui overlay hook with comprehensive exception handling to gracefully survive rendering pipeline shifts (like alt-tabbing or changing resolution) without crashing the game.
- **Log Versioning:** Cleaned up the diagnostic log version header to use dynamic plugin version data.

### v3.0
- **Zero-Config Flight Control Discovery:** The plugin now finds the flight control cluster automatically at runtime using the engine's own Setting system — no `StarfieldCustom.ini` edit required. The discovery is version-independent (scans for Setting names rather than hardcoded memory offsets).
- **Address Library Removed:** The plugin no longer requires the Starfield Address Library mod. All runtime addresses are resolved through the Setting beacon or AOB pattern scans.
- **Game Deadzone Zeroing:** The engine's built-in `fRollDeadzone` (default 0.5 — half the axis range!) is automatically zeroed at plugin load. The plugin's own per-axis deadzones in the binding wizard remain in full effect, giving HOTAS users the full precision of their hardware.
- **Throttle Authority Overhaul:** Both absolute and accumulator throttle modes now use a "silence gate" architecture that blocks all game writes to the throttle memory lanes. The hardware input is the single source of truth — no flicker, no fighting with the game engine.
- **Pilot Turn Assist:** For accumulator throttle users, optionally re-enables the game's native rotation throttle assist during hard turns. The game slows your ship to optimal maneuvering speed; when you stop turning (or release the button) your throttle resumes instantly. Supports Always, Hold, and Toggle activation modes with configurable button binding.
- **Per-Offset Silence Gate:** The +0x68 (input target) and +0x6C (effective throttle) memory lanes are now independently silenceable, enabling the Pilot Turn Assist to let the game's native assist operate on +0x6C while the accumulator retains full authority over +0x68.
- **Direct-Memory Reverse Flight:** Reverse flight no longer emulates the `S` key via keyboard injection. The plugin now writes directly to the flight control cluster's memory lanes, eliminating unintended keyboard inputs in menus, dialogue, and on-foot contexts.
- **Improved Velocity Reading:** Ship velocity is now read directly from the validated flight control cluster, removing the dependency on a fragile HUD-based static address.
- **Strafe Modifier Fix:** The Space modifier (which locks roll for strafing) now respects configured deadzone values. Previously a hardcoded 5% threshold caused minor stick noise to fire the modifier and steal roll authority.
- **Deadzone UI Fix:** The per-axis deadzone graph now correctly represents the deadzone as a proportion of the full axis range. The slider range is expanded to 0–95% (was previously capped at 50%).
- **Mouse Steering Defaults:** HOSAM alignment assist defaults tuned for real-world use — radius 130/200 (was 15), idle 50ms (was 80), decay 8.0 (was 4.0). Wizard slider maximums raised accordingly.
- **Codebase Hygiene:** Consolidated duplicate code, removed redundant imports, extracted shared utilities (`StringUtils.h`, `DeviceManager.h`), and removed the legacy `version.rc` resource.
- **Signal Hunter Fallback:** The original Signal Hunter discovery method is preserved as a fallback via `bSignalHunterFallback = true` in the INI.

This plugin reads DirectInput devices natively and provides direct authority for ship pitch, yaw, roll, strafe, and throttle via memory injection. It includes an in-game binding wizard and configurable button-to-keyboard/mouse mapping for all ship actions.

## Flight Control Discovery

The plugin automatically detects the flight control cluster at runtime — **no manual setup required**.

At load time, the plugin scans the engine's Setting system by name to plant a discovery beacon and zero game deadzones. This approach is version-independent and requires no hardcoded memory offsets.

If a Starfield update breaks detection before a plugin update is available, enable Signal Hunter fallback mode:

1. In `AbsoluteHOTAS.ini`, set `bSignalHunterFallback = true` under `[Injection]`.
2. Add the following to `Documents\My Games\Starfield\StarfieldCustom.ini`:

```ini
[Spaceship]
fThrottleAtEngineStart = 0.0314
```

> **Plugin Fails to Load / DLL Blocked?** If the plugin fails to load or shows error `000011CC`, right-click `AbsoluteHOTAS.dll`, select **Properties**, check the **Unblock** box at the bottom of the General tab, and click **Apply**. For virtualizing mod managers like MO2, see the [Mod Manager Compatibility](#mod-manager-compatibility) section at the bottom.

## Key Features

* **Direct HID Input** — Reads your HOTAS/HOSAS hardware directly via DirectInput. No vJoy or Joystick Gremlin dependency.
* **Zero-Config Discovery** — Automatically detects the flight control cluster using the engine's Setting system. No INI edits required.
* **Game Deadzone Removal** — Zeros the engine's hidden `fRollDeadzone` (default 0.5!) that steals precision from flight sim hardware.
* **In-Game Binding Wizard** — Press `Ctrl+Alt+B` (or bind a controller button) to open the ImGui overlay. Bind axes and buttons by moving/pressing them on your hardware in real-time.
* **Frame Generation Support** — Fully compatible with Starfield's built-in Frame Generation (FSR3 Frame Gen). The overlay automatically handles swap chain resizing and reinitializes seamlessly when toggled in-game.
* **Multi-Device Support** — Bind axes and buttons across multiple devices using `DeviceName@UsageID` syntax (e.g., `My Throttle@0x32`).
* **Per-Axis Calibration & Tuning** — Calibrate physical axis limits in-game (compensating for low-resolution ADCs or worn pots) and tune inversion/sensitivity sliders on the fly.
* **22 Ship Action Bindings** — Map physical buttons to flight functions (boost, weapons, power management, scanner, target selection, etc.).
* **Custom Bindings** — Map any controller button to emit any keyboard/mouse output. Includes a one-click "Add Menu Cluster" preset for quick menu navigation (WASD/Tab/E/Esc).
* **Digital Axis Buttons** — Bind hat switches or buttons to emulate axis input for roll, strafe, and reverse.
* **Live Config Reload** — Bindings saved from the wizard take effect immediately without restarting the game.
* **Mouse Cursor Support** — The overlay captures input and renders a cursor; the game pauses input processing while the wizard is open.
* **Pilot Turn Assist** — Optionally re-enables the game's native rotation throttle assist for accumulator throttle users. When active during hard turns the game slows your ship to optimal maneuvering speed; when you stop turning (or release the button) your throttle resumes instantly. Supports Always, Hold, and Toggle activation modes.
* **Silent by default** — No log file is written at all unless you opt in with `bEnableLog = true` in the INI. When enabled it logs device enumeration, hook installation, and errors/crashes — no runtime spam.

## Requirements

- Starfield 1.16.242 or later
- SFSE 0.2.20 or later

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

### Finding Device Names and IDs

The wizard's **Devices** tab is the easy way to read these off. If you can't use the overlay (it won't open on your system, or you prefer hand-editing), discover them two other ways:

- **Windows joystick panel** — press `Win+R`, run `joy.cpl`, select your device, and click **Properties**. Move an axis to see which one responds (X/Y/Z/…) and press a button to see which number lights up. Hat/POV directions usually appear as **high button numbers** (often around 129–132).
- **The plugin log** — `AbsoluteHOTAS.log` (next to the plugin DLL) prints an `[DeviceManager] === Attached HID Devices ===` block at startup, listing every device's exact name and its axis/button counts.

The device-name prefix is matched as a **case-insensitive substring**, so a short distinctive chunk is enough — `VKB Gunfighter`, `CH PRO`, `T.16000M`. Avoid generic words like `Throttle` or `USB` that several devices share.

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

## Multi-Zone Throttle Calibration

AbsoluteHOTAS allows you to divide a single physical throttle axis into up to **seven distinct zones**, providing complete control over reverse thrust, precise speed plateaus, and engine boost without ever taking your hand off the throttle.

When configured via the Binding Wizard (`Ctrl+Alt+B`), the graph provides a live, multi-colored visualization of your zones:
* **Reverse Zone (Red):** The bottom of your physical axis triggers reverse thrust.
* **Dead Stop Range (Amber):** An absolute zero velocity point that is easy to find by feel.
* **50% Cruise Plateau (Orange):** Locks your throttle to exactly 50% for consistent combat maneuverability.
* **100% Plateau (Silver):** Ensures you reach top speed without accidentally triggering your boosters.
* **Boost Trigger (Purple):** The very top limit of your axis automatically engages ship boost.

Set `bInvertThrottle = true` if your throttle reports minimum as maximum thrust.

### Manual INI Configuration

While the Binding Wizard is highly recommended for visual tuning, you can also manually configure the zones in `AbsoluteHOTAS.ini`:

```ini
[Normalization]
bUnipolarMode = true          ; Required for multi-zone throttle
bUnipolarReverse = true       ; Enable the reverse zone logic
iReverseZoneCenter = 3000     ; Raw axis value of the zero-thrust point
iReverseZoneDeadzone = 3000   ; Width of the dead-stop range
iDetentCenter = 32768         ; Raw axis value of the 50% cruise point
iDetentDeadzone = 500         ; Width of the 50% cruise plateau
bBoostZone = true             ; Enable the top-of-axis boost trigger
iBoostZoneCenter = 62000      ; Raw axis value where boost activates
iBoostZoneDeadzone = 2000     ; Width of the 100% plateau before boost
```

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

### Pilot Turn Assist

With a hardware throttle (absolute mode), the game's built-in rotation throttle assist is fully silenced — your lever is the single source of truth. But with a self-centering stick (accumulator mode), you may want the game to help slow you down during hard turns for tighter maneuvering.

**Pilot Turn Assist** re-enables the game's native assist selectively: during hard turns, the game reduces your effective speed to its optimal maneuvering zone. When you stop turning (or deactivate the assist), your throttle snaps back to its pre-turn value instantly — no gradual ramp, no lost speed.

Three activation modes are available:

| Mode | Behavior |
|------|----------|
| **Always** | Assist is active whenever you're turning hard with the throttle stick at neutral. |
| **Hold** | Assist is only active while a bound button is held. |
| **Toggle** | A button press toggles assist on/off. |

Configure via the wizard under **Dual-Stick / Accumulator Mode → Pilot Turn Assist**, or via INI:

```ini
[DualStick]
bAccumulatorTurnAssist = true    ; Enable pilot turn assist
iTurnAssistMode = 0              ; 0=Always, 1=Hold, 2=Toggle
iTurnAssistButton = -1           ; Button binding for Hold/Toggle (DeviceName@N)
```

> **How it works:** The plugin silences the game's writes to the throttle input target (+0x68) so the accumulator always owns your speed. When turn assist is active, it selectively un-silences the effective throttle lane (+0x6C), letting the game's native rotation assist write there while your input target is preserved. When the assist deactivates, a burst write pushes your original value back to both offsets.

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
bAccumulatorTurnAssist = false   ; Enable pilot turn assist
iTurnAssistMode = 0              ; 0=Always, 1=Hold, 2=Toggle
iTurnAssistButton = -1           ; Button for Hold/Toggle (-1 = unbound)
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
    * **Radius**: How close to center the mouse must be before the assist activates (default: 130 of 200 units).
    * **Idle Time**: How long the mouse must be idle before decay starts (default: 50ms).
    * **Decay Speed**: How fast steering decays to center (default: 8.0; higher = faster).
8. Click **Save & Apply**.

#### Option B: Manual INI Configuration
```ini
[Aim]
bHOSAMMode = true              ; Enable stick+mouse hybrid mode
bAlignmentAssist = true        ; Enable mouse centering assist
fAlignmentRadius = 130.0        ; Accumulator radius for assist trigger (0-200)
iAlignmentIdleMs = 50          ; Idle time before decay starts (ms)
fAlignmentDecayRate = 8.0      ; Decay speed (higher = faster centering)
```

## Tuning

```ini
[Injection]
iPollRateHz = 120             ; DirectInput polling rate
iThrottleBurstMs = 250        ; Throttle authority burst duration
bEnableLog = false            ; Write a diagnostic log (off by default)
```


## Logging

Logging is **off by default** and is fully opt-in: with `bEnableLog = false` the plugin writes **nothing at all** — no file is ever created, not even on a crash.

Set `bEnableLog = true` to write `AbsoluteHOTAS.log`. It captures the things worth seeing: device enumeration, trampoline/control-cluster hook installation (with resolved addresses), and any errors or crashes. There is no per-frame or heartbeat spam. Logs rotate at 1 MB.

## Mod Manager Compatibility

If you use a virtualizing mod manager (such as Mod Organizer 2), its Virtual File System (VFS) hook mechanism (`usvfs_x64.dll`) intercepts file paths dynamically. Because AbsoluteHOTAS hooks DirectX and uses input injection (standard techniques for Direct HID input), the combination of virtualization and hooking can occasionally trigger false positives in antivirus heuristics or Windows Smart App Control, resulting in error `000011CC` or a silent load failure.

**To resolve:**
1. Right-click `AbsoluteHOTAS.dll` inside your mod manager's physical mods directory, select **Properties**, check **Unblock** at the bottom of the General tab, and click **Apply** / **OK**.
2. If the issue persists, add exceptions/exclusions in your security software for both your **Starfield game folder** and your **Mod manager's installation/mods folder**.

## Troubleshooting

### Wizard Crash / Overlay Doesn't Open
If the overlay crashes or refuses to open on `Ctrl+Alt+B`, this is most likely caused by a leftover/orphaned SFSE plugin DLL from a previously uninstalled mod.
1. Check `Data\SFSE\Plugins\` for any `.dll` files that do not belong to your currently enabled/active mods and remove them.
2. If the issue persists: temporarily disable all mods except **AbsoluteHOTAS** and **SFSE**. Launch, open the wizard, save your bindings (which saves to the INI), and then re-enable your mods and relaunch. The plugin loads bindings automatically from the INI, so you do not need to access the wizard again in order to fly.

### Logging & Diagnostic Data
If you encounter hardware axis mapping or detection issues:
1. Set `bEnableLog = true` in `AbsoluteHOTAS.ini` under `[Injection]`.
2. Re-test the issue in-game.
3. Check `Data\SFSE\Plugins\AbsoluteHOTAS.log` for logs and report issues along with your hardware device names.

## Notes

Axis enumeration varies across hardware — if an axis doesn't map correctly via the wizard, you may need to manually adjust the usage ID in the INI. Report hardware-specific issues with your device names and the plugin log (`bEnableLog = true`).

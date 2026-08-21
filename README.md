# AbsoluteHOTAS v5.0.1

Direct HID SFSE plugin for pure HOTAS/HOSAS ship flight in Starfield — no vJoy or Joystick Gremlin required.

## Release status

**5.0.1 is the current stable AbsoluteHOTAS release.** Seventeen named ship functions, the boost
zone, strafe modifier, and six-axis flight output use validated Starfield control seams. Six
profile-compatible context inputs retain vanilla E, Esc, and arrow behavior across menus, dialogue,
power, and targeting.

Version 5.0.1 is also the HOTAS module shipped in Absolute Suite `0.2.0-beta.1`. It provides the
complete native Absolute Control menu while retaining the legacy Dear ImGui workbench as a
standalone fallback. Version 4.0.2 remains available only as a rollback option for older setups.

Renderer compatibility with third-party overlays, frame-generation layers, and Proton graphics
stacks is best effort for the legacy workbench. The Absolute Control menu does not use HOTAS's
renderer hook, and an explicit workbench bypass keeps flight controls and manual configuration
available when a graphics utility conflicts with the fallback UI.

Reports are welcome, with priority given to crashes, configuration loss, stuck
inputs, and regressions in core flight controls. See
[Reporting a 5.0 issue](#reporting-a-50-issue) for the information that makes a
report actionable.

## Changelog

### v5.0.1

- **Native Ship Functions and Universal Context Inputs:** Seventeen named ship actions use exact-gated Starfield control paths. The existing Select Target, Cancel, and four power-navigation profile slots preserve one assignment across contexts with vanilla E, Esc, and arrows; Targeting Mode switches Left/Right to its exact `SelectLeft`/`SelectRight` selector events so ship power is not changed simultaneously.
- **Optional Menu Control Reuse:** The Ship Buttons workbench can independently reuse Pitch for Up/Down, Yaw for Left/Right, and the current Primary Weapon button for Select/Accept. Per-profile inversion and actuation/release thresholds preserve the workbench approach, while neutral/release arming prevents carried flight input from acting as a menu opens.
- **Native Boost and Strafe:** Boost-zone activation and analog strafe use Starfield's internal ship-control paths. Roll and strafe remain independent and can be commanded simultaneously.
- **Direct Weapons and Camera Controls:** Weapon groups call their validated per-weapon start/stop functions on the ship update thread. After a successful start, a compatible Absolute Power build is optionally notified through its size-gated API so weapon-fire automation sees the direct HOTAS path; Power remains entirely optional. POV and exterior zoom use the active camera-state handlers.
- **Camera Look:** First-person cockpit rotation consumes OpenTrack FreeTrack 2.0 output, validated with Tobii and NeuralNet webcam inputs. The workbench exposes live per-axis output graphs, enable switches, inversion, sensitivity, limits, filtering, joystick overrides, toggle, and recenter controls.
- **Automatic Pilot Context:** Fresh selected flight-handler output identifies active piloting even when Starfield keeps the old ship object cached after getting up. Flight controls park automatically on foot by default; menus/loading suspend output without being classified as FPS, and Camera Look uses its own conservative 400 ms gate.
- **Fail-Closed Runtime Gates:** Version-specific vtables, methods, objects, and function bytes are validated before use. A failed gate disables that native operation instead of falling back to synthetic input.
- **Validated 4.x Migration:** One maintained 4.0.0 setup retained its bindings, profiles, and user data immediately after the 5.0.1 deployment with no rebinding. This is a successful real-install smoke test, not a guarantee for every device or mod stack.

### v4.0.2

- **RTSS Compatibility Hotfix:** Fixed a 4.0.1 regression where RivaTuner Statistics Server could reclaim DXGI's canonical `Present` entry after startup, leaving the workbench open request permanently deferred. AbsoluteHOTAS now chains behind a detected existing render detour instead of competing for its public prologue.
- **No Shadow Vtable:** Compatibility is restored without copying, replacing, or assuming the size of a swapchain vtable. Unclaimed render methods continue to use the canonical MinHook path.
- **Independent Workbench Toggle:** The HOTAS binding and `Ctrl+Alt+B` chord are polled by the controller thread, so an open request is recorded even when a graphics layer temporarily bypasses presentation callbacks.
- **Validated RTSS Path:** RTSS 7.3.5.28314 was loaded first with its global D3D12/DXGI hook active; the workbench initialized, rendered, closed, and reopened cleanly through both the hardware binding and keyboard chord.

### v4.0.1

- **Stable Core Release:** The validated DirectInput, analog flight-injection, output-release, configuration, and workbench paths are promoted to the recommended release track. Profiles and macros remain explicitly experimental pending broader use.
- **Renderer Compatibility Rewrite:** Replaced the fragile per-instance swap-chain vtable copy with canonical DXGI hook forwarding, removed the fixed vtable-size assumption, and hardened swap-chain/queue tracking for third-party graphics layers.
- **Fail-Open Workbench:** D3D12 resources are created only when the workbench is first opened. Renderer failures are latched for the session and forward transparently instead of taking flight controls or manual configuration down with the overlay.
- **Renderer Hook Bypass:** Set `[UI] bEnableWorkbench = false` in `AbsoluteHOTAS_Custom.ini` to install no AbsoluteHOTAS D3D12/DXGI hooks. Existing bindings and all flight controls continue to work.
- **D3D12 Lifetime Safety:** Added per-back-buffer allocator fence tracking, stronger result validation, and safer window-procedure/cursor restoration.
- **Flight Controls Workbench:** Consolidated binding, inversion, response, calibration, live signal, and injection ownership into the opening **Flight Axes (Core)** page, with clearer Flight Controls, Flight Modes, and Advanced navigation.
- **Configuration and Diagnostics:** Binding references now reject partial, malformed, and overflowing numeric values. Log state is atomic and writes are serialized. Binding, profile-overlay, and ControlMap tests are registered with `xmake test`.

### v4.0.0-beta

- **New Configuration Baseline:** Your bindings, calibration, tuning, macros, and profile routing live in `AbsoluteHOTAS_Custom.ini`, separate from the shipped `AbsoluteHOTAS.ini`. Updates replace only the DLL and default INI. Version 4.0 does not import pre-4.0 settings automatically; rebuild them through the wizard, then use profiles for future portability.
- **Hot-Swappable Profiles:** Build sparse input layers that inherit from Main Controls, assign them to runtime slots, and switch control schemes without restarting. Full setups can also be exported, backed up, shared, and safely imported through the wizard.
- **Macro Builder:** Build ordered key sequences—including chords, taps, holds, and turbo repeat—and bind them to controller buttons from the wizard.
- **Rebuilt Configuration Workbench:** Common setup stays prominent while calibration, response tuning, and advanced controls remain available through progressively disclosed specialist pages.
- **Opt-In Logging:** Diagnostics are off by default (`bEnableLog`). A normal run writes no log file at all.

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
- **Strafe Modifier Fix (3.x path):** The legacy Space-key modifier respected configured deadzones. In 5.0 this path is superseded by the native modifier and simultaneous roll/strafe output split.
- **Deadzone UI Fix:** The per-axis deadzone graph now correctly represents the deadzone as a proportion of the full axis range. The slider range is expanded to 0–95% (was previously capped at 50%).
- **Mouse Steering Defaults:** HOSAM alignment assist defaults tuned for real-world use — radius 130/200 (was 15), idle 50ms (was 80), decay 8.0 (was 4.0). Wizard slider maximums raised accordingly.
- **Codebase Hygiene:** Consolidated duplicate code, removed redundant imports, extracted shared utilities (`StringUtils.h`, `DeviceManager.h`), and removed the legacy `version.rc` resource.
- **Signal Hunter Fallback:** The original Signal Hunter discovery method is preserved as a fallback via `bSignalHunterFallback = true` in the INI.

This plugin reads DirectInput devices natively and provides direct authority for ship pitch, yaw, roll, strafe, and throttle. It includes an in-game workbench, native bindings for all named ship actions, and explicit raw keyboard/mouse passthroughs for custom non-ship commands.

## Flight Control Discovery

The plugin automatically detects the flight control cluster at runtime — **no manual setup required**.

At load time, the plugin scans the engine's Setting system by name to plant a discovery beacon and zero game deadzones. This approach is version-independent and requires no hardcoded memory offsets.

If a Starfield update breaks detection before a plugin update is available, enable Signal Hunter fallback mode:

1. Set `bSignalHunterFallback = true` under `[Injection]` in
   `AbsoluteHOTAS_Custom.ini`, or in the shipped `AbsoluteHOTAS.ini` before the
   first wizard save.
2. Add the following to `Documents\My Games\Starfield\StarfieldCustom.ini`:

```ini
[Spaceship]
fThrottleAtEngineStart = 0.0314
```

> **Plugin Fails to Load / DLL Blocked?** If the plugin fails to load or shows error `000011CC`, right-click `AbsoluteHOTAS.dll`, select **Properties**, check the **Unblock** box at the bottom of the General tab, and click **Apply**. For virtualizing mod managers like MO2, see the [Mod Manager Compatibility](#mod-manager-compatibility) section at the bottom.

## Key Features

* **Direct HID Input** — Reads your HOTAS/HOSAS hardware directly via DirectInput. No vJoy or Joystick Gremlin dependency.
* **Hot-Swappable Profiles** — Switch complete input layers during play, including bindings, axes, tuning, aim modes, and macros. Profiles support toggle, momentary, and selector-style activation.
* **Macro Builder** — Build chords, taps, timed holds, ordered sequences, and turbo actions in the wizard. Named targets use native Starfield controls or the six universal context inputs as appropriate; explicit raw key/mouse targets remain available for general-purpose automation.
* **Zero-Config Discovery** — Automatically detects the flight control cluster using the engine's Setting system. No INI edits required.
* **Game Deadzone Removal** — Zeros the engine's hidden `fRollDeadzone` (default 0.5!) that steals precision from flight sim hardware.
* **In-Game Binding Wizard** — Press `Ctrl+Alt+B` to open the ImGui overlay. The opening **Flight Axes (Core)** page keeps every direct-flight binding, response control, calibration tool, and live input display together. Bind axes and buttons by moving/pressing them on your hardware in real-time.
* **Frame Generation Support** — Supports Starfield's built-in FSR3 Frame Generation and reinitializes the overlay when the swap chain changes. Compatibility with third-party overlays, capture tools, and graphics injectors is best effort.
* **Multi-Device Support** — Bind axes and buttons across multiple devices using `DeviceName@UsageID` syntax (e.g., `My Throttle@0x32`).
* **Per-Axis Calibration & Tuning** — Calibrate physical axis limits in-game (compensating for low-resolution ADCs or worn pots) and tune inversion/sensitivity sliders on the fly.
* **17 Native Ship Actions + 6 Universal Context Inputs** — Keep direct native control for ship-specific functions while existing Select, Back, and directional bindings work across menus, dialogue, power management, and targeting without profile migration.
* **Menu Control Reuse** — Optionally use Pitch/Yaw for menu navigation and Primary Weapon for Select/Accept, with independent per-profile switches, direction inversion, hysteresis, and safe neutral arming.
* **OpenTrack-Compatible Camera Look** — Apply rotational pose directly to the first-person cockpit camera. Validated with OpenTrack's Tobii and NeuralNet webcam inputs; the Camera Look workbench adds live per-axis output graphs, enable switches, inversion/sensitivity/limits, joystick-axis overrides, toggle, and recenter bindings.
* **Automatic Pilot/FPS Detection** — Uses the live selected flight-handler cadence rather than a cached ship pointer. Flight controls park automatically after leaving the seat, while menus/loading remain a separate suspended state.
* **Custom Bindings** — Map any controller button to emit any keyboard/mouse output. Includes a one-click "Add Menu Cluster" preset for quick menu navigation (WASD/Tab/E/Esc).
* **Button-based axes** — Use hat switches or buttons for roll, strafe, and reverse when analog axes are limited.
* **Live Config Reload** — Bindings saved from the wizard take effect immediately without restarting the game.
* **Safe Workbench Session** — Plugin-owned flight injection, native actions, raw custom outputs, macros, and profile switching are parked while the wizard is open. Open it from the main or pause menu for mouse interaction; keyboard navigation remains available during gameplay.
* **Pilot Turn Assist** — Optionally re-enables the game's native rotation throttle assist for accumulator throttle users. When active during hard turns the game slows your ship to optimal maneuvering speed; when you stop turning (or release the button) your throttle resumes instantly. Supports Always, Hold, and Toggle activation modes.
* **Silent by default** — No log file is written at all unless you opt in with `bEnableLog = true` in the INI. When enabled it logs device enumeration, hook installation, and errors/crashes — no runtime spam.

## Requirements

- Starfield 1.16.242 or 1.16.244
- SFSE 0.2.20 or later
- OpenTrack with FreeTrack 2.0 output only when using tracker-based camera look
- Absolute Control is recommended for the shared native menu, but is not a flight dependency
- No Starfield Address Library required

Install `AbsoluteHOTAS.dll` and `AbsoluteHOTAS.ini` to:

```text
Data\SFSE\Plugins\
```

Or install via your mod manager using the provided archive.

## Upgrading from 3.x

Version 4.0 is a fresh configuration baseline. Do **not** copy a 3.x
`AbsoluteHOTAS.ini` into this release or attempt to import it as a 4.0 profile.
Back it up for reference, install both new files, and rebuild the setup through the
wizard. After the first save, user-owned settings live in
`AbsoluteHOTAS_Custom.ini`; future releases replace only the DLL and shipped default
INI. Use full profile exports for backup, sharing, and migration going forward.

When updating from 4.0.x to 5.0.1, replace only `AbsoluteHOTAS.dll` and the shipped
`AbsoluteHOTAS.ini`. Keep `AbsoluteHOTAS_Custom.ini` and the `Profiles` directory. Back up both
user-owned locations before any update.

## Absolute Control and modular ownership

With Absolute Control installed, open the Pause Menu, select **MOD OPTIONS**, and choose
**AbsoluteHOTAS**. The native menu provides the complete bindings, throttle tuning, axes, buttons,
profiles, shift layers, macros, devices, and diagnostics workflow. HOTAS continues to own its
configuration, live input, persistence, Apply, and Cancel behavior.

The suite resolves overlapping gameplay responsibilities explicitly:

- **AbsoluteZero:** when installed, it owns mouse pitch/yaw steering. HOTAS yields its pitch/yaw
  stick bindings but retains roll, strafe, throttle, buttons, profiles, and the shared writer hook.
- **Absolute Head Tracking:** when installed, it owns cockpit camera composition. HOTAS parks its
  embedded legacy tracker while retaining flight controls and the Absolute Input Bus.
- **Absolute Power:** remains the authority for power presets and activation. HOTAS supplies the
  optional Input Bus used for controller/POV preset capture.

When Absolute Control is absent, `Ctrl+Alt+B` opens the legacy HOTAS workbench and existing INI,
profile, and hotkey workflows remain available.

## Quick Start

1. Install the plugin files.
2. Launch the game via SFSE.
3. With Absolute Control installed, open the Pause Menu and select **MOD OPTIONS → AbsoluteHOTAS**.
   Without it, open the main or pause menu and press `Ctrl+Alt+B` for the legacy workbench.
4. Bind and tune the six flight axes while watching their live input markers.
5. Configure ship buttons, throttle behavior, profiles, and any optional shift layer.
6. Apply/save the changes. Bindings take effect without restarting.

`Ctrl+Alt+F8` is reserved as a keyboard fail-safe reset for the flight control hooks.

## Legacy standalone workbench

When Absolute Control is not installed, press `Ctrl+Alt+B` to toggle the retained Dear ImGui
workbench. Use the Starfield main or pause menu for full mouse interaction. If opened during active
gameplay, Starfield keeps the mouse locked for camera control; the workbench displays a warning and
remains usable with `Tab`, arrow keys, `Enter`, and other standard keyboard-navigation keys.

The workbench uses three built-in task groups and registers an optional fourth
group when a compatible `AbsolutePower.dll` is detected:

| Group | Pages | Purpose |
|-----|---------|---------|
| **Flight Controls** | Flight Axes (Core), Ship Buttons | The core setup path: visually grouped six-degree-of-freedom axes with binding, inversion, response, calibration, and live feedback in one place; plus reverse strategies, digital fallbacks, named ship actions, and custom outputs. |
| **Flight Modes** | Aiming & Combat, Rate Throttle | Optional independent aiming, HOSAM support, self-centering throttle behavior, and pilot assists. |
| **Advanced** | Macros, Plugin Controls, Devices | Macro construction, plugin activation and wizard-access controls, device inspection, calibration, reassignment, and profile management. |
| **Power** *(when installed)* | Power Presets, Automation, Diagnostics | The Absolute Power preset/pip editor, priority rules, live allocation preview, and HOTAS preset-button capture. Power remains authoritative for configuration reload and runtime activation; AbsoluteHOTAS supplies the optional host tab and DirectInput bindings. |

### Direct flight controls

Each core axis card identifies the control mode and describes what the bound axis
does. Throttle, pitch, yaw, roll, lateral strafe, and vertical strafe drive the
ship directly. Aim-driven steering follows the weapon reticle, while HOSAM leaves
pitch and yaw with Starfield's mouse steering.

Roll and strafe are independent controls in AbsoluteHOTAS 5.0 and can be commanded
simultaneously. This applies to both analog axes and button/POV-based strafe.

Strafe and throttle boost-zone activation use Starfield's internal ship-control
paths, so no keyboard bindings are required. Turning off **Flight controls
enabled** parks flight axes, head pose, boost-zone, and strafe output together.

By default, the same controls park automatically after the selected flight
handler has remained inactive for five seconds during gameplay. The longer latch
tolerates brief targeting-mode pauses. Camera Look is independently protected by
a 400 ms freshness gate, so head pose may pause during targeting rather than ever
following the player on foot. Menus and loading screens suspend output without
being treated as an FPS transition.

The profile selector in the fixed workbench header identifies the configuration being edited. **Save & Apply** commits without closing, **Save & Close** commits and exits, and **Close Without Saving** discards the current draft after confirmation.

## Profiles and Runtime Input Layers

Profiles are alternate control setups that can be activated without restarting the
game. They are useful when a change involves more than giving one button a second
meaning: a profile can change bindings, axes, sensitivity, throttle behavior, aim
mode, custom outputs, macros, and whether flight-axis injection is active.

Examples include:

- a precision profile with softer pitch and yaw response;
- a combat profile with different trigger mappings and macros;
- a cruise profile with fixed-speed controls instead of a physical throttle;
- an on-foot profile that parks flight axes but retains useful buttons and macros;
- separate profiles for different connected hardware.

### Main Controls, inheritance, and overrides

**Main Controls** is the base setup. A normal profile is a sparse overlay: it
inherits every setting from Main Controls and stores only the values that differ.
Changing an inherited binding later in Main Controls therefore updates the profile
too. A profile becomes independent only where it has an explicit override.

Only one profile is active at a time. Runtime profiles do not stack or merge with
one another, although temporary activation returns to the selector position or base
setup that was active beforehand.

### Creating and editing a profile

1. Open **Advanced** and expand **Profile activation and management**.
2. Create an overlay, or initialize the optional **FPS** and **Flight Aux** starters.
3. Select the profile from **Editing profile** in the fixed workbench header.
4. Change any controls or tuning that should differ from Main Controls.
5. Click **Save & Apply** to keep working or **Save & Close** to finish.

The selected profile is the destination of every workbench edit. Unsaved-change
guards prevent silently switching the editing target and losing the current draft.

### Activating profiles during play

Each profile can use a keyboard shortcut and may also be assigned a controller
trigger. Choose the activation behavior that matches the physical control:

| Mode | Intended control | Behavior |
|---|---|---|
| **Momentary** | Push button or shift paddle | Active while held; releasing returns to the previously active selection. |
| **Toggle** | Push button | First press activates the profile; the next returns to the base selection. |
| **Selector** | Maintained rotary/detent position | Active while that position's button is held. It synchronizes at startup and after closing the workbench. |

A break-before-make selector keeps the last valid position while moving between
detents instead of briefly falling back to Main Controls. Up to 16 runtime slots are
available, and swaps are preloaded so activation does not read from disk during play.

### Parking flight controls versus stopping the plugin

The default **Automatic pilot context** setting under **Advanced > Plugin
Controls** parks flight axes after leaving the pilot seat. It can instead be set
to park every plugin-owned output or disabled. The flight-control latch is
adjustable; Camera Look always retains its separate 400 ms safety gate.

Turning off **Flight controls enabled** under **Flight Controls > Flight Axes
(Core)** creates a parked profile. Pitch, yaw, roll, throttle, strafe, aim, head
pose, and boost-zone output stop, while that profile's discrete button outputs
and macros remain available. This is intended for an on-foot or menu position on
a hardware selector.

The master stop control is different: it releases direct flight controls, native
actions, raw custom outputs, and macros together. Use it as the global panic or
troubleshooting control.

| State | Flight axes | Buttons and macros |
|---|---:|---:|
| Normal profile | Active | Active |
| Parked profile | Off | Active |
| Automatic on-foot parking (default) | Off | Active |
| Master stop | Off | Off |

### Export, import, and backup

**Export base setup** creates an independent full profile suitable for backup or
sharing. Importing a full profile replaces Main Controls and automatically backs up
the current base first. Sparse runtime overlays are not imported as a new base
because they depend on the Main Controls from which they inherit.

## Macro Builder

Open **Advanced → Macros** and choose **Add Macro**, or start with the
**Grav → Shields** example. A macro maps one controller button to an ordered list of
steps:

- **Tap** presses and releases an output for the configured duration.
- **Hold** keeps an output down for the configured duration.
- **Repeat** runs the step multiple times.
- **Chord** places multiple targets in one step so they are pressed together.
- **Gap** controls the delay before the next step.
- **Turbo** repeats the complete macro while its trigger remains held.

Targets may be named Starfield ship actions such as `NextSystem`, or explicit
keyboard and mouse outputs. `SelectTarget`, `Cancel`, `IncreaseSystemPower`,
`DecreaseSystemPower`, `PreviousSystem`, and `NextSystem` are compatibility aliases
for fixed vanilla E, Esc, and arrow inputs; other named targets use native paths.

Macros are fire-and-forget after their trigger press unless turbo is enabled. They
are cancelled and all held outputs are released when the plugin is stopped, the
workbench opens, configuration reloads, or the active profile changes. Macros belong
to the profile being edited; save that profile to apply them.

Begin with short, visible sequences. Starfield must observe a release between
repeated inputs, so reducing tap or gap timing too aggressively can cause the game
to miss actions even though the macro executed.

## Frame Generation Support

Starfield's built-in FSR3 Frame Generation is supported. Enabling or disabling it
in-game causes the overlay to discard its old swap-chain state and initialize again
the next time it is opened. FSR3 upscaling without frame generation is unaffected.

Third-party overlays, recording tools, frame-generation mods, and graphics injectors
may hook the same DirectX entry points. AbsoluteHOTAS detects several common hook
conflicts and records them when logging is enabled, but compatibility with every
injector combination is best effort. If the cursor appears without the workbench—or
the workbench does not appear at all—test once without other graphics injectors
before filing a report. The workbench renderer initializes only when first opened;
if it fails, it is disabled for that session while flight controls and manual
configuration continue normally.

## Manual Configuration

The wizard is the supported editor. Save once to create the user-owned
`AbsoluteHOTAS_Custom.ini`, then edit that file if manual configuration is needed.
Do not place personal bindings or tuning in the shipped `AbsoluteHOTAS.ini`; it is
the mod-owned defaults file and is replaced during updates. Axes use HID Usage ID
syntax with an optional device-name prefix:

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

The wizard's **Advanced → Devices** page is the easy way to read these off. If you can't use the overlay (it won't open on your system, or you prefer hand-editing), discover them two other ways:

- **Windows joystick panel** — press `Win+R`, run `joy.cpl`, select your device, and click **Properties**. Move an axis to see which one responds (X/Y/Z/…) and press a button to see which number lights up. Hat/POV directions usually appear as **high button numbers** (often around 129–132).
- **The plugin log** — `AbsoluteHOTAS.log` (next to the plugin DLL) prints an `[DeviceManager] === Attached HID Devices ===` block at startup, listing every device's exact name and its axis/button counts.

The device-name prefix is matched as a **case-insensitive substring**, so a short distinctive chunk is enough — `VKB Gunfighter`, `CH PRO`, `T.16000M`. Avoid generic words like `Throttle` or `USB` that several devices share.

## Ship Buttons

Button IDs are 1-indexed DirectInput values: physical buttons use 1–128 and POV/hat directions use virtual IDs 129–144. Prefix with a device name to target a specific device:

```ini
[ShipButtons]
bShipButtonsEnabled = true
iFireBoostersButton = VKB Gunfighter@44
iFireWeapon0Button = VKB Gunfighter@1
iSelectTargetButton = VKB Gunfighter@2
iIncreaseSystemPowerButton = -1
```

Set an action to `-1` to disable it. In 5.0, ship-specific named actions call
validated Starfield functions directly. The existing Select Target, Cancel, and
four power-navigation keys instead form a universal E/Esc/arrow context cluster.
Neither path uses `[ShipButtonOutputs]` overrides.

Raw keyboard/mouse passthroughs remain available under `[ButtonExpansion]` for
menu helpers or other actions that are not native ship functions:

```ini
[ButtonExpansion]
iButton99 = key:0x21
iButton100 = mouse:3
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

While the Binding Wizard is highly recommended for visual tuning, you can also manually configure the zones in `AbsoluteHOTAS_Custom.ini` after the wizard has created it:

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
2. Under **Flight Controls → Flight Axes (Core)**, find **Throttle** and click **Bind Axis**. Move your self-centering joystick axis (for example, the left stick Y-axis).
3. Enable **Self-centering / rate throttle (HOSAS)** on the Throttle card, then open **Flight Modes → Rate Throttle**.
4. Enable **Use gamepad-style throttle**.
5. Set the parameters to your preference:
   * **Ramp Rate**: How fast the throttle ramps up when the stick is fully deflected forward (in throttle units per second).
   * **Decay Rate**: How fast the throttle decays back to zero when you release the stick. Set to `0.0` to disable decay and lock your current throttle setting when the stick centers.
   * **Reverse Gate Velocity**: The HUD speed threshold below which backward stick deflection engages reverse thrusters.
6. Click **Save & Apply** or **Save & Close**.

### Pilot Turn Assist

With a hardware throttle (absolute mode), the game's built-in rotation throttle assist is fully silenced — your lever is the single source of truth. But with a self-centering stick (accumulator mode), you may want the game to help slow you down during hard turns for tighter maneuvering.

**Pilot Turn Assist** re-enables the game's native assist selectively: during hard turns, the game reduces your effective speed to its optimal maneuvering zone. When you stop turning (or deactivate the assist), your throttle snaps back to its pre-turn value instantly — no gradual ramp, no lost speed.

Three activation modes are available:

| Mode | Behavior |
|------|----------|
| **Always** | Assist is active whenever you're turning hard with the throttle stick at neutral. |
| **Hold** | Assist is only active while a bound button is held. |
| **Toggle** | A button press toggles assist on/off. |

Configure via the wizard under **Flight Modes → Rate Throttle → Pilot Turn Assist**, or via INI:

```ini
[DualStick]
bAccumulatorTurnAssist = true    ; Enable pilot turn assist
iTurnAssistMode = 0              ; 0=Always, 1=Hold, 2=Toggle
iTurnAssistButton = -1           ; Button binding for Hold/Toggle (DeviceName@N)
```

> **How it works:** The plugin silences the game's writes to the throttle input target (+0x68) so the accumulator always owns your speed. When turn assist is active, it selectively un-silences the effective throttle lane (+0x6C), letting the game's native rotation assist write there while your input target is preserved. When the assist deactivates, a burst write pushes your original value back to both offsets.

#### Option B: Manual INI Configuration
Edit the wizard-created `AbsoluteHOTAS_Custom.ini` and configure the following sections:

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

Digital aim buttons (5-way directional) can also be bound under **Tune → Aiming & Combat** to move the reticle like a virtual cursor. A toggle button can switch between independent aim and aim-driven steering at runtime.

## HOSAM Mode (Stick + Mouse)

For players using a left-hand joystick for throttle/strafe and a mouse for steering, **HOSAM (Hands On Stick And Mouse)** mode releases the pitch and yaw cluster gates so the game's native mouse steering pipeline drives ship rotation. Throttle, strafe, and roll remain under plugin control.

Mice don't self-center like joysticks, so an optional **Alignment Assist** feature observes the mouse accumulator and gently decays the ship's steering back to neutral when the mouse is idle near center. This prevents the "drift problem" where the ship keeps turning after you stop moving the mouse.

### How to Bind and Configure

#### Option A: In-Game Binding Wizard (Recommended)
1. Press `Ctrl+Alt+B` to open the wizard.
2. Under **Flight Controls → Flight Axes (Core)**, enable **Mouse steering (HOSAM)** in the Rotation group.
3. Bind your **Throttle** and optionally **Strafe** / **Roll** axes to your joystick.
4. Leave **Pitch** and **Yaw** unbound; their cards remain visible but are marked as owned by mouse steering.
5. Optionally enable **Alignment assist** on the same page and tune:
    * **Radius**: How close to center the mouse must be before the assist activates (default: 130 of 200 units).
    * **Idle Time**: How long the mouse must be idle before decay starts (default: 50ms).
    * **Decay Speed**: How fast steering decays to center (default: 8.0; higher = faster).
6. Click **Save & Apply** or **Save & Close**.

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
If the overlay crashes or refuses to open on `Ctrl+Alt+B`:

1. Check `Data\SFSE\Plugins\` for DLLs left behind by disabled or uninstalled mods.
2. Temporarily disable third-party overlays, capture hooks, frame-generation mods,
   ReShade/Special K-style injectors, and similar graphics layers.
3. Test with only **AbsoluteHOTAS** and **SFSE** enabled.
4. Enable logging and check for prior-hook or renderer initialization messages.

Bindings load without opening the workbench, so a working
`AbsoluteHOTAS_Custom.ini` can still be used while diagnosing an overlay-only issue.
For a persistent conflict, disable the other graphics layer, open the workbench and
save your bindings, then add this startup-only escape hatch to
`AbsoluteHOTAS_Custom.ini` before restoring the graphics layer:

```ini
[UI]
bEnableWorkbench = false
```

This skips all AbsoluteHOTAS D3D12/DXGI hooks; it does not disable flight controls.

### Logging & Diagnostic Data
If you encounter hardware axis mapping or detection issues:
1. Set `bEnableLog = true` under `[Injection]` in the wizard-created `AbsoluteHOTAS_Custom.ini` (or temporarily in the shipped defaults INI before first save).
2. Re-test the issue in-game.
3. Check `Data\SFSE\Plugins\AbsoluteHOTAS.log` for logs and report issues along with your hardware device names.

### Reporting a 5.0 issue

Open an issue in the
[GitHub issue tracker](https://github.com/SultanDesync/AbsoluteHOTAS/issues) and
include:

- the exact AbsoluteHOTAS version;
- Starfield and SFSE versions;
- controller, throttle, pedal, or button-box names;
- whether the issue occurs on Main Controls, a named profile, or a shift layer;
- the smallest repeatable sequence that triggers the problem;
- `AbsoluteHOTAS.log` with `bEnableLog = true`;
- the relevant `AbsoluteHOTAS_Custom.ini` and profile file, with any personal paths
  or unrelated bindings removed.

For stuck inputs or configuration loss, say so in the title and describe how you
recovered. For overlay problems, list other overlays, capture tools, graphics
injectors, and frame-generation mods. Hardware-specific and cosmetic reports remain useful, but
safety and core-flight regressions take priority.

## Notes

Axis enumeration varies across hardware — if an axis doesn't map correctly via the wizard, you may need to manually adjust the usage ID in the INI. Report hardware-specific issues with your device names and the plugin log (`bEnableLog = true`).

Development planning lives in the [full Control feature inventory](docs/ABSOLUTE-CONTROL-HOTAS-FEATURE-INVENTORY.md),
the [workbench UX overhaul](docs/UX-OVERHAUL-HANDOFF.md), and the
[Absolute Control integration handoff](docs/ABSOLUTE-CONTROL-INTEGRATION-HANDOFF.md).

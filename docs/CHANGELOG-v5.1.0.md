AbsoluteHOTAS v5.1.0
Complete changes since the Nexus v4.0.2 stable release
======================================================

This release consolidates the work developed across the 5.0 fork after the
public v4.0.2 release. The chronology below records the intermediate 5.0.0-beta
and 5.0.1 milestones as well as the final 5.1.0 architecture. Some features,
most notably embedded head tracking and the Dear ImGui workbench, were built or
retained during the 5.0 cycle and then deliberately extracted or retired before
5.1.0. Their history is included so the record remains complete.


5.0.0-BETA — NATIVE SHIP-CONTROL REWRITE
=========================================

Native ship actions
-------------------

- Replaced the 4.x named-action keyboard emulation path with validated internal
  Starfield operations for ship-specific controls.
- Added direct routes for weapon groups, boost, flight-mode switching, scanner,
  repair, POV and zoom controls, cruise, targeting, seat/cockpit actions, and
  autopilot where exact native seams were recovered.
- Routed weapon groups through the ship update thread and Starfield's validated
  per-weapon start/stop functions.
- Removed named ship actions' dependence on ControlMap_Custom.txt and the
  player's keyboard/mouse bindings.
- Added a central ship-action catalog so button bindings, macro targets, menu
  descriptions, availability, dispatch policy, and diagnostics share the same
  stable identifiers.
- Added reference-counted logical ownership for held native actions so multiple
  bindings or macros cannot generate conflicting press/release edges.
- Native actions fail closed. If an exact function-byte, vtable, live-object,
  camera-state, handler, or runtime gate fails, that action is disabled and is
  never silently rerouted through SendInput.

Native boost, strafe, roll, and flight output
---------------------------------------------

- Routed the physical throttle boost zone through the selected flight handler's
  native booster request instead of a synthetic key.
- Routed analog and digital strafe activation through Starfield's native flight-
  mode modifier.
- Added a selected-handler output split that writes lateral strafe and roll into
  their independent output lanes after Starfield's transform.
- Fixed the former shared-lane conflict so roll can be combined with lateral or
  vertical strafe for simultaneous six-degree-of-freedom input.
- Kept throttle, pitch, yaw, roll, lateral strafe, and vertical strafe under
  direct HOTAS/HOSAS authority while preserving the existing absolute and
  accumulator/rate-throttle modes.
- Preserved native turn assist, reverse strategies, boost detents, button-based
  axes, independent aim input, and existing calibration/response shaping.

Automatic pilot context
-----------------------

- Promoted the selected flight-handler output cadence into a production signal
  for whether the player is actively seated and piloting.
- Added distinct Piloting, On Foot, and Suspended menu/loading states instead of
  relying on a cached ship pointer that can survive after the player gets up.
- Added the default Injection Only gate, which parks flight-axis output after a
  configurable 5000 ms latch while leaving discovery and permitted discrete
  controls available.
- Added a Full gate option that parks all plugin-owned output outside the pilot
  seat, plus an Off/manual mode for specialist setups.
- Kept discovery running while output is parked so cockpit re-entry can reopen
  control without restarting the plugin.
- Added deterministic pilot-state policy tests and runtime status reporting.

Embedded camera-look milestone
------------------------------

- Added OpenTrack FreeTrack 2.0 rotational cockpit head tracking during the
  initial 5.0 work, including yaw, pitch, roll, recenter, toggle, filtering,
  inversion, angle limits, deadzones, and optional joystick-look overrides.
- Validated the original implementation with Tobii Tracker 2 and NeuralNet
  webcam sources through OpenTrack.
- Added a conservative 400 ms cockpit freshness gate so cached ship state could
  not continue applying camera pose after the player left the seat.
- Added Camera Look configuration and live graphs to the transitional 5.0
  workbench.
- This embedded feature was later extracted. See the 5.1.0 section below for
  the final standalone Absolute Head Tracking architecture.


5.0.1 — UNIVERSAL CONTEXT INPUT AND TARGETING PASS
=================================================

17 native ship actions plus six universal context inputs
--------------------------------------------------------

- Clarified the 23 profile-stable ship-button slots as 17 ship-specific native
  actions plus six universal context inputs.
- Preserved every existing binding name and profile assignment while expanding
  the six context-sensitive controls:
  - Select Target emits vanilla E for Select / Accept outside its native ship
    context.
  - Cancel emits vanilla Esc for Back / Cancel.
  - Increase and Decrease System Power emit vanilla Up and Down arrows.
  - Previous and Next System emit vanilla Left and Right arrows.
- The same physical controls can now navigate menus and dialogue while retaining
  their established targeting and power-management roles in flight.
- Retained Windows SendInput only for these fixed universal context keys,
  ButtonExpansion custom outputs, and explicit key:/mouse: macro targets.
- Kept ship-specific named actions on their validated native routes with no
  synthetic fallback.

Targeting Mode routing
----------------------

- Added exact Targeting Mode detection through the validated targeting-camera
  state rather than a broad menu or piloting-state guess.
- Redirected Navigation Left and Right to Starfield's native SelectLeft and
  SelectRight component-selector events while Targeting Mode is active.
- Suppressed the arrow/power lane during Targeting Mode so component selection
  cannot redistribute ship power simultaneously.
- Added release arming when Targeting Mode closes: a held direction must return
  to neutral before it can resume its ordinary power/menu route.
- Kept Targeting Mode classified as active piloting even while Starfield's usual
  selected-handler callback is intentionally suspended.

Optional flight-control reuse in menus
--------------------------------------

- Added independent per-profile options to use Pitch for Up/Down, Yaw for
  Left/Right, and Primary Weapon for Select/Accept.
- Added independent horizontal and vertical inversion.
- Added adjustable actuation and release thresholds with hysteresis.
- Added neutral/release arming on context entry so a deflected stick or held
  trigger cannot move or accept a prompt as it opens.
- Defaulted every reuse option off so existing profiles opt in deliberately.

5.0 configuration compatibility
-------------------------------

- Kept the configuration/profile schema at version 1; existing 4.0 custom INI
  and profile files require no bulk conversion or rebinding.
- Kept legacy ShipButtonOutputs readable for migration while preventing it from
  overriding the new native/universal named-action policy.
- Preserved the two-file update contract: releases replace only the DLL and the
  mod-owned default AbsoluteHOTAS.ini.
- Confirmed one maintained 4.0.0 installation retained its bindings, profiles,
  calibration, and other user data after deployment with no rebinding. This was
  a real smoke test with a sample size of one, not a universal guarantee.


5.0 FORK — COMPLETE ABSOLUTE CONTROL PROVIDER
=============================================

Host discovery and failure isolation
------------------------------------

- Added a versioned, size-gated Absolute Control provider ABI and HOTAS module
  registration.
- Added SFSE post-data-load and post-post-data-load discovery/retry so load order
  does not decide whether the module can register.
- Kept the menu host out of flight-runtime ownership. If Absolute Control is
  absent, outdated, or rejects registration, saved flight controls continue and
  the failure is reported without installing an alternate renderer.
- Added module identity, runtime-family, provider ABI, capability, registration,
  and compatibility diagnostics.

Complete native menu coverage
-----------------------------

- Implemented the full AbsoluteHOTAS editing surface in Absolute Control rather
  than exposing only a small subset of scalar settings.
- Added Setup/Administration status and recovery controls.
- Added Flight Axes controls for the six analog flight lanes, inversion,
  sensitivity, deadzones, saturation, calibration preview, control mode, digital
  fallbacks, and live input/output markers.
- Added Throttle Setup workflows for positional and self-centering/rate modes,
  idle/reverse/boost zones, detents, cruise behavior, and direct set-by-feel
  tuning actions.
- Added Ship Buttons coverage for the 17 native ship actions, six universal
  context inputs, flight assists, keyboard/mouse shortcuts, binding method and
  availability status, and optional menu-control reuse.
- Added Aiming & Combat coverage for source-object aim, independent analog aim,
  mirror behavior, digital aim overrides, response, inversion, and smoothing.
- Added Profiles & Layers workflows for Main Controls, sparse inherited layers,
  activation slots, momentary/toggle/selector behavior, editing context, create,
  duplicate, rename, export, import, and delete operations where supported.
- Added the Macro editor for ordered key chords, taps, holds, delays, releases,
  turbo behavior, named native targets, universal inputs, and explicit raw
  keyboard/mouse targets.
- Added Devices & Calibration records for DirectInput enumeration, stable device
  identity, capabilities, axis ranges, calibration, reassignment, and live
  physical input.
- Added Plugin & Compatibility controls for the master gate, automatic pilot
  context, logging, menu binding, fail-safe information, runtime status, native
  seam availability, and companion-module ownership.

Provider-owned transactions and persistence
-------------------------------------------

- Kept HOTAS authoritative for draft creation, validation, Apply, Cancel,
  persistence, profile semantics, and live reload while Absolute Control owns
  presentation, focus, navigation, and transaction dialogs.
- Added atomic persistence to AbsoluteHOTAS_Custom.ini and profile files.
- Added a pinned editing-profile/layer context so changes always have an explicit
  destination across menu pages.
- Added dirty-switch and dirty-close handling through Apply / Discard / Stay
  transactions.
- Added field validation, clamping, cross-field checks, binding conflict checks,
  and provider-owned error messages before persistence.
- Added configuration ownership cleanup so HOTAS replaces only settings it owns,
  preserves unknown/future keys, and preserves sections owned by standalone
  modules.
- Ensured migration cleanup does not erase currently managed settings that remain
  valid in the native menu.

Live telemetry, capture, and editor safety
------------------------------------------

- Added renderer-neutral telemetry mailboxes and Absolute Control components for
  physical axis position, calibrated input, shaped output, throttle zones,
  binding state, device status, pilot context, and native-route availability.
- Added provider-owned DirectInput axis/button capture so the native host never
  polls or owns HOTAS hardware directly.
- Added stable binding catalogs and capture-kind metadata shared by menu rows,
  device capture, profile storage, and tests.
- Added safe editing-session behavior: opening Absolute Control parks flight
  injection, native actions, universal/raw outputs, macros, and profile switching.
- Added release and edge reseeding on close so controls held while editing cannot
  fire immediately when gameplay resumes.
- Added direct deep-link support so Ctrl+Alt+B and the legacy
  iToggleWizardButton profile binding open the AbsoluteHOTAS Administration page.
  The legacy setting name remains for profile compatibility.

Profiles, macros, and reusable policy hardening
-----------------------------------------------

- Hardened sparse profile overlay generation and inheritance so only intentional
  differences are stored.
- Hardened profile activation, break-before-make selector behavior, runtime slot
  limits, and preloaded no-disk-read switching.
- Hardened macro lifetime and ownership so active sequences release safely across
  menu entry, profile changes, gate changes, shutdown, and errors.
- Centralized axis shaping, aim-mode, boost-zone, capture, context, action, and
  binding policies in testable renderer-neutral components shared by the runtime
  and native menu.

Absolute Input Bus v1 and suite interoperability
------------------------------------------------

- Added a versioned, size-gated Absolute Input Bus exported by AbsoluteHOTAS.
- Published DirectInput device identity, axes, buttons, POV state, active profile,
  pilot context, and provider-owned capture without transferring hardware
  ownership to consumers.
- Added context-safe capture arbitration and bounded snapshots suitable for
  first-party companion modules.
- Added the AbsoluteHOTAS command API and suite command bindings for stable
  cross-module discovery and invocation.
- Added optional, size-gated Absolute Power discovery and direct weapon-fire
  notification only after a successful native weapon start.
- Preserved Absolute Power as the authority for power presets and activation;
  HOTAS supplies the optional Input Bus used for controller/POV preset capture.
- Added explicit mouse-steering arbitration with Absolute Zero. When Zero is
  detected, HOTAS releases its source-object alignment and native pitch/yaw
  writer gates instead of allowing two owners to fight the same lanes.
- Added camera-ownership arbitration with Absolute Head Tracking and a
  load-order-independent compatibility query.


5.1.0 — NATIVE-MENU CUTOVER AND MODULAR CLEANUP
==============================================

Direct Select Target binding fix
--------------------------------

- Restored the validated native Select Target function for the native ship
  binding and made it the catalog default.
- Added a two-route choice to the Absolute Control row:
  - Direct function invokes Starfield's exact-gated native Select Target path.
  - Ship-context SendInput E remains available as a compatibility route.
- Added a separate optional Menu Accept binding, so selecting a flight target no
  longer implicitly grants the same physical button authority over menus.
- Persisted route selection through the ordinary profile-aware
  `[ShipControlMethods] SelectTarget` setting.
- Added catalog, composition, choice, transaction, persistence, and native-route
  regression coverage for the restored method.

Bindings-tab review and capture feedback
----------------------------------------

- Made Flight Axes the first AbsoluteHOTAS tab and transferred ownership of all
  seven analog bindings to it, including dedicated reverse authority.
- Placed each analog binding directly beside its tuning and live-response card.
- Removed the duplicate FLIGHT AXES section from Ship Buttons.
- Renamed the former Bindings tab to Ship Buttons and made NATIVE SHIP CONTROLS
  its first section.
- Audited the menu against all 23 native ship-button actions in the 5.0 catalog
  and added a compile-time exact-once coverage check. They now appear as one
  complete list in Starfield's native menu order, including Select Target, Ship
  Action 1, Ship Cancel, and all four power-management controls.
- Added ABSOLUTEHOTAS HOTKEYS for Hold Current Throttle, Full Stop, 50%, Max,
  and Turn Assist.
- Added OPTIONAL MENU NAVIGATION with six independent, profile-aware bindings:
  Menu Accept (`E`), Cancel (`Esc`), and Up/Down/Left/Right (arrow keys). All are
  unbound by default and active only in known menu context.
- Kept the existing Pitch/Yaw/Primary-Weapon navigation reuse as an advanced
  menu-only alternative with neutral arming and hysteresis.
- Added CUSTOM SENDINPUT BINDINGS as the final section for arbitrary keyboard
  and mouse passthroughs; removed the redundant menu-preset shortcut from this
  presentation now that the complete menu set has first-class rows.
- Standardized every native ship action as the same binding-plus-method row.
  Power Up/Down, Previous/Next System, and Ship Cancel now retain the visible
  method selector in a locked Fixed ship-context SendInput state, with Direct
  injection explicitly marked unavailable until a validated native route exists.
- Gated every native ship binding—including compatibility SendInput routes—to
  ship or targeting context. Native ship controls can no longer double as menu
  navigation, named macro targets follow the same boundary, and held buttons are
  release-armed across context transitions.
- Added host-owned binding-record feedback to Absolute Control: an unmistakable
  centered capture panel names the active control, describes axis/button/POV or
  keyboard capture, reports that navigation is paused, and shows the cancel input.
- Added page-order, single-ownership, composition-order, label, and capture-
  presentation regression coverage.

Legacy overlay retirement
-------------------------

- Made Absolute Control the sole supported in-game configuration frontend.
- Routed MOD OPTIONS -> AbsoluteHOTAS, Ctrl+Alt+B, and the existing HOTAS menu
  binding to the same native module.
- Removed the Dear ImGui workbench and UIHook presentation units from the
  shipping target.
- Removed ImGui and MinHook packages plus D3D12 and DXGI system-link
  dependencies from the release build.
- Removed all AbsoluteHOTAS swap-chain Present/Present1, ResizeBuffers, command-
  queue, WndProc, cursor, descriptor-heap, and frame-fence menu-rendering paths
  from the release DLL.
- Removed the obsolete [UI] bEnableWorkbench setting from shipped defaults and
  migration ownership. Existing copies are ignored.
- Kept historical overlay sources and 4.x changelog entries for provenance, but
  none enter the 5.1 shipping binary.
- If Absolute Control is absent or incompatible, saved flight controls and
  manual AbsoluteHOTAS_Custom.ini configuration continue to load; there is
  intentionally no graphics-hook fallback.

Head tracking extraction
------------------------

- Removed the embedded OpenTrack runtime from the shipping target.
- Removed HOTAS's FirstPersonState camera hook and head-pose composition path.
- Removed the [HeadTracking] section from the shipped default INI and supported
  HOTAS menu surface.
- Preserved legacy HeadTracking keys as externally owned migration data so an
  update cannot destroy tuning useful to the standalone module.
- Retained a size-gated camera-ownership compatibility query that reports the
  HOTAS camera hook released, allowing load-order-independent coexistence.
- Moved supported OpenTrack-compatible cockpit camera look to the separate
  Absolute Head Tracking module:
  https://www.nexusmods.com/starfield/mods/17872
- OpenTrack is no longer a requirement for the base AbsoluteHOTAS package.

Current companion-module status
-------------------------------

- Absolute Control — required for the in-game configuration menu:
  https://www.nexusmods.com/starfield/mods/18023
- Absolute Power — compatible; provides power presets/activation and can consume
  the optional HOTAS Input Bus:
  https://www.nexusmods.com/starfield/mods/18024
- Absolute Head Tracking — compatible standalone OpenTrack camera module:
  https://www.nexusmods.com/starfield/mods/17872
- Absolute Zero — limited compatibility. While active it owns native mouse
  pitch/yaw, so HOTAS joystick pitch/yaw are unavailable. Roll, strafe, throttle,
  buttons, profiles, and the shared writer remain available:
  https://www.nexusmods.com/starfield/mods/17460

Versioning, documentation, and packaging
---------------------------------------

- Bumped the plugin version, SFSE metadata, runtime identity, default INI,
  project index, source documentation, Nexus copy, and package material to
  5.1.0.
- Replaced obsolete workbench/OpenTrack instructions with Absolute Control and
  standalone-module guidance.
- Retained the two-file archive contract. The release ZIP contains exactly:
  - SFSE/Plugins/AbsoluteHOTAS.dll
  - SFSE/Plugins/AbsoluteHOTAS.ini
- The archive does not contain or overwrite AbsoluteHOTAS_Custom.ini, Profiles,
  imports, exports, or logs.


INSTALLATION AND MIGRATION FROM 4.0.2
=====================================

- Back up AbsoluteHOTAS_Custom.ini and the Profiles directory before updating.
- Install Absolute Control and its current requirements.
- Replace only AbsoluteHOTAS.dll and the shipped default AbsoluteHOTAS.ini.
- Keep the user-owned custom INI and Profiles directory.
- The profile/configuration schema remains version 1; no mandatory profile
  conversion or rebinding step is introduced by 5.1.0.
- The old Dear ImGui menu is not available in 5.1.0. Use Pause Menu ->
  MOD OPTIONS -> AbsoluteHOTAS, Ctrl+Alt+B, or the existing HOTAS menu binding.
- Install Absolute Head Tracking separately if OpenTrack camera look is desired.


VALIDATION COMPLETED FOR THIS RELEASE
=====================================

- Debug AbsoluteHOTAS build: passed.
- Release-with-debug-information AbsoluteHOTAS build: passed.
- All 22 registered debug test suites: passed.
- Absolute Control SWF rebuild and 20-source interface architecture check:
  passed.
- All 12 registered Absolute Control native test suites: passed.
- Absolute Control descriptor, transaction, capture, device, telemetry, flight-
  axis, throttle-action, ship-button, profile, macro, and absence/failure tests:
  passed.
- Config ownership, sparse profile, binding catalog, Input Bus, companion ABI,
  mouse-steering arbitration, pilot-state, targeting/context, ship-action, and
  control-mode policy tests: passed.
- git diff whitespace/final-newline validation: passed.
- Final archive layout and staged DLL/INI identity: passed.
- PE dependency audit: DINPUT8, USER32, SHELL32, ole32, and Microsoft runtime
  libraries only; no D3D12, DXGI, ImGui, or MinHook dependency.


CURRENT KNOWN BOUNDARIES
========================

- Native hooks and internal actions are exact-gated for Starfield 1.16.242 and
  1.16.244. An unsupported update fails closed until the affected seam is
  revalidated.
- SFSE 0.2.20 or later is required; follow Absolute Control's Nexus page for the
  menu host's current dependencies.
- Undock / Take-Off and Exit Ship From Cockpit use native contextual routes but
  still benefit from broader ship, camera, and seated-state coverage.
- Other DirectInput hardware, duplicate-device layouts, long profile/macro
  sessions, and unusual mod stacks may expose behavior not present in the
  maintainer setup.
- Absolute Zero and joystick pitch/yaw are mutually exclusive while Zero owns
  native mouse pitch/yaw. This limitation does not affect roll, strafe, throttle,
  buttons, profiles, or the shared writer.
- Absolute Control is required for in-game editing. Its absence does not disable
  already configured flight control, but there is no legacy overlay fallback.
- Head tracking is no longer provided by AbsoluteHOTAS; install Absolute Head
  Tracking separately.

# AbsoluteHOTAS Workbench UX Overhaul Handoff

**Status:** Accepted design baseline; implementation sequencing continues in the Absolute Control integration handoff

**Scope:** AbsoluteHOTAS's embedded in-game configuration workbench

**Build phase:** Q1-Q4 are resolved. Begin with the implementation slices in
`ABSOLUTE-CONTROL-INTEGRATION-HANDOFF.md`; provider-owned DirectInput capture and the Control
deep-link/open command remain explicit integration blockers, not reasons to defer the rest of the
workbench.

**Related references:**

- [Absolute Control integration handoff](ABSOLUTE-CONTROL-INTEGRATION-HANDOFF.md)
- [Binding Wizard Workbench Architecture](reference/wizard-workbench-architecture.md)
- [Profiles and Runtime Input Layers](reference/profile-switching.md)
- [Ship Button Bindings](reference/ship-button-bindings.md)
- [Configuration Layout](reference/config-layout.md)
- [Native Controls and Head Tracking](reference/native-controls-and-head-tracking.md)

## 1. Purpose

AbsoluteHOTAS has a strong runtime and a large set of individually capable tools, but
the workbench no longer communicates the product as clearly as the runtime deserves.
Important capabilities are difficult to discover, several pages expose implementation
mechanisms before user goals, and some interface language no longer describes the 5.x
control paths accurately.

This handoff converts the product discussion and observed support cases into an
implementable UX and runtime plan. It is intentionally more than a visual restyle. The
overhaul must align:

- the product's information architecture;
- the user's mental model;
- the runtime's native, context, and keyboard-output routes;
- profiles and sparse inheritance;
- configuration ownership and migration;
- diagnostics, documentation, and support terminology.

The goal is not to make a universal control toolkit intrinsically simple. The goal is
to make the first successful journey simple, reveal deeper capability when it becomes
relevant, and preserve the full toolkit for users who need it.

## 2. Product definition

> **AbsoluteHOTAS gets almost any flight-control setup flying quickly, then grows into
> a complete controls workbench as the pilot needs more.**

The product has three depths of engagement:

1. **Fly** — detect hardware, bind essential axes and buttons, verify input, save, and
   return to the game.
2. **Tune** — change response, throttle behavior, calibration, aiming, camera look, and
   common flight modes.
3. **Build** — create binding layers, profiles, macros, custom outputs, multi-device
   configurations, and specialist control schemes.

These are depths within one workbench, not separate Beginner and Expert modes. The user
must never choose an identity before they understand the product. The same underlying
configuration model serves every depth.

The hierarchy of promises is:

1. Get me flying.
2. Help me make it feel right.
3. Let me build almost anything.

Promise three must not obstruct promise one. Promise three must still be discoverable
when a user goes looking for it.

## 3. Evidence from current support cases

The redesign is grounded in actual user failures rather than hypothetical preference.

### 3.1 Profiles are implemented but effectively concealed

The current workbench shows the edit-profile selector globally, but creation,
activation, import, export, and reset live under:

```text
Advanced -> any Advanced page -> Profile activation and management -> Manage profiles
```

The first disclosure is collapsed and the second is nested inside it. Users request
modifier/chord functionality that the sparse profile engine already approximates
because the capability and its purpose are not visible.

### 3.2 Throttle configuration exposes parameters before behavior

The throttle engine can support positional HOTAS input, self-centering HOSAS rate
input, dead-stop and reverse regions, a cruise detent, a full-throttle plateau, and a
boost zone. Users arrive with goals such as:

- center should be idle;
- forward travel should increase ahead thrust;
- backward travel should engage reverse;
- returning a spring-centered stick should hold or decay throttle;
- the top detent should provide 100% without accidentally boosting.

The interface currently asks them to infer those outcomes from controls such as Idle
Zone, Center, Center Deadzone, Zero-Thrust, Dead Stop Range, 100% Plateau, and rate
throttle settings. Support replies are acting as a human compiler from desired behavior
to parameters.

### 3.3 The axis page has useful telemetry but weak hierarchy

The current large live graphs are valuable and must be preserved. However, axis names
are visually understated compared with status text and controls. The beginning of one
axis card and the end of another are not immediately legible.

### 3.4 Inverted throttle zone capture has inconsistent coordinates

The throttle graph mirrors the live marker after inversion, but Set Center, Set
Zero-Thrust, and Set Boost currently capture the untouched hardware sample. Runtime
zone comparisons occur after inversion. The graph, capture, stored landmark, and
runtime interpretation therefore disagree.

The graph must be the truth: capturing a visible position must store that visible,
logical position.

### 3.5 The Ship Buttons page mixes distinct control contracts

The current page places direct ship functions, universal context inputs, unresolved
cockpit interactions, menu reuse, and explicit raw keyboard/mouse outputs near one
another while describing most rows as native. Users cannot tell which functions:

- call a validated Starfield function directly;
- use fixed navigation keys intentionally across contexts;
- rely on the user's Starfield keyboard binding;
- are arbitrary keyboard/mouse shortcuts;
- are unavailable because a direct patch gate failed.

`UndockTakeOff` and `ExitShipFromCockpit` have ControlMap-resolved keyboard outputs
prepared by the existing loader, but recognized action IDs always select the native
dispatcher. Their prepared compatibility outputs are unreachable. Regrouping the UI
without changing dispatch would preserve the dead controls.

## 4. Governing design principles

### 4.1 Behavior before mechanism

Users choose an outcome first. Parameter editors and implementation details appear
after the user chooses to customize or diagnose it.

Examples:

- “Self-centering throttle: release to hold” before accumulator rate and decay.
- “Pinky Shift — while held” before sparse overlay and momentary activation.
- “Keyboard compatibility” before `SendInput` or scan codes.

### 4.2 Progressive disclosure must not become feature concealment

A capability may be summarized, but its existence and entry point must remain visible.
Collapsed panels are suitable for detail, not for the only entrance to a major feature.

### 4.3 Navigation follows user intentions

Page and section names describe what the user is trying to accomplish, not which C++
subsystem owns the setting.

### 4.4 Advanced means unusual, risky, or composition-heavy

Powerful does not automatically mean advanced. Adding a held alternate binding is a
common binding task. Full profile inheritance, import/export, parked configurations,
and selector arbitration are advanced.

### 4.5 The visible graph is authoritative

Live markers, captured points, zone boundaries, labels, and runtime output must share
one coordinate system and one interpretation.

### 4.6 One underlying model

Binding Layers use the existing sparse-profile engine. Throttle behaviors configure
the existing throttle model. Compatibility output uses the existing ref-counted raw
output and ControlMap reconciliation services. The overhaul must not create parallel
“simple” configuration files or runtimes.

### 4.7 Safe failure remains explicit

Direct control validation continues to fail closed. A failed native gate must never
silently synthesize a keyboard input. Keyboard compatibility is an explicit user
choice or the declared recommended method for an action.

### 4.8 Terminology is a product contract

The UI, documentation, logs, diagnostics, website, tutorial scripts, and support
replies must use the same names. Once this UX ships as stable, primary page and feature
names remain frozen for the supported release line unless a safety issue requires a
change.

## 5. Scope and non-goals

### 5.1 In scope

- Workbench shell and navigation refinement.
- A flight-readiness entry experience.
- Flight Axes visual hierarchy, icons, and throttle-coordinate correction.
- Behavior-first throttle setup and executable recipes.
- Ship Buttons information architecture and runtime-route clarity.
- Per-action keyboard compatibility for eligible named ship functions.
- Continued ControlMap alignment for keyboard compatibility.
- A simplified Binding Layers workflow on the Ship Buttons page.
- A dedicated Advanced Profiles & Layers workspace.
- Inheritance/override visibility and revert-to-primary behavior.
- Diagnostics required to support all of the above.
- Migration and regression coverage proportional to the configuration risk.

### 5.2 Not in scope for the first overhaul release

- Porting the complete AbsoluteHOTAS workbench into the separate
  `AbsoluteWorkbench.dll` host.
- Replacing the existing profile or INI formats wholesale.
- Arbitrary simultaneous composition of multiple modifier layers.
- Automatic keyboard fallback after a failed direct-function gate.
- Claiming proportional analog reverse before the runtime actually supports it.
- Recording UI-specific tutorial videos before labels and navigation are stable.
- Building the public tutorial website during the core workbench implementation.
- Cosmetic modernization unrelated to hierarchy, comprehension, or feedback.

## 6. Target information architecture

The existing top-level task groups remain recognizable, but their contents become
goal-oriented.

```text
Flight Controls
  Setup Overview
  Flight Axes
  Ship Buttons

Flight Modes
  Throttle Setup
  Aiming & Combat
  Camera Look

Advanced
  Profiles & Layers
  Macros
  Devices
  Plugin & Compatibility

Power (only when Absolute Power is available)
  Power Presets
  Automation
  Diagnostics
```

### 6.1 Opening behavior

- A fresh or incomplete installation opens **Setup Overview**.
- A configured installation may reopen the last workbench route for the current game
  session. Persisting the last route across game sessions is optional and must not be
  required for the first implementation.
- A module or legacy shortcut that requests a specific page still routes directly to
  that page.
- The footer remains fixed and retains guarded Save & Apply, Save & Close, and Close
  Without Saving behavior.

### 6.2 Setup Overview

The Overview is a readiness map, not another settings page. It answers:

- Are input devices detected?
- Are the primary flight axes bound and responding?
- Is a throttle behavior selected and producing a sensible output?
- Are essential ship actions bound?
- Which binding layer/profile is being edited?
- Are there direct controls unavailable for this Starfield build?

Each item links to the exact workspace that resolves it. Do not add invented scores,
gamification, or exhaustive diagnostics. A user with a working configuration should be
able to select **Continue configuring** or **Save & Fly** without reviewing every
optional feature.

### 6.3 Persistent editing context

The shell continues to show the editing target, but the label must distinguish editing
from runtime activation:

> **Editing:** Main controls

On the Ship Buttons page this becomes the more specific Binding Layer selector defined
below. Advanced profile details do not remain expanded globally.

## 7. Binding Layers: the approachable face of profiles

### 7.1 User model

The casual concept is **Binding Layers**, not Profiles. “Modifier” alone is too narrow
because the existing engine also supports toggle and maintained selector activation.

At the top of Ship Buttons, pin:

```text
Editing bindings for: [ Primary bindings                     v ]  [ + Add binding layer ]
```

Example dropdown entries:

```text
Primary bindings
Pinky Shift — while held
  VKB Gunfighter · Button 2
Landing Controls — toggle
  T.Flight HOTAS · Button 13
Mode Dial: Combat — selector
  S-TECS · Position 3
```

Human names lead. Device names and button numbers are secondary. Do not display
unstable `Device 1` or `Device 2` labels when a hardware name is available.

### 7.2 Add Binding Layer workflow

The guided creation flow is:

1. Name the layer.
2. Capture its activation control.
3. Choose one of:
   - **While held** — existing `momentary` activation;
   - **Toggle on/off** — existing `toggle` activation;
   - **Selector position** — existing `selector` activation.
4. Open the new layer for binding edits.

The explanatory contract is:

> This layer starts with your Primary bindings. Change only the controls that should
> behave differently.

The workflow creates an ordinary sparse overlay profile underneath. No separate layer
runtime or storage format is introduced.

### 7.3 Scope of the simplified layer editor

The pinned selector appears on **Ship Buttons** and edits:

- named ship-action button assignments;
- menu/context button assignments;
- keyboard/mouse shortcuts on that layer.

It does not follow the user silently onto Flight Axes, Throttle Setup, or Camera Look.
Those pages retain the explicit Main controls/profile editing context. A Binding Layer
can still modify axes, tuning, macros, or injection when opened from Advanced Profiles
& Layers, but the simplified workflow does not invite those changes.

### 7.4 Inherited and overridden values

When a sparse layer is selected, every binding row must indicate ownership:

- **Inherited from Primary** — the profile file does not own this value.
- **Overridden in Pinky Shift** — the profile file owns this value.

An overridden row provides **Use Primary Binding**, which removes the key from the
sparse profile rather than writing a copied primary value.

The implementation therefore needs source/ownership metadata in addition to the
effective value. Comparing effective values alone is insufficient while editing: a
profile may explicitly own the same value as Primary and should be normalized back to
inheritance on Save.

### 7.5 Composition contract

The first Binding Layers UX exposes the current profile arbitration model. It does not
promise arbitrary stacking of multiple simultaneously held modifiers.

- A held layer may temporarily override a toggled or selector-owned layer and return to
  it on release, according to the current engine rules.
- The UI must not depict layers as a freely composable stack.
- More complex composition is future runtime work and requires its own design.

### 7.6 Advanced Profiles & Layers

Profiles receive a dedicated Advanced page. Remove their only management entrance from
the collapsed panel above Macros.

The advanced workspace owns:

- creating and renaming sparse overlays;
- configuring momentary, toggle, and selector activation;
- editing a profile across axes, tuning, bindings, macros, and injection state;
- parked/on-foot profiles;
- full setup export and backup;
- importing a full profile as Main controls;
- reset and recovery operations;
- profile kind, file, override count, and diagnostic details;
- future detach/materialize behavior, if implemented later.

Use **Profiles & Layers** as the page label so users can connect the approachable and
advanced presentations of the same engine.

### 7.7 Profile metadata

Add optional presentation metadata to the `[Profile]` header rather than inferring
purpose from file contents. Proposed field:

```ini
[Profile]
sKind=overlay
sPurpose=binding-layer
```

Existing profiles without `sPurpose` remain valid general profiles. The metadata is
advisory for presentation and must not change sparse-overlay runtime semantics.

## 8. Flight Axes redesign

### 8.1 Preserve the large live graphs

The current graph size and prominence are successful. Do not reduce the graph to make
room for additional controls. The graph remains the primary verification surface for
each axis.

### 8.2 Strong axis-card identity

Each core axis begins with a high-contrast header:

```text
[icon] THROTTLE
       Positional thrust and cruise authority                 BOUND
```

The required hierarchy is:

1. Axis icon and large/high-contrast axis name.
2. Short behavior description.
3. Bound/unbound/inactive state.
4. Device and input binding.
5. Large live graph.
6. Common tuning.
7. Axis-specific detail.

Axis cards use stronger separation through spacing, border/rail treatment, and header
contrast. Color may reinforce categories but cannot be the only distinction.

### 8.3 Axis icon system

Use one simple monochrome line-icon family that remains legible near 20–24 px:

- **Throttle:** a lever moving along a gated track.
- **Pitch:** a vertical curved rotation arrow around a small craft/axis marker.
- **Yaw:** a horizontal curved rotation arrow around a vertical axis marker.
- **Roll:** a circular arrow around the forward flight axis.
- **Lateral Strafe:** a craft/diamond with straight left/right translation arrows.
- **Vertical Strafe:** a craft/diamond with straight up/down translation arrows.

Curved arrows consistently mean rotation. Straight arrows consistently mean
translation. The text label remains authoritative; icons are recognition cues, not
standalone controls. Implement them as small workbench-owned draw primitives or
embedded vector paths so the plugin does not depend on an external icon font.

### 8.4 Throttle inversion coordinate invariant

Define two coordinate spaces explicitly:

- **Hardware space:** untouched DirectInput/calibrated device sample.
- **Logical throttle space:** calibrated sample after inversion; increasing logical
  position always moves toward the graph's forward/right end.

The following all use logical throttle space:

- live marker;
- center/detent landmark;
- reverse zero-thrust landmark;
- boost landmark;
- displayed zones;
- runtime zone comparisons;
- stored zone-center values.

Hardware min/max calibration remains in hardware space.

When inverted, Set Center, Set Zero-Thrust, and Set Boost mirror the hardware sample
through the calibrated range before storing it. Capturing a point must place the saved
marker exactly under the visible live marker.

Do not automatically migrate existing stored landmarks. Some users have already
compensated manually and therefore possess correct logical values. The fix changes new
capture behavior and may offer a one-time explanatory status when an inverted throttle
first enters calibration.

### 8.5 Axis acceptance cases

- Every axis boundary is recognizable without reading the preceding card.
- Axis icons remain distinguishable at the actual ImGui header size.
- Graphs retain at least their current effective width and height at the default window
  size.
- Keyboard navigation reaches every binding and tuning control in a predictable order.
- With inversion off, every throttle capture lands under the live marker.
- With inversion on, every throttle capture lands under the live marker.
- Calibrated non-`0..65535` ranges obey the same invariant.
- Toggling inversion mirrors the live marker but leaves logical zone landmarks attached
  to their intended output positions.

### 8.6 Page continuation at the default window size

The Flight Axes page must not rely on a taller default window to reveal that Throttle is
only the first axis. Window height, display resolution, and saved ImGui sizing make that
an unreliable discovery mechanism.

- Keep a compact **Axes on this page** navigator above the cards with all six axis icons,
  labels, and bound/unbound state. Selecting an item scrolls directly to that card.
- Remove release-note-style explanatory panels from the top of the tab. Keep only a
  compact flight-controls status and enable switch.
- Keep the full throttle range/detent/reverse/boost editor collapsed by default. The
  live graph and common tuning remain prominent.
- At the default window size, the page must visibly communicate continuation through the
  navigator and, where practical, the next section/card entering the viewport.

## 9. Behavior-first throttle setup

### 9.1 Entry point

The Throttle card shows a concise current behavior and a prominent
**Configure Throttle Behavior…** action. Flight Modes contains a full **Throttle Setup**
page. The common behavior is selected before the detailed parameters appear.

### 9.2 Initial behavior recipes

The first release should provide a small, honest catalog:

1. **Standard HOTAS throttle**

   Lever position directly controls 0–100% forward thrust.

2. **Forward throttle with reverse zone**

   A configurable portion of the lower travel triggers Starfield reverse. Until analog
   reverse exists, the UI must say that reverse is on/off rather than proportional.

3. **Self-centering HOSAS — release to hold**

   Push to increase throttle, pull to decrease/reverse, and release to retain the
   commanded throttle. This uses rate/accumulator mode with zero decay.

4. **Self-centering HOSAS — release toward idle**

   Push or pull to change throttle; returning to center decays the commanded throttle
   toward idle at a configurable rate.

5. **Detents and boost**

   Positional throttle with guided zero/idle, 50% cruise, 100% plateau, and optional
   boost landmarks.

6. **Custom**

   Preserve the current configuration and expose the complete manual editor.

Do not advertise a centered proportional forward/reverse throttle until the runtime can
actually produce proportional reverse authority.

### 9.3 Recipe behavior

A recipe is an executable configuration operation, not a documentation link.

- Show what behavior will result before applying it.
- Preserve the physical axis binding and calibration unless the recipe explicitly
  requires recapture.
- Preview which owned settings will change.
- Require confirmation before overwriting a non-default custom throttle setup.
- After applying, immediately show the live graph and interpreted output.
- Manual edits after a recipe change the displayed behavior to **Custom** unless the
  configuration still exactly matches a known recipe.

### 9.4 Guided landmark capture

The guided flow uses user language:

- Move the lever to the position where the ship should be stopped.
- Move the lever to the 50% cruise detent.
- Move the lever to the start of full thrust.
- Move the lever to where boost should engage.

Raw integer values remain available only in Advanced detail or diagnostics.

### 9.5 Live interpretation

While the user moves the bound axis, show:

```text
Lever position: 62% (logical)
Interpreted region: Forward thrust
Commanded throttle: 31%
Reverse: inactive
Boost: inactive
```

Only show fields relevant to the selected behavior. The runtime preview and graph must
use the same normalization functions as live injection; do not duplicate formulas in
the rendering layer.

## 10. Ship-control route model

### 10.1 User-facing methods

The workbench uses three ordinary terms:

- **Direct** — calls a validated Starfield function.
- **Context** — uses Starfield's standard Select, Back, and navigation inputs so one
  binding works across relevant contexts.
- **Keyboard compatibility** — sends the keyboard/mouse command assigned to the named
  Starfield action.

**Custom shortcut** describes an arbitrary user-chosen keyboard/mouse output. Do not use
`SendInput`, semantic event, native injection, old path, or legacy path as primary UI
language. Those terms remain appropriate in diagnostics and technical documentation.

### 10.2 Explicit domain metadata

Replace route inference from “known action ID or not” with an explicit action catalog.
Each named action definition must provide at least:

```text
action ID
display label
UI group
recommended method
allowed methods
source INI key
ControlMap context/action pair
vanilla keyboard/mouse fallback
direct action identifier, when supported
availability/validation status
```

The runtime dispatcher, workbench grouping, macro target presentation, diagnostics, and
reference documentation must consume this shared catalog or generated views of it. Do
not maintain independent lists that can disagree.

### 10.3 Methods are not interchangeable

- Direct methods remain exact-gated and fail closed.
- Context methods intentionally use fixed vanilla E, Esc, and arrows. They do not
  follow the ship-only ControlMap binding because their value is cross-context
  consistency.
- Keyboard compatibility follows the user's Starfield keyboard/mouse binding where a
  resolvable mapping exists.
- Custom shortcuts send the exact explicit output chosen by the user.

### 10.4 Per-action compatibility overrides

Eligible Direct actions expose an advanced per-action method selector:

```text
Output method: Direct function — recommended
[Use keyboard compatibility]
```

When compatibility is selected:

```text
Output method: Keyboard compatibility · Left Shift
Uses your current Starfield Controls binding.
```

Rules:

- Direct remains the default for validated ship functions.
- Keyboard compatibility is offered only when the catalog has a known, supported
  keyboard/mouse equivalent.
- A failed Direct gate never changes the selected method automatically.
- The same method applies to physical bindings and named macro targets.
- Explicit raw macro targets remain raw regardless of the named action's method.
- Compatibility choices are installation-wide, not profile- or Binding-Layer-owned.
- **Restore recommended method** deletes the explicit override.

### 10.5 Configuration ownership

Proposed storage:

```ini
[ShipControlMethods]
FireBoosters=keyboard
UndockTakeOff=keyboard
```

Only deviations from the action catalog's current recommendation need to be stored.
Absence means “use the recommendation shipped with this AbsoluteHOTAS version.”

`[ShipControlMethods]` belongs to the installation's base custom configuration. Profile
overlay serialization must exclude it, and slot-file loading must not allow a profile
to change it. Import/export behavior must be documented explicitly:

- full setup export may include the section as installation preference metadata;
- importing as Main controls may restore it only with an explicit summary/confirmation;
- sparse overlays never contain or apply it.

If including compatibility methods in full export complicates safe import, omit them
from both export and import in the first release and document them as local
compatibility preferences.

Changing a method is committed through the guarded workbench transaction and rebuilds
every preloaded profile snapshot after releasing held outputs. Method lookup and
ControlMap parsing must not move onto the polling or profile-swap paths.

### 10.6 Direct availability status

The native service needs structured status rather than log-only failure. Distinguish:

- **Supported / waiting for context** — the installed build knows the route, but no
  eligible live ship object is currently active.
- **Available now** — required runtime identity and context gates currently pass.
- **Unavailable for this build** — static/version/function-byte validation failed.
- **Unavailable in this context** — expected current-state condition, not a patch
  failure.

The ordinary binding row shows a concise warning only for actionable build
unavailability. Diagnostics may show all states and gate detail.

### 10.7 No silent automatic fallback

When a Direct route is unavailable, offer:

> Direct function unavailable for this Starfield build. Keyboard compatibility is
> available.

The user may select it explicitly. Diagnostics may provide a confirmed batch command:

> **Use keyboard compatibility for unavailable functions**

The confirmation lists every affected action before writing overrides. There is no
background or per-press automatic fallback.

## 11. ControlMap reconciliation

### 11.1 Required precedence

Keyboard compatibility for a named Starfield action uses the existing reconciliation
order:

1. the catalog's vanilla keyboard/mouse default;
2. the matching record from `ControlMap_Custom.txt`;
3. an explicit legacy `[ShipButtonOutputs]` override, when present.

This preserves the established behavior in which AbsoluteHOTAS follows the user's
Starfield keyboard rebinding without requiring Joystick Gremlin or a duplicate manual
mapping.

### 11.2 Route boundaries

- Direct methods ignore ControlMap output.
- Context methods intentionally use their fixed vanilla navigation inputs.
- Keyboard compatibility methods use the reconciled output.
- Custom shortcuts use their explicit output.

### 11.3 Refresh contract

ControlMap parsing remains off the render loop. Refresh the cached resolution at:

- plugin/controller startup;
- configuration reload;
- workbench opening, as a single session command;
- Save & Apply;
- an explicit **Reload Starfield bindings** diagnostic action.

Opening the workbench may refresh the cache once; ordinary rendering must remain pure
and perform no repeated file reads. A changed resolution rebuilds the prepared binding
snapshots for Main controls and every profile before control output resumes.

### 11.4 Diagnostics

For every keyboard-compatible named action, report:

```text
Undock / Take Off
Method: Keyboard compatibility
Resolved output: Space
Source: ControlMap_Custom.txt
Context/action: Spaceship_Interaction / TakeOff
```

When no custom record exists:

```text
Resolved output: Space
Source: Vanilla fallback
ControlMap override: not found
```

When an explicit `[ShipButtonOutputs]` value wins, label it as a manual override.

## 12. Ship Buttons page design

### 12.1 Page order

1. Pinned **Editing bindings for** layer selector and Add Binding Layer action.
2. **Direct Ship Controls**.
3. **Navigation & Context Controls**.
4. **Cockpit & Docking Shortcuts**.
5. **Flight Assist**.
6. **Keyboard & Mouse Shortcuts**.

Major sections are visibly delineated. Do not put every category inside equally styled,
collapsed headers. The primary/direct and context sections should be discoverable on
first view; specialist/custom sections may begin collapsed after their existence and
purpose are visible.

### 12.2 Direct Ship Controls

Description:

> These controls call Starfield's ship functions directly and are active only in the
> appropriate ship context.

Internally group by user purpose where the list is long:

- Weapons & Combat
- Flight Systems
- Camera

Each row shows its binding and a quiet **Direct** method indicator. The per-action
compatibility selector is hidden behind row detail or an Advanced compatibility
disclosure; it must not make every normal binding decision feel technical.

### 12.3 Navigation & Context Controls

Description:

> These controls work across flight, targeting, menus, and dialogue using Starfield's
> standard Select, Back, and navigation inputs.

Contains:

- Select / Accept;
- Back / Cancel;
- Navigation Up / Down / Left / Right;
- Menu Control Reuse options.

The targeting-mode Left/Right exception remains native internally while the exact
targeting gate is active. The user-facing method remains Context because that is the
stable behavioral contract.

### 12.4 Cockpit & Docking Shortcuts

Description:

> These shortcuts use the corresponding Starfield keyboard command. They work only
> while Starfield is accepting or displaying that cockpit action.

Candidate actions:

- Undock / Take Off;
- Get Up;
- Exit Ship.

`UndockTakeOff` and `ExitShipFromCockpit` default to Keyboard compatibility until their
direct contextual fixtures are proven. The Get Up recommendation is an open question
at the end of this document.

These are named, ControlMap-aligned action shortcuts, not arbitrary custom outputs.

### 12.5 Flight Assist

Retain Hold Current Throttle, Full Stop, Cruise 50%, and Cruise Max as AbsoluteHOTAS
flight-assist commands. Explain that they control the plugin's throttle authority rather
than sending a Starfield key. If their location proves confusing during mockup testing,
link to Throttle Setup without duplicating their bindings.

### 12.6 Keyboard & Mouse Shortcuts

Rename **Custom Key Bindings** to **Keyboard & Mouse Shortcuts**.

Description:

> Send a keyboard key or mouse button from a controller button. Use these for commands
> that do not have a named control above. Assign the same key to the intended action in
> Starfield when necessary.

Retain:

- Add Shortcut;
- menu-navigation preset;
- explicit output picker;
- Bind, Clear, and Remove;
- link to the macro chord/sequence builder.

Avoid calling these “macros”; a single held output and an ordered macro remain distinct
concepts.

## 13. Diagnostics and feedback

### 13.1 Required user-visible diagnostics

- Current AbsoluteHOTAS, Starfield, and SFSE versions.
- Direct-control build validation state.
- Current ship-context state without treating normal on-foot/menu state as failure.
- Per-action recommended and selected method.
- Per-action reconciled keyboard output and resolution source.
- ControlMap path, read status, and last refresh result.
- Active runtime profile/layer and current editor target.
- Device resolution failures.
- A clear result after Save, reload, and read-back.

### 13.2 Support-oriented language

Logs may use precise technical terms, but every entry should include the action name and
selected method. Example:

```text
[ShipRoute] UndockTakeOff -> keyboard Space (ControlMap: Spaceship_Interaction/TakeOff)
[ShipRoute] FireBoosters -> direct (available)
```

### 13.3 Error policy

- Unavailable Direct route: actionable warning with compatibility option when eligible.
- Normal wrong context: quiet contextual state, not an alarming error.
- Missing ControlMap file: use vanilla default and say so.
- Invalid explicit output: reject or fall back according to existing safe parsing, with
  a visible diagnostic.
- Compatibility key sent: do not claim that Starfield completed the action; the plugin
  cannot generally observe that outcome.

## 14. Configuration and migration

### 14.1 Preserve user-owned data

The release continues to replace only shipped binaries/defaults. It must not overwrite:

- `AbsoluteHOTAS_Custom.ini`;
- managed profile files;
- user macros;
- calibration;
- custom keyboard/mouse shortcuts.

### 14.2 Existing profiles

- Existing overlay and full profiles remain readable.
- Missing `sPurpose` defaults to a general profile.
- Starter FPS and Flight Aux profiles may receive presentation metadata only when it can
  be added without overwriting user edits.
- Existing activation modes retain their runtime meanings.

### 14.3 Existing named ship actions

- No physical button binding is discarded when the action catalog gains explicit route
  metadata.
- Absence of `[ShipControlMethods]` selects the shipped recommendation.
- Existing `[ShipButtonOutputs]` values become meaningful only when that action uses
  Keyboard compatibility; Direct and Context methods continue to ignore them where
  specified.

### 14.4 Existing inverted throttle landmarks

Do not mirror existing saved landmark values automatically. Correct only new capture.
Document the behavior change and provide a recapture prompt or Reset Throttle Landmarks
action for users whose existing configuration was built around the old mismatch.

### 14.5 Save boundaries

- Compatibility method overrides are not modified by saving a Binding Layer.
- Applying a throttle recipe participates in the ordinary draft and Save & Apply
  transaction.
- Repository operations such as profile creation/import/reset retain confirmation and
  recovery behavior.
- Route selection and ControlMap refresh must not perform partial hidden saves.

## 15. Implementation architecture

### 15.1 Domain catalog first

Introduce the shared ship-action catalog and explicit routing before redesigning Ship
Buttons. The current model stores a native action and a prepared raw output but infers
dispatch from whether `ActionFromId` succeeds. That is the behavior to replace.

Suggested domain concepts:

```text
ShipControlMethod
  Direct
  Context
  KeyboardCompatibility
  CustomShortcut

ShipActionDefinition
ShipActionAvailability
ResolvedKeyboardOutput
ShipControlMethodOverrideRepository
ControlMapResolutionService
```

Names are illustrative; the behavioral separation is required.

### 15.2 Do not route captures by numeric ranges indefinitely

The existing workbench architecture already identifies numeric capture slots as a
legacy implementation detail. New Binding Layer and compatibility controls should use
typed capture targets/actions rather than expanding the numeric range scheme further.

### 15.3 One normalization implementation

Move or expose throttle logical-coordinate and preview calculations from the runtime
domain so both injection and the workbench call the same tested functions. Rendering
must not maintain a second approximation of runtime behavior.

### 15.4 Render purity

Pages render snapshots and submit actions. They do not:

- parse ControlMap per frame;
- create profiles during ordinary draw;
- silently reload runtime state;
- decide route defaults independently from the action catalog;
- infer inheritance only from rendered values.

## 16. Build sequence

### Slice 1 — Ship-control routing foundation

- Add explicit action catalog and route metadata.
- Preserve Direct and Context behavior.
- Make ControlMap-aligned Keyboard compatibility reachable for eligible actions.
- Add installation-wide per-action method overrides.
- Route named macro targets through the selected named-action method.
- Add structured availability and resolution diagnostics.
- Add unit tests for routing, precedence, no-silent-fallback, profile isolation, and
  ownership/release behavior.

### Slice 2 — Ship Buttons and Binding Layers

- Rebuild the page into the sections in this document.
- Add method indicators and advanced compatibility selection.
- Add pinned layer selector and guided Add Binding Layer flow.
- Add inherited/overridden markers and Use Primary Binding.
- Move complete management to a dedicated Profiles & Layers page.
- Test unsaved-target switching, activation capture, sparse serialization, and held
  output release.

### Slice 3 — Flight Axes refinement

- Implement the axis icon family.
- Strengthen card hierarchy and demarcation.
- Preserve graph prominence.
- Fix logical throttle landmark capture under inversion and calibration.
- Add focused normalization/capture tests and visual inspection at supported window
  sizes.

### Slice 4 — Behavior-first Throttle Setup

- Add recipe catalog and configuration preview.
- Add guided landmark capture.
- Add live interpreted-output feedback using runtime normalization.
- Retain complete manual customization.
- Test every recipe against its stated behavior and configuration round-trip.

### Slice 5 — Overview, navigation, and final profile workspace

- Add Setup Overview and readiness links.
- Finalize top-level/secondary navigation and opening behavior.
- Complete Profiles & Layers advanced tools and override summaries.
- Stabilize terminology throughout UI, README, references, logs, and release notes.

### Slice 6 — Release validation and documentation freeze

- Exercise the acceptance scenarios below with real hardware/in-game coverage.
- Run the full registered test suite and configuration migration tests.
- Freeze primary labels and locations.
- Only then build the tutorial website's click paths, screenshots, and video scripts.

## 17. Acceptance scenarios

### 17.1 First flight

- A fresh user can identify missing devices/axes from Setup Overview.
- They can bind throttle, pitch, yaw, and roll and verify each on a prominent graph.
- They can bind essential ship controls without opening Advanced.
- They understand what Save & Apply will modify.
- They can return to the game without learning profiles, macros, route technology, or
  raw INI terms.

### 17.2 Held alternate action

Given Button 1 fires Weapon 1 on Primary:

1. Select Add Binding Layer.
2. Name it Pinky Shift.
3. Capture Button 5 and choose While held.
4. Select Pinky Shift.
5. Override Button 1 to Weapon 2.
6. Save.

Holding Button 5 and pressing Button 1 fires Weapon 2. Releasing Button 5 returns Button
1 to Weapon 1. The user never has to understand sparse profiles to complete the task.

### 17.3 Inheritance

- Rebinding a Primary control propagates into a layer that inherits it.
- An overridden layer control remains independent.
- Use Primary Binding removes the sparse override rather than copying a value.
- The UI accurately labels inherited and overridden rows before and after Save.

### 17.4 Patch compatibility

- A failed Direct validation leaves the action inactive and emits no keyboard input.
- The workbench marks the Direct route unavailable without treating normal on-foot
  context as a patch failure.
- Selecting Keyboard compatibility uses the ControlMap-resolved output.
- Restoring the recommended method deletes the override and returns to Direct when the
  validated route is available.
- A named macro target follows the same selected method.

### 17.5 ControlMap alignment

- With no `ControlMap_Custom.txt`, Keyboard compatibility uses the vanilla default.
- With a valid custom keyboard binding, it uses the resolved custom output.
- An explicit `[ShipButtonOutputs]` override wins when the selected method permits it.
- Context controls continue to use fixed E/Esc/arrows regardless of ship-only rebinds.
- Reload Starfield Bindings refreshes diagnostics and runtime output without restart.

### 17.6 Cockpit interactions

- Undock / Take Off works through its declared keyboard-compatible route while the
  corresponding Starfield context is active.
- Exit Ship works through its declared keyboard-compatible route while the
  corresponding Starfield context is active.
- No cockpit shortcut claims success merely because a key event was emitted.
- These shortcuts do not fire while the workbench is open or the Full output gate is
  active.

### 17.7 Throttle behavior

- A standard positional recipe produces 0–100% forward command.
- A self-centering hold recipe changes command with deflection and holds at center.
- A decay recipe returns command toward idle at the displayed rate.
- A reverse-zone recipe labels reverse as binary until proportional reverse exists.
- Detent and boost capture land under the live marker with inversion both off and on.
- The displayed interpreted output matches runtime injection for the same sample.

### 17.8 Regression safety

- Profile swaps perform no file I/O on the polling path.
- A swap releases held native and keyboard outputs.
- Held buttons are consumed across a swap until released.
- Workbench open/close continues to park output and reseed edges safely.
- Existing custom/profile configurations round-trip without unrelated churn.

## 18. Documentation plan after UX stabilization

Prepare outcome-based curriculum during implementation, but delay UI-specific
screenshots and recording until navigation and labels are frozen.

Website structure:

- Quick Start
- Hardware setup paths: HOTAS, HOSAS, HOSAM, throttle quadrants
- Visual throttle recipe gallery
- Binding Layer recipes
- Advanced Profiles & Layers
- Macro examples
- Troubleshooting decision tree
- Complete configuration/reference material

Initial video set:

- Get Flying in Five Minutes
- Choose Your Throttle Behavior
- Add Alternate Controls with Binding Layers
- Tune Your Stick and Deadzones
- Profiles and Flight Modes — Advanced
- Diagnose Missing or Unresponsive Controls

Every guide states the AbsoluteHOTAS version it describes. In-workbench Learn links
open the relevant task page, not a generic documentation home page.

## 19. Decisions already closed

The following should not be reopened during implementation without new evidence:

- AbsoluteHOTAS remains both a quick-start flight input system and a deep controls
  toolkit.
- There is one configuration/runtime model, not separate simple and expert modes.
- Major capabilities remain discoverable even when their detailed controls are hidden.
- Binding Layers are a simplified presentation of sparse profiles.
- Full profile management receives a dedicated Advanced page.
- The large axis graphs remain prominent.
- Axis cards receive strong labels, demarcation, and a coherent icon family.
- Inverted throttle landmark capture uses logical coordinates consistent with the live
  marker and runtime.
- Ship controls use explicit Direct, Context, or Keyboard compatibility contracts.
- There is no global old/new output toggle.
- Eligible actions may have an explicit per-action Keyboard compatibility override.
- There is no silent fallback from Direct to keyboard output.
- Keyboard compatibility keeps the existing ControlMap reconciliation behavior.
- Universal context inputs remain fixed E/Esc/arrows by design.
- Compatibility method choices are not Binding-Layer/profile settings.
- The first UI overhaul preserves the current binary reverse runtime. Proportional
  reverse is a separate injection-research project and does not block the overhaul.
- UI-specific videos wait until the UX and terminology are stable.

## 20. Review questions and recorded decisions

### Q1. Get Up recommended method — resolved

`GetUp` currently has a native `SelectTarget` seat-exit lifecycle route and a prepared
ControlMap-aligned keyboard output. Keep Get Up as Direct and group it visually with
the cockpit interactions while retaining its Direct method indicator. Undock/Take-Off
and Exit Ship default to Keyboard compatibility. New runtime evidence may revise an
individual method later without changing the page structure.

### Q2. Analog reverse scope — resolved

The current reverse zone activates reverse as an on/off action rather than proportional
negative thrust. The first UI overhaul keeps that runtime unchanged and presents it
honestly as a binary Reverse Zone. Proportional reverse and the associated input-path
research are explicitly deferred to a later runtime project. If that work is validated,
the throttle recipe and visualization can be upgraded without restructuring the first
overhaul.

### Q3. Compatibility preferences in full exports — resolved

Compatibility methods are installation/patch preferences rather than control-layer
behavior. Omit `[ShipControlMethods]` from profile overlays and full profile
export/import in the first release. Preserve it locally in
`AbsoluteHOTAS_Custom.ini` and document it as an installation preference.

### Q4. Setup Overview reopening — resolved

Open Overview for fresh or incomplete configurations. Otherwise, reopen the last route
used in the current workbench session. This is session convenience state, not part of
the saved flight-control configuration.

## 21. Review exit criteria

The design is ready for implementation. Review established that:

- Q1–Q4 have recorded decisions;
- the target navigation labels are accepted;
- the Direct/Context/Keyboard compatibility terminology is accepted;
- the first Binding Layer workflow is accepted as a truthful representation of current
  profile arbitration;
- the initial throttle recipes are accepted without implying unsupported analog
  reverse;
- no acceptance scenario requires a second configuration model or silent fallback.

The first usable build is an evaluation milestone: further refinements should be driven
by observed setup and usage friction rather than additional speculative restructuring.

Implementation begins with Slice 1. Do not start with cosmetic page
rearrangement: explicit route metadata, compatibility dispatch, configuration
ownership, and tests are the foundation on which the new Ship Buttons UI depends.

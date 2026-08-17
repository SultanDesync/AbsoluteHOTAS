# AbsoluteHOTAS legacy workbench feature inventory for Absolute Control

> **Status:** UX/design input; implementation requirements, not a visual style guide
> **Date:** 2026-08-17
> **Scope:** Complete user-visible capability inventory of the embedded Dear ImGui
> workbench, reconciled with the accepted AbsoluteHOTAS UX overhaul and the modular
> Absolute Control suite boundary.

## 1. Purpose

This document tells UX and implementation teams what the Absolute Control version of
AbsoluteHOTAS must eventually do. It is the parity ledger between the existing embedded
workbench and the target native menu. It also records features that are visible in the
legacy HOTAS menu but belong to another suite module.

Use this document to:

- produce the AbsoluteHOTAS page flows and updated suite style guide;
- identify reusable visual concepts and their exact source locations;
- distinguish host component work from HOTAS provider work;
- prevent a visually complete menu from silently omitting advanced runtime behavior;
- plan migration without making Absolute Control the owner of HOTAS configuration or
  DirectInput; and
- decide which low-level compatibility settings deserve a supported UI.

This is not permission to port ImGui drawing code into the Scaleform host. Absolute
Control owns presentation. AbsoluteHOTAS supplies bounded state, domain meaning, draft
actions, capture, telemetry, persistence, and verification.

## 2. Source of truth and evidence boundary

The inventory was derived from the current implementation and the already accepted UX
decisions in:

- `src/BindingWizard.cpp` — shell, navigation, capture and dirty-close dialogs;
- `src/WizardFlightAxesPage.cpp` — axis cards, visualizations, reverse, digital axes,
  HOSAM and transitional alignment controls;
- `src/WizardBindingsPage.cpp` and `include/ShipActionCatalog.h` — named ship actions,
  menu reuse and custom shortcuts;
- `src/WizardTunePages.cpp` — aiming, transitional Camera Look and rate throttle;
- `src/WizardAdvancedPages.cpp` — macros, plugin controls and devices;
- `src/WizardProfileUI.cpp` — editing context, activation and profile operations;
- `include/WizardConfig.h`, `include/WizardDefs.h` and `src/WizardConfigCodec.cpp` —
  editable state and persistence mapping;
- [Workbench architecture](reference/wizard-workbench-architecture.md);
- [Profile switching](reference/profile-switching.md);
- [Accepted UX overhaul](UX-OVERHAUL-HANDOFF.md); and
- [Absolute Control integration handoff](ABSOLUTE-CONTROL-INTEGRATION-HANDOFF.md).

The accepted UX overhaul extends the legacy implementation in a few deliberate ways:
Setup Overview, behavior-first Throttle Setup, explicit binding-layer inheritance, a
dedicated Profiles & Layers workspace, and explicit ship-control method metadata are
requirements even where the current ImGui screen is less complete.

## 3. Product ownership: do not design across these boundaries

| Capability | Target product owner | Target menu location |
|---|---|---|
| DirectInput discovery, identity, sampling and calibration | AbsoluteHOTAS | AbsoluteHOTAS / Devices |
| HOTAS/HOSAS axes, throttle, strafe and reverse | AbsoluteHOTAS | AbsoluteHOTAS / Flight Axes and Throttle Setup |
| Ship buttons, custom shortcuts, profiles and macros | AbsoluteHOTAS | AbsoluteHOTAS pages |
| Bound pitch/yaw steering | AbsoluteHOTAS | AbsoluteHOTAS / Flight Axes |
| Independent reticle/aim axes and digital aiming | AbsoluteHOTAS | AbsoluteHOTAS / Aiming & Combat |
| HOSAM mode that releases pitch/yaw to native mouse steering | AbsoluteHOTAS | AbsoluteHOTAS / Flight Axes or Aiming & Combat summary |
| OpenTrack input, pose shaping, camera composition, recenter and toggle | Absolute Head Tracking | Its own Control module/pages |
| Idle mouse-centering policy, radius, delay and decay | AbsoluteZero | Its own Control module/page |
| Shared flight-lane arbitration | Optional Absolute Flight Runtime | Headless runtime; status may appear in diagnostics |
| Page layout, focus, modals, help and generic transactions | Absolute Control | Host shell |

### 3.1 Mouse ownership rule

Mouse input itself does not belong to AbsoluteZero. During flight:

- a bound HOTAS pitch or yaw axis claims the corresponding steering lane;
- HOSAM deliberately releases pitch/yaw steering to Starfield's native mouse path;
- independent aiming remains a separate HOTAS-owned lane; and
- AbsoluteZero may center native mouse steering only while the arbitration state says
  that path is active and no higher-priority owner has claimed it.

The Control UI should show the effective owner in plain language. It must not imply that
installing AbsoluteZero transfers mouse or steering ownership away from HOTAS.

### 3.2 Transitional legacy features

The current HOTAS binary and ImGui menu still contain Camera Look and alignment assist.
They remain in this inventory so their behavior can be migrated without loss, but they
must not become settings on the target `absolute.hotas` Control module:

- **Camera Look** migrates to Absolute Head Tracking.
- **Alignment assist** migrates to AbsoluteZero.
- The embedded Absolute Power tab is not a HOTAS feature and is not reproduced. Absolute
  Power already registers as its own Control module.

## 4. Target information architecture

AbsoluteHOTAS is one installed module in the Control sidebar. Flight Controls, Flight
Modes and Advanced are local groupings, not additional sidebar modules.

| Group | Page | Stable ID | Role |
|---|---|---|---|
| Flight Controls | Setup Overview | `hotas-setup` | Readiness map and deep links |
| Flight Controls | Flight Axes | `hotas-flight-axes` | Bind, observe and tune six-axis flight |
| Flight Controls | Ship Buttons | `hotas-ship-buttons` | Named actions, layers and simple shortcuts |
| Flight Modes | Throttle Setup | `hotas-throttle` | Behavior recipes and complete throttle interpretation |
| Flight Modes | Aiming & Combat | `hotas-aiming` | Aim-driven steering, independent aim and digital aim |
| Advanced | Profiles & Layers | `hotas-profiles` | Selected-profile management and sparse ownership |
| Advanced | Macros | `hotas-macros` | Chords, sequences, holds and turbo |
| Advanced | Devices & Calibration | `hotas-devices` | Device identity, live state, reassignment and calibration |
| Advanced | Plugin & Compatibility | `hotas-diagnostics` | Runtime gating, menu access, compatibility and support state |

The shorter visible label **Devices** remains acceptable if space is constrained, but
calibration must be discoverable from the page label, subtitle or overview link.

## 5. Capability/dependency legend

Every feature below carries one or more implementation dependencies.

| Code | Meaning |
|---|---|
| **STD** | Expressible with current standard Control controls such as toggle, slider, choice, text, action or ordinary binding display |
| **HUX** | Requires host presentation work such as a compound editor, status treatment, modal or selected-record workflow |
| **HABI** | Requires an optional, size-gated Absolute Control API extension |
| **CAP** | Requires provider-owned DirectInput axis/button/POV capture orchestrated by the host |
| **LIVE** | Requires a bounded live range/plot component and renderer |
| **PROV** | Requires extraction into a renderer-neutral HOTAS provider/domain service |
| **RT** | Requires HOTAS runtime/domain work rather than menu work alone |
| **SUITE** | Requires a separately owned suite module or the optional runtime coordinator |

Current Control can render scalar controls and keyboard bindings, but it cannot yet ask
HOTAS to capture DirectInput axes, buttons or POV directions. Its live range/plot channel
is experimental and lacks the final product renderer. Treat **CAP** and **LIVE** as real
gates, not visual polish.

## 6. Cross-cutting shell and transaction requirements

These behaviors apply to every editable HOTAS page.

### 6.1 Opening and navigation

- Fresh or incomplete configurations open Setup Overview. Otherwise, reopen the last
  HOTAS route used in the current game session.
- Overview cards and contextual actions deep-link to the exact resolving page/control.
- The legacy Toggle Wizard binding must eventually request
  `Show("absolute.hotas", pageId)` through an optional host command. It must never
  synthesize Pause, Escape, F2 or mouse input to navigate the menu. **HABI, PROV**
- When Control or the open-command capability is absent, the standalone HOTAS runtime
  and its supported fallback access path continue to work.
- Keyboard, mouse and controller navigation are host responsibilities. DirectInput
  gameplay capture is a separate provider-owned operation.
- When opened without a gameplay-pause/input-owning context, present a concise warning
  and the available navigation method rather than implying that mouse interaction works.
- Keep one page scroll owner. The host shell retains selected-control help, status and
  stable Apply/Cancel/Close actions outside the scrolling provider content.

### 6.2 Editing context

- The current editing target is always visible and distinct from the active runtime
  profile: for example, **Editing: Main controls** versus **Active: Landing Controls**.
- Ship Buttons presents the approachable **Binding Layer** selector. Other pages use
  explicit Main/profile editing context unless entered from Profiles & Layers.
- Switching edit target while dirty presents **Apply / Discard / Stay** before the
  provider changes target.
- A capture is bound to module, page, control ID, profile identity and draft generation.
  Route or profile changes cancel it rather than applying to the new target.

### 6.3 Save, apply and close

- Edits change a provider draft. A successful capture is still only a draft change.
- Apply validates, atomically writes the correct custom/profile file, reloads through
  the normal runtime path and verifies semantic read-back.
- Cancel restores the saved snapshot without persistence.
- A failed write, reload or read-back keeps the draft recoverable and reports a bounded,
  actionable error.
- The host may present per-page transactions, but HOTAS remains the sole configuration
  owner and must prevent two frontends from holding authoritative editable sessions.
- Profile repository operations—create, import, reset and any future delete/detach—use
  explicit confirmations and retain the documented backup/recovery behavior.

### 6.4 Safe input ownership

- Opening an editable HOTAS session parks plugin-owned flight injection and output lanes
  covered by the editor-arbitration policy.
- Capture parks affected gameplay output, macros and profile switching.
- Close, cancellation, host loss and device loss release transient output and reseed held
  edges before gameplay resumes.
- Normal rendering never performs disk I/O, DirectInput enumeration, ControlMap parsing,
  profile creation or runtime reload.

### 6.5 Provider-owned DirectInput capture contract

The host presents one shell-wide capture modal. HOTAS owns polling and interpretation.
The contract needs begin, status/read, commit and cancel operations plus provider-formatted
prompt, result and error text. **HABI, CAP, PROV**

Required capture kinds:

| Kind | Legacy behavior to preserve |
|---|---|
| Axis | Snapshot all available devices; detect movement beyond the current threshold; require five stable detection frames; identify X/Y/Z/Rx/Ry/Rz/Slider0/Slider1 |
| Button or POV | New press edge only; two-frame bounce guard; last confirmed press wins after a 50 ms quiet window |
| Selector/activation position | Same settle-to-quiescence rule with a 300 ms quiet window so rotary detents and deep trigger stages can settle |
| Timeout | Eight seconds; no-input timeout cancels without creating a binding |

Prompts must explain that an already-held selector has to move to the desired position.
Duplicate product names must display a stable disambiguator. Escape/Back, route change,
profile change, device loss, host loss, menu close and provider unregistration cancel.

## 7. Page specification — Setup Overview

**Purpose:** Answer whether the installation is ready to fly and link directly to the
place that resolves each incomplete item. It is not a scorecard or duplicate settings
page. **HUX, PROV, LIVE optional**

Required cards/status:

1. **Devices detected** — count, duplicate-name warning and Devices deep link.
2. **Primary axes** — bound/responding state for throttle, pitch, yaw, roll, lateral
   strafe and vertical strafe; pitch/yaw may truthfully show mouse-owned under HOSAM.
3. **Throttle behavior** — selected recipe/custom state, sensible live interpretation,
   and Throttle Setup deep link.
4. **Essential ship actions** — useful bound count and Ship Buttons deep link. Do not
   invent a universal mandatory list without a product decision.
5. **Editing and active profile/layer** — clearly distinguished with Profiles deep link.
6. **Runtime compatibility** — build validation, unavailable direct routes and diagnostic
   deep link.
7. **Suite ownership** — concise effective steering/aim/centering state when other flight
   modules are installed; no settings duplication.

Actions:

- **Continue configuring** goes to the next incomplete task or last page.
- **Save & Fly** applies a dirty draft when valid, or closes immediately when clean.
- Each readiness item exposes a direct resolving link.

Acceptance:

- A fresh user can identify missing devices and primary axes without opening Advanced.
- A working user can leave without reviewing optional features.
- Missing live telemetry degrades to bound/detected state rather than blocking the page.

## 8. Page specification — Flight Axes

**Purpose:** Bind, observe and tune the six core flight axes. Throttle behavior detail
lives on Throttle Setup, but its binding, common tuning, live graph and current behavior
remain discoverable here.

### 8.1 Page-level controls

| Feature | Behavior | Dependencies |
|---|---|---|
| Flight controls enabled | Profile-owned `bEnableInjection`; a parked profile may disable flight injection while retaining its buttons/macros | **STD, PROV** |
| Required-axis summary | Bound count plus unavailable/mouse-owned distinctions | **HUX, PROV** |
| Axis navigator | Six semantic icons, labels and bound state; selecting one moves focus/scroll to its card | **HUX** |

### 8.2 Core axis cards

The six cards are Throttle, Pitch, Yaw, Roll, Strafe Lateral and Strafe Vertical.
Each card requires:

- semantic icon, strong name, behavior description and status;
- binding value, Bind/Rebind, Clear and duplicate-device-aware label;
- inversion;
- sensitivity when supported by the current schema;
- saturation;
- center deadzone;
- a large live graph/range meter; and
- explicit control path and behavior, not an internal mechanism label.

Supported tuning by axis:

| Axis | Invert | Sensitivity | Saturation | Deadzone | Special state |
|---|---:|---:|---:|---:|---|
| Throttle | Yes | Yes | Yes | Yes | Positional/rate behavior; configure action |
| Pitch | Yes | Yes | Yes | Yes | Direct, aim-driven or native mouse-owned |
| Yaw | Yes | Yes | Yes | Yes | Direct, aim-driven or native mouse-owned |
| Roll | Yes | Yes | Yes | Yes | Direct roll |
| Strafe Lateral | Yes | Yes | Yes | Yes | Direct 6-DOF translation |
| Strafe Vertical | Yes | No separate sensitivity in current schema | Yes | Yes | Direct 6-DOF translation |

All bindings require **CAP, PROV**. Standard tuning is **STD, PROV**. The graphs require
**LIVE, HUX, PROV**.

Pitch and yaw cards must preserve their binding/tuning while HOSAM is enabled, show them
inactive rather than empty, and state that Starfield's mouse path currently owns
steering.

### 8.3 Throttle card summary

- Show **Positional throttle** or **Rate/self-centering throttle**.
- Provide **Configure Throttle Behavior…** deep link.
- Keep the large logical-range graph and common axis tuning.
- Detailed landmarks, regions and recipes belong to Throttle Setup to avoid two
  competing editors for the same fields.

### 8.4 Reverse strategies

The complete product supports three mutually understandable hardware strategies:

1. reverse zone at the low end of the main throttle;
2. held digital Reverse button; and
3. dedicated Reverse analog axis with binding, invert, sensitivity, saturation and
   live meter.

Current reverse-zone output is binary, not proportional. Say so. A held reverse button
has precedence when it is active. The dedicated axis remains an input strategy even
though the present runtime's negative-authority behavior must be described truthfully.

### 8.5 Digital fallbacks

Bindings:

- Digital Reverse;
- Digital Roll Left;
- Digital Roll Right;
- Digital Strafe Left;
- Digital Strafe Right;
- Digital Strafe Up; and
- Digital Strafe Down.

Tuning:

- Digital roll strength; and
- Digital strafe strength.

These accept buttons or POV directions. **CAP, STD, PROV**

### 8.6 HOSAM

Retain **Mouse steering (HOSAM)** as a HOTAS routing setting. The page must explain:

- pitch/yaw HOTAS injection is parked while native mouse steering owns those lanes;
- roll, strafe, throttle, buttons and independent aim remain HOTAS capabilities; and
- idle centering, when installed, is configured in AbsoluteZero.

Show the current aiming mode with a deep link to Aiming & Combat. Do not show alignment
radius, idle delay or decay settings here. **STD, PROV, SUITE status optional**

### 8.7 Flight Axes acceptance

- The page communicates all six axes without relying on scrolling far enough to discover
  the second card.
- Every boundary is recognizable without color alone.
- Binding, movement, inversion, tuning and saved read-back can be verified end to end.
- Live graphs use runtime normalization, not a renderer-side approximation.
- HOSAM never erases preserved pitch/yaw bindings.

## 9. Page specification — Throttle Setup

**Purpose:** Select desired behavior first, then expose only the parameters needed to
produce and verify it. **HUX, PROV, CAP, LIVE**

### 9.1 Behavior recipes

Required initial choices:

1. **Standard HOTAS throttle** — lever position produces 0–100% forward thrust.
2. **Forward throttle with reverse zone** — lower travel invokes binary Starfield
   reverse.
3. **Self-centering HOSAS — release to hold** — rate mode, zero decay.
4. **Self-centering HOSAS — release toward idle** — rate mode with configurable decay.
5. **Detents and boost** — positional throttle with guided stop/idle, cruise, full
   thrust and optional boost landmarks.
6. **Custom** — preserves and exposes the complete current configuration.

A recipe previews the fields it will change, preserves physical binding/calibration
unless stated otherwise, confirms before overwriting a non-default custom setup, and
participates in the normal draft transaction. Manual divergence changes the detected
recipe to Custom.

### 9.2 Positional throttle editor

Required controls and interpretation:

- logical lever live marker;
- idle plateau;
- symmetrical deadzones;
- center/cruise detent center and width;
- Capture Center/Detent;
- reverse zone enabled, zero-thrust center and width;
- Capture Zero-Thrust;
- boost zone enabled, start/center and width;
- Capture Boost;
- 100% plateau/hold-for-boost behavior;
- main throttle invert, sensitivity, saturation and deadzone summary/deep link;
- dedicated reverse axis and held reverse button strategy links; and
- reset/recapture landmarks action with confirmation where user work would be lost.

### 9.3 Rate/self-centering editor

- Enable rate/self-centering mode.
- Ramp rate.
- Decay rate.
- Reverse velocity gate.
- Pilot Turn Assist enabled.
- Turn Assist activation: Always, While held or Toggle.
- Capture/clear the activation binding for held/toggle modes.

### 9.4 Guided capture and live interpretation

Guided prompts use behavior language: stopped position, 50% cruise detent, start of full
thrust and boost engagement. Raw integer landmarks live in Advanced detail/diagnostics.

While the lever moves, show relevant values such as:

- logical lever percentage;
- interpreted region;
- commanded throttle;
- reverse active/inactive; and
- boost active/inactive.

### 9.5 Coordinate invariant

Device calibration min/max remain in hardware space. The live marker, inversion,
captured throttle landmarks, visible zones, stored centers and runtime comparisons all
use logical throttle space. With inversion on or off, capturing a point must place its
marker directly under the current live marker. Existing stored landmarks are not
blindly mirrored; offer recapture/reset guidance.

## 10. Page specification — Ship Buttons

**Purpose:** Bind named ship behavior using clear Direct, Context and Keyboard
compatibility contracts; provide simple per-layer shortcuts without making every user
learn macros. **CAP, HUX, PROV, RT for method work**

### 10.1 Binding Layer context

Pin **Editing bindings for** and **Add binding layer** above the action list. Human layer
names lead; activation device/button is secondary. On a sparse layer, each row shows:

- **Inherited from Primary** or **Overridden in [layer]**; and
- **Use Primary Binding** for an override, which removes the sparse key rather than
  copying the effective base value.

### 10.2 Named action catalog

Every row needs binding/capture/clear, selected output method, availability, and detail
for resolved compatibility output where relevant.

**Weapons & Combat**

- Fire Weapon 1
- Fire Weapon 2
- Fire Weapon 3

**Flight Systems**

- Fire Boosters
- Switch Flight Modes
- Ship Action 1
- Open Scanner
- Repair Ship
- Ship Alternate Control
- Cruise
- Autopilot On / Off

**Camera**

- Toggle POV
- Zoom Camera In
- Zoom Camera Out

**Navigation & Context Controls**

- Select / Accept
- Back / Cancel
- Navigation Up
- Navigation Down
- Navigation Left
- Navigation Right

**Cockpit & Docking Shortcuts**

- Undock / Take-Off
- Get Up
- Exit Ship

Recommended method requirements:

- Select/Back/navigation are Context.
- Get Up is Direct.
- Undock/Take-Off and Exit Ship are Keyboard compatibility.
- Other validated named ship actions default to Direct and may offer an explicit
  per-action Keyboard compatibility override only when the catalog supports it.
- A failed Direct route never falls back silently. Offer a user-confirmed method change.
- The same named-action method applies to its physical binding and named macro target.
- Explicit raw macro targets remain raw.

Availability distinguishes **available now**, **supported/waiting for context**,
**unavailable in this context** and **unavailable for this build**. Only actionable
build failures need warning emphasis on the ordinary row.

Fire Boosters also exposes **Let boost temporarily take throttle authority**.

### 10.3 Menu control reuse

Optional profile-owned reuse:

- Pitch axis for menu Up/Down;
- Yaw axis for Left/Right;
- Primary Weapon for Select/Accept;
- invert vertical navigation;
- invert horizontal navigation;
- engage threshold; and
- release threshold.

The release threshold must remain below the engage threshold to provide hysteresis.
On context entry the axis/button must first return neutral/released so carried gameplay
input cannot activate a prompt. **STD, PROV, RT validation**

### 10.4 Flight Assist

Retain these HOTAS-native throttle commands:

- Hold Current Throttle;
- Full Stop;
- Cruise 50%; and
- Cruise Max.

Explain that they change HOTAS throttle authority rather than emit a Starfield key.
Link to Throttle Setup if needed; do not duplicate their binding editor in two places.

### 10.5 Keyboard & Mouse Shortcuts

This is the renamed legacy Custom Key Bindings feature:

- add shortcut;
- add the menu-navigation preset (WASD, Tab, E and Escape);
- capture controller button/POV;
- select one explicit keyboard key or mouse button output;
- Bind, Clear and Remove; and
- link to Macros for chords/sequences.

The output catalog preserves mouse buttons 1–4, modifiers/control keys, alphabet,
numbers, punctuation, arrows, numpad, F1–F12 and extended navigation keys. Unknown but
valid stored tokens remain visible instead of being silently dropped. A single held
shortcut is not called a macro.

## 11. Page specification — Aiming & Combat

**Purpose:** Explain and configure the relationship between steering and reticle/aim
authority. **STD, CAP, LIVE optional, PROV**

### 11.1 Aim system and mode

- Enable Aim System.
- Show the effective mode:
  - **Independent Aim & Steer** when separate aim axes are bound; or
  - **Aim-Driven Steering** when they are unbound.
- In aim-driven mode, expose Steering Sensitivity.
- Show the relationship with HOSAM without duplicating the HOSAM toggle unless UX
  testing demonstrates that one synchronized control is clearer than a deep link.

### 11.2 Independent analog aim

- Aim Yaw binding, invert and sensitivity.
- Aim Pitch binding, invert and sensitivity.
- Aim smoothing.
- Live input/reticle feedback when the component is available.

### 11.3 Digital aim override

- Aim Left binding.
- Aim Right binding.
- Aim Up binding.
- Aim Down binding.
- Aim Center binding.
- Digital aim speed/strength.
- Aim Mode Toggle binding.

The page must not absorb OpenTrack/camera-pose controls merely because both features
affect where the pilot looks.

## 12. Page specification — Profiles & Layers

**Purpose:** Expose the existing sparse overlay engine truthfully, while letting a
casual user create a Binding Layer without understanding file composition. **HUX, CAP,
PROV**

### 12.1 Add Binding Layer

Guided workflow:

1. Name the layer.
2. Capture its activation control.
3. Choose **While held**, **Toggle on/off** or **Selector position**.
4. Open Ship Buttons with the new sparse overlay selected.

Explain: “This layer starts with your Primary bindings. Change only the controls that
should behave differently.” Do not copy Main values into the overlay.

### 12.2 Advanced selected-profile workspace

Required list/detail behavior:

- select Main controls or one managed profile;
- show profile name, kind, purpose, managed filename, slot/order and activation;
- show active runtime profile separately;
- show override count;
- create and rename sparse overlays;
- configure momentary, toggle and selector activation;
- configure Main-controls return/detent activation as momentary or selector, matching
  the existing base-activation model;
- capture, replace and clear a hardware activation trigger;
- show optional keyboard activation where supported;
- jump into axes, tuning, bindings, macros or injection state for that edit target;
- show parked/on-foot profiles (`bEnableInjection=false`) without calling them globally
  disabled;
- export Main/effective configuration as a full profile;
- import a full profile as Main controls only, rejecting sparse overlay import;
- reset Main to shipped defaults with backup while preserving local profile routing;
- expose starter FPS parked and Flight Aux overlay creation where still supported; and
- show operation results and recovery/backup path.

### 12.3 Sparse ownership

For every editable profile value, the provider must supply source ownership, not only
effective value. The UI needs:

- inherited/overridden marker;
- **Use Primary [value/binding]** to remove the override;
- explicit ownership even when the override equals Main; and
- normalization back to inheritance on Save where the accepted domain rule applies.

Runtime profile switching remains preloaded and performs no disk I/O. A swap releases
held output/macros and reseeds input. The UI must not depict profiles as an arbitrary
freely composable stack.

### 12.4 Deferred profile operations that need explicit product decisions

Delete and Detach/Materialize are not complete legacy guarantees. If added, define
activation cleanup, backup/recovery, active-profile behavior and sparse/full semantics
before presenting enabled actions. Do not make a decorative disabled button the only
specification.

## 13. Page specification — Macros

**Purpose:** Build ordered controller-triggered actions while preserving incomplete or
hand-authored source rows losslessly. Use a constant-size selected-record list/detail
editor rather than one scalar control per possible step. **HUX, CAP, PROV**

Macro list and operations:

- add macro;
- select macro;
- edit name;
- duplicate-name disambiguation in the list;
- capture/clear trigger;
- enable turbo repeat;
- delete macro with confirmation when appropriate; and
- add the Grav → Shields example preset.

Ordered step editor:

- add/delete/reorder steps;
- one or more simultaneous targets per step, displayed as a chord with `+`;
- add/remove chord targets;
- choose a named ship action or explicit raw keyboard/mouse target;
- preserve unknown source tokens visibly;
- choose Tap or Hold;
- Tap amount is repetition count;
- Hold amount is duration in milliseconds;
- configure delay/gap before the next step; and
- retain incomplete draft macros in the file even when they are not runnable.

Named action targets follow the action's selected Direct/Context/Keyboard compatibility
method. Raw `key:` and `mouse:` targets do not. Validation must distinguish a recoverable
incomplete draft from a malformed value that cannot be saved safely.

## 14. Page specification — Devices & Calibration

**Purpose:** Explain what HOTAS can see, resolve duplicate identity, verify live input and
maintain hardware-space calibration. **HUX, CAP/LIVE, PROV**

For every DirectInput device show:

- product name;
- stable instance/identity detail;
- VID/PID when available;
- runtime index as diagnostic detail, not the primary user identity;
- axis and button counts;
- connected/resolved state;
- current axis/button/POV state on demand; and
- saved min/max for all eight possible axes.

Operations:

- start full-device calibration;
- instruct the user to move every axis through its extremes;
- show which axes are changing and observed min/max;
- Commit or Cancel calibration;
- clear saved device calibration with confirmation; and
- detect adjacent duplicate product names and stage a complete device reassignment.

Duplicate reassignment must visit every binding-bearing field: core/reverse/aim axes,
buttons, named ship actions, digital axes/aim, custom shortcuts, macros, profile
activation, transitional data still owned during migration and calibration keys. It is
a draft operation until Apply.

Disconnect/reconnect must not crash or silently retarget a different same-name device.
Device loss during capture or calibration ends the operation with an actionable state.

## 15. Page specification — Plugin & Compatibility

**Purpose:** Put runtime ownership, safety, menu access, compatibility and support
evidence in one place without overwhelming the setup flow.

### 15.1 Plugin control bindings

- Activate HOTAS output.
- Stop HOTAS output.
- Open Absolute Control / AbsoluteHOTAS page—the successor to Toggle Wizard once the
  optional host deep-link capability exists.
- Show the legacy/recovery shortcut while the fallback workbench remains supported.

The labels **Activate** and **Stop** need explanatory copy or a future naming decision;
they are distinct from profile-owned **Flight controls enabled** and the keyboard panic
kill switch. **CAP, HABI, PROV**

### 15.2 Automatic pilot context

- Outside the pilot seat: Off, Park flight controls, or Park all plugin output.
- Automatic pilot detection enabled.
- Pilot-context latch from 500 to 30000 ms.
- Current context and last transition as diagnostic status.
- Manual signal mode remains diagnostic/compatibility-only unless product evidence
  justifies a normal UI.

### 15.3 Runtime and compatibility status

Show:

- AbsoluteHOTAS, Starfield, SFSE and relevant suite module versions;
- current plugin active/parked state and reason;
- Control provider registration/capability state;
- native/direct action build validation;
- current ship context without treating normal on-foot/menu state as an error;
- per-action recommended/selected method and resolved keyboard output/source in detail;
- ControlMap path, read status and last refresh result;
- active runtime profile and editor target;
- device resolution failures;
- optional coordinator present/absent and current lane ownership;
- last Apply/write/reload/read-back result; and
- bounded support copy/log actions where the host supports them.

Actions:

- Reload Starfield bindings/ControlMap cache.
- Restore a per-action recommended method.
- Optionally apply Keyboard compatibility to a reviewed list of unavailable actions,
  with confirmation and no silent fallback.
- Open the responsible Absolute Head Tracking or AbsoluteZero module when installed;
  do not mirror their controls.

### 15.4 Compatibility controls needing a supported placement decision

The runtime contains `bEnabled`, native-controls enablement, named ship-buttons
enablement, logging and Signal Hunter fallback beyond the current WizardState. Their
recommended disposition is recorded in section 19 rather than silently inventing UI.

## 16. Separate-module extraction inventory

These are required migration features, not HOTAS pages.

### 16.1 Absolute Head Tracking replacement for Camera Look

The target Head Tracking module must account for the current legacy feature set:

- master camera-look enable;
- OpenTrack input enable and source state;
- runtime Toggle and Recenter bindings;
- Yaw, Pitch and Roll component enablement;
- optional per-component joystick override axis;
- per-component invert, sensitivity/scale and maximum degrees;
- tracker angular deadzone;
- joystick override deadzone;
- smoothing;
- live current degrees for yaw/pitch/roll;
- clear source status: disabled, waiting for OpenTrack, OpenTrack active or joystick
  override active; and
- reset tuning.

The existing implementation reference is `src/WizardTunePages.cpp`, especially
`DrawHeadLookAxisGraph` and `DrawCameraLookTab`. `sSource` and stale-milliseconds policy
also exist in the INI but are not fully presented by the legacy workbench; their target
status needs a Head Tracking product decision.

### 16.2 AbsoluteZero replacement for alignment assist

The target AbsoluteZero module must account for:

- idle mouse-centering enabled;
- activation radius;
- idle delay before decay;
- decay/centering speed;
- effective native-mouse-steering state;
- suppression reason when HOTAS owns pitch/yaw, the menu owns input, or another lane
  owner wins; and
- standalone/coordinator availability status.

The current transitional implementation reference is
`src/WizardFlightAxesPage.cpp:452` and `src/WizardConfigCodec.cpp` fields
`bAlignmentAssist`, `fAlignmentRadius`, `iAlignmentIdleMs` and
`fAlignmentDecayRate`.

### 16.3 Optional Absolute Flight Runtime

The coordinator is headless and optional. It may expose bounded diagnostic state to the
menu, but its absence must not prevent Control from hosting pages or any suite mod from
starting safely. The menu needs no duplicate coordinator settings unless the runtime
contract later defines a genuine user choice. It does need status sufficient to explain:

- current steering, independent-aim, camera-pose and centering owners;
- why a lane is parked or denied;
- menu/capture suspension; and
- incompatible standalone combinations that failed closed.

## 17. Reusable legacy visual language and assets

No standalone SVG, PNG or icon-font asset exists for the current axis icons. They are
code-native Dear ImGui draw geometry. Reuse the semantics and proportions by extracting
or recreating host-owned vector assets; do not call the ImGui renderer from Control.

| Legacy element | Source | Reusable meaning | Target treatment |
|---|---|---|---|
| Axis icon family | `src/WizardFlightAxesPage.cpp:487-631` | Throttle gate/lever; pitch/yaw/roll rotation; lateral/vertical translation | Rebuild as one monochrome 20–24 px vector family with text labels |
| Axis navigator | `src/WizardFlightAxesPage.cpp:646` | All-six discoverability plus bound state | Control page local navigation/focus pattern |
| Axis card header | `src/WizardFlightAxesPage.cpp:815` | Icon, behavior, path and bound/inactive status | Strong host card/section hierarchy |
| Throttle range graph | `src/WizardFlightAxesPage.cpp:22` | Zones, landmarks, live input and output meaning | Host RangeMeter/plot using semantic tokens and labels |
| Bipolar axis graph | `src/WizardFlightAxesPage.cpp:301` | Negative/center/positive input and deadzone | Host live range component |
| Head-look graph | `src/WizardTunePages.cpp:87` | Component angle and active source | Move to Head Tracking visual vocabulary |
| Route summary | `src/WizardBindingsPage.cpp:120` | Direct/Context/Keyboard method and availability | Quiet method badge plus expandable detail |
| Macro step/chord rows | `src/WizardAdvancedPages.cpp:253` | Ordered steps and simultaneous `+` targets | Selected-record compound editor |
| Calibration workflow | `src/WizardAdvancedPages.cpp:16` | Device identity, observed extremes and commit/cancel | Guided device operation/modal |
| Profile context and dirty switch | `src/WizardProfileUI.cpp:89-277` | Edit-target identity and guarded switching | Persistent host context plus Apply/Discard/Stay |

### 17.1 Icon semantics

- Throttle: twin track rails, moving lever and knob.
- Pitch: horizontal axis/craft cue with curved rotation arrow.
- Yaw: vertical axis cue with curved rotation arrow.
- Roll: craft/forward cue with full rotation arc.
- Lateral and vertical strafe: centered craft/square with straight double-ended arrows.
- Curved arrows always mean rotation; straight arrows always mean translation.
- Icons are recognition cues. Text remains authoritative and accessible.

### 17.2 Semantic color cues

The legacy workbench uses amber/orange for thrust, cyan for rotation and purple for
translation, plus green bound and orange unbound status. Throttle graphs also distinguish
dead, active, cruise/detent, boost, center and live values.

UX may evolve the palette, but must preserve semantic separation, contrast and a
non-color label/pattern for every state. Colors belong to Control tokens rather than
provider-supplied RGBA constants.

### 17.3 Interaction patterns worth preserving

- axis status and behavior before tuning parameters;
- large authoritative live graphs;
- visible binding state and device identity;
- explicit Direct/Context/Keyboard behavior;
- selected-record editing for unbounded profiles/macros/devices;
- chord targets joined by `+` and sequence order shown spatially;
- incomplete drafts remain visible rather than disappearing; and
- guided capture/calibration language instead of raw numbers first.

## 18. Component and API gap ledger

| Requirement | Existing basis | Remaining work |
|---|---|---|
| Scalar booleans/numbers/choices/text | Control standard controls | Provider descriptors, validation and persistence |
| Dirty navigation resolution | Host rejects unsafe changes today | Apply / Discard / Stay modal and provider target lock |
| DirectInput capture | `WizardCapture` owns working behavior | Optional host/provider capture ABI and modal |
| Open/deep-link | Host registration/state queries | Optional Show(module,page,control) command |
| Live axis/throttle/head-look state | Experimental live frame and range/plot contracts | Product renderer, bounded provider frames and visibility throttling |
| Axis navigator/card | Legacy ImGui pattern | Host-native local navigation and card hierarchy |
| Binding layer inheritance | Sparse profile runtime exists | Source-ownership metadata and per-field revert action |
| Selected-record profiles/macros/devices | Legacy editors exist | Constant-size host list/detail components and provider actions |
| Compound macro editor | None in standard scalar vocabulary | Ordered steps, chords and validation UX |
| Device calibration | Legacy polling workflow | Host operation/modal plus provider state machine |
| Ship method/availability | Shared action catalog partly exists | Finish method override runtime/config and structured status |
| Suite lane status | Ownership decision exists | Coordinator ABI/status model and separate module adapters |

## 19. Configuration coverage and disposition

### 19.1 Supported Control surface

| Configuration family | Target page/owner |
|---|---|
| `[Injection] bEnableInjection` | Flight Axes; profile-owned flight controls switch |
| `[Hardware]` core/reverse axes and tuning | Flight Axes |
| `[Normalization]` throttle regions | Throttle Setup |
| `[DualStick]` accumulator/rate throttle | Throttle Setup |
| `[DigitalAxes]` | Flight Axes |
| `[Aim]` source/aim axes/digital aim/HOSAM | Aiming & Combat plus HOSAM summary/toggle |
| `[Buttons]` three hardware bindings | Plugin & Compatibility |
| `[Gate]` automatic context mode/signal/latch | Plugin & Compatibility |
| `[ControlExtensions]` | Ship Buttons / Flight Assist, linked to throttle |
| `[ShipButtons]` 23 named actions | Ship Buttons |
| `[MenuControls]` | Ship Buttons / Navigation & Context |
| `[ButtonExpansion]` | Ship Buttons / Keyboard & Mouse Shortcuts |
| `[Calibration]` | Devices & Calibration |
| `[Macro:*]` | Macros |
| `[Profiles]` and profile metadata/routing | Profiles & Layers |
| `[HeadTracking]` | Absolute Head Tracking, not HOTAS |
| `[Aim]` alignment fields | AbsoluteZero, not HOTAS |

### 19.2 Keys that need an explicit supported-UI decision

| Key/family | Current meaning | Recommended first disposition |
|---|---|---|
| `[General] bEnabled` | Product master enable | Expose only if UX can distinguish it from injection, Activate/Stop and panic kill; otherwise diagnostics/manual |
| `[NativeControls] bEnabled` | Native named ship actions, boost and strafe modifier | Advanced compatibility toggle with strong consequences and availability summary |
| `[ShipButtons] bShipButtonsEnabled` | Master named-action output | Advanced Ship Buttons or compatibility control if a real disable use case is retained |
| `[Buttons] iToggleActiveKey` | Keyboard panic kill/release | Supported safety binding if Control gains keyboard-key capture/edit semantics that preserve the default |
| `[Injection] bEnableLog` | Verbose diagnostic logging | Advanced diagnostics toggle, ideally session-scoped or clearly persistent |
| `[Injection] bSignalHunterFallback` | Legacy flight-cluster discovery requiring external Starfield INI setup | Expert compatibility action with prerequisite validation; never normal setup |
| `[HeadTracking] sSource`, `iStaleMilliseconds` | Tracker source/staleness | Absolute Head Tracking product decision |
| `[Injection] iPollRateHz`, `iThrottleBurstMs` | Runtime engineering tuning | Manual/support-only unless safe validated ranges and user benefit are established |

### 19.3 Legacy/manual compatibility keys not proposed as ordinary controls

- `[General] bSyncShipOutputsFromControlMap` — legacy 4.x compatibility behavior.
- `[UI] bEnableWorkbench` — fallback frontend installation/restart concern during the
  transition, not flight behavior.
- `[Buttons] bAlwaysOn` — legacy activation behavior until its relationship with current
  Activate/Stop is intentionally redesigned.
- `[Gate] iManualToggleKey` — diagnostic key used only with Manual signal.
- `[Injection] bRollEnabled` — obsolete/redundant beside binding/profile behavior unless
  runtime evidence establishes a separate need.
- `[Aim] bMirrorFlightToAim` — current implementation-owned invariant, not a user choice.
- `[Normalization] bUnipolarMode` and `bReverseAxisEnabled` — expose through behavior
  choices and reverse strategy, not raw boolean labels.
- `[ShipButtonOutputs]` — legacy/manual compatibility data; show resolved values in
  diagnostics where relevant rather than a normal editor.

Unknown and future custom keys must continue to follow the provider's preservation and
schema-version rules. Control must not parse or rewrite the INI itself.

## 20. UX and product decisions that still deserve intervention

These are not blockers to drawing the main page system, but they should be answered
before their affected controls ship as editable.

1. **Master-state vocabulary.** Define user-facing distinctions among product enabled,
   flight injection enabled, Activate, Stop, outside-pilot parking and the keyboard panic
   kill. Recommendation: treat injection as profile behavior, parking as automatic
   context, and panic kill as a persistent safety action; avoid multiple generic Enabled
   switches.
2. **Legacy menu access during migration.** Decide whether Toggle Wizard becomes Open
   Absolute Control when available and retains a separate fallback chord, or whether the
   legacy workbench remains independently bindable until parity acceptance.
3. **Essential actions on Overview.** Approve the small readiness subset, or use only a
   neutral “N of 23 bound” status. Do not let engineering invent a required play style.
4. **Advanced runtime switches.** Confirm whether native controls and named ship-button
   master switches remain supported user choices or become diagnostics/support tools.
5. **Profile destructive operations.** Specify rename collision, delete, active-profile,
   backup and detach/materialize behavior before enabling those actions.
6. **Macro editing pattern.** Choose the Control host's selected-record/list-detail
   treatment for ordered steps and chords; scalar rows alone cannot represent this well.
7. **Axis icon deliverable.** Decide whether UX supplies SVGs, host vector path data or a
   documented draw primitive family. The current code is a useful reference, not a
   distributable asset set.
8. **Cross-module status placement.** Decide whether lane-owner/arbitration status appears
   only in Plugin & Compatibility, also on Overview, or in a suite-level Control page.
   Recommendation: concise Overview state plus detailed diagnostics.
9. **AbsoluteZero terminology.** Approve a public name for the migrated “alignment
   assist” behavior. “Idle mouse centering” is more literal and avoids implying aim
   correction.
10. **Head Tracking source controls.** Decide whether source selection and stale timeout
    are supported settings or expert diagnostics in the separate module.

Ownership, module separation, binary reverse scope, throttle logical coordinates,
Binding Layer sparse semantics and Direct/Context/Keyboard terminology are already
closed decisions and do not need to be reopened for stylistic work.

## 21. Requested UX/design outputs

The UX handoff should produce:

- an information-architecture pass for the nine HOTAS pages within one Control module;
- wireframes for Setup Overview, Flight Axes, Ship Buttons, Throttle Setup, Aiming &
  Combat and the four Advanced pages, including realistic long-content states;
- a shared axis card and live-range specification with normal, unbound, inactive,
  mouse-owned, stale, disconnected and capture-active states;
- the six-axis vector icon family or an explicit host-native construction spec;
- DirectInput capture, dirty-target switch, calibration, recipe-overwrite and destructive
  profile-operation modal flows;
- binding-row states for Direct, Context and Keyboard compatibility, including waiting,
  unavailable-build and inherited/overridden variants;
- list/detail patterns for profiles, macros and devices that do not depend on generating
  one host control for every possible record;
- keyboard, mouse and controller focus order for the major flows;
- responsive behavior at the supported Control menu sizes; and
- companion module sketches for the Head Tracking Camera Look replacement and
  AbsoluteZero idle-centering page so extraction does not feel like feature removal.

Mocks should annotate **STD/HUX/HABI/CAP/LIVE/PROV/RT/SUITE** dependencies where a
visual state cannot function on the current host contract. A design can still show the
intended end state; the annotation prevents it from being mistaken for already available
infrastructure.

## 22. Full-parity acceptance checklist

The Absolute Control migration is feature-complete only when the following have retained
evidence:

- Setup Overview identifies devices, axes, throttle, actions, profiles and compatibility.
- Six core axes bind, clear, invert, tune, render live state, save, reload and execute.
- All reverse and digital fallback strategies remain usable.
- HOSAM parks pitch/yaw without losing them; independent aim continues separately.
- Every one of the 23 named ship actions captures and reports its truthful method/state.
- Menu reuse neutral-arming and threshold hysteresis prevent carried input.
- All four Flight Assist commands and custom keyboard/mouse shortcuts remain usable.
- Positional and rate throttle recipes round-trip without duplicating runtime math.
- Inverted/calibrated throttle landmark capture lands under the visible live marker.
- Analog and digital aim, aim toggle, smoothing and aim-driven steering retain behavior.
- Sparse profile inheritance, revert-to-Primary, activation and dirty switching are safe.
- Import/export/reset preserve backups, profile routing and user-owned data.
- Macros preserve chords, order, taps, holds, repeats, gaps, turbo and incomplete drafts.
- Device enumeration, duplicate reassignment and calibration cover every binding family.
- Capture cancels safely on every route/profile/device/host/menu teardown path.
- Opening/editing never leaks gameplay input; close reseeds held edges.
- Control absent, incompatible or rejected leaves standalone HOTAS operational.
- HOTAS + Control + Power coexist without page or transaction collisions.
- Camera Look has an accepted Absolute Head Tracking replacement before legacy removal.
- Alignment assist has an accepted AbsoluteZero replacement before legacy removal.
- Optional coordinator absence remains safe and visible, not a menu-host failure.
- No page depends on color alone, unstable device indices or raw implementation jargon.
- No second config/profile model or concurrent authoritative frontend exists.

Only after this matrix passes should removal or default-disablement of the embedded ImGui
workbench be considered as a separate release decision.

# AbsoluteHOTAS ImGui interaction parity contract

> **Status:** Audit gate. The current Absolute Control surface is an experimental
> first-pass scaffold, not a feature-complete replacement for the Dear ImGui
> workbench.
>
> **Rule:** A persisted value appearing in Absolute Control is not parity. Parity
> requires the same user task, state transitions, feedback, safety behavior, and
> runtime consequence to be possible with pointer, keyboard, and controller.

This document is the implementation authority for the next pass. It inventories
the in-scope Dear ImGui interaction models and maps them to an existing Absolute
Control primitive or a host capability that must be implemented first.

## Audited source baseline

The behavioral reference is AbsoluteHOTAS branch `5.0.0-beta` at
`cf94cc3d962e51c189952cfad41ac30eff87da31`, read from the committed sources rather
than the subsequently modified native-menu working tree.

| Source | Interaction authority |
|---|---|
| `src/BindingWizard.cpp` | shell, navigation, capture/close modals, scrolling body, fixed footer, and save/discard behavior |
| `src/WizardFlightAxesPage.cpp` | flight summary, jump navigator, axis cards/icons, graphs, tuning, throttle recipes, reverse strategies, and digital fallbacks |
| `src/WizardBindingsPage.cpp` | ship action groups/routes, menu reuse, flight assists, and shortcut repeater |
| `src/WizardTunePages.cpp` | aiming, excluded camera/head page, and rate-throttle/turn-assist page |
| `src/WizardProfileUI.cpp` | persistent edit context, dirty switching, activation, layer creation, import/export/reset |
| `src/WizardAdvancedPages.cpp` | macro editor, plugin controls, device manifest, and calibration workflow |
| `src/WizardCapture.cpp` | axis/button/POV/selector capture timing and edge policy |
| `src/WizardSession.cpp` | route/profile-bound transient state and cancellation |
| `src/WizardConfigCodec.cpp`, `src/WizardProfiles.cpp` | persistence, sparse overlays, atomic writes, backups, import/export/reset |
| `src/ThrottleController.cpp`, `src/AimController.cpp`, `src/ShipOutput.cpp`, `src/MacroEngine.cpp` | runtime meaning and precedence behind the UI |

The committed Power route and Camera Look/HOSAM editors are classified only to
prove their removal from HOTAS. Their controls are not parity targets.

Power, Head Tracking/camera pose, and AbsoluteZero mouse steering/HOSAM/alignment
remain outside AbsoluteHOTAS configuration ownership. Read-only ownership and lane
status are allowed; duplicate editors are not.

## Parity record format

Every element is reviewed as six separate concerns:

1. **Presentation** — what the pilot sees and how related values are grouped.
2. **Interaction** — pointer, keyboard, controller, capture, modal, or gesture flow.
3. **Draft state** — values and transient workflow state changed before Apply.
4. **Persistence** — owning file/section/profile and sparse-inheritance rules.
5. **Runtime effect** — the controller/output behavior that consumes the result.
6. **Feedback** — live value, availability, validation, conflict, stale, or failure state.

An implementation is not accepted if any of these are replaced with prose or a
generic status row when the workbench provides an operable interaction.

## Absolute Control primitive assessment

| Primitive/capability | Current support | Parity conclusion |
|---|---|---|
| Typed toggle, integer/float slider, choice, text, action | Implemented | Suitable for atomic values, not by itself for a compound task. |
| Group header and adjacent action layout | Implemented | Useful sectioning; cannot express a mixed binding/value/status row or axis card. |
| Page Apply/Cancel and dirty close | Implemented | Suitable if every page action joins the same pinned transaction. |
| Provider button/POV/axis capture and conflict resolution | Implemented | Capture policy can match HOTAS, but capture presentation still needs page context and adjacent Clear/Rebind affordances. |
| Action confirmation | Implemented | Suitable for destructive one-step operations. |
| Record collection/list-detail | Implemented | Suitable for selecting one record; insufficient for a continuously visible table/repeater or deeply nested ordered editor. |
| Page-open request/deep link | Implemented | Suitable for jump actions, subject to focus restoration tests. |
| Range meter and telemetry plot | Implemented first pass | Suitable for display only. Slider/marker association, direct manipulation, compact card layout, and accessibility remain unproven. |
| Conditional availability | Partial | A callback can reject/disable a control, but there is no declarative conditional subgroup with a visible reason. |
| Read-only status with severity | Missing | Current read-only `InputBinding` workaround cannot truthfully encode normal/waiting/warning/error semantics. |
| Compound/card layout | Missing | Required for the six axis cards, throttle recipes, and calibration axis rows. |
| Mixed row/table/repeater | Missing | Required for bindings with inline actions/status, shortcuts, device manifests, and stored calibration ranges. |
| Page anchor/jump navigator | Missing | Required for six-axis navigation and long-page discoverability. |
| Cross-page pinned context | Missing | Required for the always-visible edit-profile target and dirty indicator. |
| Ordered nested collection editor | Missing | Required for macros: macro -> ordered steps -> chord targets without serial popup hopping. |
| Provider workflow/modal | Missing | Required for create-and-capture, dirty profile switch, and calibration Begin/Sweep/Commit/Cancel flows. |
| Progress/session component | Missing | Required for capture countdown and eight-axis calibration extrema/progress. |
| Field source/override indicator | Missing | Needed to make sparse profile editing understandable even when explicit per-field revert remains out of scope. |
| Live-control association beyond grid Choice | Missing | Required to keep graph markers, sliders, values, and capture actions visibly and semantically connected. |

## Workbench shell and editing session

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Pause/gameplay warning | Explains that editing parks gameplay output. Runtime parks output while either frontend is open and reseeds held edges on resume. | Persistent shell banner driven by actual editor/runtime state. | Partial: runtime gate exists; presentation is generic. |
| Primary/secondary navigation | Task hierarchy plus a single scroll body; route changes cancel transient capture/calibration gestures. | Stable task hierarchy, page focus restoration, and provider cancellation on every route change. | Partial. |
| Profile context bar | Always-visible Main/profile selector, dirty state, and Add Binding Layer action across non-Power pages. | Host-owned pinned module context region, not a Profiles-page-only record popup. | Missing host primitive. |
| Fixed footer | Save & Apply, Save & Close, Close Without Saving, status, and dirty state remain visible while content scrolls. | Existing footer must expose provider result and exact close outcome consistently. | Partial; needs journey testing. |
| Dirty close modal | Save/Discard/Cancel, with failure retaining the draft. | Host modal with provider error, focus restoration, and no silent close. | Implemented generically; runtime proof pending. |
| Dirty profile switch modal | Save and switch / Discard and switch / Cancel for a different edit target. | Provider workflow modal distinct from closing the page. | Worked around with actions; missing workflow primitive. |
| Capture modal | Target label, settle/timeout feedback, Cancel; capture is route/profile/generation bound. | Host capture modal with phase/countdown/detail and automatic cancellation on route/profile/source change. | Partial. |
| Single-editor arbitration | Embedded workbench and Control cannot own authoritative drafts simultaneously. | Visible owner/read-only reason and stale-revision recovery. | Backend implemented; presentation/testing incomplete. |

## Flight Axes

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Readiness summary | Shows active/required axes and master Flight Controls Enabled. | Compact summary card with live completeness and ownership. | Missing compound summary. |
| Six-axis jump navigator | Throttle, Pitch, Yaw, Roll, Lateral Strafe, Vertical Strafe buttons scroll to cards. | Anchor navigator with controller focus and scroll restoration. | Missing. |
| Axis pictograms | Each navigator/card uses a semantic ship-motion icon so pitch/yaw/roll/strafe remain recognizable without reading repeated labels. | Host-owned semantic icon role or accessible compact diagram with text alternative. | Missing. |
| Axis card | Binding display, Bind/Rebind/Clear, invert, semantic path/status, live meter, graph, and tuning stay together. | Reusable `AxisCard` compound primitive; splitting these into unrelated rows is not parity. | Missing. |
| Bipolar graph | Shows center, deadzone, saturation, calibrated/inverted live input and shaped output. | Range meter with bands/markers and explicit associated tuning controls in the same card. | Display exists; association/layout missing. |
| Throttle graph | Shows inactive/active regions, idle, detent, reverse, boost, calibrated/inverted raw position, and shaped response. | Specialized range recipe card with marker labels and unavailable/stale state. | First-pass display exists; task layout incomplete. |
| Exact tuning matrix | Invert/saturation/deadzone for six axes; sensitivity for five axes; vertical strafe visibly shares lateral sensitivity. | Shared-value/source annotation rather than duplicating or silently omitting vertical sensitivity. | Values exist; relationship presentation missing. |
| Control-path ownership | Explains injected flight, digital fallback, dedicated reverse, and external mouse-steering lane ownership. | Severity/status primitive adjacent to the affected card. | Text/status workaround only. |
| Reverse strategies | Primary low-end zone, held digital reverse, and dedicated analog reverse, including suppression/precedence. | One compound strategy section with current effective authority and why another source is suppressed. | Controls exist; relationship feedback missing. |
| Digital fallback cluster | Roll L/R and strafe L/R/U/D bindings plus roll/strafe strengths. | Mixed binding grid grouped by output axis. | Flat rows; table primitive missing. |
| Rate-throttle summary/deep link | One shared master field, summarized on Flight Axes and edited on Throttle. | Read-only summary plus page-open action; never a second draft field. | Implemented first pass; UX proof pending. |

## Throttle Setup

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Positional recipe | Idle plateau, linked/symmetric deadzone convenience, cruise detent, optional reverse and boost regions. | Recipe card whose graph and controls redraw as one task. | Flat rows plus actions; compound layout missing. |
| Capture landmark | Reads the current primary-throttle position into detent/reverse/boost draft values with inversion/calibration semantics. | Inline Capture Current action beside each marker and visible captured result. | Backend action exists; inline association missing. |
| Symmetric link operation | Presentation-only operation links saturation to idle; not a durable boolean. | Explicit one-shot action with before/after preview, never a toggle. | Backend action exists; preview/layout missing. |
| Reverse truthfulness | Runtime hidden deadzone/activation threshold affects dedicated reverse engagement. | Expose it or publish normalized engagement telemetry so the visual never claims zero deadzone. | Product decision still open. |
| Rate throttle | Enable, ramp, decay, reverse velocity gate. | Conditional group enabled by mode, with units and current accumulator response plot. | Values/plot exist; conditional grouping missing. |
| Pilot Turn Assist | Enable; Always/Hold/Toggle; binding only meaningful for Hold/Toggle. | Conditional binding row with reason, capture, Clear, and current activation state. | Flat controls; conditional row missing. |

## Ship Buttons

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Action groups | 23 actions grouped as Weapons, Flight Systems, Camera, Navigation/Context, Cockpit/Docking. | Section/card grouping that keeps binding and route together. | Bindings/status exist but are long flat lists. |
| Binding row | Label, current binding, Bind/Rebind/Clear, colored unbound state. | Mixed row primitive with capture actions and accessible state text. | Generic binding control is less expressive. |
| Route summary | Direct/Context/Keyboard compatibility, resolved output/source, waiting/build/context availability. | Status severity and route detail immediately beneath or beside its binding. | Data exists; layout/status primitive missing. |
| Boost authority | Checkbox nested under Fire Boosters, because it modifies that action's throttle behavior. | Associated control within the Fire Boosters card. | Currently separated. |
| Menu-control reuse | Collapsible explanation; pitch/yaw/primary toggles; conditional invert and engage/release sliders; hysteresis invariant. | Conditional compound section with neutral-arming explanation and validation feedback. | Values exist; compound conditional UX missing. |
| Four flight assists | Native throttle commands with binding rows. | Small grouped binding grid. | Flat rows. |
| Shortcut repeater | Visible add/remove rows with trigger, raw output selector, Bind/Clear/Remove and empty state. | Bounded repeater/table; record popup alone is not parity. | Missing host primitive. |
| Menu preset | Adds seven incomplete/explicit shortcut rows in one draft operation. | Draft action followed by visible inserted rows and focus on the first incomplete row. | Backend exists; visible result/focus missing. |
| Macro deep link | Moves directly to macro editor. | Page-open request with deterministic destination focus. | Page open exists; destination focus proof pending. |

## Aiming & Combat

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Effective mode summary | Aim-Driven Steering versus Independent Aim & Steer is emergent from analog or directional-digital inputs and runtime toggle. | Provider-computed status, never UI inference from analog bindings alone. | Backend policy fixed; status presentation incomplete. |
| Analog aim pair | Optional yaw/pitch bindings with invert and per-axis sensitivity. | Two compact axis cards or paired rows with live aim input/output. | Values/telemetry exist; compound layout missing. |
| Smoothing | Bounded smoothing with live response consequence. | Slider associated with aim response plot. | Association missing. |
| Digital five-way | Left/right/up/down/center bindings plus shared speed. | Directional binding pad/grid with center semantics. | Flat rows. |
| Aim-mode toggle | Button switches independent/aim-driven only when separate aim input exists. | Conditional binding row plus effective runtime state and unavailable reason. | Binding exists; conditional/status UX missing. |

## Profiles & Binding Layers

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Main/profile edit target | Persistent context across all HOTAS editing pages. | Pinned context region with dirty and inheritance summary. | Missing host primitive. |
| Create and bind layer | Name + While Held/Toggle, create sparse overlay, select it, immediately capture modifier. | One provider workflow spanning text, choice, repository mutation, selection, and capture with rollback. | Split into independent actions; not parity. |
| Activation editor | Controller/selector trigger; base modes differ from overlay modes; keyboard shortcut shown read-only. | Conditional mixed row with capture timing appropriate to mode. | Backend exists; presentation fragmented. |
| Sparse inheritance | Effective state is base + overlay; saves only differences. | Selected profile and at least page/section-level inherited-vs-overridden feedback. | Persistence exists; source visibility missing. |
| Repository management | Add overlay, export base, select/import full profile, reset with confirmation/backup; show kind/file/slot. | Record selector plus visible detail panel and consequence-bearing actions. | Record popup exists; continuous detail/task layout weak. |

## Macros

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Macro list/editor | Add/delete/rename, selected macro, trigger capture, Turbo. | Master/detail layout with list visible while editing. | Popup record selector; not parity. |
| Ordered steps | Multiple visible steps with add/delete/reorder. | Ordered repeater with stable focus after mutation. | Popup step selection plus actions; not parity. |
| Chord targets | Multiple named/raw targets within one step; add/remove inline. | Nested bounded repeater inside the selected step. | Serial record popup; not parity. |
| Tap/Hold polymorphism | Tap amount means repeat count; Hold amount means duration; gap remains milliseconds. | Conditional label, bounds, units, and validation. | Generic integer row; conditional semantics weak. |
| Incomplete drafts | Unbound/no-step macros remain representable; Apply validates only required durable constraints. | Visible incomplete state without silently removing records. | Backend supports it; status/severity UX incomplete. |
| Power preset exclusion | Generic macro editor remains; Grav -> Shields preset is absent. | No Power-specific affordance. | Correct. |

## Plugin Controls & Compatibility

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Activate/Stop/Open | Three HOTAS bindings; Open routes to Control when supported and legacy UI otherwise. | Binding group plus current command availability. | Backend exists; grouped presentation missing. |
| Automatic pilot context | Off / park flight only / park all; detection toggle; latch duration. | Compact policy card with current detected context and parked scope. | Scalar rows plus diagnostics; relationship weak. |
| Runtime diagnostics | Exact-gate, controller, frontend, paths, and ownership status. | Dedicated status primitive with severity and copyable details. | Status workaround; primitive missing. |

## Devices & Calibration

| Element | Dear ImGui behavior and controlled state | Required Control behavior | Status |
|---|---|---|---|
| Device manifest | All devices visible with index, product, VID/PID, instance, axes/buttons and empty state. | Table/list with stable identity and selectable rows. | Record popup; not parity. |
| Duplicate reassignment | Reassign duplicate identities across every HOTAS-owned binding family and calibration, without assuming adjacency. | Explicit source/destination device workflow, preview count, confirmation, and reconnect guidance. | Backend improved; UI workflow insufficient. |
| Calibration session | Begin, ask user to sweep all controls, track eight extrema live, accept only ranges >5000, Commit/Cancel. | Multi-step workflow/modal with eight progress rows and live extrema. | Backend exists; host workflow/progress missing. |
| Stored ranges | Show each saved axis range and Clear All. | Visible calibration table with confirmed clear action. | Record/status workaround. |
| Hot-plug/device loss | Capture/calibration cancels or becomes unavailable without stale commit. | Explicit stale/disconnected state and recovery action. | Backend paths exist; runtime UX proof pending. |

## Host work required before the next page port

The following are host features, not HOTAS-specific Scaleform code:

1. `StatusText` with normal/waiting/warning/error severity and copyable detail.
2. `CompoundGroup/Card` with explicit child-control associations and compatible flat fallback.
3. `MixedRow/Table/Repeater` for binding/action/status rows and dynamic visible records.
4. `AnchorNavigator` for long task pages.
5. `PinnedContext` for module-wide edit target and dirty/source state.
6. `ConditionalGroup` with provider-supplied visible/enabled state and reason.
7. `WorkflowSession` for multi-step provider flows, including capture handoff and
   Commit/Cancel.
8. `ProgressRows` for calibration/capture extrema and completion state.
9. `OrderedCollectionEditor` for macro steps and nested chord targets.
10. General live-component/control associations for sliders, actions, and markers,
    not only Choice controls in the Power grid.

Each must be renderer-neutral, bounded, size-gated, keyboard/controller complete,
and usable by other Absolute modules. No capability should be added solely as an
`absolute.hotas` rendering exception.

## Tab-by-tab rebuild order and gates

No page advances merely because its descriptors register.

1. **Flight Axes** — proves cards, anchors, mixed binding rows, live range
   association, capture, shared-value annotation, and ownership status.
2. **Ship Buttons** — proves grouped repeated rows, per-action route status,
   conditional menu reuse, and visible shortcut repeater.
3. **Throttle Setup** — proves recipe cards, live marker association, current-position
   capture, conditional rate mode, and truthful reverse engagement.
4. **Aiming & Combat** — proves emergent mode status, paired axis controls, digital
   direction layout, and live smoothing association.
5. **Profiles & Layers** — proves pinned edit context, dirty switching, sparse source
   feedback, and create-then-capture workflow.
6. **Macros** — proves master/detail, ordered steps, nested chords, conditional
   tap/hold semantics, and incomplete drafts.
7. **Devices & Calibration** — proves manifest table, explicit reassignment workflow,
   eight-axis progress, Commit/Cancel, hot-plug, and stored ranges.
8. **Plugin Controls, Setup, Diagnostics** — proves policy cards, status severity,
   page-open requests, ownership reporting, and support details.

### Page acceptance evidence

Every page requires:

- a source-level element map back to the Dear ImGui implementation;
- contract tests for state, validation, failure, cancellation, and capability fallback;
- a pointer journey, keyboard-only journey, and controller-only journey;
- Apply, Cancel, dirty close, reopen/read-back, and stale-revision proof;
- capture/hot-plug/focus-loss proof where relevant;
- screenshots or video showing the complete task, not only registered rows; and
- explicit confirmation that Power, Head Tracking, and AbsoluteZero configuration
  cannot be edited or overwritten by HOTAS.

Until this matrix is complete, the Dear ImGui workbench remains the behavioral
reference and the Absolute Control HOTAS pages remain experimental.

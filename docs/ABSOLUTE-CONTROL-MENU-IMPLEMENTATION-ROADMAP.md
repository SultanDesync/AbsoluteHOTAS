# AbsoluteHOTAS menu implementation roadmap

> **Status:** First-pass scaffold; superseded as a parity authority by
> [ABSOLUTE-HOTAS-IMGUI-INTERACTION-PARITY-CONTRACT.md](ABSOLUTE-HOTAS-IMGUI-INTERACTION-PARITY-CONTRACT.md)
> **Target host:** Absolute Control (`AbsoluteControlPanel.dll`)
> **Provider module:** `absolute.hotas`
> **Parity authority:** [ABSOLUTE-CONTROL-HOTAS-FEATURE-INVENTORY.md](ABSOLUTE-CONTROL-HOTAS-FEATURE-INVENTORY.md)

The milestones below produced useful provider, capture, transaction, dynamic-record,
and telemetry infrastructure. They did **not** establish interaction parity with the
Dear ImGui workbench. Registered fields and automated contract tests are scaffolding;
page acceptance now follows the element-level behavior and runtime UX gates in the
interaction parity contract.

## Product boundary

AbsoluteHOTAS owns flight axes, throttle behavior, ship buttons, aiming, profiles,
macros, device discovery, and calibration. The native menu must not recreate:

- Absolute Power settings or allocation editing;
- OpenTrack, camera-pose, recenter, or head-tracking settings; or
- AbsoluteZero mouse-steering/HOSAM enablement, alignment, idle-centering, radius,
  delay, or decay settings.

HOTAS may report the effective owner of a shared flight lane and link to a sibling
module, but it does not edit that sibling's configuration.

The target is the native Scaleform Absolute Control host. Absolute Workbench is a
historical ImGui prototype, not a second target implementation.

## Stable page contract

The provider registers one module with these page IDs. IDs are permanent even while a
page is being delivered in phases.

| Page | Stable ID | Provider responsibility |
|---|---|---|
| Setup Overview | `hotas-setup` | readiness, ownership, and resolving links |
| Flight Axes | `hotas-flight-axes` | six flight axes, reverse, digital fallbacks, steering-owner status |
| Ship Buttons | `hotas-ship-buttons` | named actions, menu reuse, flight assists, shortcuts |
| Throttle Setup | `hotas-throttle` | positional/rate recipes, landmarks, turn assist |
| Aiming & Combat | `hotas-aiming` | aim routing, analog aim, digital aim |
| Profiles & Layers | `hotas-profiles` | edit target, sparse ownership, activation, repository actions |
| Macros | `hotas-macros` | triggers, ordered steps, chords, holds, repetition, turbo |
| Devices & Calibration | `hotas-devices` | identity, live state, reassignment, calibration |
| Plugin & Compatibility | `hotas-diagnostics` | runtime gates, frontends, paths, coordination, support state |

## Delivery sequence

### M0 — SDK candidate and provider foundation

- Snapshot a numbered Absolute Control SDK contract into AbsoluteHOTAS with provenance.
- Preserve compatibility with hosts that expose only the committed ABI-v1 base.
- Split the current six-scalar proof into renderer-neutral descriptor, session,
  capture, live-state, and repository seams.
- Register all nine stable pages without claiming unavailable behavior is complete.
- Keep AbsoluteHOTAS operational when Control is absent, old, rejected, or unloaded.

Gate: ABI layout tests, absence/incompatibility tests, descriptor validation, and the
existing HOTAS suite all pass.

### M1 — Complete transactional settings model

- Represent every HOTAS-owned scalar and static binding in one provider draft.
- Load shipped defaults plus user custom settings and the selected sparse profile.
- Apply by validating, atomically writing, reloading through the normal runtime path,
  and performing semantic read-back.
- Cancel restores the saved snapshot; stale sources and failures retain the draft.
- Enforce one authoritative editor across Control and the embedded workbench.

Gate: field-by-field mapping, wrong-kind/range rejection, Apply, Cancel, stale source,
write failure, reload failure, and read-back mismatch are mechanically tested.

### M2 — DirectInput capture and profile context

- Adapt the existing HOTAS axis/button/POV/selector capture engine to Control's
  provider-capture callbacks.
- Bind a capture to module, page, control, profile identity, and draft generation.
- Add Main/profile edit-target switching and explicit inherited/overridden state.
- Implement removal semantics for **Use Primary Binding** rather than copying a value.

Gate: settle windows, held-input behavior, timeout, cancellation routes, device loss,
conflict reassignment, dirty profile switching, and held-edge reseeding pass.

### M3 — Flight Axes and Throttle Setup

- Deliver the six core axes, reverse strategies, and digital fallbacks. Report native
  mouse steering as externally owned state; do not expose its enablement or tuning.
- Deliver positional/rate throttle recipes, landmark capture, boost/reverse regions,
  turn assist, and truthfully described current runtime behavior.
- Productize bounded range/plot components in the host; callbacks copy prepared state
  and never perform disk I/O, device enumeration, or gameplay traversal.

Gate: bind/clear/tune/apply/read-back/runtime behavior is proven for every axis;
inversion-aware landmark capture and all recipe transitions are tested.

### M4 — Ship Buttons and Aiming & Combat

- Deliver all named ship actions and their Direct/Context/Keyboard method state.
- Deliver flight-assist commands, menu-control reuse, and custom shortcuts.
- Deliver direct versus aim-driven steering, independent analog aim, digital aim,
  smoothing, and the runtime aim-mode toggle.

Gate: all named actions round-trip; unavailable direct routes never silently fall back;
menu hysteresis and unknown valid shortcut preservation pass; independent HOTAS aim
continues while the external mouse-steering module owns pitch/yaw.

### M5 — Profiles, macros, devices, and calibration

- Use bounded selected-record/list-detail contracts instead of one scalar descriptor
  per dynamic record.
- Deliver create/import/export/reset profile flows with confirmation and backup rules.
- Deliver lossless macro editing, device identity/reassignment, and transactional
  calibration.

Gate: macro ordering and incomplete drafts survive; all binding families participate in
device reassignment; calibration Commit/Cancel and duplicate-name reconnect scenarios
pass.

### M6 — Qualification and frontend transition

- Exercise pointer, keyboard-only, and controller-only journeys.
- Exercise HOTAS alone; HOTAS with Control; and Control with Power, Head Tracking, and
  AbsoluteZero independently and together.
- Exercise dirty/capture-active close, disconnect/reconnect, hot-plug, alt-tab,
  repeated reopen, supported display shapes, and host loss/incompatibility.
- Keep the embedded Dear ImGui workbench as a fallback until this matrix is accepted.

Gate: `xmake`, `xmake test`, Absolute Control's `validate-current.cmd`, and the recorded
in-game matrix pass. Automated tests do not substitute for runtime or UX evidence.

## Definition of feature complete

Feature complete means every in-scope row in the parity inventory is either:

1. editable and persisted through its owning HOTAS service;
2. represented as truthful live/status information with a tested unavailable state; or
3. explicitly removed from HOTAS because it belongs to Power, Head Tracking, or
   AbsoluteZero.

A visible placeholder, an ImGui-only workflow, or a setting that writes without normal
runtime reload and semantic read-back does not satisfy parity.

## Migration correctness rules

The native provider must preserve intended behavior, not accidental ImGui coupling:

- Resolve the current mismatch where Boost Zone can be enabled in the editor but the
  runtime asserts it only when reverse-zone mode is also enabled.
- Make dedicated-reverse deadzone and activation threshold truthful in either exposed
  controls or provider-normalized telemetry; do not render a false zero-deadzone graph.
- Derive independent-aim state from analog **or digital** aim input, matching runtime.
- Treat symmetrical throttle deadzones as an explicit draft operation or durable key;
  do not copy the current non-round-tripping render-time mutation.
- Keep the duplicated rate-throttle summary and editor bound to one draft field.
- Reassign duplicate devices by stable identity across the complete HOTAS binding graph,
  not by assuming equal product names are adjacent enumeration indices.
- Stop legacy HOTAS saves from rewriting Head Tracking, HOSAM, or alignment settings once
  those configurations are owned by standalone modules.

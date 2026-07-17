# Binding Wizard Workbench Architecture

## Product model

The Binding Wizard is an in-game configuration workbench, not a sequential
setup wizard. A user can enter at any task, edit either Main controls or a
profile, move between tasks, leave a draft unfinished, and return later.

The governing interaction is:

```text
open workbench -> select editing context -> modify a draft -> validate -> save and apply
```

Functionality defines state and behavior. State and behavior define the
architecture. The architecture defines information hierarchy. Visual styling
is applied only after those layers are stable.

## Functional scope

The workbench must preserve these capabilities:

- overlay access from keyboard or a bound controller button;
- gameplay/menu context detection and a clear gameplay mouse-lock warning;
- safe suppression of plugin-owned flight, button, macro, and profile-switch
  output while editing;
- complete keyboard navigation and predictable focus;
- one visible editing target: Main controls or a selected profile;
- dirty-state tracking, guarded navigation/close, validation, atomic save,
  runtime reload, and visible results;
- flight-axis binding, inversion, response tuning, calibration, and live input
  visualization;
- button, throttle-zone, and dedicated-axis reverse strategies with explicit
  precedence;
- positional and accumulator throttle, throttle zones, HOSAM, aim-driven and
  independent aiming, digital axes, digital aim, and flight assists;
- named Starfield actions, raw keyboard/mouse outputs, and plugin controls;
- macros with triggers, chords, taps, holds, repetition, gaps, ordering, turbo,
  and presets;
- device enumeration, live state, calibration, and complete duplicate-device
  reassignment;
- base, sparse-overlay, and full profiles with create/import/export/reset,
  backup/recovery, and momentary/toggle/selector activation.

## Architectural invariants

### One editing transaction

Ordinary configuration changes modify the current draft. Save & Apply is the
single commit point. Repository operations such as import, reset, profile
creation, and deletion are explicit commands with confirmation and recovery.

### One interaction session

The workbench session owns the current profile, navigation, editable draft,
saved snapshot, dirty state, pending capture, pending navigation, modal state,
and status result. Pages do not own competing copies of those concepts.

### One scroll owner

The shell owns fixed context, navigation, and footer regions. Only the page body
owns the main vertical scrollbar. Pages may use bounded local scrolling only
when the boundary is visually explicit.

### Safe editing

While the workbench is open, plugin-owned memory injection and synthetic output
are parked, macros and profile switching are suspended, and capture remains
available. Closing reseeds edges so held controls do not fire unexpectedly.

### Context-bound capture

A capture transaction records capture kind, editing profile, draft revision,
typed target field, start time, and progress. It is cancelled when its profile,
page context, or session becomes invalid. Numeric slot ranges are a legacy
implementation detail and must not remain the architectural routing mechanism.

### Render purity

Rendering reads session state and emits actions. It does not perform per-frame
file discovery, write configuration, reload runtime state, create profiles, or
infer commit targets from mutable vector positions.

## State separation

Persistent draft state contains only values that can be saved: bindings,
tuning, calibration, control modes, custom outputs, macros, and profile
activation metadata.

Session state contains navigation, expansion state, current profile, dirty
snapshot, pending navigation, dialogs, status, capture, and calibration
gestures.

Runtime telemetry is read-only: device availability and values, game menu state,
runtime active profile, capture progress, config generation, and overlay state.

## Target layers

```text
Platform
  OverlayHost (D3D12/ImGui, window input, cursor and resize lifecycle)
  GameContextReader

Application
  WorkbenchSession (draft, navigation, validation, capture, commands, status)

Domain
  ControlProfile, bindings, tuning, throttle, aim, macros, calibration, activation

Services
  ConfigRepository, ProfileRepository, RuntimeApplyService,
  CaptureService, DeviceService, BackupService

Presentation
  WorkbenchShell
    fixed context banner and profile identity
    primary navigation
    single scrollable page host
    fixed Save/status footer
    modal host
  Pages
    Bind Controls
    Tune
    Advanced
  Components
    responsive binding table, axis preview, section, choice group,
    capture control, status banner, confirmation dialog, empty/error states
```

Pages render the session and request actions. They do not write files, reload
configuration, or manage global navigation independently.

## Hierarchy of complexity

The persistent shell shows the editing profile, dirty state, game-context
warning, primary navigation, Save & Apply, status, and a close affordance.

Primary tasks expose flight bindings, ship actions, and tuning directly.
Optional capabilities reveal their subordinate controls only when selected or
enabled. Macros, profile management, device tools, plugin controls, and
diagnostics live in Advanced unless a contextual link brings the user there.

The style system then maps those roles to consistent spacing, typography,
responsive tables, action hierarchy, semantic status colors, visible keyboard
focus, truncation/tooltips, DPI-aware sizing, and confirmation-dialog behavior.
Color reinforces meaning but is never its only carrier.

## Acceptance criteria

- Exactly one main vertical scrollbar exists.
- Context and footer remain visible at 720p and supported DPI scales.
- Every workflow is completable without a mouse.
- Opening the workbench parks every plugin-owned gameplay output.
- Capture cannot commit into the wrong profile or field.
- Dirty profile switching and closing are guarded.
- Duplicate-device reassignment visits every binding-bearing field.
- Ordinary frame rendering performs no disk access.
- Save is atomic and reports validation at the relevant workflow.
- Renderer reinitialization restores cursor and input state.
- Main, sparse-overlay, and full-profile semantics remain unchanged.
- Every pre-refactor feature has an explicit parity test.

## Migration order

1. Establish the session, navigation, shell, and lifecycle contracts.
2. Establish shared responsive components.
3. Migrate Bind Controls, then Tune, then Advanced.
4. Route capture and persistence through typed session commands.
5. Validate keyboard, DPI, gameplay/menu, profile, capture, and renderer
   lifecycle behavior.
6. Remove the legacy monolithic orchestration only after parity is verified.

## Initial implementation boundary

The first workbench migration establishes:

- a `WizardSession` owner for route, status, dirty lifecycle, profile switching,
  close guarding, capture context, and activation drafts;
- a fixed shell with profile context, two-level navigation, one scrollable page
  host, and a persistent Save/status footer;
- responsive table-based binding, activation, custom-output, macro-action, and
  navigation components instead of fixed horizontal coordinates;
- modal, profile-and-route-bound capture;
- complete draft-only duplicate-device reassignment across bindings, macros,
  calibration, and profile activations;
- runtime suspension of plugin memory writes and synthetic output while the
  workbench is open, with edge reseeding on resume; and
- cursor/clip restoration during normal close, renderer failure, resize, and
  shutdown.

Remaining migration work is deliberately narrower: replace the numeric capture
slot adapter with typed field targets, move repository commands behind a batch
transaction where multiple activation records must commit together, finish
page-specific spacing/DPI polish, and run the in-game parity matrix across menu,
gameplay, renderer-resize, profile, and device scenarios.

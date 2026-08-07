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

While the workbench is open, plugin-owned flight injection and outputs are
parked, macros and profile switching are suspended, and capture remains
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
    Flight Controls
    Flight Modes
    Advanced
  Components
    responsive binding table, axis preview, section, choice group,
    capture control, status banner, confirmation dialog, empty/error states
```

Pages render the session and request actions. They do not write files, reload
configuration, or manage global navigation independently.

## Presentation module map

The implementation mirrors those presentation layers:

- `BindingWizard.cpp` owns the window shell, navigation, page host, footer,
  capture/close modals, and top-level route dispatch;
- `WizardUICommon.cpp` owns shared binding-row presentation and workbench UI
  logging;
- `WizardFlightAxesPage.cpp`, `WizardTunePages.cpp`,
  `WizardBindingsPage.cpp`, and `WizardAdvancedPages.cpp` own cohesive page
  families;
- `WizardProfileUI.cpp` owns profile context, profile-management presentation,
  and the legacy capture-slot commit adapter; and
- `WizardUI.h` is the internal presentation boundary consumed by the shell.

Page-local helpers remain private to their translation unit. Cross-page
workflow state belongs in `WizardSession`, not in presentation modules.

## Configuration module map

The wizard configuration implementation keeps its existing public
`WizardConfig` API while separating storage and repository concerns:

- `WizardConfig.cpp` owns the editable/base drafts, saved snapshot, runtime
  loading, profile-load orchestration, dirty detection, and save routing;
- `WizardConfigCodec.cpp` owns INI decoding/encoding, macro rows, collections,
  and deterministic state signatures;
- `WizardProfiles.cpp` owns atomic persistence, sparse overlays, activation
  slots, starter profiles, import/export, backup, and reset operations;
- `WizardBindingDisplay.cpp` owns user-facing binding labels; and
- `WizardConfigInternal.h` is the private collaboration boundary between those
  implementation modules.

Only `WizardConfig.h` is consumed outside this configuration subsystem.

## Overlay host module map

The platform overlay keeps its existing public `UIHook` API while separating
the renderer-sensitive implementation:

- `UIHook.cpp` owns private state definitions and the public install, shutdown,
  toggle, visibility, and callback lifecycle;
- `UIHookSwapChain.cpp` owns vtable discovery, prior-hook diagnostics,
  command-queue association, and DXGI/D3D12 interception;
- `UIHookRenderer.cpp` owns ImGui/D3D12 initialization, render targets, frame
  submission, resize handling, teardown, and exception recovery;
- `UIHookInput.cpp` owns the window procedure, hotkey message, input capture,
  and cursor restoration; and
- `UIHookInternal.h` is the private state and calling-convention contract shared
  by those implementation modules.

Only `UIHook.h` is consumed outside the overlay-host subsystem.

## Hierarchy of complexity

The persistent shell shows the editing profile, dirty state, game-context
warning, primary navigation, Save & Apply, status, and a close affordance.

The opening Flight Axes (Core) page treats direct flight control as the product,
not as configuration data. Thrust, rotation, and six-degree-of-freedom
translation are visually distinct groups. Each axis card keeps its binding,
inversion, response controls, calibration context, status, and live signal
together so configuring an axis never requires a second page. It also names the
control mode and explains its user-visible behavior. The page states plainly that
roll and strafe are independent controls and can be commanded simultaneously.

Flight Modes holds optional systems such as independent aiming, camera look, and
rate throttle. Camera Look owns OpenTrack enablement, optional per-component
joystick overrides, inversion, sensitivity, output limits, filtering, toggle,
and recenter bindings. Core switches remain linked from the axes they modify.
Optional capabilities reveal subordinate controls when selected or enabled. Macros,
profile management, device tools, plugin controls, and diagnostics live in
Advanced unless a contextual link brings the user there.

The style system then maps those roles to consistent spacing, typography,
responsive tables, action hierarchy, semantic status colors, visible keyboard
focus, truncation/tooltips, DPI-aware sizing, and confirmation-dialog behavior.
Color reinforces meaning but is never its only carrier.

## Acceptance criteria

- Exactly one main vertical scrollbar exists.
- Primary task tabs are visually distinct from their labeled subordinate routes.
- Every flight axis discloses its control mode and user-visible behavior.
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
3. Migrate Flight Controls, then Flight Modes, then Advanced.
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
- runtime suspension of plugin flight injection and outputs while the
  workbench is open, with edge reseeding on resume; and
- cursor/clip restoration during normal close, renderer failure, resize, and
  shutdown;
- first-open renderer initialization, session-latched renderer failure, and a
  startup bypass that leave manual configuration and flight controls active.

Remaining migration work is deliberately narrower: replace the numeric capture
slot adapter with typed field targets, move repository commands behind a batch
transaction where multiple activation records must commit together, finish
page-specific spacing/DPI polish, and run the in-game parity matrix across menu,
gameplay, renderer-resize, profile, and device scenarios.

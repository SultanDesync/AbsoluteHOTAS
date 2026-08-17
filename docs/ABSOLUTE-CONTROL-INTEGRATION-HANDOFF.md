# AbsoluteHOTAS -> Absolute Control integration handoff

> **Status:** H0 release-verified; ready for checkpoint commit
> **Date:** 2026-08-16
> **Immediate objective:** Add fail-optional, provider-owned Absolute Control pages to
> AbsoluteHOTAS without delaying or destabilizing its 5.0.1 gameplay update.
> **Release posture:** Absolute Power Presets and Diagnostics may ship alongside this work;
> its unfinished Automation / Cheats route is an explicitly unavailable **Coming Soon** preview.

## First-turn brief

Begin in the AbsoluteHOTAS repository. Read this document completely, then inspect the files and
contracts linked under [Required reading](#required-reading). Do not start by copying ImGui draw
code or by adding settings to Absolute Control itself.

The first implementation slice is:

1. introduce an `AbsoluteControlSubscriber` adapter inside AbsoluteHOTAS;
2. dynamically discover the product or ResearchDev Control host without linking to it;
3. register module `absolute.hotas` plus read-only **Setup Overview** and
   **Plugin & Compatibility** pages;
4. keep all flight controls, manual INI use, profiles, and the existing overlay functional when
   Control is absent or rejected; and
5. add ABI/descriptor/absence tests before exposing an editable setting.

Stop and document a blocker rather than inventing a synthetic key path, letting Control parse a
HOTAS INI, or moving DirectInput polling into the menu host.

## Product and release decision

AbsoluteHOTAS remains a standalone flight-control mod. Absolute Control is its preferred native
PauseMenu editor when present, not a runtime parent and not a new configuration owner. The embedded
ImGui workbench remains a supported transition/fallback until the Control route reaches the agreed
parity threshold. This lets the HOTAS runtime update continue while integration proceeds in bounded
vertical slices.

The early simultaneous release does **not** require:

- complete migration of every advanced HOTAS page;
- removal of the legacy overlay;
- public SDK freeze;
- release of Absolute Power automation; or
- a new gameplay dependency between HOTAS, Power, and Control.

It does require truthful capability labels. An unavailable editor or capture path must say so; a
partially exercised page is not “integrated” merely because it registered or rendered once.

## Suite modularization and optional runtime coordination

The Absolute product may ship an optional headless runtime coordinator alongside the Control menu,
but menu hosting and gameplay arbitration remain separate binaries and lifecycles. The intended
package boundary is:

```text
Absolute Control
  AbsoluteControlPanel.dll       optional native menu host
  AbsoluteControlPanelMenu.swf   menu presentation
  AbsoluteFlightRuntime.dll      optional headless flight-lane coordinator
```

`AbsoluteFlightRuntime.dll` is specified direction, not part of H0. Control must continue to work
without it, and HOTAS, Head Tracking, and AbsoluteZero must retain safe standalone behavior without
the menu. When the coordinator is absent, incompatible combinations fail closed instead of
installing competing hooks.

The target feature ownership is:

| Concern | Product owner |
|---|---|
| DirectInput devices, HOTAS/HOSAS axes, throttle, buttons, profiles, macros | AbsoluteHOTAS |
| Bound pitch/yaw steering and independent reticle/aim routing | AbsoluteHOTAS |
| HOSAM mode that releases pitch/yaw steering to Starfield's mouse path | AbsoluteHOTAS |
| OpenTrack input, pose shaping, camera composition, recenter/toggle | Absolute Head Tracking |
| Idle mouse-centering policy, radius, delay, decay, and suppression | AbsoluteZero |
| Shared lane arbitration when several flight modules are installed | Optional Absolute Flight Runtime |
| Module pages, transactions, capture presentation, and diagnostics | Absolute Control |

AbsoluteZero is a conditional modifier, not the owner of mouse input. It may center the source only
when runtime arbitration declares native mouse steering active, normally HOSAM or mouse-only flight.
Bound HOTAS pitch/yaw claims steering; independent aiming remains a separate HOTAS-managed lane.
Menu/capture suspension parks affected lanes. No raw Starfield pointer crosses a suite ABI.

Until extraction lands, the embedded HOTAS workbench and runtime still contain transitional Head
Tracking and alignment-assist code. H0 must not expose those as `absolute.hotas` Control settings.
Before H1 inventories editable settings, specify the coordinator ABI and a reviewed extraction/
migration path. Absolute Head Tracking and AbsoluteZero register their own modules and retain their
own configuration owners.

## Current repository truth

### AbsoluteHOTAS

- The gameplay plugin and manual/profile configuration are already standalone.
- The current editor is `BindingWizard` over a shared `WizardState`, `WizardSession`,
  `WizardConfig`, and `WizardCapture` implementation.
- Current routes are Flight Axes, Ship Buttons, Aiming, Camera Look, Rate Throttle, Macros,
  Plugin Controls, and Devices. The accepted redesign adds Setup Overview, behavior-first
  Throttle Setup, and Profiles & Layers.
- `AbsoluteHOTAS_QueryApi(1)` is the existing suite command-binding/capture contract used by
  other mods. It is not a Control provider API.
- `AbsoluteControlSubscriber` now discovers either Control product DLL name at SFSE
  post-data-load, registers copied read-only descriptors, retries a `NotReady` host once at
  post-post-data-load, and leaves standalone gameplay unchanged when the host is absent or rejects
  the provider.
- `xmake` builds and optionally deploys the DLL/default INI. `xmake test` currently exercises
  nine standalone test targets, including the Control ABI/subscriber contract suite.

### Absolute Control

- The product ABI authority is `include/AbsoluteControlPanelAPI.h`, ABI v1.
- The host copies module/page/control descriptors and invokes provider callbacks through leases.
- Standard controls, labeled choices, text entry, page transactions, keyboard capture, and the
  experimental live-component channel exist.
- Mouse/controller/HOTAS binding capture is not currently implemented.
- There is no public host command that opens Control directly at a requested module/page.
- Dirty page/module navigation is safely rejected today; the accepted Apply / Discard / Stay
  modal is still pending.
- The public SDK and live-component contract are not release-frozen.

### Working-tree caution

All three suite repositories contain broad, intentional uncommitted work from the Control and
Power phases. Inspect `git status` and focused diffs before editing. Do not discard, normalize, or
commit unrelated changes as part of this handoff.

## Required reading

Read these as authoritative/current unless the file itself says it is historical:

- [HOTAS UX overhaul](UX-OVERHAUL-HANDOFF.md)
- [HOTAS workbench architecture](reference/wizard-workbench-architecture.md)
- [HOTAS configuration layout](reference/config-layout.md)
- [HOTAS profile switching](reference/profile-switching.md)
- [Control current state](<../../Absolute-Control-Panel-Research/docs/CURRENT-STATE.md>)
- [Control provider API](<../../Absolute-Control-Panel-Research/docs/MODULE-API.md>)
- [Native menu contract](<../../Absolute-Control-Panel-Research/docs/NATIVE-MENU-CONTRACT.md>)
- [Scalability, transactions, and teardown](<../../Absolute-Control-Panel-Research/docs/SCALABILITY-TRANSACTIONS-AND-TEARDOWN.md>)
- [AI-assisted provider workflow](<../../Absolute-Control-Panel-Research/docs/AI-INTEGRATION-HARNESS.md>)
- [Control UI specification](<../../Absolute-Control-Panel-Research/docs/absolute_control_ui_specification.md>)
- [Power integration checkpoint](<../../Absolute Power/docs/CONTROL-PANEL-INTEGRATION.md>)
- [Headless subscriber contract](<../../Absolute Workbench/docs/HEADLESS-SUBSCRIBER-CONTRACT.md>)

Historical builder-v1 documents and the old ImGui Power frontend are evidence/reference only.

## Non-negotiable ownership boundaries

| Concern | Owner |
|---|---|
| DirectInput enumeration, sampling, device identity, and HOTAS capture | AbsoluteHOTAS |
| Gameplay injection, native ship actions, pilot gating, and held-edge reseeding | AbsoluteHOTAS |
| Defaults/custom/profile precedence and sparse profile semantics | AbsoluteHOTAS |
| Draft validation, persistence, reload, verification, and source ownership | AbsoluteHOTAS |
| Module/page navigation, focus, scroll, modal presentation, and generic Apply/Cancel | Absolute Control |
| Scaleform rendering, visual tokens, selected-control help, and device-aware prompts | Absolute Control |

Consequences:

- Control must never parse or rewrite an AbsoluteHOTAS INI.
- A provider callback submits bounded intent; it does not reach into host navigation or render
  state.
- Control absence, incompatibility, rendering failure, or unregistration must not stop HOTAS
  gameplay initialization.
- Do not create a second “Control config” or flatten sparse profiles into a parallel model.
- Never silently fall back from a failed native ship action to synthesized input.
- The existing overlay may remain, but two frontends must not hold independent authoritative
  drafts or save concurrently.

## Lessons from the Absolute Power integration

### Patterns to reuse

1. **Fail-optional discovery.** Probe both `AbsoluteControlPanel.dll` and the explicitly named
   local ResearchDev host after the HOTAS runtime is independently initialized. Validate ABI,
   `structSize`, callbacks, and capabilities. Retry only at a documented SFSE boundary.
2. **Provider-owned transaction.** Copy committed state into an opening snapshot and draft,
   remember a generation, validate on Apply, persist atomically through the existing owner,
   reload, verify semantic read-back, and return an actionable result. Cancel restores the
   opening state without writing.
3. **Constant-size selected-record pages.** Profiles, layers, devices, and macros are bounded
   libraries. Use one populated transient dropdown rather than one control per record or
   Previous/Next actions. Selection alone must not dirty the page.
4. **Explicit lifecycle actions.** New, Duplicate, Delete, Revert, Import, and similar actions
   carry `kControlMutatesDraft`; actions that must consume a saved draft use
   `kControlAppliesDraftBeforeInvoke`.
5. **One authoritative selector.** Do not repeat “Selected profile,” a selector showing the same
   profile, and a rename field with the same value. The selector identifies; the text field says
   `Rename selected ...`.
6. **Truthful lifecycle state.** Queue acceptance is not completion. Capture, save/reload,
   profile activation, and runtime reload each need observable waiting/success/failure states.
7. **Immutable live mailboxes.** Prepare telemetry away from the UI callback and publish bounded
   fixed-capacity frames. The visible route may poll; hidden routes do not traverse devices or
   gameplay state.
8. **Host transaction participation before provider mutation.** Compound edits and mutating
   actions must pin the ordinary transaction first, so close, failure, and unregister cannot
   strand provider state.
9. **Clean teardown.** Cancel capture on route change, close, host loss, profile change, and
   unregistration. Park gameplay injection while a capture/menu owns input, then reseed every held
   edge before resuming.
10. **Narrow evidence claims.** Registration, rendering, one successful edit, and one successful
    gameplay path are four different facts. Record them separately.

### Mistakes not to repeat

- Do not expose an internal policy engine merely because it is configurable. Power's general rule
  builder produced a complicated surface before the “on-demand power” user behavior was settled.
- Do not leak zero-based or implementation terminology into visible choices. Use Weapon 1/2/3,
  device names, and behavior-first language.
- Do not ship a control whose backend source is only partially implemented. Prefer a read-only
  **Coming Soon** route.
- Do not declare a whole feature qualified from the first happy-path record. Power's Weapon 1 test
  did not cover cross-weapon identity, short-demand settlement, or simultaneous use.
- Do not perform every release before every assignment when a resource pool already has safe
  headroom. More generally, encode the user's behavioral invariant rather than mistaking a safety
  implementation detail for product policy.
- Do not leave action rows as ambiguous clickable text. The Control shell uses explicit action
  treatment; descriptions state whether an operation edits a draft, saves, activates, or runs
  immediately.

## Target information architecture

Register one module, `absolute.hotas`, with stable page IDs. The visible arrangement follows the
accepted HOTAS UX handoff, but implementation is sliced by risk.

| Page | Suggested stable ID | First release posture |
|---|---|---|
| Setup Overview | `hotas-setup` | Read-only readiness plus links/status; first slice |
| Flight Axes | `hotas-flight-axes` | Core editing target |
| Ship Buttons | `hotas-ship-buttons` | Core editing target after provider capture exists |
| Throttle Setup | `hotas-throttle` | Behavior-first core target |
| Aiming & Combat | `hotas-aiming` | Later editable slice |
| Profiles & Layers | `hotas-profiles` | Selected-record transaction slice |
| Macros | `hotas-macros` | Advanced selected-record slice |
| Devices | `hotas-devices` | Read-only inventory first; calibration later |
| Plugin & Compatibility | `hotas-diagnostics` | Read-only first slice |

Camera Look is a transitional legacy-workbench route, not a target `absolute.hotas` Control page.
Absolute Head Tracking owns its replacement module/pages. Likewise, AbsoluteZero owns mouse-
centering settings; HOTAS retains only HOSAM routing and independent aiming behavior.

Do not make the sidebar contain Flight Controls, Flight Modes, and Advanced as if they were
separate installed mods. The sidebar entry is AbsoluteHOTAS; its pages/tabs express local tasks.

## Configuration adapter design

Do not pass `WizardState` across the ABI or compare its raw bytes. It contains strings, vectors,
maps, runtime-only calibration flags, and padding. Instead:

1. Extract or wrap a renderer-neutral HOTAS configuration service around `WizardConfig` and the
   existing codec.
2. Give the provider a bounded internal draft that can still use C++ containers because it never
   crosses the DLL boundary.
3. Expose only POD/string values through Control callbacks.
4. Track the edited base/profile identity and a semantic generation or source fingerprint.
5. Use the current full-base versus sparse-overlay writer; do not materialize inherited values
   into an overlay.
6. Preserve unrelated/unknown custom keys where the existing writer promises to do so.
7. Apply through the normal reload path and verify the effective runtime state.

Page Apply may persist the whole current edit target, but it must only contain intentional draft
changes. A transient profile selector must reject switching while the provider has a dirty draft
unless the host has first resolved Apply / Discard / Stay. Do not let a transient choice bypass
the current `WizardSession` close/profile-switch safety.

## Two host capabilities that must be designed before full parity

### Provider-owned DirectInput binding capture

Control v1 can capture keyboard input and reserves mouse/controller flags, but it cannot ask
AbsoluteHOTAS to capture a DirectInput button, POV direction, or axis. Moving DirectInput into the
host would violate ownership and make Control a gameplay dependency.

Add a size-gated optional provider-capture contract with these semantics before implementing HOTAS
binding pages:

- one shell-wide capture owner;
- begin, poll/read status, commit, and cancel operations;
- provider-formatted binding result and actionable error detail;
- Control owns modal presentation and navigation lock;
- HOTAS owns device polling, duplicate-device identity, settle/debounce windows, and validation;
- a successful capture changes the provider draft but does not persist until Apply;
- Escape, Tab/B Back, page/module change, host loss, device loss, and menu close cancel safely;
- gameplay injection and plugin-owned outputs park during capture; and
- held inputs are reseeded before gameplay resumes.

Update ABI prefix/tail tests in Control and HOTAS. Until this exists, HOTAS bindings in Control are
read-only or explicitly unavailable; do not accept a keyboard string as a fake DirectInput binding.

### Open/deep-link command

Control v1 has registration, refresh, and state queries but no provider command to open the host at
a particular module/page. The existing HOTAS workbench toggle therefore cannot yet become a clean
“Open Absolute Control -> AbsoluteHOTAS” binding.

Design an optional host tail or suite navigation export that:

- requests Show with module/page IDs;
- validates registered targets;
- uses the same PauseMenu/gameplay ownership and teardown path as ordinary opening;
- defers publication instead of rebuilding during an input callback;
- returns a bounded result; and
- remains optional so old Control builds and Control-absent HOTAS continue to work.

Do not synthesize F2, Escape, Pause, or mouse input to navigate into the host.

## Live graphs and custom components

The existing large HOTAS graphs are valuable and should migrate only after scalar editing is
stable. Use Control's experimental `TelemetryPlot` capability as a suite-private, size-gated
integration until its public status is decided.

- Publish calibrated hardware input, logical input, and injected/output values as separately
  labeled series.
- Provider code prepares immutable samples at a bounded rate; `readLiveFrame` only copies the
  latest fixed-capacity frame.
- Preserve the HOTAS rule that the visible graph is authoritative: inversion, capture landmarks,
  zone boundaries, and runtime comparisons share logical coordinates.
- No semantics depend on color alone. Label series and zones.
- Stop or reduce publication when the route is hidden or Control is closed.
- A missing live capability removes graphs, not scalar configuration or HOTAS gameplay.

## Implementation slices

### H0 — Contract and adapter skeleton

Implementation checkpoint: code, ABI/descriptor tests, full automated test suite, a no-live-deploy
release build, and Absolute Control's maintained cross-repository product validator passed on
2026-08-16. A supervised Starfield smoke test then loaded the ResearchDev Control host and
AbsoluteHOTAS without problems, enumerated the `absolute.hotas` module/pages, and exercised their
current read-only behavior as expected. A second run with Absolute Power installed enumerated and
operated both providers without a module-list or registration conflict. A final Control-absent run
then loaded AbsoluteHOTAS normally and exercised real binding changes and flight injection without
regression. H0 is release-verified on the automated and supervised runtime evidence required by this
slice.

- Copy the current Control ABI header into HOTAS under its existing dependency policy.
- Add `AbsoluteControlSubscriber.{h,cpp}` with dynamic discovery and copied descriptors.
- Register `absolute.hotas`, Setup Overview, and Plugin & Compatibility as read-only pages.
- Add exact ABI layout, duplicate-ID, bad-type, missing-host, rejected-host, and callback-exception
  tests.
- Verify HOTAS builds/tests and operates with Control absent.

Exit: module registration and host absence are mechanically proven; no gameplay behavior changes.

Exit satisfied on 2026-08-16. Control-present, Control-plus-Power, and Control-absent Starfield smoke
tests passed; binding edits and flight injection continued to operate as expected.

### H1 — Provider-owned scalar transaction

- Extract renderer-neutral load/draft/apply/cancel services from `WizardConfig`/`WizardSession`.
- Start with Plugin Controls and a small Flight Axes scalar subset.
- Apply through current persistence and live reload; verify semantic read-back.
- Add stale edit-target/profile protection and write/reload failure results.

Exit: one real setting round-trips in Control and the legacy overlay sees the same value afterward.

### H2 — Selected profiles and layers

- Add one populated transient profile/layer selector with source/activation/override metadata.
- Preserve sparse inheritance and **Use Primary Binding** removal semantics.
- Add create/rename/activation/revert/import/export only through explicit provider-owned actions.
- Reconcile profile switching with dirty page resolution before selection changes.

Exit: no profile operation flattens inherited state or silently loses another page's draft.

### H3 — Provider-owned HOTAS capture

- Land the optional capture contract and shell modal first.
- Adapt `WizardCapture` rather than duplicating DirectInput enumeration.
- Port core axis and ship-button bindings.
- Prove cancellation, device loss, duplicate device labels, parked output, and held-edge reseeding.

Exit: a DirectInput binding captures, drafts, applies, reloads, and executes without input leakage.

### H4 — Flight Axes and Throttle behavior

- Port all six axis bindings and common scalar tuning.
- Add behavior-first throttle choices before raw parameters.
- Correct the logical-coordinate invariant for inverted throttle capture.
- Add live graphs only through the size-gated mailbox component.

Exit: bind -> observe -> tune -> save -> fly works without the legacy overlay.

### H5 — Remaining HOTAS pages and legacy arbitration

- Port Aiming & Combat, Macros, Devices/calibration, and advanced profile workflows.
- Require separately installed Head Tracking and AbsoluteZero modules to coexist through the
  reviewed runtime-coordinator contract; do not port their settings into `absolute.hotas`.
- Add the deep-link/open capability and route the HOTAS menu binding when available.
- Ensure only one frontend owns an editable session; keep the overlay as a Control-absent fallback
  until parity is explicitly accepted.
- Remove or default-disable the overlay only in a separate reviewed release decision.

Exit: agreed feature parity and frontend arbitration matrix pass.

## AP Automation treatment for the simultaneous early release

Keep the stable page ID `power-automation`, but present it as:

> **Automation / Cheats (Coming Soon)**
> Preview only. Cross-weapon policy, demand settlement, and the final on-demand power UX are not
> release-qualified.

The early UI should not expose misleading editable automation controls. Prefer a read-only status
surface plus an immediate **Disable All Automation Now** safety action. Headless defaults remain
disabled. Do not delete the backend research; redesign it later around user-facing On-Demand Power
and possible Auto Combat Mode.

This keeps Absolute Control and the stable Presets/Diagnostics work shippable without turning one
Weapon 1 proof into a public automation promise.

## Build, deployment, and validation

### Automated baseline

From AbsoluteHOTAS:

```powershell
xmake
xmake test
```

Use the existing project-local deployment option; never commit the local path:

```powershell
xmake f --deploydir='<MO2 test mod>\SFSE\Plugins'
xmake
```

From Absolute Control, run the maintained product validator before Starfield:

```powershell
.\tools\process\validate-current.cmd
```

A pass is automated evidence only; it does not prove runtime or UX.

### Supervised runtime ladder

1. Create an ignored `hotas-control-smoke.local.json` from the example/current local manifest.
2. Require the production AbsoluteHOTAS mod and exactly one ResearchDev Control host in the
   isolated profile. Do not co-load canonical and ResearchDev hosts.
3. Build/deploy the matching HOTAS DLL/default INI and Control DLL/SWF/manifest.
4. Launch the manifest's SFSE shortcut directly through `tools/research/run-probe.cmd`; do not
   navigate Mod Organizer as a substitute.
5. Require registration, populated bridge model, normal Pause-origin open, Back to PauseMenu,
   ultrawide aspect restoration, close/reopen, and no new crash dump.
6. Exercise one scalar Apply/Cancel and require provider-owned read-back.
7. When capture lands, exercise capture, cancel, page-switch cancel, close cancel, and gameplay
   reseeding with a known device.
8. Remove/disable Control and repeat HOTAS startup plus representative flight controls.

Plugin or SWF changes require a full Starfield restart. Retained menu cycles are valid only when
the loaded binaries have not changed.

## Required acceptance matrix

- HOTAS alone, legacy overlay enabled.
- HOTAS alone, legacy overlay disabled, manual/profile configuration retained.
- HOTAS + Control, registration and scalar editing.
- HOTAS + Control + Power, module switching with clean and dirty pages.
- Control absent/incompatible/rejected after a previously successful run.
- Profile/layer switch while clean, dirty, and capture-active.
- Device disconnect/reconnect during an open page and during capture.
- Keyboard, mouse, controller navigation, and DirectInput capture as separately evidenced paths.
- Pause-origin Back, direct-open Close, ultrawide restoration, alt-tab, and repeated reopen.
- Unsupported runtime/native seam failures remain visible and fail closed.
- Existing HOTAS runtime actions, macros, independent aiming, reverse, strafe, and profile
  activation show no regression with the menu closed.
- After the extraction slice, Absolute Head Tracking and AbsoluteZero separately prove their
  camera-pose and HOSAM-centering paths with and without the optional runtime coordinator.

## Definition of done

The integration is not complete until:

- Control is optional in code and observed absent at runtime;
- every exposed value maps to one existing HOTAS owner and persistence path;
- supported settings survive Apply/reload/read-back and Cancel without loss;
- DirectInput capture remains HOTAS-owned and is safely orchestrated by the shell;
- no second authoritative profile/config model exists;
- the frontend arbitration rule prevents concurrent editable sessions;
- live graphs cannot stall device/gameplay threads;
- optional Head Tracking and mouse-centering policies are not reintroduced as HOTAS-owned Control
  settings;
- the supervised installation matrix has retained semantic evidence and human UX review; and
- documentation distinguishes implemented, automated verified, runtime verified, observed, and
  deferred work.

Do not publish, upload, merge broad dirty worktrees, or remove the legacy overlay merely because
the first page renders.

# AbsoluteHOTAS -> Absolute Control integration handoff

> **Status:** Native-menu cutover implemented; legacy renderer frontend retired
> **Date:** 2026-08-22
> **Immediate objective:** Runtime-qualify the complete Absolute Control workflow and the
> Control-present/Control-absent release matrix without reintroducing a graphics-hook fallback.
> **Release posture:** Absolute Power Presets and Diagnostics may ship alongside this work;
> its unfinished Automation / Cheats route is an explicitly unavailable **Coming Soon** preview.

## First-turn brief

Begin in the AbsoluteHOTAS repository. Read this document completely, then inspect the files and
contracts linked under [Required reading](#required-reading). Do not start by copying ImGui draw
code or by adding settings to Absolute Control itself.

The native provider now publishes all nine HOTAS pages and owns capture, transactions, profiles,
macros, calibration, telemetry, and persistence. The retired workbench remains a historical behavior
reference only. Preserve flat native pages as the deterministic fallback for an older or rejecting
composition host, but do not restore an embedded renderer frontend.

Stop and document a blocker rather than inventing a synthetic key path, letting Control parse a
HOTAS INI, or moving DirectInput polling into the menu host.

## Product and release decision

AbsoluteHOTAS remains a standalone flight-control mod. Absolute Control is its sole in-game
PauseMenu editor, not a runtime parent and not a new configuration owner. HOTAS gameplay and manual
configuration remain fail-optional when Control is absent, but the plugin no longer compiles,
links, or installs the Dear ImGui/D3D12 workbench.

The early simultaneous release does **not** require:

- complete migration of every advanced HOTAS page;
- restoration of the retired legacy overlay;
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
| Bound pitch/yaw steering and independent reticle/aim routing when AbsoluteZero is absent | AbsoluteHOTAS |
| Installed-suite declaration that native mouse owns pitch/yaw, plus idle centering policy | AbsoluteZero |
| OpenTrack input, pose shaping, camera composition, recenter/toggle | Absolute Head Tracking |
| Idle mouse-centering policy, radius, delay, decay, and suppression | AbsoluteZero |
| Shared lane arbitration when several flight modules are installed | Optional Absolute Flight Runtime |
| Module pages, transactions, capture presentation, and diagnostics | Absolute Control |

AbsoluteZero is a conditional modifier, not the owner of raw mouse input. Its installed presence is
the suite declaration that native mouse steering owns pitch/yaw: HOTAS releases both writer gates,
its source-aim path, and its embedded alignment assist. HOTAS retains roll/strafe and the remaining
flight lanes. Standalone HOTAS pitch/yaw and independent aiming behavior remain unchanged.
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
- `AbsoluteHOTAS_QueryInputBusApi(1)` is the new pre-release, fail-optional Input Bus. It mirrors
  copied DirectInput snapshots and edges, runs serialized provider-owned capture, publishes active
  profile identity, and shares validated pilot/gameplay/targeting context. It does not dispatch
  consumer actions or replace the separate runtime-ownership arbitration boundary. See
  [Absolute Input Bus ABI v1](ABSOLUTE-INPUT-BUS-V1.md).
- `AbsoluteControlSubscriber` now discovers either Control product DLL name at SFSE
  post-data-load and registers all nine stable HOTAS page IDs. Flight Axes, Ship Buttons,
  Throttle Setup, Aiming & Combat, and Plugin & Compatibility publish the 63 HOTAS-owned static
  settings that ABI-v1 scalar controls can express. The provider-owned capture tail publishes the
  53-target static HOTAS binding catalog, including the Rate Throttle Turn Assist activation.
  Throttle Setup exposes inversion-aware guided landmark capture: a transient set-by-feel gesture
  moves detent/zero-thrust/boost bands with the live lever, then the second press or Apply commits
  the current position. Zone widths and idle/saturation edits publish into the same preview before
  persistence. The legacy symmetric idle/saturation relationship remains an explicit one-shot draft
  action rather than persistent presentation state.
  Every named ship binding publishes its selected Direct/Context/Keyboard route and live availability.
  Profiles & Layers publishes a bounded Main/overlay/full-profile collection, sparse inheritance
  summaries, activation capture, dirty-switch resolution, and the existing create/export/import/reset
  repository operations. Twelve experimental live channels add six card-local Flight Axes plots,
  calibrated/shaped overview plots, throttle range/landmark/response, HOTAS aim telemetry, and
  selected-device state without changing the stable ABI. The C2 composition lane publishes the
  Flight Axes page as an 83-node tree: six jump anchors, four sections, nine cards,
  all 43 composed controls, six embedded plots, and 10 direct-manipulation associations. Flight
  Axes is the first tab and exclusively owns all seven analog bindings. Ship Buttons contains no
  duplicate axes and publishes a 100-node, four-section layout: complete Native Ship Controls,
  AbsoluteHOTAS Hotkeys, Optional Menu Navigation, and Custom SendInput Bindings.
  Control's live lane publishes active-page-only latest-sample patches at the movie frame boundary;
  the pinned throttle range redraws independently of the scrolling controls, while throttle response
  history is a host-owned secondary disclosure collapsed by default.
  Macros and custom shortcuts publish bounded selected-record editing, while Devices publishes
  bounded inventory, binding reassignment, and transactional calibration. Setup remains status-only. It
  retries a `NotReady` host once at post-post-data-load and leaves standalone gameplay unchanged
  when the optional host is absent or rejects the provider.
- The scalar adapter owns one renderer-neutral, revisioned draft for Main controls, atomically
  updates its catalogued keys in `AbsoluteHOTAS_Custom.ini`, requests the normal controller
  reload, and performs semantic read-back.
- Opening Absolute Control parks HOTAS gameplay output; held input edges are reseeded when editing
  ends. There is no second in-process frontend or renderer arbitration path.
- `xmake` builds and optionally deploys the DLL/default INI. `xmake test` currently exercises
  22 standalone test targets, including composition, Control subscriber, capture, transaction,
  device, macro/profile, ownership-migration, and live-mailbox suites.

### Absolute Control

- The product ABI authority is `include/AbsoluteControlPanelAPI.h`, ABI v1.
- AbsoluteHOTAS's byte-identical vendored snapshot and update provenance are recorded in
  [Absolute Control SDK snapshot](ABSOLUTE-CONTROL-SDK-SNAPSHOT.md).
- The host copies module/page/control descriptors and invokes provider callbacks through leases.
- Standard controls, labeled choices, text entry, page transactions, keyboard capture, and the
  experimental live-component channel exist.
- The separately negotiated C2 semantic-composition API adds bounded sections, cards, anchors,
  status/conditions, same-page live slots, and validated marker/series associations. It does not
  advertise direct graph manipulation or the later record/workflow vocabulary.
- Mouse/controller/HOTAS capture is provider-owned through the optional size-gated capture tail.
- The appended, size/capability-gated page-open command asynchronously routes the legacy-named
  `iToggleWizardButton` and `Ctrl+Alt+B` to `absolute.hotas/hotas-setup`; older or unavailable hosts
  leave gameplay/manual configuration active and log that no native editor route is available.
  The provider call performs no synthesized input or direct Starfield UI work.
- The same command backs renderer-neutral deep links from Flight Axes to the single-owner Throttle
  Setup draft and from Ship Buttons shortcuts to Macros. Pre-command hosts receive the original
  read-only guidance instead of a nonfunctional Action.
- Dirty page/module navigation and Close now use the host-owned Apply / Discard / Stay modal.
  Provider Apply failure retains the draft and lease; abnormal Hide/destruction still performs
  fail-safe Cancel.
- The public SDK and live-component contract are not release-frozen.

### Working-tree caution

All three suite repositories contain broad, intentional uncommitted work from the Control and
Power phases. Inspect `git status` and focused diffs before editing. Do not discard, normalize, or
commit unrelated changes as part of this handoff.

## Required reading

Read these as authoritative/current unless the file itself says it is historical:

- [Full HOTAS legacy-menu feature inventory for Control and UX](ABSOLUTE-CONTROL-HOTAS-FEATURE-INVENTORY.md)
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
- Do not reintroduce a second embedded frontend or renderer-owned draft.

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

Control v1 now exposes the optional, size-gated provider command needed to open a registered
module/page. The legacy-named HOTAS menu binding is a clean
“Open Absolute Control -> AbsoluteHOTAS” route.

The optional host tail:

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

Implementation checkpoint: completed on 2026-08-17. The first native editing slice exposes
Main-controls flight enable, pitch inversion, pitch sensitivity, outside-pilot-seat behavior,
automatic pilot detection, and pilot latch duration. Automated coverage proves typed validation,
labeled choices, draft read-back, Apply, Cancel, stale-source rejection, failed-Apply retention,
host absence/rejection, and host-open arbitration. The complete nine-test HOTAS suite and nine-test
Control suite pass; both rebuilt artifacts are staged in the Starfield Testing Baseline. A
supervised Starfield run on 2026-08-17 then exercised every exposed function, including draft
editing, Apply/Cancel, persistence/reload, guarded navigation/Close, and frontend transitions. The
tester reported clean behavior with no visible seams.

- Extract renderer-neutral load/draft/apply/cancel services from `WizardConfig`/`WizardSession`.
- Start with Plugin Controls and a small Flight Axes scalar subset.
- Apply through current persistence and live reload; verify semantic read-back.
- Add stale edit-target/profile protection and write/reload failure results.

Exit: one real setting round-trips in Control and the HOTAS runtime observes the verified reload.

Exit satisfied on 2026-08-17. Applied values survived reopening and the normal HOTAS reload path;
all exposed H1 functions passed the supervised smoke test.

### Daughter-module extraction checkpoint

Implementation checkpoint: completed on 2026-08-17 for the frontend/configuration side. The
standalone Absolute Head Tracking and AbsoluteZero DLLs both register through the current product
ABI and render as isolated suite modules in Absolute Control. Head Tracking's General, Axes, and
Bindings pages passed change, discard, save, stale-setting, and binding smoke tests. AbsoluteZero's
Mouse Alignment page passed its supervised menu/runtime smoke test with product keyboard capture,
provider-owned editing, and the current ResearchDev lifecycle. A combined run enumerated Head
Tracking, Absolute Power, and AbsoluteZero without module-list conflicts.

The current compatibility slice now gates HOTAS's legacy alignment implementation whenever
AbsoluteZero is installed. HOTAS remains the single rotational-writer patch owner, releases its
pitch/yaw and source-aim claims, and exposes size-versioned bounded accumulator operations;
AbsoluteZero installs no second trampoline and retains its own centering policy/configuration.
Build and contract tests pass, and the paired runtime smoke confirmed that AbsoluteZero owns mouse
auto-centering while HOTAS retains the shared hook and releases its mouse pitch/yaw claims.
The camera extraction/coexistence slice is now implemented for runtime validation. Absolute Head
Tracking declares sole camera ownership through a size-versioned ABI after SFSE plugin loading;
HOTAS parks its embedded tracker and skips or releases its legacy FirstPersonState hook while
retaining the flight-output observer. Its shared timestamp is independent of HOTAS controller
acquisition/enabled state. Head Tracking consumes only the observer's bounded signal age and
installs the sole camera hook. Older HOTAS builds without the ownership ABI remain
fail-closed. This is runtime arbitration between daughter modules, not an Absolute Control
registration responsibility.

Supervised combined-suite smoke passed on 2026-08-17: Absolute Control enumerated the full installed
suite in one menu and the Head Tracking/HOTAS camera-ownership boundary loaded successfully. The
current HOTAS pages remain a proof of concept/technical demonstration. There is not yet a provider-
owned joystick binding UI for Absolute Power controls or Absolute Head Tracking toggle and recenter
actions. The Input Bus backend for those pages is now implemented in HOTAS and pending full-suite
runtime qualification plus first-party consumer dogfooding; daughter action/configuration ownership
does not move back into AbsoluteHOTAS.

### Input Bus v1 backend — before remaining menu work

Implementation checkpoint: the pre-release ABI, copied device snapshots, digital press/release
counters, controller-thread capture service, active-profile identity, and typed runtime-context
snapshot are implemented. The complete eleven-test HOTAS suite and full DLL build pass. The current
test build is deployed for an ordinary HOTAS load/gameplay smoke; public stability is not claimed
until first-party consumers have exercised the contract.

- Keep HOTAS as the sole DirectInput poller and mirror snapshots after its normal poll.
- Keep action semantics, transactions, and persistence in each consumer mod.
- Treat consumer discovery as fail-optional; custom/keyboard bindings remain the fallback.
- Dogfood button capture in Absolute Head Tracking and Absolute Power before publishing an SDK.
- Keep full profile serialization out of the bus; expose identity and generation only.
- Publish pilot, gameplay-active, targeting, and handler-age signals with validity metadata.

Exit: Head Tracking and Power record and execute direct flight-stick bindings through the same bus,
both still load without HOTAS, and a profile switch/reconnect produces no synthetic press.

### H2 — Selected profiles and layers

- Add one populated transient profile/layer selector with source/activation/override metadata.
- Preserve sparse inheritance and **Use Primary Binding** removal semantics.
- Add create/rename/activation/revert/import/export only through explicit provider-owned actions.
- Reconcile profile switching with dirty page resolution before selection changes.

Exit: no profile operation flattens inherited state or silently loses another page's draft.

### H3 — Provider-owned HOTAS capture

- Use the landed Input Bus capture backend; add the Control shell modal/presentation contract.
- Adapt the existing HOTAS binding pages and first-party daughter pages to the shared backend rather
  than duplicating DirectInput enumeration or recording logic.
- Port core axis and ship-button bindings.
- Prove cancellation, device loss, duplicate device labels, parked output, and held-edge reseeding.

Exit: a DirectInput binding captures, drafts, applies, reloads, and executes without input leakage.

### H4 — Flight Axes and Throttle behavior

Implementation checkpoint: the first tab-by-tab rebuild is mechanically complete. Flight Axes
publishes the legacy page summary, six-axis jump navigator, Thrust/Rotation/6-DOF/Fallback
sections, six core axis cards, Reverse and Digital Fallback cards, the complete 45-control stable
surface, and six embedded input/output plots. Node state reports bound source, missing-required
severity, reverse strategy, fallback coverage, and unapplied draft state. The provider registers
this enhancement only after both the stable page and live channels succeed; otherwise the existing
flat page remains available. Runtime visual/input qualification is still required, so this is not
yet a parity or release claim.

- Port all six axis bindings and common scalar tuning.
- Add behavior-first throttle choices before raw parameters.
- Correct the logical-coordinate invariant for inverted throttle capture.
- Add live graphs only through the size-gated mailbox component.

Exit: bind -> observe -> tune -> save -> fly works without the legacy overlay.

### H5 — Remaining HOTAS pages and native-menu cutover

- Port Aiming & Combat, Macros, Devices/calibration, and advanced profile workflows.
- Require separately installed Head Tracking and AbsoluteZero modules to coexist through the
  reviewed runtime-coordinator contract; do not port their settings into `absolute.hotas`.
- Add the deep-link/open capability and route the HOTAS menu binding when available.
- Keep Absolute Control as the sole in-game frontend while HOTAS remains the configuration owner.
- Retire the overlay build units, renderer dependencies, runtime hook installation, fallback route,
  obsolete `[UI]` switch, and user-facing workbench guidance together.

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

- HOTAS alone, no renderer hooks, manual/profile configuration retained.
- HOTAS alone, menu chord/binding fails safely and logs the absent native host.
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
- the single native frontend parks gameplay and safely tears down capture/edit sessions;
- live graphs cannot stall device/gameplay threads;
- optional Head Tracking and mouse-centering policies are not reintroduced as HOTAS-owned Control
  settings;
- the supervised installation matrix has retained semantic evidence and human UX review; and
- documentation distinguishes implemented, automated verified, runtime verified, observed, and
  deferred work.

Do not publish or upload until the complete native-menu path and Control-absent runtime have current
automated and supervised evidence.
# Throttle direct-edit refinement

The pinned throttle range is the primary positional editor. Absolute Control resolves the
provider's range semantics into distinct idle/zero/active/cruise/full/reverse/boost colours and
draws the physical lever separately in white. The three center markers retain typed scalar
`controlId` links, so pointer dragging uses the ordinary HOTAS draft-write, validation, Apply, and
Cancel path. A direct graph write cancels any active physical set-by-feel capture before updating
the draft, preventing two input sources from moving one landmark.

The page groups the remaining controls by workflow: direct/by-feel landmark setup, positional
zone widths, advanced precise landmark values, rate throttle, turn assist, and the one-shot
idle/saturation utility. Width controls continue to redraw the range immediately. Physical
tracking remains the preferred tactile workflow and precise raw sliders remain the accessible
keyboard/controller fallback.

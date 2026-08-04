# 4.0.1 Technical-Debt Baseline

This review captures structural risk observed at the start of the 4.0.1 branch.
It is a stabilization backlog, not a mandate to refactor working runtime code
without a reproducible reason.

## Baseline validation

- The release plugin builds successfully.
- The sparse-profile overlay tests pass.
- The ControlMap fixture tests pass when run from the repository root, but their
  previous documented `xmake run` command ran from the target output directory and
  could not find its fixtures.
- Binding-reference parsing had no standalone coverage and accepted partial numeric
  strings such as `42garbage`.
- Logging is used from controller, UI/render, and hook threads, but rotation and
  append previously shared unsynchronized state.

The first 4.0.1 maintenance patch registers all standalone targets with `xmake test`,
gives the fixture test a stable run directory, covers strict binding parsing, and
serializes logging.

The Flight Axes redesign also removes the separate axis-tuning route and its
duplicate renderer. Binding, inversion, response shaping, calibration context,
and live input now share one axis-card implementation on the opening page. This
closes a UI ownership split that could otherwise let the binding and tuning
workflows diverge.

## Priority backlog

### High: shared runtime state

`DeviceManager` owns process-global device vectors, DirectInput handles, and cached
states. The controller polls them while wizard/render code reads snapshots and may
open additional devices. The ownership model is implicit and has no synchronization
contract. Before adding hot-plug or live refresh, move device mutation to one owner
thread or introduce an explicit snapshot/locking boundary.

`ThrottleController::GetConfig()` returns a mutable reference to the live config.
The controller replaces that object during reload/profile activation while the UI
copies fields after observing a generation counter. The counter orders completed
reloads but does not prevent a later reload from overlapping a UI read. Replace the
reference API with an immutable snapshot published at a defined synchronization
point.

### Medium: lifecycle and test seams

`ThrottleController` detaches its worker thread. `Stop()` requests shutdown but
cannot join or prove that device and output cleanup completed. A joinable lifetime
would make shutdown, reload tests, and future plugin teardown safer.

The largest behavioral modules remain concentrated in
`ThrottleController.cpp`, `ThrottleHook.cpp`, and `UIHookRenderer.cpp`. Extract
pure transition logic only when adding tests for a bug or feature; avoid a broad
file split that merely moves state around.

Current standalone coverage protects ControlMap parsing, sparse overlays, and
binding references. High-value next seams are macro timing/transitions, output
ownership/reference counting, config precedence, and stop/profile-transition
release behavior.

### Low: maintenance ergonomics

Add a CI job that performs a clean release build plus `xmake test`. Until then,
release validation depends on a maintainer remembering both commands.

Keep warning cleanup and formatting changes separate from behavioral patches so
4.0.1 regressions remain easy to bisect.

## Post-4.0.1 architecture review

This section is deliberately outside the 4.0.1 release scope. After 4.0.1 and any
required hot-fixes have settled, review each subsystem's underlying mechanism and
classify it as retain, harden, refactor, or replace. Judge changes by demonstrated
stability, compatibility, testability, maintenance cost across Starfield updates,
and measurable performance rather than rewriting validated code for its own sake.

The highest-value research target is native named-action dispatch. Analog flight is
already solved well enough for the current release, apart from two specific gaps:
a downstream lateral-thruster writer and a true signed/analog reverse path. Broad
analog writer discovery is therefore lower priority than finding the engine boundary
after native binding resolution but before ship-action handling.

A developer-only discovery tool could stimulate known native bindings, record action
identifiers or event objects and their call stacks, identify the shared dispatcher,
and validate candidates by replaying press/release events. The preferred end state is
one native action backend for named ship functions, retaining the current
ControlMap-aware `SendInput` path as a compatibility fallback. Candidate addresses or
signatures must remain reviewable artifacts and must never be distributed to users
automatically without runtime-specific validation.

Profiles and macros are the strongest candidates for small deterministic state-machine
refactors with injected clocks and output sinks. DirectInput, synthetic output,
configuration publication, worker lifetime, low-level trampolines, and the optional
renderer hook should each receive the same mechanism review after the release window.

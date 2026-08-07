# 5.0.0-beta Pilot-Context Validation

This checklist validates the production integration of the selected flight-handler
freshness seam. The isolated research probe already established the signal; these
cases cover the release controller, workbench, and output gates.

## Default configuration

Use the shipped defaults:

```ini
[Gate]
PilotGateMode = InjectionOnly
PilotSignal = Auto
iPilotLatchMilliseconds = 5000
```

Enable `bEnableLog=true` for the run. Expected transition messages use the
`[PilotState]` prefix and distinguish `Piloting`, `OnFoot`, and `Suspended`.

## Required lifecycle pass

1. Start or load **on foot**, then enter the cockpit.
   - Discovery must remain live while parked.
   - `Piloting` should appear as soon as the selected handler begins executing.
   - Flight axes, native ship buttons, and Camera Look should become active.
2. Get up from the pilot seat.
   - Camera Look must stop within roughly 400 ms.
   - Flight-axis injection must park after the configured 5000 ms latch.
   - A custom raw on-foot binding or macro should remain available in
     `InjectionOnly` mode.
3. Re-enter the cockpit.
   - The gate must reopen immediately on a fresh handler hit.
   - A ship-action button held throughout the transition must not fire until it is
     released and pressed again.
4. Open and close the pause menu while seated.
   - Context should become `Suspended`, never `OnFoot`.
   - Output should resume without a profile change or stale button edge.
5. Exercise a loading transition from both cockpit and on-foot states.
   - Loading remains `Suspended`.
   - The post-load gameplay state is classified only after context becomes active.

## Recorded validation

- 2026-08-06: production Camera Look automatically deactivated when the maintainer
  exited the pilot seat. This validates the conservative selected-handler freshness
  gate on the highest-risk cockpit-to-FPS transition.
- The deployed default had `bEnableLog=false`; consequently, the runtime log in the
  mod folder remained stale and belonged to the isolated research build. This pass
  is recorded from direct in-game observation rather than a production
  `[PilotState]` trace.
- Cockpit entry/re-entry, delayed flight-axis parking, pause/loading, targeting,
  and `Full` mode remain useful beta coverage; this confirmed head-pose safety does
  not imply those separate cases have passed.

## Targeting-mode pass

1. Enter targeting mode for less than five seconds.
   - Camera Look may pause after 400 ms.
   - General flight controls should remain latched.
2. Remain in targeting mode longer than five seconds.
   - Record whether the general gate parks.
   - Leaving targeting must restore `Piloting` immediately on the next handler hit.
3. If normal targeting sessions exceed the default latch, increase
   `iPilotLatchMilliseconds` or investigate a dedicated targeting-state exception
   before publishing this integration.

## Full-mode pass

Set `PilotGateMode=Full` in the workbench and repeat cockpit exit/re-entry.

- Axes, native actions, raw bindings, and macros must all release outside the seat.
- Detection must continue while closed so re-entry reopens the gate.
- Inputs held during re-entry must be consumed until released and pressed again.

## Release decision

The validated cockpit-exit head-pose gate is sufficient to promote this integration
to the opt-in beta. Treat the remaining lifecycle, targeting, and Full-mode results
as beta tuning evidence; record any false `OnFoot` transition with timestamps and
the matching log excerpt.

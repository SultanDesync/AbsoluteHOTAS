# Profile Switching (design)

Status: design. **Supersedes the shift-layer design** this file previously held. A
profile slot binds a physical button to a whole configuration, swapped live:
bindings, macros, axis assignments, curves, deadzones, aim mode — the lot.

## Why this instead of shift layers

A shift layer gives every button a second meaning by qualifying its binding lookup.
It is a narrower primitive than it appears, because it conditions *binding lookup*
and nothing else. The moment a user wants a "precision mode" that also softens the
pitch curve, or a cruise mode that changes what the throttle axis even does, layers
have nothing to say — those are tuning values and axis assignments, not bindings.
The old design punted both to future work.

Swapping the whole config subsumes them. It also removes a cross-cutting concern:
layers required threading a `:L<n>` qualifier through the button-reference parser
and every binding site that consumes it. Profile switching adds one piece of state —
*which config is active* — and touches nothing else. Fewer places to get wrong.

## Model

- **Base profile** — what loads today: `AbsoluteHOTAS.ini` overlaid by
  `AbsoluteHOTAS_User.ini`. Always slot 0.
- Up to **3 switch slots** (1–3), each naming a profile file and a trigger button.
  Bounded so the wizard stays a 4-pill selector; nobody flies 5 configs.
- **One profile is active at a time.** Slots do not compose.

### Activation modes

| Mode | Behavior |
| --- | --- |
| `momentary` | Active while the button is held; on release, return to the previously active profile. |
| `toggle` | Press flips between base and this profile; press again returns. |

Momentary save/restore composes with a latched toggle: holding a momentary slot on
top of a toggled one returns to the toggled one on release.

**Deactivate returns to base.** The master toggle, the stop binding, and the pilot
gate all reset to slot 0. There is always a way home that does not depend on a
latched slot being in the state the user thinks it is.

### Sparse profiles are fallthrough

A switch profile need not be a full snapshot. `LoadConfig` already merges
main → user → macros with last-value-wins per key, so **adding the active profile as
a further overlay makes a sparse file behave exactly like a layer**:

```ini
; Profiles/precision.ini — a "shift layer", expressed as two overridden keys
[Hardware]
fPitchSensitivity = 0.40
fYawSensitivity   = 0.40
```

Everything not mentioned is inherited from the base. No duplicated ship-button
tables, no drift when the user rebinds their throttle. A full snapshot (what
`Export Profile` writes) also works — it simply overrides everything.

One file format, one mechanism; sparse or full is a property of the *content*, not a
flag. This is the payoff for the layered overlay built in
[config-layout.md](config-layout.md).

## Import vs Swap — same file, opposite verbs

These are easy to conflate, and conflating them eats the user's config.

| | Import | Swap |
| --- | --- | --- |
| Effect | Replaces `_User.ini` + `_Macros.ini` | Overlays the base, in memory |
| Persistence | Written to disk, survives restart | Transient, gone on restart |
| Disk I/O | Writes both user files, auto-backs up first | **None** |
| Trigger | Wizard button, deliberate | Physical button, mid-flight |
| Reversible by | Importing something else | Releasing / re-pressing the button |

**A swap must never call `ImportProfile`.** That writes `_User.ini`, so a shift
button would overwrite the user's base config with the layer, fire an auto-backup on
every press, and — via `ReloadConfig` → `LoadShipButtonBindings` — re-read
`ControlMap_Custom.txt` from the Documents folder on every press.

## Runtime rules

**Preload, don't reload.** At startup and on wizard Save, build each slot's merged
config once: load main → user → slot file, produce a `Config`, run `ResolveAll` so
device names resolve to indices. A swap is then an index change plus a release of
held outputs. Zero file I/O. This is mandatory, not an optimization: a momentary slot
swaps twice per press.

*What a swap costs.* Index change, one `SendInput` per currently-held output
(typically 0–3), and a pass over the button table to seed `previousPressed`. Tens of
microseconds against an 8.3 ms tick at 120 Hz — under 1% of one poll, imperceptible.

*What a reload would cost.* `ReloadConfig` → `LoadConfig` parses three INIs, and
`LoadShipButtonBindings` reads `ControlMap_Custom.txt` from the Documents folder.
Disk I/O on the control thread, potentially cold: milliseconds, i.e. dropped polls,
felt as an axis hitch. **"Just call LoadConfig again" is the failure mode this design
is most likely to acquire from a well-meaning change.** Don't.

*Ownership refactor this implies.* The active configuration is not one struct. It is
spread across `ThrottleController::s_config`, `ShipOutputSystem::s_shipButtonBindings`
(which carries the ControlMap-resolved outputs), and `MacroEngine::s_macros` +
`s_runtime`. Preloading means each becomes an indexed set with one active slot rather
than a lone global. Mechanical, but it is the real work, and it must land before any
slot UI — otherwise the disk read sneaks back in as the path of least resistance.
`ShipOutputSystem::s_heldShipOutputs` stays single and global: it is runtime state,
and a swap releases it.

**Consume buttons held across a swap.** This is the one thing shift layers'
press-time resolution was genuinely buying. If the user holds Fire, taps the swap
button, and the ship-button table is rebuilt with `previousPressed = false`, the
button is still physically down and re-fires as whatever it means in the new
profile. On swap, seed `previousPressed = true` for every button currently down, so
it is consumed until genuinely released. One contained place — versus threading
press-time ownership through four binding sites.

**A swap releases everything.** All held outputs and macro-held keys are released
(`ReleaseAllShipButtonOutputs`, `MacroEngine::ReleaseAll`). Coarser than per-binding
layer semantics, but a cleaner contract, and it cannot leave a key stuck.

**Runtime state survives; config does not.** The accumulator throttle value, aim
smoothing, and calibration are runtime state and are not reset by a swap — the same
guarantee the existing hot-reload path already provides while flying.

**No swapping while the wizard is open.** Slots are suppressed with the overlay up,
as macros already are, so the config cannot be yanked out from under the editor.

### Step response, not lag

A swap is instantaneous, which means its artifacts are discontinuities rather than
stutter. Both are inherent; neither is a performance problem.

- **Held outputs release.** Swap while holding Fire and the key comes up. Correct —
  it is what prevents a stuck key — but it is felt.
- **Axis values step.** A slot that changes pitch sensitivity 1.0 → 0.4 makes the
  injected pitch jump from 0.8 to 0.32 in a single tick; for a deliberate precision
  toggle that *is* the feature. A slot that reassigns pitch to a *different physical
  axis* will jolt the ship to wherever that other stick is resting. The cruise recipe
  has the same shape in reverse: swapping back hands the throttle from the game to a
  stick that may be at 30%, and the ship drops to 30% at once. That is correct 1:1
  behavior, and it will still surprise a first-time user.

Guidance follows from that: `momentary` slots for tuning changes, `toggle` slots for
modes the user is deliberately entering. Reassigning axes between slots is legal and
occasionally what you want, but it is the one change that can jolt a ship in flight.

## Open-loop reality

A profile swap is a blind config change. It cannot read game state, and it does not
know whether the game did what the user's other button asked. This is the same
constraint macros live under.

The sharp edge is **binding a swap to the same button as a game action that can
fail**. Cruise is the worst case: it is a locked progression ability for many
players, so the game routinely ignores the keypress. If one button both emits
`Cruise` and latches the cruise profile, a player without the ability latches a
config whose throttle axis is unbound while flying normally — their throttle stick
goes dead, and it reads as "the mod broke my throttle." That is a louder version of
a support trap this project already has.

Guidance:

- **Bind the swap to its own button**, not the one emitting the game action.
- Prefer `momentary` for shift-style slots; reserve `toggle` for modes the user
  deliberately lives in.
- Deactivate always returns to base (above).

Reading actual flight-mode state from the control cluster would close the loop and is
the obvious dream. Resist it for now: every auto pilot-state flag investigated so far
has been a dead end, and a headline feature should not hang on another one.

## INI form

Slot definitions are user-owned → `AbsoluteHOTAS_User.ini`. Profile files live in
`Profiles/`, the same directory Export writes to.

```ini
[Profiles]
; Slot<N> = <file> <button ref> <momentary|toggle>
Slot1 = precision.ini  VKBSim Gunfighter@11  momentary
Slot2 = cruise.ini     VKBSim Gunfighter@12  toggle
```

## Worked example — HOSAS cruise mode

The flagship demo, and the one that shows this is a recipe rather than a mechanism.

A HOSAS pilot wants 1:1 throttle on their left stick Y axis in normal flight, but
when cruising they want the ship pinned at cruise speed with the stick out of the
loop. Implementing that as "axis logic swapping" would mean a new conditional inside
the axis pipeline. As a profile it is two lines:

```ini
; Profiles/cruise.ini
[Hardware]
iThrottleAxis =      ; explicitly cleared -> unbound
```

An unbound throttle axis makes the plugin stop silencing the game's throttle channel
(the 3.0.2 fix), so the game owns speed — which, in cruise, is exactly who should.
Swap back and stick authority returns instantly at the stick's current position,
which is what 1:1 means.

The left stick Y is now free while cruising, and nothing stops the profile from
reassigning it to something else entirely — expressiveness shift layers never had,
since they never touched axis assignment.

Per the hazard above: bind the swap to its own button, not to the Cruise action.

## Profile kind — `full` vs `overlay`

`Profiles/` holds two kinds of file, and they must be distinguishable because Import
is a full replace. Importing a two-key `precision.ini` would otherwise leave the user
with two keys plus mod defaults for everything else — a trap sitting in a dropdown.

**Decision: mark the kind in the `[Profile]` header** (`sKind = full | overlay`). The
header already exists, so it costs one key. Import refuses an `overlay` with an
explanation; slots accept either.

| Written by | `sKind` | Contents |
| --- | --- | --- |
| Export Profile | `full` | Complete snapshot of the user files |
| Slot editor | `overlay` | Only the keys this slot overrides |
| Slot editor → "Detach from base" | `full` | Snapshot of the slot's effective config |

Rejected: separate directories (splits the "send me your INIs" story), and inferring
the kind from content (a silent heuristic on the destructive path).

## No "copy base into this slot"

The obvious feature request is a button that seeds a slot from the primary profile,
so the user has something to edit. **Do not materialize that copy.** Every key in the
copy becomes an override, fallthrough dies, and the slot silently stops tracking
base: rebind an axis after buying a new stick, swap to the slot, and the stick is
dead because the slot still names the old device. That is precisely the drift that
sparse overlays exist to prevent, reintroduced through the front door.

The starting point is already there, virtually. The slot editor renders the effective
config — base values, greyed — so opening a slot *looks* like a copy. Editing a field
is what creates the override. Three edits produce a three-line file, and rebinding the
throttle in base still propagates, because the slot never claimed to own it.

What the UI needs instead is the inverse of Copy:

- a per-row **override marker** (this value is mine, not inherited),
- a per-row **Revert to base**, and
- an **override count** per slot, so a precision layer reads as "2 overrides" rather
  than looking identical to base.

This also keeps the files legible. A sparse `precision.ini` is two lines and
self-documenting. A copied full profile is a hundred lines whose intent cannot be
recovered without diffing it against base — which matters as soon as someone posts
one on Nexus.

A real copy remains available as an explicit **"Detach from base"** action, for a
config the user deliberately wants to stop tracking base. It writes `sKind = full`,
and it is the same artifact Export produces.

## Wizard UI

- **Profiles section:** three slots, each with a file picker over `Profiles/*.ini`,
  a Bind capture for the trigger button, and a momentary/toggle combo.
- **Slot pills** — `Base | 1 | 2 | 3` — at the top of the Buttons, Flight Axes, and
  Macros tabs. Editing with a slot selected writes to that slot's profile file;
  values inherited from base render greyed, so the user sees the *effective* config
  rather than a sparse one, and can tell at a glance what this slot actually changes.
  Overridden rows are marked and carry a **Revert to base**; the pill shows the
  slot's override count. See "No copy base into this slot" above — the greyed
  inherited values *are* the starting point, and materializing them would be a
  regression, not a convenience.
- **Live indicator** in the wizard footer showing the active profile — the test
  surface for momentary/toggle behavior.

## Test checklist

- Hold a button bound in slot 1, tap the swap button, release → no stuck key, the
  base binding does not fire on release.
- Sparse profile: a key absent from the slot file falls through to base.
- Momentary held on top of a latched toggle → returns to the toggled profile.
- Swap during an in-flight macro → macro releases cleanly, does not resume.
- Cruise recipe: swap to a profile with `iThrottleAxis =` → throttle returns to the
  game; swap back → stick authority resumes at the stick's current position.
- Master toggle / stop while a slot is latched → returns to base.
- Swap with an accumulator throttle mid-integration → value is preserved.
- Wizard Save while a slot is active → all slots rebuild; no disk write on swap.
- Edit one field under a slot → the slot file gains exactly one key; everything else
  still follows base when base changes.
- Revert to base on that field → the key is removed from the slot file, not zeroed.
- Import refuses an `sKind = overlay` file; accepts a `full` one.

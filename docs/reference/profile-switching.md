# Profile Switching (design)

Status: **engine and wizard workflow implemented** (4.0.0-beta), in-game-verified in
representative workflows and open to broader public beta validation.
The wizard clearly identifies the profile being edited, protects dirty edits during
target changes, and exposes slot activation controls. Supersedes the shift-layer design
this file previously held. A profile slot binds a physical button to a whole
configuration, swapped live: bindings, macros, axis assignments, curves, deadzones,
aim mode — the lot.

## Known limitations (v1 engine)

- **Throttle hardware range is not re-detected on swap.** `axisMin/axisMax` are
  measured once at control-loop start for the base throttle axis; a slot that binds
  the throttle to a *different physical axis* would normalize against the base
  axis's range. The existing hot-reload has the same limitation, and the cruise
  recipe *unbinds* the throttle (no range needed), so the flagship case is fine.
  Re-detecting on swap is future work.
- **`pollRateHz` and `bAlwaysOn` don't change on swap.** The loop rate and the
  startup arm-state are read once; a slot changing them takes effect on the next
  hot-reload, not on the swap. Both are startup concerns, not per-mode tuning.
- **Wizard UI is partial.** The profile selector, effective-load for editing existing
  profiles, dirty-state protection, edit-target Save with visible results, sparse
  override writes, starter profiles, add-overlay flow, trigger capture, activation
  modes, and import/export controls are implemented. Override markers/revert and
  independent-copy creation remain to be built.

The Flight Axes (Core) page exposes `Injection enabled`, backed by
`[Injection] bEnableInjection`. It is editable in base and profiles; the FPS starter
overlay opens with it disabled while retaining button and macro output.

The Ship Buttons page likewise serializes all `[MenuControls]` reuse switches,
independent horizontal/vertical inversion, and engage/release thresholds. A
selector can therefore expose a dedicated menu-oriented layer without imposing
those choices on the base flight profile.

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
  `AbsoluteHOTAS_Custom.ini`. Always slot 0.
- Up to **16 switch slots**, each naming a profile file (or base), trigger, and
  activation mode. The wizard exposes them through the profile dropdown rather than
  dedicating fixed-width controls to every slot.
- **One profile is active at a time.** Slots do not compose.

### Activation modes

Three modes, chosen per slot to match the hardware — none is the default mechanism;
a rig may use any mix.

| Mode | Driven by | Behavior |
| --- | --- | --- |
| `momentary` | edge | Active while the button is held; on release, return to the profile that was active when it was pressed. For a spring-return shift button. |
| `toggle` | edge | Press flips into this profile; press again returns to the base selection. |
| `selector` | **level** | Active while its button is *held*, evaluated by position not edge. For a rotary/detent switch where each position keeps a distinct button held (e.g. a 5-way where positions map to buttons 3–7). |

**Why `selector` is level-driven, not just another momentary.** A rotary holds its
position, so the binding is a sustained state, not a press. Two things fall out that
an edge model gets wrong:

- **Startup sync.** At launch the switch is already at some position, its button
  already held — there is no press edge. An edge mode would sit on base while the
  detent, LED, and label all say "3." A level read picks up the held position on the
  first armed tick, so software matches the physical indicator immediately.
- **Self-healing.** After the overlay closes or the mod re-arms, a level read
  re-asserts the physical position on its own; an edge mode would need a manual
  wiggle of the switch.

Selector acts only when the position **changes** (not every tick), so an override
layered on top is not stomped. A gap with no position held — a break-before-make
rotary between detents — **holds the last position** rather than flickering through
base. A rotary is always *somewhere*; make one detent your default by pointing its
profile at a near-empty overlay, or a **parked** profile (below) for an off position.

### How the modes compose

All three share one active slot and coexist:

- **The base selection** is the selector's current position, or slot 0 if no selector
  is engaged. Momentary release and toggle-off both return *here* — so a momentary
  shift held over rotary-position-3 returns to 3 on release, not to slot 0.
- Momentary nests: each press records the slot that was active at that instant, so
  stacked momentaries unwind in order.
- **Deactivate returns to base.** The master toggle, the stop binding, the pilot gate,
  and opening the overlay all reset to slot 0; on resume the selector re-syncs to its
  physical position. There is always a way home that does not depend on a latched slot
  being in the state the user thinks it is.

### Binding a trigger — settle-to-quiescence capture

Two switch types the old first-edge-wins capture could not bind:

- a **rotary/selector detent**, which holds its button continuously — the capture
  ignored anything already down, so a resting detent was invisible;
- the **deep stage of a 2-stage trigger**, because the capture committed on the
  first edge (the soft stage) and never saw the hard pull.

Both are solved by one rule: **the last press to land wins, and every new press
resets a short settle timer.** When input goes quiet for the window, whatever was
pressed last commits. A 2-stage pull ends on the hard edge; a rotary you turn (or
sweep) to ends on the final detent. Already-held buttons produce no down-edge, so
they are still ignored — for a detent already at the target, *turn the switch to it*
(the standard binding gesture).

The window is the only tuned quantity, and it trades two ways: tight so a trigger
commits crisply and two edges this close must be one physical pull; generous for a
selector so it bridges the gap between detents as you turn. So it is mode-tuned —
`kButtonCaptureMs` (~50 ms) for buttons/triggers, `kSelectorCaptureMs` (~300 ms) for
selector binds. A short per-edge bounce guard keeps a contact bounce from posing as a
later press, and a max-capture timeout stops a noisy device from spinning forever.
The cost is inherent: the commit trails your last input by the window — imperceptible
at 50 ms, a deliberate "I've stopped turning" pause at 300 ms.

### Parking — `bEnableInjection` as a profile option

There is no special "disabled" mode. Turning flight injection off is a **profile
option**, so a "parked" position is just a profile like any other — one whose config
sets `[Injection] bEnableInjection = false` and carries whatever on-foot button
bindings, custom key outputs, and macros you want. The user defines what parking
means; the engine only tracks the flag.

`bEnableInjection = false` suppresses the flight cluster
(pitch/yaw/roll/throttle/strafe), source-object aim, head pose, and the native
strafe/boost-zone movement requests. Discrete named ship buttons,
`[ButtonExpansion]` custom keys, and macros/turbos remain available according to
that profile. Ship bindings remain gated to ship/targeting context. The six
optional menu bindings and explicit raw targets use keyboard synthesis.

This is the same memory-parking mechanism the pilot gate's `InjectionOnly` mode
uses. The profile flag folds into the one `injectionAllowed` check, so landing on
a parked profile releases the memory gates without touching the master `active`
gate. A parked profile is therefore not a global button/macro safety gate: use
the master deactivate control when every plugin-owned output must be released.

This is why the earlier "disable detent that points at the ScrollLock kill" idea was
dropped. Two tiers cover everything, and they are different tools:

| | Rotary-parked profile | Master toggle (ScrollLock / stop) |
| --- | --- | --- |
| Flight injection | off | off |
| Button/macro outputs | **your parked mappings still run** | all released |
| Native movement modifiers | strafe and boost zone released | all released |
| Granularity | per-profile, surgical | global panic kill |

The 5.0 selected-handler cadence now supplies a reliable automatic pilot/FPS
context, but the gate deliberately does not choose a profile. `InjectionOnly`
parks flight controls while leaving the currently selected profile's buttons and
macros intact. A parked profile remains useful when a physical selector should
also choose an explicit set of on-foot mappings. Automatically mapping `OnFoot`
to a named profile is a separate arbitration feature because it must define how
automatic context interacts with selector, momentary, and toggle profile owners.

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
| Effect | Replaces `_Custom.ini` | Overlays the base, in memory |
| Persistence | Written to disk, survives restart | Transient, gone on restart |
| Disk I/O | Writes the custom file, auto-backs up first | **None** |
| Trigger | Wizard button, deliberate | Physical button, mid-flight |
| Reversible by | Importing something else | Releasing / re-pressing the button |

**A swap must never call `ImportProfile`.** That writes `_Custom.ini`, so a shift
button would overwrite the user's base config with the layer, fire an auto-backup on
every press, and — via `ReloadConfig` → `LoadShipButtonBindings` — re-read
`ControlMap_Custom.txt` from the Documents folder on every press.

## Runtime rules

**Preload, don't reload.** At startup and on wizard Save, build each slot's merged
config once: load main → user → slot file, produce a `Config`, run `ResolveAll` so
device names resolve to indices. A swap is then an index change plus a release of
held outputs. Zero file I/O. This is mandatory, not an optimization: a momentary slot
swaps twice per press.

*What a swap costs.* Index change, release of currently-held native/raw owners,
and a pass over the button table to seed `previousPressed`. There is no file I/O
or named-action `SendInput` dispatch on the swap path.

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

The selected-handler cadence now closes the broad pilot-versus-FPS loop, but it
does not confirm whether an individual game action succeeded. Cruise progression,
targeting state, and other action-specific modes still need their own validated
feedback before an action-coupled profile swap can be transactional.

## INI form

Slot definitions are user-owned → `AbsoluteHOTAS_Custom.ini`. Profile files live in
`Profiles/`, the same directory Export writes to.

Discrete keys per slot, not one packed value — a device-name button ref contains
spaces and can't be split from a mode keyword cleanly. `File` is resolved against
`Profiles/`. `Mode` is `momentary` (default), `toggle`, or `selector`.

```ini
[Profiles]
; A pinky shift and a mode toggle
Slot1File   = precision.ini
Slot1Button = VKBSim Gunfighter@11
Slot1Mode   = momentary

Slot2File   = cruise.ini
Slot2Button = VKBSim Gunfighter@12
Slot2Mode   = toggle

; A 5-position throttle rotary (buttons 3..7), one profile per detent. Position 1
; (button 3) is "parked": its profile disables flight injection for on-foot use.
Slot3File   = parked.ini
Slot3Button = S-TECS SPACE-L THROTTLE STANDARD STEM@3
Slot3Mode   = selector
Slot4File   = combat.ini
Slot4Button = S-TECS SPACE-L THROTTLE STANDARD STEM@4
Slot4Mode   = selector
Slot5File   = precision.ini
Slot5Button = S-TECS SPACE-L THROTTLE STANDARD STEM@5
Slot5Mode   = selector

; Base as a first-class detent: File = (base) selects the base config directly, so a
; rotary always has a "home" position instead of only reaching base by leaving all
; detents. Momentary and selector only — toggling base<->base is a no-op.
Slot6File   = (base)
Slot6Button = S-TECS SPACE-L THROTTLE STANDARD STEM@6
Slot6Mode   = selector
```

**`File = (base)`** makes a slot select the base config itself — essential for a
rotary, which is always in *some* position and so needs an explicit base detent. The
engine models it as a slot whose snapshot equals base, so it flows through the swap
state machine unchanged.

**Legacy/custom keyboard triggers** use `key:<VK>` in `SlotNButton`, and accept a **modifier chord**
as a `+`-joined VK list — `key:0x11+0x31` is Ctrl+1 (`0x11` Ctrl, `0x10` Shift, `0x12`
Alt fold into modifiers; the remaining VK is the key). Chords matter because the
game claims the plain keys: F5/F9 are quicksave/quickload and the F-row is otherwise
spoken for.

Starter keyboard shortcuts are independent toggle fallbacks stored in each
profile's own header:

```ini
[Profile]
sKeyboardShortcut = key:0x11+0x31
```

FPS and Flight Aux default to **Ctrl+1 / Ctrl+2**. The shortcut remains active
alongside the optional `SlotNButton` controller/custom activation. Set
`sKeyboardShortcut = -1` in the profile file to disable it, or replace it with
another chord if another mod or utility has a collision.

```ini
; Profiles/parked.ini — on-foot: no flight injection, but keep menu/on-foot mappings
[Injection]
bEnableInjection = false
; ...plus whatever [ButtonExpansion] custom keys and [Macro:*] you want on foot
```

Slots are read `Slot1..Slot16`; a gap in the numbering is tolerated (a slot with no
`File` key is skipped).

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
| Export Profile | `full` | Materialized effective configuration |
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

The starting point is already there, virtually. The profile editor renders the
effective config, so opening an overlay *looks* like a copy. Editing a field is what
creates the override. Three edits produce a three-line file, and rebinding the throttle
in base still propagates because the overlay never claimed to own it. Per-field visual
distinction between inherited and overridden values remains future UI work.

What the UI needs instead is the inverse of Copy:

- a per-row **override marker** (this value is mine, not inherited),
- a per-row **Revert to base**, and
- an **override count** per slot, so a precision layer reads as "2 overrides" rather
  than looking identical to base.

This also keeps the files legible. A sparse `precision.ini` is two lines and
self-documenting. A copied full profile is a hundred lines whose intent cannot be
recovered without diffing it against base — which matters as soon as someone posts
one on Nexus.

A future **"Detach from base"** action can materialize a real copy for a config the
user deliberately wants to stop tracking. It would write `sKind = full`, the same
artifact type that Export already produces.

## Wizard UI

Design principle: **ignorable, but easy to get into if you look at it.** Profiles
must cost the single-config user nothing, while teaching the interested user by
example rather than by manual.

### Profile context header (every tab)

A compact, collapsed-by-default **"Editing: Main controls"** header sits at the top
of each primary tab: **Flight Controls**, **Flight Modes**, and **Advanced**. When a non-base
profile is selected, its name replaces "Main controls" and the header gains a blue
accent so it is difficult to edit the wrong target by accident. A basic user never
needs to expand it, but always knows where Save & Apply will write.

Profile selection and activation settings are revealed by expanding the header.
Creation, import/export, and reset actions appear only from the Advanced tab's
additional **Manage profiles** disclosure.

Expanded, it shows:

- **A profile dropdown**, defaulting to **Main controls** (the base config).
- **Activation controls** for the selected profile, right beside the dropdown: a
  Bind capture for the trigger and a mode combo — **toggle**, **per-button**
  (momentary), or **selector** — matching the engine's `SwapMode`.
- A dirty marker and guarded Save / Discard / Cancel workflow when changing edit targets.

### The edit target follows the dropdown

Selecting a profile makes it the **Save target**: binding edits on any tab write to
*that* profile. `SaveActiveProfile` routes base edits to `_Custom.ini` and profile
edits to the selected managed profile file.

- Dropdown entry 0, **Main controls**, is base → Save writes `_Custom.ini`,
  as today.
- Any other profile → Save writes a **sparse `Profiles/<name>.ini`**: only the keys
  that differ from base. **This is the hard requirement everything rests on.** A full
  dump turns an overlay into a frozen copy and the inherit model collapses (see "No
  copy base into this slot"). Per-row override markers and **Revert to base** remain
  future UI work; saving a value equal to base removes that override from the sparse file.

### Two seed profiles (teach by example)

When the header is first expanded, the wizard ensures two ready-made **overlay**
profiles exist, so they track Main controls until deliberately changed:

- **FPS** — a parked profile: `{ bEnableInjection = false }`. Teaches the
  injection-disable option and defaults to a Ctrl+1 toggle.
- **Flight Aux** — an empty overlay. Teaches config override: "a second flight
  profile identical to your main one; change only what you want."

Flight Aux defaults to a Ctrl+2 toggle. Both triggers can be rebound or cleared from
the profile context header.

### Creating a profile — overlay vs copy

**Add overlay** creates an empty sparse profile that inherits base and keeps tracking
it (`sKind = overlay`). "Blank" and "inherit my settings" are the same thing here.

**Export base setup** creates a full independent snapshot (`sKind = full`). A full
profile can later be imported as base. Creating an independent copy from an arbitrary
profile and detaching an overlay remain future UI work.

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
- **Selector startup sync:** launch with the rotary already at a detent → that
  profile is active on the first armed tick, no wiggle needed.
- **Selector transition:** turn the dial one detent → single swap to the new
  position; a break-before-make gap does not flicker through base.
- **Selector + shift compose:** hold a momentary shift over a rotary detent →
  override active; release → back to the detent's profile, not slot 0.
- **Selector re-sync:** open the overlay (snaps to base) with the rotary held at a
  detent, close it → the detent's profile re-asserts.
- **Parked profile:** swap to a profile with `bEnableInjection = false` → flight
  injection stops (stick no longer moves the ship; the game reclaims the cluster),
  but that profile's button/macro outputs still fire. Swap away → injection resumes.
- **Parked on foot:** with a parked detent selected, walking around shows no phantom
  movement/sprint interference, while the parked profile's on-foot key mappings work.
- Edit one field under a slot → the slot file gains exactly one key; everything else
  still follows base when base changes.
- Future Revert-to-base UI: reverting a field removes the key from the slot file rather than zeroing it.
- Import refuses an `sKind = overlay` file; accepts a `full` one.

# Input Layers — shift/mode layers (design)

Status: design / planned. A **layer** gives every physical button a second (third,
fourth) meaning, activated by a designated **shift button** — the single most
expected feature from flight-sim binding software (VKB shift registers, Virpil
shift, TARGET layers, Joystick Gremlin modes) that AbsoluteHOTAS doesn't have yet.

A layer is **not** a new binding system. It's a modifier on binding *resolution*:
the lookup key changes from `(device, button)` to `(device, button, layer)`. Ship
actions, custom key bindings, digital axes, and macro triggers all ride the same
button-reference syntax, so they all gain layers from one change.

## Model

- **Base layer** (L0) — everything today. Bindings without a qualifier live here.
- Up to **3 shift layers** (L1–L3), each activated by one shift button. Bounded on
  purpose: the wizard UI stays a 4-pill selector, and nobody sane flies 5 layers.
- A shift button is **consumed** — while designated as a shift it never emits a
  binding of its own. (Shift-with-tap / tempo behavior is a future trigger
  primitive, not smuggled in here.)

### Activation modes

| Mode | Behavior |
| --- | --- |
| `momentary` | Layer active while the shift button is held; on release, return to whatever was active before the press. |
| `toggle` | Press flips between base and this layer; press again to return. |

One layer is active at a time (`activeLayer` in the controller). Momentary
save/restore composes naturally with a latched toggle: hold-shift on top of a
toggled layer returns to the toggled layer on release.

### Resolution semantics

- **Press-time resolution.** The action is chosen at button-down using the layer
  active *at that instant*, and that action owns the button until button-up — even
  if the layer changes mid-hold. No stuck keys, no mid-hold retrigger. (Composes
  with the reference-counted held-key system: the owner is fixed at press.)
- **Fallthrough.** If the active layer has no binding for `(device, button)`, the
  base-layer binding fires. So a shift layer only needs the handful of buttons it
  changes — no duplicating the whole map. (Matches Joystick Gremlin's inherit
  default. A per-layer "opaque" flag is possible later if anyone asks; not in v1.)

## INI form

All layer config is user-owned → `AbsoluteHOTAS_User.ini` (see
[config-layout.md](config-layout.md)). Brand-new feature, so no migration; it's
born inside the split with `iConfigVersion` already stamped.

### Layer definitions

```ini
[Layers]
; Layer<N> = <button ref> <momentary|toggle>
Layer1 = VKBSim Gunfighter@11 momentary   ; pinky shift
Layer2 = VKBSim Gunfighter@12 toggle      ; "mode switch"
```

### Binding qualifier — `:L<n>` suffix on the button reference

Existing refs are `DeviceName@Value` or `#DeviceIndex@Value` (`#` prefix is
already the device-index marker, so the layer rides as a suffix). No qualifier =
base layer; everything shipped today parses unchanged.

```ini
; value side (action -> button): ship actions, macro triggers
iBoostButton = VKBSim Gunfighter@7:L1

; key side (button -> output): [ButtonExpansion]
VKBSim Gunfighter@iButton7:L2 = key:0x1E

; macro trigger
[Macro:GravToShields]
iButton = VKBSim Gunfighter@7:L2
```

Macro *steps* are layer-blind — layers are a trigger-side concept; the emission
engine doesn't change.

## Scope: buttons only (v1)

Layers apply to button resolution. **Layered axis tuning** (e.g. a "precision
mode" that swaps sensitivity/curves while shifted) is a real community pattern but
a separate primitive — it conditions *tuning values*, not binding lookup. Listed
under future work so the decision is recorded, not forgotten.

Also explicitly out of v1, as future **trigger primitives** (same family, each its
own small spec when scheduled):

- **Tempo** — short press vs long press → different actions.
- **Chord triggers** — button A + button B pressed together → action (trigger-side
  chords; output-side chords already exist in macros).
- **Toggle outputs** — press once to hold a key, press again to release.

## Wizard UI

- **Shift Layers section** (top of the Buttons tab): three slots, each with a Bind
  capture button and a momentary/toggle combo. Binding a shift button here removes
  it from normal capture eligibility (it's consumed).
- **Layer selector pills** — `Base | L1 | L2 | L3` — at the top of the Buttons and
  Macros tabs. Binding rows show and edit the selected layer; on a shift layer,
  buttons inherited via fallthrough render greyed with their base binding, so the
  user sees the *effective* map, not a sparse one.
- **Capture is explicit, not modal**: the layer comes from the selected pill, not
  from physically holding the shift during capture. (Capture-while-shifted can't
  distinguish "bind on L1" from "bind the shift button itself".)
- **Live indicator**: the wizard footer shows the currently active layer at
  runtime — doubles as the test surface for momentary/toggle behavior.

## Runtime touch points

- `activeLayer` state + shift-button evaluation at the top of the poll loop,
  before binding dispatch.
- Binding lookup keyed `(device, button, layer)` with base fallthrough; owner
  fixed at press edge.
- Button-reference parser: accept/emit the `:L<n>` suffix (one place, shared by
  all binding sites).

## Test checklist

- Hold an L1-bound action, release the shift mid-hold → action releases cleanly on
  button-up, no stuck key, base binding does not fire.
- Unbound button on L1 falls through to its base binding.
- Momentary shift while a toggle layer is latched → returns to the toggled layer
  on release.
- Macro with `iButton = ...:L2` fires only on L2; same physical button on base
  runs its base binding.
- Wizard capture on a non-base pill writes the `:L<n>` qualifier; base writes none.
- Shift button itself emits nothing on any layer.

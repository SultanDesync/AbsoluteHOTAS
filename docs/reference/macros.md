# Macro Builder (design)

Status: **implemented** (3.1-beta) — engine, `[Macro:*]` parsing, and the wizard
Macros tab. Deferred: the per-macro Test button (see the tab section). A macro lets
one physical button play an ordered sequence of key actions — chords (multi-key),
holds, taps, and turbo (repeat-while-held) — so fiddly multi-press routines become
one press.

## Model

A **macro** = an ordered list of **steps** + a `turbo` flag, fired by a physical button.

A **step**:

| Field | Meaning |
| --- | --- |
| targets | one ship action / key (single) or several joined with `+` (a **chord**, pressed together) |
| action | `tap` (press+release) or `hold` (press, hold, release) |
| amount | `tap`: repeat count · `hold`: duration in ms |
| gap | ms to wait before the next step |

Chord = a one-step macro with N targets. Turbo = repeat the whole macro while the
button is held. Sequence = multiple steps. One engine, no special cases.

## Targeting — rides the ControlMap layer

Steps name **logical ship actions** (`NextSystem`, `IncreaseSystemPower`, …)
wherever possible, resolved through the same binding/ControlMap layer as ship
buttons. So a macro **follows the user's in-game keybinds automatically** — write
it once, it works whether they're on arrows, WASD, or a remap. Raw keys
(`key:0xNN`, `mouse:N`) are allowed for non-ship outputs.

## Emission engine

A per-active-macro state machine ticked by the existing control loop (already
running at the poll rate):

- On press: start at step 0.
- Each tick: advance by **wall-clock** elapsed time (poll-rate-independent);
  press/hold/release targets via `SetOutputHeld` under a per-macro owner ID, so it
  composes with the existing reference-counted held-key system.
- On completion: stop. If `turbo` and still held: loop from step 0.

Durations and gaps are real time, not tick counts.

### Release semantics — fire-and-forget, not hold-to-run

A plain sequence **runs to completion on the press edge**; releasing the trigger
does not abort it. `turbo` is the only mode that tracks the button: it repeats
while held and stops the instant it comes up.

This is deliberate, and was originally the other way round. Aborting on release
made a macro's run length equal to how long the user held the button, which quietly
broke the whole premise ("one press"): Grav → Shields takes ~2.7 s, so a normal
~150 ms thumb press produced exactly **two taps** and stopped. Worse, the abort is
silent and leaks no stuck key — the sequence just ends half-applied, draining the
grav drive without ever filling the shields.

An in-flight sequence is still cancellable: the master toggle, the stop binding,
the pilot gate, and opening the wizard all route through `MacroEngine::ReleaseAll()`.

## Robustness — open-loop reality

Macros are blind key sequences; they can't read game state. Reliability comes from:

- **Edge anchor** — the power selector clamps at the ends, so `NextSystem ×6`
  guarantees the rightmost pool from any starting selection.
- **Over-shoot is safe** — holding Increase/Decrease ~1.2 s fully fills/drains a
  pool; exceeding the cap is harmless.
- **Stable anchors** — ENG/SHD/GRV are always the rightmost three, so anchoring
  *right* avoids the variable weapon pools entirely.

**Gap is the key-*up* window.** A tap is `kTapHoldMs` (40 ms) down, then `gap` ms
up. The gap is what lets the game observe a distinct release before the next press.
5 ms is snappy and holds up because the engine consumes discrete input events rather
than sampling key state per frame. It is, however, the first knob to suspect if
presses are ever dropped: a dropped `NextSystem` breaks the right-edge anchor, and
the macro then drains whatever pool it happens to be sitting on instead of GRV —
turning a harmless overshoot into a wrong-pool drain. Raise the gap before anything
else.

## INI form

One section per macro, indexed steps (maps 1:1 to the wizard tab):

```ini
[Macro:GravToShields]
iButton = VKBSim Gunfighter@7
bTurbo  = false
; Step<N> = <targets> <tap|hold> <amount> [gapMs]
Step0 = NextSystem tap 6 5
Step1 = DecreaseSystemPower hold 1200
Step2 = PreviousSystem tap 1 5
Step3 = IncreaseSystemPower hold 1200
```

Targets: a ship action id, a `key:0xNN` / `mouse:N`, or several joined with `+`
for a chord (`key:0x2A+key:0x11` = L Shift + W).

## Wizard "Macros" tab — dedicated tab

Macros get their **own tab**, not a section on the Buttons tab. A macro editor is a
list-of-lists (per macro: button, turbo flag, N step rows) — a full-height editor,
and the Buttons tab already carries four collapsing headers. It also gives a clean
1:1 support story: *Macros tab ↔ `[Macro:*]` sections in
`AbsoluteHOTAS_Custom.ini`* (see [config-layout.md](config-layout.md)).

Renames that landed with it (the old name promised what it did not deliver):

- Tab "Buttons & Macros" → **"Buttons"**.
- Its "Custom Keyboard Macros" header → **"Custom Key Bindings"** (those are simple
  one-button→one-output bindings, not macros).

Tab contents: list macros; per macro: name, trigger button, turbo toggle, and
ordered step rows (target picker + tap/hold + amount + gap + reorder/delete). An
"Add Grav → Shields Preset" button seeds the worked example below.

**Step row shape — flat list, targets inline.** One row per step. A step's targets
render as inline combos joined by `+`, with a `+` button to add one. So the common
case (one target) is a single combo, and a chord grows horizontally without a nested
editor. Chord-ness is a property of the row, not a different kind of row.

**The tab edits the INI, not the engine's model.** `MacroStep` holds *resolved*
`ShipOutput`s — the token `NextSystem` is gone by the time the engine has it. If the
wizard round-tripped through `MacroEngine`, saving would rewrite every action target
as a raw `key:0xNN` and silently destroy the control-map-follows-your-rebinds
property. So the wizard parses and writes the custom INI's macro sections itself, at the
token level. Two parsers, one grammar; they must stay in step.

**Half-built macros persist.** A macro with no trigger button or no steps is still
written (as `iButton = -1` / no `Step` keys). The engine already ignores such macros
at load, and the wizard reloads from this file after every Save — so dropping them
would make a macro vanish while the user was still building it. Only unnamed and
duplicate-named macros are skipped, since neither can be an INI section; the tab
flags both in red.

**Save is a targeted rewrite** of `[Macro:*]` sections in the custom file. The wizard
preserves every non-macro section while removing stale macro sections before writing
the current macro rows.

**Deferred: the Test button.** Firing a macro from the wizard would emit keys into
the game behind the open overlay (macros are suppressed while it's up), which is
both awkward to reason about and easy to mistake for a stuck key. Bind, Save, and
try it in flight instead.

Profiles: a macro belongs to whichever profile defines it, so a
[profile swap](profile-switching.md) changes the whole macro set. No per-macro
qualifier is needed — the tab shows the same slot pills as the Buttons tab, and
editing under a slot writes that slot's profile. A swap releases any in-flight macro.

## Worked example — "Grav → Shields"

The flagship demo: dump the (combat-useless) grav-drive power into shields in one
press. Anchors to the right edge, so it only ever touches the always-rightmost
GRV/SHD pools — robust regardless of weapon loadout.

```ini
[Macro:GravToShields]
iButton = <your button>
bTurbo  = false
Step0 = NextSystem tap 6 5             ; anchor to right edge = GRV (selector clamps)
Step1 = DecreaseSystemPower hold 1200  ; drain grav drive -> shared surplus
Step2 = PreviousSystem tap 1 5         ; GRV -> SHD (always adjacent)
Step3 = IncreaseSystemPower hold 1200  ; pour the surplus into shields
```

Exercises every engine primitive: timed **taps** (navigation), **holds** with
duration (drain/fill), and an **ordered dependency** (must drain before filling —
no surplus otherwise). Power-user variant: drain GRV **and** ENG, then fill SHD.

## Power UI reference (vanilla)

6 pools — `LAS BAL MSL | ENG SHD GRV` (weapons | ship systems). Each holds up to
**12 pips**; reactor total ~14. Up/Down move pips between the selected pool and a
shared **surplus**; Left/Right select and **clamp** at the edges. Holding
Increase/Decrease ~**1.2 s** fully fills/drains the selected pool.

## Navigation reliability (resolved)

Unpopulated weapon pools **collapse** — they're skipped in navigation, so the
left-side count varies by loadout. But the selector clamps and over-pressing is
harmless, so anchoring absorbs it completely:

- **Right anchor** (`NextSystem ×6`) always lands on GRV; ENG/SHD/GRV are a fixed
  trio from the right regardless of weapon count. Right-anchored macros are bulletproof.
- **Left anchor** (`PreviousSystem ×6`) always lands on the leftmost populated pool.
- The only thing that's *not* reliable open-loop is hitting a **specific weapon
  type** by index (its position shifts with loadout). Prefer right-anchoring; reserve
  weapon-specific presets for "sweep all weapons" style macros that don't need a
  fixed index.

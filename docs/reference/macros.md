# Macro Builder (design)

Status: design / in development for 3.1. A macro lets one physical button play an
ordered sequence of key actions — chords (multi-key), holds, taps, and turbo
(repeat-while-held) — so fiddly multi-press routines become one press.

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
- On release: stop, release all held keys. If `turbo` and still held: loop.

Durations and gaps are real time, not tick counts.

## Robustness — open-loop reality

Macros are blind key sequences; they can't read game state. Reliability comes from:

- **Edge anchor** — the power selector clamps at the ends, so `NextSystem ×6`
  guarantees the rightmost pool from any starting selection.
- **Over-shoot is safe** — holding Increase/Decrease ~1.2 s fully fills/drains a
  pool; exceeding the cap is harmless.
- **Stable anchors** — ENG/SHD/GRV are always the rightmost three, so anchoring
  *right* avoids the variable weapon pools entirely.

## INI form

One section per macro, indexed steps (maps 1:1 to the wizard tab):

```ini
[Macro:GravToShields]
iButton = VKBSim Gunfighter@7
bTurbo  = false
; Step<N> = <targets> <tap|hold> <amount> [gapMs]
Step0 = NextSystem tap 6 50
Step1 = DecreaseSystemPower hold 1200
Step2 = PreviousSystem tap 1 60
Step3 = IncreaseSystemPower hold 1200
```

Targets: a ship action id, a `key:0xNN` / `mouse:N`, or several joined with `+`
for a chord (`key:0x2A+key:0x11` = L Shift + W).

## Wizard "Macros" tab

List macros; per macro: assign button, turbo toggle, ordered step rows (target
picker + tap/hold + amount + gap), Test button. Writes the `[Macro:*]` sections.

## Worked example — "Grav → Shields"

The flagship demo: dump the (combat-useless) grav-drive power into shields in one
press. Anchors to the right edge, so it only ever touches the always-rightmost
GRV/SHD pools — robust regardless of weapon loadout.

```ini
[Macro:GravToShields]
iButton = <your button>
bTurbo  = false
Step0 = NextSystem tap 6 50            ; anchor to right edge = GRV (selector clamps)
Step1 = DecreaseSystemPower hold 1200  ; drain grav drive -> shared surplus
Step2 = PreviousSystem tap 1 60        ; GRV -> SHD (always adjacent)
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

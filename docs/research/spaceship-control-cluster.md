# Starfield Spaceship Control Cluster Research

This note summarizes the research that led to the current AbsoluteHOTAS flight-control hook strategy. It is intentionally sanitized for publication: it describes reproducible structures and validation rules, but it does not include local machine paths, raw private trace logs, or broad experimental dumps.

## Summary

Starfield's ship movement state contains a compact downstream control cluster that receives signed flight-control values. The cluster is populated by engine writer blocks every frame. Direct polling-thread writes into the cluster can be visible in memory but are not always authoritative. The stable strategy is to hook the engine writer path and replace or gate selected stores at engine timing.

The validated downstream cluster lanes are:

| Cluster lane | Meaning |
| ---: | --- |
| `+0x58` | Shared roll / lateral output lane |
| `+0x5C` | Vertical strafe output lane |
| `+0x60` | Signed yaw output |
| `+0x64` | Signed pitch output |
| `+0x68` | Signed throttle target / reverse-capable throttle lane |
| `+0x6C` | Signed throttle companion / current-state lane |

The current public plugin keeps throttle on the downstream cluster and uses gate-inject hooks for rotational control.

## Why Engine-Timed Hooks Matter

The important discovery was that memory visibility and ship authority are different things. A value written from a polling thread may appear at a candidate address but still lose to the next engine writer or bypass timing-sensitive logic. Pitch, yaw, and roll became reliable when the plugin hooked the writer sequence itself and substituted values at the same point Starfield was already writing them.

The production rule is:

- Capture candidates from real writer execution.
- Validate candidate bases by nearby sane signed-axis values.
- Replace only the minimal store needed for the controlled lane.
- Restore original bytes on uninstall.
- Avoid hardcoded absolute process addresses.

## Validated Writer Forms

The rotational writer block has been observed writing the downstream cluster through a small sequence of scalar float stores. The relevant forms are:

```asm
lea    rdx,[rax+58]
vmovss [rdx],xmm0      ; roll / lateral
vmovss [rdx+04],xmm2   ; vertical strafe
vmovss [rax+60],xmm3   ; yaw
vmovss [rax+64],xmm1   ; pitch
```

In the current hook strategy:

- roll is gated at the writer that stores the `+0x58` lane
- yaw is gated at the writer that stores the `+0x60` lane
- pitch is gated at the writer that stores the `+0x64` lane
- throttle remains on `+0x68`, with companion behavior at `+0x6C` when needed

The hook installer searches for narrow instruction patterns in the executable text segment and rejects duplicates or unsafe candidates.

## Candidate Validation

Candidate cluster bases should be treated as valid only when nearby lanes look like live ship-control state:

| Check | Expected shape |
| --- | --- |
| Roll/lateral | finite signed float, usually `-1.0..+1.0` |
| Yaw | finite signed float, usually `-1.0..+1.0` |
| Pitch | finite signed float, usually `-1.0..+1.0` |
| Throttle | finite throttle-like float, commonly `0.0..1.0`, reverse-capable under vanilla input |
| Companion lanes | nearby values should phase-match the primary lane during vanilla input |

Do not validate against one magic value. Runtime throttle state can legitimately vary depending on ship state, flight mode, and timing. The useful signal is the structure: several adjacent lanes phase together with isolated ship inputs.

## Upstream Source Object

Later research found an upstream source object that feeds the downstream control cluster. The source object appears to hold processed flight-intent values after Starfield input mapping but before the final downstream copy.

Observed source-to-cluster mapping:

| Source lane | Cluster lane | Meaning |
| ---: | ---: | --- |
| `source+0x38` | `cluster+0x58` | Roll / lateral |
| `source+0x3C` | `cluster+0x5C` | Vertical strafe |
| `source+0x54` | `cluster+0x60` | Yaw |
| `source+0x58` | `cluster+0x64` | Pitch |
| `source+0x1E8` | cluster base pointer | Downstream control cluster |

This suggests a likely pipeline:

```text
hardware / keyboard / controller input
    -> Starfield input mapping
    -> processed ship intent
    -> source object lanes
    -> downstream control cluster
    -> physics/control application
```

The source object may eventually be the better insertion point for preserving more vanilla camera lead, assist curves, or mode behavior. It is not yet the public default because the current cluster writer-gate path has broader validation.

## DirectInput And Synthetic Input

Another practical finding is that Starfield remains willing to expose and accept DirectInput device state when the plugin polls it directly, even in setups where external keyboard/mouse emulation is inconsistent. This is why AbsoluteHOTAS treats DirectInput as the reliable hardware ingress path and keeps synthetic keyboard/mouse output as a final compatibility layer for game actions that still need Starfield's binding system.

Observed behavior:

- DirectInput axes/buttons can be read by the plugin without depending on Steam Input's controller translation layer.
- This has been validated by users on Proton/Linux, where the plugin path can let Steam Deck users keep trackpads/sticks available without forcing Steam Input to impersonate a gamepad for flight controls.
- Joystick Gremlin keyboard/mouse simulation can work for simple cases, but Starfield may receive it inconsistently depending on focus, input mode, Steam Input, Proton/desktop routing, and whether the game is switching between gamepad and KBM UI contexts.
- Mixed gamepad/KBM paths can cause Starfield UI/input flicker, while plugin-polled DirectInput avoids that input-mode tug of war for the hardware side.
- Starfield registers synthetic keyboard/mouse actions more reliably when they are held across at least a normal input frame; very short pulses can be missed.

Design implication:

- Use DirectInput polling for HOTAS/HOSAS hardware state whenever possible.
- Use `SendInput` from the plugin only for discrete Starfield actions that need to pass through vanilla bindings, and mirror physical button hold/release instead of emitting instant pulses.
- Prefer explicit Starfield binding alignment for those synthetic outputs, either by choosing vanilla outputs or by documenting/customizing matching `ControlMap_Custom.txt` bindings.
- Keep Steam Input out of the critical flight path unless the user has a specific reason to use it.

## Roll And Strafe Share A Lane

Roll and lateral strafe converge on the same signed lane in both the source object and downstream cluster. This is why AbsoluteHOTAS avoids owning the roll/lateral lane when the analog roll axis is centered: vanilla lateral strafe can continue to pass through while HOTAS roll is idle.

Important implication:

- `source+0x38` / `cluster+0x58` should not be treated as independent roll and lateral strafe channels.
- A public analog strafe feature needs explicit ownership rules so it does not freeze or suppress normal roll/lateral behavior.

## Reverse / Brake

Reverse is not merely a separate keyboard action. Under sustained vanilla reverse input, the throttle lanes can become signed:

| Lane | Role |
| ---: | --- |
| `cluster+0x68` | signed throttle target |
| `cluster+0x6C` | signed throttle companion/current lane |

For the stable public build, unipolar forward throttle remains the default and vanilla reverse/brake handoff is preserved. Signed negative throttle is a plausible future mode, but it needs feel testing before becoming a default because it may bypass Bethesda's ramp or assist behavior.

## Patch Resilience

The approach is intended to survive minor patches better than absolute offsets:

- scan executable text for narrow writer patterns
- validate nearby lane structure before enabling injection
- accept that optional hooks may safely skip on unknown runtimes
- keep runtime version checks conservative
- use Address Library/SFSE compatibility checks for loader safety

Patch compatibility should still be validated after each Starfield/SFSE update. A skipped optional hook is safer than a bad write into the wrong structure.

## Current Open Questions

- Whether upstream source-object injection preserves better vanilla camera and assist behavior than downstream cluster gates.
- Whether direct signed-throttle writes should become a supported reverse mode.
- Whether adjacent source-object fields contain boost, flight mode, power allocation, or weapon/targeting state.
- Whether future builds should add a separate game-data bridge for richer in-game state, or keep the public release strictly DLL/INI-only.

## Publication Boundary

Good public material:

- validated lane maps
- writer-pattern strategy
- candidate validation rules
- safety and patching principles
- known limitations

Keep in the experimental workspace:

- broad raw trace dumps
- absolute process addresses without context
- failed hook branches
- machine-specific paths
- unreleased test builds and binary artifacts

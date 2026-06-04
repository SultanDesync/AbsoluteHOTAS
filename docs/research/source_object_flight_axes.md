# Source Object Flight Axes

Date: 2026-05-05

## Purpose

This note captures the post-1.0b flight-axis research for an experimental AbsoluteHOTAS build. The main finding is that the previously used control cluster is a downstream output target, while an upstream source object feeds that cluster every frame. The source object is a better candidate for pitch/yaw/roll/strafe experiments than direct writes into the downstream cluster.

The recommended implementation base is AbsoluteHOTAS 1.0b, commit `ab5e1bf` (`Backup current HOTAS build before pilot-state experiments`), because it contains the lateral-strafe freeze mitigation and avoids the later abandoned pilot-state work.

## Existing 1.0b Baseline

1.0b added authoritative throttle, pitch, yaw, and roll using the validated engine writer path.

The important 1.0b lateral-strafe behavior is not full analog lateral strafe support. It is a bug mitigation: roll only owns the shared `+0x58` writer while the roll axis is displaced, so vanilla or Joystick Gremlin lateral strafe can pass through while roll is centered.

This should remain in the bugfix/stable line.

## Downstream Control Cluster

The control cluster remains valid and operational:

| Lane | Offset | Meaning |
| --- | ---: | --- |
| Roll / lateral | `+0x58` | Shared signed output lane |
| Vertical strafe | `+0x5C` | Vertical translation output |
| Yaw | `+0x60` | Signed yaw output |
| Pitch | `+0x64` | Signed pitch output |
| Signed throttle / reverse | `+0x68` | Throttle target / target velocity proportion, supports `-1.0..+1.0` |
| Signed throttle companion | `+0x6C` | Companion/current-state throttle value, mirrors signed throttle under vanilla input |

Throttle is still a good use of this cluster. It behaves like a traditional space-sim throttle target: a persistent proportion of top speed / target thrust. `ReverseTrace` later confirmed that the same pair supports signed reverse when vanilla `S` is held long enough.

Pitch, yaw, roll, and strafe are less ideal at this layer. Injecting into the cluster gives authority, but can bypass upstream flight effects such as camera lead, turn-direction camera rotation, assist curves, mode-dependent roll/strafe behavior, and input blending.

## Writer Block Finding

The known writer block copies values from a source object into the downstream control cluster.

Observed writer pattern:

```asm
vmovss xmm2,[r13+3C]
vmovss xmm0,[r13+38]
vmovss xmm3,[r13+54]
vmovss xmm1,[r13+58]
mov    rax,[r13+1E8]
lea    rdx,[rax+58]
vmovss [rdx],xmm0
vmovss [rdx+04],xmm2
vmovss [rax+60],xmm3
vmovss [rax+64],xmm1
```

Interpretation:

| Source object lane | Downstream lane | Meaning |
| ---: | ---: | --- |
| `r13+0x38` | `cluster+0x58` | Roll / lateral |
| `r13+0x3C` | `cluster+0x5C` | Vertical strafe |
| `r13+0x54` | `cluster+0x60` | Yaw |
| `r13+0x58` | `cluster+0x64` | Pitch |
| `r13+0x1E8` | cluster base pointer | Downstream control cluster |

`r13+0x54` was independently confirmed in Cheat Engine as a yaw value in the `-1.0..+1.0` range.

## Probe Validation

`ClusterDump` was extended to capture the source object (`r13`) from the writer block and dump both source and downstream lanes.

Representative validated capture:

```text
sourceBase = 0x7FF782C69260
source+0x1E8 = 0x2497EA0AF70
clusterBase = 0x2497EA0AF70

SourceYaw   source+0x54 = -0.585272
ClusterYaw cluster+0x60 = -0.585272

SourcePitch   source+0x58 = -0.810837
ClusterPitch cluster+0x64 = -0.810837

SourceRoll/Lateral   source+0x38 = -1.000000
ClusterRoll/Lateral cluster+0x58 = -1.000000

SourceVerticalStrafe   source+0x3C = 1.000000
ClusterVerticalStrafe cluster+0x5C = 1.000000
```

This validates that the source object is the immediate upstream source for the cluster, not an incidental unrelated structure.

## Roll / Strafe Classifier

`RollStrafeTrace` was added after the first anchored roll trace showed that roll and lateral strafe share the same signed lane. The probe uses the captured source object, scans a local window, and cycles:

```text
neutral
A / D
Space + A / Space + D
Space + W / Space + S
Space only
```

This was based on the working theory that Space is the vanilla strafe modifier and WASD provides lateral / vertical strafe directions while modified.

Representative result:

```text
#1 source+0x3C label=verticalOnly
   rollL/R=0/0 strafeL/R=0/0 strafeF/B=1/-1 mod=0

#2 source+0x60 label=verticalOnly
   rollL/R=0/0 strafeL/R=0/0 strafeF/B=1/-1 mod=0

#3 source+0x2D4 label=rollOnly
   rollL/R=1/-1 strafeL/R=0/0 strafeF/B=0/0 mod=0

#4 source+0x38 label=sharedRollLateral
   rollL/R=-1/1 strafeL/R=-1/1 strafeF/B=0/0 mod=0

#5 source+0x5C label=sharedRollLateral
   rollL/R=-1/1 strafeL/R=-1/1 strafeF/B=0/0 mod=0
```

Interpretation:

| Source object lane | Observed signature | Current role |
| ---: | --- | --- |
| `source+0x38` | roll `-1/+1`, lateral strafe `-1/+1` | Primary shared roll / lateral source lane |
| `source+0x3C` | vertical strafe `+1/-1` | Primary vertical strafe source lane |
| `source+0x5C` | roll `-1/+1`, lateral strafe `-1/+1` | Secondary mirror / companion for shared roll-lateral intent |
| `source+0x60` | vertical strafe `+1/-1` | Secondary mirror / companion for vertical strafe intent |
| `source+0x2D4` | roll-only `+1/-1`, no lateral strafe response | Roll-only inverse marker, not a write target |

The important outcome is that vanilla roll and vanilla lateral strafe both converge at `source+0x38`, while vanilla vertical strafe converges at `source+0x3C`. This means the source object contains a compact flight-intent cluster for signed roll/lateral and vertical translation before the final downstream control-cluster copy.

## Theory

The source object is likely a processed flight-intent object or controller state object. It is upstream of the downstream control cluster, but probably still downstream of raw hardware/controller input.

Expected pipeline:

```text
hardware / keyboard / controller input
    -> Starfield input mapping
    -> flight intent, assists, mode logic, camera behavior
    -> source object lanes
    -> downstream control cluster
    -> physics/control application
```

The current working theory is that `source+0x38` and `source+0x3C` are near-optimal analog insertion points for the mod's strafe/roll goals because Starfield's own split digital keyboard actions converge there:

- bare `A/D` writes roll into `source+0x38`
- `Space+A/D` writes lateral strafe into `source+0x38`
- `Space+W/S` writes vertical strafe into `source+0x3C`

This does not prove that the source object is the raw input layer. It more likely sits after input mapping and semantic selection. However, for AbsoluteHOTAS, that may be the right layer: it unifies the engine's digital commands into signed flight-intent lanes without requiring us to own the lower-level keyboard/controller mapping path.

`source+0x2D4` is useful as a research marker because it tracks roll only and does not respond to lateral strafe. It should not be used as an injection lane, especially because its sign is inverted relative to `source+0x38`.

## Implementation Direction

Build an experimental fork from 1.0b with clear authority layers:

1. Keep throttle at the downstream cluster:
   - write `cluster+0x68`
   - update `cluster+0x6C` as current/readback companion if needed
   - retain existing signpost/cluster capture safety logic

2. Move pitch/yaw/roll experiments to the source object:
   - yaw -> `source+0x54`
   - pitch -> `source+0x58`
   - roll/lateral -> `source+0x38`
   - vertical strafe -> `source+0x3C`
   - ignore `source+0x2D4` for writes; treat it as a roll-only diagnostic marker
   - treat `source+0x5C` and `source+0x60` as secondary mirrors until a write test proves they are required

3. Capture the source object from the known writer block:
   - identify exact writer block by AOB
   - capture `r13` as `sourceBase`
   - capture `[r13+0x1E8]` as `clusterBase`
   - validate both objects with finite signed lane values before enabling injection

4. Gate source injection per axis:
   - retain the 1.0b roll deadzone / ownership gate
   - write analog roll to `source+0x38` only while the roll axis is meaningfully displaced
   - let vanilla lateral strafe pass through `source+0x38` while analog roll is centered
   - write analog vertical strafe to `source+0x3C` only when the mod is explicitly providing vertical strafe
   - keep yaw and pitch on `source+0x54` / `source+0x58` for the first source-injection experiment

## Reverse / Brake

`ReverseTrace` was added to resolve whether reverse is a separate brake/impulse path or a signed extension of the throttle cluster. The probe first drives forward with `W`, forcibly resets `cluster+0x68` and `cluster+0x6C` to `0.0`, then holds bare `S` and compares the result with `Space+S`.

The first short-hold test (`ReverseHoldMs=1500`) was ambiguous: bare `S` primarily appeared as back/vertical strafe through `source+0x3C`, `source+0x60`, and `cluster+0x5C`, while throttle remained at zero.

The second test held bare `S` for five seconds after forced throttle zero. That produced the decisive result:

```text
cluster+0x68 neutral=0.000527 forward=0.498096 forcedZero=0.000000 bareS=-1.000000 recovery=0.000000 spaceS=0.000000
cluster+0x6C neutral=0.000527 forward=0.498096 forcedZero=0.000000 bareS=-1.000000 recovery=0.000000 spaceS=0.000000

source+0x3C neutral=0.000000 forward=1.000000 forcedZero=0.000000 bareS=-1.000000 recovery=0.000000 spaceS=-1.000000
cluster+0x5C neutral=0.000000 forward=1.000000 forcedZero=0.000000 bareS=-1.000000 recovery=0.000000 spaceS=-1.000000
```

Interpretation:

| Input phase | Observed behavior |
| --- | --- |
| `W` | drives forward/back strafe lane and positive throttle lanes |
| forced zero | resets `cluster+0x68` / `cluster+0x6C` to `0.0` |
| short bare `S` | initially appears as back/vertical strafe with no immediate signed throttle |
| sustained bare `S` | ramps signed throttle lanes to `-1.0` |
| `Space+S` | drives back/vertical strafe lane but leaves throttle lanes at `0.0` |

Conclusion: reverse is a native signed throttle state on `cluster+0x68` and `cluster+0x6C`, with delayed activation / ramp behavior under vanilla `S`. The source strafe lane still reports back/vertical intent during `S`, so the movement cluster has both a strafe-like back lane and a signed throttle/reverse lane.

Implementation options:

1. Stable default:
   - keep 1.0b unipolar throttle `0.0..1.0`
   - preserve vanilla `S` handoff for reverse/brake
   - this retains Bethesda's delayed reverse ramp and brake behavior

2. Experimental signed-throttle mode:
   - write `cluster+0x68` and `cluster+0x6C` as `-1.0..+1.0`
   - map a centered or detented physical throttle to reverse/forward pilot intention
   - test whether direct negative writes feel like native reverse or bypass useful ramp/assist behavior

This gives two viable design philosophies: simulate vanilla thrust/brake impulses with key handoff, or write absolute pilot intention as a signed throttle target.

## First Experimental Test

The first build should be a minimal source-injection build:

1. Capture `sourceBase` and `clusterBase`.
2. Poll HOTAS yaw/pitch/roll as usual.
3. Write yaw to `sourceBase+0x54`.
4. Write pitch to `sourceBase+0x58`.
5. Write roll to `sourceBase+0x38` only while outside the roll deadzone.
6. Leave `sourceBase+0x38` untouched while roll is centered so vanilla lateral strafe can pass.
7. Optionally test vertical strafe at `sourceBase+0x3C`.
8. Do not install downstream yaw/pitch/roll override gates.
9. Keep throttle using the existing cluster throttle path.
10. Compare in-game feel against 1.0b cluster-level injection:
   - does the ship respond with the same authority?
   - does camera lead / turn-direction camera rotation return?
   - do damping and assist effects feel more vanilla?
   - does input remain stable across cockpit/third-person, boost, and menu transitions?

Success condition: yaw/pitch remain authoritative while restoring more native Starfield camera/flight feel.

Roll/lateral success condition: analog roll owns `source+0x38` only while displaced, and vanilla lateral strafe still works through the same lane while roll is centered.

Vertical strafe success condition: analog vertical strafe at `source+0x3C` behaves like vanilla `Space+W/S`.

Reverse success condition: either signed cluster throttle produces stable backward thrust with correct readback, or the build explicitly preserves vanilla reverse/brake handoff and documents it as the supported reverse path.

## Known Limitations

- `source+0x38` is still a shared roll/lateral lane. Do not treat source injection as a solution for simultaneous independent roll and lateral strafe.
- `source+0x5C` and `source+0x60` mirror the roll/lateral and vertical strafe signatures, but their role is not yet proven. Prefer `source+0x38` and `source+0x3C` until write tests show otherwise.
- `source+0x2D4` is a roll-only inverse marker. It is useful for tracing the roll path but is not an analog insertion target.
- Reverse is mapped as signed throttle on `cluster+0x68` / `cluster+0x6C`, but direct negative writes still need feel testing before becoming a public default.
- The source object address is runtime-specific. Always capture it from the writer block; never hardcode absolute addresses.
- CE data breakpoints on hot lanes may crash the engine. Prefer probe capture/logging over data breakpoints for routine validation.
- The source object is probably still not raw input. If source injection still bypasses desired camera behavior, the next research target is the writer of `source+0x54` / `source+0x58`.

## Nearby Ship-Function Speculation

Because the source object and downstream cluster contain a compact set of ship movement intentions, it is plausible that adjacent fields in the same object graph hold other ship-control state: boost, brake/reverse ramp state, flight-assist flags, landing/takeoff state, power allocation, weapon group triggers, targeting, or mode bits.

This is speculation, not part of the validated movement map. The strongest reason to look nearby is that `source+0x5C`, `source+0x60`, and `source+0x2D4` already show secondary/mirror/marker behavior adjacent to the primary movement lanes. Future probes should keep the same discipline used here: isolate one vanilla action, force known movement lanes to neutral when possible, and rank candidates by phase signatures rather than broad logging.

### Aiming Reticle & Accumulator Pathway Findings (2026-06-03)

Recent testing and Cheat Engine analyses confirmed that the visual ship aiming reticle is driven by accumulator variables located upstream of the final steering intent:
- **`sourceBase + 0x44`**: Yaw / Horizontal Raw Input (Float) - *Used in Gamepad Mode*
- **`sourceBase + 0x48`**: Pitch / Vertical Raw Input (Float) - *Used in Gamepad Mode*
- **`sourceBase + 0x4C`**: Yaw / Horizontal Accumulator (Float) - *Used in Mouse/Keyboard Mode*
- **`sourceBase + 0x50`**: Pitch / Vertical Accumulator (Float) - *Used in Mouse/Keyboard Mode*
- **`sourceBase + 0x54`**: Yaw / Horizontal Intent (Float, normalized `[-1.0f, 1.0f]`)
- **`sourceBase + 0x58`**: Pitch / Vertical Intent (Float, normalized `[-1.0f, 1.0f]`)

### Scales & Behaviors

#### Mouse & Keyboard Mode (Relative Pathway)
- **Accumulator Range**: Both accumulators (`+0x4C` / `+0x50`) scale between **`-200.00f` and `200.00f`**, representing the displacement coordinates of the visual HUD reticle relative to the center.
- **Steering Intent Mapping**: The game reads the accumulator values and maps them to normalized steering intent values (`+0x54` / `+0x58`) in the `[-1.0f, 1.0f]` range (e.g., `114.47f` accumulator deflection maps to `~0.37f` intent; `159.75f` maps to `0.88f`).
- **Persistence**: Stopping mouse movement leaves the accumulator at its current deflection (e.g. `114.47f`), maintaining both visual reticle offset and ship rotation.

#### Gamepad / vJoy Mode (Absolute Pathway)
- **Raw Input Range**: When using a controller/vJoy, the game completely bypasses the accumulators (`+0x4C` and `+0x50` remain exactly `0.000000`). Instead, it writes stick deflection directly to `SourceRawYaw` (`+0x44`) and `SourceRawPitch` (`+0x48`) scaling between **`-2.00f` and `2.00f`**.
- **Steering Intent Mapping**: The game calculates the final steering intent (`+0x54` / `+0x58`) directly from the raw inputs, clamping a `+/-2.00f` raw deflection to `+/-1.00f` intent.
- **Auto-Decay**: Releasing the stick to neutral writes `0.000000` to the raw inputs, instantly centering the visual reticle and stopping ship rotation.

### Injection Heuristics
1. **Direct Intent Write (Fail)**: Injecting directly into Yaw/Pitch Intent (`+0x54` / `+0x58`) succeeds in turning the ship but leaves the HUD reticle centered.
2. **Absolute Joystick Injection (Winner)**: Injecting scaled physical stick values (`joystick_value * 2.0f`) into the Raw Input offsets (`+0x44` / `+0x48`) is the ideal injection pathway. It leverages the game's built-in gamepad processing to automatically drive both the visual reticle deflection and the physical ship rotation, while naturally resetting to center when the stick is neutral.

### Implementation Status (v2.3.1+)

The AbsoluteHOTAS codebase targets the **Raw Input pathway** (`+0x44` / `+0x48`) when `bSourceObjectAim=true`. `ThrottleHook::SetSourceObjectAim` writes `pitch/yaw * fAimSensitivity * 2.0f` (clamped to `[-2.0, +2.0]`) into these offsets via SEH-guarded polling writes each control loop iteration. When source aim is active, the cluster-level rotational gate overrides for yaw (`+0x60`) and pitch (`+0x64`) are **suppressed** to avoid conflicting with the engine's own intent values derived from the raw inputs.

## Probe Notes

`StarfieldFlightControlProbe` has a `ClusterDump` mode for validating this map:

```ini
[Probe]
Mode=ClusterDump
ClusterAnchor=0

[ClusterDump]
AutoStart=1
SnapshotButtons=37,38,39
SnapshotVirtualKeys=105,57,33
```

With `ClusterAnchor=0`, the probe installs pass-through capture hooks and logs both source and cluster maps. With `ClusterAnchor` set, it runs passively and logs downstream cluster addresses only.

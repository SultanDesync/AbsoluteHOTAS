# Operating Reverse Flight Without Keyboard Emulation ('S')

Historically, reverse flight in Starfield required emulating the physical `S` key. This was because the game’s flight engine logic handles reverse as a combination of deceleration (braking) intent and signed negative throttle. Holding `S` eventually transitions the flight controller state machine into reverse.

Through diagnostic probing, we have validated an authoritative, direct-memory method to engage and operate reverse flight purely through the plugin, without any key emulation.

---

## Memory Architecture & The Handoff Problem

The ship's flight input is processed across two main structures:
1. **Upstream Source Object** (`sourceBase`): Contains the raw, unfiltered pilot intentions before assists and curves.
2. **Downstream Control Cluster** (`clusterBase`): Contains the final calculated values sent to the physics engine.

During keyboard-driven reverse (sustained `S` key), the game drives the following offsets negative:
- `source+0x3C` (Upstream Vertical Strafe / Decel) $\rightarrow$ `-1.0`
- `cluster+0x5C` (Downstream Vertical Strafe / Decel) $\rightarrow$ `-1.0`
- `cluster+0x68` (Downstream Throttle Target) $\rightarrow$ `-1.0`
- `cluster+0x6C` (Downstream Effective Throttle) $\rightarrow$ `-1.0`

### The Frame-by-Frame Overwrite
Attempting to write a negative value to `cluster+0x5C` from a background thread fails because the game's **rotational writer block** runs in the main engine loop every single frame, copying `source+0x3C` to `cluster+0x5C`. Because `source+0x3C` remains at `0.0` (as no physical keys are pressed), the game instantly overwrites the negative value in `cluster+0x5C` back to `0.0`.

---

## The Solution: Dual-Lane authoritative Override

To successfully operate reverse without key emulation, the plugin must override the flight-intent calculations at both the upstream and downstream layers in synchronization with the engine.

### 1. Downstream Interception (Trampoline Hook)
Instead of a simple memory write, the plugin hooks the game's rotational writer block instructions using a mid-function assembly trampoline:
- **Instruction Targeted**: `vmovss [rdx+04], xmm2` (which writes to `cluster+0x5C`).
- **Trampoline Logic**: If the override is active (e.g. `g_vertStrafeOverrideEnabled = 1`), the hook intercepts the write and forces `g_vertStrafeBits` (containing the negative throttle value) into the target address instead of the unmodified register value.
- **Result**: This runs in-thread on the engine's time step, ensuring the vertical strafe value remains negative and cannot be overwritten.

### 2. Upstream Alignment
To ensure the game's flight state machine transitions cleanly, the plugin also writes the negative throttle value directly to the upstream source object:
- **Offset**: `sourceBase + 0x3C` (Primary Vertical Strafe / Decel).
- **Result**: The upstream flight controller registers the pilot's raw deceleration intention, transitioning the ship into reverse flight when forward speed drops to zero.

### 3. Throttle Target Injection
Finally, the negative target is written to the throttle target:
- **Offset**: `clusterBase + 0x68` (Throttle Input Target).
- **Note**: The effective throttle (`clusterBase + 0x6C`) must **never** be overwritten by the plugin, as this destroys the game's turn-rate penalty calculations. The game naturally calculates `clusterBase + 0x6C` based on the negative input target at `clusterBase + 0x68`.

### 4. Velocity Gating via In-Cluster Offset (+0x70)

The flight control cluster contains a velocity field at `clusterBase + 0x70` that tracks the ship's current speed. This should be used **instead of** the fragile HUD-derived static module offset (`kVelocityModuleOffset = 0x5E75644`) currently used by `ReadShipVelocity()` in the plugin.

**Why this matters:**
- The existing `ReadShipVelocity()` depends on a hard-coded static offset discovered via Cheat Engine integer-matching against the HUD speedometer display. This offset lives outside the flight control cluster in an unrelated HUD-rate cache structure, and must be manually re-validated every game update.
- `clusterBase + 0x70` lives **inside the same validated control cluster** that the plugin already discovers at runtime via the SignalHunter trampoline. Once the cluster base pointer is locked, `+0x70` is immediately available with zero additional pointer resolution.
- This eliminates an entire fragile dependency: no separate `GetModuleHandle` + static offset, no HUD structure coupling, and no risk of the velocity address silently breaking across game patches while the rest of the plugin continues to function.

**Usage for reverse gating:**
- When the plugin needs to decide whether to allow a reverse transition (e.g., `fReverseGateVelocity` threshold check), read `*(float*)(clusterBase + 0x70)` directly from the already-validated `s_activeThrottlePtr`.
- This applies to all reverse gating contexts: unipolar reverse zone transitions, accumulator-mode reverse, digital reverse button, and dedicated reverse axis — unifying them under a single, cluster-local velocity source.

### 5. Reverse Is Binary (−1.0 Only)

Empirical testing confirms that the game's reverse flight state machine only responds to a full `−1.0` signal across all three lanes (`source+0x3C`, `cluster+0x5C`, `cluster+0x68`). Fractional negative values (e.g., `−0.5`) **fail to engage reverse** — the ship stalls at zero velocity without transitioning.

This means all reverse inputs from the plugin must be treated as **digital on/off at −1.0**, regardless of whether the hardware source is analog (axis) or digital (button).

> **Future work**: Proportional reverse speed could theoretically be simulated via **pulse-width modulation** — rapidly toggling between `−1.0` and `0.0` while monitoring `clusterBase + 0x70` (velocity) to converge on a target reverse speed. This is out of scope for the current implementation.

---

## Configuration & Usage Flow

When mapping a bipolar analog joystick throttle axis (where the lower half of the axis represents reverse):

1. **Forward Input (Axis > Center)**:
   - Write positive target throttle `0.0 .. +1.0` to `clusterBase + 0x68`.
   - Ensure vertical strafe overrides are disabled (`g_vertStrafeOverrideEnabled = 0`) to allow normal vertical translation (space/c keys) or joystick strafe inputs to pass through.

2. **Reverse Input (Axis < Center)**:
   - Determine reverse scale `0.0 .. -1.0` based on deflection below center.
   - Write the negative target throttle directly to `clusterBase + 0x68`.
   - Write the negative value to upstream `sourceBase + 0x3C`.
   - Enable the vertical strafe override via `ThrottleHook::SetVerticalStrafeOverride(targetThrottle, true)` to lock `clusterBase + 0x5C` to the negative value.

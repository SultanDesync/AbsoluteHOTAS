# Absolute Input Bus ABI v1

> **Status:** pre-release implementation; ABI/layout tests, build, in-game no-consumer smoke, and
> Absolute Head Tracking enumeration/capture/action/pilot-context dogfood pass. Absolute Power and
> third-party validation remain pending.
> **Provider:** AbsoluteHOTAS
> **Export:** `AbsoluteHOTAS_QueryInputBusApi(1)`
> **Authority:** [`include/AbsoluteInputBusAPI.h`](../include/AbsoluteInputBusAPI.h)

## Purpose and boundary

Absolute Input Bus lets another SFSE plugin observe HOTAS-owned DirectInput devices and ask
AbsoluteHOTAS to record a physical control without opening a second DirectInput poller. It is the
shared backend for first-party Absolute Head Tracking and Absolute Power joystick bindings and a
pre-release integration point for third-party flight-control mods.

The bus is a read-only mirror, not a new hop in HOTAS's latency-critical output path. AbsoluteHOTAS
still polls and consumes its device state directly. It copies bounded POD snapshots into the bus
after each poll. A consumer owns its action semantics, binding configuration, transaction, and
gameplay behavior.

This is not the suite runtime-arbitration service. It does not decide camera/mouse/flight-lane
ownership and it does not dispatch actions between mods.

## Dependency posture

Consumers discover the export dynamically and validate ABI version, `structSize`, capabilities,
and required function pointers. They do not link against `AbsoluteHOTAS.lib`. A consumer that can
operate through keyboard/custom bindings must continue to load when HOTAS or the Input Bus is
absent. The UI may then say that direct flight-stick recording is unavailable and retain its
keyboard fallback.

The returned API table has process lifetime. Every returned device, snapshot, profile, context,
capture result, and string is a caller-owned copy. No STL type, DirectInput interface, game pointer,
callback, or cross-DLL allocation crosses the ABI.

## Device publication

`getDeviceCount` and `getDevice` enumerate the same DirectInput game-controller devices used by
HOTAS. `DeviceInfoV1` includes:

- process-local device index;
- instance and product GUID bytes;
- instance GUID formatted as the durable `persistentId`;
- VID/PID and bounded device names; and
- bounded axis/button/POV counts.

`getSnapshot` returns one copied `DeviceSnapshotV1`:

- raw HID axes `0x30..0x37`;
- active HOTAS calibration minima/maxima and bipolar normalized values;
- four raw POV angles (`-1` when centered);
- 144 digital down states: buttons `1..128`, then POV directions `129..144`; and
- monotonic press/release counters for all 144 digital channels.

The counters preserve a short press even when a consumer polls more slowly than HOTAS. A consumer
must establish a new baseline when `producerGeneration` changes. Counter subtraction uses ordinary
unsigned wraparound rules. A first observation and a device reconnection are level baselines; a
control already held at either boundary does not synthesize a press.

## Provider-owned capture

`beginCapture` starts the one global Input Bus capture session. The request selects buttons, POV
directions, axes, or a combination and supplies bounded settle/timeout values plus a diagnostic
consumer ID. UI and game threads only create, poll, or cancel the session. All device observation
and debounce work advances on the HOTAS controller thread after its normal `PollAll`.

Capture behavior is:

- physical button or POV: new down edge, two-frame bounce confirmation, then 10–1000 ms
  settle-to-quiescence (50 ms by default);
- axis: more than 8000 raw units from the opening baseline for five consecutive polls; and
- timeout: 500–30000 ms (8 seconds by default).

An already-held digital control is ignored until released and pressed again. A newly connected
device is rebased before it can produce a capture. Only one consumer can capture at a time; a
second request receives `Busy`. Sessions carry a monotonically changing ID so a late UI poll cannot
consume another module's result.

The result is descriptive and is never persisted by the bus. It contains kind, device identity,
control ID, product name, and a portable text token such as:

```text
{INSTANCE-GUID}@button:7
{INSTANCE-GUID}@pov:0:up
{INSTANCE-GUID}@axis:0x30
```

The destination mod decides whether the binding is global or associated with the active HOTAS
profile, writes its own configuration only when its normal transaction is applied, and interprets
the physical input as its own command.

## Profile publication

`getProfileState` exposes only:

- active HOTAS slot;
- stable profile ID (`base` or the profile filename); and
- a generation that changes on activation and profile reload.

The bus does not serialize profiles and cannot store another mod's bindings. A consumer that offers
per-HOTAS-profile bindings keys its own data by `profileId` and rebases on `generation`. A consumer
that offers global bindings ignores this state.

## Runtime context signals

`getRuntimeContext` publishes HOTAS's validated flight-context observation so companion mods do not
need console commands or duplicate game hooks. ABI v1 includes:

- `RuntimeContext::{Piloting, OnFoot, Suspended}`;
- `isPilot`;
- gameplay-context active (distinguishes gameplay from menus/loading when known);
- Targeting Mode active; and
- age in milliseconds of the selected flight-handler output signal.

`validSignals` and `activeSignals` are separate. In particular, `isPilot` is not valid during
`Suspended`; consumers must not reinterpret that state as authoritative `false`. `sequence` changes
on each publication, while `contextGeneration` changes only when semantic state/validity changes.
`sourceFlags` tells a consumer when the configured automatic pilot detector produced the state.

New signals may consume additional defined bits or a size-gated ABI tail. Existing bit meanings
must never change.

## Minimal consumer discovery

```cpp
using QueryInputBus = const AbsoluteInputBusApi::ApiV1* (__cdecl*)(std::uint32_t) noexcept;

const AbsoluteInputBusApi::ApiV1* DiscoverInputBus()
{
    const HMODULE hotas = GetModuleHandleW(L"AbsoluteHOTAS.dll");
    if (!hotas) return nullptr;
    const auto query = reinterpret_cast<QueryInputBus>(
        GetProcAddress(hotas, "AbsoluteHOTAS_QueryInputBusApi"));
    if (!query) return nullptr;
    const auto* api = query(AbsoluteInputBusApi::kAbiVersion);
    constexpr std::size_t required =
        offsetof(AbsoluteInputBusApi::ApiV1, cancelCapture) +
        sizeof(api->cancelCapture);
    if (!api || api->structSize < required ||
        api->abiVersion != AbsoluteInputBusApi::kAbiVersion ||
        !api->getDeviceCount || !api->getDevice || !api->getSnapshot ||
        !api->beginCapture || !api->pollCapture || !api->cancelCapture) {
        return nullptr;
    }
    return api;
}
```

Discovery should occur after SFSE has loaded peer plugins, and may be retried at the next documented
message boundary. Do not scan repeatedly every frame.

## v1 release gates

Before calling this ABI public/stable:

1. run the full AbsoluteHOTAS automated suite and an in-game no-consumer smoke;
2. dogfood enumeration and capture in Absolute Head Tracking and Absolute Power;
3. prove missing-HOTAS fallback in both consumers;
4. prove simultaneous capture returns `Busy`, stale sessions are rejected, and cancellation leaves
   no saved draft;
5. prove profile switches update identity without synthesizing button presses; and
6. validate at least one third-party-style client against a pre-release SDK copy.

After that freeze, v1 changes are additive through capabilities and size-gated tails. Incompatible
changes require a new query version; they must not silently alter the v1 table.

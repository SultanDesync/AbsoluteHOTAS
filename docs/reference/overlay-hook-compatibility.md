# Overlay Hook Compatibility (Best Practice)

How AbsoluteHOTAS's in-game ImGui overlay coexists — and sometimes deliberately
doesn't — with other software that hooks the Direct3D 12 / DXGI render chain.
This is the adopted design stance for `UIHook` (`shared/src/UIHook.cpp`).

## The problem

The overlay works by hooking `IDXGISwapChain::Present` / `Present1` and
`ID3D12CommandQueue::ExecuteCommandLists`, then submitting ImGui draw commands to
the game's present queue. That only renders correctly if we are the chain owner
— or at least cooperate cleanly with whoever is. In practice the render chain is
crowded:

- **Frame generation / upscalers** — NVIDIA Streamline (`sl.interposer.dll`,
  DLSS-G / Reflex), driver-level **Smooth Motion**, Intel **XeSS**
  (`libxess.dll`), AMD FSR3 FG. These proxy the swapchain and spin up private
  command queues.
- **Driver post-process** — NVIDIA App **RTX HDR** / **Dynamic Vibrance** filters.
- **Capture / overlays** — OBS game-capture, Steam, Discord, ShadowPlay.
- **Security software** — broad user-mode API hooking (e.g. Bitdefender ATC).

When any of these hooks the same entry points first, two things break the
overlay while leaving the **cursor working** (cursor is OS-level, swapchain-
agnostic):

1. **Wrong present queue** — our heuristic latches a DIRECT queue that isn't the
   one presenting the visible frame, so draws execute into nothing.
2. **Wrong/proxy swapchain or back buffer** — we draw into a surface the proxy
   doesn't scan out.

This is a well-known class of problem for every D3D12 overlay injector, not an
ImGui issue (ImGui is render-agnostic; it draws wherever we submit).

## The ceiling — why "perfect" coexistence is not a goal

We will not beat the established projects here, and we don't try to. Two hard
limits:

- As the **second hooker** on a chain another injector owns, correct behavior
  depends on that injector's internals, which we cannot control or guarantee
  across arbitrary combinations.
- **Frame generation wants UI excluded from interpolation** (or the HUD ghosts).
  A blindly-injected overlay can't tag itself as UI, so even mature overlays
  (Special K, ReShade, RTSS) treat injected-overlay + FG as best-effort, not
  seamless.

So "graceful" = robust capture + clear messaging + a documented workaround. Not
invisible integration.

## Prior art (reference implementations)

Studied rather than reinvented:

- **Special K** — deepest DLSS-Frame-Generation / Streamline handling; the
  canonical reference for injected-overlay coexistence.
- **ReShade** — proxy-swapchain aware; added Streamline/FG compatibility.
- **RTSS (RivaTuner)** — most battle-tested overlay; broad custom-D3D
  compatibility and hook-ordering logic.

## Adopted best practice

1. **Present-queue association (temporal capture).** Capture the DIRECT queue
   that submits *immediately before each Present on the target swapchain*, not
   the first DIRECT queue seen. Multi-queue interposers (Streamline FG) defeat
   first-seen; they don't defeat "the queue that fed the present." This replaces
   the first-seen heuristic in `HookedExecuteCommandLists`.

2. **Per-instance swapchain hooking (robustness).** Prefer hooking the vtable of
   the actual swapchain instance handed to the game so a proxy's own `Present`
   is caught, over patching only the canonical dxgi function harvested from a
   dummy device. (Secondary — only needed when the proxy presents through its
   own entry point.)

3. **Detect-and-tell.** We already detect a prior hook on the render entry
   points (the `[Compat]` diagnostic). Promote that into the shipping build as a
   clear log line so a silent no-overlay becomes a self-serve diagnosis, e.g.:
   *"Incompatible prior render hook detected on Present — another frame-gen /
   capture / overlay layer owns the chain; the overlay may not render. Try
   disabling frame generation or capture overlays."*

4. **Documented incompatibilities + workarounds.** For injectors we can't
   coexist with, name the conflict and the fix rather than chasing a universal
   solution. The overlay is a config-time tool (Ctrl+Alt+B), so toggling a
   conflicting layer off during setup is an acceptable workaround.

## Mapping to code

| Practice | Where |
| --- | --- |
| Queue capture (primary / fallback) | `HookedCreateSwapChainForHwnd`, `HookedExecuteCommandLists` |
| Prior-hook detection | `[Compat]` instrumentation at hook install |
| Proxy/FG swapchain rebind | `RebindRenderTargetsIfNeeded`, per-frame monitor in `HandlePresent` |
| Submit to present queue | `RenderOverlayFrame` → `pQueue->ExecuteCommandLists` |

## Known incompatibilities

Workaround for all: disable the conflicting layer (at least while using the
overlay). Update to the latest AbsoluteHOTAS first — the standardized hooking in
3.0.1+ resolves several earlier cases.

| Layer | Where to disable |
| --- | --- |
| NVIDIA DLSS Frame Generation / Streamline | In-game graphics settings |
| NVIDIA Smooth Motion (driver-level FG) | NVIDIA App → Driver/Graphics settings |
| NVIDIA DLSS Override (forced FG) | NVIDIA App → Driver settings |
| NVIDIA RTX HDR / Dynamic Vibrance | NVIDIA App → game filters |
| Intel XeSS | In-game upscaler setting |
| Capture / overlay (OBS, Steam, Discord, ShadowPlay) | The respective app's overlay/capture toggle |
| Multi-display present-mode edge cases (MPO / independent flip) | Test single display; borderless vs. exclusive fullscreen |

## Provenance / confidence

- Failure mode and the queue/back-buffer split: confirmed by code review against
  a real diagnostic-build log (cursor worked, overlay did not; primary queue
  capture bypassed, fallback heuristic used).
- Specific culprit on that log: **not** definitively identified — the module
  list leans toward NVIDIA Streamline, but recording/driver-level layers are not
  excluded. Identity does not change the approach above.

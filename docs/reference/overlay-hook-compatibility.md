# Overlay Hook Compatibility (Best Practice)

How AbsoluteHOTAS's in-game ImGui overlay coexists — and sometimes deliberately
doesn't — with other software that hooks the Direct3D 12 / DXGI render chain.
This is the adopted design stance for `UIHook` (`src/UIHook.cpp`).

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

1. **Per-swapchain presentation-queue association.** Intercept
   `CreateSwapChainForHwnd` and associate the returned swapchain's COM identity
   with the D3D12 DIRECT queue passed to DXGI. Keep that authoritative mapping
   across `ResizeBuffers`; clearing it creates a race in which RTSS or a
   frame-generation helper queue can be mistaken for the game's queue.

2. **Temporal late-injection fallback.** If the swapchain predates hook
   installation, sample the most recently submitting DIRECT queue at Present
   and require its D3D12 device to match the presented swapchain. This is still
   heuristic, but is strictly better than permanently latching the first DIRECT
   queue observed.

3. **Canonical entry-point chaining.** Hook the SDK-defined DXGI/D3D12 entry
   points with MinHook and always forward through its trampoline. Do not replace
   an individual COM object's vtable: proxy swapchains may expose implementation
   details that make copied table sizes and lifetime assumptions unsafe.

4. **Fence every frame context.** Record the fence value after each overlay
   submission and wait for it before resetting that back buffer's command
   allocator. Swapchain rotation does not by itself prove the GPU has finished
   using an allocator.

5. **Detect-and-tell.** At startup, `[UIHook]` records recognizable inline hooks
   on the render entry points, including the destination module when it can be
   resolved. This turns a silent no-overlay into a useful lead. Vtable replacement,
   proxy objects, and hooks installed after AbsoluteHOTAS remain invisible to this
   check, so absence of the message does not clear other graphics layers.

6. **Fail-open workbench.** Initialize ImGui/D3D12 only after the first open
   request. A renderer failure is latched for the session and returns every hook
   to transparent forwarding; controller polling and manual configuration remain
   active. `[UI] bEnableWorkbench=false` bypasses renderer-hook installation.

7. **Documented incompatibilities + workarounds.** For injectors we can't
   coexist with, name the conflict and the fix rather than chasing a universal
   solution. The overlay is a config-time tool (Ctrl+Alt+B), so toggling a
   conflicting layer off during setup is an acceptable workaround.

## Mapping to code

| Practice | Where |
| --- | --- |
| Per-swapchain queue registry | `AssociateSwapChainQueue`, `SelectPresentQueue` |
| Queue capture (primary / fallback) | `HookedCreateSwapChainForHwnd`, `HookedExecuteCommandLists` |
| Prior-hook detection | `[UIHook]` instrumentation at hook install |
| Proxy/FG swapchain rebind | `RebindRenderTargetsIfNeeded`, per-frame monitor in `HandlePresent` |
| Canonical hook forwarding | `OriginalPresentFor`, `OriginalPresent1For`, `OriginalResizeBuffersFor` |
| Per-frame allocator fences | `WaitForFrameAllocator`, `MarkFrameSubmitted` |
| Fail-open / manual bypass | `g_faulted`, `HandleRenderException`, `[UI] bEnableWorkbench` |
| Submit to present queue | `RenderOverlayFrame` → `pQueue->ExecuteCommandLists` |

## Verified combinations

| Layer | Result |
| --- | --- |
| RTSS 7.3.5, D3D12, RTSS loaded first | Verified: wizard renders with canonical hook chaining |

## Reported combinations awaiting confirmation

| Layer | Report | Current classification | Workaround / retest |
| --- | --- | --- | --- |
| Razer Cortex In-Game FPS/features overlay | One incompatibility report; AbsoluteHOTAS version uncertain and possibly 3.0.2 | Plausible member of the injected capture/overlay hook category, not yet a confirmed 4.0 incompatibility | In Razer Cortex, turn off the in-game features under **Game Booster → In-Game**, restart Starfield, and compare with them enabled on the latest AbsoluteHOTAS build |

Razer documents that Cortex renders an in-game FPS overlay and that the features
can be enabled or disabled under **Game Booster → In-Game**:
[Razer Cortex support and FAQs](https://mysupport.razer.com/app/answers/detail/a_id/6104/~/razer-cortex-10-support-%26-faqs).
Razer does not document the graphics entry points it intercepts. Legacy Cortex
diagnostics observed during this review explicitly refer to process injection and
hook lifecycle, which supports the category classification but does not prove a
specific `Present`, `Present1`, or `ExecuteCommandLists` implementation.

The current per-swapchain queue association landed after the packaged 3.0.2
build. A 3.0.2-only report therefore must be
reproduced on 4.0 before it becomes a named incompatibility.

For a useful retest, record:

- exact AbsoluteHOTAS and Razer Cortex versions;
- whether the Razer FPS/features overlay is enabled;
- whether the system cursor appears when `Ctrl+Alt+B` is pressed;
- whether each overlay renders when tested alone;
- launch order, display mode, frame-generation setting, and other graphics layers;
- `AbsoluteHOTAS.log` from one enabled run and one Razer-disabled control run.

The most useful `[UIHook]` lines are the prior-hook destination module,
swapchain/queue association, selected present queue, ImGui
initialization, and first overlay submission.

## Risk categories and workarounds

Workaround for all: disable the conflicting layer (at least while using the
overlay). Presence in this table identifies a shared render-chain risk, not proof
that every listed product/version conflicts. Update to the latest AbsoluteHOTAS
first because the 4.0 renderer includes compatibility work absent from 3.0.2.

| Layer | Where to disable |
| --- | --- |
| NVIDIA DLSS Frame Generation / Streamline | In-game graphics settings |
| NVIDIA Smooth Motion (driver-level FG) | NVIDIA App → Driver/Graphics settings |
| NVIDIA DLSS Override (forced FG) | NVIDIA App → Driver settings |
| NVIDIA RTX HDR / Dynamic Vibrance | NVIDIA App → game filters |
| Intel XeSS | In-game upscaler setting |
| Razer Cortex FPS/features overlay | Razer Cortex → Game Booster → In-Game |
| Capture / overlay (OBS, Steam, Discord, ShadowPlay) | The respective app's overlay/capture toggle |
| Multi-display present-mode edge cases (MPO / independent flip) | Test single display; borderless vs. exclusive fullscreen |

## Provenance / confidence

- Failure mode and the queue/back-buffer split: confirmed by code review against
  a real diagnostic-build log (cursor worked, overlay did not; primary queue
  capture bypassed, fallback heuristic used).
- Specific culprit on that log: **not** definitively identified — the module
  list leans toward NVIDIA Streamline, but recording/driver-level layers are not
  excluded. Identity does not change the approach above.
- Razer Cortex: product feature and disable path are documented by Razer; process
  injection/hook lifecycle is supported by locally observed legacy Cortex
  diagnostics; exact graphics entry points and current 4.0 incompatibility remain
  unverified.

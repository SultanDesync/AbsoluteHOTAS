# Reference Index

## Runtime References

| File | Purpose |
| --- | --- |
| `key-output-reference.md` | Keyboard/mouse `SendInput` values for explicit raw custom bindings and macro targets. |
| `absolutehotas-driver-plugin.md` | Standalone DLL/INI package and startup reference. |
| `ship-button-bindings.md` | The 17 native ship actions, six universal context inputs, optional menu-control reuse, ownership, and raw-output boundary. |
| `native-controls-and-head-tracking.md` | 5.0 native-control architecture, movement modifiers, OpenTrack setup, Tobii route, and fail-closed gates. |

## Research Summaries

| File | Purpose |
| --- | --- |
| `wizard-workbench-architecture.md` | Governing requirements, invariants, layers, and migration contract for the Binding Wizard workbench refactor. |
| `control-cluster-architecture.md` | Sanitized summary of the ship movement cluster, writer hooks, source object, and patch-resilience strategy. |
| `control-map-ship-functions.md` | Legacy 4.x `ControlMap_Custom.txt` binary format and vanilla MAIN/ALT ship-function map; 5.0 native and universal named inputs bypass this path. |
| `overlay-hook-compatibility.md` | How the ImGui overlay coexists with other D3D12/DXGI render-chain hooks (frame-gen, upscalers, capture); adopted best practice and known incompatibilities. |
| `macros.md` | Macro builder design: step model (chord/tap/hold/turbo), logical-action targeting, emission engine, and the "Grav → Shields" worked example. |
| `config-layout.md` | 4.0 data contract: two-file release, generated custom/profile data, layered loading, atomic saves, and the fresh-start compatibility boundary. |
| `profile-switching.md` | Live profile swapping: slots + momentary/toggle activation, sparse profiles as fallthrough, Import vs Swap, and the HOSAS cruise-mode recipe. Supersedes the shift-layer design. |

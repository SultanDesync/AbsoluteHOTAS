# Reference Index

## Runtime References

| File | Purpose |
| --- | --- |
| `key-output-reference.md` | Keyboard/mouse `SendInput` output values for `ShipButtonOutputs`. |
| `absolutehotas-driver-plugin.md` | Standalone DLL/INI package and startup reference. |
| `ship-button-bindings.md` | Ship action keys, ControlMap reconciliation, explicit overrides, and vanilla fallbacks. |

## Research Summaries

| File | Purpose |
| --- | --- |
| `wizard-workbench-architecture.md` | Governing requirements, invariants, layers, and migration contract for the Binding Wizard workbench refactor. |
| `control-cluster-architecture.md` | Sanitized summary of the ship movement cluster, writer hooks, source object, and patch-resilience strategy. |
| `control-map-ship-functions.md` | `ControlMap_Custom.txt` binary format, full vanilla MAIN/ALT ship-function map, and the `ControlMapReader` lookup/resolution strategy. |
| `overlay-hook-compatibility.md` | How the ImGui overlay coexists with other D3D12/DXGI render-chain hooks (frame-gen, upscalers, capture); adopted best practice and known incompatibilities. |
| `macros.md` | Macro builder design: step model (chord/tap/hold/turbo), logical-action targeting, emission engine, and the "Grav → Shields" worked example. |
| `config-layout.md` | 3.1 data contract: two-file release, generated custom/profile data, layered loading, atomic saves, and the fresh-start compatibility boundary. |
| `profile-switching.md` | Live profile swapping: slots + momentary/toggle activation, sparse profiles as fallthrough, Import vs Swap, and the HOSAS cruise-mode recipe. Supersedes the shift-layer design. |

# Reference Index

## Runtime References

| File | Purpose |
| --- | --- |
| `key-output-reference.md` | Keyboard/mouse `SendInput` output values for `ShipButtonOutputs`. |
| `absolutehotas-driver-plugin.md` | Standalone DLL/INI package and startup reference. |
| `ship-button-bindings.md` | AbsoluteHOTAS ship action binding keys and vanilla output defaults. |

## Research Summaries

| File | Purpose |
| --- | --- |
| `control-cluster-architecture.md` | Sanitized summary of the ship movement cluster, writer hooks, source object, and patch-resilience strategy. |
| `control-map-ship-functions.md` | `ControlMap_Custom.txt` binary format, full vanilla MAIN/ALT ship-function map, and the `ControlMapReader` lookup/resolution strategy. |
| `overlay-hook-compatibility.md` | How the ImGui overlay coexists with other D3D12/DXGI render-chain hooks (frame-gen, upscalers, capture); adopted best practice and known incompatibilities. |
| `macros.md` | Macro builder design: step model (chord/tap/hold/turbo), logical-action targeting, emission engine, and the "Grav → Shields" worked example. |
| `config-layout.md` | 3.1 config split design: mod/user/macros file ownership, layered overlay load, the v3.0.x→3.1 first-load migration, and profile export/import. |
| `input-layers.md` | Shift/mode layer design: layer definitions + activation modes, `:L<n>` binding qualifier, press-time resolution with base fallthrough, wizard layer pills. |

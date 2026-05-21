# AbsoluteHOTAS Standalone Plugin

AbsoluteHOTAS 1.6 ships as a standalone SFSE DLL and INI. The previous
Papyrus/ESM driver bridge has been removed from the release path.

## Target Artifact

Install these files into `Data\SFSE\Plugins\`:

- `AbsoluteHOTAS.dll`
- `AbsoluteHOTAS.ini`

No `AbsoluteHOTAS.esm`, `.pex` scripts, or Creation Kit-generated driver quest
is required for the standalone build.

## Runtime Startup

The controller starts when SFSE loads the DLL. Discovery is controlled by:

- `bAlwaysOn = true`: arm discovery when the DLL starts.
- `bAlwaysOn = false`: wait for `F8` or `iActivateButtonId`.
- `iStopButtonId`: disarm the current capture/override state.

The default public configuration leaves `bAlwaysOn = false` so users can arm
the signal hunter deliberately after confirming their device mapping.

## Package Layout

CMake post-build output is staged under:

```text
contrib\PluginRelease\Data\SFSE\Plugins\
```

That staging directory is ignored by git and should be treated as build output,
not source.

## Runtime Check

With `bLogThrottle=true`, `Data\SFSE\Plugins\StarfieldThrottleLog.txt` should
show:

- `[Main] Plugin load complete.`
- `[Controller] Config Loaded - AbsoluteHOTAS 6DOF Dashboard Initialized.`
- `[PilotState] Standalone mode active; waiting for F8 or activate button.`

If `bAlwaysOn=true`, the final line should instead report that discovery was
armed automatically.

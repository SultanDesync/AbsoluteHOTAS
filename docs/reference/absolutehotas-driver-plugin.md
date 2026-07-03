# AbsoluteHOTAS Standalone Plugin

AbsoluteHOTAS 3.0 ships as a standalone SFSE DLL and INI. The previous
Papyrus/ESM driver bridge has been removed from the release path.

## Target Artifact

Install these files into `Data\SFSE\Plugins\`:

- `AbsoluteHOTAS.dll`
- `AbsoluteHOTAS.ini`

No `AbsoluteHOTAS.esm`, `.pex` scripts, or Creation Kit-generated driver quest
is required for the standalone build.

## Runtime Startup

The controller starts when SFSE loads the DLL. Discovery is controlled by:

- `bAlwaysOn = true` (default): arm discovery automatically when the pilot seat is entered.
- `bAlwaysOn = false`: wait for `iActivateButtonId` or Ctrl+Alt+F8.
- `iStopButtonId`: disarm the current capture/override state.

The default public configuration leaves `bAlwaysOn = true` so users get
immediate flight control without manual activation.

## Package Layout

CMake post-build output is staged under:

```text
contrib\PluginRelease\Data\SFSE\Plugins\
```

That staging directory is ignored by git and should be treated as build output,
not source.

## Runtime Check

With `bEnableLog=true`, `Data\SFSE\Plugins\AbsoluteHOTAS.log` should
show:

- `[Main] Plugin load complete.`
- `[Controller] Config Loaded - AbsoluteHOTAS 6DOF Dashboard Initialized.`
- `[PilotState] Standalone mode active; discovery armed automatically.`

If `bAlwaysOn=false`, the final line should instead report that it is
waiting for the activate button.

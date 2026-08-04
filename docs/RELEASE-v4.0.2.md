# Release Plan - AbsoluteHOTAS v4.0.2

**Branch:** `codex/4.0.2-rtss-hotfix`
**Release track:** stable hotfix for 4.0.1

## Release position

4.0.2 supersedes 4.0.1 as the recommended stable download. Its scope is limited
to restoring configuration-workbench compatibility with RTSS and similar render
hooks while retaining 4.0.1's lazy renderer initialization, fail-open behavior,
per-frame allocator fences, and stable flight-control core.

Profiles and macros remain supported but experimental, as in 4.0.1.

## Regression and fix

With RTSS 7.3.5.28314 loaded first and its global D3D12/DXGI hook enabled, 4.0.1
received one startup `Present` callback and was then bypassed when RTSS retained
the canonical DXGI prologue. Both the HOTAS binding and `Ctrl+Alt+B` correctly
requested the workbench, but lazy initialization could never reach another frame.

4.0.2 detects an existing leading render detour and attaches to its executable
destination. The existing graphics layer stays outermost and AbsoluteHOTAS
forwards through a MinHook trampoline. Unclaimed methods retain the canonical
entry-point path. No COM vtable is copied or replaced.

Workbench toggle polling now belongs to the controller thread rather than a
render callback or initialized WndProc. This preserves both keyboard and hardware
requests independently of temporary render-hook delivery.

## Validation

- [x] Reproduced the 4.0.1 regression with RTSS active.
- [x] Confirmed both toggle inputs were recognized while 4.0.1 received no later
  `Present` callback.
- [x] Loaded the 4.0.2 diagnostic build with RTSS active and loaded first.
- [x] Confirmed all five existing DXGI/D3D12 detour destinations were selected and
  hooked successfully.
- [x] Confirmed ImGui initialized and submitted workbench command lists.
- [x] Confirmed visible, clean workbench open/close cycles through the configured
  HOTAS button and `Ctrl+Alt+B`.
- [x] Built the release configuration and passed all registered `xmake test`
  targets.
- [x] Verify the final stable DLL version string, clean two-file archive, and
  release hashes.

## Final artifacts

- Archive: `releases/v4.0.2/AbsoluteHOTAS-v4.0.2-Release.zip`
  - SHA-256: `DC313FF6972F586552AB70C5117DD82515E6DD9BEE3BB358AFD78AB77EE9878A`
- Packaged and smoke-tested DLL:
  - SHA-256: `2EC4466A3067C34CCB39A5EF4C5F7CD097C2C339A1D41E6528F261A9674DC094`
- Packaged INI:
  - SHA-256: `3B940848841945B47E69BA4D992F40440688ABFD9AD375CE948F88FAC6AF6D5F`

The clean-extracted archive contains exactly:

- `SFSE\Plugins\AbsoluteHOTAS.dll`
- `SFSE\Plugins\AbsoluteHOTAS.ini`

The packaged DLL contains the stable runtime version string `4.0.2`. The DLL in
the archive is byte-for-byte identical to the build used for the final RTSS and
maintainer smoke tests.

## Compatibility priority

RTSS and comparable Windows overlays/capture hooks are a supported compatibility
priority for this hotfix. Linux/Proton and third-party frame generation remain
best effort. `[UI] bEnableWorkbench = false` remains the renderer-only escape
hatch; flight controls and manual configuration continue without workbench hooks.

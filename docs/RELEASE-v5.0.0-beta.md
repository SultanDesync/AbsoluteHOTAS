# Release Plan — AbsoluteHOTAS v5.0.0-beta

**Branch:** `5.0.0-beta`
**Release track:** opt-in beta alongside the established 4.0.2 stable file

## Release position

5.0.0-beta is the public validation build for native ship actions, native
movement modifiers, simultaneous roll/strafe output, and rotational cockpit head
tracking. Keep 4.0.2 available as the stable fallback while 5.0 gains broader
hardware, ship-state, camera, and mod-stack coverage.

The release archive retains the 4.0 two-file configuration contract. It replaces
only the plugin DLL and mod-owned default INI; it does not contain or overwrite
`AbsoluteHOTAS_Custom.ini`, profile files, logs, or other user-owned data.

## Implemented

- [x] Route all 23 named ship-button actions and named macro targets through
  validated internal Starfield control paths with no `SendInput` fallback.
- [x] Route boost-zone and strafe activation through Starfield's internal
  ship-control paths without requiring keyboard bindings.
- [x] Keep roll and lateral/vertical strafe independent so they can be commanded
  simultaneously.
- [x] Add OpenTrack FreeTrack 2.0 rotational camera look, including Tobii-through-
  OpenTrack and webcam tracking.
- [x] Add Camera Look workbench controls for per-axis enable, inversion,
  sensitivity, maximum angle, filtering, joystick override, toggle, recenter, and
  live graph/readout.
- [x] Preserve the explicit raw-output boundary for `[ButtonExpansion]` and
  `key:`/`mouse:` macro targets.
- [x] Update runtime, INI, workbench, and reference documentation to describe the
  direct controls and independent roll/strafe behavior accurately.

## Validation

- [x] Boost and native movement-modifier behavior validated in game.
- [x] Simultaneous strafe and roll validated in game.
- [x] Weapon groups and most named ship commands validated in game.
- [x] Basic head tracking validated with a Tobii Tracker 2 source through OpenTrack.
- [x] Basic head tracking validated with OpenTrack NeuralNet webcam input.
- [x] Camera Look workbench panel and core workbench functions validated.
- [x] One maintained 4.0.0 setup retained its bindings, profiles, and user data
  immediately after deployment with no rebinding.
- [x] Release build completed and all three registered `xmake test` suites passed.
- [x] Verify the final two-file archive through a clean extraction and record all
  release hashes.

The upgrade result is a successful sample of one. It establishes that the 4.0
configuration contract works on a real maintained setup, but beta users should
still back up their custom INI and Profiles directory before installing.

## Known beta boundaries

- `Undock / Take-Off` and `Exit Ship From Cockpit` use native contextual routes but
  still need broader seated-state validation.
- Other DirectInput devices, duplicate-device layouts, profiles, overlays, and mod
  stacks may expose issues not present in the maintainer setup.
- Head tracking is rotational yaw/pitch/roll only. Translational 6-DOF camera motion
  is not implemented.
- OpenTrack must be running with FreeTrack 2.0 Enhanced output for tracker input.
- Exact native gates are version-specific and fail closed when a supported runtime
  signature or object validation does not match.

## Final artifacts

- Archive: `releases/v5.0.0-beta/AbsoluteHOTAS-v5.0.0-beta-Release.zip`
  - SHA-256: `120820BEE6F50B0CEB3D2336AF9AA227042A89640EF835557A589CF76F09B169`
- Packaged DLL:
  - SHA-256: `8D9E9D9A3864852995DF993FF5723A44E00E5AD7537B49C823024D3FE3938250`
- Packaged INI:
  - SHA-256: `C3A5B227F840FE46F51FAF3E9B4D058C98707BFCA1002487DD319DCFD011FDBA`

The clean-extracted archive must contain exactly:

- `SFSE\Plugins\AbsoluteHOTAS.dll`
- `SFSE\Plugins\AbsoluteHOTAS.ini`

## Publication checklist

- [x] Keep 4.0.2 available as the stable fallback.
- [x] Describe 5.0.0-beta as opt-in rather than replacing the main stable file.
- [x] Document the tested 4.0.0 user-data migration without overstating its sample
  size.
- [x] Include rollback, logging, and feedback instructions in package/Nexus copy.
- [ ] Upload the archive as an optional beta file on Nexus.
- [ ] Publish the Nexus description and changelog.

## Beta feedback priorities

Prioritize reports in this order:

1. Crashes, configuration loss, stuck outputs, or failure to load.
2. Regressions in core pitch, yaw, roll, throttle, strafe, reverse, boost, weapons,
   or configuration saving.
3. Native ship actions that fail in a specific cockpit, camera, or ship state.
4. Head-tracking direction, centering, filtering, or OpenTrack transport failures.
5. Workbench and profile migration problems on other device layouts.

Request the plugin version, Starfield/SFSE versions, hardware and tracker source,
reproduction steps, `AbsoluteHOTAS.log` with logging enabled, and sanitized copies
of the relevant custom/profile configuration.

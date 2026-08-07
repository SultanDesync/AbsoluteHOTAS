# Release Plan — AbsoluteHOTAS v5.0.1

**Source branch:** `5.0.0-beta`
**Release track:** opt-in experimental Nexus file alongside the established 4.0.2 stable file

## Release position

5.0.1 is the experimental validation build for universal context controls,
targeting-aware navigation, native ship actions, native movement modifiers,
simultaneous roll/strafe output, and rotational cockpit head tracking. Keep 4.0.2
available as the stable fallback while 5.0 gains broader hardware, ship-state,
camera, context-routing, and mod-stack coverage.

The release archive retains the 4.0 two-file configuration contract. It replaces
only the plugin DLL and mod-owned default INI; it does not contain or overwrite
`AbsoluteHOTAS_Custom.ini`, profile files, logs, or other user-owned data.

## Implemented

- [x] Route 17 ship-specific button actions and named macro targets through
  validated internal Starfield control paths with no synthetic fallback.
- [x] Preserve the six existing Select, Back, and directional profile slots as
  universal vanilla E/Esc/arrow inputs across menus, dialogue, and power, with
  exact-gated `SelectLeft`/`SelectRight` routing for Targeting Mode components.
- [x] Add per-profile Menu Control Reuse options for Pitch-to-Up/Down,
  Yaw-to-Left/Right, and Primary-Weapon-to-Select, with independent inversion,
  adjustable hysteresis, and neutral/release arming on menu entry.
- [x] Route boost-zone and strafe activation through Starfield's internal
  ship-control paths without requiring keyboard bindings.
- [x] Keep roll and lateral/vertical strafe independent so they can be commanded
  simultaneously.
- [x] Add OpenTrack FreeTrack 2.0 rotational camera look, including Tobii-through-
  OpenTrack and webcam tracking.
- [x] Add Camera Look workbench controls for per-axis enable, inversion,
  sensitivity, maximum angle, filtering, joystick override, toggle, recenter, and
  live graph/readout.
- [x] Derive piloting/FPS context from selected flight-handler output freshness,
  with distinct `Piloting`, `OnFoot`, and menu/loading `Suspended` states.
- [x] Park flight controls automatically by default after a 5000 ms general latch,
  while enforcing an independent 400 ms head-pose gate at the camera hook.
- [x] Add Automatic Pilot Context controls to Advanced > Plugin Controls.
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
- [x] Isolated lifecycle probe validated that the selected-handler callback runs
  continuously while piloting and stops immediately when getting up while the
  cached handler remains valid.
- [x] Production Camera Look automatically deactivated when the maintainer exited
  the pilot seat.
- [x] Release build completed and all six registered `xmake test` suites passed,
  including the universal-context map, menu-reuse policy, and pilot-context
  freshness policy.
- [x] Verify the final two-file archive through a clean extraction and record all
  release hashes.

The upgrade result is a successful sample of one. It establishes that the 4.0
configuration contract works on a real maintained setup, but experimental users
should still back up their custom INI and Profiles directory before installing.

## Known experimental boundaries

- `Undock / Take-Off` and `Exit Ship From Cockpit` use native contextual routes but
  still need broader seated-state validation.
- Other DirectInput devices, duplicate-device layouts, profiles, overlays, and mod
  stacks may expose issues not present in the maintainer setup.
- Head tracking is rotational yaw/pitch/roll only. Translational 6-DOF camera motion
  is not implemented.
- OpenTrack must be running with FreeTrack 2.0 Enhanced output for tracker input.
- Exact native gates are version-specific and fail closed when a supported runtime
  signature or object validation does not match.
- The production head-pose gate passed cockpit exit. Cockpit re-entry, the delayed
  general flight-control latch, long targeting sessions, pause/loading transitions,
  and Full mode still need broader experimental coverage.

## Final artifacts

- Archive: `releases/v5.0.1/AbsoluteHOTAS-v5.0.1-Release.zip`
  - SHA-256: `97ADA6AF6D19B2557A52BBE01291302A953EB38D10A8481CC1145073FB60E3E0`
- Packaged DLL:
  - SHA-256: `5BB5EB45A8FED446231654558E87E9BB14DF9205FC3880032AF424C918BDA173`
- Packaged INI:
  - SHA-256: `260E11700D04A64CD9CF99FC1E1A4E1E3DAD93F1DF496A5A124633434EB83D72`

The clean-extracted archive contains exactly:

- `SFSE\Plugins\AbsoluteHOTAS.dll`
- `SFSE\Plugins\AbsoluteHOTAS.ini`

## Publication checklist

- [x] Keep 4.0.2 available as the stable fallback.
- [x] Describe 5.0.1 as experimental and opt-in rather than replacing the main stable file.
- [x] Document the tested 4.0.0 user-data migration without overstating its sample
  size.
- [x] Include rollback, logging, and feedback instructions in package/Nexus copy.
- [ ] Upload the archive to the Nexus experimental branch.
- [ ] Publish the Nexus description and changelog.

## Experimental feedback priorities

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

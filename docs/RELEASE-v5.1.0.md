# Release Plan — AbsoluteHOTAS v5.1.0

**Source branch:** `5.0.0-beta`
**Release track:** current stable standalone release

## Release position

Version 5.1.0 completes the move to
[Absolute Control](https://www.nexusmods.com/starfield/mods/18023) as the sole in-game
configuration frontend. The legacy Dear ImGui workbench, its graphics-hook dependencies, the
embedded OpenTrack runtime, and HOTAS's FirstPersonState camera hook are absent from the shipping
target. Existing flight configuration can still load without the menu host, but there is no
fallback overlay.

This is the first public 5.x successor to the Nexus 4.0.2 stable file. The complete chronological
record reconstructed from the 5.0 fork and its two intermediate release packages is in
[`CHANGELOG-v5.1.0.md`](CHANGELOG-v5.1.0.md). It covers the native-control
rewrite, 5.0.1 context controls, the complete Absolute Control provider, companion-module APIs and
ownership, the 5.1 cleanup, migration guarantees, validation, and current boundaries.

The release archive retains the two-file update contract: it replaces only `AbsoluteHOTAS.dll` and
the mod-owned default `AbsoluteHOTAS.ini`. It does not contain or overwrite
`AbsoluteHOTAS_Custom.ini`, profiles, or logs.

## Companion modules

- [Absolute Power](https://www.nexusmods.com/starfield/mods/18024) is compatible and can consume
  HOTAS's optional Input Bus for controller/POV preset capture.
- [Absolute Head Tracking](https://www.nexusmods.com/starfield/mods/17872) now owns the extracted
  OpenTrack-compatible cockpit camera feature.
- [Absolute Zero](https://www.nexusmods.com/starfield/mods/17460) has limited compatibility. While
  active it owns native mouse pitch/yaw, so HOTAS joystick pitch/yaw are unavailable; roll, strafe,
  throttle, buttons, profiles, and the shared writer remain available.

## Validation

- [x] Debug plugin build completed.
- [x] All 22 registered debug test suites passed.
- [x] Release-with-debug-information plugin build completed.
- [x] Legacy ImGui/MinHook/D3D12/DXGI units excluded from the shipping target.
- [x] Embedded HeadTracking unit and FirstPersonState camera hook excluded from the shipping path.
- [x] Final archive contains exactly the DLL and shipped default INI under `SFSE/Plugins`.
- [x] Final archive and payload hashes recorded in the package README.
- [x] Flight Axes is the first HOTAS tab and exclusively owns all analog-axis bindings.
- [x] Ship Buttons has four sections: complete Native Ship Controls in Starfield order, AbsoluteHOTAS Hotkeys, Optional Menu Navigation, and Custom SendInput Bindings.
- [x] Select Target defaults to its validated native function; six menu bindings are independent, unbound by default, and menu-context gated.
- [x] All 23 native ship actions use uniform binding-plus-method rows; five context-only routes expose locked, read-only method selectors rather than disappearing.
- [x] The required Absolute Control build presents a visible binding-record state.

## Publication checklist

- [ ] Upload `releases/v5.1.0/AbsoluteHOTAS-v5.1.0-Release.zip` as the primary Nexus file.
- [ ] Add Absolute Control as a Nexus requirement.
- [ ] Publish `releases/v5.1.0/nexus_description.txt`.
- [ ] Publish `releases/v5.1.0/CHANGELOG.txt`.
- [ ] Pin `releases/v5.1.0/nexus_pinned_comment.txt`.

## Final artifacts

- Archive: `releases/v5.1.0/AbsoluteHOTAS-v5.1.0-Release.zip`
  - SHA-256: `726284CC13AE98FE52307E1EFC40FB403BCBC59A1773C93C884B48C758DF06AF`
- Packaged DLL:
  - SHA-256: `2067F7A0C0880A84A4A1A352DE9B81D986F6307AD1D9DDEADFFDF9A031ABF2B3`
- Packaged INI:
  - SHA-256: `4723C35A6BA9AC130F09D2FD038BB87C82A3596BB352AF9B729FC51215900B4F`

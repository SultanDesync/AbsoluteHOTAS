# Release Plan — AbsoluteHOTAS v4.0.0-beta

**Branch:** `4.0.0-beta`
**Release track:** opt-in beta alongside the established 3.0.2 download

## Release position

4.0.0 is a public validation release for the new configuration baseline, runtime
profiles, Macro Builder, and rebuilt workbench. It is not intended to replace 3.0.2
immediately. Keep 3.0.2 available as the stable fallback until real-world installs
show no recurring core-flight, configuration-safety, or stuck-output blockers.

The maintainer release gate is representative stability, not exhaustive validation
of every device and feature combination. Broader hardware, selector, macro, overlay,
and mod-stack coverage belongs to the beta period.

## Implemented

- [x] Version sources, INI banner, metadata, and documentation identify
  `4.0.0-beta`.
- [x] Two-file configuration baseline: mod-owned defaults plus user-owned custom
  settings.
- [x] Hot-swappable sparse profile overlays with momentary, toggle, selector, and
  keyboard activation.
- [x] Parked profiles, starter profiles, full-profile export, and guarded import.
- [x] Macro Builder with chords, taps, holds, repeats, gaps, and turbo.
- [x] ControlMap-aware named ship outputs.
- [x] Rebuilt configuration workbench with profile-aware editing and guarded saves.
- [x] Opt-in versioned logging.
- [x] Initial release archive and Nexus/package copy.

## Release gate

- [x] Core flight axes work in representative use.
- [x] Configuration save/reload and the fresh 4.0 baseline work in representative
  use.
- [x] Representative profile creation, editing, activation, and return work.
- [x] Representative macro creation and playback work.
- [x] Parked injection and global stop have distinct, working behavior.
- [x] The current build is considered suitable for public beta validation.
- [x] Rename the branch to `4.0.0-beta`.
- [x] Run a release build and both standalone test targets.
- [x] Confirm the packaged DLL and INI match that validated build by SHA-256.
- [ ] Check the archive contents from a clean extraction.
- [ ] Final proofread of GitHub, Nexus, package README, and changelog.

## Publication

- [x] Keep the Nexus description concise and link the detailed GitHub README.
- [x] Document the fresh-start upgrade boundary prominently.
- [x] State that 3.0.2 remains the stable fallback during the beta period.
- [ ] Rebuild `releases/v4.0.0-beta/AbsoluteHOTAS-v4.0.0-beta-Release.zip`.
- [ ] Commit the 4.0 release preparation.
- [ ] Tag `v4.0.0-beta`.
- [ ] Publish the beta without replacing or removing 3.0.2.

## Beta support priorities

Prioritize reports in this order:

1. Crashes, configuration loss/corruption, or stuck synthetic inputs.
2. Regressions in core pitch, yaw, roll, throttle, strafe, reverse, saving, or
   startup.
3. Recurring profile or macro failures with clear reproduction steps.
4. UX confusion and ordinary polish for a possible 4.0.1 build.
5. New feature requests only when they fit the maintainer's own scope.

Request the exact plugin version, enabled log, relevant configuration/profile files,
hardware names, Starfield/SFSE versions, and reproducible steps for beta reports.

## Promotion to 4.0.1

Consider 4.0.1 the stable promotion candidate after a meaningful spread of real
installs produces no recurring major blockers, the most common profile UX problems
are addressed, and installation/upgrade guidance has proved adequate. At that point,
4.0.1 can become the main download and 3.0.2 can move to optional or archived files.

Bug intake, severity, lifecycle, verification, and the promotion gate are defined
in [`BUG-TRIAGE-v4.0.1.md`](BUG-TRIAGE-v4.0.1.md).

The initial maintenance review and prioritized structural-risk backlog are recorded
in [`TECH-DEBT-v4.0.1.md`](TECH-DEBT-v4.0.1.md).

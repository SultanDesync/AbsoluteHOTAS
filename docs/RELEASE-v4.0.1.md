# Release Plan - AbsoluteHOTAS v4.0.1

**Branch:** `4.0.1`
**Release track:** stable core release; profiles and macros remain experimental

## Release position

4.0.1 becomes the recommended main download. The currently deployed 4.0 core has
validated DirectInput polling, analog flight injection, output release,
configuration saving, and the configuration workbench well enough for stable
status. Version 3.0.2 may remain available as a legacy 3.x option, but no longer
needs to be presented as the stable fallback.

Profiles and macros remain part of 4.0.1 and are supported, but retain an
experimental label until more users exercise varied activation, selector, and
sequence workflows. Limited adoption is not treated as a blocker for the validated
core release.

## 4.0.1 scope

- [x] Replace the fragile per-instance swap-chain shadow vtable with canonical
  DXGI hook forwarding.
- [x] Lazily create ImGui/D3D12 resources when the workbench is first opened.
- [x] Latch renderer failures for the session and leave flight/manual config live.
- [x] Add `[UI] bEnableWorkbench = false` as a complete renderer-hook bypass.
- [x] Track command-allocator completion per back buffer.
- [x] Consolidate core flight-axis setup into `Flight Axes (Core)`.
- [x] Harden binding parsing, logging concurrency, and regression coverage.
- [x] Record the post-release mechanism and technical-debt review separately.

## Validation position

- [x] The deployed 4.0 core functions are maintainer-validated.
- [x] ReShade and RTSS have been exercised as compatible renderer-stack examples.
- [x] Third-party frame generation, graphics injectors, overlays, and Proton
  translation layers are documented as best-effort compatibility.
- [x] A renderer-only incompatibility has a documented non-CTD operating mode.
- [x] Run the final 4.0.1 build and the complete registered test suite.
- [x] Verify a clean extraction contains only the versioned DLL and shipped INI.
- [x] Record SHA-256 hashes for the final archive, DLL, and INI.

## Final artifacts

- Archive: `releases/v4.0.1/AbsoluteHOTAS-v4.0.1-Release.zip`
  - SHA-256: `75D73D5A51262BAD04202B8E17E2409A0C883C4B84842CD0B21B33D795C9B774`
- Packaged DLL:
  - SHA-256: `6DD0A1B48F90D252DAFCC98141DD34A0E171E0764E6C55894C2A58AC7E90AE1E`
- Packaged INI:
  - SHA-256: `A70976880294353BFACE51EEDBBAC47952A8F376D7ED132A03823A68BBCBD7D0`

The build output, staged copy, package copy, and clean-extracted copy match for
both install files. The packaged DLL contains the stable runtime version string
`4.0.1`. The archive contains exactly:

- `SFSE\Plugins\AbsoluteHOTAS.dll`
- `SFSE\Plugins\AbsoluteHOTAS.ini`

## Publication copy

- [x] Present 4.0.1 as the stable core release and recommended main file.
- [x] Label profiles and macros experimental without labeling the whole build beta.
- [x] Explain the 4.0.0-beta and 3.x upgrade paths separately.
- [x] Document the renderer-utility bind/disable/re-enable workaround.
- [x] Include `bEnableWorkbench = false` in package and Nexus guidance.
- [x] Keep crashes, configuration loss, stuck inputs, and core regressions at the
  top of support triage.

## After 4.0.1

After the release and any required hotfixes, review the mechanism used for each
major function for performance, stability, compatibility, and technical debt.
Potential refactors and downstream-control discovery work are tracked in
[`TECH-DEBT-v4.0.1.md`](TECH-DEBT-v4.0.1.md); they are not release blockers.

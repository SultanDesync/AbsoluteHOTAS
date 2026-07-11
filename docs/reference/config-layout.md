# Configuration Data Contract

Status: **3.1 release baseline**

## Release package

AbsoluteHOTAS ships exactly two files:

```text
Data/SFSE/Plugins/AbsoluteHOTAS.dll
Data/SFSE/Plugins/AbsoluteHOTAS.ini
```

The archive never contains `AbsoluteHOTAS_Custom.ini` or a `Profiles/` directory.
MO2, Vortex, and manual upgrades may therefore replace every shipped file without
touching user-owned data.

## Runtime files

| Path | Owner | Created when | Update policy |
| --- | --- | --- | --- |
| `AbsoluteHOTAS.ini` | mod | installed | replaced on update |
| `AbsoluteHOTAS_Custom.ini` | user/wizard | first Save | never shipped or replaced |
| `Profiles/profile_NN.ini` | user/wizard | first profile creation/export | never shipped or replaced |

The custom file contains bindings, tuning, calibration, custom outputs, `[Macro:*]`
sections, and `[Profiles]` slot routing. Keeping all local customization in one file
makes backup, atomic replacement, and support inspection straightforward.

## Load precedence

```text
AbsoluteHOTAS.ini
-> AbsoluteHOTAS_Custom.ini
-> active Profiles/profile_NN.ini
```

Each layer is loaded into the same SimpleIni object with last value winning. The
shipped INI contains the complete current schema and defaults. A key absent from the
custom file or active overlay falls through to the shipped value.

Hot swaps preload this stack and switch in memory. They never import a profile or
write any file.

## Save rules

- The plugin never writes `AbsoluteHOTAS.ini`.
- Base Save writes `AbsoluteHOTAS_Custom.ini` through a temporary file followed by
  an atomic replace.
- Macro Save replaces only `[Macro:*]` sections inside the custom file and preserves
  all other sections.
- Profile Save writes the selected profile atomically.
- Reset base creates a timestamped backup, clears the custom control payload and
  macros, preserves `[Profiles]` routing, and reloads shipped defaults.
- User-owned files carry `iConfigVersion` for migrations after the 3.1 baseline.
- Unsupported future schema versions must fail visibly rather than being rewritten.

## Profiles

Profile filenames are application-managed: `profile_01.ini` through
`profile_16.ini`. The UI reads the user-facing identity from metadata:

```ini
[Profile]
sName = Precision
sKind = overlay
iSequence = 2
iConfigVersion = 1
```

`iSequence` controls UI ordering. It does not bind the profile to a hot-swap slot.
Hardware-specific routing stays in the custom file:

```ini
[Profiles]
Slot2File = profile_02.ini
Slot2Button = Throttle@7
Slot2Mode = momentary
```

Expanding the wizard's Profiles section for the first time offers three usable
starting points without shipping user-data files: Flight is the base configuration,
FPS is a parked overlay (`bEnableInjection=false`) toggled with F10, and Flight Aux
is an empty inherited overlay toggled with F11. Trigger capture can replace either
keyboard default with a HOTAS button or selector position.

Two profile kinds are valid:

| Kind | Contents | Hot-swap | Import as base |
| --- | --- | --- | --- |
| `full` | materialized effective configuration | yes | yes |
| `overlay` | differences from base | yes | no |

Full export excludes `[Profiles]` routing because slot assignments belong to the
local installation. Import backs up the current custom file, rejects overlays,
replaces the control payload, and preserves local slot routing.

## 3.1 compatibility boundary

3.1 is a fresh configuration baseline. The plugin does not automatically import or
transform pre-3.1 `AbsoluteHOTAS.ini` files or experimental 3.1 profile files.
Release notes must tell existing users to back up the old INI for reference before
their mod manager overwrites it, then rebuild through the binding wizard.

After 3.1, preserving this data contract and providing ordered schema migrations is
a release requirement.

## Safety checklist

- Fresh install contains only DLL + shipped INI.
- No custom file is created before the user saves.
- Updating replaces only DLL + shipped INI.
- Base, macro, profile, backup, and import writes are atomic.
- Full export contains no `[Profiles]` section.
- Overlay import is refused before any backup or write.
- Import preserves current slot routing.
- Reset base preserves profile files and slot routing and can be recovered from its
  `_autobackup_reset_*` file.
- Profile paths cannot escape `Profiles/`.
- A hot swap performs no disk I/O.

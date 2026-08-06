# Legacy ControlMap_Custom.txt Ship-Function Reference

This document records the reverse-engineered format of Starfield's
`ControlMap_Custom.txt` and the complete vanilla binding map for every spaceship
function AbsoluteHOTAS emits. It is the source of truth for the lookup tables in
`ControlMapReader` (`include/ControlMapReader.h`).

File location:
`%USERPROFILE%/Documents/My Games/Starfield/ControlMap_Custom.txt`

## 5.0 status

This table documents the retired 4.x ship-function output path. In 5.0, all 23
named ship actions and named macro targets use validated internal Starfield
operations. They do not emit keyboard/mouse input, do not depend on the player's
bindings, and never fall back to `SendInput`.

The parser and fixtures remain useful as a reverse-engineering reference and for
legacy configuration metadata. Explicit `[ButtonExpansion]` and `key:`/`mouse:`
macro targets are still raw outputs by design, but their values are supplied by
the user rather than resolved as named ship functions through this table.

## Binary format

Despite the `.txt` extension the file is a compact binary override table — it
stores only *deltas* from the base `ControlMap.txt`, not a full dump. A default
profile (every binding vanilla) is **9 bytes**: three empty sections.

```
File    = one or more Sections, laid out back-to-back
Section = 0x03 | lenHi | lenLo | records...        ; len is BIG-endian, counts the 3 header bytes
Record  = context\0  action\0  <8-byte payload>
Payload = device(u32 LE) | token(u16 LE) | flag(u8) | pad(u8)
```

| Payload field | Meaning |
| --- | --- |
| `device` | `1` = keyboard/mouse group (what the plugin can emit). Other values (e.g. gamepad) are skipped. |
| `token`  | keyboard: the **low byte is a Windows Virtual-Key code** (e.g. `K`=`0x004B`, `/`=`0x00BF`, Left=`0x0025`); mouse: `0x0000`–`0x0003`; `0x02FF` = explicitly unbound. |
| `flag`   | `0x00` = primary slot; any other value = an alternate slot |
| `pad`    | `0x00` |

Notes confirmed by capturing real game writes:

- **Sections group records by device:** Section 0 = keyboard, Section 1 = mouse
  (a vanilla-mouse action's primary slot — e.g. Fire Weapon 0's Mouse1 — lives in
  Section 1). A single function's primary + alternate can therefore **span
  sections**, so resolution must aggregate every `device==1` record across all
  sections, not just Section 0. Records with `device != 1` are skipped.
- Editing one function in-game **rewrites that function's full slot set** (both
  primary and alternate are written, even when one is unbound `0x02FF`).
- A `(context, action)` pair can appear in **multiple records** (one per slot),
  so readers must aggregate, never last-write-wins.
- Record order is **not stable** — Starfield reorders Section 0 on rewrite. Key
  on `(context, action)`, never on position.

## Vanilla ship function map

`context / action` is what the file stores. `MAIN` / `ALT` are the two keyboard
slots as shown in Starfield's settings menu (captured with an empty override
file, i.e. true vanilla). `token` is the Starfield key token; `sc` is the
Windows scancode the plugin emits.

| Function | `context / action` | MAIN (key → sc → token) | ALT (key → sc → token) |
| --- | --- | --- | --- |
| Fire Boosters | `ShipHUD / Boosters` | L Shift → `0x2A` → `0x00A0` | — |
| Switch Flight Modes | `ShipHUD / SwitchFlightModes` | Space → `0x39` → `0x0120` | — |
| Toggle POV | `ShipHUD / TogglePOV` | Q → `0x10` → `0x0151` | Mouse3 → — → `0x0002` |
| Fire Weapon 0 | `ShipHUD / WeaponGroup1` | Mouse1 → — → `0x0000` | — |
| Fire Weapon 1 | `ShipHUD / WeaponGroup2` | Mouse2 → — → `0x0001` | — |
| Fire Weapon 2 | `ShipHUD / WeaponGroup3` | G → `0x22` → `0x0147` | Mouse4 → — → `0x0003` |
| Ship Action 1 | `ShipHUD / XButton` | R → `0x13` → `0x0152` | — |
| Select Target | `ShipHUD / SelectTarget` | E → `0x12` → `0x0145` | — |
| Increase System Power | `ShipHUD / Up` | Up → `0x48`\* → `0x0026` | V → `0x2F` → `0x0156` |
| Decrease System Power | `ShipHUD / Down` | Down → `0x50`\* → `0x0028` | C → `0x2E` → `0x0143` |
| Previous System | `ShipHUD / Left` | Left → `0x4B`\* → `0x0025` | Z → `0x2C` → `0x015A` |
| Next System | `ShipHUD / Right` | Right → `0x4D`\* → `0x0027` | X → `0x2D` → `0x0158` |
| Open Scanner | `ShipHUD / SHMonocle` | F → `0x21` → `0x0146` | — |
| Repair | `ShipHUD / RepairShip` | O → `0x18` → `0x014F` | — |
| Ship Alternate Control Hold | `ShipHUD / AltHold` | L Alt → `0x38` → `0x00A4` | — |
| Cruise | `ShipHUD / Cruise` | T → `0x14` → `0x0154` | — |
| Cancel | `ShipHUD_Cancel / Cancel` | — (unmapped) | — |
| Undock / Take-Off | `Spaceship_Interaction / TakeOff` | Space → `0x39` → `0x0120` | — |
| Get Up | `Spaceship_Interaction / Cancel` | E → `0x12` → `0x0145` | — |
| Exit Ship From Cockpit | `Spaceship_Interaction / ExitShip` | X → `0x2D` → `0x0158` | — |
| Zoom Camera In | `ShipFlightCam_FreeRot / FOVZoomIn` | Mouse1 → — → `0x0000` | — |
| Zoom Camera Out | `ShipFlightCam_FreeRot / FOVZoomOut` | Mouse2 → — → `0x0001` | — |
| Autopilot On / Off | `ShipHUD_CruiseMode / LockCourse` | Space → `0x39` → `0x0120` | — |

`*` extended key (`KEYEVENTF_EXTENDEDKEY`).

Two traps baked into this table:

1. **The `Cancel` collision.** `action == "Cancel"` maps to two different
   functions by context: `ShipHUD_Cancel/Cancel` = Cancel,
   `Spaceship_Interaction/Cancel` = Get Up. Always key on `(context, action)`.
2. **Six functions ship with an ALT default** (Toggle POV, Fire Weapon 2, and the
   four System-Power controls). Those ALT keys live in the base `ControlMap.txt`,
   **not** in the override file — a reader watching only the override file is
   blind to them. This matters only in the "user unbinds MAIN but keeps the
   base ALT" edge case.

## Token namespace

A keyboard token's **low byte is a Windows Virtual-Key code** (e.g. `K`=`0x4B`,
`/`=`VK_OEM_2` `0xBF`, Left=`VK_LEFT` `0x25`). `TokenToOutput` converts it to a
layout-correct scancode at runtime via `MapVirtualKey` (`MAPVK_VK_TO_VSC_EX`,
which also yields the extended-key flag) — no hardcoded table. Mouse tokens are
`0x0000`–`0x0003` = mouse buttons 1–4. The unbound sentinel is `0x02FF`.

The high byte varies by writer: Starfield's in-game menu writes `0x00 | VK`,
while the retired configurator wrote `0x01 | VK` for most keys. Both carry the
same VK in the low byte, so masking the low byte handles either. (The `token`
values in the map above are the configurator's `0x01xx` form; the game writes the
`0x00xx` form of the same key.)

## Output resolution

For each function, given the `device==1` records aggregated across **all
sections** (`ControlMapFile::DeviceRecords`) and its baked-in vanilla default:

| State for the function | Plugin output |
| --- | --- |
| Bound primary (`flag 0x00`) override | follow the override — the main rebind case |
| Primary `0x00` unbound, a bound alternate slot present | follow the alternate |
| Primary `0x00` unbound, no usable alternate | none (user cleared the binding) |
| No primary override record | vanilla default (primary slot untouched) |

This is implemented in `ControlMap::ResolveBinding`.

## Provenance / confidence

- Format and `(context, action)` names: originally from the (now-retired)
  AbsoluteHOTAS-Configurator; the plugin reader is **read-only** and does not
  preserve configurator conventions.
- The VK token encoding, the `flag 0x00` primary-write behavior, and the
  **cross-section** layout (mouse primary in Section 1) were all confirmed
  against Starfield's own in-game rebind writes — e.g. Cruise→K wrote
  `ShipHUD/Cruise = 0x004B` (§0), and unbinding Fire Weapon 0's primary wrote
  `ShipHUD/WeaponGroup1 = 0x02FF` in **Section 1** while the new key landed in §0.
- Vanilla MAIN/ALT map: confirmed against the in-game settings menu with an
  empty override file.
- An early `Section 1` record (`tok=0x0004`) was initially dismissed as probe
  residue; that was wrong — Section 1 is the **mouse** device group and is part
  of the canonical format. The reader now reads all `device==1` sections.

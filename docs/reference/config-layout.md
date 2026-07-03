# Config Split & Migration (design)

Status: design / planned for 3.1.

## Goal

Make upgrades safe and trivial. Today everything lives in one `AbsoluteHOTAS.ini`
that the wizard rewrites and that a release also ships — so an update can clobber a
user's bindings. Split the file by **write authority** so a new release can
overwrite the mod-owned files freely while the user's bindings and macros persist
untouched.

This solves *overwrite-on-update*. It does **not** solve *semantic* changes (a key
whose meaning changes between versions) — that needs the config version stamp +
migrations in the last section.

## File layout

| File | Owner | Shipped in archive? | Overwrite on update? |
| --- | --- | --- | --- |
| `AbsoluteHOTAS.ini` | mod (defaults + tunables + doc comments) | yes | **yes, freely (3.2+)** |
| `AbsoluteHOTAS_User.ini` | wizard (bindings, calibration, tuning) | no (created at runtime) | never |
| `AbsoluteHOTAS_Macros.ini` | wizard Macros tab (`[Macro:*]`) | no (created at runtime) | never |

Paths come from `RuntimePaths` ([RuntimePaths.cpp:31](../../src/RuntimePaths.cpp)).
Add `UserIniPath()` and `MacrosIniPath()` alongside `IniPath()`.

Keep it to three files. More than that makes support ("send me your INIs") and the
user's mental model worse.

## Ownership table

Load is **always layered** (defaults overlaid by user, per-key — see below), so
*anything* can be overridden by the user file. This table defines **write
authority**: which keys the wizard writes to the user file and which keys migration
lifts out of the old monolith. Everything not listed as user-owned stays mod-owned.

**User-owned → `AbsoluteHOTAS_User.ini`** (wizard-written, migrated):

- `[Hardware]` — entire section (axis bindings, invert, sensitivity, saturation, deadzone)
- `[Buttons]` — entire section (`iActivateButtonId`, `iStopButtonId`, `iToggleWizardButton`, `bAlwaysOn`, `iToggleActiveKey`)
- `[ShipButtons]` — entire section
- `[ShipButtonOutputs]` — entire section (explicit output overrides; usually empty thanks to ControlMap sync)
- `[ButtonExpansion]` — entire section
- `[Normalization]` — entire section (throttle shaping = user calibration)
- `[DigitalAxes]` — entire section
- `[Aim]` — entire section (bindings + tuning + HOSAM/alignment)
- `[DualStick]` — entire section
- `[Calibration]` — entire section (auto-generated per-device ranges)
- `bHoldForBoost` — **currently mis-homed in `[Injection]`.** Relocate to `[DualStick]` (or `[Normalization]`) and read the old `[Injection]` location as a migration alias.

**Mod-owned → `AbsoluteHOTAS.ini`** (shipped defaults, overwrite-safe):

- `[General]` — `bEnabled`, `bSyncShipOutputsFromControlMap` (user-overridable via overlay, but default shipped here)
- `[Injection]` — `iPollRateHz`, `iThrottleBurstMs`, `bRollEnabled`, `bEnableLog`, `bSignalHunterFallback`

`[Macro:*]` → `AbsoluteHOTAS_Macros.ini`. Brand-new in 3.1, so nothing to migrate;
the file just starts absent/empty. Keeping macros in their own file also avoids the
"enumerate and delete every `[Macro:*]` section on each wizard save" problem the
monolith would have.

> Note `InitLogging()` ([RuntimePaths.cpp](../../src/RuntimePaths.cpp))
> reads `bEnableLog` straight from `IniPath()`. It stays mod-owned in the main
> file, so that path is unchanged.

## Load path (layered overlay)

`CSimpleIniA::LoadFile` merges into the existing object, last value winning for
single-value keys (default `bAllowMultiKey=false`). So the overlay is three loads
into one `ini`, in priority order:

```cpp
void ThrottleController::LoadConfig() {
    MigrateIfNeeded();              // see below; runs once, before any load

    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(RuntimePaths::IniPath().string().c_str());        // 1. mod defaults
    ini.LoadFile(RuntimePaths::UserIniPath().string().c_str());    // 2. user overrides win
    ini.LoadFile(RuntimePaths::MacrosIniPath().string().c_str());  // 3. macros merge in

    // ... existing GetValue() calls unchanged; MacroEngine::LoadMacros(ini) still
    //     sees [Macro:*] because they were merged into the same object.
}
```

The rest of `LoadConfig` is untouched — same `ini` object, same `GetValue` calls,
same defaults. `MacroEngine::LoadMacros(ini)` ([ThrottleController.cpp:183](../../src/ThrottleController.cpp))
keeps working because macros are merged before it runs.

> Verify on implementation: confirm sequential `LoadFile` overwrites single-value
> keys (it should with multikey off). If a build ever flips multikey on, this
> assumption breaks.

## Save path (wizard)

`SaveBindingsToINI()` ([WizardConfig.cpp:192](../../src/WizardConfig.cpp))
currently loads/saves `IniPath()`. Change it to operate on `UserIniPath()`:

- Load `UserIniPath()`, `SetValue` the user-owned keys, save `UserIniPath()`.
- **Write nothing to `IniPath()`.** This is the one correctness invariant — a single
  user key still written to the main file reintroduces the clobber bug.
- Macro tab writes `MacrosIniPath()` (separate save).
- `bHoldForBoost` write moves to its new home (`[DualStick]`).

A partial section in the user file is fine: it only needs the keys the user changed;
the overlay fills the rest from defaults.

## Migration (v3.0.x monolith → split)

**Trigger: first load, not first save.** If it waited for a wizard Save, an upgrader
would boot 3.1 with no bindings until they happened to open the wizard — the exact
data loss we're preventing. So migration runs at the top of `LoadConfig`, before the
overlay reads anything.

### The chicken-and-egg with "overwrite freely"

Migration reads the old bindings *out of the old `AbsoluteHOTAS.ini`*. But install is
a file copy that runs before our DLL — if the 3.1 archive overwrites the main ini
with shipped blank defaults, the bindings are gone before migration can read them.

So **the 3.1 release is a one-time exception: it must not overwrite an existing
`AbsoluteHOTAS.ini`.** Ship it FOMOD "skip if present," or ship it as
`AbsoluteHOTAS.ini.default` and let the DLL generate the real one. From **3.2
onward** the main ini can be overwritten freely, because no user data lives there.

### Algorithm (`MigrateIfNeeded`)

1. If `AbsoluteHOTAS_User.ini` exists → already migrated. Return. (Idempotent gate.)
2. Else:
   a. **Back up** the existing `AbsoluteHOTAS.ini` → `AbsoluteHOTAS.ini.v30.bak`. Never destroy the source.
   b. Load the old `AbsoluteHOTAS.ini`. For each **user-owned** section/key in the table above, copy it into a fresh `AbsoluteHOTAS_User.ini` (apply the `bHoldForBoost` alias).
   c. Stamp `iConfigVersion` in the user file.
   d. Save `AbsoluteHOTAS_User.ini`. `AbsoluteHOTAS_Macros.ini` stays absent (no pre-3.1 macros).
3. Continue into the layered load.

### Edge cases

- **Fresh install** (only shipped default present, no real bindings): same path runs
  harmlessly — user-owned keys are empty/default, so it writes an effectively empty
  user file. No special-casing needed.
- **Re-run safety:** gating on `AbsoluteHOTAS_User.ini` existence makes it run exactly
  once. A user who deletes their user file gets a re-migrate from the `.bak` if the
  monolith is gone — optional: fall back to `.bak` if main ini has no user keys.
- **Read-only / locked file:** if the user file can't be written, log and run from
  in-memory defaults+monolith for the session rather than crashing.

## Config versioning (semantic changes)

File-splitting protects bindings from being *overwritten*. It does nothing when a
key's *meaning* changes (e.g. a future analog to the 3.0→3.1 deadzone fix, but
config-side). Defend that separately:

- Stamp `iConfigVersion` in `AbsoluteHOTAS_User.ini`.
- On load, if the stamp is older than current, run ordered one-shot migrations
  (rename keys, rescale values, relocate `bHoldForBoost`, etc.), then rewrite the
  stamp.
- Keep migrations as small pure transforms on the loaded `ini` before `GetValue`.

`bHoldForBoost`'s relocation is the first such migration and a good template.

## Implementation touch list

- `src/RuntimePaths.{h,cpp}` — add `UserIniPath()`, `MacrosIniPath()`.
- `ThrottleController::LoadConfig` — add `MigrateIfNeeded()` + the two overlay loads.
- New `MigrateIfNeeded()` (ThrottleController or a small `ConfigMigration` unit) — backup, split, stamp.
- `WizardConfig::SaveBindingsToINI` — retarget to `UserIniPath()`; never touch main ini; relocate `bHoldForBoost`.
- Macros save/load — `MacrosIniPath()` (lands with the macro builder work).
- `AbsoluteHOTAS.ini` shipped file — strip user-owned sections down to documented defaults only.
- Packaging — 3.1 archive must not overwrite an existing main ini (FOMOD skip-if-present or `.default` + DLL generate).

## Test checklist

- Fresh install → empty user file created, defaults apply, fly works.
- v3.0.x upgrade (main ini preserved) → bindings/calibration intact, `.v30.bak` written, `_User.ini` populated.
- v3.0.x upgrade where main ini *was* overwritten → bindings lost (this is the case packaging must prevent; verify the `.default`/skip path avoids it).
- Wizard Save → writes only `_User.ini` (+ `_Macros.ini`); main ini untouched (diff it).
- Simulate 3.2 update: overwrite main ini → user bindings still load.
- `bHoldForBoost` set in an old `[Injection]` → still honored after migration.
- Delete `_User.ini` and relaunch → idempotent re-create (from `.bak`/monolith if present).

# AbsoluteHOTAS Driver Plugin

AbsoluteHOTAS needs a tiny game-data plugin in addition to the SFSE DLL and
Papyrus scripts. The DLL exposes the native functions; the plugin starts a
quest that attaches `AbsoluteHOTASPlayer` so Papyrus can tell the DLL when the
player sits in, or leaves, a ship pilot seat.

## Target Artifact

- `Data\AbsoluteHOTAS.esm`
- ESL/light master flagged if Creation Kit offers the option.
- Master: `Starfield.esm`
- One start-game-enabled quest.
- Quest script: `AbsoluteHOTASPlayer`
- Script property:
  - `Ship_PilotSeat_RefType` = vanilla `Ship_PilotSeat_RefType`

## Creation Kit Pass

1. Deploy or copy the current payload so CK can see:
   - `Data\Scripts\AbsoluteHOTAS.pex`
   - `Data\Scripts\AbsoluteHOTASPlayer.pex`
   - `Data\Scripts\Source\AbsoluteHOTAS.psc`
   - `Data\Scripts\Source\AbsoluteHOTASPlayer.psc`

2. Open `CreationKit.exe`.

3. Load `Starfield.esm`.

4. Create a new quest:
   - Editor ID: `AbsoluteHOTAS_DriverQuest`
   - Name: `AbsoluteHOTAS Driver`
   - Type: Miscellaneous
   - Start Game Enabled: checked
   - Run Once: unchecked

5. Attach script `AbsoluteHOTASPlayer` to the quest.

6. Fill script property:
   - Property: `Ship_PilotSeat_RefType`
   - Value: `Ship_PilotSeat_RefType`

7. Save as `AbsoluteHOTAS.esm`.

8. Compact/flag as light/ESL if CK offers it. This plugin has only one new
   record, so it is safe to keep light.

9. Put `AbsoluteHOTAS.esm` in:
   - `contrib\PluginRelease\Data\AbsoluteHOTAS.esm`

10. Re-run:
   - `tools\deploy_payload.ps1`

## Runtime Check

With `bLogThrottle=true`, `Data\SFSE\Plugins\AbsoluteHOTAS.log` should show:

- `[Main] Plugin load complete.`
- `[PapyrusHook] Installed at Starfield.exe+33611E7`
- `[Papyrus] Pilot state entered.`
- `[Controller] Config Loaded - AbsoluteHOTAS 6DOF Dashboard Initialized.`

The controller should start only after entering the pilot seat.

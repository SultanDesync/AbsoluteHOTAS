# Reliability update validation

Build locally with deployment disabled:

```powershell
xmake f -y -m debug --deploydir=
xmake build AbsoluteHOTAS
xmake test
xmake f -y -m releasedbg --deploydir=
xmake build AbsoluteHOTAS
xmake test
./tools/verify-package.ps1
```

The package validator requires exactly `SFSE/Plugins/AbsoluteHOTAS.dll` and
`SFSE/Plugins/AbsoluteHOTAS.ini`, compares them with the build/source, creates
`build/AbsoluteHOTAS-validation.zip`, and checks entry names and content hashes.
It refuses a stage containing custom configuration, profiles, logs, or any extra
file. The GitHub workflow runs the same two build modes and retains that ZIP.

Standalone coverage includes real-zero versus missing samples, neutral/release
rearming, calibrated centered axes, positional/rate/inverted/reverse-zone throttle
neutral conditions, GUID parsing and enumeration reordering, duplicate-device
reassignment, macro cancellation limited to disconnected hardware, report snapshot
publication, export replacement failures, and current/older menu-host descriptors.
The recovery test explicitly defines `NDEBUG`; test requirements still execute.

In-game smoke checks remain necessary before publishing a release:

1. Disconnect throttle while holding forward thrust, reverse, or boost. Confirm
   the affected override releases and keyboard controls remain usable. Reconnect
   at a non-neutral position, then return to idle/zero thrust before moving again.
2. Repeat for calibrated pitch/yaw/roll/strafe and independent aim. Confirm no
   nonzero injection resumes before neutral, including with inversion/smoothing.
3. Disconnect while holding a weapon, menu button, POV direction, or long macro.
   Reconnect while still holding it; release and press again to rearm.
4. Record both identical sticks, restart with reversed enumeration order, and
   confirm GUID bindings still resolve to the intended instances. Check indexed
   calibration separately; its existing storage format is unchanged.
5. Export the report with logging off, inspect its active-profile and recovery
   details, and repeat under MO2 to confirm the Overwrite destination. Confirm
   failed export reports an error and leaves the preceding report intact.

Hardware absent when the game enumerates devices is not discovered dynamically.
GUIDs may change after driver reinstallation or physical device replacement;
legacy bindings and calibration are not silently rewritten.

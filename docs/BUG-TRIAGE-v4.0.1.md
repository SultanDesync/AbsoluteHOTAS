# 4.0.1 Bug Intake and Triage

This is the working process for turning 4.0.0-beta field reports into a focused
4.0.1 stabilization release. GitHub issues are the report system of record; this
document defines how reports move from intake to a release decision.

## Intake

Direct reporters to the repository's **Bug report** issue form. A useful report has:

- exact AbsoluteHOTAS, Starfield, and SFSE versions;
- exact controller names and the affected axis, button, profile, or macro;
- install environment and relevant input, overlay, UI, or ship-control mods;
- expected and observed behavior, numbered reproduction steps, and frequency;
- `AbsoluteHOTAS.log` captured with `bEnableLog=true`;
- relevant `AbsoluteHOTAS.ini`, `AbsoluteHOTAS_Custom.ini`, and profile files.

Ask reporters to remove private information before uploading files. Do not diagnose
from a settings screenshot when the underlying text files can be attached.

## First pass

For each new report:

1. Confirm that the report concerns an official build and record its exact version.
2. Search for duplicates; link the canonical issue before closing a duplicate.
3. Check that reproduction steps and evidence are sufficient. Request only the
   missing evidence, then mark the report as needing information.
4. Try to classify the affected subsystem: startup/hooking, axis injection,
   outputs, configuration, profiles, macros, workbench/renderer, packaging, or docs.
5. Assign one severity and one state from the tables below.

Do not treat absence of a log as proof that the plugin did not load: logging is
opt-in. Ask for one clean reproduction with logging enabled.

## Severity

| Severity | Definition | Response |
| --- | --- | --- |
| S0 blocker | Crash loop, configuration loss/corruption, or synthetic input that remains stuck after stop/exit | Investigate immediately; block 4.0.1 |
| S1 major | Repeatable regression in startup, saving, pitch, yaw, roll, throttle, strafe, or reverse | Prioritize for 4.0.1 |
| S2 normal | Repeatable profile/macro failure or substantial workbench problem with a workaround | Fix when reproducible and bounded |
| S3 polish | UX confusion, documentation, cosmetic issue, or low-impact edge case | Batch after stability work |
| Feature | New behavior rather than a regression or defect | Defer unless explicitly accepted into scope |

Severity describes user impact, not how difficult the fix appears.

## State

Use this progression:

`new` → `needs-info` or `needs-repro` → `confirmed` → `in-progress` →
`needs-verification` → `fixed`

`duplicate`, `cannot-reproduce`, `declined`, and `deferred` are terminal outcomes.
Whenever a state changes, leave a short comment stating the evidence or decision.

## Reproduction and fixes

- Reproduce against the current 4.0.1 branch with a minimal configuration when
  practical, then restore the reporter's relevant settings one group at a time.
- Preserve the original report files; attach a minimized reproducer separately.
- Add or extend a standalone test when the affected logic can be exercised without
  the game.
- Keep one issue per independently verifiable defect. Split secondary discoveries.
- Link the issue from the fixing commit or pull request.
- A fix is not complete until the original steps no longer reproduce and adjacent
  stop/reload/profile-transition behavior has been checked for regressions.

## 4.0.1 release gate

4.0.1 is ready for stable promotion when:

- no confirmed S0 or S1 issue remains open;
- fixed S0/S1 reports have been verified on a packaged release candidate;
- representative startup, core axes, saving/reload, stop, profiles, and macros pass;
- the release archive passes a clean-extraction install check;
- known S2/S3 issues are documented or explicitly deferred; and
- installation and fresh-4.0 configuration guidance still matches the package.

Before cutting the release, summarize fixed, deferred, and unconfirmed reports in
the release notes. Do not silently close beta reports solely because 4.0.1 ships.

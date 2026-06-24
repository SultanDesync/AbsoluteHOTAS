# Release Plan — AbsoluteHOTAS v3.1.0-beta

**Target:** Wednesday, July 1, 2026
**Branch:** `3.1-beta`
**Drafted:** 2026-06-23

Working runway from draft to target: Jun 23–26 (Tue–Fri), Jun 29–30, Jul 1 (Mon–Wed) ≈ 7 working days.

## Theme

Two headline features plus the version roll:

1. **ControlMap-aware ship output** — read `ControlMap_Custom.txt` so plugin output follows the user's in-game Starfield rebinds automatically.
2. **Macros (first pass)** — key combos (simultaneous chord) paired with turbo / auto-repeat.

---

## Phase 0 — Done

- [x] Bump version to `3.1.0-beta` (CMake `PROJECT_VERSION`, INI banner, README header + changelog stub).
- [x] Create `3.1-beta` branch.
- [x] `ControlMapReader` module scaffolded (parser + token table + `ResolveBinding`), standalone tests passing against real captured fixtures.
- [x] Format + vanilla-map documentation (`docs/reference/control-map-ship-functions.md`).

## Phase 1 — Macro design (planning session) · _gate before any macro code_

- [ ] Hold the macro planning session. Direction: **chord** (multi-key simultaneous press) as the first pass, **combined with turbo** (auto-repeat while held).
- [ ] Decide INI syntax (new `[Macros]` section vs. extending `[ButtonExpansion]` / `[ShipButtonOutputs]` output grammar).
- [ ] Decide emission model in `ShipOutput`: how a chord holds/releases multiple keys via the existing reference-counted held-output system, and how turbo interval is configured + ticked.
- [ ] Decide interaction with existing Hold/Pulse modes and the held-key ownership model.
- [ ] Capture the design as `docs/reference/macros.md` (or a design note) before implementing.

## Phase 2 — Macros first pass

- [ ] Implement chord output (multi-key press/release through the held-output owners).
- [ ] Implement turbo / auto-repeat (interval-driven re-pulse while button held).
- [ ] Unit tests for parsing + emission scheduling where feasible.

## Phase 3 — ControlMap reader integration

- [ ] Clean-room confirm the in-game **write** shape: from empty baseline, rebind one primary (Boosters→J), flush, decode; add as a third test fixture.
- [x] Wire `ResolveBinding` into `LoadShipButtonBindings` as the middle precedence layer: vanilla default → control-map-derived → explicit `[ShipButtonOutputs]`. Added `[General] bSyncShipOutputsFromControlMap` (default true), the actionId→(context,action) table, path resolution via `FOLDERID_Documents`, and per-action realignment logging. Builds clean via `release-user`.
- [x] Unpin default `[ShipButtonOutputs]` (commented out) so the control-map layer actually engages — explicit values otherwise win and the feature was inert. Verified the wizard does **not** re-write `[ShipButtonOutputs]` on Save, so the trigger can't re-pin it.
- [x] Reload trigger decided: **startup + wizard Save** (Save already calls `ReloadConfig` → re-reads the map). No file-watch — bindings are static state; startup is the primary read. Dedicated hotkey is an optional future add.
- [ ] **In-game verification**: with an INI that has no explicit `sXxxOutput`, rebind a ship primary in Starfield, confirm output follows it + the `[ShipOutput] ControlMap sync:` log lines. (Existing INIs keep explicit outputs — delete the deployed INI to re-seed, or clear the lines.)
- [ ] Tests for the precedence layering (fold into the pending CTest target).
- [ ] Release-notes caveat: existing installs keep their explicit `[ShipButtonOutputs]`; document "clear them to enable auto-follow."

## Phase 3.5 — Overlay hook compatibility (best-effort, non-blocking)

Adopted stance: match established best practice (Special K / ReShade / RTSS), don't out-engineer it. See `docs/reference/overlay-hook-compatibility.md`. Driven by a reporter's "cursor works, no GUI" log (prior render-chain hook, likely NVIDIA Streamline).

- [ ] Wait on reporter's retest with 3.0.1 + FG/driver-layers off (confirms which layer wins the hook race).
- [x] **Detect-and-tell**: prior-hook detection added to `UIHook::Install` (`LooksHooked` + warning over all 5 render entry points). Builds clean via `release-user`. _Pending in-game verification._
- [ ] **Queue-association capture**: replace the first-seen DIRECT-queue heuristic in `HookedExecuteCommandLists` with "DIRECT queue that fed the most recent Present." Strictly better than first-seen even single-injector; needs in-game verification.
- [ ] Seed the "Known incompatibilities" list (done in the compat doc) and link it from the README.
- [ ] Does **not** block Jul 1 — pre-existing environmental incompatibility, not a regression.

## Phase 4 — Build & verify

- [x] Full plugin build under MSVC `/W4` (vcpkg / CommonLibSF toolchain) — `ControlMapReader.cpp` compiles + links into `AbsoluteHOTAS.dll` clean via the `release-user` preset (only the project-wide benign `D9025 /Ob1→/Ob3` flag warning). _Re-run after Phases 2–3 land more code._
- [ ] In-game smoke test: macros (chord + turbo) and ControlMap auto-alignment with a real rebind.
- [ ] Fix fallout.

## Phase 5 — Release mechanics

- [ ] Replace the README `### v3.1.0-beta (in development)` stub with real changelog bullets (ControlMap auto-align, macros).
- [ ] Build `package-mo2` target → `releases/AbsoluteHOTAS-v3.1.0-Release.zip`.
- [ ] Tag `v3.1.0-beta`.
- [ ] Draft release notes.
- [ ] Publish (Nexus / GitHub release — confirm channel).

---

## Suggested sequencing toward Jul 1

| Window | Focus |
| --- | --- |
| Jun 23–25 | Phase 1 macro planning session + design note; Phase 3 clean-room confirm |
| Jun 26 / Jun 29 | Phase 2 macro implementation; Phase 3 wiring |
| Jun 30 | Phase 4 build + in-game testing |
| Jul 1 | Phase 5 package, tag, notes, publish |

## Open decisions

- **Macro INI syntax & emission model** — the Phase 1 planning session settles this.
- **Publish channel** — Nexus, GitHub release, or both?
- **Scope guard** — if the macro planning session reveals more depth than a "first pass" allows, ship ControlMap auto-align alone on Jul 1 and defer macros to 3.1.1 rather than slip the date.

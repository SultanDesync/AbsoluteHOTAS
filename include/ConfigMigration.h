#pragma once

// ============================================================================
// ConfigMigration — one-time v3.0.x monolith -> split config migration.
//
// Pre-3.1 shipped a single AbsoluteHOTAS.ini that held both mod defaults and the
// user's bindings, so a release could clobber tuned setups. 3.1 splits by write
// authority (see docs/reference/config-layout.md): mod-owned stays in the main
// ini; user-owned (bindings, calibration, tuning, macros, layers) moves to
// AbsoluteHOTAS_User.ini, which is never shipped and never overwritten.
//
// MigrateIfNeeded() lifts the user-owned sections out of an existing monolith on
// first load. It is idempotent (gated on the user file's existence) and never
// destroys the source — the old ini is backed up, not modified.
// ============================================================================

#include <SimpleIni.h>

namespace ConfigMigration {

// Config schema version stamped into the user file. Bump when a key's *meaning*
// changes (not just its file) and add an ordered migration keyed on this stamp.
inline constexpr int kConfigVersion = 1;

// Pure split transform: copy the user-owned sections out of a loaded monolith
// (`src`) into a fresh user config (`dst`), applying the bHoldForBoost relocation
// and stamping iConfigVersion. No filesystem, no RuntimePaths — this is the seam
// the unit test drives. MigrateIfNeeded() wraps it with the file I/O and gating.
void SplitUserConfig(const CSimpleIniA& src, CSimpleIniA& dst);

// Run the monolith->split migration exactly once, before the layered config load.
// No-op if AbsoluteHOTAS_User.ini already exists. Best-effort: on any file error
// it logs and returns, leaving the layered load to fall back to the (still-intact)
// monolith for the session.
void MigrateIfNeeded();

} // namespace ConfigMigration

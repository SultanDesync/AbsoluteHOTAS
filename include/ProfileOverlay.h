#pragma once

// ============================================================================
// ProfileOverlay — the sparse-diff at the heart of profile editing.
//
// A switch profile is stored as an *overlay*: only the keys whose value differs
// from the base config. This transform computes that overlay from the effective
// config (base + the user's edits) and the pristine base. Getting it right is the
// requirement the whole profile UX rests on — a full dump instead of a diff freezes
// a profile into a copy that stops tracking base. See profile-switching.md.
//
// Pure and filesystem-free so it can be unit-tested (config_overlay_test).
// ============================================================================

#include <SimpleIni.h>

namespace ProfileOverlay {

// Merge the sparse overlay of (eff vs base) into `out`:
//   - a key whose eff value differs from base (or is absent in base) is written;
//   - a key whose eff value equals base but is present in `out` is removed (the
//     user reverted an earlier override).
// `out` is merged into, not cleared, so overrides from earlier sessions that were
// not touched this time survive. Returns the number of overrides written.
int ComputeDiff(const CSimpleIniA& eff, const CSimpleIniA& base, CSimpleIniA& out);

} // namespace ProfileOverlay

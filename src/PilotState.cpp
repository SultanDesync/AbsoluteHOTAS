#include "PCH.h"
#include "PilotState.h"
#include "RuntimePaths.h"

// ============================================================================
// PilotState — the gate's "is the player piloting" signal.
//
// WORKING signal: the manual toggle (SetPiloting / Toggle, bound to a config key).
// This is the only validated pilot signal today.
//
// DEAD ENDS — automatic pilot detection. Every native signal investigated was
// invalidated. There is currently NO working automatic pilot signal. Do not
// re-attempt these without new evidence:
//
//   1. GetSpaceshipPilot (Address Library ID 173834). The function ID mis-resolved
//      after the 1.16.x patch — the versionlib entry was present but pointed at an
//      unrelated 4-arg handle-table routine, so calling it access-violated at the
//      gate's poll rate. Function-ID *calls* are fragile across game patches and
//      unsafe as a gate; prefer read-only signals.
//   2. PlayerCamera camera-state read (currentState at +0x10 vs cameraStates[] at
//      +0x188). Starfield renders first-person COCKPIT piloting with the same
//      kFirstPerson camera state as on-foot first person — camera-indistinguishable,
//      so it cannot gate the FP-cockpit case. Dead as a standalone signal.
//   3. Source-object +0x1B4 flag. Turned out to be a MENU-OPEN flag (0 only while a
//      game menu is up), not a piloting flag.
//
// A real signal almost certainly exists — piloting swaps in an entire HUD and input
// scheme — but finding it is deferred. Until then, Auto mode falls back to the
// manual value (transparent by default), and the +0x3C edge-trigger fix in
// ThrottleHook removes the underlying reason the gate was needed for on-foot sprint.
// ============================================================================

namespace PilotState {

// Default TRUE so the gate is transparent until something says otherwise.
static std::atomic<bool> s_piloting{ true };

bool IsPiloting() {
    return s_piloting.load(std::memory_order_relaxed);
}

void SetPiloting(bool piloting) {
    s_piloting.store(piloting, std::memory_order_relaxed);
}

void Toggle() {
    SetPiloting(!s_piloting.load(std::memory_order_relaxed));
}

bool Update(bool autoSource, float /*dt*/, int /*debounceMs*/) {
    // Auto mode is a DEAD END for now (see header): no validated automatic pilot
    // signal exists, so fall back to the manual value — the gate stays safe and
    // transparent. Log once so an Auto config setting has visible, defined behavior.
    if (autoSource) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            RuntimePaths::Log("[PilotState]",
                "PilotSignal=Auto, but no automatic pilot signal is available "
                "(all candidates invalidated) — using the manual value.");
        }
    }
    return s_piloting.load(std::memory_order_relaxed);
}

}  // namespace PilotState

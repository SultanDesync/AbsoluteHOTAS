#pragma once

// ============================================================================
// PilotState — the gate's "is the player piloting" signal.
//
// Manual mode: a toggle key flips the piloting value. This is the only validated
// pilot signal and drives the gate plumbing (InjectionOnly / Full).
//
// Auto mode: DEAD END for now. Every automatic pilot signal investigated
// (GetSpaceshipPilot ID, PlayerCamera camera-state, source-object +0x1B4) was
// invalidated — see the dead-ends catalogue in PilotState.cpp. Auto currently
// falls back to the manual value; do not re-attempt those approaches without new
// evidence.
// ============================================================================

namespace PilotState {

// Flip the current state (bound to the test toggle key).
void Toggle();

// Refresh and return the piloting signal. autoSource=false → manual toggle value;
// autoSource=true → currently also the manual value (no working auto signal; see
// PilotState.cpp).
bool Update(bool autoSource);

}  // namespace PilotState

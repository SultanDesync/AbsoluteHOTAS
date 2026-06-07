#pragma once
#include "ThrottleController.h"
#include <cstdint>

// ============================================================================
// SignalHunter — Throttle pointer discovery, plausibility validation,
//                and direct-memory injection.
//
// Runs exclusively on the ThrottleController loop thread. Manages the full
// lifecycle from initial AOB-triggered candidate collection through magic-
// number lock, delta-burst injection, accumulator throttle, reverse gate, and
// reacquire watchdog.
// ============================================================================
namespace SignalHunter {

// Called once per frame while the loop is running.
// candCount  — number of hook candidates available this frame
// throttle   — normalised throttle [0,1] (absolute mode) or rate [-1,1] (accum mode)
// dt         — actual frame delta time in seconds
// iter       — monotonic loop iteration counter
void Tick(int candCount, float throttle, float dt, uint64_t iter);

// Main injection call — writes throttle and rotation to game memory.
// All six DOF values are in [-1, +1] normalised range.
void Inject(float throttle, float pitch, float yaw, float roll,
            float strafeX, float strafeY, float dt, uint64_t iter,
            bool reverseHeld);

// Disarm all state (called on pilot-seat exit).
void Disarm();

// Re-arm for reacquire (called on activate button or persistent signal loss).
void ArmForReacquire(const char* reason);

// Query current state
bool     IsLocked();
uintptr_t GetActivePtr();

} // namespace SignalHunter

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
// iter       — monotonic loop iteration counter
void Tick(int candCount, uint64_t iter);

// Main injection call — writes throttle and rotation to game memory.
// All six DOF values are in [-1, +1] normalised range.
// suppressPitchYaw — when true (HOSAM mode), releases the yaw/pitch cluster
//                    gates so the game's native mouse pipeline owns steering.
void Inject(float throttle, float pitch, float yaw, float roll,
            float strafeX, float strafeY, float dt,
            bool reverseHeld, bool suppressPitchYaw = false,
            bool strafeLatActive = false, bool strafeVertActive = false,
            bool cruiseOverride = false, float cruiseTarget = 0.0f);
float GetCurrentThrottleTarget();

// Disarm all state (called on pilot-seat exit).
void Disarm();

// Park flight-axis injection (silence / overrides / aim / accumulator) WITHOUT
// releasing discrete bindings — used by the InjectionOnly pilot gate. Capture
// stays enabled so injection resumes cleanly when piloting returns.
void SuspendInjection();

// Re-arm for reacquire (called on activate button or persistent signal loss).
void ArmForReacquire();

} // namespace SignalHunter

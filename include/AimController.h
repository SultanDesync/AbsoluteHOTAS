#pragma once
#include "ThrottleController.h"

// ============================================================================
// AimController — Source-object reticle injection and HOSAM alignment assist.
//
// Handles all aim-related memory writes to the source object's mouse
// accumulator lanes (+0x4C yaw, +0x50 pitch).  Called once per control-loop
// frame after rotational overrides are set.
// ============================================================================
namespace AimController {

// Process one frame of aim input.
// pitch/yaw  — flight-stick values in [-1,+1] (used for mirror/aim-driven modes)
// hasSeparateAimInput  — true if independent analog or digital aim axes are bound
// hasSeparateAimAxes   — true if analog aim axes specifically are bound
// hasDigitalAimButtons — true if any directional digital aim button is bound
// sourceObjectAimAllowed — false when binding policy gives vanilla mouse
//                          steering exclusive ownership of the accumulator
// dt         — actual frame delta time in seconds
void Update(const ThrottleController::Config& cfg,
            float yaw, float pitch,
            bool hasSeparateAimInput,
            bool hasSeparateAimAxes,
            bool hasDigitalAimButtons,
            bool sourceObjectAimAllowed,
            float dt);

} // namespace AimController

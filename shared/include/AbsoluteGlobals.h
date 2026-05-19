#pragma once
#include <atomic>
#include <cstdint>

// ---- Absolute v3.5 Pure 1.95 Restoration State ----
// These globals manage the synchronization between the Physics Hooks and the Controller.

namespace AbsoluteGlobals {
    // Safety Gates
    extern std::atomic<uint8_t> g_magicArmed;   // Locked & Validated (0.0314f seen)
    extern std::atomic<uint8_t> g_isArmed;      // Camera 32-36 gate
    extern std::atomic<uint8_t> g_isStandingDown; // Autonomous Handover
    extern std::atomic<uintptr_t> g_lockedRDI;
    extern std::atomic<uintptr_t> g_lockedRCX;
    extern std::atomic<uintptr_t> g_capturedRDI;
    extern std::atomic<uintptr_t> g_capturedRCX;
    extern std::atomic<float> g_axisPitch;   // -1.0 to 1.0, normalized
    extern std::atomic<float> g_axisYaw;
    extern std::atomic<uint8_t> g_probeHotWriteActive;
    extern std::atomic<uint32_t> g_probeHotWriteBits;
    extern std::atomic<uint8_t> g_probeInstallRollGate;
    extern std::atomic<uint8_t> g_probeInstallPitchGate;
    extern std::atomic<uint8_t> g_probeInstallYawGate;
    extern std::atomic<bool> g_isPilotState;
}

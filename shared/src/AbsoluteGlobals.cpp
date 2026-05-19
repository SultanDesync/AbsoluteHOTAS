#include "AbsoluteGlobals.h"

namespace AbsoluteGlobals {
    std::atomic<uint8_t> g_magicArmed = 0;
    std::atomic<uint8_t> g_isArmed = 0;
    std::atomic<uint8_t> g_isStandingDown = 0;
    std::atomic<uintptr_t> g_lockedRDI = 0;
    std::atomic<uintptr_t> g_lockedRCX = 0;
    std::atomic<uintptr_t> g_capturedRDI = 0;
    std::atomic<uintptr_t> g_capturedRCX = 0;
    std::atomic<float> g_axisPitch = 0.0f;
    std::atomic<float> g_axisYaw = 0.0f;
    std::atomic<uint8_t> g_probeHotWriteActive = 0;
    std::atomic<uint32_t> g_probeHotWriteBits = 0;
    std::atomic<uint8_t> g_probeInstallRollGate = 0;
    std::atomic<uint8_t> g_probeInstallPitchGate = 0;
    std::atomic<uint8_t> g_probeInstallYawGate = 0;
    std::atomic<bool> g_isPilotState = false;
}

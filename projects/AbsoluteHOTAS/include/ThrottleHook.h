#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>

// Phase 1: AOB Signature Scan + Mid-Function Trampoline Hook
// Captures candidate ThrottleInterface base pointers (RDI register) from the engine's
// "vmovss [rdi+68], xmm0" instruction at runtime.
// Multiple candidates are stored; the controller validates which is the throttle.
class ThrottleHook {
public:
    static bool Install();
    static bool IsInstalled();
    static void Uninstall();
    static uintptr_t GetBasePtr();
    static uintptr_t GetSourceBasePtr();
    static uintptr_t GetWriterClusterBasePtr();
    static bool IsActive();

    // Get all captured candidate pointers
    static constexpr int MAX_CANDIDATES = 2048;
    static uintptr_t GetCandidate(int index);
    static int GetCandidateCount();
    static void ClearCandidates();

    // Toggle the discovery hooks (Passive Stability)
    static void SetCaptureEnabled(bool enabled);
    static bool IsCaptureEnabled();

    // Toggle persistent "Surgical Silence" (NOPing game writes)
    static void SetSilenceEnabled(bool enabled);
    static bool IsSilenceEnabled();

    // Engine-timed rotational gates for the validated +58/+60/+64 writer block.
    static void SetRotationalOverride(float lateral, float yaw, float pitch, bool enabled, bool lateralEnabled = true, float vertical = 0.0f, bool verticalEnabled = false, bool yawEnabled = true, bool pitchEnabled = true);
    static void SetManualLaneOverride(uintptr_t offset, float value, bool enabled);
    static int GetManualGateCount();
    static uintptr_t GetManualGateAddress(int index);
    static uintptr_t GetManualGateOffset(int index);
    static void SetManualGateOverride(int index, float value, bool enabled);

    // Drive the aiming reticle via source-object mouse accumulator writes.
    // yaw/pitch are pre-scaled values in [-200.0, +200.0] (mouse accumulator range).
    // When enabled, the values are written to g_capturedSourceR13+0x4C (yaw)
    // and g_capturedSourceR13+0x50 (pitch) each poll iteration, feeding the
    // game's mouse-driven aiming pipeline which drives the reticle independently
    // of controller mode state.
    static void SetSourceObjectAim(float yaw, float pitch, bool enabled);
    static bool IsSourcePtrValid();

    // Engage or disengage direct-memory reverse flight.
    // When enabled: locks cluster+0x5C to -1.0 via the vertical strafe trampoline gate,
    // writes -1.0 to source+0x3C (upstream decel intent).
    // Caller is responsible for writing -1.0 to cluster+0x68 (throttle target).
    // When disabled: releases the vertical strafe gate and clears source+0x3C.
    static void SetReverseOverride(bool enabled);

private:
    static std::atomic<uintptr_t> s_basePtr;
    static std::atomic<int64_t>   s_lastHookTimestamp;
    static uint8_t*               s_trampoline;
    static uintptr_t              s_hookAddress;
};

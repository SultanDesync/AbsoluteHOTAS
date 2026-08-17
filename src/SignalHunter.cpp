#include "PCH.h"
#include "SignalHunter.h"
#include "ThrottleHook.h"
#include "ShipOutput.h"
#include "RuntimePaths.h"
#include "NativeShipControl.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================================
// Logging
// ============================================================================
static void SHLog(const char* msg) {
    RuntimePaths::Log("[SignalHunter]", msg);
}

// ============================================================================
// SEH-guarded memory helpers
// ============================================================================
#pragma warning(push)
#pragma warning(disable: 4733)

static void SafeInjectThrottle(uintptr_t baseAddr, float throttle, bool forceEffective = false) {
    if (!baseAddr) return;
    __try {
        *(float*)(baseAddr + 0x68) = throttle;
        
        float gameEffective = *(float*)(baseAddr + 0x6C);
        
        // +0x6C write logic:
        // forceEffective: always write both offsets (accumulator mode — we own everything).
        // Otherwise: only write +0x6C downward (decelerating / exiting boost).
        // When accelerating, the game's native rotation throttle assist may be
        // managing +0x6C independently, so we don't override upward.
        if (forceEffective || throttle <= gameEffective) {
            *(float*)(baseAddr + 0x6C) = throttle;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool IsThrottlePlausible(uintptr_t basePtr) {
    __try {
        float val = *(volatile float*)(basePtr + 0x68);
        return std::isfinite(val);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float SafeReadThrottle(uintptr_t basePtr) {
    __try { return *(volatile float*)(basePtr + 0x68); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -999.0f; }
}

// Read ship velocity from flight control cluster (+0x70).
static float ReadClusterVelocity(uintptr_t clusterBase) {
    if (!clusterBase) return -1.0f;
    __try { return *(volatile float*)(clusterBase + 0x70); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
}

#pragma warning(pop)

// ============================================================================
// State
// ============================================================================
static constexpr int kMaxCandidates   = 2048;

static int       s_candidateAges[kMaxCandidates]     = {};
static uintptr_t s_lastFrameCandidates[kMaxCandidates] = {};

static bool      s_discoveryLocked      = false;
static bool      s_discoveryArmed       = false;
static int       s_activeCandidateIndex = -1;
static uintptr_t s_activeThrottlePtr   = 0;
static int       s_plausibilityFailCount = 0;
static bool      s_reacquireWatchdogEnabled = false;
static float     s_lastInjectedThrottle = -999.0f;

// Burst injection state (absolute throttle mode)
static int       s_throttleBurstFrames  = 0;
static float     s_throttleBurstValue   = 0.0f;

// Accumulator throttle state (dual-stick mode)
static float     s_accumulatorThrottle  = 0.0f;
static int       s_accumBurstFrames     = 0;
static float     s_lastAccumBurstValue  = -999.0f;
static bool      s_prevBoostHeld        = false;

// ============================================================================
// Public API
// ============================================================================
namespace SignalHunter {

void Disarm() {
    s_discoveryArmed          = false;
    s_discoveryLocked         = false;
    s_activeCandidateIndex    = -1;
    s_activeThrottlePtr       = 0;
    s_plausibilityFailCount   = 0;
    s_reacquireWatchdogEnabled = false;
    s_lastInjectedThrottle    = -999.0f;
    s_throttleBurstFrames     = 0;
    s_throttleBurstValue      = 0.0f;
    s_accumulatorThrottle     = 0.0f;
    s_accumBurstFrames        = 0;
    s_lastAccumBurstValue     = -999.0f;
    s_prevBoostHeld           = false;
    NativeShipControl::UpdateCluster(0);
    NativeShipControl::SetSplitFlightAxes(0.0F, 0.0F, false);
    ShipOutputSystem::ReleaseAllShipButtonOutputs();
    ThrottleHook::SetReverseOverride(false);
    ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
    ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, false);
    ThrottleHook::SetSilenceEnabled(false);
    ThrottleHook::SetSilence6CEnabled(false);
    ThrottleHook::SetCaptureEnabled(false);
    ThrottleHook::ClearCandidates();
}

void SuspendInjection() {
    // InjectionOnly pilot gate: stop flight-axis injection while leaving discrete
    // bindings untouched (unlike Disarm, which also releases ship actions/raw
    // outputs and tears down capture). Capture stays live for clean re-pilot.
    s_lastInjectedThrottle = -999.0f;
    s_throttleBurstFrames  = 0;
    s_throttleBurstValue   = 0.0f;
    s_accumulatorThrottle  = 0.0f;
    s_accumBurstFrames     = 0;
    s_lastAccumBurstValue  = -999.0f;
    s_prevBoostHeld        = false;
    NativeShipControl::SetSplitFlightAxes(0.0F, 0.0F, false);
    ThrottleHook::SetReverseOverride(false);
    ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
    ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, false);
    ThrottleHook::SetSilenceEnabled(false);
    ThrottleHook::SetSilence6CEnabled(false);
}

void ArmForReacquire() {
    s_discoveryArmed        = true;
    s_discoveryLocked       = false;
    s_activeCandidateIndex  = -1;
    s_activeThrottlePtr     = 0;
    s_plausibilityFailCount = 0;
    s_throttleBurstFrames   = 0;
    NativeShipControl::UpdateCluster(0);
    NativeShipControl::SetSplitFlightAxes(0.0F, 0.0F, false);
    ThrottleHook::SetReverseOverride(false);
    ThrottleHook::SetRotationalOverride(0.0f, 0.0f, 0.0f, false);
    ThrottleHook::SetSilenceEnabled(false);
    ThrottleHook::SetSilence6CEnabled(false);
    ThrottleHook::ClearCandidates();
    ThrottleHook::SetCaptureEnabled(true);
}



void Tick(int candCount, uint64_t iter) {
    const auto& cfg = ThrottleController::GetConfig();

    // The validated rotational writer gives us the flight-control cluster in
    // RAX directly. Prefer it over the historical broad RDI scan, but only
    // accept it after NativeShipControl proves the selected flight-handler
    // layout and exact method table. This is also evaluated after a heuristic
    // lock so a newly observed ship cluster can replace a stale one.
    const uintptr_t writerCluster = ThrottleHook::GetWriterClusterPtr();
    if (writerCluster && writerCluster != s_activeThrottlePtr) {
        NativeShipControl::UpdateCluster(writerCluster);
        if (NativeShipControl::ShipHandlerReady()) {
            char buf[160];
            sprintf_s(buf,
                "Control cluster located via validated rotational writer at 0x%llX",
                (unsigned long long)writerCluster);
            SHLog(buf);
            s_activeCandidateIndex     = -1;
            s_activeThrottlePtr        = writerCluster;
            s_discoveryLocked          = true;
            s_discoveryArmed           = false;
            s_reacquireWatchdogEnabled = true;
            s_plausibilityFailCount    = 0;
            ThrottleHook::SetCaptureEnabled(false);
            ThrottleHook::SetSilenceEnabled(false);
            ThrottleHook::SetSilence6CEnabled(false);
        }
    }

    // SIGNAL HUNTER: Magic Number Pulse detection
    if (s_discoveryArmed && !s_discoveryLocked && candCount > 0) {
        // Periodic candidate-buffer flush for noise reduction.
        if (iter % (cfg.pollRateHz * 5) == 0) {
            ThrottleHook::ClearCandidates();
        }

        for (int i = 0; i < candCount && i < kMaxCandidates; i++) {
            uintptr_t cand = ThrottleHook::GetCandidate(i);
            if (!cand) continue;

            float memVal = SafeReadThrottle(cand);

            if (cand == s_lastFrameCandidates[i]) {
                s_candidateAges[i]++;
            } else {
                s_candidateAges[i]        = 0;
                s_lastFrameCandidates[i]  = cand;
            }

            // Magic-number lock: game signals with 0.0314f
            if (std::abs(memVal - 0.0314f) < 0.0001f) {
                char buf[128];
                sprintf_s(buf, "Control cluster located via magic 0.0314 (candidate #%d) at 0x%llX",
                    i, (unsigned long long)cand);
                SHLog(buf);
                s_activeCandidateIndex     = i;
                s_activeThrottlePtr        = cand;
                s_discoveryLocked          = true;
                s_reacquireWatchdogEnabled = true;
                s_plausibilityFailCount    = 0;
                ThrottleHook::SetCaptureEnabled(false);
                ThrottleHook::SetSilenceEnabled(false);
                ThrottleHook::SetSilence6CEnabled(false);
                break;
            }

            // Manual correlation fallback: stable signal in plausible float range
            if (s_candidateAges[i] >= 15 && memVal >= -2.1f && memVal <= 2.1f) {
                char buf[128];
                sprintf_s(buf, "Control cluster located via stable-signal fallback (candidate #%d) at 0x%llX",
                    i, (unsigned long long)cand);
                SHLog(buf);
                s_activeCandidateIndex     = i;
                s_activeThrottlePtr        = cand;
                s_discoveryLocked          = true;
                s_reacquireWatchdogEnabled = true;
                s_discoveryArmed           = false;
                s_plausibilityFailCount    = 0;
                ThrottleHook::SetCaptureEnabled(false);
                ThrottleHook::SetSilenceEnabled(false);
                ThrottleHook::SetSilence6CEnabled(false);
                break;
            }
        }
    }

    // Reacquire watchdog
    if (s_reacquireWatchdogEnabled) {
        if (s_discoveryLocked && !s_activeThrottlePtr)
            ArmForReacquire();
        else if (!s_discoveryLocked && !s_discoveryArmed)
            ArmForReacquire();
    }

    NativeShipControl::UpdateCluster(s_activeThrottlePtr);
}

void Inject(float throttle, float pitch, float yaw, float roll,
            float strafeX, float strafeY, float dt,
            bool reverseHeld, bool suppressPitchYaw,
            bool strafeLatActive, bool strafeVertActive,
            bool cruiseOverride, float cruiseTarget)
{
    if (!s_activeThrottlePtr) return;

    const auto& cfg = ThrottleController::GetConfig();

    if (!IsThrottlePlausible(s_activeThrottlePtr)) {
        s_plausibilityFailCount++;
        if (s_plausibilityFailCount > 60) {
            SHLog("Persistent signal loss. Pointer invalidated.");
            ArmForReacquire();
            s_lastInjectedThrottle = -999.0f;
            ShipOutputSystem::ReleaseAllShipButtonOutputs();
        }
        return;
    }

    s_plausibilityFailCount = 0;

    constexpr float kThrottleDeltaAuthority = 0.015f;
    int throttleBurstFrameCount = (cfg.throttleBurstMs <= 0)
        ? 1 : std::max(1, (cfg.pollRateHz * cfg.throttleBurstMs) / 1000);

    // Rotational override.
    // Strafe activation is decided by the controller from the pre-deadzone axis
    // magnitude (so the deadzone is not applied twice) and passed in here; the
    // value written (strafeX/strafeY) is still the single-deadzoned value.
    bool rollOverrideActive       = cfg.rollEnabled && (std::abs(roll) > 0.05f);
    bool strafeLatOverrideActive  = strafeLatActive;
    bool strafeVertOverrideActive = strafeVertActive;
    float lateral = strafeLatOverrideActive ? strafeX : roll;

    // HOSAM mode: release yaw/pitch gates so the game's native mouse pipeline
    // owns steering.  All other rotational lanes (roll, strafe) stay active.
    bool yawGateEnabled   = !suppressPitchYaw;
    bool pitchGateEnabled = !suppressPitchYaw;

    ThrottleHook::SetRotationalOverride(
        lateral, yaw, pitch, true,
        strafeLatOverrideActive || rollOverrideActive,
        strafeY, strafeVertOverrideActive,
        yawGateEnabled, pitchGateEnabled);

    NativeShipControl::SetSplitFlightAxes(
        cfg.rollEnabled ? roll : 0.0F, strafeX,
        rollOverrideActive || strafeLatOverrideActive);

    // --- BOOST GUARD & CANCEL LOGIC ---
    const bool boostHeld = cfg.bHoldForBoost && ShipOutputSystem::IsBoostRequested();
    
    if (!boostHeld && s_prevBoostHeld) {
        if (cfg.bAccumulatorThrottle) {
            s_accumulatorThrottle = 1.0f;
            s_lastAccumBurstValue = -999.0f;
        }
        s_lastInjectedThrottle = -999.0f; // Force a throttle burst on release to cancel boost
    }

    s_prevBoostHeld = boostHeld;

    if (cfg.bAccumulatorThrottle) {
        float stickDeflection = throttle;

        if (!boostHeld) {
            // Accumulator value is the single source of truth.
            // Silence +0x68 writes so the accumulator always owns the input target.
            // +0x6C silence is managed per-frame by the turn assist logic below.
            ThrottleHook::SetSilenceEnabled(true);

            const float kDeadzone = std::max(cfg.fThrottleDeadzone, 0.05f);

            if (cruiseOverride) {
                ThrottleHook::SetReverseOverride(false);
                s_accumulatorThrottle = std::clamp(cruiseTarget, 0.0f, 1.0f);
            } else if (stickDeflection > kDeadzone) {
                ThrottleHook::SetReverseOverride(false);
                s_accumulatorThrottle += stickDeflection * cfg.fAccumulatorRate * dt;
                s_accumulatorThrottle = std::clamp(s_accumulatorThrottle, 0.0f, 1.0f);
            } else if (stickDeflection < -kDeadzone) {
                if (s_accumulatorThrottle > 0.0f) {
                    s_accumulatorThrottle += stickDeflection * cfg.fAccumulatorRate * dt;
                    if (s_accumulatorThrottle < 0.0f) s_accumulatorThrottle = 0.0f;
                } else {
                    s_accumulatorThrottle = 0.0f;
                    float vel   = ReadClusterVelocity(s_activeThrottlePtr);
                    bool gateOpen = (vel < 0.0f) || (vel >= 0.0f && vel <= cfg.fReverseGateVelocity);

                    if (gateOpen) {
                        ThrottleHook::SetReverseOverride(true);
                        SafeInjectThrottle(s_activeThrottlePtr, -1.0f, true);
                        s_lastInjectedThrottle = -1.0f;
                    } else {
                        ThrottleHook::SetReverseOverride(false);
                        SafeInjectThrottle(s_activeThrottlePtr, 0.0f, true);
                    }
                }
            } else {
                // Stick at neutral
                ThrottleHook::SetReverseOverride(false);
                if (cfg.fAccumulatorDecay > 0.0f && s_accumulatorThrottle > 0.0f) {
                    s_accumulatorThrottle -= cfg.fAccumulatorDecay * dt;
                    if (s_accumulatorThrottle < 0.0f) s_accumulatorThrottle = 0.0f;
                }
            }

            // ---- Turn Assist: Native game assist via selective +0x6C silence ----
            // When active, we lift +0x6C silence so the game's built-in rotation
            // throttle assist writes to effective throttle. Our accumulator value
            // in +0x68 stays untouched. When assist deactivates, we re-silence +0x6C
            // and trigger a burst to push our value back to both offsets.
            static bool s_prevTurnAssistActive = false;
            bool turnAssistActive = ThrottleController::IsTurnAssistActive();
            ThrottleHook::SetSilence6CEnabled(!turnAssistActive);

            if (s_prevTurnAssistActive && !turnAssistActive) {
                // Transition: assist OFF → force burst to resume our throttle
                s_accumBurstFrames = throttleBurstFrameCount;
                s_lastAccumBurstValue = -999.0f; // force delta detection
            }
            s_prevTurnAssistActive = turnAssistActive;

            // Delta burst injection
            float accTarget = std::max(s_accumulatorThrottle, 0.0f);

            bool accMoved   = (std::abs(accTarget - s_lastAccumBurstValue) > kThrottleDeltaAuthority);
            if (accMoved) {
                s_accumBurstFrames    = throttleBurstFrameCount;
                s_lastAccumBurstValue = accTarget;
            }
            if (s_accumBurstFrames > 0) {
                s_lastInjectedThrottle = accTarget;
                SafeInjectThrottle(s_activeThrottlePtr, accTarget, true);
                s_accumBurstFrames--;
            }
        } else {
            // Boost held: release all silence, game owns throttle
            ThrottleHook::SetSilenceEnabled(false);
            ThrottleHook::SetSilence6CEnabled(false);
            s_accumBurstFrames = 0;
        }

    } else if (reverseHeld) {
        // Direct-memory reverse — release silence so the game can process reverse
        ThrottleHook::SetSilenceEnabled(false);
        ThrottleHook::SetSilence6CEnabled(false);
        float vel = ReadClusterVelocity(s_activeThrottlePtr);
        bool gateOpen = (vel < 0.0f) || (vel >= 0.0f && vel <= cfg.fReverseGateVelocity);

        if (gateOpen) {
            ThrottleHook::SetReverseOverride(true);
            SafeInjectThrottle(s_activeThrottlePtr, -1.0f);
            s_lastInjectedThrottle = -1.0f;
        } else {
            ThrottleHook::SetReverseOverride(false);
            SafeInjectThrottle(s_activeThrottlePtr, 0.0f);
            s_lastInjectedThrottle = 0.0f;
        }

        s_throttleBurstFrames  = 0;
        s_lastInjectedThrottle = -999.0f;

    } else if (boostHeld) {
        // Suppress throttle writes during boost — release silence so game owns throttle
        ThrottleHook::SetSilenceEnabled(false);
        ThrottleHook::SetSilence6CEnabled(false);
        ThrottleHook::SetReverseOverride(false);
        s_throttleBurstFrames = 0;
    } else {
        // Standard absolute throttle injection
        // The hardware lever is the single source of truth. The game's
        // "rotation throttle assist" (which auto-centers throttle for optimal
        // turn rates on gamepad/keyboard) is not needed with a physical lever.
        // We silence the game's writes to +0x68/+0x6C so the lever always wins,
        // and use delta-burst injection for efficiency.
        ThrottleHook::SetReverseOverride(false);

        // Throttle-release rule: when no throttle axis is bound the plugin is not
        // managing throttle, so hand the channel back to the game's vanilla input
        // (keyboard accelerate/decelerate) instead of silencing it. Silencing with
        // no source mutes those keys AND pins the lever at 0; lifting the silence is
        // what actually releases the channel. Unbinding the axis alone used to leave
        // it muted, which is what users hit when they tried to fall back to keyboard.
        const bool throttleBound = cruiseOverride || (cfg.throttleAxis.IsValid() && cfg.throttleAxis.value > 0);
        ThrottleHook::SetSilenceEnabled(throttleBound);
        ThrottleHook::SetSilence6CEnabled(throttleBound);

        if (!throttleBound) {
            s_throttleBurstFrames  = 0;
            s_lastInjectedThrottle = -999.0f;
        } else {
            bool firstCommand  = (s_lastInjectedThrottle == -999.0f);
            bool throttleMoved = firstCommand ||
                (std::abs(throttle - s_lastInjectedThrottle) > kThrottleDeltaAuthority);

            if (throttleMoved) {
                s_throttleBurstFrames = throttleBurstFrameCount;
                s_throttleBurstValue  = throttle;
                s_lastInjectedThrottle = throttle;
            }

            if (s_throttleBurstFrames > 0) {
                SafeInjectThrottle(s_activeThrottlePtr, s_throttleBurstValue, true);
                s_throttleBurstFrames--;
            }
        }
    }
}

float GetCurrentThrottleTarget() {
    return std::clamp(s_accumulatorThrottle, 0.0f, 1.0f);
}

} // namespace SignalHunter

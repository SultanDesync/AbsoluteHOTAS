#include "PCH.h"
#include "AimController.h"
#include "ThrottleHook.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"
#include <cstdio>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

// ============================================================================
// Internal helpers
// ============================================================================

static float SafeReadFloat(uintptr_t addr) {
    __try { return *(volatile float*)(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -999.0f; }
}

static void SafeWriteFloat(uintptr_t addr, float value) {
    __try { *(volatile float*)addr = value; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Bipolar normalisation for an analog axis, with optional calibration override.
static float NormBipolar(const ThrottleController::Config& cfg,
                         const BindingRef& ref, float sens, bool invert)
{
    if (!ref.IsValid() || ref.value <= 0) return 0.0f;
    const DIJOYSTATE2* st = DeviceManager::GetCachedState(ref.deviceIndex);
    if (!st) return 0.0f;

    float raw  = static_cast<float>(DeviceManager::GetAxisFromState(st, ref.value));
    float aMin = 0.0f, aMax = 65535.0f;

    if (ref.deviceIndex >= 0) {
        int calibKey = (ref.deviceIndex << 8) | ref.value;
        auto it = cfg.axisCalibration.find(calibKey);
        if (it != cfg.axisCalibration.end()) {
            aMin = static_cast<float>(it->second.first);
            aMax = static_cast<float>(it->second.second);
        }
    }

    float center    = (aMin + aMax) / 2.0f;
    float halfRange = (aMax - aMin) / 2.0f;
    if (halfRange <= 0.0f) return 0.0f;
    float val = (raw - center) / halfRange;
    val *= sens;
    val  = invert ? -val : val;
    return std::clamp(val, -1.0f, 1.0f);
}

// ============================================================================
// Public API
// ============================================================================
namespace AimController {

void Update(const ThrottleController::Config& cfg,
            float yaw, float pitch,
            bool hasSeparateAimInput,
            bool hasSeparateAimAxes,
            bool hasDigitalAimButtons,
            bool sourceObjectAimAllowed,
            float dt)
{
    // ---- HOSAM Alignment Assist ----
    // Observes the game's native mouse accumulator. When the mouse has been
    // idle for the configured duration and within the alignment radius, apply
    // exponential decay toward (0,0).
    if (!ThrottleHook::ExternalMouseSteeringOwnerActive() &&
        cfg.bHOSAMMode && cfg.bAlignmentAssist && ThrottleHook::IsSourcePtrValid()) {
        static float s_prevAccumYaw   = 0.0f;
        static float s_prevAccumPitch = 0.0f;
        static int   s_alignIdleFrames = 0;

        uintptr_t src   = ThrottleHook::GetSourceBasePtr();
        float curYaw    = SafeReadFloat(src + 0x4C);
        float curPitch  = SafeReadFloat(src + 0x50);

        constexpr float kMoveEpsilon = 0.5f;
        bool mouseMoved = (std::abs(curYaw   - s_prevAccumYaw)   > kMoveEpsilon)
                       || (std::abs(curPitch - s_prevAccumPitch) > kMoveEpsilon);
        s_prevAccumYaw   = curYaw;
        s_prevAccumPitch = curPitch;

        if (mouseMoved) s_alignIdleFrames = 0;
        else            s_alignIdleFrames++;

        int idleFrameThreshold = std::max(1, (cfg.pollRateHz * cfg.iAlignmentIdleMs) / 1000);
        if (s_alignIdleFrames >= idleFrameThreshold) {
            float dist = std::sqrt(curYaw * curYaw + curPitch * curPitch);
            if (dist <= cfg.fAlignmentRadius && dist > 0.1f) {
                float decayFactor = std::exp(-cfg.fAlignmentDecayRate * dt);
                float newYaw   = curYaw   * decayFactor;
                float newPitch = curPitch * decayFactor;
                if (std::abs(newYaw)   < 0.1f) newYaw   = 0.0f;
                if (std::abs(newPitch) < 0.1f) newPitch = 0.0f;
                SafeWriteFloat(src + 0x4C, newYaw);
                SafeWriteFloat(src + 0x50, newPitch);
                s_prevAccumYaw   = newYaw;
                s_prevAccumPitch = newPitch;
            }
        }
    }

    // ---- Source-object reticle injection ----
    if (!sourceObjectAimAllowed || !cfg.bSourceObjectAim || cfg.bHOSAMMode) {
        // HOSAM: mouse owns steering — disable the aim system entirely so the
        // chase blender doesn't fight the native mouse accumulator. The same
        // release applies to the automatic all-unbound vanilla-mouse fallback.
        ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, false);
        return;
    }

    if (!ThrottleHook::IsSourcePtrValid()) {
        ThrottleHook::SetSourceObjectAim(0.0f, 0.0f, true);
        return;
    }

    float aimYaw = 0.0f, aimPitch = 0.0f;

    if (hasSeparateAimInput && hasSeparateAimAxes) {
        // Separate analog aim axes
        float targetYaw   = NormBipolar(cfg, cfg.aimYawAxis,   cfg.fAimYawSensitivity,   cfg.bInvertAimYaw);
        float targetPitch = NormBipolar(cfg, cfg.aimPitchAxis, cfg.fAimPitchSensitivity, cfg.bInvertAimPitch);
        aimYaw   = targetYaw;
        aimPitch = targetPitch;

        // EMA smoothing for low-resolution sensors
        if (cfg.fAimSmoothing > 0.001f) {
            static float s_smoothYaw   = 0.0f;
            static float s_smoothPitch = 0.0f;
            float smoothPow   = std::pow(cfg.fAimSmoothing, dt * 60.0f);
            s_smoothYaw   = s_smoothYaw   * smoothPow + aimYaw   * (1.0f - smoothPow);
            s_smoothPitch = s_smoothPitch * smoothPow + aimPitch * (1.0f - smoothPow);
            aimYaw   = s_smoothYaw;
            aimPitch = s_smoothPitch;
        }
    } else if (!hasSeparateAimInput && cfg.bMirrorFlightToAim) {
        // Aim-driven steering: flight stick mirrors to reticle
        aimYaw   = yaw   * cfg.fAimSensitivity;
        aimPitch = pitch * cfg.fAimSensitivity;
    }
    // else: digital-only or no aim input — accumulators below will supply position

    // Digital aim: accumulator-style virtual cursor
    {
        static float s_digitalAimYaw   = 0.0f;
        static float s_digitalAimPitch = 0.0f;

        if (!hasSeparateAimInput) {
            s_digitalAimYaw   = 0.0f;
            s_digitalAimPitch = 0.0f;
        } else if (DeviceManager::IsButtonPressed(cfg.digitalAimCenterButton)) {
            s_digitalAimYaw   = 0.0f;
            s_digitalAimPitch = 0.0f;
        } else {
            float dY = 0.0f, dP = 0.0f;
            if (DeviceManager::IsButtonPressed(cfg.digitalAimLeftButton))  dY -= 1.0f;
            if (DeviceManager::IsButtonPressed(cfg.digitalAimRightButton)) dY += 1.0f;
            if (DeviceManager::IsButtonPressed(cfg.digitalAimUpButton))    dP -= 1.0f;
            if (DeviceManager::IsButtonPressed(cfg.digitalAimDownButton))  dP += 1.0f;

            float dMag = std::sqrt(dY * dY + dP * dP);
            if (dMag > 1.0f) { dY /= dMag; dP /= dMag; }

            s_digitalAimYaw   += dY * cfg.fDigitalAimValue * dt;
            s_digitalAimPitch += dP * cfg.fDigitalAimValue * dt;
            s_digitalAimYaw   = std::clamp(s_digitalAimYaw,   -1.0f, 1.0f);
            s_digitalAimPitch = std::clamp(s_digitalAimPitch, -1.0f, 1.0f);
        }

        if (hasSeparateAimInput && hasDigitalAimButtons) {
            aimYaw        = s_digitalAimYaw;
            aimPitch      = s_digitalAimPitch;

        }
    }

    // Circular normalisation: clamp magnitude to 1.0 before scaling
    {
        float mag = std::sqrt(aimYaw * aimYaw + aimPitch * aimPitch);
        if (mag > 1.0f) { aimYaw /= mag; aimPitch /= mag; }
    }

    // Scale to mouse accumulator range [-200, +200]
    aimYaw   *= 200.0f;
    aimPitch *= 200.0f;

    ThrottleHook::SetSourceObjectAim(aimYaw, aimPitch, true);
}

} // namespace AimController

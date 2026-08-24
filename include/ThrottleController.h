#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <unordered_map>
#include "BindingRef.h"
#include "MenuNavigationCatalog.h"
#include "HeadTracking.h"

// DirectInput polling, normalization, and memory injection.
// Runs on a dedicated background thread at ~120Hz.
class ThrottleController {
public:
    // Pilot-state gate mode.
    //   Off           = legacy behavior (master switch controls everything).
    //   InjectionOnly = while not piloting, park flight-axis injection; keep discrete bindings.
    //   Full          = while not piloting, park all plugin-owned control output.
    enum class GateMode { Off, InjectionOnly, Full };

    // Pilot signal source: Manual (legacy toggle key) or Auto (selected-handler
    // output freshness with menu/loading suspension).
    enum class PilotSignal { Manual, Auto };

    // Configuration loaded from AbsoluteHOTAS.ini.
    struct Config {
        // [General]
        bool    enabled = true;

        // Per-binding axis references (DeviceName@AxisUsageId or just AxisUsageId)
        BindingRef throttleAxis;
        BindingRef pitchAxis;
        BindingRef yawAxis;
        BindingRef rollAxis;
        BindingRef strafeLatAxis;
        BindingRef strafeVertAxis;
        BindingRef reverseAxis;

        float   fPitchSensitivity = 1.0f;
        float   fYawSensitivity = 1.0f;
        float   fRollSensitivity = 1.0f;
        float   fStrafeSensitivity = 1.0f;
        float   fReverseSensitivity = 1.0f;
        float   fThrottleSensitivity = 1.0f;

        float   fThrottleSaturation = 1.0f;
        float   fPitchSaturation = 1.0f;
        float   fYawSaturation = 1.0f;
        float   fRollSaturation = 1.0f;
        float   fStrafeSaturation = 1.0f;
        float   fStrafeVertSaturation = 1.0f;
        float   fReverseSaturation = 1.0f;

        bool    bInvertPitch = true;
        bool    bInvertThrottle = false;
        bool    bInvertYaw = false;
        bool    bInvertRoll = false;
        bool    bInvertStrafeLat = false;
        bool    bInvertStrafeVert = false;
        bool    bInvertReverse = false;

        // Per-binding button references (DeviceName@ButtonId or just ButtonId)
        BindingRef activateButton;
        BindingRef stopButton;
        BindingRef toggleWizardButton;
        BindingRef cruiseHoldButton;
        BindingRef fullStopButton;
        BindingRef cruiseHalfButton;
        BindingRef cruiseMaxButton;
        bool    alwaysOn = true;          // Auto-arm discovery when the standalone controller starts
        int     toggleActiveKey = 0x91;   // Keyboard VK code that toggles the master on/off gate (0=disabled, default ScrollLock)

        // [Normalization]
        long    detentCenter = 32768;   // Raw axis value at physical detent center
        long    detentDeadzone = 500;   // Deadzone around detent (raw units)
        bool    reverseEnabled = false; // Axis reverse is disabled for the beta; use keyboard S.
        bool    unipolarMode = true;    // If true, maps whole axis (min-max) to 0.0-1.0 range (linear)
        bool    bUnipolarReverse = false; // Split unipolar axis: above reverseZone = forward, reverseZone±dz = dead stop, below = reverse
        long    reverseZoneCenter = 3000;   // Raw axis value at zero-thrust boundary (separate from detentCenter)
        long    reverseZoneDeadzone = 3000; // Deadzone around zero-thrust boundary (raw units)
        bool    bBoostZone = false;         // Enable boost zone at top of throttle axis
        long    boostZoneCenter = 62000;    // Raw axis value where boost zone starts
        long    boostZoneDeadzone = 2000;   // Deadzone: area below boost = flat 100%, above = boost trigger
        float   idlePlateau = 0.05f;    // Software deadzone at the bottom (0.0-1.0)
        float   reverseDeadzone = 0.05f;
        float   reverseActivationThreshold = 0.05f;

        // Per-axis deadzones (applied after normalization, before injection)
        float   fThrottleDeadzone = 0.0f;
        float   fPitchDeadzone = 0.0f;
        float   fYawDeadzone = 0.0f;
        float   fRollDeadzone = 0.0f;
        float   fStrafeDeadzone = 0.05f;      // 5% default to prevent accidental HOSAS lateral actuation
        float   fStrafeVertDeadzone = 0.05f;

        // [Injection]
        int     pollRateHz = 120;
        int     throttleBurstMs = 250;    // Throttle authority window after movement; 0 = one frame
        bool    rollEnabled = true;       // Restored independently at the selected-handler output.
        bool    reverseAxisEnabled = true;
        // Master switch for flight-axis and source-object aim injection.
        // Discrete ship actions, raw custom bindings, and macros are unaffected. Default true;
        // a "parked" profile sets it false to disable flight injection on foot while
        // keeping its own button/macro mappings. See profile-switching.md.
        bool    bEnableInjection = true;

        bool    bHoldForBoost = true;   // Pause throttle injection while boost is held; cancel on release

        // Patch 5.0 native control layer. Named ship operations fail closed at
        // their exact Starfield gates and never need a ControlMap/SendInput path.
        bool    bNativeShipControls = true;
        // Retained only as a migration shape for legacy configuration. The
        // standalone Absolute Head Tracking module owns camera behavior.
        HeadTracking::Settings headTracking;
        // [ShipButtons]
        bool    shipButtonsEnabled = true;

        // [MenuControls] — optional reuse of already-bound flight controls while
        // Starfield reports a suspended menu/loading context.
        std::array<BindingRef, kMenuNavigationCatalog.size()>
            menuNavigationBindings{};
        bool    bUsePitchAxisForMenu = false;
        bool    bUseYawAxisForMenu = false;
        bool    bUsePrimaryWeaponForMenuSelect = false;
        bool    bInvertMenuVertical = false;
        bool    bInvertMenuHorizontal = false;
        float   fMenuAxisEngageThreshold = 0.55f;
        float   fMenuAxisReleaseThreshold = 0.35f;

        // [Gate] pilot-state gate
        GateMode    pilotGateMode = GateMode::InjectionOnly;
        PilotSignal pilotSignal = PilotSignal::Auto;
        int         pilotGateManualToggleKey = 0;        // VK to toggle the manual signal (0 = disabled)
        int         pilotLatchMilliseconds = 5000;

        // [Aim] - Source-object reticle injection
        bool    bSourceObjectAim  = false;  // Enable HOTAS-driven aiming reticle
        float   fAimSensitivity   = 1.0f;   // Scale applied to pitch/yaw before writing source obj (mirror mode)

        // Separated aiming axes — if bound, these drive the reticle independently
        // from the flight stick. If unbound, behavior depends on bMirrorFlightToAim.
        BindingRef aimYawAxis;              // HID usage for aiming yaw (e.g., Rx on throttle)
        BindingRef aimPitchAxis;            // HID usage for aiming pitch (e.g., Ry on throttle)
        float   fAimYawSensitivity   = 1.0f;
        float   fAimPitchSensitivity = 1.0f;
        bool    bInvertAimYaw   = false;
        bool    bInvertAimPitch = false;
        float   fAimDeadzone    = 0.04f;  // Neutral gate for independent analog aim axes
        float   fAimSmoothing   = 0.0f;   // EMA smoothing for analog aim axes (0.0 = off, 1.0 = max)
        bool    bMirrorFlightToAim = true;  // If no aim axes bound, mirror flight stick to reticle

        // Digital aim buttons — 5-way directional override for aiming reticle
        BindingRef digitalAimLeftButton;
        BindingRef digitalAimRightButton;
        BindingRef digitalAimUpButton;
        BindingRef digitalAimDownButton;
        BindingRef digitalAimCenterButton;  // Snaps reticle to center (0,0)
        float   fDigitalAimValue = 1.0f;    // Deflection value when digital aim is active

        // Toggle button: switches between aim-driven steering and independent aim at runtime
        BindingRef toggleAimModeButton;

        // HOSAM (Hands On Stick And Mouse) — release pitch/yaw gates for native mouse steering
        bool    bHOSAMMode = false;
        bool    bAlignmentAssist = false;     // Gently decay mouse to center when idle
        float   fAlignmentRadius = 130.0f;    // Accumulator radius within which assist triggers (0-200)
        int     iAlignmentIdleMs = 50;         // Milliseconds mouse must be idle before decay starts
        float   fAlignmentDecayRate = 8.0f;    // Exponential decay speed (higher = faster centering)

        // [DigitalAxes] — Per-binding button references
        BindingRef digitalReverseButton;
        BindingRef digitalRollLeftButton;
        BindingRef digitalRollRightButton;
        BindingRef digitalStrafeLeftButton;
        BindingRef digitalStrafeRightButton;
        BindingRef digitalStrafeUpButton;
        BindingRef digitalStrafeDownButton;
        float   digitalRollValue = 1.0f;
        float   digitalStrafeValue = 1.0f;

        // Per-axis calibration overrides from [Calibration] section.
        // Key = (deviceIndex << 8) | usageId.  Value = {min, max}.
        // When present, these replace hardware-reported DIPROP_RANGE values.
        std::unordered_map<int, std::pair<long, long>> axisCalibration;

        // [DualStick] — Self-centering throttle accumulator mode
        bool    bAccumulatorThrottle = false;  // Treat throttle axis as rate input, not absolute position
        float   fAccumulatorRate = 1.0f;       // Throttle units/second at full deflection
        float   fAccumulatorDecay = 0.0f;      // Throttle units/second decay toward 0 at neutral (0 = hold)
        float   fReverseGateVelocity = 5.0f;   // Velocity (m/s) below which reverse throttle is allowed

        // Pilot turn assist: lifts +0x6C silence to let the game's native rotation
        // throttle assist operate. Throttle resumes automatically when deactivated.
        bool    bAccumulatorTurnAssist = false;  // Enable pilot turn assist
        int     iTurnAssistMode = 0;             // 0=Always, 1=Hold, 2=Toggle
        BindingRef turnAssistButton;             // Button binding for Hold/Toggle activation
    };

    static bool Initialize();
    static void Start();
    static void Stop();
    static Config& GetConfig();

    // Signal the control loop to reload config from INI on its next iteration.
    // Thread-safe — can be called from any thread (e.g., ImGui render thread).
    static void ReloadConfig();

    // Monotonic counter bumped after a reload or runtime profile swap has been fully
    // applied to s_config. Absolute Control refreshes clean snapshots while preserving a
    // dirty working copy. A changed value guarantees GetConfig() reflects the new
    // runtime state. Thread-safe.
    static uint32_t ConfigGeneration();

    // Runtime turn assist state (computed from INI master switch + button mode).
    // Called by SignalHunter to decide whether to apply the turn assist decay.
    static bool IsTurnAssistActive();

    // Ship action info for Absolute Control and runtime diagnostics.
    struct ShipActionInfo {
        const char* label;
        const char* iniKey;
        BindingRef  binding;
    };
    static std::vector<ShipActionInfo> GetShipActionBindings();

    // Returns the stored per-axis calibration map (read-only).
    static const std::unordered_map<int, std::pair<long, long>>& GetCalibrationData();

private:
    static Config s_config;
    static std::atomic<bool> s_running;
    static std::atomic<bool> s_configReloadRequested;
    static std::atomic<uint32_t> s_configGeneration;
    static std::thread s_thread;

    // Load the layered config into the active globals. slotFile, when non-null,
    // is appended as a fourth overlay (main -> user -> macros -> slot) so a switch
    // profile's overrides win. See docs/reference/profile-switching.md.
    static void LoadConfig(const std::string* slotFile = nullptr);

    static void ControlLoop();
    static float NormalizeAxis(long rawValue, long axisMin, long axisMax);

    // --- Profile switching ---
    // Resolve every BindingRef in the active config + ship bindings + macros to a
    // device index and open the device. Runs per slot during preload.
    static void ResolveActiveDevices();
    // Build, resolve, and snapshot every slot ([Profiles] in the custom file), then
    // make the base profile active. Runs at control-loop start and on hot-reload.
    static void PreloadProfiles();
    // Swap the active configuration to a preloaded slot: release held outputs and
    // macro keys, copy the slot's snapshot into the live globals, and consume any
    // physically-held button so it will not re-fire under its new meaning.
    static void ActivateProfile(int slot);
};

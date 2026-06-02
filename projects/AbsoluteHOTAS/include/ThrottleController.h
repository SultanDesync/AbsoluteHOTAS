#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include "BindingRef.h"

// DirectInput polling, normalization, and memory injection.
// Runs on a dedicated background thread at ~120Hz.
class ThrottleController {
public:
    // Configuration loaded from AbsoluteHOTAS.ini.
    struct Config {
        // [General]
        bool    enabled = true;

        // [Hardware] — Legacy fallback device (used when BindingRef has no device)
        int     vJoyDeviceId = 1;         // Legacy device index fallback (formerly vJoyDeviceId)
        std::string deviceName;           // Optional DirectInput instance/product name match

        // [InputDevices] — Legacy fallback device names for bindings without explicit device
        int     axisDeviceIndex = 0;
        std::string axisDeviceName;
        int     shipButtonDeviceIndex = 0;
        std::string shipButtonDeviceName;

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
        bool    alwaysOn = true;          // Auto-arm discovery when the standalone controller starts

        // [Normalization]
        long    detentCenter = 16384;   // Raw axis value at physical detent center
        long    detentDeadzone = 500;   // Deadzone around detent (raw units)
        bool    reverseEnabled = false; // Axis reverse is disabled for the beta; use keyboard S.
        bool    unipolarMode = true;    // If true, maps whole axis (min-max) to 0.0-1.0 range (linear)
        float   idlePlateau = 0.05f;    // Software deadzone at the bottom (0.0-1.0)
        float   reverseDeadzone = 0.05f;
        float   reverseActivationThreshold = 0.05f;
        bool    incrementalThrottleMode = false; // Use centering spring-loaded sticks as rate controllers
        float   throttleRampRate = 0.67f; // Ramping sensitivity (rate per second)
        bool    physicsAdherenceMode = false; // Release control on sharp turns to let engine physics limit speed
        float   physicsAdherenceDeflection = 0.15f; // Pitch/yaw deflection threshold (>15%)
        float   physicsAdherenceThrottleThreshold = 0.5f; // Throttle threshold (>50%)
        bool    incrementalKeyboardMode = false; // Emulate W/S keys dynamically with duty cycle modulation


        // [Injection]
        int     pollRateHz = 60;          // Polling frequency
        int     throttleBurstMs = 250;    // Throttle authority window after movement; 0 = one frame
        bool    rollEnabled = true;       // Roll shares the +0x58 writer with lateral strafe.
        bool    reverseAxisEnabled = true;

        bool    logThrottle = false;    // Log throttle values to file
        // [ShipButtons]
        bool    shipButtonsEnabled = true;

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
    };

    static bool Initialize();
    static void Start();
    static void Stop();
    static Config& GetConfig();

    // Signal the control loop to reload config from INI on its next iteration.
    // Thread-safe — can be called from any thread (e.g., ImGui render thread).
    static void ReloadConfig();

    // Returns the last normalized throttle value (-1.0 to 1.0)
    static float GetCurrentThrottle();

    // Ship action info for the binding wizard
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
    static std::atomic<bool> s_isStandingDown;
    static std::atomic<bool> s_configReloadRequested;
    static std::atomic<float> s_currentThrottle;
    static std::thread s_thread;

    static void LoadConfig();
    static void ControlLoop();
    static float NormalizeAxis(long rawValue, long axisMin, long axisMax);
};

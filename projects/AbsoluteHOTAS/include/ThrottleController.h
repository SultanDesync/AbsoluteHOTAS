#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <chrono>
#include <fstream>

// DirectInput polling, normalization, and memory injection.
// Runs on a dedicated background thread at ~120Hz.
class ThrottleController {
public:
    // Configuration loaded from AbsoluteHOTAS.ini.
    struct Config {
        // [General]
        bool    enabled = true;

        // [Hardware]
        int     vJoyDeviceId = 1;         // vJoy device number (1-16)
        std::string deviceName;           // Optional DirectInput instance/product name match
        int     axisDeviceIndex = 0;      // [InputDevices] fallback DirectInput enumeration index
        std::string axisDeviceName;       // [InputDevices] axis source device name match
        int     shipButtonDeviceIndex = 0;// [InputDevices] fallback DirectInput enumeration index
        std::string shipButtonDeviceName; // [InputDevices] ship button source device name match
        int     throttleAxisId = 0x32;    // HID usage: 0x32 = Z axis (default)
        int     pitchAxisId = 0x31;       // HID usage: 0x31 = Y axis
        int     yawAxisId = 0x30;         // HID usage: 0x30 = X axis
        int     rollAxisId = 0x33;        // HID usage: 0x33 = Rx axis
        int     strafeLatAxisId = 0x33;   // HID usage: 0x33 = Rx axis
        int     strafeVertAxisId = 0x34;  // HID usage: 0x34 = Ry axis

        float   fPitchSensitivity = 1.0f;
        float   fYawSensitivity = 1.0f;
        float   fRollSensitivity = 1.0f;
        float   fStrafeSensitivity = 1.0f;

        bool    bInvertPitch = true;
        bool    bInvertThrottle = false;
        bool    bInvertYaw = false;
        bool    bInvertRoll = false;
        bool    bInvertStrafeLat = false;
        bool    bInvertStrafeVert = false;

        int     activateButtonId = 69;    // 1-indexed vJoy button to activate hook
        int     stopButtonId = 70;        // 1-indexed vJoy button to stop hook
        int     boostButtonId = -1;       // Optional: Button to pause injection for boost
        bool    alwaysOn = false;         // Auto-arm discovery when the standalone controller starts

        // [Normalization]
        long    detentCenter = 16384;   // Raw axis value at physical detent center
        long    detentDeadzone = 500;   // Deadzone around detent (raw units)
        bool    reverseEnabled = false; // Axis reverse is disabled for the beta; use keyboard S.
        bool    unipolarMode = true;    // If true, maps whole axis (min-max) to 0.0-1.0 range (linear)
        float   idlePlateau = 0.05f;    // Software deadzone at the bottom (0.0-1.0)
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

        bool    logThrottle = false;    // Log throttle values to file
        int     scoutKey = 0x79;        // Virtual Key Code for Scout Mode (F10 default)

        // [ShipButtons]
        bool    shipButtonsEnabled = true;
    };

    static bool Initialize();
    static void Start();
    static void Stop();
    static Config& GetConfig();

    // Returns the last normalized throttle value (-1.0 to 1.0)
    static float GetCurrentThrottle();

private:
    static Config s_config;
    static std::atomic<bool> s_running;
    static std::atomic<bool> s_isStandingDown;
    static std::atomic<float> s_currentThrottle;
    static std::thread s_thread;

    static void LoadConfig();
    static void ControlLoop();
    static float NormalizeAxis(long rawValue, long axisMin, long axisMax);
};

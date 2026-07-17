#pragma once

#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string>
#include "BindingRef.h"

struct DeviceInfo {
    GUID        guidInstance;
    GUID        guidProduct;
    uint16_t    vid;              // USB Vendor ID
    uint16_t    pid;              // USB Product ID
    std::string instanceName;
    std::string productName;
    std::string vidpidString;     // "044F:B10A" format
    int         axisCount;
    int         buttonCount;
};

class DeviceManager {
public:
    static bool Initialize();     // Creates DI8, enumerates all devices
    static void Shutdown();
    static void Refresh();        // Re-enumerate (for hot-plug)

    // Query
    static int GetDeviceCount();
    static const DeviceInfo& GetDevice(int index);

    // Open/close
    static LPDIRECTINPUTDEVICE8 OpenDevice(int index);
    static void CloseDevice(int index);

    // Resolution: find best match given VID:PID, name, index (priority order)
    static int ResolveDevice(const std::string& vidpid,
                             const std::string& name,
                             int index);

    // Batch polling: poll all open devices once per tick, cache states
    static void PollAll();
    // Get the cached state from the last PollAll() call (returns nullptr if device not open/failed)
    static const DIJOYSTATE2* GetCachedState(int deviceIndex);

    // Convenience: resolve by name only (common case for BindingRef)
    static int ResolveByName(const std::string& name);

    // Logging
    static void LogDeviceManifest();  // "=== Attached HID Devices ==="
    
    // Open ALL enumerated devices for polling (used by BindingWizard)
    static void OpenAllDevices();

    // Extract an axis value from DIJOYSTATE2 by HID usage ID (0x30-0x37)
    static long GetAxisFromState(const DIJOYSTATE2* st, int usageId);

    // Read a raw axis value via BindingRef, returning 32768 (neutral) on failure.
    static inline long GetRawAxis(const BindingRef& ref) {
        if (ref.value > 0) {
            const DIJOYSTATE2* st = GetCachedState(ref.deviceIndex);
            if (st) return GetAxisFromState(st, ref.value);
        }
        return 32768;
    }

    // Check if a button (1-128 physical, 129-144 POV virtual) is pressed via BindingRef.
    static bool IsButtonPressed(const BindingRef& ref);
};

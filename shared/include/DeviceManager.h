#pragma once

#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string>
#include <vector>

struct DeviceInfo {
    GUID        guidInstance;
    GUID        guidProduct;
    uint16_t    vid;              // USB Vendor ID
    uint16_t    pid;              // USB Product ID
    std::string instanceName;
    std::string productName;
    std::string vidpidString;     // "044F:B10A" format
    int         enumIndex;
    int         axisCount;
    int         buttonCount;
    bool        isOpen;
};

class DeviceManager {
public:
    static bool Initialize();     // Creates DI8, enumerates all devices
    static void Shutdown();
    static void Refresh();        // Re-enumerate (for hot-plug)

    // Query
    static int GetDeviceCount();
    static const DeviceInfo& GetDevice(int index);
    static const std::vector<DeviceInfo>& GetAllDevices();

    // Open/close
    static LPDIRECTINPUTDEVICE8 OpenDevice(int index);
    static void CloseDevice(int index);

    // Resolution: find best match given VID:PID, name, index (priority order)
    static int ResolveDevice(const std::string& vidpid,
                             const std::string& name,
                             int index);

    // Polling
    static bool PollDevice(int index, DIJOYSTATE2& outState);

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
    
    // DirectInput Context
    static LPDIRECTINPUT8 GetDirectInputContext();
};

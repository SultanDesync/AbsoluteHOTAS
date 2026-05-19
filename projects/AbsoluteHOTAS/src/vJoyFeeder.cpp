#include "vJoyFeeder.h"
#include <windows.h>
#include <iostream>

// --- Runtime Loader for vJoyInterface.dll ---
typedef BOOL(__cdecl* PFN_vJoyEnabled)();
typedef VjdStat(__cdecl* PFN_GetVJDStatus)(UINT);
typedef BOOL(__cdecl* PFN_AcquireVJD)(UINT);
typedef VOID(__cdecl* PFN_RelinquishVJD)(UINT);
typedef BOOL(__cdecl* PFN_ResetVJD)(UINT);
typedef BOOL(__cdecl* PFN_SetAxis)(LONG, UINT, UINT);
typedef BOOL(__cdecl* PFN_SetBtn)(BOOL, UINT, UCHAR);

static PFN_vJoyEnabled pfnvJoyEnabled = nullptr;
static PFN_GetVJDStatus pfnGetVJDStatus = nullptr;
static PFN_AcquireVJD pfnAcquireVJD = nullptr;
static PFN_RelinquishVJD pfnRelinquishVJD = nullptr;
static PFN_ResetVJD pfnResetVJD = nullptr;
static PFN_SetAxis pfnSetAxis = nullptr;
static PFN_SetBtn pfnSetBtn = nullptr;
static HMODULE hvJoy = nullptr;

static void Log(const std::string& msg) {
    std::ofstream log("Data\\SFSE\\Plugins\\AbsoluteProbe.log", std::ios::app);
    if (log.is_open()) { log << "[vJoyFeeder] " << msg << std::endl; }
}

static bool LoadvJoy() {
    if (hvJoy) return true;
    Log("Attempting to load vJoyInterface.dll...");
    
    // 1. Try standard search path
    hvJoy = LoadLibraryA("vJoyInterface.dll");
    
    // 2. Try common installation paths
    if (!hvJoy) {
        const char* commonPaths[] = {
            "C:\\Program Files\\vJoy\\SDK\\lib\\amd64\\vJoyInterface.dll",
            "C:\\Program Files\\vJoy\\x64\\vJoyInterface.dll",
            "C:\\Program Files (x86)\\vJoy\\x64\\vJoyInterface.dll",
            "Data\\SFSE\\Plugins\\vJoyInterface.dll" // Local fallback
        };
        for (const char* path : commonPaths) {
            Log("Searching in: " + std::string(path));
            hvJoy = LoadLibraryA(path);
            if (hvJoy) break;
        }
    }

    if (!hvJoy) {
        Log("FAILED to load vJoyInterface.dll. Ensure vJoy is installed and x64 version is accessible.");
        return false;
    }

    pfnvJoyEnabled = (PFN_vJoyEnabled)GetProcAddress(hvJoy, "vJoyEnabled");
    pfnGetVJDStatus = (PFN_GetVJDStatus)GetProcAddress(hvJoy, "GetVJDStatus");
    pfnAcquireVJD = (PFN_AcquireVJD)GetProcAddress(hvJoy, "AcquireVJD");
    pfnRelinquishVJD = (PFN_RelinquishVJD)GetProcAddress(hvJoy, "RelinquishVJD");
    pfnResetVJD = (PFN_ResetVJD)GetProcAddress(hvJoy, "ResetVJD");
    pfnSetAxis = (PFN_SetAxis)GetProcAddress(hvJoy, "SetAxis");
    pfnSetBtn = (PFN_SetBtn)GetProcAddress(hvJoy, "SetBtn");

    if (!pfnvJoyEnabled || !pfnGetVJDStatus || !pfnAcquireVJD || !pfnRelinquishVJD || !pfnResetVJD || !pfnSetAxis || !pfnSetBtn) {
        Log("FAILED to find all required exports in vJoyInterface.dll.");
        return false;
    }

    return true;
}

vJoyFeeder::vJoyFeeder(UINT deviceId) : m_deviceId(deviceId), m_initialized(false) {}

vJoyFeeder::~vJoyFeeder() {
    if (m_initialized && pfnRelinquishVJD) {
        pfnRelinquishVJD(m_deviceId);
    }
}

bool vJoyFeeder::Initialize() {
    if (!LoadvJoy()) {
        return false;
    }

    if (!pfnvJoyEnabled()) {
        Log("vJoy is reported as DISABLED by the driver.");
        return false;
    }

    VjdStat status = pfnGetVJDStatus(m_deviceId);
    if (status == VJD_STAT_OWN) {
        Log("vJoy device " + std::to_string(m_deviceId) + " is already owned by this process.");
    } else if (status == VJD_STAT_FREE && !pfnAcquireVJD(m_deviceId)) {
        Log("FAILED to acquire vJoy device " + std::to_string(m_deviceId) + ". Is it enabled in vJoyConf?");
        return false;
    } else if (status == VJD_STAT_BUSY) {
        Log("vJoy device " + std::to_string(m_deviceId) + " is BUSY (Owned by another app). Close Joystick Gremlin/Feeder.");
        return false;
    } else if (status == VJD_STAT_MISS) {
        Log("vJoy device " + std::to_string(m_deviceId) + " is MISSING. Check vJoy Configuration.");
        return false;
    } else if (status != VJD_STAT_FREE && status != VJD_STAT_OWN) {
        Log("vJoy device " + std::to_string(m_deviceId) + " has unknown status: " + std::to_string((int)status));
        return false;
    }

    pfnResetVJD(m_deviceId);
    m_initialized = true;
    Log("vJoy device " + std::to_string(m_deviceId) + " successfully acquired and reset.");
    return true;
}

void vJoyFeeder::UpdateThrottleAxis(float throttlePercentage) {
    if (!m_initialized) return;

    // vJoy axes typically range from 1 to 32768
    long vJoyValue = static_cast<long>(1 + (throttlePercentage * 32767.0f));
    
#ifndef HID_USAGE_Z
#define HID_USAGE_Z ((UINT)0x32)
#endif
    pfnSetAxis(vJoyValue, m_deviceId, HID_USAGE_Z);
}

bool vJoyFeeder::UpdateAxis(long vJoyValue, UINT usage) {
    if (!m_initialized) return false;
    BOOL ok = pfnSetAxis(vJoyValue, m_deviceId, usage);
    if (!ok) {
        Log("!!! vJoy SetAxis FAILED for Device " + std::to_string(m_deviceId) +
            ", Usage " + std::to_string(usage) +
            ", Value " + std::to_string(vJoyValue));
    }
    return ok != FALSE;
}

bool vJoyFeeder::SetButton(bool pressed, UCHAR buttonId) {
    if (!m_initialized || !pfnSetBtn) return false;
    return pfnSetBtn(pressed, m_deviceId, buttonId);
}

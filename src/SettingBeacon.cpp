#include "PCH.h"

#include "SettingBeacon.h"
#include "RuntimePaths.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <format>

// ---- Bethesda Setting layout (from CommonLibSF RE::Setting, 0x20 bytes) ----
// +0x00: vtable ptr     (8 bytes)
// +0x08: value union    (8 bytes) — float at +0x08
// +0x10: defaultValue   (8 bytes)
// +0x18: const char* key (8 bytes) — pointer to name string in .rdata

static bool s_beaconActive = false;

static void BeaconLog(const std::string& msg) {
    RuntimePaths::Log("[SettingBeacon]", msg);
}

// SEH-isolated write helper (MSVC C2712 workaround)
static bool SafeWriteFloat(volatile float* addr, float value) {
    __try {
        *addr = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH-isolated read helper
static bool SafeReadFloat(volatile float* addr, float* out) {
    __try {
        *out = *addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Find a Bethesda Setting object by scanning .data for structs whose
// +0x18 (key pointer) matches a name string found in .rdata.
// Returns pointer to the Setting's value float (+0x08), or nullptr.
static volatile float* FindSettingValue(uintptr_t moduleBase,
                                         const char* settingName,
                                         uintptr_t dataStart, size_t dataSize,
                                         uintptr_t rdataStart, size_t rdataSize) {
    // Step 1: Find the name string in .rdata
    size_t nameLen = strlen(settingName);
    uintptr_t nameAddr = 0;

    for (size_t i = 0; i <= rdataSize - nameLen; i++) {
        if (memcmp((const void*)(rdataStart + i), settingName, nameLen + 1) == 0) {
            nameAddr = rdataStart + i;
            break;
        }
    }

    if (!nameAddr) return nullptr;

    // Step 2: Scan .data for a Setting struct whose +0x18 key ptr matches nameAddr.
    // Setting objects are 0x20-aligned in .data.
    for (size_t i = 0; i + 0x20 <= dataSize; i += 8) {
        uintptr_t* candidate = (uintptr_t*)(dataStart + i);

        // Check +0x18 (key pointer) — relative to the start of this candidate as +0x00
        // candidate[0] = vtable, candidate[1] = value, candidate[2] = defaultValue, candidate[3] = key
        if (candidate[3] == nameAddr) {
            // Verify vtable pointer is within module range (sanity check)
            uintptr_t vtable = candidate[0];
            if (vtable >= moduleBase && vtable < moduleBase + 0x10000000) {
                // Return pointer to the value union at +0x08
                return (volatile float*)&candidate[1];
            }
        }
    }

    return nullptr;
}

bool SettingBeacon::PlantBeacon() {
    HMODULE hModule = GetModuleHandleA(NULL);
    if (!hModule) {
        BeaconLog("ERROR: Could not get module handle.");
        return false;
    }
    uintptr_t moduleBase = (uintptr_t)hModule;

    // Parse PE headers to find .data and .rdata sections
    auto* dos = (IMAGE_DOS_HEADER*)moduleBase;
    auto* nt  = (IMAGE_NT_HEADERS*)(moduleBase + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);

    uintptr_t dataStart = 0, rdataStart = 0;
    size_t dataSize = 0, rdataSize = 0;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        char name[9] = {};
        memcpy(name, section[i].Name, 8);
        if (strcmp(name, ".data") == 0) {
            dataStart = moduleBase + section[i].VirtualAddress;
            dataSize  = section[i].Misc.VirtualSize;
        } else if (strcmp(name, ".rdata") == 0) {
            rdataStart = moduleBase + section[i].VirtualAddress;
            rdataSize  = section[i].Misc.VirtualSize;
        }
    }

    if (!dataStart || !rdataStart) {
        BeaconLog("ERROR: Could not locate .data or .rdata sections.");
        return false;
    }

    // ---- Plant the beacon: fThrottleAtEngineStart ----
    bool beaconOk = false;
    auto* throttleSetting = FindSettingValue(moduleBase,
        "fThrottleAtEngineStart:Spaceship", dataStart, dataSize, rdataStart, rdataSize);

    if (throttleSetting) {
        float oldVal = 0.0f;
        SafeReadFloat(throttleSetting, &oldVal);

        if (SafeWriteFloat(throttleSetting, 0.0314f)) {
            float verify = 0.0f;
            SafeReadFloat(throttleSetting, &verify);
            if (verify == 0.0314f) {
                BeaconLog(std::format("Beacon planted: fThrottleAtEngineStart {:.4f} -> 0.0314", oldVal));
                beaconOk = true;
                s_beaconActive = true;
            } else {
                BeaconLog("ERROR: Beacon verify failed.");
            }
        } else {
            BeaconLog("ERROR: Could not write beacon value.");
        }
    } else {
        BeaconLog("WARNING: Could not find fThrottleAtEngineStart Setting. Using StarfieldCustom.ini fallback.");
    }

    // ---- Zero game deadzones (our plugin handles its own) ----
    // Only fRollDeadzone:Spaceship is confirmed to exist. The engine may use
    // different names or mechanisms for yaw/pitch deadzones.
    auto* rollDz = FindSettingValue(moduleBase,
        "fRollDeadzone:Spaceship", dataStart, dataSize, rdataStart, rdataSize);
    if (rollDz) {
        float oldVal = 0.0f;
        SafeReadFloat(rollDz, &oldVal);
        if (SafeWriteFloat(rollDz, 0.0f)) {
            BeaconLog(std::format("Roll deadzone: {:.4f} -> 0.0 (plugin handles its own).", oldVal));
        }
    }

    return beaconOk;
}

bool SettingBeacon::IsActive() {
    return s_beaconActive;
}

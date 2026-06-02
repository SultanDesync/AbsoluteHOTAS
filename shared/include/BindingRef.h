#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <cstdlib>

// Represents a parsed INI binding reference.
// Format: "DeviceName@0x32" or "DeviceName@42" or just "0x32" / "42"
// The '@' delimiter separates an optional device name from the axis/button value.
struct BindingRef {
    std::string deviceName;   // Empty = use legacy default device from [InputDevices]
    int         value;        // Axis usage ID (0x30-0x37) or button ID (1-128), or -1
    int         deviceIndex;  // Resolved DeviceManager index, -1 = unresolved/default

    bool HasDevice() const { return !deviceName.empty(); }
    bool IsValid()   const { return value >= 0; }
};

// Parses an INI value into a BindingRef.
//   "S-TECS SPACE-L THROTTLE@0x32"  → { "S-TECS SPACE-L THROTTLE", 0x32, -1 }
//   "VKBsim Gladiator EVO R@1"      → { "VKBsim Gladiator EVO R", 1, -1 }
//   "0x32"                           → { "", 0x32, -1 }
//   "42"                             → { "", 42, -1 }
//   "-1"                             → { "", -1, -1 }
//   ""                               → { "", defaultValue, -1 }
inline BindingRef ParseBindingRef(const char* iniValue, int defaultValue) {
    BindingRef ref;
    ref.deviceIndex = -1;

    // Key missing entirely (nullptr) → use legacy default for backwards compat
    // Key present but empty ("") → user explicitly cleared it → unbound
    if (!iniValue) {
        ref.value = defaultValue;
        return ref;
    }
    if (iniValue[0] == '\0') {
        ref.value = -1;
        return ref;
    }

    std::string_view sv(iniValue);

    // Trim leading/trailing whitespace
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))  sv.remove_suffix(1);

    if (sv.empty()) {
        ref.value = -1;
        return ref;
    }

    // Find the last '@' — device names may contain spaces but not '@'
    auto atPos = sv.rfind('@');
    if (atPos != std::string_view::npos && atPos > 0 && atPos < sv.size() - 1) {
        // Everything before '@' is the device name
        std::string_view devPart = sv.substr(0, atPos);
        std::string_view valPart = sv.substr(atPos + 1);

        // Trim the device name
        while (!devPart.empty() && std::isspace(static_cast<unsigned char>(devPart.back())))  devPart.remove_suffix(1);
        while (!devPart.empty() && std::isspace(static_cast<unsigned char>(devPart.front()))) devPart.remove_prefix(1);

        ref.deviceName = std::string(devPart);

        // Trim the value part
        while (!valPart.empty() && std::isspace(static_cast<unsigned char>(valPart.front()))) valPart.remove_prefix(1);

        // Parse as hex (0x...) or decimal
        std::string valStr(valPart);
        char* endPtr = nullptr;
        long parsed = std::strtol(valStr.c_str(), &endPtr, 0); // base 0 auto-detects hex/dec
        ref.value = (endPtr != valStr.c_str()) ? static_cast<int>(parsed) : defaultValue;
    } else {
        // No '@' found — legacy format, just a number
        std::string valStr(sv);
        char* endPtr = nullptr;
        long parsed = std::strtol(valStr.c_str(), &endPtr, 0);
        ref.value = (endPtr != valStr.c_str()) ? static_cast<int>(parsed) : defaultValue;
    }

    return ref;
}

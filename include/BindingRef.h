#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>

// Represents a parsed INI binding reference.
// Format: "DeviceName@0x32" or "#0@0x32" or just "0x32" / "42"
// The '@' delimiter separates an optional device name (or #N index) from the axis/button value.
struct BindingRef {
    std::string deviceName;   // Empty = use legacy default device from [InputDevices]
    int         value;        // Axis usage ID (0x30-0x37), button/POV ID (1-144), or -1
    int         deviceIndex;  // Resolved DeviceManager index, -1 = unresolved/default

    bool HasDevice() const { return !deviceName.empty(); }
    bool HasIndex()  const { return deviceIndex >= 0; }
    bool IsValid()   const { return value >= 0; }
};

namespace BindingRefDetail {

inline void Trim(std::string_view& text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))  text.remove_suffix(1);
}

inline bool ParseInt(std::string_view text, int base, int& result) {
    Trim(text);
    if (text.empty()) return false;

    const std::string owned(text);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(owned.c_str(), &end, base);
    if (errno == ERANGE || end == owned.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

}  // namespace BindingRefDetail

// Parses an INI value into a BindingRef.
//   "S-TECS SPACE-L THROTTLE@0x32"  → { "S-TECS SPACE-L THROTTLE", 0x32, -1 }
//   "#0@0x32"                       → { "", 0x32, 0 }
//   "#1@5"                          → { "", 5, 1 }
//   "VKBsim Gladiator EVO R@1"      → { "VKBsim Gladiator EVO R", 1, -1 }
//   "0x32"                           → { "", 0x32, -1 }
//   "42"                             → { "", 42, -1 }
//   "-1"                             → { "", -1, -1 }
//   ""                               → { "", -1, -1 }  (explicitly cleared)
inline BindingRef ParseBindingRef(const char* iniValue, int defaultValue) {
    BindingRef ref{ "", -1, -1 };

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

    BindingRefDetail::Trim(sv);

    if (sv.empty()) {
        ref.value = -1;
        return ref;
    }

    // Check for #N@ device index prefix (e.g., "#0@0x30", "#1@5")
    if (sv.front() == '#') {
        const auto atPos = sv.find('@');
        int index = -1;
        int value = -1;
        if (atPos != std::string_view::npos &&
            BindingRefDetail::ParseInt(sv.substr(1, atPos - 1), 10, index) &&
            index >= 0 &&
            BindingRefDetail::ParseInt(sv.substr(atPos + 1), 0, value)) {
            ref.deviceIndex = index;
            ref.value = value;
        }
        return ref;
    }

    // Find the last '@' — device names may contain spaces but not '@'
    auto atPos = sv.rfind('@');
    if (atPos != std::string_view::npos && atPos > 0 && atPos < sv.size() - 1) {
        // Everything before '@' is the device name
        std::string_view devPart = sv.substr(0, atPos);
        std::string_view valPart = sv.substr(atPos + 1);

        // Trim the device name
        BindingRefDetail::Trim(devPart);

        // Trim the value part
        BindingRefDetail::Trim(valPart);

        // Parse as hex (0x...) or decimal
        int value = -1;
        if (!devPart.empty() && BindingRefDetail::ParseInt(valPart, 0, value)) {
            ref.deviceName = std::string(devPart);
            ref.value = value;
        }
    } else {
        // No '@' found — legacy format, just a number
        BindingRefDetail::ParseInt(sv, 0, ref.value);
    }

    return ref;
}

// Format a BindingRef as a human-readable display string.
// When hex=true, values are printed as 0x%02X (axis usage IDs).
// When hex=false, values are printed as decimal (button IDs).
inline std::string FormatBindingRef(const BindingRef& ref, bool hex) {
    if (!ref.IsValid() || ref.value <= 0) return "(unbound)";
    char buf[256];
    if (hex) {
        if (ref.HasIndex() && ref.deviceName.empty())
            std::snprintf(buf, sizeof(buf), "#%d@0x%02X", ref.deviceIndex, ref.value);
        else if (ref.deviceName.empty())
            std::snprintf(buf, sizeof(buf), "0x%02X", ref.value);
        else
            std::snprintf(buf, sizeof(buf), "%s@0x%02X", ref.deviceName.c_str(), ref.value);
    } else {
        if (ref.HasIndex() && ref.deviceName.empty())
            std::snprintf(buf, sizeof(buf), "#%d@%d", ref.deviceIndex, ref.value);
        else if (ref.deviceName.empty())
            std::snprintf(buf, sizeof(buf), "%d", ref.value);
        else
            std::snprintf(buf, sizeof(buf), "%s@%d", ref.deviceName.c_str(), ref.value);
    }
    return buf;
}

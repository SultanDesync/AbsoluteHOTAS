#include "PCH.h"

#include "WizardConfig.h"

#include "WizardCapture.h"

#include <cstdio>

namespace WizardConfig {


std::string FormatBindingDisplay(const std::string& binding) {
    if (binding == "(unbound)") return binding;
    if (binding.rfind("key:", 0) == 0) {
        bool ctrl = false, shift = false, alt = false;
        std::vector<std::string> keys;
        const char* cursor = binding.c_str() + 4;
        while (*cursor) {
            char* end = nullptr;
            const int vk = (int)std::strtol(cursor, &end, 0);
            if (end == cursor) break;
            if (vk == VK_CONTROL) ctrl = true;
            else if (vk == VK_SHIFT) shift = true;
            else if (vk == VK_MENU) alt = true;
            else {
                char keyName[64]{};
                const UINT scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
                if (scan && GetKeyNameTextA((LONG)(scan << 16), keyName,
                                            (int)std::size(keyName)) > 0)
                    keys.emplace_back(keyName);
                else {
                    char fallback[16]{};
                    std::snprintf(fallback, sizeof(fallback), "0x%02X", vk);
                    keys.emplace_back(fallback);
                }
            }
            cursor = end;
            while (*cursor == '+' || *cursor == ' ') ++cursor;
        }
        std::vector<std::string> parts;
        if (ctrl) parts.emplace_back("Ctrl");
        if (shift) parts.emplace_back("Shift");
        if (alt) parts.emplace_back("Alt");
        parts.insert(parts.end(), keys.begin(), keys.end());
        if (!parts.empty()) {
            std::string display;
            for (const auto& part : parts) {
                if (!display.empty()) display += "+";
                display += part;
            }
            return display;
        }
    }
    auto atPos = binding.rfind('@');
    std::string numPart = (atPos != std::string::npos) ? binding.substr(atPos + 1) : binding;
    char* endPtr = nullptr;
    long btnId = std::strtol(numPart.c_str(), &endPtr, 10);
    if (endPtr != numPart.c_str() && *endPtr == '\0' && btnId >= 129 && btnId <= 144) {
        int povIndex = (int)(btnId - 129) / 4;
        int direction = (int)(btnId - 129) % 4;
        char label[256];
        std::snprintf(label, sizeof(label), "%s (POV%d-%s)", binding.c_str(), povIndex + 1,
            WizardCapture::PovDirectionName(direction));
        return label;
    }
    return binding;
}

}  // namespace WizardConfig

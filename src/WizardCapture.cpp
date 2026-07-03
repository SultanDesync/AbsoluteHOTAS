#include "PCH.h"

#include "WizardCapture.h"
#include "WizardDefs.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"

#include <cstring>
#include <cmath>
#include <climits>
#include <cstdio>

static void WizLog(const std::string& msg) {
    RuntimePaths::Log("[BindingWizard]", msg);
}

namespace WizardCapture {

static PendingBind s_pendingBind;
static DeviceCalibState s_devCalib;

void DeviceCalibState::Reset() {
    active = false;
    deviceIndex = -1;
    for (int i = 0; i < 8; i++) {
        observedMin[i] = LONG_MAX;
        observedMax[i] = LONG_MIN;
    }
}

PendingBind& GetPendingBind() { return s_pendingBind; }
DeviceCalibState& GetCalibState() { return s_devCalib; }

const char* AxisName(int usageId) {
    switch (usageId) {
        case 0x30: return "X";
        case 0x31: return "Y";
        case 0x32: return "Z";
        case 0x33: return "Rx";
        case 0x34: return "Ry";
        case 0x35: return "Rz";
        case 0x36: return "Slider0";
        case 0x37: return "Slider1";
        default: return "?";
    }
}

bool IsPovDirectionActive(DWORD pov, int direction) {
    if (LOWORD(pov) == 0xFFFF) return false;
    static constexpr DWORD kDirAngles[4] = { 0, 9000, 18000, 27000 };
    DWORD target = kDirAngles[direction];
    DWORD diff = (pov > target) ? (pov - target) : (target - pov);
    if (diff > 18000) diff = 36000 - diff;
    return diff <= 4500;
}

const char* PovDirectionName(int direction) {
    static const char* names[4] = { "Up", "Right", "Down", "Left" };
    return (direction >= 0 && direction < 4) ? names[direction] : "?";
}

// --- Internal helpers ---

static void ResetDebounce() {
    s_pendingBind.debounceDeviceIndex = -1;
    s_pendingBind.debounceButtonIndex = -1;
    s_pendingBind.debounceButtonFrames = 0;
    s_pendingBind.debounceAxisDeviceIndex = -1;
    s_pendingBind.debounceAxisIndex = -1;
    s_pendingBind.debounceAxisFrames = 0;
}

static void TakeSnapshots() {
    s_pendingBind.snapshots.clear();
    int count = DeviceManager::GetDeviceCount();
    for (int d = 0; d < count; d++) {
        const auto* st = DeviceManager::GetCachedState(d);
        if (!st) continue;

        DeviceSnapshot snap;
        snap.deviceIndex = d;
        for (int a = 0; a < 8; a++) {
            snap.axes[a] = DeviceManager::GetAxisFromState(st, 0x30 + a);
        }
        memcpy(snap.buttons, st->rgbButtons, 128);
        memcpy(snap.povs, st->rgdwPOV, sizeof(snap.povs));
        s_pendingBind.snapshots.push_back(snap);
    }
}

static bool HasDuplicateName(int devIdx) {
    const auto& name = DeviceManager::GetDevice(devIdx).productName;
    for (int d = 0; d < DeviceManager::GetDeviceCount(); d++) {
        if (d != devIdx && DeviceManager::GetDevice(d).productName == name)
            return true;
    }
    return false;
}

static std::string FormatCapturedBinding(int deviceIndex, int value, bool hex) {
    const auto& info = DeviceManager::GetDevice(deviceIndex);
    char buf[256];
    if (HasDuplicateName(deviceIndex)) {
        if (hex) std::snprintf(buf, sizeof(buf), "#%d@0x%02X", deviceIndex, value);
        else     std::snprintf(buf, sizeof(buf), "#%d@%d", deviceIndex, value);
    } else {
        if (hex) std::snprintf(buf, sizeof(buf), "%s@0x%02X", info.productName.c_str(), value);
        else     std::snprintf(buf, sizeof(buf), "%s@%d", info.productName.c_str(), value);
    }
    return buf;
}

// --- Public API ---

void StartAxisCapture(int slotIndex, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = slotIndex;
    ResetDebounce();
    TakeSnapshots();
    WizLog("Axis capture started for: " + std::string(label));
}

void StartButtonCapture(int slotIndex, int categoryOffset, const char* label) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = categoryOffset + slotIndex;
    ResetDebounce();
    TakeSnapshots();
    WizLog("Button capture started for: " + std::string(label));
}

bool UpdateCapture(BindingCommitFn commitFn) {
    if (!s_pendingBind.active) return false;

    int slot = s_pendingBind.targetConfigSlot;

    constexpr int kButtonDebounceFrames = 8;
    constexpr int kAxisDebounceFrames = 5;

    if (CaptureSlot::IsAxis(slot)) {
        constexpr long kAxisThreshold = 8000;

        for (auto& snap : s_pendingBind.snapshots) {
            if (snap.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
            if (!st) continue;

            for (int a = 0; a < 8; a++) {
                long current = DeviceManager::GetAxisFromState(st, 0x30 + a);
                long delta = std::abs(current - snap.axes[a]);
                if (delta > kAxisThreshold) {
                    if (s_pendingBind.debounceAxisDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceAxisIndex == a) {
                        s_pendingBind.debounceAxisFrames++;
                    } else {
                        s_pendingBind.debounceAxisDeviceIndex = snap.deviceIndex;
                        s_pendingBind.debounceAxisIndex = a;
                        s_pendingBind.debounceAxisFrames = 1;
                    }

                    if (s_pendingBind.debounceAxisFrames >= kAxisDebounceFrames) {
                        int usageId = 0x30 + a;
                        std::string binding = FormatCapturedBinding(snap.deviceIndex, usageId, true);
                        commitFn(slot, binding.c_str());
                        WizLog("Axis captured: " + binding + " for " + s_pendingBind.targetLabel);
                        s_pendingBind.active = false;
                        return true;
                    }
                } else if (s_pendingBind.debounceAxisDeviceIndex == snap.deviceIndex &&
                           s_pendingBind.debounceAxisIndex == a) {
                    s_pendingBind.debounceAxisFrames = 0;
                }
            }
        }
    } else {
        // Button capture (all categories)
        for (auto& snap : s_pendingBind.snapshots) {
            if (snap.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
            if (!st) continue;

            auto CommitAndReturn = [&](const char* buf) -> bool {
                commitFn(slot, buf);
                WizLog("Button captured: " + std::string(buf) + " for " + s_pendingBind.targetLabel);
                s_pendingBind.active = false;
                return true;
            };

            // Physical buttons (1-128)
            for (int b = 0; b < 128; b++) {
                bool nowPressed = (st->rgbButtons[b] & 0x80) != 0;
                bool wasPressed = (snap.buttons[b] & 0x80) != 0;

                if (!nowPressed || wasPressed) {
                    if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceButtonIndex == b && !nowPressed) {
                        s_pendingBind.debounceButtonFrames = 0;
                    }
                    continue;
                }

                if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                    s_pendingBind.debounceButtonIndex == b) {
                    s_pendingBind.debounceButtonFrames++;
                } else {
                    s_pendingBind.debounceDeviceIndex = snap.deviceIndex;
                    s_pendingBind.debounceButtonIndex = b;
                    s_pendingBind.debounceButtonFrames = 1;
                }

                if (s_pendingBind.debounceButtonFrames >= kButtonDebounceFrames) {
                    std::string binding = FormatCapturedBinding(snap.deviceIndex, b + 1, false);
                    return CommitAndReturn(binding.c_str());
                }
            }

            // POV / HAT switches (virtual buttons 129-144)
            for (int p = 0; p < 4; p++) {
                for (int dir = 0; dir < 4; dir++) {
                    int virtualBtn = 129 + p * 4 + dir;
                    bool nowActive = IsPovDirectionActive(st->rgdwPOV[p], dir);
                    bool wasActive = IsPovDirectionActive(snap.povs[p], dir);

                    if (!nowActive || wasActive) {
                        if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                            s_pendingBind.debounceButtonIndex == virtualBtn && !nowActive) {
                            s_pendingBind.debounceButtonFrames = 0;
                        }
                        continue;
                    }

                    if (s_pendingBind.debounceDeviceIndex == snap.deviceIndex &&
                        s_pendingBind.debounceButtonIndex == virtualBtn) {
                        s_pendingBind.debounceButtonFrames++;
                    } else {
                        s_pendingBind.debounceDeviceIndex = snap.deviceIndex;
                        s_pendingBind.debounceButtonIndex = virtualBtn;
                        s_pendingBind.debounceButtonFrames = 1;
                    }

                    if (s_pendingBind.debounceButtonFrames >= kButtonDebounceFrames) {
                        std::string binding = FormatCapturedBinding(snap.deviceIndex, virtualBtn, false);
                        return CommitAndReturn(binding.c_str());
                    }
                }
            }
        }
    }

    return false;
}

} // namespace WizardCapture

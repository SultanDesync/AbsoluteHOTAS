#include "PCH.h"

#include "WizardCapture.h"
#include "WizardDefs.h"
#include "DeviceManager.h"
#include "RuntimePaths.h"

#include <cstring>
#include <cmath>
#include <climits>
#include <cstdio>
#include <chrono>

static constexpr int kBounceFrames = 2;             // a raw edge must persist this long
static constexpr unsigned long long kMaxCaptureMs = 8000;  // give up if nothing settles

static unsigned long long NowMs() {
    using namespace std::chrono;
    return (unsigned long long)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

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
    auto& pb = s_pendingBind;
    pb.debounceAxisDeviceIndex = -1;
    pb.debounceAxisIndex = -1;
    pb.debounceAxisFrames = 0;
    pb.targetDeviceIndex = -1;
    pb.targetValue = -1;
    pb.lastConfirmMs = 0;
    pb.candDeviceIndex = -1;
    pb.candValue = -1;
    pb.candFrames = 0;
}

// Refresh prevFrame (button/POV state) for next-frame edge detection.
static void UpdatePrevFrame() {
    for (auto& snap : s_pendingBind.prevFrame) {
        const auto* st = DeviceManager::GetCachedState(snap.deviceIndex);
        if (!st) continue;
        memcpy(snap.buttons, st->rgbButtons, 128);
        memcpy(snap.povs, st->rgdwPOV, sizeof(snap.povs));
    }
}

// Is a captured value (1..128 physical, 129..144 POV) currently held on its device?
static bool IsValueHeld(int deviceIndex, int value) {
    const auto* st = DeviceManager::GetCachedState(deviceIndex);
    if (!st) return false;
    if (value >= 1 && value <= 128) return (st->rgbButtons[value - 1] & 0x80) != 0;
    const int p = (value - 129) / 4, dir = (value - 129) % 4;
    return IsPovDirectionActive(st->rgdwPOV[p], dir);
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

void StartButtonCapture(int slotIndex, int categoryOffset, const char* label, int settleWindowMs) {
    s_pendingBind.active = true;
    s_pendingBind.targetLabel = label;
    s_pendingBind.targetConfigSlot = categoryOffset + slotIndex;
    ResetDebounce();
    TakeSnapshots();
    s_pendingBind.prevFrame = s_pendingBind.snapshots;  // frame-0 reference for edge detect
    s_pendingBind.settleWindowMs = settleWindowMs > 0 ? settleWindowMs : kButtonCaptureMs;
    s_pendingBind.captureStartMs = NowMs();
    WizLog("Button capture started for: " + std::string(label));
}

bool UpdateCapture(BindingCommitFn commitFn) {
    if (!s_pendingBind.active) return false;

    int slot = s_pendingBind.targetConfigSlot;

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
        // Button / POV capture — settle-to-quiescence. Each confirmed NEW press becomes
        // the target and resets a settle timer; when input goes quiet for settleWindowMs,
        // the LAST press wins. This binds the deep stage of a 2-stage trigger (the pull
        // ends on the last edge) and the final detent of a rotary you turn to, with one
        // rule. A short bounce guard stops a contact bounce from posing as a later press.
        // Already-held buttons produce no down-edge, so they are ignored until re-pressed
        // — for a selector already at the target, turn the switch to it. See the capture
        // design in docs/reference/profile-switching.md.
        auto& pb = s_pendingBind;
        const unsigned long long now = NowMs();

        // Find at most one NEW press edge this frame (down now, up last frame).
        int edgeDev = -1, edgeVal = -1;
        for (const auto& prev : pb.prevFrame) {
            if (edgeVal >= 0) break;
            if (prev.deviceIndex >= DeviceManager::GetDeviceCount()) continue;
            const auto* st = DeviceManager::GetCachedState(prev.deviceIndex);
            if (!st) continue;

            for (int b = 0; b < 128 && edgeVal < 0; b++) {
                if ((st->rgbButtons[b] & 0x80) && !(prev.buttons[b] & 0x80)) {
                    edgeDev = prev.deviceIndex; edgeVal = b + 1;
                }
            }
            for (int p = 0; p < 4 && edgeVal < 0; p++) {
                for (int dir = 0; dir < 4 && edgeVal < 0; dir++) {
                    if (IsPovDirectionActive(st->rgdwPOV[p], dir) &&
                        !IsPovDirectionActive(prev.povs[p], dir)) {
                        edgeDev = prev.deviceIndex; edgeVal = 129 + p * 4 + dir;
                    }
                }
            }
        }

        // Bounce guard: hold a raw edge as a candidate until it persists kBounceFrames,
        // then confirm it as the (new) target and reset the settle timer.
        if (edgeVal >= 0) {
            pb.candDeviceIndex = edgeDev; pb.candValue = edgeVal; pb.candFrames = 1;
        } else if (pb.candValue >= 0) {
            if (IsValueHeld(pb.candDeviceIndex, pb.candValue)) {
                if (++pb.candFrames >= kBounceFrames) {
                    pb.targetDeviceIndex = pb.candDeviceIndex;
                    pb.targetValue       = pb.candValue;
                    pb.lastConfirmMs     = now;
                    pb.candDeviceIndex = -1; pb.candValue = -1; pb.candFrames = 0;
                }
            } else {
                pb.candDeviceIndex = -1; pb.candValue = -1; pb.candFrames = 0;  // bounce
            }
        }

        UpdatePrevFrame();

        const bool haveTarget = pb.targetValue >= 0;
        const bool settled = haveTarget && pb.candValue < 0 &&
                             (now - pb.lastConfirmMs) >= (unsigned long long)pb.settleWindowMs;
        const bool timedOut = (now - pb.captureStartMs) >= kMaxCaptureMs;

        if (settled || (timedOut && haveTarget)) {
            std::string binding = FormatCapturedBinding(pb.targetDeviceIndex, pb.targetValue, false);
            commitFn(slot, binding.c_str());
            WizLog("Button captured: " + binding + " for " + pb.targetLabel);
            pb.active = false;
            return true;
        }
        if (timedOut) {  // nothing pressed the whole time — give up silently
            WizLog("Button capture timed out for " + pb.targetLabel);
            pb.active = false;
            return false;
        }
    }

    return false;
}

} // namespace WizardCapture

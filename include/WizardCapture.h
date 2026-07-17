#pragma once

// Input capture subsystem for the BindingWizard.
// Handles device polling, debounce, snapshot comparison, and binding assignment.

#include <string>
#include <vector>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

namespace WizardCapture {

struct DeviceSnapshot {
    int deviceIndex;
    long axes[8];
    BYTE buttons[128];
    DWORD povs[4];
};

// Button-capture settle windows (ms). A press starts a timer; the LAST press to
// land before the timer runs out wins. Tight for buttons/triggers (snappy, and two
// edges this close must be one 2-stage pull); generous for a rotary/selector, where
// the window must bridge the gap between detents as you turn to your position. See
// the capture design in docs/reference/profile-switching.md.
inline constexpr int kButtonCaptureMs   = 50;
inline constexpr int kSelectorCaptureMs = 300;

struct PendingBind {
    bool active = false;
    std::string targetLabel;
    int targetConfigSlot = -1;
    std::vector<DeviceSnapshot> snapshots;   // initial state (axis-capture reference)
    std::vector<DeviceSnapshot> prevFrame;   // previous frame (button/POV edge detection)

    // Axis debounce
    int debounceAxisDeviceIndex = -1;
    int debounceAxisIndex = -1;
    int debounceAxisFrames = 0;

    // Button/POV settle-to-quiescence capture. Each confirmed new press becomes the
    // target and resets lastConfirmMs; when no new press lands for settleWindowMs, the
    // current target commits.
    int           settleWindowMs = kButtonCaptureMs;
    int           targetDeviceIndex = -1;   // best binding so far; -1 = none yet
    int           targetValue = -1;         // 1..128 physical, 129..144 POV
    unsigned long long lastConfirmMs = 0;   // when target was last (re)confirmed
    unsigned long long captureStartMs = 0;
    // Bounce guard: a raw edge must persist kBounceFrames before it confirms.
    int           candDeviceIndex = -1;
    int           candValue = -1;
    int           candFrames = 0;
};

// Full-device calibration state — tracks all 8 axes simultaneously
struct DeviceCalibState {
    bool  active = false;
    int   deviceIndex = -1;
    long  observedMin[8];
    long  observedMax[8];
    void Reset();
};

PendingBind& GetPendingBind();
DeviceCalibState& GetCalibState();
void CancelCapture();

void StartAxisCapture(int slotIndex, const char* label);
// settleWindowMs selects the capture window; pass kSelectorCaptureMs when binding a
// rotary/selector position, or leave the default for buttons/triggers.
void StartButtonCapture(int slotIndex, int categoryOffset, const char* label,
                        int settleWindowMs = kButtonCaptureMs);

// Call once per frame to poll devices and detect completed captures.
// Writes the result string into the appropriate binding array via the callback.
// Returns true if a capture was completed this frame.
using BindingCommitFn = void(*)(int captureSlot, const char* bindingString);
bool UpdateCapture(BindingCommitFn commitFn);

// Display formatting helpers
const char* PovDirectionName(int direction);
const char* AxisName(int usageId);

} // namespace WizardCapture

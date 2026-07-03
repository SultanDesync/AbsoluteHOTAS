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

struct PendingBind {
    bool active = false;
    std::string targetLabel;
    int targetConfigSlot = -1;
    std::vector<DeviceSnapshot> snapshots;

    // Debounce state
    int debounceDeviceIndex = -1;
    int debounceButtonIndex = -1;
    int debounceButtonFrames = 0;
    int debounceAxisDeviceIndex = -1;
    int debounceAxisIndex = -1;
    int debounceAxisFrames = 0;
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

void StartAxisCapture(int slotIndex, const char* label);
void StartButtonCapture(int slotIndex, int categoryOffset, const char* label);

// Call once per frame to poll devices and detect completed captures.
// Writes the result string into the appropriate binding array via the callback.
// Returns true if a capture was completed this frame.
using BindingCommitFn = void(*)(int captureSlot, const char* bindingString);
bool UpdateCapture(BindingCommitFn commitFn);

// POV helpers (shared with display formatting)
bool IsPovDirectionActive(DWORD pov, int direction);
const char* PovDirectionName(int direction);
const char* AxisName(int usageId);

} // namespace WizardCapture

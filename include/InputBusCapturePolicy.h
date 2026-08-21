#pragma once

#include <algorithm>

namespace InputBusCapturePolicy {

struct AxisDebounce {
    int deviceIndex{-1};
    int axisIndex{-1};
    int frames{};
};

inline bool UpdateAxis(AxisDebounce& state, int deviceIndex, int axisIndex,
                       long movement, long threshold,
                       int requiredFrames) noexcept
{
    if (deviceIndex < 0 || axisIndex < 0 || movement <= threshold) {
        state = {};
        return false;
    }
    if (state.deviceIndex == deviceIndex && state.axisIndex == axisIndex) {
        ++state.frames;
    } else {
        state.deviceIndex = deviceIndex;
        state.axisIndex = axisIndex;
        state.frames = 1;
    }
    return state.frames >= std::max(1, requiredFrames);
}

struct DigitalDebounce {
    int deviceIndex{-1};
    int channel{-1};
    int frames{};
};

struct DigitalConfirmation {
    bool confirmed{};
    int deviceIndex{-1};
    int channel{-1};
};

// A new edge supersedes the existing candidate. With no new edge, the current
// candidate must remain held for the requested frame count before confirmation.
inline DigitalConfirmation UpdateDigital(DigitalDebounce& state,
                                          int edgeDeviceIndex,
                                          int edgeChannel,
                                          bool candidateHeld,
                                          int requiredFrames) noexcept
{
    if (edgeDeviceIndex >= 0 && edgeChannel >= 0) {
        state.deviceIndex = edgeDeviceIndex;
        state.channel = edgeChannel;
        state.frames = 1;
        return {};
    }
    if (state.deviceIndex < 0 || state.channel < 0) return {};
    if (!candidateHeld) {
        state = {};
        return {};
    }
    if (++state.frames < std::max(1, requiredFrames)) return {};
    const DigitalConfirmation result{true, state.deviceIndex, state.channel};
    state = {};
    return result;
}

} // namespace InputBusCapturePolicy

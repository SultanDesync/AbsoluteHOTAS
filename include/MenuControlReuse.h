#pragma once

#include <cmath>

// Pure neutral-arming and hysteresis policy for optional menu-control reuse.
// Keeping it independent of DirectInput/SendInput makes the safety behavior
// executable in a standalone regression test.
namespace MenuControlReuse {

struct AxisState {
    bool contextActive = false;
    bool neutralArmed = false;
    int direction = 0; // -1, 0, +1
};

struct ButtonState {
    bool contextActive = false;
    bool releasedArmed = false;
};

constexpr int UpdateAxis(AxisState& state, bool contextActive, bool enabled,
                         bool inputValid, float value, float engageThreshold,
                         float releaseThreshold) noexcept
{
    if (!contextActive || !enabled || !inputValid) {
        state = {};
        return 0;
    }

    if (!state.contextActive) {
        state.contextActive = true;
        state.neutralArmed = false;
        state.direction = 0;
    }

    const float magnitude = std::abs(value);
    if (!state.neutralArmed) {
        if (magnitude <= releaseThreshold) state.neutralArmed = true;
        return 0;
    }

    if (state.direction == 0) {
        if (value <= -engageThreshold) state.direction = -1;
        else if (value >= engageThreshold) state.direction = 1;
    } else if (magnitude <= releaseThreshold) {
        state.direction = 0;
    } else if (state.direction < 0 && value >= engageThreshold) {
        state.direction = 1;
    } else if (state.direction > 0 && value <= -engageThreshold) {
        state.direction = -1;
    }
    return state.direction;
}

constexpr bool UpdateButton(ButtonState& state, bool contextActive, bool enabled,
                            bool inputValid, bool pressed) noexcept
{
    if (!contextActive || !enabled || !inputValid) {
        state = {};
        return false;
    }

    if (!state.contextActive) {
        state.contextActive = true;
        state.releasedArmed = false;
    }
    if (!state.releasedArmed) {
        if (!pressed) state.releasedArmed = true;
        return false;
    }
    return pressed;
}

} // namespace MenuControlReuse

#pragma once

#include "AbsoluteInputBusAPI.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace InputBusState {

using DigitalArray =
    std::array<bool, AbsoluteInputBusApi::kDigitalControlCount>;

struct EdgeState {
    bool initialized{};
    bool connected{};
    DigitalArray previousDown{};
    std::array<std::uint32_t, AbsoluteInputBusApi::kDigitalControlCount> pressCount{};
    std::array<std::uint32_t, AbsoluteInputBusApi::kDigitalControlCount> releaseCount{};
};

constexpr bool PovDirectionActive(std::int32_t rawPov, std::size_t direction) noexcept
{
    if (direction >= 4 || (static_cast<std::uint32_t>(rawPov) & 0xFFFFU) == 0xFFFFU)
        return false;
    constexpr std::array<std::uint32_t, 4> angles{0, 9000, 18000, 27000};
    const auto pov = static_cast<std::uint32_t>(rawPov);
    const auto target = angles[direction];
    auto difference = pov > target ? pov - target : target - pov;
    if (difference > 18000) difference = 36000 - difference;
    return difference <= 4500;
}

inline void UpdateEdges(EdgeState& state, bool connected,
                        const DigitalArray& currentDown) noexcept
{
    // First observation and device reconnection establish a baseline. Controls
    // already held at either boundary must not synthesize new presses.
    if (!state.initialized || (!state.connected && connected)) {
        state.initialized = true;
        state.connected = connected;
        state.previousDown = connected ? currentDown : DigitalArray{};
        return;
    }

    const DigitalArray effective = connected ? currentDown : DigitalArray{};
    for (std::size_t index = 0; index < effective.size(); ++index) {
        if (effective[index] && !state.previousDown[index]) {
            ++state.pressCount[index];
        } else if (!effective[index] && state.previousDown[index]) {
            ++state.releaseCount[index];
        }
    }
    state.previousDown = effective;
    state.connected = connected;
}

inline bool Down(const std::uint64_t* words, std::size_t channel) noexcept
{
    return words && channel < AbsoluteInputBusApi::kDigitalControlCount &&
        (words[channel / 64] & (1ULL << (channel % 64))) != 0;
}

} // namespace InputBusState

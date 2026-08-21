#pragma once

#include <cstdint>

// ============================================================================
// PilotState — live cockpit/FPS context derived from the selected flight
// handler's output cadence. The handler pointer itself can remain cached after
// getting up, so Auto uses timestamp freshness and reports menus/loading as a
// separate suspended/unknown state rather than falsely declaring OnFoot.
// ============================================================================

namespace PilotState {

enum class State : std::uint8_t {
    Piloting,
    OnFoot,
    Suspended,
};

struct Observation {
    std::int64_t selectedOutputAgeMilliseconds = -1;
    bool gameplayContextKnown = false;
    bool gameplayContextActive = false;
    bool targetingModeActive = false;
};

struct Snapshot {
    State state = State::Suspended;
    std::int64_t selectedOutputAgeMilliseconds = -1;
    bool headTrackingAllowed = false;
    bool gameplayContextKnown = false;
    bool gameplayContextActive = false;
    bool targetingModeActive = false;
};

// Pure policy functions kept in the header so the latch and conservative camera
// gate can be exercised without loading Starfield or installing hooks.
constexpr State EvaluateAutomatic(const Observation& observation,
                                  std::int64_t pilotLatchMilliseconds) noexcept
{
    if (observation.gameplayContextKnown && !observation.gameplayContextActive)
        return State::Suspended;
    if (observation.targetingModeActive)
        return State::Piloting;
    if (observation.selectedOutputAgeMilliseconds >= 0 &&
        observation.selectedOutputAgeMilliseconds <= pilotLatchMilliseconds)
        return State::Piloting;
    if (!observation.gameplayContextKnown)
        return State::Suspended;
    return State::OnFoot;
}

constexpr bool EvaluateHeadTracking(const Observation& observation,
                                    std::int64_t freshnessMilliseconds = 400) noexcept
{
    if (observation.gameplayContextKnown && !observation.gameplayContextActive)
        return false;
    return observation.selectedOutputAgeMilliseconds >= 0 &&
        observation.selectedOutputAgeMilliseconds <= freshnessMilliseconds;
}

// Flip the legacy manual state (bound to the optional test toggle key).
void Toggle();

// Refresh both gates. Auto uses the selected-handler timestamp; Manual retains
// the legacy toggle for diagnostics while head tracking remains freshness-gated.
Snapshot Update(bool autoSource, int pilotLatchMilliseconds);

}  // namespace PilotState

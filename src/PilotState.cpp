#include "PCH.h"
#include "PilotState.h"
#include "NativeShipControl.h"
#include "RuntimePaths.h"
#include "ThrottleHook.h"

// ============================================================================
// PilotState — the selected handler's output cadence is the validated positive
// cockpit signal. It runs continuously while piloting and stops as the get-up
// transition begins, even though the handler and flight cluster remain cached.
//
// Earlier candidates remain useful only as context or documented dead ends:
//
//   1. GetSpaceshipPilot (old Address Library ID 173834) mis-resolved after the
//      1.16.x update and is unsafe to call as a high-frequency gate.
//   2. First-person PlayerCamera state is shared by cockpit and on-foot views.
//   3. Source-object +0x1B4 is not a pilot flag, but it does distinguish active
//      gameplay from menus/loading. Auto uses it only to report Suspended so a
//      paused or loading interval is never classified as an on-foot transition.
// ============================================================================

namespace PilotState {
namespace {

std::atomic<bool> s_manualPiloting{ true };
std::atomic<State> s_lastAutomaticState{ State::Suspended };

std::optional<bool> ReadGameplayContext()
{
    const auto source = ThrottleHook::GetSourceBasePtr();
    if (source < 0x10000) return std::nullopt;
    __try {
        return *reinterpret_cast<volatile std::uint8_t*>(source + 0x1B4) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return std::nullopt;
    }
}

const char* StateName(State state)
{
    switch (state) {
    case State::Piloting: return "Piloting";
    case State::OnFoot: return "OnFoot";
    case State::Suspended: return "Suspended";
    }
    return "Unknown";
}

} // namespace

void Toggle()
{
    s_manualPiloting.store(
        !s_manualPiloting.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

Snapshot Update(bool autoSource, int pilotLatchMilliseconds)
{
    Observation observation;
    observation.selectedOutputAgeMilliseconds =
        NativeShipControl::SelectedHandlerOutputAgeMilliseconds();
    observation.targetingModeActive = NativeShipControl::TargetingModeActive();
    if (const auto gameplay = ReadGameplayContext()) {
        observation.gameplayContextKnown = true;
        observation.gameplayContextActive = *gameplay;
    }

    Snapshot snapshot;
    snapshot.selectedOutputAgeMilliseconds =
        observation.selectedOutputAgeMilliseconds;
    snapshot.headTrackingAllowed = EvaluateHeadTracking(observation);
    snapshot.gameplayContextKnown = observation.gameplayContextKnown;
    snapshot.gameplayContextActive = observation.gameplayContextActive;
    snapshot.targetingModeActive = observation.targetingModeActive;
    if (!autoSource) {
        snapshot.state = s_manualPiloting.load(std::memory_order_relaxed)
            ? State::Piloting : State::OnFoot;
        return snapshot;
    }

    snapshot.state = EvaluateAutomatic(
        observation, std::clamp(pilotLatchMilliseconds, 500, 30000));
    const auto previous = s_lastAutomaticState.exchange(
        snapshot.state, std::memory_order_acq_rel);
    if (previous != snapshot.state) {
        RuntimePaths::Log("[PilotState]", std::format(
            "Auto context {} -> {} (selected-output age={} ms, gameplay={}, targeting={}).",
            StateName(previous), StateName(snapshot.state),
            observation.selectedOutputAgeMilliseconds,
            !observation.gameplayContextKnown ? "unknown" :
                (observation.gameplayContextActive ? "active" : "suspended"),
            observation.targetingModeActive ? "active" : "inactive"));
    }
    return snapshot;
}

}  // namespace PilotState

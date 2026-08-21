#pragma once

#include "AbsoluteInputBusAPI.h"
#include "PilotState.h"

namespace InputBusContextPolicy {

inline AbsoluteInputBusApi::RuntimeContextV1 Build(
    const PilotState::Snapshot& snapshot,
    bool automaticPilotSource) noexcept
{
    AbsoluteInputBusApi::RuntimeContextV1 result;
    switch (snapshot.state) {
    case PilotState::State::Piloting:
        result.context = AbsoluteInputBusApi::RuntimeContext::Piloting;
        break;
    case PilotState::State::OnFoot:
        result.context = AbsoluteInputBusApi::RuntimeContext::OnFoot;
        break;
    case PilotState::State::Suspended:
        result.context = AbsoluteInputBusApi::RuntimeContext::Suspended;
        break;
    }
    if (snapshot.state != PilotState::State::Suspended) {
        result.validSignals |= AbsoluteInputBusApi::kContextSignalIsPilot;
        if (snapshot.state == PilotState::State::Piloting) {
            result.activeSignals |= AbsoluteInputBusApi::kContextSignalIsPilot;
        }
    }
    if (snapshot.gameplayContextKnown) {
        result.validSignals |=
            AbsoluteInputBusApi::kContextSignalGameplayActive;
        if (snapshot.gameplayContextActive) {
            result.activeSignals |=
                AbsoluteInputBusApi::kContextSignalGameplayActive;
        }
    }
    result.validSignals |= AbsoluteInputBusApi::kContextSignalTargetingMode;
    if (snapshot.targetingModeActive) {
        result.activeSignals |= AbsoluteInputBusApi::kContextSignalTargetingMode;
    }
    if (automaticPilotSource) {
        result.sourceFlags |=
            AbsoluteInputBusApi::kContextSourceAutomaticPilot;
    }
    result.selectedOutputAgeMilliseconds =
        snapshot.selectedOutputAgeMilliseconds;
    return result;
}

} // namespace InputBusContextPolicy

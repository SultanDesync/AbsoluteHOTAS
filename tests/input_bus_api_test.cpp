#include "AbsoluteInputBusAPI.h"
#include "InputBusCapturePolicy.h"
#include "InputBusContextPolicy.h"
#include "InputBusState.h"

#include <cassert>
#include <cstddef>

int main()
{
    using namespace AbsoluteInputBusApi;

    static_assert(sizeof(void*) == 8);
    static_assert(sizeof(DeviceInfoV1) == 360);
    static_assert(sizeof(DeviceSnapshotV1) == 1352);
    static_assert(sizeof(BindingV1) == 464);
    static_assert(sizeof(CaptureRequestV1) == 80);
    static_assert(sizeof(CaptureResultV1) == 672);
    static_assert(sizeof(ProfileStateV1) == 112);
    static_assert(sizeof(RuntimeContextV1) == 56);
    static_assert(offsetof(ApiV1, capabilities) == 32);
    static_assert(offsetof(ApiV1, getDeviceCount) == 40);
    static_assert(offsetof(ApiV1, getRuntimeContext) == 72);
    static_assert(offsetof(ApiV1, cancelCapture) == 96);
    static_assert(sizeof(ApiV1) == 104);

    // First observation and reconnect are baselines, not synthetic presses.
    InputBusState::EdgeState edges;
    InputBusState::DigitalArray down{};
    down[5] = true;
    InputBusState::UpdateEdges(edges, true, down);
    assert(edges.pressCount[5] == 0);
    down[5] = false;
    InputBusState::UpdateEdges(edges, true, down);
    assert(edges.releaseCount[5] == 1);
    down[5] = true;
    InputBusState::UpdateEdges(edges, true, down);
    assert(edges.pressCount[5] == 1);
    InputBusState::UpdateEdges(edges, false, down);
    assert(edges.releaseCount[5] == 2);
    InputBusState::UpdateEdges(edges, true, down);
    assert(edges.pressCount[5] == 1);

    assert(InputBusState::PovDirectionActive(0, 0));
    assert(InputBusState::PovDirectionActive(4500, 0));
    assert(InputBusState::PovDirectionActive(4500, 1));
    assert(!InputBusState::PovDirectionActive(-1, 0));
    std::uint64_t words[kDigitalWordCount]{};
    words[2] = 1ULL << 15; // channel 143
    assert(InputBusState::Down(words, 143));
    assert(!InputBusState::Down(words, 142));

    InputBusCapturePolicy::AxisDebounce axis;
    assert(!InputBusCapturePolicy::UpdateAxis(axis, 0, 2, 8000, 8000, 5));
    for (int frame = 0; frame < 5; ++frame) {
        const bool captured = InputBusCapturePolicy::UpdateAxis(
            axis, 0, 2, 8001, 8000, 5);
        assert(captured == (frame == 4));
    }

    InputBusCapturePolicy::DigitalDebounce digital;
    auto confirmation = InputBusCapturePolicy::UpdateDigital(
        digital, 1, 17, false, 2);
    assert(!confirmation.confirmed);
    confirmation = InputBusCapturePolicy::UpdateDigital(
        digital, -1, -1, true, 2);
    assert(confirmation.confirmed);
    assert(confirmation.deviceIndex == 1 && confirmation.channel == 17);
    InputBusCapturePolicy::UpdateDigital(digital, 0, 3, false, 2);
    confirmation = InputBusCapturePolicy::UpdateDigital(
        digital, -1, -1, false, 2);
    assert(!confirmation.confirmed && digital.channel == -1);

    PilotState::Snapshot piloting;
    piloting.state = PilotState::State::Piloting;
    piloting.selectedOutputAgeMilliseconds = 12;
    piloting.gameplayContextKnown = true;
    piloting.gameplayContextActive = true;
    piloting.targetingModeActive = false;
    const auto live = InputBusContextPolicy::Build(piloting, true);
    assert(live.context == RuntimeContext::Piloting);
    assert((live.validSignals & kContextSignalIsPilot) != 0);
    assert((live.activeSignals & kContextSignalIsPilot) != 0);
    assert((live.sourceFlags & kContextSourceAutomaticPilot) != 0);
    assert(live.selectedOutputAgeMilliseconds == 12);

    PilotState::Snapshot suspended;
    suspended.state = PilotState::State::Suspended;
    const auto unknown = InputBusContextPolicy::Build(suspended, true);
    assert(unknown.context == RuntimeContext::Suspended);
    assert((unknown.validSignals & kContextSignalIsPilot) == 0);
    assert((unknown.activeSignals & kContextSignalIsPilot) == 0);

    return 0;
}

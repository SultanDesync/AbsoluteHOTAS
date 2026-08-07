#include "PilotState.h"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void CheckAutomaticContext()
{
    std::printf("AutomaticContext\n");
    using PilotState::Observation;
    using PilotState::State;

    CHECK(PilotState::EvaluateAutomatic({ 8, true, true }, 5000) == State::Piloting);
    CHECK(PilotState::EvaluateAutomatic({ 4999, true, true }, 5000) == State::Piloting);
    CHECK(PilotState::EvaluateAutomatic({ 5001, true, true }, 5000) == State::OnFoot);
    CHECK(PilotState::EvaluateAutomatic({ -1, true, true }, 5000) == State::OnFoot);

    // Menu/loading wins over a recently fresh hook and remains distinct from FPS.
    CHECK(PilotState::EvaluateAutomatic({ 8, true, false }, 5000) == State::Suspended);
    CHECK(PilotState::EvaluateAutomatic({ -1, false, false }, 5000) == State::Suspended);

    // A fresh selected-handler hit is decisive even before the auxiliary gameplay
    // flag becomes available.
    CHECK(PilotState::EvaluateAutomatic({ 8, false, false }, 5000) == State::Piloting);
}

static void CheckHeadTrackingGate()
{
    std::printf("HeadTrackingGate\n");
    CHECK(PilotState::EvaluateHeadTracking({ 399, true, true }));
    CHECK(PilotState::EvaluateHeadTracking({ 400, true, true }));
    CHECK(!PilotState::EvaluateHeadTracking({ 401, true, true }));
    CHECK(!PilotState::EvaluateHeadTracking({ -1, true, true }));
    CHECK(!PilotState::EvaluateHeadTracking({ 8, true, false }));
    CHECK(PilotState::EvaluateHeadTracking({ 8, false, false }));
}

int main()
{
    CheckAutomaticContext();
    CheckHeadTrackingGate();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
